// SGX の libc の口（2026-10-24、docs/sgx_abi.md）。newlib の libc（文字列・数学など、OS に頼らないもの）はそのまま使い、
// OS に頼るものだけ API へ向ける: printf の仲間（ファームの書式化）・malloc の仲間（PSRAM の置き場に先頭適合）・exit・abort。
// ファイルの stdio（fopen など）は無い（API の read_file / ra_* を使う。Doom は自分の口で包む）
#include "sgx_api.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

extern const sgx_api_t *sgx;

// ---- 書式化 ----
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) { return sgx->vsnprintf(buf, n, fmt, ap); }
int snprintf(char *buf, size_t n, const char *fmt, ...) { va_list ap; va_start(ap, fmt); const int r = vsnprintf(buf, n, fmt, ap); va_end(ap); return r; }
int vsprintf(char *buf, const char *fmt, va_list ap) { return vsnprintf(buf, 0x7FFFFFFF, fmt, ap); }
int sprintf(char *buf, const char *fmt, ...) { va_list ap; va_start(ap, fmt); const int r = vsprintf(buf, fmt, ap); va_end(ap); return r; }
int vprintf(const char *fmt, va_list ap) {
    char b[256];
    const int r = vsnprintf(b, sizeof b, fmt, ap);
    sgx->log(b, r < 0 ? 0u : (unsigned)r < sizeof b ? (unsigned)r : (unsigned)sizeof b - 1u);
    return r;
}
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); const int r = vprintf(fmt, ap); va_end(ap); return r; }
int puts(const char *s) { sgx->log(s, (unsigned)strlen(s)); sgx->log("\n", 1); return 1; }
int putchar(int c) { const char ch = (char)c; sgx->log(&ch, 1); return c; }
// newlib の再入版（2026-11-06）。C++ の標準ライブラリなどがこちらを呼ぶ。置いておかないと newlib の同じ部品の snprintf まで引き込まれてぶつかる
struct _reent;
int _vsnprintf_r(struct _reent *r, char *buf, size_t n, const char *fmt, va_list ap) { (void)r; return vsnprintf(buf, n, fmt, ap); }
int _snprintf_r(struct _reent *r, char *buf, size_t n, const char *fmt, ...) { (void)r; va_list ap; va_start(ap, fmt); const int k = vsnprintf(buf, n, fmt, ap); va_end(ap); return k; }
int _vsprintf_r(struct _reent *r, char *buf, const char *fmt, va_list ap) { (void)r; return vsprintf(buf, fmt, ap); }
int _sprintf_r(struct _reent *r, char *buf, const char *fmt, ...) { (void)r; va_list ap; va_start(ap, fmt); const int k = vsprintf(buf, fmt, ap); va_end(ap); return k; }
// 整数だけの別名（strftime などが呼ぶ。中身は同じ）
int vsniprintf(char *buf, size_t n, const char *fmt, va_list ap) { return vsnprintf(buf, n, fmt, ap); }
int sniprintf(char *buf, size_t n, const char *fmt, ...) { va_list ap; va_start(ap, fmt); const int k = vsnprintf(buf, n, fmt, ap); va_end(ap); return k; }
int siprintf(char *buf, const char *fmt, ...) { va_list ap; va_start(ap, fmt); const int k = vsprintf(buf, fmt, ap); va_end(ap); return k; }
int vsiprintf(char *buf, const char *fmt, va_list ap) { return vsprintf(buf, fmt, ap); }
int _sniprintf_r(struct _reent *r, char *buf, size_t n, const char *fmt, ...) { (void)r; va_list ap; va_start(ap, fmt); const int k = vsnprintf(buf, n, fmt, ap); va_end(ap); return k; }
int _vsniprintf_r(struct _reent *r, char *buf, size_t n, const char *fmt, va_list ap) { (void)r; return vsnprintf(buf, n, fmt, ap); }

