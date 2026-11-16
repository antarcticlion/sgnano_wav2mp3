/* SGX のオーバーレイのモジュールのリンカの台本（2026-11-06、include/sgx_overlay.h）。
   番地 0 に結ぶ（読み込むときスロットの番地を足す）。-Wl,-q で再配置の情報を残し、sgx_ovl_pack.py が像と表をパックへ入れる。
   静的な初期化（.init_array）は持てない（モジュールには crt0 が無い。初めの値は .data に置く） */
MEMORY
{
    OVL (rwx) : ORIGIN = 0x00000000, LENGTH = 4096K
}
ENTRY(sgx_ovl_header)

SECTIONS
{
    .sgx_ovl_header : { KEEP(*(.sgx_ovl_header)) } > OVL
    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > OVL
    .data : {
        *(.data .data.*)
        . = ALIGN(8);
    } > OVL
    __ovl_image_end = .;
    .bss (NOLOAD) : {
        __ovl_bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __ovl_bss_end = .;
    } > OVL
    __ovl_image_size = __ovl_image_end - ORIGIN(OVL);
    __ovl_bss_size = __ovl_bss_end - __ovl_bss_start;
    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) *(.init_array*) *(.fini_array*) }
}
