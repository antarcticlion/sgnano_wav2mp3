// 言語のヒープの共通の決まり（2026-11-03）。
//
// どの言語も 64KB から始め、空きが 32KB を切ったら 64KB 増やす。空きが 128KB に
// なったら、まるごと空いた 64KB の塊を返す。スクリプトはもっと大きな最小値を
// 指定できる（例: 512KB を必ず確保する）。言語をまたいで同じ値を使うため、
// ファームの Ruby / Python / BASIC と SDK の SGX アプリがこのヘッダを見る。
#ifndef SGNANO_HEAP_H
#define SGNANO_HEAP_H

#define SGNANO_HEAP_MIN    (64u * 1024u)    // 最小（言語の既定）
#define SGNANO_HEAP_CHUNK  (64u * 1024u)    // 増減の単位
#define SGNANO_HEAP_LOW    (32u * 1024u)    // これ未満で増やす
#define SGNANO_HEAP_HIGH   (128u * 1024u)   // これ以上で減らす
#define SGNANO_HEAP_MAX    (4u * 1024u * 1024u)   // 1 つのヒープの上限（PSRAM 8MB の一部）

#endif