// ---- malloc（PSRAM の置き場。64KB から始め、空きが 32KB を切ったら 64KB 増やし、
//      空きが 128KB になったら空いた 64KB の塊を返す。スクリプト/アプリは sgx_heap_reserve() で
//      もっと大きな最小値を指定できる）----
#include "sgnano_heap.h"
#define SGX_HEAP_MAX_CHUNKS 48u
typedef struct blk { uint32_t size; uint32_t used; } blk_t;   // size は頭も入れた大きさ（8 の倍数）
typedef struct { uint8_t *mem; uint32_t size, used_blocks; bool fixed; } heap_chunk_t;   // fixed = sgx_heap_add_region で渡された置き場（返さない）
static heap_chunk_t chunks[SGX_HEAP_MAX_CHUNKS];
static uint32_t heap_total, heap_free_bytes, heap_peak, heap_in_use;
static uint32_t heap_min_bytes = SGNANO_HEAP_MIN;
uint32_t sgx_heap_bytes;
// 【2026-11-06】malloc / free / realloc は SGX のミューテックスで守る。POV-Ray のように同じ優先度のタスクが時間で切り替わると、
// 守らなければヒープの鎖が壊れる。ミューテックスは最初の malloc で作る（その時点でタスクが 1 つだけなら取り合わない）。
// 作れなければ守らずに続ける（タスクを作らないプログラムなら困らない）
static sgx_mutex_t *heap_mx;
static bool heap_mx_tried;
static void heap_lock(void) {
    if (!heap_mx && !heap_mx_tried) { heap_mx_tried = true; heap_mx = sgx->mutex_new(); }
    if (heap_mx) (void)sgx->mutex_lock(heap_mx, SGX_WAIT_FOREVER);
}
static void heap_unlock(void) { if (heap_mx) sgx->mutex_unlock(heap_mx); }

