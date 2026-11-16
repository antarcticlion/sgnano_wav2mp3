// 長い処理の進み具合（SDK の部品、2026-11-16。ヘッダだけ）。
// - シェルの前景（SGX_RUN_SHELL）とファイラーの端末（SGX_RUN_FILER）: 1 行を \r で書き替える（全体の %・経過・残りの見込み・出力の大きさ）
// - シェルの背景・パイプの段・ほかのプログラムの子: 10% ごとに 1 行
// - ファイラーのコンテキストメニューの !（SGX_RUN_POPUP）: ファイラーの画面の上の窓（ABI 5.11 の progress_box）。端末には書かない
//   （メッセージは UART のログと窓の段の行へ）。終わったら窓に結果を出し、何かキーで閉じる
// 古いファーム（5.10 まで）では端末の形で出す。
//   sgxp_t P; sgxp_begin(&P, "wav2mp3: a.vgz", "B:/a.mp3");
//   sgxp_update(&P, done, total, out_bytes, out_total);   // 何度呼んでもよい（出すのは 0.5〜1 秒に 1 度）
//   return sgxp_end(&P, rc, err, out_bytes);              // rc: SGXP_OK / SGXP_FAIL / SGXP_CANCELED。戻り値はプログラムの終わりの番号
#ifndef SGX_PROGRESS_H
#define SGX_PROGRESS_H

#include "sgx_api.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern const sgx_api_t *sgx;

enum { SGXP_OK = 0, SGXP_FAIL = 1, SGXP_CANCELED = 2 };

typedef struct {
    const char *title, *file;
    unsigned ctx;                                 // SGX_RUN_*
    bool     box;                                 // 窓に出す
    bool     line;                                // \r で書き替えている行がある（次の文の前に改行）
    uint32_t t0, last_ms;
    int      pct, last_pct;
    uint64_t out, out_total;
    char     stage[96];
} sgxp_t;

