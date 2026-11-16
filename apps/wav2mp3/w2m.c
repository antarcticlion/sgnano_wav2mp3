// WAV -> MP3 の変換の中身（2026-11-14）。説明は w2m.h
// SPDX-License-Identifier: LGPL-2.0-or-later
#include "w2m.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lame.h"

#define CHUNK 2048u                         // 1 回に読む入力の標本（フレーム）
#define HB_TAPS 95                          // 半帯域フィルタの長さ（奇数、(HB_TAPS-1)/2 は奇数）。阻止域 約 -80dB
#define HB_MID ((HB_TAPS - 1) / 2)

typedef struct {                            // 2:1 の半帯域フィルタ 1 段（チャンネルごとに履歴）
    float hist[2][HB_TAPS];
    unsigned pos;                           // 次に書く場所
    unsigned phase;                         // 0 = 出す番、1 = 捨てる番
} hb_t;

struct w2m {
    const w2m_plat_t *pl;
    w2m_opts_t o;
    void *fh;
    uint32_t data_off, data_len;            // WAV の data
    unsigned ch, bits, fmt, block;          // fmt: 1 = PCM、3 = 浮動小数点
    uint32_t rate_in, rate_out, frames_in, frames_out;
    unsigned stages;                        // 半帯域の段の数（0 / 1 / 2）
    hb_t hb[2];
    float coef[HB_TAPS];
    lame_global_flags *gf;
    uint32_t total_frames, mp3_bytes;
    uint8_t *rd;                            // 読む受け場
    float *l, *r;                           // LAME へ渡す（CHUNK 分）
    float *buf1[2], *buf2[2];               // 段の間（ヒープ。2026-11-15: スタックに 24KB 置いていて、LAME の flush でスタックが溢れた）
    uint8_t *mp3;                           // LAME の出力の受け場
    uint32_t mp3_cap;
    char desc[120];
    w2m_pcm_read_t pcm_rd; void *pcm_ctx;   // w2m_open_pcm: 呼び手から 16 ビット・ステレオを受け取る
};

void w2m_default_opts(w2m_opts_t *o) { memset(o, 0, sizeof *o); o->bitrate = 320; o->quality = 3; }

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void fail(char *err, size_t n, const char *m) { if (err && n) snprintf(err, n, "%s", m); }

static const char *parse_wav(w2m_t *w, uint32_t size) {
    uint8_t h[12];
    if (size < 44u || w->pl->read(w->fh, 0, h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return "not a WAV file";
    bool have_fmt = false;
    for (uint32_t p = 12; p + 8u <= size; ) {
        uint8_t c[8];
        if (w->pl->read(w->fh, p, c, 8)) return "read error";
        const uint32_t len = le32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            uint8_t f[40];
            const uint32_t n = len < sizeof f ? len : sizeof f;
            if (n < 16u || w->pl->read(w->fh, p + 8u, f, n)) return "bad fmt chunk";
            w->fmt = le16(f); w->ch = le16(f + 2); w->rate_in = le32(f + 4); w->block = le16(f + 12); w->bits = le16(f + 14);
            if (w->fmt == 0xFFFEu && n >= 26u) w->fmt = le16(f + 24);   // WAVE_FORMAT_EXTENSIBLE: 中身の形
            have_fmt = true;
        } else if (!memcmp(c, "data", 4)) {
            if (!have_fmt) return "data before fmt";
            w->data_off = p + 8u;
            w->data_len = len <= size - w->data_off ? len : size - w->data_off;   // 書き途中で切れたファイルも読めるだけ
            break;
        }
        p += 8u + len + (len & 1u);
        if (p < 12u) break;                 // 溢れ
    }
    if (!have_fmt || !w->data_off) return "no fmt / data chunk";
    if (w->ch < 1u || w->ch > 2u) return "only mono or stereo WAV is supported";
    const bool pcm = w->fmt == 1u && (w->bits == 8u || w->bits == 16u || w->bits == 24u || w->bits == 32u);
    const bool flt = w->fmt == 3u && w->bits == 32u;
    if (!pcm && !flt) return "only 8/16/24/32-bit PCM or 32-bit float WAV is supported";
    if (w->block != w->ch * (w->bits / 8u)) return "bad block align";
    w->frames_in = w->data_len / w->block;
    return NULL;
}

// MP3 の速さ（MPEG-1 の 32k / 44.1k / 48k）へ、2:1 を何段で落とすか
static bool pick_rate(w2m_t *w) {
    static const uint32_t base[3] = { 32000u, 44100u, 48000u };
    for (unsigned s = 0; s <= 2u; s++)
        for (unsigned k = 0; k < 3u; k++)
            if (w->rate_in == base[k] << s) { w->stages = s; w->rate_out = base[k]; return true; }
    return false;
}

// 窓をかけた sinc の半帯域フィルタ（Kaiser, beta 8）。中央 0.5、偶数番目（中央を除く）は 0
static double bessel_i0(double x) {
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 40; k++) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
    return s;
}
static void make_coef(float *c) {
    const double beta = 8.0, i0b = bessel_i0(beta);
    double sum = 0.0;
    double tmp[HB_TAPS];
    for (int i = 0; i < HB_TAPS; i++) {
        const int m = i - HB_MID;
        double v;
        if (m == 0) v = 0.5;
        else if (m % 2 == 0) v = 0.0;
        else v = sin(3.14159265358979323846 * m / 2.0) / (3.14159265358979323846 * m);
        const double r = (double)m / HB_MID;
        v *= bessel_i0(beta * sqrt(1.0 - r * r)) / i0b;
        tmp[i] = v; sum += v;
    }
    for (int i = 0; i < HB_TAPS; i++) c[i] = (float)(tmp[i] / sum);   // 直流の利得を 1 に
}