// 空きの数え直し（全部の塊をたどる）。ふだんは malloc / free が heap_free_bytes を差分で直す（2026-11-06: 毎回たどっていて、
// 余りのページ 1.8MB を足した POV-Ray では malloc が core1 の時間の 2 割を使っていた）。塊を返したときだけ呼ぶ
static void heap_stats(void) {
    uint32_t fr = 0;
    for (unsigned c = 0; c < SGX_HEAP_MAX_CHUNKS; c++) {
        if (!chunks[c].mem) continue;
        uint8_t *p = chunks[c].mem;
        const uint8_t *end = p + chunks[c].size;
        while (p < end) {
            const blk_t *b = (const blk_t *)p;
            if (b->size < sizeof(blk_t) || b->size > (uint32_t)(end - p)) break;
            if (!b->used) fr += b->size - sizeof(blk_t);
            p += b->size;
        }
    }
    heap_free_bytes = fr;
    sgx_heap_bytes = heap_total;
}
static bool heap_add(uint32_t bytes) {
    bytes = (bytes + SGNANO_HEAP_CHUNK - 1u) & ~(SGNANO_HEAP_CHUNK - 1u);
    if (bytes < SGNANO_HEAP_CHUNK) bytes = SGNANO_HEAP_CHUNK;
    if (bytes > SGNANO_HEAP_MAX || heap_total > SGNANO_HEAP_MAX - bytes) return false;
    for (unsigned c = 0; c < SGX_HEAP_MAX_CHUNKS; c++) {
        if (chunks[c].mem) continue;
        uint8_t *mem = sgx->psram_alloc(bytes, 8);
        if (!mem) return false;
        ((blk_t *)mem)->size = bytes; ((blk_t *)mem)->used = 0;
        chunks[c].mem = mem; chunks[c].size = bytes; chunks[c].used_blocks = 0; chunks[c].fixed = false;
        heap_total += bytes;
        heap_free_bytes += bytes - (uint32_t)sizeof(blk_t); sgx_heap_bytes = heap_total;
        return true;
    }
    return false;
}
static bool heap_up(void) {
    if (heap_total) return true;
    while (heap_total < heap_min_bytes)
        if (!heap_add(SGNANO_HEAP_CHUNK)) return false;
    return true;
}
static uint32_t heap_reserve_unlocked(uint32_t bytes);
uint32_t sgx_heap_reserve(uint32_t bytes) { heap_lock(); const uint32_t r = heap_reserve_unlocked(bytes); heap_unlock(); return r; }
static uint32_t heap_reserve_unlocked(uint32_t bytes) {
    if (bytes < SGNANO_HEAP_MIN) bytes = SGNANO_HEAP_MIN;
    if (bytes > SGNANO_HEAP_MAX) bytes = SGNANO_HEAP_MAX;
    if (bytes > heap_min_bytes) heap_min_bytes = bytes;
    if (!heap_up()) return heap_total;
    while (heap_total < heap_min_bytes)
        if (!heap_add(SGNANO_HEAP_CHUNK)) break;
    return heap_total < heap_min_bytes ? heap_total : heap_min_bytes;
}
static void heap_shrink(void) {
    while (heap_total > heap_min_bytes && heap_total - heap_in_use >= SGNANO_HEAP_HIGH) {
        int pick = -1;
        for (unsigned c = 0; c < SGX_HEAP_MAX_CHUNKS; c++) {
            if (!chunks[c].mem || chunks[c].used_blocks || chunks[c].fixed) continue;
            if (heap_total - chunks[c].size < heap_min_bytes) continue;
            if (pick < 0) pick = (int)c;
            if (chunks[c].size == SGNANO_HEAP_CHUNK) { pick = (int)c; break; }
        }
        if (pick < 0) break;
        sgx->psram_free(chunks[pick].mem);
        heap_total -= chunks[pick].size;
        chunks[pick].mem = NULL; chunks[pick].size = 0;
        heap_stats();
    }
}
// 前もって伸ばす（空きが SGNANO_HEAP_LOW を切ったとき）のに失敗したら、0.5 秒は前もって伸ばさない（2026-11-06）。PSRAM が尽きると
// malloc のたびにファームの psram_alloc が断って UART に知らせを書き（1 回 約 1ms）、POV-Ray の受け手のタスクが 1 秒に 9 ブロックまで遅くなった。
// 入る空きが本当に無いときは今までどおり毎回伸ばしてみる（足りないのに NULL を返さない）
static uint32_t heap_grow_fail_ms;
static bool heap_grow_ahead(void) {
    if (heap_grow_fail_ms && sgx->ms() - heap_grow_fail_ms < 500u) return false;
    if (heap_add(SGNANO_HEAP_CHUNK)) { heap_grow_fail_ms = 0; return true; }
    heap_grow_fail_ms = sgx->ms() | 1u;
    return false;
}
static void *malloc_unlocked(size_t n) {
    if (!heap_up() || n > 0x7FFFFFF0u) { errno = ENOMEM; return NULL; }
    const uint32_t need = ((uint32_t)n + sizeof(blk_t) + 7u) & ~7u;
    while (heap_free_bytes < SGNANO_HEAP_LOW)
        if (!heap_grow_ahead()) break;
    for (;;) {
        for (unsigned c = 0; c < SGX_HEAP_MAX_CHUNKS; c++) {
            if (!chunks[c].mem) continue;
            uint8_t *p = chunks[c].mem;
            const uint8_t *end = p + chunks[c].size;
            while (p < end) {
                blk_t *b = (blk_t *)p;
                if (b->size < sizeof(blk_t) || b->size > (uint32_t)(end - p)) break;
                if (b->used) { p += b->size; continue; }
                while (p + b->size < end && !((blk_t *)(p + b->size))->used) { b->size += ((blk_t *)(p + b->size))->size; heap_free_bytes += (uint32_t)sizeof(blk_t); }   // うしろの空きとつなぐ
                if (b->size < need) { p += b->size; continue; }
                if (b->size - need >= 32u) { blk_t *r = (blk_t *)(p + need); r->size = b->size - need; r->used = 0; b->size = need; heap_free_bytes -= need; }
                else heap_free_bytes -= b->size - (uint32_t)sizeof(blk_t);
                b->used = 1;
                chunks[c].used_blocks++;
                heap_in_use += b->size;
                if (heap_in_use > heap_peak) heap_peak = heap_in_use;
                if (heap_free_bytes < SGNANO_HEAP_LOW) (void)heap_grow_ahead();
                return p + sizeof(blk_t);
            }
        }
        const uint32_t add = need > SGNANO_HEAP_CHUNK ? need : SGNANO_HEAP_CHUNK;
        if (!heap_add(add)) { errno = ENOMEM; return NULL; }
    }
}
static void free_unlocked(void *q) {
    if (!q) return;
    blk_t *b = (blk_t *)((uint8_t *)q - sizeof(blk_t));
    for (unsigned c = 0; c < SGX_HEAP_MAX_CHUNKS; c++) {
        if (!chunks[c].mem) continue;
        uint8_t *mem = chunks[c].mem;
        const uint8_t *end = mem + chunks[c].size;
        if ((uint8_t *)b < mem || (uint8_t *)b >= end) continue;
        const uint32_t sz = b->size;
        b->used = 0;
        if (chunks[c].used_blocks) chunks[c].used_blocks--;
        heap_in_use -= sz;
        heap_free_bytes += sz - (uint32_t)sizeof(blk_t);
        heap_shrink();
        return;
    }
}
// 【2026-11-06】memset は volatile な関数ポインタ越しに呼ぶ。GCC は「malloc のあとの memset(0)」を calloc に置き換えるので、
// そのまま書くと calloc が自分を末尾で呼び続けて止まらなかった（sgnano_povray が実機で見つけた。-fno-builtin-calloc では止まらない）
// 切り分け用（2026-11-06）: sgx_heap_trace_min を 0 より大きくすると、その大きさ以上の malloc を呼び出しの道筋つきでログへ出す
// （C++ のプログラムなら巻き戻しの表から番地を拾う。番地は像の頭からの位置で出すので、nm の値と比べられる）
uint32_t sgx_heap_trace_min;
#include <unwind.h>
extern char __sgx_image_size[];
struct heap_bt { uintptr_t pc[8]; unsigned n; };
static _Unwind_Reason_Code heap_bt_cb(struct _Unwind_Context *c, void *a) {
    struct heap_bt *b = a;
    if (b->n >= 8u) return _URC_END_OF_STACK;
    b->pc[b->n++] = (uintptr_t)_Unwind_GetIP(c);
    return _URC_NO_REASON;
}
extern const sgx_header_t sgx_header;
static void heap_trace(size_t n, void *p) {
    struct heap_bt b; b.n = 0;
    (void)_Unwind_Backtrace(heap_bt_cb, &b);
    const uintptr_t base = (uintptr_t)&sgx_header;
    char line[200]; int k = snprintf(line, sizeof line, "[heap] malloc %lu -> %p in use %lu:", (unsigned long)n, p, (unsigned long)heap_in_use);
    for (unsigned i = 0; i < b.n && k > 0 && k < (int)sizeof line - 12; i++) k += snprintf(line + k, sizeof line - (size_t)k, " %lx", (unsigned long)(b.pc[i] - base));
    if (k > 0 && k < (int)sizeof line - 1) { line[k++] = '\n'; sgx->log(line, (unsigned)k); }
}
// ---- 小さい確保の速い道（2026-11-07）----
// 頭も入れて 512 バイトまでは、16 バイトごとの大きさの組（32 組）に分け、返されたものを組ごとの鎖に積んで次に使う。
// 新しいものは 16KB の塊（下の malloc_unlocked から取る）を頭から切り出す。塊は返さない（組の鎖に戻るだけ）。
// それより大きいものは今までの道（塊の頭から探す）。POV-Ray は小さな確保が非常に多く（ホストで 1 枚に 270 万回）、
// 余りのページ 1.8MB をヒープに足すと頭から探す道が数千の塊をたどり、core1 の時間の 7 割が malloc だった
#define SM_STEP   16u
#define SM_MAX    512u
#define SM_SLAB   (16u * 1024u)
#define SM_USED   3u                    // blk_t.used: 小さい道で使っている（今までの道は 1 / 0）
#define SM_FREE   5u                    // 小さい道で返されて鎖にいる
static blk_t *sm_list[SM_MAX / SM_STEP];
static uint8_t *sm_cur;
static uint32_t sm_left;
uint32_t sgx_heap_small_live, sgx_heap_small_cached, sgx_heap_small_slabs;   // 切り分け用: 小さい道で使っている・鎖にいるバイト数、取った塊の数
static void *small_alloc(uint32_t need) {
    const unsigned c = need / SM_STEP - 1u;
    blk_t *b = sm_list[c];
    if (b) { sm_list[c] = *(blk_t **)(void *)(b + 1); b->used = SM_USED; sgx_heap_small_live += need; sgx_heap_small_cached -= need; return b + 1; }
    if (sm_left < need) {
        uint8_t *slab = malloc_unlocked(SM_SLAB - sizeof(blk_t));
        if (!slab) return NULL;
        sm_cur = slab; sm_left = (SM_SLAB - (uint32_t)sizeof(blk_t)) & ~(SM_STEP - 1u); sgx_heap_small_slabs++;
    }
    b = (blk_t *)(void *)sm_cur; sm_cur += need; sm_left -= need;
    b->size = need; b->used = SM_USED; sgx_heap_small_live += need;
    return b + 1;
}
void *malloc(size_t n) {
    const uint32_t need = n <= SM_MAX ? (((uint32_t)n + (uint32_t)sizeof(blk_t) + SM_STEP - 1u) & ~(SM_STEP - 1u)) : 0u;
    heap_lock(); void *p = (need && need <= SM_MAX) ? small_alloc(need) : malloc_unlocked(n); heap_unlock();
    if (sgx_heap_trace_min && n >= sgx_heap_trace_min) heap_trace(n, p);
    return p;
}
void free(void *q) {
    if (!q) return;
    blk_t *b = (blk_t *)((uint8_t *)q - sizeof(blk_t));
    heap_lock();
    if (b->used == SM_USED) {                     // 小さい道: 組の鎖に積む
        const unsigned c = b->size / SM_STEP - 1u;
        b->used = SM_FREE; sgx_heap_small_live -= b->size; sgx_heap_small_cached += b->size;
        *(blk_t **)(void *)(b + 1) = sm_list[c];
        sm_list[c] = b;
    } else free_unlocked(q);
    heap_unlock();
}
// 置き場をヒープに足す（2026-11-06）。fb_lend_spare で借りた FB のページなど、プログラムの終わりまで持つものに使う。
// 返さないので、終わる前に元へ返す（fb_return など）ときはその前に使い終えること。8 バイトに揃えて使う
int sgx_heap_add_region(void *p, uint32_t bytes) {
    uint8_t *m = (uint8_t *)(((uintptr_t)p + 7u) & ~(uintptr_t)7u);
    if (!p || bytes < (uint32_t)(m - (uint8_t *)p) + 256u) return -1;
    bytes = (bytes - (uint32_t)(m - (uint8_t *)p)) & ~7u;
    heap_lock();
    int rc = -1;
    for (unsigned c = 0; c < SGX_HEAP_MAX_CHUNKS; c++) {
        if (chunks[c].mem) continue;
        ((blk_t *)m)->size = bytes; ((blk_t *)m)->used = 0;
        chunks[c].mem = m; chunks[c].size = bytes; chunks[c].used_blocks = 0; chunks[c].fixed = true;
        heap_total += bytes;
        heap_free_bytes += bytes - (uint32_t)sizeof(blk_t); sgx_heap_bytes = heap_total;
        rc = 0;
        break;
    }
    heap_unlock();
    return rc;
}
static void *(*volatile const calloc_memset)(void *, int, size_t) = memset;
void *calloc(size_t a, size_t b) {
    if (a && b > SIZE_MAX / a) { errno = ENOMEM; return NULL; }
    void *p = malloc(a * b); if (p) calloc_memset(p, 0, a * b); return p;
}
void *realloc(void *q, size_t n) {
    if (!q) return malloc(n);
    if (!n) { free(q); return NULL; }
    const blk_t *b = (const blk_t *)((uint8_t *)q - sizeof(blk_t));
    const size_t old = b->size - sizeof(blk_t);
    if (old >= n) return q;
    void *p = malloc(n);
    if (p) { memcpy(p, q, old); free(q); }
    return p;
}
uint32_t sgx_heap_peak(void) { return heap_peak; }
uint32_t sgx_heap_total(void) { return heap_total; }
uint32_t sgx_heap_free_bytes(void) { return heap_free_bytes; }
uint32_t sgx_heap_min_bytes(void) { return heap_min_bytes; }

