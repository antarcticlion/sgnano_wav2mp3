// SGX のオーバーレイ（SDK の部品、2026-11-06）。決まりは include/sgx_overlay.h。
// 読み込みの段取り: 表の 1 行を読む -> 大きさを確かめる -> 像をスロットへ読む -> 像の頭を確かめる -> 再配置の表を 64 語ずつ読んで
// 語にスロットの番地を足す -> .bss を 0 に -> sync（DSB / ISB）。途中で失敗したらスロットは空（-1）にする（半端な像を呼ばせない）
#include "sgx_overlay.h"

#include <string.h>

#define RELOC_CHUNK 64u

static int rd(sgx_ovl_t *o, uint32_t off, void *dst, uint32_t len) {
    if (!len) return 0;
    const int e = o->io.read(o->io.user, off, dst, len);
    if (e) return e < 0 ? e : -SGX_OVL_EIO;
    o->st.bytes_read += len;
    return 0;
}
static void sync_code(sgx_ovl_t *o, void *p, uint32_t n) {
    if (o->io.sync) o->io.sync(o->io.user, p, n);
#if defined(__arm__)
    __asm volatile ("dsb\n isb" ::: "memory");      // 書いたコードを取り込み直す（前に取り込んだ命令を捨てる）
#endif
}

int sgx_ovl_open(sgx_ovl_t *o, const sgx_ovl_io_t *io, const void *host) {
    if (!o || !io || !io->read) return -SGX_OVL_EINVAL;
    memset(o, 0, sizeof *o);
    o->io = *io;
    o->host = host;
    sgx_ovl_pack_header_t h;
    const int e = rd(o, 0, &h, sizeof h);
    if (e) return e;
    if (h.magic != SGX_OVL_PACK_MAGIC) return -SGX_OVL_EINVAL;
    if (h.version != SGX_OVL_VERSION) return -SGX_OVL_ENOTSUP;
    if (h.count == 0 || h.count > 65535u || h.table_off < sizeof h || (h.table_off & 3u)) return -SGX_OVL_EINVAL;
    o->count = h.count;
    o->table_off = h.table_off;
    return 0;
}

int sgx_ovl_add_slot(sgx_ovl_t *o, void *base, uint32_t size) {
    if (!o || !base || size < sizeof(sgx_ovl_module_header_t) || ((uintptr_t)base & 7u)) return -SGX_OVL_EINVAL;
    if (o->nslots >= SGX_OVL_MAX_SLOTS) return -SGX_OVL_ENOMEM;
    sgx_ovl_slot_t *s = &o->slot[o->nslots];
    s->base = (uint8_t *)base; s->size = size; s->module = -1; s->pin = 0; s->gen = 0;
    return (int)o->nslots++;
}

int sgx_ovl_count(const sgx_ovl_t *o) { return o ? (int)o->count : 0; }

int sgx_ovl_info(sgx_ovl_t *o, int module, sgx_ovl_entry_t *out) {
    if (!o || !out || module < 0 || (uint32_t)module >= o->count) return -SGX_OVL_EINVAL;
    return rd(o, o->table_off + (uint32_t)module * sizeof *out, out, sizeof *out);
}

int sgx_ovl_find(sgx_ovl_t *o, const char *name) {
    if (!o || !name) return -SGX_OVL_EINVAL;
    for (uint32_t i = 0; i < o->count; i++) {
        sgx_ovl_entry_t e;
        const int r = sgx_ovl_info(o, (int)i, &e);
        if (r) return r;
        if (!strncmp(e.name, name, SGX_OVL_NAME_MAX)) return (int)i;
    }
    return -SGX_OVL_EINVAL;
}

static sgx_ovl_slot_t *slot_of(sgx_ovl_t *o, int slot) {
    return (o && slot >= 0 && (unsigned)slot < o->nslots) ? &o->slot[slot] : NULL;
}

static int fail(sgx_ovl_t *o, sgx_ovl_slot_t *s, int e) {
    s->module = -1;                                 // 半端な像は呼ばせない
    o->st.failures++;
    return e;
}

