// wav2mp3: WAV（と VGM / VGZ）を MP3 にする（実機の SGX、2026-11-14、ユーザー指示: 既定は 320kbps の CBR）。
// VGM / VGZ はファームの合成から 44.1kHz・16 ビットの PCM を受け取る（SGX ABI 5.7 の music_pcm_*。2026-11-15）。
// エンコーダは LAME 3.100（LGPL、third_party/lame）。sgnano のファームウェア / wav2mp3.sgx がリンクする。
// LGPL の対応ソース（apps/wav2mp3・third_party/lame・sdk/sgx）と、LAME を差し替えて組み直す手順は、
// このリポジトリ（sgnano_wav2mp3）で配布する。
// SPDX-License-Identifier: LGPL-2.0-or-later
//   wav2mp3 in.wav [out.mp3] [--bitrate kbps] [-q 0..9] [--mono]
// - 出力を省くと、入力と同じフォルダ・同じ名前の .mp3。
// - 96kHz / 88.2kHz などは 48kHz / 44.1kHz へ落とす（MP3 は 48kHz まで）。
// - 出力の大きさを先に計算してファイルを作り（create_file、ABI 5.6）、その場へ書く（ra_write）。
// - ESC で止める（途中までの出力は消す）。
#include "sgx_api.h"
#include "w2m.h"
#include "sgx_progress.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const sgx_api_t *sgx;

static void say(const char *s) { sgx->term_write(s, (unsigned)strlen(s)); }
static void sayf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void sayf(const char *fmt, ...) {
    char b[300];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    say(b);
}

static void *p_open(const char *path, uint32_t *size) {
    int e = 0;
    sgx_file_t *f = sgx->ra_open(path, &e);
    if (f) *size = sgx->ra_size(f);
    return f;
}
static int p_read(void *h, uint32_t off, void *dst, uint32_t len) { return sgx->ra_read(h, off, dst, len); }
static void p_close(void *h) { sgx->ra_close(h); }
static sgxp_t PG;                                       // 進み具合（シェルは 1 行を書き替え、ファイラーの ! は窓。2026-11-16）
static void p_msg(const char *s) { sgxp_msg(&PG, s); }
static bool stop;
static bool p_cancel(void) {
    for (int k; (k = sgx->key(false)) >= 0; ) if ((k & 0xFF) == 0x1B) stop = true;
    if (sgx->stop_requested()) stop = true;               // ファームが ESC を受けた（ファイラー・シェルの前景）
    return stop;
}
static uint32_t t_start;
static uint64_t out_now, out_all;                          // 書いた大きさ・出力の大きさ（進み具合の表示）
static void p_progress(uint32_t done, uint32_t total) { sgxp_update(&PG, done, total, out_now, out_all); }

// 出力: 16KB ずつ、その場へ書く
#define WBUF (16u * 1024u)
typedef struct { sgx_file_t *f; uint32_t off, n; int err; uint8_t buf[WBUF]; } wr_t;
static int wr_flush(wr_t *w) {
    if (w->n && !w->err) w->err = sgx->ra_write(w->f, w->off, w->buf, w->n);
    w->off += w->n; w->n = 0;
    out_now = w->off;
    return w->err;
}
static int wr_sink(void *ctx, const uint8_t *p, uint32_t n) {
    wr_t *w = ctx;
    while (n && !w->err) {
        uint32_t c = WBUF - w->n;
        if (c > n) c = n;
        memcpy(w->buf + w->n, p, c); w->n += c; p += c; n -= c;
        if (w->n == WBUF) (void)wr_flush(w);
    }
    return w->err;
}

static int pcm_read(void *ctx, int16_t *stereo, unsigned frames) { (void)ctx; return sgx->music_pcm_read(stereo, frames); }
static bool has_ext(const char *p, const char *e) {
    const size_t a = strlen(p), b = strlen(e);
    if (a < b) return false;
    for (size_t i = 0; i < b; i++) { char c = p[a - b + i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != e[i]) return false; }
    return true;
}

static const char USAGE[] =
    "usage: wav2mp3 in.wav|in.vgm|in.vgz [out.mp3] [--bitrate kbps] [-q 0..9] [--mono]\n"
    "  --bitrate  CBR bit rate (default 320)\n"
    "  -q         LAME quality, 0 = best .. 9 = fastest (default 3)\n"
    "  input: 8/16/24/32-bit PCM or 32-bit float, mono or stereo,\n"
    "         32 / 44.1 / 48 kHz (88.2 / 96 / 176.4 / 192 kHz are halved down)\n"
    "  .vgm / .vgz: rendered by the firmware at 44.1 kHz (the file plays once)\n";