// ---- 終わり ----
void exit(int code) { sgx->exit(code); for (;;) {} }
void _exit(int code) { sgx->exit(code); for (;;) {} }
void abort(void) { sgx->log("abort\n", 6); sgx->exit(134); for (;;) {} }
void __assert_func(const char *file, int line, const char *fn, const char *expr) {
    printf("assertion failed: %s (%s:%d %s)\n", expr, file, line, fn ? fn : "");
    sgx->exit(134);
    for (;;) {}
}
// newlib の中身が頼む口
void *_sbrk(ptrdiff_t n) { (void)n; errno = ENOMEM; return (void *)-1; }

// ---- ファイル（2026-11-06、ABI 4.1 の SDK。fopen / fread / fwrite / fseek / fclose・open / read / write / lseek など C の口）----
// fd 0〜2 は端末（1・2 はこれまでどおりログへ。0 は読むと終わり）。3 から: 読むだけは ra_open / ra_read で必要な所だけ読む
// （大きいファイルも）。書く（O_WRONLY / O_RDWR）ものはヒープに丸ごと持ち、close で write_file して書き戻す（O_APPEND・O_RDWR は
// 初めに read_file で中身を読む）。パスはほかの API と同じ（"A:/dir/name" かプログラムのフォルダからの相対）
#define SGX_FD_MAX 16
typedef struct {
    bool used, wr, dirty;
    sgx_file_t *ra;                 // 読むだけ
    uint8_t *buf; uint32_t len, cap;   // 書くもの（ヒープ）
    uint32_t pos;
    char path[200];
} sgx_fd_t;
static sgx_fd_t fds[SGX_FD_MAX];
static int set_errno(int e) {        // SGX_E* -> errno
    static const int map[] = { 0, ENOENT, EEXIST, EACCES, ENOTEMPTY, ENOTDIR, EISDIR, EROFS, ENOSPC, EIO, ENOSYS, EINVAL, ENOMEM, ENODEV, EBUSY };
    errno = (e > 0 && e < (int)(sizeof map / sizeof map[0])) ? map[e] : EIO;
    return -1;
}
static sgx_fd_t *fd_of(int fd) { return (fd >= 3 && fd < SGX_FD_MAX && fds[fd].used) ? &fds[fd] : NULL; }
static bool fd_grow(sgx_fd_t *f, uint32_t need) {
    if (need <= f->cap) return true;
    uint32_t c = f->cap ? f->cap : 4096u;
    while (c < need) c = c * 2u > c ? c * 2u : need;
    uint8_t *b = realloc(f->buf, c);
    if (!b) { errno = ENOMEM; return false; }
    if (c > f->cap) memset(b + f->cap, 0, c - f->cap);
    f->buf = b; f->cap = c;
    return true;
}
int _open(const char *path, int flags, int mode) {
    (void)mode;
    int fd = -1;
    for (int i = 3; i < SGX_FD_MAX; i++) if (!fds[i].used) { fd = i; break; }
    if (fd < 0) { errno = EMFILE; return -1; }
    sgx_fd_t *f = &fds[fd];
    memset(f, 0, sizeof *f);
    snprintf(f->path, sizeof f->path, "%s", path);
    const int acc = flags & O_ACCMODE;
    if (acc == O_RDONLY) {
        int e = 0;
        f->ra = sgx->ra_open(path, &e);
        if (!f->ra) return set_errno(e ? e : SGX_ENOENT);
        f->used = true;
        return fd;
    }
    f->wr = true;
    sgx_stat_t st;
    const bool exists = sgx->stat(path, &st) == 0;
    if (exists && st.is_dir) { errno = EISDIR; return -1; }
    if (!exists && !(flags & O_CREAT)) { errno = ENOENT; return -1; }
    if (exists && (flags & O_CREAT) && (flags & O_EXCL)) { errno = EEXIST; return -1; }
    if (exists && !(flags & O_TRUNC) && st.size) {           // 中身を残す（O_APPEND・O_RDWR）
        if (!fd_grow(f, st.size)) return -1;
        uint32_t got = 0;
        const int e = sgx->read_file(path, f->buf, st.size, &got);
        if (e) { free(f->buf); return set_errno(e); }
        f->len = got;
    }
    f->dirty = !exists || (flags & O_TRUNC);                 // 作る・空にするなら、何も書かなくても close で書く
    if (flags & O_APPEND) f->pos = f->len;
    f->used = true;
    return fd;
}
int _read(int fd, char *p, int n) {
    if (fd == 0) return 0;
    sgx_fd_t *f = fd_of(fd);
    if (!f || n < 0) { errno = EBADF; return -1; }
    if (f->ra) {
        const uint32_t size = sgx->ra_size(f->ra);
        if (f->pos >= size) return 0;
        const uint32_t k = (uint32_t)n < size - f->pos ? (uint32_t)n : size - f->pos;
        if (sgx->ra_read(f->ra, f->pos, p, k)) { errno = EIO; return -1; }
        f->pos += k;
        return (int)k;
    }
    if (f->pos >= f->len) return 0;
    const uint32_t k = (uint32_t)n < f->len - f->pos ? (uint32_t)n : f->len - f->pos;
    memcpy(p, f->buf + f->pos, k);
    f->pos += k;
    return (int)k;
}
int _write(int fd, const char *p, int n) {
    if (fd == 1 || fd == 2) { sgx->log(p, (unsigned)n); return n; }   // これまでどおり（printf はログへ）
    sgx_fd_t *f = fd_of(fd);
    if (!f || !f->wr || n < 0) { errno = EBADF; return -1; }
    if (!fd_grow(f, f->pos + (uint32_t)n)) return -1;
    memcpy(f->buf + f->pos, p, (size_t)n);
    f->pos += (uint32_t)n;
    if (f->pos > f->len) f->len = f->pos;
    f->dirty = true;
    return n;
}
int _lseek(int fd, int o, int w) {
    sgx_fd_t *f = fd_of(fd);
    if (!f) { errno = EBADF; return -1; }
    const int64_t end = f->ra ? (int64_t)sgx->ra_size(f->ra) : (int64_t)f->len;
    const int64_t at = (w == SEEK_SET ? 0 : w == SEEK_CUR ? (int64_t)f->pos : end) + o;
    if (at < 0 || at > 0x7FFFFFFF) { errno = EINVAL; return -1; }
    f->pos = (uint32_t)at;
    return (int)at;
}
int _close(int fd) {
    sgx_fd_t *f = fd_of(fd);
    if (!f) { errno = EBADF; return -1; }
    int r = 0;
    if (f->ra) sgx->ra_close(f->ra);
    if (f->wr && f->dirty) { const int e = sgx->write_file(f->path, f->buf ? f->buf : (const uint8_t *)"", f->len); if (e) r = set_errno(e); }
    free(f->buf);
    memset(f, 0, sizeof *f);
    return r;
}
int _fstat(int fd, struct stat *st) {
    memset(st, 0, sizeof *st);
    if (fd >= 0 && fd < 3) { st->st_mode = S_IFCHR; return 0; }
    sgx_fd_t *f = fd_of(fd);
    if (!f) { errno = EBADF; return -1; }
    st->st_mode = S_IFREG | 0644;
    st->st_size = f->ra ? (off_t)sgx->ra_size(f->ra) : (off_t)f->len;
    st->st_blksize = 4096;
    return 0;
}
int _stat(const char *path, struct stat *st) {
    sgx_stat_t s;
    memset(st, 0, sizeof *st);
    const int e = sgx->stat(path, &s);
    if (e) return set_errno(e);
    st->st_mode = s.is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_size = (off_t)s.size;
    return 0;
}
int _unlink(const char *path) { const int e = sgx->remove(path); return e ? set_errno(e) : 0; }
int _rename(const char *a, const char *b) { const int e = sgx->rename(a, b); return e ? set_errno(e) : 0; }
// newlib の rename は _rename を使わず _link + _unlink で付け替えようとする（_link は無い）ので、rename そのものを置く
int rename(const char *a, const char *b) { return _rename(a, b); }
int _link(const char *a, const char *b) { (void)a; (void)b; errno = EMLINK; return -1; }
// 時刻（2026-11-06）: time()・gettimeofday() は API の日時（NTP で合わせた UTC。合わせていなければ既定の日時 + 起動からの時間）
#include <sys/time.h>
int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) { const uint64_t us = sgx->utc_us(); tv->tv_sec = (time_t)(us / 1000000u); tv->tv_usec = (suseconds_t)(us % 1000000u); }
    return 0;
}
int _isatty(int fd) { return fd >= 0 && fd < 3; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }

