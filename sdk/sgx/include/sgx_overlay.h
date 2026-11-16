// SGX のオーバーレイ（SDK の部品、2026-11-06）。使い方は docs/sgx_abi.md の §10。
//
// 大きなプログラムを「常駐のコア」と「入れ替えるモジュール」に分け、モジュールをファイル（パック）から
// 決まった置き場（スロット）へ読み込んで、関数の表を通して呼ぶ。MMU が無いので、コードは置いたスロットの番地で動く
// （モジュールは番地 0 に結び、読み込むときに再配置の表の語へスロットの番地を足す）。
//
// - ファームの ABI には頼らない（4.0 の表だけで動く）。読み出しは呼び手の口（sgx_ovl_io_t）で、SGX なら ra_read、
//   試しならホストのファイルから。スロットの置き場も呼び手が用意する（psram_alloc や fb_lend_spare のページ）
// - モジュールは常駐のシンボルに結ばない。常駐の機能は init に渡す host の表から、モジュールの機能は init が埋める iface の表から使う
// - モジュールを替えると、前のモジュールの関数・定数・データの番地はすべて無効になる。生きている間は pin しておく
//   （C++ の仮想関数の表・モジュールの中の文字列を指すポインタも同じ）
// - どの関数も同じタスクから呼ぶこと（中で排他しない）
// このファイルは sgnano の SDK の一部で、MIT ライセンス
#ifndef SGX_OVERLAY_H
#define SGX_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#define SGX_OVL_PACK_MAGIC   0x4F584753u   // "SGXO"（パックの頭）
#define SGX_OVL_MOD_MAGIC    0x4D584753u   // "SGXM"（モジュールの像の頭）
#define SGX_OVL_VERSION      1u
#define SGX_OVL_MAX_SLOTS    8u
#define SGX_OVL_NAME_MAX     16u

// 誤り（負の数で返す。値は sgx_api.h の SGX_E* と同じ）
#define SGX_OVL_EIO     9    // SGX_EIO: 読めない
#define SGX_OVL_ENOTSUP 10   // SGX_ENOTSUP: 版が違う
#define SGX_OVL_EINVAL  11   // SGX_EINVAL: 引数・パック・像がおかしい
#define SGX_OVL_ENOMEM  12   // SGX_ENOMEM: スロットに入らない
#define SGX_OVL_EBUSY   14   // SGX_EBUSY: スロットが pin されている

// ---- パックの形（すべて小さい方から、4 バイト整列）----
// [sgx_ovl_pack_header_t][sgx_ovl_entry_t x count][モジュール 0 の像][表 0]...
typedef struct {
    uint32_t magic;          // SGX_OVL_PACK_MAGIC
    uint32_t version;        // SGX_OVL_VERSION
    uint32_t count;          // モジュールの数
    uint32_t table_off;      // 入口の表の位置
} sgx_ovl_pack_header_t;
typedef struct {
    char     name[SGX_OVL_NAME_MAX];   // 0 で終わる（16 字ちょうどなら終わりの 0 は無い）
    uint32_t off;            // 像の位置（パックの頭から）
    uint32_t image_size;     // 像（見出し・コード・定数・初めの値）
    uint32_t bss_size;       // 像の後ろに 0 で取る
    uint32_t reloc_count;    // 像の後ろに続く再配置の表（像の頭からのバイトの位置、32 ビットずつ）
} sgx_ovl_entry_t;

// ---- モジュールの像の頭（sgx_ovl.ld の .sgx_ovl_header、SGX_OVL_MODULE で置く）----
typedef struct sgx_ovl_ctx sgx_ovl_ctx_t;
typedef int (*sgx_ovl_init_t)(const sgx_ovl_ctx_t *ctx, void *iface, uint32_t iface_size);
typedef struct {
    uint32_t magic;          // SGX_OVL_MOD_MAGIC
    uint32_t version;        // SGX_OVL_VERSION
    uint32_t image_size;     // パックの表と同じ（確かめに使う）
    uint32_t bss_size;
    sgx_ovl_init_t init;     // 入口（再配置で番地が直る）
    uint32_t reserved[3];
} sgx_ovl_module_header_t;

