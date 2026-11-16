/* sgnano の外部実行ファイル（SGX）のリンカの台本（2026-10-24、docs/sgx_abi.md）。
   番地 0 に結ぶ（再配置できる）。-Wl,-q で再配置の情報を残し、sgx_link.py が像（先頭の見出しから .data の終わりまで）と再配置の表を書き出す。
   ファームは PSRAM のどこかに置いて表の語に置いた番地を足し、うしろの .bss を 0 にしてから入口を呼ぶ */
MEMORY
{
    SGX (rwx) : ORIGIN = 0x00000000, LENGTH = 4096K
}
ENTRY(sgx_start)

SECTIONS
{
    .sgx_header : { KEEP(*(.sgx_header)) } > SGX
    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.gcc_except_table .gcc_except_table.*)    /* C++ の例外の表（2026-11-06、povray） */
        . = ALIGN(4);
        __sgx_init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __sgx_init_array_end = .;
    } > SGX
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > SGX
    .ARM.exidx : {
        __exidx_start = .;                          /* C++ の例外で巻き戻す表（libgcc の巻き戻しが探す） */
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > SGX
    .data : {
        *(.data .data.*)
        . = ALIGN(8);
    } > SGX
    __sgx_image_end = .;
    .bss (NOLOAD) : {
        __sgx_bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __sgx_bss_end = .;
    } > SGX
    __sgx_image_size = __sgx_image_end - ORIGIN(SGX);
    __sgx_bss_size = __sgx_bss_end - __sgx_bss_start;
    /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.note*) }
}