int sgx_main(const sgx_api_t *api, int argc, char **argv) {
    (void)api;
    if (sgx->size < offsetof(sgx_api_t, ra_write) + sizeof sgx->ra_write) { say("wav2mp3 needs firmware with SGX ABI 5.6\n"); return 1; }
    w2m_opts_t o; w2m_default_opts(&o);
    const char *in = NULL, *outp = NULL;
    for (int i = 1; i < argc; i++) {
        const char *s = argv[i];
        if (!strcmp(s, "--bitrate") && i + 1 < argc) o.bitrate = atoi(argv[++i]);
        else if (!strcmp(s, "-q") && i + 1 < argc) o.quality = atoi(argv[++i]);
        else if (!strcmp(s, "--mono")) o.mono = true;
        else if (!strcmp(s, "-h") || !strcmp(s, "--help")) { say(USAGE); return 0; }
        else if (s[0] == '-') { sayf("unknown option: %s\n%s", s, USAGE); return 2; }
        else if (!in) in = s;
        else if (!outp) outp = s;
        else { sayf("too many file names: %s\n", s); return 2; }
    }
    if (!in) { say(USAGE); return 2; }
    static char out[260];
    if (outp) snprintf(out, sizeof out, "%s", outp);
    else {
        snprintf(out, sizeof out - 4, "%s", in);
        char *dot = strrchr(out, '.'), *sl = strrchr(out, '/');
        if (dot && (!sl || dot > sl)) *dot = 0;
        strcat(out, ".mp3");
    }
    static char title[300];
    const char *ib = strrchr(in, '/');
    snprintf(title, sizeof title, "wav2mp3: %s", ib ? ib + 1 : in);
    sgxp_begin(&PG, title, out);
    if (!sgxp_popup(&PG)) sayf("wav2mp3: %s -> %s\n", in, out);
    t_start = sgx->ms();
    const w2m_plat_t pl = { p_open, p_read, p_close, p_msg, p_cancel, p_progress };
    char err[200] = "";
    const bool vgm = has_ext(in, ".vgm") || has_ext(in, ".vgz");
    bool pcm_open = false;
    w2m_t *w = NULL;
    if (vgm) {
        if (sgx->size < offsetof(sgx_api_t, music_pcm_close) + sizeof sgx->music_pcm_close) { say("VGM input needs firmware with SGX ABI 5.7\n"); return 1; }
        uint32_t frames = 0, rate = 0;
        sgxp_msg(&PG, "opening the VGM (a big .vgz is unpacked to a work file first)");
        if (sgx->music_pcm_open(in, &frames, &rate)) {
            sgx_music_info_t mi; sgx->music_info(&mi);
            return sgxp_end(&PG, SGXP_FAIL, mi.error[0] ? mi.error : "cannot render the VGM", 0);
        }
        pcm_open = true;
        char m[96];
        snprintf(m, sizeof m, "VGM: %lu frames at %lu Hz (%lu s)", (unsigned long)frames, (unsigned long)rate, (unsigned long)(frames / (rate ? rate : 1u)));
        sgxp_msg(&PG, m);
        w = w2m_open_pcm(&pl, rate, frames, pcm_read, NULL, &o, err, sizeof err);
    } else w = w2m_open(&pl, in, &o, err, sizeof err);
    if (!w) { if (pcm_open) sgx->music_pcm_close(); return sgxp_end(&PG, SGXP_FAIL, err, 0); }
    const uint32_t total = w2m_size(w);
    out_all = total;
    {
        char m[200];
        snprintf(m, sizeof m, "%s, %lu KB", w2m_describe(w), (unsigned long)(total / 1024u));
        sgxp_msg(&PG, m);
    }
    int r = 0;
    bool made = false;
    const int e = sgx->create_file(out, total);
    if (e) { r = -1; snprintf(err, sizeof err, "cannot create %s: %s", out, sgx->strerror(e)); }
    else made = true;
    wr_t *wr = NULL;
    if (!r) {
        int oe = 0;
        wr = calloc(1, sizeof *wr);
        if (wr) wr->f = sgx->ra_open(out, &oe);
        if (!wr || !wr->f) { r = -1; snprintf(err, sizeof err, "cannot open %s: %s", out, wr ? sgx->strerror(oe) : "out of memory"); }
    }
    if (!r) {
        r = w2m_encode(w, wr_sink, wr, err, sizeof err);
        if (!r && wr_flush(wr)) { r = -1; snprintf(err, sizeof err, "write failed: %s", sgx->strerror(wr->err)); }
        if (!r && wr->off != total) { r = -1; snprintf(err, sizeof err, "internal error: wrote %lu of %lu bytes", (unsigned long)wr->off, (unsigned long)total); }
    }
    if (wr && wr->f) sgx->ra_close(wr->f);
    free(wr);
    w2m_close(w);
    if (pcm_open) sgx->music_pcm_close();
    if (r) {
        if (made) (void)sgx->remove(out);                   // 途中までの出力を残さない
        return sgxp_end(&PG, stop ? SGXP_CANCELED : SGXP_FAIL, err[0] ? err : "failed", 0);
    }
    if (!sgxp_popup(&PG)) sayf("%s: %lu bytes (%lu s)\n", out, (unsigned long)total, (unsigned long)((sgx->ms() - t_start) / 1000u));
    return sgxp_end(&PG, SGXP_OK, NULL, total);
}