// init に渡す文脈
struct sgx_ovl_ctx {
    uint32_t    version;     // SGX_OVL_VERSION
    int         module;      // パックの中の番号
    int         slot;
    uint8_t    *base;        // スロットの頭（= 像の頭）
    uint32_t    size;        // スロットの大きさ
    const void *host;        // 常駐が渡す表（sgx_ovl_open の host）
};

// モジュールの側: 入口の関数を見出しに置く（モジュールのソースに 1 回だけ書く）
#define SGX_OVL_MODULE(init_fn) \
    extern char __ovl_image_size[], __ovl_bss_size[]; \
    __attribute__((section(".sgx_ovl_header"), used)) const sgx_ovl_module_header_t sgx_ovl_header = { \
        SGX_OVL_MOD_MAGIC, SGX_OVL_VERSION, (uint32_t)__ovl_image_size, (uint32_t)__ovl_bss_size, init_fn, { 0, 0, 0 } }

// ---- 常駐の側 ----
typedef struct {
    // パックの off から len バイトを dst へ。0 = 読めた、負 = 誤り
    int  (*read)(void *user, uint32_t off, void *dst, uint32_t len);
    // コードを書いたあとに呼ぶ（NULL なら DSB / ISB だけ）。キャッシュを通らない窓に書いたときは、ここでキャッシュを捨てる
    void (*sync)(void *user, void *p, uint32_t n);
    void *user;
} sgx_ovl_io_t;

typedef struct {
    uint8_t *base;           // キャッシュを通る窓（コードを走らせる番地）。8 バイト整列
    uint32_t size;
    int      module;         // 載っているモジュール（-1 = 空）
    uint16_t pin;
    uint32_t gen;            // 読み込むたびに 1 増える（iface が古くなったかを見るのに使う）
} sgx_ovl_slot_t;

typedef struct {
    uint32_t loads, hits, bytes_read, relocs, failures;
} sgx_ovl_stats_t;

typedef struct {
    sgx_ovl_io_t   io;
    const void    *host;
    uint32_t       count, table_off;
    unsigned       nslots;
    sgx_ovl_slot_t slot[SGX_OVL_MAX_SLOTS];
    sgx_ovl_stats_t st;
} sgx_ovl_t;

int  sgx_ovl_open(sgx_ovl_t *o, const sgx_ovl_io_t *io, const void *host);   // パックの頭を確かめる。0 か負の誤り
int  sgx_ovl_add_slot(sgx_ovl_t *o, void *base, uint32_t size);              // スロットの番号か負の誤り
int  sgx_ovl_count(const sgx_ovl_t *o);                                      // モジュールの数
int  sgx_ovl_find(sgx_ovl_t *o, const char *name);                           // 名前から番号（無ければ負）
int  sgx_ovl_info(sgx_ovl_t *o, int module, sgx_ovl_entry_t *out);           // パックの表の 1 行
int  sgx_ovl_load(sgx_ovl_t *o, int slot, int module);                       // 読み込む（もう載っていれば何もしない）。0 か負の誤り
int  sgx_ovl_bind(sgx_ovl_t *o, int slot, int module, void *iface, uint32_t iface_size);   // 読み込んで init を呼ぶ。init の戻り値か負の誤り
int  sgx_ovl_unload(sgx_ovl_t *o, int slot);                                 // 空にする（pin なら -EBUSY）
int  sgx_ovl_loaded(const sgx_ovl_t *o, int slot);                           // 載っているモジュールか -1
int  sgx_ovl_pin(sgx_ovl_t *o, int slot);                                    // 入れ替えを止める（数える）
int  sgx_ovl_unpin(sgx_ovl_t *o, int slot);
uint32_t sgx_ovl_gen(const sgx_ovl_t *o, int slot);

#endif