// 1 段: n 標本（ch チャンネル、ch 本の配列）を入れ、出た標本を out へ。出た数を返す
static unsigned hb_run(const float *coef, hb_t *h, unsigned ch, float *const in[2], unsigned n, float *const out[2]) {
    unsigned m = 0;
    for (unsigned i = 0; i < n; i++) {
        for (unsigned c = 0; c < ch; c++) h->hist[c][h->pos] = in[c][i];
        h->pos = h->pos + 1u == HB_TAPS ? 0u : h->pos + 1u;
        if (h->phase == 0u) {
            for (unsigned c = 0; c < ch; c++) {
                // 最新の HB_TAPS 標本（古い順に h->pos から）との畳み込み。0 の係数は飛ばす
                const float *x = h->hist[c];
                float acc = coef[HB_MID] * x[(h->pos + HB_MID) % HB_TAPS];
                for (int k = 0; k < HB_TAPS; k += 2) acc += coef[k] * x[(h->pos + (unsigned)k) % HB_TAPS];
                out[c][m] = acc;
            }
            m++;
        }
        h->phase ^= 1u;
    }
    return m;
}

// CBR の出力の大きさ: フレームの数（lame_get_totalframes）× 1 フレームの大きさ（詰め物は LAME の encoder.c と同じ数え方）
static uint32_t cbr_bytes(uint32_t frames, uint32_t rate, int kbps) {
    const uint32_t num = 2u * 72000u * (uint32_t)kbps;   // MPEG-1（version 1）
    const uint32_t base = num / rate, frac = num % rate;
    int32_t slot_lag = (int32_t)frac;
    uint64_t total = 0;
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t pad = 0;
        if ((slot_lag -= (int32_t)frac) < 0) { slot_lag += (int32_t)rate; pad = 1u; }
        total += base + pad;
    }
    return total > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)total;
}

static void lame_quiet(const char *fmt, va_list ap) { (void)fmt; (void)ap; }
static bool setup(w2m_t *w, char *err, size_t n);

w2m_t *w2m_open(const w2m_plat_t *pl, const char *path, const w2m_opts_t *o, char *err, size_t n) {
    w2m_t *w = calloc(1, sizeof *w);
    if (!w) { fail(err, n, "out of memory"); return NULL; }
    w->pl = pl; w->o = *o;
    uint32_t size = 0;
    if (!(w->fh = pl->open(path, &size))) { fail(err, n, "cannot open the WAV file"); w2m_close(w); return NULL; }
    const char *e = parse_wav(w, size);
    if (e) { fail(err, n, e); w2m_close(w); return NULL; }
    return setup(w, err, n) ? w : NULL;
}

