// WAV -> MP3 の変換の中身（2026-11-14、ユーザー指示: 既定は 320kbps の CBR）。エンコーダは LAME 3.100（LGPL、third_party/lame）。
// 実機の SGX（main_sgx.c）とホストの試し（host/）が同じものを使う。LAME は sgnano のファームウェア /
// wav2mp3.sgx がリンクする。LGPL の対応ソースとビルド手順はこのリポジトリ（sgnano_wav2mp3）で配布する。
// SPDX-License-Identifier: LGPL-2.0-or-later
//
// 出力の大きさを先に決める: CBR のフレームの大きさ（144 × ビットレート ÷ 標本化の速さ + 詰め物）と詰め物の入り方は音の中身によらず、
// フレームの数も標本の数だけで決まる（LAME の lame_get_totalframes）。LAME の作り直し（推測を含む）を通さないよう、
// 2 倍・4 倍の速さ（88.2k / 96k / 176.4k / 192k など）は自前の半帯域フィルタで正確に半分にしてから渡す。
// Xing / Info の頭と ID3 は書かない（大きさがフレームの合計だけになるように）。
#ifndef W2M_H
#define W2M_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *(*open)(const char *path, uint32_t *size);
    int   (*read)(void *h, uint32_t off, void *dst, uint32_t len);   // 0 = 読めた
    void  (*close)(void *h);
    void  (*msg)(const char *s);
    bool  (*cancel)(void);                                          // NULL 可
    void  (*progress)(uint32_t done, uint32_t total);               // 入力の標本で（NULL 可）
} w2m_plat_t;

typedef struct {
    int bitrate;                            // kbps（既定 320）
    int quality;                            // LAME の -q（0 = 最良 .. 9 = 最速。既定 3 = LAME の既定）
    bool mono;                              // ステレオでもモノラルにする
} w2m_opts_t;

void w2m_default_opts(w2m_opts_t *o);

typedef struct w2m w2m_t;
typedef int (*w2m_sink_t)(void *ctx, const uint8_t *p, uint32_t n);   // 0 = 受けた

w2m_t      *w2m_open(const w2m_plat_t *pl, const char *wav_path, const w2m_opts_t *o, char *err, size_t n);
// WAV の代わりに、16 ビット・ステレオの PCM を呼び手から受け取る（VGM / VGZ をファームの合成で、SGX ABI 5.7）。
// read は左右に並べた標本を frames まで書き、書いた数を返す（0 = 終わり、負 = 失敗）。frames_total は前もって分かる長さ
typedef int (*w2m_pcm_read_t)(void *ctx, int16_t *stereo, unsigned frames);
w2m_t      *w2m_open_pcm(const w2m_plat_t *pl, uint32_t rate, uint32_t frames_total, w2m_pcm_read_t rd, void *ctx,
                         const w2m_opts_t *o, char *err, size_t n);
uint32_t    w2m_size(const w2m_t *w);       // 出力の MP3 のバイト数（前もって分かる）
int         w2m_encode(w2m_t *w, w2m_sink_t sink, void *ctx, char *err, size_t n);   // 0 = 書けた（大きさが w2m_size と同じ）
void        w2m_close(w2m_t *w);
const char *w2m_describe(const w2m_t *w);   // "96000 Hz 24 bit stereo -> 48000 Hz 320 kbps joint stereo" など

#endif