static inline bool sgxp_has_511(void) {
    return sgx->size >= offsetof(sgx_api_t, progress_box) + sizeof sgx->progress_box;
}
static inline void sgxp__say(const char *s) { sgx->term_write(s, (unsigned)strlen(s)); }
static inline void sgxp__time(char *o, size_t n, uint32_t s) {
    if (s >= 3600u) snprintf(o, n, "%u:%02u:%02u", (unsigned)(s / 3600u), (unsigned)(s / 60u % 60u), (unsigned)(s % 60u));
    else snprintf(o, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}
static inline void sgxp__size(char *o, size_t n, uint64_t b) {
    if (b >= 1024u * 1024u) snprintf(o, n, "%lu.%lu MB", (unsigned long)(b >> 20), (unsigned long)((b & 0xFFFFFu) * 10u >> 20));
    else snprintf(o, n, "%lu KB", (unsigned long)((b + 1023u) >> 10));
}
static inline uint32_t sgxp_elapsed(const sgxp_t *p) { return (sgx->ms() - p->t0) / 1000u; }
static inline int32_t sgxp__left(const sgxp_t *p) {
    const uint32_t e = sgxp_elapsed(p);
    if (p->pct < 1 || p->pct >= 100 || e < 5u) return -1;
    return (int32_t)((uint64_t)e * (uint64_t)(100 - p->pct) / (uint64_t)p->pct);
}
static inline void sgxp__box(sgxp_t *p, uint32_t flags, const char *err) {
    sgx_progress_t b;
    memset(&b, 0, sizeof b);
    b.title = p->title; b.file = p->file; b.stage = p->stage[0] ? p->stage : NULL;
    b.pct = p->pct; b.elapsed_s = sgxp_elapsed(p); b.left_s = sgxp__left(p);
    b.out_bytes = p->out; b.out_total = p->out_total; b.flags = flags; b.error = err;
    sgx->progress_box(&b);
}
static inline void sgxp__print(sgxp_t *p) {
    char l[160], t[16], r[16], a[24], o[24];
    sgxp__time(t, sizeof t, sgxp_elapsed(p));
    int n = p->pct >= 0 ? snprintf(l, sizeof l, "  %3d%%  %s", p->pct, t) : snprintf(l, sizeof l, "  %s", t);
    const int32_t left = sgxp__left(p);
    if (left >= 0 && n < (int)sizeof l) { sgxp__time(r, sizeof r, (uint32_t)left); n += snprintf(l + n, sizeof l - (size_t)n, "  left ~%s", r); }
    if (p->out && n < (int)sizeof l) {
        sgxp__size(a, sizeof a, p->out);
        if (p->out_total) { sgxp__size(o, sizeof o, p->out_total); n += snprintf(l + n, sizeof l - (size_t)n, "  out %s / %s", a, o); }
        else n += snprintf(l + n, sizeof l - (size_t)n, "  out %s", a);
    }
    if (p->stage[0] && n < (int)sizeof l) n += snprintf(l + n, sizeof l - (size_t)n, "  (%s)", p->stage);
    const bool tty = p->ctx == SGX_RUN_SHELL || p->ctx == SGX_RUN_FILER;
    if (tty) { sgxp__say("\r"); sgxp__say(l); sgxp__say("\033[K"); p->line = true; }
    else { sgxp__say(l); sgxp__say("\n"); }
}

static inline void sgxp_begin(sgxp_t *p, const char *title, const char *file) {
    memset(p, 0, sizeof *p);
    p->title = title; p->file = file;
    p->ctx = sgxp_has_511() ? sgx->run_context() : SGX_RUN_SHELL;
    p->box = p->ctx == SGX_RUN_POPUP;
    p->t0 = sgx->ms(); p->last_ms = p->t0 - 10000u;
    p->pct = -1; p->last_pct = -100;
    if (p->box) sgxp__box(p, 0, NULL);
}
static inline bool sgxp_popup(const sgxp_t *p) { return p->box; }   // 窓に出している（端末には書かない）
// 文（段の変わり目など）: 端末なら 1 行、窓なら UART のログと段の行
static inline void sgxp_msg(sgxp_t *p, const char *s) {
    if (p->box) {
        sgx->log(s, (unsigned)strlen(s)); sgx->log("\n", 1);
        snprintf(p->stage, sizeof p->stage, "%s", s);
        sgxp__box(p, 0, NULL); p->last_ms = sgx->ms();
        return;
    }
    if (p->line) { sgxp__say("\r\033[K"); p->line = false; }
    sgxp__say(s); sgxp__say("\n");
    if (p->pct >= 0 && (p->ctx == SGX_RUN_SHELL || p->ctx == SGX_RUN_FILER)) sgxp__print(p);
}
// 段の名前（表示の終わりに括弧で。NULL で消す）
static inline void sgxp_stage(sgxp_t *p, const char *s) { snprintf(p->stage, sizeof p->stage, "%s", s ? s : ""); p->last_ms = sgx->ms() - 10000u; }
// done / total（全体の割合）と、書いた出力の大きさ
static inline void sgxp_update(sgxp_t *p, uint64_t done, uint64_t total, uint64_t out, uint64_t out_total) {
    const int pct = total ? (int)(done * 100u / total) : -1;
    p->pct = pct > 100 ? 100 : pct;
    p->out = out; p->out_total = out_total;
    const uint32_t now = sgx->ms();
    if (p->box || p->ctx == SGX_RUN_SHELL || p->ctx == SGX_RUN_FILER) {
        if (now - p->last_ms < (p->box ? 500u : 1000u)) return;
        p->last_ms = now;
        if (p->box) {
            const bool stopping = sgx->stop_requested();
            sgxp__box(p, stopping ? SGX_PB_STOPPING : 0u, NULL);
        } else sgxp__print(p);
    } else if (p->pct >= p->last_pct + 10) {
        p->last_pct = p->pct - p->pct % 10;
        sgxp__print(p);
    }
}
// 終わり。窓なら結果を出して何かキーを待つ（止めたときは待たない: ファームが止めに来る）。戻り値はプログラムの終わりの番号
static inline int sgxp_end(sgxp_t *p, int rc, const char *err, uint64_t out) {
    p->out = out;
    if (rc == SGXP_OK) p->pct = 100;
    if (p->box) {
        sgxp__box(p, rc == SGXP_OK ? SGX_PB_DONE : rc == SGXP_CANCELED ? SGX_PB_CANCELED : SGX_PB_FAILED, err);
        if (rc == SGXP_CANCELED || sgx->stop_requested()) return 0;
        while (sgx->key(false) >= 0) { }
        (void)sgx->key(true);
        return 0;                                 // 窓で結果を見せたので、ファイラーへそのまま戻る
    }
    if (p->line) { sgxp__say("\r\033[K"); p->line = false; }
    if (rc == SGXP_OK) return 0;
    if (rc == SGXP_CANCELED) { sgxp__say("canceled\n"); return 1; }
    if (err) { sgxp__say("error: "); sgxp__say(err); sgxp__say("\n"); }
    return 1;
}

#endif