w2m_t *w2m_open_pcm(const w2m_plat_t *pl, uint32_t rate, uint32_t frames_total, w2m_pcm_read_t rd, void *ctx,
                    const w2m_opts_t *o, char *err, size_t n) {
    w2m_t *w = calloc(1, sizeof *w);
    if (!w) { fail(err, n, "out of memory"); return NULL; }
    w->pl = pl; w->o = *o;
    w->pcm_rd = rd; w->pcm_ctx = ctx;
    w->fmt = 1; w->ch = 2; w->bits = 16; w->block = 4; w->rate_in = rate; w->frames_in = frames_total;
    return setup(w, err, n) ? w : NULL;
}

// 速さを決めて LAME を用意する（WAV と PCM の共通の後半）。だめなら w を閉じて false
static bool setup(w2m_t *w, char *err, size_t n) {
    if (!pick_rate(w)) {
        snprintf(err, n, "%lu Hz is not supported (32 / 44.1 / 48 kHz and 2x / 4x of them)", (unsigned long)w->rate_in);
        w2m_close(w); return false;
    }
    make_coef(w->coef);
    uint32_t f = w->frames_in;
    for (unsigned s = 0; s < w->stages; s++) f = (f + 1u) / 2u;   // 偶数番目を出す: ceil(n / 2)
    w->frames_out = f;
    const unsigned out_ch = (w->o.mono || w->ch == 1u) ? 1u : 2u;
    if (!(w->gf = lame_init())) { fail(err, n, "out of memory (LAME)"); w2m_close(w); return false; }
    lame_set_errorf(w->gf, lame_quiet); lame_set_debugf(w->gf, lame_quiet); lame_set_msgf(w->gf, lame_quiet);
    lame_set_num_channels(w->gf, (int)out_ch);
    lame_set_in_samplerate(w->gf, (int)w->rate_out);
    lame_set_out_samplerate(w->gf, (int)w->rate_out);
    lame_set_num_samples(w->gf, w->frames_out);
    lame_set_VBR(w->gf, vbr_off);
    lame_set_brate(w->gf, w->o.bitrate);
    lame_set_quality(w->gf, w->o.quality);
    lame_set_mode(w->gf, out_ch == 1u ? MONO : JOINT_STEREO);
    lame_set_bWriteVbrTag(w->gf, 0);                        // Xing / Info の頭は書かない（大きさをフレームの合計だけに）
    lame_set_write_id3tag_automatic(w->gf, 0);
    if (lame_init_params(w->gf) < 0) { fail(err, n, "LAME rejected the settings (bitrate?)"); w2m_close(w); return false; }
    if ((uint32_t)lame_get_out_samplerate(w->gf) != w->rate_out || lame_get_VBR(w->gf) != vbr_off) {
        fail(err, n, "LAME changed the output settings"); w2m_close(w); return false;
    }
    w->total_frames = (uint32_t)lame_get_totalframes(w->gf);
    w->mp3_bytes = cbr_bytes(w->total_frames, w->rate_out, lame_get_brate(w->gf));
    w->rd = malloc(CHUNK * 8u);
    w->l = malloc(CHUNK * sizeof(float)); w->r = malloc(CHUNK * sizeof(float));
    w->mp3_cap = CHUNK * 5u / 4u + 7200u;
    w->mp3 = malloc(w->mp3_cap);
    for (unsigned c = 0; c < 2u; c++) { w->buf1[c] = malloc(CHUNK * sizeof(float)); w->buf2[c] = malloc(CHUNK / 2u * sizeof(float)); }
    if (!w->rd || !w->l || !w->r || !w->mp3 || !w->buf1[0] || !w->buf1[1] || !w->buf2[0] || !w->buf2[1]) { fail(err, n, "out of memory"); w2m_close(w); return false; }
    snprintf(w->desc, sizeof w->desc, "%lu Hz %u bit%s %s -> %lu Hz %d kbps %s, q%d", (unsigned long)w->rate_in, w->bits,
             w->fmt == 3u ? " float" : "", w->ch == 1u ? "mono" : "stereo", (unsigned long)w->rate_out,
             lame_get_brate(w->gf), out_ch == 1u ? "mono" : "joint stereo", w->o.quality);
    return true;
}

uint32_t w2m_size(const w2m_t *w) { return w->mp3_bytes; }
const char *w2m_describe(const w2m_t *w) { return w->desc; }

