// 倍精度の計算をファームの DCP（倍精度のコプロセッサ）の版へ向け直す（2026-10-25、ABI 1.1、docs/sgx_abi.md）。
// コンパイラが出す __aeabi_dadd などと、sqrt・sin などを --wrap でここへ（sgx.cmake）。表（__sgx_dtab、sgx_libc.c が API の double_funcs から入れる）
// があれば表の関数へそのまま飛び（r0-r3・スタックの引数も戻り値・比べの印も、そのまま渡る）、無ければ（古いファーム）素の版（__real_*、libgcc・newlib）へ。
// 壊すのは ip（r12）と印だけ（どちらも呼び出しで壊してよい）
    .syntax unified
    .cpu cortex-m33
    .thumb

    .macro sgx_df name, idx
    .section .text.__wrap_\name, "ax"
    .global __wrap_\name
    .type __wrap_\name, %function
    .thumb_func
__wrap_\name:
    ldr     ip, 2f
    ldr     ip, [ip]
    cmp     ip, #0
    beq     1f
    ldr     ip, [ip, #(\idx * 4)]
    bx      ip
1:  b.w     __real_\name
    .p2align 2
2:  .word   __sgx_dtab
    .endm

#define SGX_DF(n) sgx_df n, SGX_DF_IDX ; .set SGX_DF_IDX, SGX_DF_IDX + 1 ;
    .set SGX_DF_IDX, 0
#include "sgx_dfuncs.inc"
