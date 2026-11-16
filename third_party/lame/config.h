/* LAME 3.100 を SGX（Cortex-M33、newlib-nano）で組むための config.h（2026-11-14、sgnano。configure は走らせない） */
#ifndef LAME_CONFIG_H
#define LAME_CONFIG_H
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_ERRNO_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1            /* log の表で速く（LAME の既定の選択肢） */
#define TAKEHIRO_IEEE754_HACK 1   /* IEEE 754 の float の並びを使う速い量子化（ARM は little endian の IEEE 754） */
#define SIZEOF_DOUBLE 8
#define SIZEOF_FLOAT 4
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_SHORT 2
#define SIZEOF_UNSIGNED_INT 4
#define SIZEOF_UNSIGNED_LONG 4
#define SIZEOF_UNSIGNED_SHORT 2
#define PACKAGE "lame"
#define VERSION "3.100"
#define LAME_LIBRARY_BUILD 1
/* configure が書く型（util.h の fast_log2 などが使う） */
typedef float ieee754_float32_t;
/* デコーダ（mpglib）・SSE は使わない */
#undef HAVE_MPGLIB
#undef DECODE_ON_THE_FLY
#undef HAVE_XMMINTRIN_H
#endif