// C++ の静的なオブジェクトの後片づけの登録（__cxa_atexit）が見る印（2026-11-06。ふつうは起動のファイル crtbegin が置く）
void *__dso_handle = &__dso_handle;

// 倍精度の計算の表（sgx_dmath.S が見る）。ファームが 1.1 以上で、表の数が SDK の並び以上なら使い、そうでなければ NULL（素のソフトウェアの計算）
enum {
#define SGX_DF(n) SGX_DF_N_##n,
#include "sgx_dfuncs.inc"
#undef SGX_DF
    SGX_DF_COUNT
};
const void *const *__sgx_dtab;
void sgx_libc_init(const sgx_api_t *api) {
    __sgx_dtab = 0;
    if ((api->abi_major >= 2u || api->abi_minor >= 1u) && api->size >= offsetof(sgx_api_t, double_funcs) + sizeof api->double_funcs && api->double_funcs) {
        unsigned n = 0;
        const void *const *t = api->double_funcs(&n);
        if (t && n >= SGX_DF_COUNT) __sgx_dtab = t;
    }
}

// newlib の再入用の確保の口（2026-10-31）: strdup・asprintf などは中で _malloc_r を呼ぶ。用意していないと NULL が返った（sgbasic の LOAD が失敗）
struct _reent;
void *_malloc_r(struct _reent *r, size_t n) { (void)r; return malloc(n); }
void  _free_r(struct _reent *r, void *p) { (void)r; free(p); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return realloc(p, n); }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { (void)r; return calloc(a, b); }