int sgx_ovl_load(sgx_ovl_t *o, int slot, int module) {
    sgx_ovl_slot_t *s = slot_of(o, slot);
    if (!s || module < 0 || (uint32_t)module >= o->count) return -SGX_OVL_EINVAL;
    if (s->module == module) { o->st.hits++; return 0; }
    if (s->pin) return -SGX_OVL_EBUSY;
    sgx_ovl_entry_t e;
    int r = sgx_ovl_info(o, module, &e);
    if (r) return fail(o, s, r);
    if (e.image_size < sizeof(sgx_ovl_module_header_t) || (e.image_size & 3u) || (e.off & 3u)) return fail(o, s, -SGX_OVL_EINVAL);
    if ((uint64_t)e.image_size + e.bss_size > s->size) return fail(o, s, -SGX_OVL_ENOMEM);
    s->module = -1;
    s->gen++;                                       // ここから前の iface は無効
    r = rd(o, e.off, s->base, e.image_size);
    if (r) return fail(o, s, r);
    sgx_ovl_module_header_t h;
    memcpy(&h, s->base, sizeof h);
    if (h.magic != SGX_OVL_MOD_MAGIC) return fail(o, s, -SGX_OVL_EINVAL);
    if (h.version != SGX_OVL_VERSION) return fail(o, s, -SGX_OVL_ENOTSUP);
    if (h.image_size != e.image_size || h.bss_size != e.bss_size) return fail(o, s, -SGX_OVL_EINVAL);
    // 再配置: 表は像の後ろ。語は 4 の倍数の位置で、像の中
    const uint32_t base = (uint32_t)(uintptr_t)s->base;
    uint32_t rel[RELOC_CHUNK];
    for (uint32_t done = 0; done < e.reloc_count;) {
        const uint32_t n = e.reloc_count - done < RELOC_CHUNK ? e.reloc_count - done : RELOC_CHUNK;
        r = rd(o, e.off + e.image_size + done * 4u, rel, n * 4u);
        if (r) return fail(o, s, r);
        for (uint32_t k = 0; k < n; k++) {
            const uint32_t at = rel[k];
            if ((at & 3u) || at > e.image_size - 4u) return fail(o, s, -SGX_OVL_EINVAL);
            uint32_t w;
            memcpy(&w, s->base + at, 4);
            w += base;
            memcpy(s->base + at, &w, 4);
        }
        done += n;
        o->st.relocs += n;
    }
    // 入口を確かめる（再配置で番地が直ったあと: スロットの中の Thumb の番地）
    uint32_t init;
    memcpy(&init, s->base + offsetof(sgx_ovl_module_header_t, init), 4);
    if (!(init & 1u) || init - 1u < base + sizeof h || init - 1u >= base + e.image_size) return fail(o, s, -SGX_OVL_EINVAL);
    memset(s->base + e.image_size, 0, e.bss_size);
    sync_code(o, s->base, e.image_size + e.bss_size);
    s->module = module;
    o->st.loads++;
    return 0;
}

int sgx_ovl_bind(sgx_ovl_t *o, int slot, int module, void *iface, uint32_t iface_size) {
    int r = sgx_ovl_load(o, slot, module);
    if (r) return r;
    sgx_ovl_slot_t *s = &o->slot[slot];
    const sgx_ovl_module_header_t *h = (const sgx_ovl_module_header_t *)(const void *)s->base;
    const sgx_ovl_ctx_t ctx = { SGX_OVL_VERSION, module, slot, s->base, s->size, o->host };
    return h->init(&ctx, iface, iface_size);
}

int sgx_ovl_unload(sgx_ovl_t *o, int slot) {
    sgx_ovl_slot_t *s = slot_of(o, slot);
    if (!s) return -SGX_OVL_EINVAL;
    if (s->pin) return -SGX_OVL_EBUSY;
    if (s->module >= 0) s->gen++;
    s->module = -1;
    return 0;
}

int sgx_ovl_loaded(const sgx_ovl_t *o, int slot) {
    return (o && slot >= 0 && (unsigned)slot < o->nslots) ? o->slot[slot].module : -1;
}
int sgx_ovl_pin(sgx_ovl_t *o, int slot) {
    sgx_ovl_slot_t *s = slot_of(o, slot);
    if (!s || s->module < 0 || s->pin == 0xFFFFu) return -SGX_OVL_EINVAL;
    s->pin++;
    return 0;
}
int sgx_ovl_unpin(sgx_ovl_t *o, int slot) {
    sgx_ovl_slot_t *s = slot_of(o, slot);
    if (!s || !s->pin) return -SGX_OVL_EINVAL;
    s->pin--;
    return 0;
}
uint32_t sgx_ovl_gen(const sgx_ovl_t *o, int slot) {
    return (o && slot >= 0 && (unsigned)slot < o->nslots) ? o->slot[slot].gen : 0u;
}
