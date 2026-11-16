// wav2mp3（apps/wav2mp3 の中身 w2m.c と LAME）をホストで走らせる（2026-11-14、tests/test_wav2mp3.py）。
//   w2mhost in.wav out.mp3 [--bitrate kbps] [-q N] [--mono]
//   w2mhost --pcm RATE FRAMES out.mp3 ...
// SPDX-License-Identifier: LGPL-2.0-or-later
#include "w2m.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *h_open(const char *path, uint32_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *size = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    return f;
}
static int h_read(void *h, uint32_t off, void *dst, uint32_t len) {
    if (fseek(h, (long)off, SEEK_SET)) return -1;
    return fread(dst, 1, len, h) == len ? 0 : -1;
}
static void h_close(void *h) { fclose(h); }
static void h_msg(const char *s) { fprintf(stderr, "%s\n", s); }
static int to_file(void *ctx, const uint8_t *p, uint32_t n) { return fwrite(p, 1, n, ctx) == n ? 0 : -1; }

// --pcm: WAV の代わりに正弦波を作って w2m_open_pcm を通す（VGM の合成の代わり、2026-11-15）
static uint32_t pcm_rate, pcm_total, pcm_pos;
static int pcm_read(void *ctx, int16_t *stereo, unsigned frames) {
    (void)ctx;
    if (pcm_pos >= pcm_total) return 0;
    unsigned n = pcm_total - pcm_pos;
    if (n > frames) n = frames;
    for (unsigned i = 0; i < n; i++) {
        const double t = (double)(pcm_pos + i) / (double)pcm_rate;
        stereo[i * 2] = (int16_t)(0.5 * 32767.0 * sin(2.0 * 3.14159265358979 * 1000.0 * t));
        stereo[i * 2 + 1] = (int16_t)(0.5 * 32767.0 * sin(2.0 * 3.14159265358979 * 1500.0 * t));
    }
    pcm_pos += n;
    return (int)n;
}

int main(int argc, char **argv) {
    w2m_opts_t o; w2m_default_opts(&o);
    const char *in = NULL, *out = NULL;
    bool use_pcm = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bitrate") && i + 1 < argc) o.bitrate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-q") && i + 1 < argc) o.quality = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mono")) o.mono = true;
        else if (!strcmp(argv[i], "--pcm") && i + 2 < argc) {
            use_pcm = true;
            pcm_rate = (uint32_t)atoi(argv[++i]);
            pcm_total = (uint32_t)atoi(argv[++i]);
        }
        else if (!in) in = argv[i]; else out = argv[i];
    }
    if (use_pcm && in && !out) { out = in; in = NULL; }   // --pcm のときの位置引数は出力
    if ((!in && !use_pcm) || !out) {
        fprintf(stderr, "usage: w2mhost in.wav out.mp3 [--bitrate kbps] [-q N] [--mono]\n"
                        "       w2mhost --pcm RATE FRAMES out.mp3 [--bitrate kbps] [-q N] [--mono]\n");
        return 2;
    }
    const w2m_plat_t pl = { h_open, h_read, h_close, h_msg, NULL, NULL };
    char err[200];
    w2m_t *w = use_pcm ? w2m_open_pcm(&pl, pcm_rate, pcm_total, pcm_read, NULL, &o, err, sizeof err)
                       : w2m_open(&pl, in, &o, err, sizeof err);
    if (!w) { fprintf(stderr, "error: %s\n", err); return 1; }
    FILE *f = fopen(out, "wb");
    if (!f) { w2m_close(w); return 1; }
    const uint32_t pred = w2m_size(w);
    const int r = w2m_encode(w, to_file, f, err, sizeof err);
    fclose(f);
    if (r) { fprintf(stderr, "error: %s\n", err); w2m_close(w); return 1; }
    printf("%s: %s, %lu bytes (predicted %lu)\n", out, w2m_describe(w), (unsigned long)pred, (unsigned long)pred);
    w2m_close(w);
    return 0;
}