// ---- C++ の例外の置き場（2026-11-06）----
// libsupc++ は例外の頭（__cxa_refcounted_exception）の参照の数を、スレッドの有無を見ずに不可分の命令（LDAEX / STLEX）で
// 増減する（catch の終わりの __gxx_exception_cleanup・exception_ptr・set_terminate）。RP2350 の排他アクセスは SRAM の番地にしか
// 効かず、PSRAM（ヒープ・.bss）では STLEX が失敗し続けてその場で回る（POV-Ray が最初の catch で止まった）。
// そこで例外の置き場だけを SRAM から取る: 256 バイトの枠 16 個の溜め（4KB、初めて投げたときに取る）、大きいもの・溢れは 1 つずつ。
// 頭の大きさは arm-none-eabi の GCC 10.3 の libsupc++ のもの（__cxa_allocate_exception が足す 128、依存の例外は 120）。
// リンクは sgx.cmake の --wrap（libsupc++ の中の呼び出しも差し替わる。緊急用の溜め eh_alloc.o は引き込まれない）
#define EH_HDR       128u
#define EH_DEP       120u
#define EH_SLOT      256u
#define EH_SLOTS     16u
typedef struct {
    uint32_t used;
    // cppcheck-suppress unusedStructMember   // slot を 8 バイトに整列するための詰めもの
    uint32_t pad;
    uint8_t slot[EH_SLOTS][EH_SLOT];
} eh_pool_t;
static eh_pool_t *volatile eh_pool;   // 2 つのタスクが同時に初めて投げると 1 つ余分に取るが、余りは終わりに返る（害は無い）