// 入力の 1 標本を [-1, 1) の float へ
static float sample_at(const w2m_t *w, const uint8_t *p) {
    switch (w->bits) {
    case 8: return ((float)p[0] - 128.0f) * (1.0f / 128.0f);
    case 16: return (float)(int16_t)le16(p) * (1.0f / 32768.0f);
    case 24: return (float)((int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8) * (1.0f / 8388608.0f);
    default:
        if (w->fmt == 3u) { float f; uint32_t u = le32(p); memcpy(&f, &u, 4); return f; }
        return (float)(int32_t)le32(p) * (1.0f / 2147483648.0f);
    }
}

int w2m_encode(w2m_t *w, w2m_sink_t sink, void *ctx, char *err, size_t n) {
    memset(w->hb, 0, sizeof w->hb);
    uint32_t written = 0;
    const unsigned out_ch = (w->o.mono || w->ch == 1u) ? 1u : 2u;
    float *const *buf1 = w->buf1, *const *buf2 = w->buf2;
    for (uint32_t done = 0; done < w->frames_in; ) {
        const uint32_t cnt = w->frames_in - done < CHUNK ? w->frames_in - done : CHUNK;
        if (w->pcm_rd) {                                    // 呼び手から（VGM の合成）。決まった数がそろうまで
            for (uint32_t got = 0; got < cnt; ) {
                const int k = w->pcm_rd(w->pcm_ctx, (int16_t *)w->rd + got * 2u, cnt - got);
                if (k <= 0) {                               // 前もって聞いた長さより早く終わった: 残りは無音（大きさを変えない）
                    if (k < 0) { fail(err, n, "cannot render the input"); return -1; }
                    memset(w->rd + got * 4u, 0, (cnt - got) * 4u);
                    break;
                }
                got += (uint32_t)k;
            }
        } else if (w->pl->read(w->fh, w->data_off + done * w->block, w->rd, cnt * w->block)) { fail(err, n, "cannot read the WAV file"); return -1; }
        for (uint32_t i = 0; i < cnt; i++) {
            const uint8_t *p = w->rd + i * w->block;
            const float a = sample_at(w, p), b = w->ch == 2u ? sample_at(w, p + w->bits / 8u) : a;
            if (out_ch == 1u) buf1[0][i] = w->ch == 2u ? (a + b) * 0.5f : a;
            else { buf1[0][i] = a; buf1[1][i] = b; }
        }
        float *in[2] = { buf1[0], buf1[1] }, *o1[2] = { buf2[0], buf2[1] }, *o2[2] = { buf1[0], buf1[1] };
        unsigned m = cnt;
        float *const *src = in;
        if (w->stages >= 1u) { m = hb_run(w->coef, &w->hb[0], out_ch, in, m, o1); src = o1; }
        if (w->stages >= 2u) { m = hb_run(w->coef, &w->hb[1], out_ch, o1, m, o2); src = o2; }
        for (unsigned i = 0; i < m; i++) {
            w->l[i] = src[0][i];
            w->r[i] = out_ch == 2u ? src[1][i] : src[0][i];
        }
        const int k = m ? lame_encode_buffer_ieee_float(w->gf, w->l, w->r, (int)m, w->mp3, (int)w->mp3_cap) : 0;
        if (k < 0) { snprintf(err, n, "LAME error %d", k); return -1; }
        if (k > 0) { if (sink(ctx, w->mp3, (uint32_t)k)) { fail(err, n, "write failed"); return -1; } written += (uint32_t)k; }
        done += cnt;
        if (w->pl->progress) w->pl->progress(done, w->frames_in);
        if (w->pl->cancel && w->pl->cancel()) { fail(err, n, "canceled"); return -2; }
    }
    const int k = lame_encode_flush(w->gf, w->mp3, (int)w->mp3_cap);
    if (k < 0) { snprintf(err, n, "LAME error %d", k); return -1; }
    if (k > 0) { if (sink(ctx, w->mp3, (uint32_t)k)) { fail(err, n, "write failed"); return -1; } written += (uint32_t)k; }
    if (written != w->mp3_bytes) {
        snprintf(err, n, "internal error: wrote %lu bytes, expected %lu", (unsigned long)written, (unsigned long)w->mp3_bytes);
        return -3;
    }
    return 0;
}

void w2m_close(w2m_t *w) {
    if (!w) return;
    if (w->gf) lame_close(w->gf);
    if (w->fh) w->pl->close(w->fh);
    free(w->rd); free(w->l); free(w->r); free(w->mp3);
    for (unsigned c = 0; c < 2u; c++) { free(w->buf1[c]); free(w->buf2[c]); }
    free(w);
}
