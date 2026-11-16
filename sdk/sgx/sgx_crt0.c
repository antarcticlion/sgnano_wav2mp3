// SGX の始まり（2026-10-24、docs/sgx_abi.md）。見出しと入口。プログラムごとに一緒に組む（CMake の sgx_add_program）。
// 名前・印・スタックの大きさは組むときの定義（SGX_APP_NAME・SGX_APP_FLAGS・SGX_APP_STACK）から
#include "sgx_api.h"

#ifndef SGX_APP_NAME
#define SGX_APP_NAME "program"
#endif
#ifndef SGX_APP_FLAGS
#define SGX_APP_FLAGS 0u
#endif
#ifndef SGX_APP_STACK
#define SGX_APP_STACK 0u
#endif

extern char __sgx_image_size[], __sgx_bss_size[];        // リンカの台本の値（番地として読む）
extern void (*__sgx_init_array_start[])(void), (*__sgx_init_array_end[])(void);
int sgx_main(const sgx_api_t *api, int argc, char **argv);   // プログラムが書く
void sgx_libc_init(const sgx_api_t *api);               // sgx_libc.c

const sgx_api_t *sgx;                                   // どこからでも使える表（sgx_libc.c も使う）

int sgx_start(const sgx_api_t *api, int argc, char **argv);
int sgx_start(const sgx_api_t *api, int argc, char **argv) {
    sgx = api;
    sgx_libc_init(api);
    for (void (**f)(void) = __sgx_init_array_start; f < __sgx_init_array_end; f++) (*f)();
    return sgx_main(api, argc, argv);
}

__attribute__((section(".sgx_header"), used)) const sgx_header_t sgx_header = {
    .magic = SGX_MAGIC,
    .abi_major = SGX_ABI_MAJOR, .abi_minor = SGX_ABI_MINOR,
    .header_size = sizeof(sgx_header_t),
    .link_base = SGX_LINK_BASE,
    .image_size = (uint32_t)__sgx_image_size,
    .bss_size = (uint32_t)__sgx_bss_size,
    .entry = (uint32_t)sgx_start,
    .stack_size = SGX_APP_STACK,
    .flags = SGX_APP_FLAGS,
    .name = SGX_APP_NAME,
};