static void *eh_take(size_t n) {
    if (n <= EH_SLOT) {
        eh_pool_t *p = eh_pool;
        if (!p) { p = sgx->sram_alloc(sizeof *p, 8); if (p) { p->used = 0; eh_pool = p; } }
        if (p) {
            // 印は SRAM にあるので不可分の命令が効く
            uint32_t u = __atomic_load_n(&p->used, __ATOMIC_ACQUIRE);
            while (u != 0xFFFFu) {
                const unsigned k = (unsigned)__builtin_ctz(~u);
                if (__atomic_compare_exchange_n(&p->used, &u, u | (1u << k), false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return p->slot[k];
            }
        }
    }
    return sgx->sram_alloc((uint32_t)n, 8);
}
static void eh_give(void *q) {
    eh_pool_t *p = eh_pool;
    uint8_t *base = p ? p->slot[0] : NULL;
    const uint8_t *end = base ? base + EH_SLOTS * EH_SLOT : NULL;
    if (p && (uint8_t *)q >= base && (uint8_t *)q < end) {
        const unsigned k = (unsigned)(((uint8_t *)q - base) / EH_SLOT);
        __atomic_fetch_and(&p->used, ~(1u << k), __ATOMIC_RELEASE);
        return;
    }
    sgx->sram_free(q);
}
void *__wrap___cxa_allocate_exception(size_t thrown);
void  __wrap___cxa_free_exception(void *obj);
void *__wrap___cxa_allocate_dependent_exception(void);
void  __wrap___cxa_free_dependent_exception(void *d);
void *__wrap___cxa_allocate_exception(size_t thrown) {
    uint8_t *p = eh_take(thrown + EH_HDR);
    if (!p) { sgx->log("no SRAM for a C++ exception\n", 28); abort(); }
    memset(p, 0, EH_HDR);
    return p + EH_HDR;
}
void __wrap___cxa_free_exception(void *obj) { eh_give((uint8_t *)obj - EH_HDR); }
void *__wrap___cxa_allocate_dependent_exception(void) {
    void *p = eh_take(EH_DEP);
    if (!p) { sgx->log("no SRAM for a C++ exception\n", 28); abort(); }
    memset(p, 0, EH_DEP);
    return p;
}
void __wrap___cxa_free_dependent_exception(void *d) { eh_give(d); }
