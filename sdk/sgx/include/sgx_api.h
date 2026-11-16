// sgnano の外部実行ファイル（SGX）の ABI（2026-10-24）。使い方は docs/sgx_abi.md。
//
// 外部実行ファイル（.sgx）は再配置できる（番地 0 に結び、再配置の表を持つ）。ファームが PSRAM のどこか（キャッシュを通る窓）に置いて番地を直し、
// M33BEATOS のタスクとして走らせる。いくつものプログラム・タスクを同時に走らせ、協調させられる（spawn・task_create・mutex など）。
// ファームの機能は、入口に渡される関数の表（sgx_api_t）だけから使う（ファームのシンボルには結ばない）。
// 表は ABI の版（major.minor）を持ち、minor を上げるときは末尾に足すだけにする（並びを変えない）。major が違えば読み込まない
// （【2026-11-06】4.0 だけは例外: 3.x の表の末尾に足しただけなので、ファームは 3.x のプログラムも読み込む。SGX_ABI_COMPAT_MAJOR）。
//
// 呼び出しの決まり: AAPCS（ARMv8-M Mainline、Thumb、浮動小数点は softfp = 引数・戻り値は整数のレジスタ）。
// どの関数も、プログラムのタスク（入口を呼ばれたタスク）からだけ呼ぶ。割り込みは持てない。
// このファイルは sgnano の SDK の一部で、MIT ライセンス（プログラムのライセンスは自由に選んでよい）。
#ifndef SGX_API_H
#define SGX_API_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sgx_overlay.h"                       // オーバーレイの型（ABI 4.1。SDK の部品 sgx_overlay.c と同じもの）

#define SGX_ABI_MAJOR 5
#define SGX_ABI_MINOR 11                         // 5.11（2026-11-16）: run_context（ファイラー / シェル / パイプ / 子）と progress_box（ファイラーの上の進み具合の窓）
                                                 // 5.10（2026-11-15）: 表計算のマクロ macro_run（mruby / MicroPython / SG BASIC の Sheet からセルを読み書き）
                                                 // 5.9                          // 5.9（2026-11-15）: IME（ime_key / ime_on / ime_composing / ime_draw / ime_state / ime_cancel）、キー SGX_KEY_IME・MENU・GUI_L / R
                                                 // 5.8（2026-11-15）: set_mode の印 SGX_MODE_NO_LR（表は 5.7 と同じ）
                                                 // 5.7（2026-11-15）: music_pcm_open / read / close（VGM / VGZ を 44.1kHz の PCM として受け取る）。
                                                 // 5.6（2026-11-14）: create_file・ra_write（大きさの決まったファイルを作り、その場へ書く）を末尾に足した。
                                                 // 5.5（2026-11-13）: music_play_mem（メモリの VGM）を末尾に足した。
// 5.4（2026-11-13）: set_mode の印 SGX_MODE_LR_SMALL（表は 5.3 と同じ）。
// 5.3                          // 5.3（2026-11-13）: FB のプリミティブ描画（gfx_*、SPEC_gfx.md）を末尾に足した。
                                                 // 5.2（2026-11-11）: RP2350 内蔵温度センサ（adc_temperature_mc）を末尾に足した。
                                                 // 5.1（2026-11-11）: ADC（単発読み取りと、ピンを選べる連続取り込み）を末尾に足した。
                                                 // 5.0（2026-11-11）: GPIO と PIO2（ユーザー開放）を末尾に足した。
                                                 //      4.x の表の末尾に足しただけなので、ファームは 4.x のプログラムも読み込む（SGX_ABI_COMPAT_MAJOR）
                                                 // 4.13（2026-11-11）: MIDI（midi_available/read/write/bytes/usb/route）を足した
                                                 // 4.11（2026-11-10）: bm_flush（キャッシュ付き PSRAM ビットマップの書き戻し）を末尾に足した
                                                 // 4.10（2026-11-10）: bm_page_back / bm_page_flip（モード 7 のページ切替ダブルバッファ）を末尾に足した
                                                 // 4.9（2026-11-10）: music_tap（鳴っている PCM の直近）と bm_wrap_clear / bm_wrap_set（ビットマップの横循環）を末尾に足した
                                                 // 4.8（2026-11-09）: mouse_warp（システムカーソルを画面座標へ動かす）を末尾に足した
                                                 // 4.7（2026-11-09）: music_render_wav / music_render_progress（VGM/VGZ -> 96kHz/24bit WAV）を末尾に足した
                                                 // 4.6（2026-11-08）: music_sn_pct（SN76489 の MIX）と music_voice_chip（ボイスごとのチップ）を末尾に足した
                                                 // 4.5（2026-11-08）: 音楽の MIX 設定（music_settings_reload / music_ssg_pct）を末尾に足した（4.4 = 2026-11-07 音楽の口）
                                                 // 4.3（2026-11-07）: fb_lend_spare をモード 7 でも貸す・貸している間もモード 0・1・2・7 へは替えてよい（表は 4.2 と同じ）
                                                 // 4.2（2026-11-07）: voice ごとの stream_* を末尾に足した。4.1 以前のプログラムはそのまま動く
                                                 // 4.0（2026-11-06）: ブロックストア（store_*）・FB の余りのページを借りる fb_lend_spare と SGX_EBUSY を足した。表の並びは 3.5 の後ろに足しただけ。
                                                 //      ファームは 3.x のプログラムもそのまま走らせる（SGX_ABI_COMPAT_MAJOR）。4.0 で組んだものは 4.x のファームだけ
                                                 // 3.5（2026-11-06）: stderr へ書く term_write_err を末尾に足した（3.4 は request_tracker）
                                                 // 3.3（2026-11-04）: 組み込みアプリの UI の訳（translate）と SGX_F_LOCALIZE を足した
                                                 // 3.2（2026-11-03）: S: のクラスタ地図とデフラグ・エンフラグ・0 クリア（sysdisk_*）を末尾に足した
                                                 // 3.1（2026-11-02）: メモリの使用量（mem_info）とタスクの一覧（task_info）を末尾に足した
                                                 // 3.0（2026-10-31）: ローレゾのマップを 1 ます 16 ビットに（lr_map は uint16_t *）。表の並びは 2.0 と同じ。2.x で組んだものは読み込まない
                                                 // 2.0（2026-10-31）: 1.x の区切り（ユーザー判断）

#define SGX_ABI_COMPAT_MAJOR 4                   // ファームが読み込むもう 1 つの major（5.0 は 4.x の上位互換）

// ---- 実行ファイルの形 ----
#define SGX_MAGIC       0x31584753u              // "SGX1"
#define SGX_LINK_BASE   0x00000000u              // 結ぶ番地（sgx.ld）。読み込んだら置いた番地との差を表の語に足す
#define SGX_LOAD_MAX    (4u * 1024u * 1024u)     // 像（コード・定数・初めの値）と .bss の合計の上限
#define SGX_F_ESC_KEY   0x0001u                  // ESC をプログラムに渡す（ファームは ESC で止めない。止めるのは Shift+ESC）
#define SGX_F_SRAM_STACK 0x0002u                 // スタックを SRAM に（速い。取れなければ PSRAM）
#define SGX_F_WAIT_KEY  0x0004u                  // ふつうに（0 で）終わっても、画面をそのままにして何かキーを待ってからファイラーへ（Ruby と同じ。2026-10-25）
#define SGX_F_FULL_TEXT 0x0008u                  // テキスト面の 30 行すべてをアプリが使う（0〜29 行目。2026-11-04）
#define SGX_F_LOCALIZE  0x0010u                  // 組み込みアプリの内部 UI も表示言語へ（2026-11-04）

// ---- ブロックストア（ABI 4.0、2026-11-06）----
// ファイルを決まった大きさのブロックの並びとして、PSRAM のキャッシュに必要な分だけ載せて読み書きする。
// PSRAM に収まらない大きなデータ（メッシュ・テクスチャ・チェックポイント）をストレージに置くため。
// ブロックは生のポインタでなくストアの番号とブロックの番号で指し、使う間だけ get で取り出して release で返す。
//
// - 置き場はファイル 1 つ（USB ストレージの FAT12/16/32・exFAT・NTFS。S: は読むだけ。CD・フロッピーは使えない）。
//   開いている間、ファイルの大きさと置き場は変えない（ほかのプログラム・シェルから書き換えない・消さない）。
// - 書き込みは、ファイルの中のセクタをその場で書き換える（大きさ・置き場・日時は変わらない。ファイルの属性の
//   読み出し専用は開くときに断る）。書くのは、追い出すとき・store_flush・store_close・プログラムが終わるとき。
// - どの関数も、プログラムのどのタスクから呼んでもよい（中で順番に並べる）。待つ間（読み書き）は眠る。
// - ブロックの中身の番地はキャッシュを通る窓（0x11...）。release するまで動かない・追い出されない。
#define SGX_STORE_READONLY  0x0001u   // 読むだけ（release の dirty は -SGX_EROFS）
#define SGX_STORE_WRITEBACK 0x0002u   // 書いたブロックを書き戻す（READONLY でなければ同じ扱い。書き方をはっきりさせる印）
#define SGX_STORE_CREATE    0x0004u   // ファイルが無ければ 0 で埋めて作る（block_size x block_count。1MB に約 2 秒）
#define SGX_STORE_RAW       0x0008u   // 予約（生の区画。4.0 では -SGX_ENOTSUP）
#define SGX_STORE_COMPRESS  0x0010u   // 予約（圧縮。4.0 では -SGX_ENOTSUP）
#define SGX_STORE_MAX       8u        // 1 回の実行で同時に開けるストア
#define SGX_STORE_BLOCK_MIN 512u
#define SGX_STORE_BLOCK_MAX (256u * 1024u)
#define SGX_STORE_CACHE_MAX 256u      // 1 つのストアのキャッシュのブロック数の上限

typedef struct {
    const char *path;       // ファイル（"A:/dir/name" か、プログラムのフォルダからの相対）
    uint32_t block_size;    // 512〜256KB の 2 のべき乗
    uint32_t block_count;   // ブロックの数。0 = 既存のファイルの大きさから（大きさ / block_size、端は使わない。CREATE とは組めない）
    uint32_t cache_blocks;  // キャッシュのブロック数（1〜SGX_STORE_CACHE_MAX）。PSRAM に block_size x cache_blocks を取る
    uint32_t flags;         // SGX_STORE_*
    uint32_t reserved[3];   // 0 にしておく
} sgx_store_config_t;

typedef struct {
    uint32_t block_size, block_count, cache_blocks;
    uint32_t resident_blocks, pinned_blocks, dirty_blocks;
    uint32_t hits, misses, evictions, writebacks, errors;
    uint32_t bytes_read, bytes_written;
    uint32_t max_wait_us;   // 1 回の get / flush で待った最長
} sgx_store_stats_t;

struct sgx_macro_sheet;                           // 5.10 の表計算のマクロ（下で定義）
struct sgx_progress;                              // 5.11 の progress_box（定義は表の後ろ）
typedef struct sgx_api sgx_api_t;
typedef int (*sgx_entry_t)(const sgx_api_t *api, int argc, char **argv);

typedef struct {
    uint32_t magic;                              // SGX_MAGIC
    uint16_t abi_major, abi_minor;               // 作ったときの SGX_ABI_MAJOR / MINOR（ファームの minor 以下であること）
    uint32_t header_size;                        // sizeof(sgx_header_t)
    uint32_t link_base;                          // SGX_LINK_BASE
    uint32_t image_size;                         // 像の大きさ（見出しも入れて）。ファイルはこの後ろに再配置の表（reloc_count 個の 32 ビット）が続く。像の後ろを .bss として 0 にする
    uint32_t bss_size;
    uint32_t entry;                              // sgx_entry_t の番地（Thumb なので奇数）
    uint32_t stack_size;                         // タスクのスタック（0 なら 16KB）
    uint32_t flags;                              // SGX_F_*
    char     name[32];                           // 表示に使う名前（UTF-8、0 で終わる）
    uint32_t reloc_count;                        // 再配置の表の数（像の頭からのバイトの位置。その 32 ビットの語に、置いた番地 - link_base を足す）。sgx_link.py が書く
    uint32_t reserved[3];
} sgx_header_t;

// ---- 色・画面 ----
// 画素の値（RGB555 の形。ファームの決まった並び）は rgb() で作る。CLUT8・CLUT4 の FB とビットマップは色の番号
enum {
    SGX_MODE_OFF = 0,           // 表示なし（同期だけ）
    SGX_MODE_TEXT = 1,          // テキスト（80x30、24kHz は 80x25）
    SGX_MODE_TEXT_FB = 2,       // テキスト + FB（RGB555、640x480）
    SGX_MODE_TEXT_LR_C8 = 3,    // テキスト + ローレゾ + FB（CLUT8）
    SGX_MODE_TEXT_LR_C4 = 4,    // テキスト + ローレゾ + FB（CLUT4）
    SGX_MODE_TEXT_LR = 5,       // テキスト + ローレゾ
    SGX_MODE_LR = 6,            // ローレゾだけ
    SGX_MODE_TEXT_BM8 = 7,      // テキスト（25 行）+ 320x200x8 のビットマップ
};
#define SGX_MODE_NO_TEXT 0x100                   // set_mode に足す: テキスト面を開かない・閉じる（SRAM が約 52KB 空く。ビットマップの 2 枚の面などに）
#define SGX_MODE_BM_CACHED 0x200                 // set_mode に足す（4.11）: モード 7 のビットマップを PSRAM のキャッシュ窓に取る。
                                                 // 書いたら bm_flush で PSRAM へ書き戻す（テキスト面を開いたまま OGG を鳴らすため）
#define SGX_MODE_LR_SMALL 0x800                  // set_mode に足す（5.4、2026-11-13）: ローレゾ面はパターン 16KB でよい（VGM の合成の区画を借りずに開ける。
                                                 // 足さないとき、借りずに開く形は 32KB・16 本以上。テキスト面のあるモード 3〜6 で VGM を鳴らすなら足す）
#define SGX_MODE_NO_LR 0x1000                    // set_mode に足す（5.8、2026-11-15）: モード 3〜5 でもローレゾ面を開かない（テキスト + FB だけ。
                                                 // ローレゾ面の 50〜60KB の SRAM をテキスト面などに回す。SGCALC）
#define SGX_MODE_TEXT_SMALL 0x400                // set_mode に足す（4.12）: モード 7 のテキスト面を 25 行・グリフ 19 行にする。
                                                 // 約 24KB の SRAM が空き、64KB のビットマップを SRAM に置ける（mplay）。
                                                 // 4.12 より前のファームはこのビットを無視する（30 行のまま）
#define SGX_FB_W 640
#define SGX_FB_H 480
#define SGX_BM_W 320
#define SGX_BM_H 200
#define SGX_LR_W 320
#define SGX_LR_H 240
#define SGX_LR_OBJS 128
#define SGX_LR_MAP_W 64
#define SGX_LR_MAP_H 64
#define SGX_LR_COLORS 128
#define SGX_LR_TRANSPARENT 0x80u                 // パターンの画素の bit7 = 透過
#define SGX_LR_ME_TILE  0x03FFu                  // マップのます（16 ビット、ABI 3.0）: bit0-9 タイル（0〜1023 = パターンの先頭 64KB）、bit10 横反転、bit11 縦反転、bit12-15 パレット（0〜15）
#define SGX_LR_ME_HFLIP 0x0400u
#define SGX_LR_ME_VFLIP 0x0800u
#define SGX_LR_ME_PAL_SHIFT 12
#define SGX_LR_ME_TILES 1024u                    // BG から指せるタイルの数（オブジェクトはパターン全体）
#define SGX_LR_ME_PALS  16u                      // BG から指せるパレットの数（オブジェクトは lr_palettes() 本）
#define SGX_LR_OBJ_SHOW 0x4000u                  // obj.attr: bit0-7 高さ-1、bit8-9 幅（0=8 1=16 2=32 3=64）、bit10-11 優先度、bit12 横反転、bit13 縦反転、bit14 表示
typedef struct { int16_t x, y; uint16_t pattern, attr; uint32_t attr2; } sgx_lr_obj_t;
typedef struct { bool bg[2], obj; uint16_t sx[2], sy[2]; } sgx_lr_ctrl_t;

// テキスト面のセルの属性（32 ビット。src/video/text.h と同じ）
#define SGX_T_FG(c)      ((uint32_t)(c) & 15u)   // 文字色（16 色、PC-98 の並び）
#define SGX_T_BG(c)      (((uint32_t)(c) & 15u) << 4)
#define SGX_T_FG_TRANS   0x00000200u
#define SGX_T_BG_TRANS   0x00000400u
#define SGX_T_REVERSE    0x00000800u
#define SGX_T_BLINK      0x00001000u
#define SGX_T_UNDER      0x00002000u
#define SGX_T_STRIKE     0x00004000u
#define SGX_T_OVER       0x00008000u
#define SGX_T_BOLD       0x00010000u
#define SGX_T_ROT90      0x00020000u
#define SGX_T_ROT180     0x00040000u
#define SGX_T_ROT270     0x00060000u
#define SGX_T_DH         0x00080000u             // 縦倍角
#define SGX_T_DW         0x00100000u             // 横倍角
#define SGX_T_LEFT_LINE  0x00800000u             // 左ライン（2026-10-27）
#define SGX_T_RIGHT_LINE 0x01000000u             // 右ライン

typedef struct {
    const char *timing, *mode_name, *fb_name;
    uint32_t line_hz, frame_hz_x100;
    uint32_t frames, underrun, fifo_stall, recover, late_start, late_render;
    uint32_t pair_us[2];
} sgx_video_info_t;
typedef struct {
    uint32_t uptime_s, mem_free, mem_largest;    // 起動からの秒・SRAM のアリーナの空きの合計と最大の塊
    uint32_t usb_devices, hubs, pads, usb_reinit, usb_wedged;
    bool     kbd, mouse;
    uint16_t kbd_vid, kbd_pid;
    uint8_t  kbd_leds;
} sgx_sys_info_t;

// ---- 入力 ----
// キーのコード: 文字は ASCII、特殊キーは 0x80 以上（src/input/keycode.h と同じ）
#define SGX_KEY_F(n)     (0x80 + (n))            // F1〜F12 = 0x81〜0x8C
#define SGX_KEY_UP       0x90
#define SGX_KEY_DOWN     0x91
#define SGX_KEY_LEFT     0x92
#define SGX_KEY_RIGHT    0x93
#define SGX_KEY_PGUP     0x94
#define SGX_KEY_PGDN     0x95
#define SGX_KEY_HOME     0x96
#define SGX_KEY_END      0x97
#define SGX_KEY_INS      0x98
#define SGX_KEY_IME      0x99                    // Ctrl+Space（IME のオン・オフ）
#define SGX_KEY_MENU     0x9B                    // メニューキー（アプリケーションキー）
#define SGX_KEY_GUI_L    0x9C                    // 左の GUI キー（Windows キー）を押した（5.9、2026-11-15。押した瞬間に 1 回）
#define SGX_KEY_GUI_R    0x9D                    // 右の GUI キー
#define SGX_KEY_ENTER    0x0D
#define SGX_KEY_BS       0x08
#define SGX_KEY_TAB      0x09
#define SGX_KEY_ESC      0x1B
#define SGX_KEY_DEL      0x7F
#define SGX_KEY_UART     0x10000                 // key の結果に付く: UART（端末）から来たキー（押した知らせだけで、離した知らせは無い）
#define SGX_KEY_SHIFT    0x100                   // key_down で使う修飾キー
#define SGX_KEY_CTRL     0x101
#define SGX_KEY_ALT      0x102
typedef struct { bool connected; uint8_t buttons; int32_t x, y, wheel; } sgx_mouse_t;   // buttons: bit0 左・bit1 右・bit2 中。x, y は累計
typedef struct { bool connected, ready, proximity, tip, button1, button2; uint8_t tool, pad; uint16_t x, y, pressure, x_max, y_max, pressure_max;
                 int32_t wheel; const char *name; } sgx_tablet_t;
#define SGX_PAD_MAX  4
#define SGX_PAD_AXES 6
typedef struct { bool connected; uint16_t vid, pid; uint8_t hat, buttons_n, axes_mask; uint32_t buttons; int16_t axis[SGX_PAD_AXES]; } sgx_pad_t;
// hat: 0 = 上・時計回り・8 以上 = 中立。buttons: F310 の D モードの並び（bit0 X・1 A・2 B・3 Y・4 LB・5 RB・6 LT・7 RT・8 BACK・9 START）。axis は -127〜127

// ---- 音 ----
enum { SGX_WAVE_SINE = 0, SGX_WAVE_TRIANGLE, SGX_WAVE_SAW, SGX_WAVE_SQUARE, SGX_WAVE_PULSE25, SGX_WAVE_ORGAN, SGX_WAVE_NOISE };   // src/audio/audio.h と同じ
enum { SGX_SND_WT = 0, SGX_SND_PCM, SGX_SND_STREAM, SGX_SND_NOISE };   // NOISE は 4ch 目のドラム用（2026-10-29）
enum { SGX_PCM_U8 = 0, SGX_PCM_S16 = 1 };        // pcm_play の標本の形（モノラル）

// ---- ファイル ----
// パスは "A:/dir/name"（ドライブ文字・コロン・スラッシュ）か、プログラムのフォルダからの相対。UTF-8
enum {                                            // 誤り（src/ruby/rbfs.h と同じ並び）
    SGX_OK = 0, SGX_ENOENT, SGX_EEXIST, SGX_EACCES, SGX_ENOTEMPTY, SGX_ENOTDIR, SGX_EISDIR, SGX_EROFS, SGX_ENOSPC, SGX_EIO,
    SGX_ENOTSUP, SGX_EINVAL, SGX_ENOMEM, SGX_ENODEV,
    SGX_EBUSY,                                    // 4.0: 使っている（取り出し中のブロックがある・キャッシュがすべて取り出し中）
    SGX_EMACRO,                                   // 5.10: マクロの構文・実行の誤り（文は macro_run の err）
};
#define SGX_ATTR_RO   0x01u
#define SGX_ATTR_HID  0x02u
#define SGX_ATTR_SYS  0x04u
#define SGX_ATTR_DIR  0x10u
#define SGX_ATTR_ARC  0x20u
typedef struct { bool is_dir; uint8_t attr; uint32_t size, mtime, ctime, atime; } sgx_stat_t;   // 日時は 1970 年からの UTC の秒
typedef struct { const char *name; unsigned name_len; bool is_dir; uint8_t attr; uint32_t size, mtime; } sgx_dirent_t;
typedef void (*sgx_dirent_fn)(const sgx_dirent_t *e, void *user);
typedef struct sgx_file sgx_file_t;              // 大きいファイルを好きな位置から読む口（ra_*）
// 音楽の口（ABI 4.4）の様子
#define SGX_MUSIC_NOCOPY 0x0100u                 // music_play_mem（5.5）: data（PSRAM）を写さずに使う。music_stop（か次の曲）まで返さないこと
#define SGX_MUSIC_LOOP  0x0001u                  // くり返す（VGM はループの点からいつまでも、ほかは頭から）。無ければ VGM は 2 回まわして終わる
enum { SGX_MUSIC_IDLE = 0, SGX_MUSIC_PLAYING, SGX_MUSIC_PAUSED, SGX_MUSIC_ENDED, SGX_MUSIC_ERROR };
enum { SGX_MUSIC_FMT_NONE = 0, SGX_MUSIC_FMT_MP3, SGX_MUSIC_FMT_OGG, SGX_MUSIC_FMT_WAV, SGX_MUSIC_FMT_VGM };
#define SGX_VGM_YM2203   0x0001u                 // sgx_music_info_t の chips
#define SGX_VGM_YM2203x2 0x0002u
#define SGX_VGM_YM2151   0x0004u
#define SGX_VGM_YM2149   0x0008u
#define SGX_VGM_OKIM6258 0x0010u
#define SGX_VGM_SEGAPCM  0x0020u
#define SGX_VGM_SN76489  0x0040u
#define SGX_VGM_YM2612   0x0080u
#define SGX_VGM_NES_APU  0x0100u
#define SGX_VGM_AY8910   0x0200u
#define SGX_VGM_YM2610   0x0400u
#define SGX_VGM_YM2610B  0x0800u
#define SGX_VGM_MSM5205  0x1000u
#define SGX_VGM_AY8910x2 0x2000u                 // 2 台目（表示用。チップのビットは 1 台目と同じ扱い）
#define SGX_VGM_YM2149x2 0x4000u
#define SGX_VGM_SN76489x2 0x8000u
#define SGX_VGM_YM2608   0x10000u                // OPNA。ADPCM-A のリズムは同梱の ROM（Tetorica、BSD-3-Clause）
#define SGX_VGM_YM3812   0x20000u                // OPL2
#define SGX_VGM_YM3526   0x40000u                // OPL
#define SGX_VGM_YMF262   0x80000u                // OPL3
#define SGX_VGM_C140     0x100000u               // Namco C140（PCM）
#define SGX_VGM_FDS      0x200000u               // ファミコンの FDS（波形 1 声）
#define SGX_VGM_HUC6280  0x400000u               // PC エンジンの HuC6280（波形 6 声）
#define SGX_VGM_K051649  0x800000u               // コナミ SCC（波形 5 声）
#define SGX_VGM_OKIM6295 0x1000000u              // OKI M6295（ADPCM 4 声）
#define SGX_VGM_K054539  0x2000000u              // コナミ K054539（PCM 8 声）
#define SGX_VGM_C352     0x4000000u              // Namco C352（PCM 32 声）
#define SGX_VGM_YM2612x2 0x8000000u              // YM2612 / YM3438 の 2 台目（2026-11-13）
#define SGX_VGM_RF5C68   0x10000000u             // RF5C68（PCM 8 声。セガ System 18・FM TOWNS）
#define SGX_VGM_RF5C164  0x20000000u             // RF5C164（PCM 8 声。メガ CD）
typedef struct {
    uint8_t  state, format, voices, loops;       // SGX_MUSIC_* / SGX_MUSIC_FMT_* / 使っているポートの数（0 から） / まわった回数
    uint32_t rate, chips;                        // 標本の速さ（VGM は 44100） / VGM のチップ（SGX_VGM_*）
    uint32_t pos_ms, len_ms;                     // 経過・長さ（分からなければ 0。VGM は 2 回まわしの長さ）
    uint16_t load_pct, underruns;                // 作るのにかかった時間 / 曲の時間（%）・追い越された回数
    char     title[96], game[96], system[64], author[64];   // VGM の GD3（UTF-8）。ほかはファイルの名前を title に
    char     error[64];
} sgx_music_info_t;
typedef struct { char letter; uint8_t type; bool ready; } sgx_drive_t;   // type: 1 FAT（USB ストレージ・S:）・2 FAT（フロッピー）・3 exFAT・4 NTFS・5 CD（ISO9660・音楽）・6 そのほか
typedef struct { char vendor[9], product[17]; uint32_t mb; } sgx_storage_t;

// ---- メモリとタスク（ABI 3.1、2026-11-02）----
// メモリのアリーナ（SRAM / PSRAM）と、アプリが借りている量、sgsh のヒープの状態。バイト単位
typedef struct {
    uint32_t sram_total, sram_free, sram_largest;     // SRAM のアリーナ
    uint32_t psram_total, psram_free, psram_largest;  // PSRAM のアリーナ
    uint32_t app_count, app_sram_bytes, app_psram_bytes;   // 走っているアプリが借りているもの
    uint32_t shell_heap_free, shell_heap_total;       // sgsh のヒープ（sgsh が走っていなければ 0）
    uint32_t shell_backlog_lines;                     // sgsh のバックログの行数
} sgx_mem_info_t;
// タスクの状態（src/rtos/m33beatos.h の MB_T_* と同じ並び）
enum { SGX_TASK_READY = 1, SGX_TASK_BLOCKED, SGX_TASK_SLEEP, SGX_TASK_DEAD };
typedef struct {
    char     name[16];                    // 名前（0 で終わる。長ければ切る）
    uint8_t  state;                       // SGX_TASK_*
    uint8_t  prio, base_prio;             // いまの優先度（貸し出し中は上がる）・決めた優先度
    uint8_t  current;                     // 1 = いま走っている
    uint8_t  suspended;                   // 1 = 止められている
    uint8_t  qmi;                         // QMI（フラッシュ・PSRAM）の印
    uint16_t reserved;
    uint32_t stack_size, stack_unused;    // スタックの大きさ・使っていない量
    uint32_t run_us, switches;            // 走った時間の合計（µs）・走り出した回数
} sgx_task_info_t;

// ---- GPIO と PIO2（ABI 5.0、2026-11-11）----
// GPIO は SIO の入出力。ピンは 0〜47。空きピンは基板で違う（C ボードは GP34〜41、P ボードは GP42/43・GP46/47。
// GP44/45 はコンソール、C の 42/43 は MIDI、46/47 は ADC）。ファームが使っているピンは触らないこと。
enum { SGX_GPIO_IN = 0, SGX_GPIO_OUT = 1 };
enum { SGX_GPIO_PULL_NONE = 0, SGX_GPIO_PULL_UP, SGX_GPIO_PULL_DOWN };
// io_bank0 の機能の番号（RP2350）。SIO = 5、PIO2 = 8。SGX_GPIO_FUNC_NULL で機能を外す
// GP42/43 の UART1 は UART_AUX（11）。GPIO_FUNC_UART（2）では繋がらない。
enum {
    SGX_GPIO_FUNC_HSTX = 0, SGX_GPIO_FUNC_SPI, SGX_GPIO_FUNC_UART, SGX_GPIO_FUNC_I2C,
    SGX_GPIO_FUNC_PWM, SGX_GPIO_FUNC_SIO, SGX_GPIO_FUNC_PIO0, SGX_GPIO_FUNC_PIO1,
    SGX_GPIO_FUNC_PIO2, SGX_GPIO_FUNC_GPCK, SGX_GPIO_FUNC_USB, SGX_GPIO_FUNC_UART_AUX,
    SGX_GPIO_FUNC_NULL = 0x1F,
};
// PIO2 はユーザー用（PIO0 = 映像、PIO1 = 音源 0〜3）。1 つのプログラムが占有する。
// GPIOBASE は 16 なので、PIO2 が使えるピンは 16〜47。命令は pioasm（RP2350、絶対ピン番号）の語をそのまま渡す。
#define SGX_PIO2_SM_MAX   4
#define SGX_PIO2_PIN_MIN  16
#define SGX_PIO2_PIN_MAX  47
enum { SGX_PIO_FIFO_JOIN_NONE = 0, SGX_PIO_FIFO_JOIN_TX = 1, SGX_PIO_FIFO_JOIN_RX = 2 };   // hardware/pio.h と同じ
enum { SGX_PIO_SHIFT_RIGHT = 0, SGX_PIO_SHIFT_LEFT = 1 };

// ---- ADC（ABI 5.1/5.2、2026-11-11）----
// P ボード: GP40/41（ADC0/1）が L/R。C ボード: GP46/47（ADC6/7）が専用 L/R、GP40/41（A6/A7、ADC0/1）も選べる。
// adc_start の l は低い側（偶数チャンネル）、r = l + 1。l = r = 0 は基板の既定の L/R。
// ABI 5.2: adc_temperature_mc で RP2350 内蔵温度センサ（ADC のチャンネル 8）を読む。
#define SGX_ADC_MAX_PINS 4
#define SGX_ADC_CAPTURE_MAX (96000u * 10u)   // adc_capture の frames の上限（10 秒。ABI 5.2 で 16384 から広げた）
typedef struct {
    uint32_t frames, dropped, underruns;              // 取り込んだフレーム・捨てたフレーム・再生の途切れ
    uint16_t peak[2], dc[2];                          // 前回の adc_stats からの振れ幅（読むと 0 に戻る）と直流分（12 ビット）
    uint32_t fifo_over;                               // ADC FIFO があふれた回数（左右がずれるおそれ）
} sgx_adc_stats_t;

// ---- タスクとプログラム（協調）----
typedef struct sgx_task sgx_task_t;               // プログラムの中のタスク（入口の main もその 1 つ）
typedef struct sgx_mutex sgx_mutex_t;
typedef struct sgx_sem sgx_sem_t;
typedef struct sgx_queue sgx_queue_t;
typedef struct sgx_socket sgx_socket_t;           // TCP / TLS のソケット（ABI 1.7）
#define SGX_WAIT_FOREVER 0xFFFFFFFFu             // 待つ関数の timeout_us に（止める頼みが来れば、待っている所で止まる）

struct sgx_api {
    uint16_t abi_major, abi_minor;
    uint32_t size;                                // この表の大きさ（バイト）。新しい関数は size を見てから使う

    // ==== 実行 ====
    void     (*exit)(int code);                   // 終わる（戻らない）。入口から戻っても同じ
    bool     (*stop_requested)(void);             // 止めてほしいと頼まれた（ESC・Shift+ESC）。長い処理の区切りで見る
    void     (*sleep_us)(uint32_t us);            // 眠る（ほかのタスクが走る）
    void     (*yield)(void);                      // 少し譲る（1ms 眠る）
    uint32_t (*ms)(void);                         // 起動からのミリ秒
    uint64_t (*us)(void);                         // 起動からのマイクロ秒
    uint64_t (*utc_us)(void);                     // いまの日時（1970 年からの UTC の µs。時計を合わせていなければ既定の日時 + 起動からの時間）
    bool     (*clock_set)(void);                  // NTP で時計を合わせてある
    uint32_t (*random_seed)(void);
    void     (*log)(const char *s, unsigned n);   // UART のログへ
    int      (*vsnprintf)(char *buf, size_t n, const char *fmt, va_list ap);   // ファームの printf の書式（%d %u %x %s %c %p %f %e %g、幅・精度。%n は無い）
    void     (*term_write)(const char *s, unsigned n);   // テキストの端末へ（Ruby の puts と同じ。ANSI のエスケープ列、UART にも）
    unsigned (*term_getline)(char *buf, unsigned max);   // 1 行打ってもらう（"\n" つき。Ctrl-D なら 0）
    const char *(*program_dir)(void);             // プログラムのフォルダ（"A:/dir"）
    const char *(*program_path)(void);            // プログラムのファイル

    // ==== メモリ ====
    // PSRAM はキャッシュを通る窓（0x11...）で返す。取ったものは終わるときにファームがまとめて返す
    void    *(*psram_alloc)(uint32_t bytes, uint32_t align);
    void     (*psram_free)(void *p);
    uint32_t (*psram_largest)(void);
    void    *(*sram_alloc)(uint32_t bytes, uint32_t align);   // SRAM（速いが少ない。数十 KB まで）
    void     (*sram_free)(void *p);
    uint32_t (*sram_largest)(void);
    // FB のページ（各 600KB）を借りる。画面モード 7 など FB を使わない間だけ。返すときは黒で消す（終わるときにも）
    unsigned (*fb_borrow)(void **pages, unsigned max, uint32_t *page_bytes);
    void     (*fb_return)(void);

    // ==== 画面 ====
    int      (*mode)(void);
    const char *(*set_mode)(int mode);            // 画面モードを替える（NULL = できた、ほかは理由）。終わるまで待つ
    bool     (*fb_visible)(void);
    void     (*set_fb_visible)(bool on);
    bool     (*text_visible)(void);
    void     (*set_text_visible)(bool on);
    void     (*wait_frames)(unsigned n);          // n フレーム待つ（vsync）
    uint32_t (*frame_count)(void);
    void     (*video_info)(sgx_video_info_t *o);
    uint16_t (*rgb)(unsigned r, unsigned g, unsigned b);   // 0..255 -> 画素の値

    // ---- FB（640x480。RGB555 = 1 画素 16 ビット、CLUT8 = 8、CLUT4 = 4 ビット（上位ニブルが左））----
    int      (*fb_pages)(void);
    int      (*disp_page)(void);
    void     (*set_disp_page)(int p);             // 表示の切り替えはフレームの境目
    int      (*write_page)(void);
    void     (*set_write_page)(int p);
    void    *(*fb_page)(int p);                   // ページの先頭（キャッシュを通らない窓。直に書いてよい）
    int      (*fb_bits)(void);
    void     (*fb_palette)(int i, uint16_t color);   // CLUT8（0〜255）・CLUT4（0〜15）
    uint16_t (*fb_palette_get)(int i);
    // 描くのは書き込みページ。c は RGB555 なら画素の値、CLUT なら色の番号。範囲の外は描かない
    void     (*fb_clear)(uint32_t c);
    void     (*fb_pset)(int x, int y, uint32_t c);
    int32_t  (*fb_pget)(int x, int y);            // 範囲の外は -1
    void     (*fb_line)(int x0, int y0, int x1, int y1, uint32_t c);
    void     (*fb_rect)(int x, int y, int w, int h, uint32_t c);
    void     (*fb_fill_rect)(int x, int y, int w, int h, uint32_t c);
    void     (*fb_triangle)(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c);
    void     (*fb_fill_triangle)(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c);
    void     (*fb_blit)(int src_page, int sx, int sy, int w, int h, int dx, int dy, int32_t key);   // key < 0 なら透明色なし
    void     (*fb_get_block)(int x, int y, int w, int h, void *out);   // 1 画素 2 バイト（RGB555）か 1 バイト（CLUT8・CLUT4）
    void     (*fb_put_block)(int x, int y, int w, int h, const void *in, int32_t key);

    // ---- ローレゾ面（モード 3〜6。320x240、BG 2 面 + オブジェクト 128 個）----
    // パターン・マップ・パレットは直に書く（すぐ効く）。オブジェクトとスクロール・表示は影に書いて lr_commit（次のフレームの頭に写す）
    bool     (*lr_ready)(void);
    uint8_t *(*lr_pattern)(uint32_t *bytes);      // 1 タイル = 8x8 = 64 バイト（色の番号、bit7 = 透過）
    uint16_t *(*lr_map)(int plane);               // 64 x 64 ます（1 ます 16 ビット、ABI 3.0）
    uint16_t *(*lr_palette)(int pal);             // 128 色（画素の値）
    int      (*lr_planes)(void);
    int      (*lr_palettes)(void);
    sgx_lr_obj_t  *(*lr_oam)(void);
    sgx_lr_ctrl_t *(*lr_ctrl)(void);
    void     (*lr_commit)(void);

    // ---- ビットマップ（モード 7、320x200、1 画素 1 バイト = 256 色のパレットの番号）----
    uint8_t *(*bm_vram)(void);                    // 表示している面（SRAM。いつ書いてもすぐ出る）
    void     (*bm_palette)(int i, uint16_t color);
    uint16_t (*bm_palette_get)(int i);
    // 2 枚の面: 後ろの面に描き、bm_present で表示の面へ写す（フレームの境目に。写すまで待つ）。ちらつかない
    uint8_t *(*bm_back)(void);                    // 後ろの面（SRAM 64KB）。取れなければ NULL。ファイルを書く操作のあと番地が変わることがある（呼び直す）
    void     (*bm_present)(void);

    // ---- テキスト面（80 桁。行はモードで 30・25）----
    unsigned (*text_cols)(void);
    unsigned (*text_rows)(void);
    void     (*text_clear)(uint32_t attr);
    unsigned (*text_put)(unsigned col, unsigned row, uint32_t codepoint, uint32_t attr);   // 使った桁（全角は 2）
    unsigned (*text_puts)(unsigned col, unsigned row, const char *utf8, uint32_t attr);
    bool     (*text_get)(unsigned col, unsigned row, uint32_t *codepoint, uint32_t *attr);
    uint16_t (*text_palette_get)(unsigned i);
    // 端末（term_write）の文字の属性とカーソル
    uint32_t (*term_attr)(void);
    void     (*term_set_attr)(uint32_t attr);
    void     (*term_locate)(unsigned x, unsigned y);
    void     (*term_cursor)(unsigned *x, unsigned *y);
    void     (*term_clear)(uint32_t attr);
    bool     (*font_glyph)(uint32_t codepoint, uint16_t rows[16], bool *half);   // フォント ROM（bit15 が左端。半角は上位 8 ビット）

    // ==== 入力 ====
    int      (*key)(bool wait);                   // 次のキー（届いた順）。無ければ -1
    void     (*key_clear)(void);
    bool     (*key_down)(int key);                // いま押されているか（USB キーボードだけ。SGX_KEY_SHIFT など）
    bool     (*keyboard)(void);                   // USB キーボードがつながっている
    void     (*mouse)(sgx_mouse_t *o);
    void     (*tablet)(sgx_tablet_t *o);
    bool     (*pad)(int i, sgx_pad_t *o);         // つながっていなければ false

    // ==== 音（PT8211 のポート。sndmgr・シーケンサー）====
    unsigned (*snd_ports)(void);
    bool     (*snd_open)(void);                   // 初めて使う前に（終わるときにファームが閉じて消音にする）
    bool     (*snd_note_on)(unsigned ch, unsigned note, unsigned velocity);
    bool     (*snd_note_off)(unsigned ch);
    bool     (*snd_volume)(unsigned ch, unsigned v);
    bool     (*snd_expression)(unsigned ch, unsigned v);
    bool     (*snd_adsr)(unsigned ch, unsigned a, unsigned d, unsigned s, unsigned r);
    bool     (*snd_wave)(unsigned ch, unsigned wave);   // SGX_WAVE_*
    bool     (*snd_bend)(unsigned ch, int bend, unsigned range);
    bool     (*snd_freq_mhz)(unsigned ch, uint32_t millihz);
    bool     (*snd_all_off)(void);
    bool     (*snd_master)(unsigned v);
    unsigned (*snd_mode)(unsigned ch);
    bool     (*snd_set_mode)(unsigned ch, unsigned mode);
    unsigned (*snd_level)(unsigned ch);
    unsigned (*snd_phase)(unsigned ch);
    // シーケンサー（SMF 0・1、SGT v1・v2）。データはファームが写して持つ。SGT は BGM（seq_play）だけ
    bool     (*seq_play)(const void *smf, uint32_t len, bool loop, const char **why);
    void     (*seq_stop)(void);
    bool     (*seq_pause)(bool pause);
    bool     (*seq_playing)(void);
    bool     (*seq_paused)(void);
    uint32_t (*seq_position_ms)(void);
    uint32_t (*seq_length_ms)(void);
    void     (*seq_tempo)(unsigned percent);
    bool     (*seq_sfx)(const void *smf, uint32_t len, unsigned ch, const char **why);
    void     (*seq_sfx_stop)(unsigned ch);
    bool     (*seq_sfx_playing)(unsigned ch);
    void     (*seq_reserve)(unsigned ch, bool on);
    // PCM を混ぜて鳴らす（最後のポートを流しで使う。BGM はそのポートを使わない）。標本はプログラムのメモリに置いたまま、鳴り終わるまで動かさない
    bool     (*pcm_open)(unsigned rate_hz, unsigned channels);   // 混ぜる速さ（11025〜48000）とチャンネル数（1〜8）
    void     (*pcm_close)(void);
    bool     (*pcm_play)(unsigned ch, const void *samples, uint32_t count, unsigned format, unsigned rate_hz, unsigned vol_l, unsigned vol_r);   // vol 0〜255
    void     (*pcm_set_volume)(unsigned ch, unsigned vol_l, unsigned vol_r);
    void     (*pcm_stop)(unsigned ch);
    bool     (*pcm_playing)(unsigned ch);

    // ==== ファイル ====
    const char *(*strerror)(int err);
    int      (*stat)(const char *path, sgx_stat_t *st);
    int      (*list)(const char *dir, sgx_dirent_fn fn, void *user);
    int      (*read_file)(const char *path, void *dst, uint32_t max, uint32_t *got);   // 丸ごと読む（max まで）
    int      (*write_file)(const char *path, const void *src, uint32_t len);            // 作る・置き換える
    int      (*remove)(const char *path);         // ファイル・空のフォルダ
    int      (*mkdir)(const char *path);
    int      (*rename)(const char *from, const char *to);
    int      (*set_attr)(const char *path, uint8_t attr);
    bool     (*writable)(const char *path);
    // 大きいファイルを好きな位置から読む（WAD など。開いている間、受け場に SRAM を 16KB 使う）
    sgx_file_t *(*ra_open)(const char *path, int *err);
    int      (*ra_read)(sgx_file_t *f, uint32_t offset, void *dst, uint32_t len);
    uint32_t (*ra_size)(sgx_file_t *f);
    void     (*ra_close)(sgx_file_t *f);
    unsigned (*drives)(sgx_drive_t *out, unsigned max);
    bool     (*storage)(int i, sgx_storage_t *o);

    // ==== 機械の状態 ====
    void     (*sys_info)(sgx_sys_info_t *o);

    // ==== タスクとプログラム（協調）====
    // タスク: 同じプログラムの中で並んで走る。優先度は 0（ふつう、入口と同じ）〜 20（低い）。スタックは PSRAM（sram なら SRAM、取れなければ PSRAM）
    sgx_task_t *(*task_create)(void (*fn)(void *arg), void *arg, uint32_t stack_bytes, unsigned prio, bool sram_stack, const char *name);
    bool     (*task_alive)(sgx_task_t *t);
    int      (*task_join)(sgx_task_t *t, uint32_t timeout_us);   // 終わるまで待つ（0 = 終わった、-1 = 時間切れ）
    void     (*task_exit)(void);                  // このタスクを終える（入口のタスクなら、プログラムが終わる）
    sgx_task_t *(*task_self)(void);
    // 排他・数・待ち行列（どのタスクからでも。待つ間は眠る）
    sgx_mutex_t *(*mutex_new)(void);
    int      (*mutex_lock)(sgx_mutex_t *m, uint32_t timeout_us);   // 0 = 取れた、-1 = 時間切れ（重ねて取れる）
    void     (*mutex_unlock)(sgx_mutex_t *m);
    void     (*mutex_free)(sgx_mutex_t *m);
    sgx_sem_t *(*sem_new)(uint32_t count, uint32_t max);
    int      (*sem_take)(sgx_sem_t *s, uint32_t timeout_us);
    void     (*sem_give)(sgx_sem_t *s);
    void     (*sem_free)(sgx_sem_t *s);
    sgx_queue_t *(*queue_new)(uint32_t item_bytes, uint32_t capacity);   // 中身は写して渡す
    int      (*queue_send)(sgx_queue_t *q, const void *item, uint32_t timeout_us);
    int      (*queue_recv)(sgx_queue_t *q, void *item, uint32_t timeout_us);
    void     (*queue_free)(sgx_queue_t *q);
    // ほかの外部実行ファイルを読み込んで走らせる（同じ画面・キー・音を分け合う）。戻り値はプログラムの番号（0 以上）か、-誤り
    int      (*spawn)(const char *path, int argc, const char *const *argv);
    bool     (*program_alive)(int id);
    int      (*program_wait)(int id, uint32_t timeout_us, int *exit_code);   // 0 = 終わった、-1 = 時間切れ
    void     (*program_stop)(int id);             // 止める頼み（そのプログラムのタスクは、待っている所・区切りで止まる）
    int      (*program_id)(void);                 // 自分の番号（ファイラーから走らせたものは 0）
    // 名前で指し示すもの（プログラムの間の受け渡し。16 個・名前は 15 バイトまで）。NULL を置けば消す
    bool     (*share_set)(const char *name, void *ptr);
    void    *(*share_get)(const char *name);

    // ==== 1.1 から（2026-10-25）====
    // 倍精度の計算（ファームの DCP = RP2350 の倍精度のコプロセッサの版）の関数の表。並びは sgx_dfuncs.inc、*count はその数。
    // ふつうは使わない（SDK の sgx_dmath.S が、コンパイラの __aeabi_d* と sqrt・sin などを自動でこの表へ向ける）
    const void *const *(*double_funcs)(unsigned *count);

    // ==== 1.2 から（2026-10-28）====
    // ラスタオペレーションリスト（表示ラインごとの背景色・ローレゾ BG の CLUT/スクロール）。アプリ時間に書く。
    // y は表示ライン 0..479。plane は 0 = 手前、1 = 奥。clut はマップのパレット番号に足す。sx/sy は画素の補正。
    void (*raster_clear)(void);
    void (*raster_backdrop)(int y, uint16_t color);                 // color は SGX の rgb() の値
    void (*raster_lowres)(int y, int plane, int clut, int sx, int sy);

    // ==== 1.3 から（2026-10-28）====
    // table（1 バイト 1 要素、128 = 0）から全表示ラインのスクロール補正を一括で書く。
    // ライン y は table[(phase + y) % n]。clut / sy はそのまま。毎フレーム phase を +1 すると波が動く
    void (*raster_wave)(int plane, const uint8_t *table, int n, int phase);

    // ==== 1.7 から（2026-10-30）====
    // ---- Socket / TLS ----
    // host は名前か IPv4 の文字列。timeout_ms は接続・読み書きの待ち（SGX_WAIT_FOREVER も可）。
    // 失敗は NULL。理由は sock_error(NULL) で取る。sock_open は TCP、sock_open_tls は TCP + TLS 1.2。
    // verify=true のときは ca_pem（PEM の CA 証明書、0 終わり）が要る。verify=false なら NULL でよい。
    // sock_start_tls は開いている TCP を TLS へ替える（成功したら s は TLS になる。失敗したら閉じている）。
    sgx_socket_t *(*sock_open)(const char *host, uint16_t port, uint32_t timeout_ms);
    sgx_socket_t *(*sock_open_tls)(const char *host, uint16_t port, bool verify, const char *ca_pem, uint32_t timeout_ms);
    int      (*sock_start_tls)(sgx_socket_t *s, const char *host, bool verify, const char *ca_pem, uint32_t timeout_ms);   // 0 = 成功、-1 = 失敗
    int      (*sock_write)(sgx_socket_t *s, const void *buf, uint32_t len, uint32_t timeout_ms);   // 送れたバイト数、-1 = 失敗
    int      (*sock_read)(sgx_socket_t *s, void *buf, uint32_t len, uint32_t timeout_ms);          // >0 = バイト数、0 = 相手が閉じた、-1 = 時間切れ、-2 = エラー
    void     (*sock_close)(sgx_socket_t *s);
    bool     (*sock_closed)(sgx_socket_t *s);
    const char *(*sock_error)(sgx_socket_t *s);   // s = NULL なら最後の open の理由
    unsigned (*sock_available)(sgx_socket_t *s);  // いま読めるバイト数（0 なら待つ）
    bool     (*sock_peer)(sgx_socket_t *s, char *ip, uint32_t n, uint16_t *port);   // ip は "a.b.c.d"

    // ---- FFT ----
    // 複素 FFT（その場で変換）。n は 2〜4096 の 2 のべき乗。inverse=true で逆変換（1/n の掛け算はしない）。
    // 戻り値: 0 = 成功、-SGX_EINVAL = n が悪い、-SGX_ENOMEM = 作業場が取れない。re/im は長さ n の float。
    int      (*fft)(float *re, float *im, uint32_t n, bool inverse);
    // 実数の入力 in[0..n-1] を FFT し、振幅 out[0..n/2] を返す（窓は掛けない。out は長さ n 以上）。
    // 戻り値は fft と同じ。
    int      (*fft_real)(const float *in, float *out, uint32_t n);

    // ==== 3.1 から（2026-11-02）====
    // メモリの使用量を o に書く（待たない。軽い）
    void     (*mem_info)(sgx_mem_info_t *o);
    // タスクの一覧を out に max 件まで書き、タスクの総数を返す（max より多いときは先頭から max 件）。
    // 並びは作った順。max = 0 なら数を数えるだけ
    unsigned (*task_info)(sgx_task_info_t *out, unsigned max);

    // ==== 3.2 から（2026-11-03）====
    // S: のディスク（FAT12、4KB クラスタ）。デフラグは 1 回の step で 4KB を 1 つ書き、
    // 終わるまで finished = false。地図・進みを描きながら、待つフレームに 1 回ずつ呼ぶ。
    uint32_t (*sysdisk_clusters)(void);              // データのクラスタ数
    uint32_t (*sysdisk_free_clusters)(void);
    uint64_t (*sysdisk_free_bytes)(void);
    // 1 クラスタ 1 バイトの地図（0 = 空き、1 = 使用）を out に書く。out は clusters バイト以上。
    // max が足りなければ false
    bool     (*sysdisk_cluster_map)(uint8_t *out, unsigned max);
    // back = true なら高いクラスタ側へ、false なら低いクラスタ側へ寄せる（フォルダは動かさない）
    bool     (*sysdisk_defrag_start_dir)(uint32_t *files, uint32_t *clusters, bool back);
    bool     (*sysdisk_defrag_step)(uint32_t *done, uint32_t *total, uint32_t *done_files, uint32_t *files, bool *finished);
    bool     (*sysdisk_enfrag_start)(uint32_t *files, uint32_t *clusters);
    bool     (*sysdisk_enfrag_step)(uint32_t *done, uint32_t *total, uint32_t *done_files, uint32_t *files, bool *finished);
    void     (*sysdisk_enfrag_seed)(uint32_t seed);
    // 空きクラスタを 0 で書く（消す）
    bool     (*sysdisk_zero_start)(uint32_t *clusters);
    bool     (*sysdisk_zero_step)(uint32_t *done, uint32_t *total, bool *finished);
    bool     (*sysdisk_work_active)(void);
    void     (*sysdisk_work_abort)(void);            // 途中でやめる

    // ==== 3.3 から（2026-11-04）====
    // 組み込みアプリの UI の英語の文を、いまの表示言語へ。訳が無ければ en をそのまま返す。
    // 戻り値はファームの小さな静的な Ring（次の呼び出しで上書きされる）。すぐ描くこと。
    const char *(*translate)(const char *en);

    // ==== 3.4 から（2026-11-04）====
    // シェルから組み込みトラッカーを開く。いまのシェルは終わり、ファイラーが tracker を開く。
    // path は .SGT / .SMF の絶対パス（NULL か空なら空の画面）。0 = 受け付けた。
    int (*request_tracker)(const char *path);
    // ==== ABI 3.5（2026-11-06）====
    // stderr へ書く（シェル sgsh から走らせたときは stderr の行き先へ。ファイラーから走らせたときは term_write と同じ画面の端末）。
    // 使う前に size でこの欄があるか確かめる（3.4 までのファームには無い）
    void (*term_write_err)(const char *s, unsigned n);
    // ==== ABI 4.0（2026-11-06）: ブロックストア ====
    int      (*store_open)(const sgx_store_config_t *cfg);   // ストアの番号（0 以上）。失敗は -SGX_E*
    int      (*store_close)(int id);                          // 書き戻してキャッシュを返す。取り出し中のブロックがあれば -SGX_EBUSY（閉じない）
    void    *(*store_get)(int id, uint32_t block);            // ブロックを取り出す（中身の番地）。失敗は NULL（理由は store_error）
    int      (*store_release)(void *p, int dirty);            // get で取り出したブロックを返す（p はブロックの中のどこでもよい）。dirty = 書いた
    int      (*store_prefetch)(int id, uint32_t block, uint32_t count);   // 先に読んでおく（取り出さない）。読めた数か -SGX_E*
    int      (*store_flush)(int id);                          // 書いたブロックをすべて書き戻す（戻ったら機器に書けている）
    int      (*store_stats)(int id, sgx_store_stats_t *out);
    int      (*store_error)(int id);                          // 最後の誤り（-SGX_E*、無ければ 0）
    // ==== ABI 4.0: FB の余りのページを借りる ====
    // FB を表示しているモード（2・3・4）で、keep のページ（表示と書き込みをそこにする）を残して、残りのページ（各 600KB、
    // いまは 3 ページ = 1.8MB）を PSRAM として借りる。番地はキャッシュを通る窓。借りたページ数を返す（モードが違う・もう借りている・keep が悪いと 0）。
    // 借りている間は、借りたページへの set_disp_page / set_write_page・fb_page・fb_blit の元は効かず、set_mode は断る。
    // 返すのは fb_return（黒で消す）か、プログラムの終わり。FB を使わないモードで全ページを借りるのは今までどおり fb_borrow
    unsigned (*fb_lend_spare)(int keep, void **pages, unsigned max, uint32_t *page_bytes);   // 4.1 から番地の小さい順に並べて返す

    // ==== ABI 4.1（2026-11-06）: オーバーレイ ====
    // パック（sgx_ovl_pack.py で作る .ovl）からモジュールをスロットへ読み込んで、関数の表を通して呼ぶ（docs/sgx_abi.md §10）。
    // 中身は SDK の sgx_overlay.c と同じ（ファームはパックのファイルとスロットの PSRAM を受け持つ）。プログラムごとにパック 1 つ。
    // 誤りは負の SGX_E*。プログラムが終わればパックを閉じ、ovl_add_slot で取ったスロットを返す
    int      (*ovl_open)(const char *path, const void *host);           // パックを開く（前のパックとスロットは閉じる）。host は init の ctx->host
    int      (*ovl_add_slot)(uint32_t bytes);                            // ファームが PSRAM から取る。スロットの番号
    int      (*ovl_add_slot_mem)(void *base, uint32_t bytes);            // プログラムの置き場（psram_alloc・fb_lend_spare のページ。キャッシュを通る窓、8 バイト整列）
    int      (*ovl_count)(void);
    int      (*ovl_find)(const char *name);                              // 名前から番号（無ければ負）
    int      (*ovl_info)(int module, sgx_ovl_entry_t *out);
    int      (*ovl_load)(int slot, int module);                          // もう載っていれば 0 で何もしない。pin のスロットは -SGX_EBUSY
    int      (*ovl_bind)(int slot, int module, void *iface, uint32_t iface_size);   // 読み込んで init を呼ぶ（init の戻り値）
    int      (*ovl_unload)(int slot);
    int      (*ovl_loaded)(int slot);                                    // 載っているモジュールか -1
    int      (*ovl_pin)(int slot);
    int      (*ovl_unpin)(int slot);
    uint32_t (*ovl_gen)(int slot);                                       // 読み込むたびに増える（iface が古くなったか）
    int      (*ovl_stats)(sgx_ovl_stats_t *out);
    void     (*ovl_close)(void);

    // ==== ABI 4.2（2026-11-07）: voice ごとのストリーム ====
    // 既存の pcm_* は最後のポート固定・1 本だけ。こちらは任意の PT8211 ポートに
    // 独立したリング（8KB = 44.1kHz で約46ms）を持つ。開閉はアプリ時間に片づける。
    // stream_write は「左, 右」の 16bit 符号付き。戻り値は書けたフレーム数（負 = SGX_E*）。
    // 書き込みはノンブロッキング。空きは stream_space で見る。
    int      (*stream_open)(unsigned port, unsigned rate_hz);          // 0 = 成功、負 = SGX_E*（EBUSY など）
    int      (*stream_write)(unsigned port, const int16_t *stereo, unsigned frames);
    unsigned (*stream_space)(unsigned port);                            // 書けるフレーム数
    void     (*stream_close)(unsigned port);
    unsigned (*stream_underruns)(unsigned port);                        // 追い越された回数

    // ==== ABI 4.4（2026-11-07）: 音楽の口 ====
    // ファイルを 1 曲、ファームの裏のタスクで鳴らす。形式は中身から見分ける: MP3 / Ogg Vorbis / WAV（ポート 0、本来の速さ）と
    // VGM / VGZ（YM2203 x1〜2・YM2151・YM2149・AY8910・YM2612・YM2610/B・SN76489・NES APU・MSM5205・OKIM6258・SegaPCM。
    // チップごとにポート 0 から順に、44.1kHz）。
    // 鳴らしている間、そのポートは stream_open が EBUSY で断る。プログラムが終わると止まる
    int      (*music_play)(const char *path, unsigned flags);          // 0 = 始めた、負 = SGX_E*（理由は music_info の error）。SGX_MUSIC_LOOP
    void     (*music_stop)(void);
    void     (*music_pause)(bool pause);
    void     (*music_volume)(unsigned vol);                             // 0..254（127 = 100%・既定、254 = 200%。2026-11-14 から。前のファームは 127 で頭打ち）
    void     (*music_info)(sgx_music_info_t *out);
    int      (*music_seek)(uint32_t ms);                                // 頭からの位置へ飛ぶ（MP3 / OGG / WAV。VGM は SGX_ENOTSUP）

    // ==== ABI 4.5（2026-11-08）: 音楽の MIX 設定 ====
    // S:/SystemSettings/Music.cfg（ym2203_ssg=0..200・sn76489=0..200、100 = 元の大きさ）を読み直す。
    // 鳴っている VGM にも次の塊から効く。0 = 読めた、負 = SGX_E*（読めない。今の値のまま）。
    // ファイルが無ければ既定 76・50（見本は起動時にファイラーが書く）。
    // 使う前に size でこの欄があるか確かめる（4.4 までのファームには無い）
    int      (*music_settings_reload)(void);
    unsigned (*music_ssg_pct)(void);                                    // いまの YM2203 SSG の大きさ（%）

    // ==== ABI 4.6（2026-11-08）: SN76489 の MIX と、ボイスごとのチップ ====
    // music_sn_pct: Music.cfg の sn76489=0..200（100 = 元の大きさ、既定 50）で決まる。music_settings_reload で読み直せる。
    // music_voice_chip: port は music_info の voices の番号（0 から）。返すのはそのボイスが鳴らしているチップ（SGX_VGM_*）。
    // 5 つ目からは 4 つ目にまとめるので、複数のビットが立つことがある。鳴っていなければ 0。
    // 使う前に size でこの欄があるか確かめる（4.5 までのファームには無い）
    unsigned (*music_sn_pct)(void);                                     // いまの SN76489 の大きさ（%）
    uint32_t (*music_voice_chip)(unsigned port);                        // そのボイスのチップ（SGX_VGM_*）

    // ==== ABI 4.7（2026-11-09）: VGM/VGZ を 96kHz/24bit WAV に書き出す ====
    // in_path の VGM/VGZ を out_path（S:/A:/G: などの絶対パス）へ 96kHz/24bit ステレオ PCM の WAV で書き出す。
    // 0 = 終わった、負 = SGX_E*。曲の長さの 1〜3 倍の時間がかかる。書き出し中は music_render_progress が 0..100。
    // 使う前に size でこの欄があるか確かめる（4.6 までのファームには無い）
    int      (*music_render_wav)(const char *in_path, const char *out_path);
    unsigned (*music_render_progress)(void);

    // ==== ABI 4.8（2026-11-09）: システムカーソルを動かす ====
    // x, y は 640x480 の画面座標。ゲームが IZ などで内部カーソルを動かしたときに、
    // ファームのシステムカーソル（矢印）も同じ位置へ動かす。0 = 動かした、負 = SGX_E*。
    // 使う前に size でこの欄があるか確かめる（4.7 までのファームには無い）
    int      (*mouse_warp)(int x, int y);

    // ==== ABI 4.9（2026-11-10）: スペクトログラム ====
    // music_tap: 鳴っている音の直近の PCM を out へ「左, 右」の 16bit で写す。戻り値は写したフレーム数。
    // frames は 1〜4096。最新の frames フレームだけを返す（鳴り始めなどで足りなければ、その分だけ）。
    // music_info の pos_ms とは違い、書き出しの環ではなく鳴らした PCM の窓なので、表示の列を作るのに使う。
    unsigned (*music_tap)(int16_t *out, unsigned frames);
    // bm_wrap_clear / bm_wrap_set: モード 7 のビットマップの横方向の循環（音楽 CD のスペクトログラムと同じ）。
    // x から w 画素を off だけずらして読む（off は w で割った余り）。2 か所まで。clear で普通の行に戻る。
    // 使う前に size でこの欄があるか確かめる（4.8 までのファームには無い）
    void     (*bm_wrap_clear)(void);
    void     (*bm_wrap_set)(unsigned x, unsigned w, unsigned off);

    // ==== ABI 4.10（2026-11-10）: モード 7 のページ切替ダブルバッファ ====
    // bm_page_back: 表示していない面（2 枚目）を返す。初回に 64KB を確保して表示面を写す。
    // 取れなければ NULL（そのときは bm_vram() に直に描く）。
    // bm_page_flip: 表裏をフレームの境目で入れ替える。戻ってきたら表示面が変わっている。
    // 呼び手は bm_page_back() を呼び直して、新しい後ろの面へ次のフレームを描く。
    // 使う前に size でこの欄があるか確かめる（4.9 までのファームには無い）
    uint8_t *(*bm_page_back)(void);
    void     (*bm_page_flip)(void);

    // ==== ABI 4.11（2026-11-10）: キャッシュ付き PSRAM ビットマップの書き戻し ====
    // SGX_MODE_BM_CACHED で開いたモード 7 のビットマップを、x,y,w,h の範囲だけ PSRAM へ書き戻す。
    // キャッシュ付きでない面（SRAM・普通の PSRAM）では何もしない。描いたら表示の前に呼ぶ。
    // 使う前に size でこの欄があるか確かめる（4.10 までのファームには無い）
    void     (*bm_flush)(unsigned x, unsigned y, unsigned w, unsigned h);

    // ==== ABI 4.13（2026-11-11）: MIDI ====
    // C ボードの UART1 GP42/43（31250 8N1）と、USB MIDI ホスト。P ボードでは
    // midi_available() = 0、読み書きは 0。route の bit0 = DIN、bit1 = USB。
    // 使う前に size でこの欄があるか確かめる（4.12 までのファームには無い）
    unsigned (*midi_available)(void);                    // DIN MIDI が使える（C のみ 1）
    unsigned (*midi_read)(uint8_t *buf, unsigned max);   // 受け取ったバイトを読む
    unsigned (*midi_write)(const uint8_t *buf, unsigned len); // 選んだ出力へ書く（選んだバイト数）
    unsigned (*midi_available_bytes)(void);
    unsigned (*midi_usb_available)(void);                // USB MIDI ホストが接続中
    void     (*midi_set_route)(unsigned in_mask, unsigned out_mask);
    unsigned (*midi_input_route)(void);
    unsigned (*midi_output_route)(void);

    // ==== ABI 5.0（2026-11-11）: GPIO と PIO2（ユーザー開放）====
    // 使う前に size でこの欄があるか確かめる（4.13 までのファームには無い）。
    // GPIO: 0 = 成功、負 = -SGX_E*。gpio_get はレベル（0/1）。
    int      (*gpio_init)(unsigned pin);                         // SIO の入力、プル無し
    int      (*gpio_set_dir)(unsigned pin, bool out);
    int      (*gpio_set_pull)(unsigned pin, int pull);           // SGX_GPIO_PULL_*
    int      (*gpio_set_function)(unsigned pin, unsigned func);  // SGX_GPIO_FUNC_*
    void     (*gpio_put)(unsigned pin, bool value);
    bool     (*gpio_get)(unsigned pin);

    // PIO2: 戻り値は 0 / 1 / 位置（0〜31）か負の -SGX_E*。PIO2 は 1 つのプログラムが占有し、
    // そのプログラムが終わるとファームが SM・命令メモリ・FIFO を片づける。
    // pio2_available はこのプログラムが使えるか（ほかのプログラムが使っていれば 0）。
    // pio2_load は pioasm の命令語をそのまま載せ、置いた位置を返す。配列を渡せない言語は
    // pio2_insn で 1 語ずつ置いてから pio2_load_staged(count) を使う。
    // pio2_sm_config は SM を初期設定する（offset から実行、分周、wrap、FIFO の結合）。
    // pio2_sm_pins / pio2_sm_shift は後からピン割り当て・シフトを変える。
    // pio2_sm_put / pio2_sm_get は待たない（1 = できた、0 = FIFO が一杯・空）。
    int      (*pio2_available)(void);
    int      (*pio2_load)(const uint16_t *insns, unsigned count);
    int      (*pio2_insn)(unsigned index, unsigned insn);
    int      (*pio2_load_staged)(unsigned count);
    int      (*pio2_gpio_init)(unsigned pin);
    int      (*pio2_sm_claim)(unsigned sm);
    int      (*pio2_sm_unclaim)(unsigned sm);
    int      (*pio2_sm_config)(unsigned sm, unsigned offset, unsigned clkdiv_int, unsigned clkdiv_frac8,
                               unsigned wrap_top, unsigned wrap_bottom, unsigned fifo_join);
    int      (*pio2_sm_pins)(unsigned sm, unsigned out_base, unsigned out_count, unsigned set_base, unsigned set_count,
                             unsigned in_base, unsigned sideset_base, unsigned sideset_count,
                             unsigned sideset_opt, unsigned sideset_pindirs, unsigned jmp_pin);
    int      (*pio2_sm_shift)(unsigned sm, unsigned out_dir, unsigned out_auto, unsigned out_thresh,
                              unsigned in_dir, unsigned in_auto, unsigned in_thresh);
    int      (*pio2_sm_clkdiv)(unsigned sm, unsigned div_int, unsigned frac8);
    int      (*pio2_sm_enable)(unsigned sm, bool on);
    void     (*pio2_sm_restart)(unsigned sm);
    void     (*pio2_sm_clear_fifos)(unsigned sm);
    void     (*pio2_sm_exec)(unsigned sm, unsigned insn);
    int      (*pio2_sm_put)(unsigned sm, uint32_t value);
    int      (*pio2_sm_get)(unsigned sm, uint32_t *value);
    unsigned (*pio2_sm_tx_level)(unsigned sm);
    unsigned (*pio2_sm_rx_level)(unsigned sm);
    bool     (*pio2_sm_tx_full)(unsigned sm);
    bool     (*pio2_sm_rx_empty)(unsigned sm);
    int      (*pio2_sm_set_pins)(unsigned sm, unsigned base, uint32_t values, unsigned count);
    int      (*pio2_sm_set_pindirs)(unsigned sm, unsigned base, uint32_t dirs, unsigned count);
    void     (*pio2_sm_drain_tx)(unsigned sm);
    uint32_t (*pio2_irq_flags)(void);
    void     (*pio2_irq_clear)(uint32_t mask);

    // ==== ABI 5.1（2026-11-11）: ADC ====
    // 単発読み取り: adc_read(pin) は 0〜4095、負 = -SGX_E*（-EINVAL = そのピンは ADC に使えない、
    // -EBUSY = adc_start が ADC を使っている）。adc_can_read で先に確かめられる。
    // 連続取り込み: adc_start(pin_l, pin_r) は 96kHz のステレオをボイス 0 で鳴らす。
    // pin_l は低い側（偶数チャンネル）、pin_r = pin_l + 1。0,0 は基板の既定の L/R
    // （P: 40/41、C: 46/47）。C は 40/41（A6/A7）も選べる。0 = 始めた、負 = -SGX_E*。
    // adc_stats は sgx_adc_stats_t を書く（peak は読むと 0 に戻る）。adc_pins は使えるピンを
    // out に max 件まで書き、総数を返す（C: 40,41,46,47 / P: 40,41）。
    // 使う前に size でこの欄があるか確かめる（5.0 までのファームには無い）。
    int      (*adc_read)(unsigned pin);
    int      (*adc_start)(unsigned pin_l, unsigned pin_r);
    void     (*adc_stop)(void);
    bool     (*adc_active)(void);
    void     (*adc_stats)(sgx_adc_stats_t *out);
    unsigned (*adc_pins)(uint8_t *out, unsigned max);
    bool     (*adc_can_read)(unsigned pin);
    // 生の 1 ブロック取り込み（96kHz、L,R 交互、12 ビット）。out は frames x 2 個の uint16_t。
    // 音源には触らない（音質の測定用）。0 = できた、負 = -SGX_E*。frames は 1〜SGX_ADC_CAPTURE_MAX（10 秒）。
    int      (*adc_capture)(unsigned pin_l, unsigned pin_r, uint16_t *out, unsigned frames);
    // RP2350 内蔵温度センサ。摂氏の 1/1000 を out に書く。0 = 読めた、-SGX_EBUSY = 連続取り込み中。
    int      (*adc_temperature_mc)(int32_t *out);
    // ---- ABI 5.3: FB のプリミティブ描画（SPEC_gfx.md）----
    // t は描く先（SGX_GFX_FB・SGX_GFX_BITMAP・SGX_GFX_BITMAP_BACK・画像の番号）。状態（ペン・ブラシ・…）は描く先ごとに持つ。
    // 色はその面の値（RGB555 は rgb() の値、CLUT は色の番号。gfx_rgb で面に合わせて作れる）。
    // 点の並び xy は (x, y) の対が n 個。図形の座標は画素の番号（線は画素の中心を通る。長方形などは右と下を含まない）。
    // パス（gfx_path_*）の座標は 24.8 の固定小数点で、変換行列を通る。行列の a〜d は 16.16、e f は 24.8。角度は 1/16 度。
    // 戻り値の無い関数は、描く先が使えなければ何もしない（モード 7 を開いていないビットマップなど）。
    void     (*gfx_reset)(int t);                                         // 状態を既定へ
    uint32_t (*gfx_rgb)(int t, unsigned r, unsigned g, unsigned b);       // 0..31 の色を面の値へ（CLUT は近い色の番号）
    void     (*gfx_pen)(int t, uint32_t color, unsigned width, unsigned style);
    void     (*gfx_pen_ext)(int t, unsigned cap, unsigned join);
    void     (*gfx_brush)(int t, uint32_t color, unsigned style);
    void     (*gfx_bk)(int t, unsigned mode, uint32_t color);
    void     (*gfx_rop2)(int t, unsigned rop2);
    void     (*gfx_fill_rule)(int t, unsigned rule);
    void     (*gfx_aa)(int t, bool on);
    void     (*gfx_clip)(int t, int32_t l, int32_t tp, int32_t r, int32_t b);   // r <= l で全体
    void     (*gfx_matrix)(int t, const int32_t m[6]);
    void     (*gfx_concat)(int t, const int32_t m[6]);
    void     (*gfx_get_matrix)(int t, int32_t m[6]);
    void     (*gfx_pset)(int t, int32_t x, int32_t y, uint32_t c);
    int32_t  (*gfx_pget)(int t, int32_t x, int32_t y);                    // 外・使えない描く先は -1
    void     (*gfx_move_to)(int t, int32_t x, int32_t y);
    void     (*gfx_line_to)(int t, int32_t x, int32_t y);
    void     (*gfx_line)(int t, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void     (*gfx_polyline)(int t, const int32_t *xy, unsigned n);
    void     (*gfx_polyline_to)(int t, const int32_t *xy, unsigned n);
    void     (*gfx_poly_bezier)(int t, const int32_t *xy, unsigned n);    // n = 1 + 3k
    void     (*gfx_poly_bezier_to)(int t, const int32_t *xy, unsigned n); // n = 3k
    void     (*gfx_arc)(int t, int32_t l, int32_t tp, int32_t r, int32_t b, int32_t xs, int32_t ys, int32_t xe, int32_t ye);
    void     (*gfx_arc_to)(int t, int32_t l, int32_t tp, int32_t r, int32_t b, int32_t xs, int32_t ys, int32_t xe, int32_t ye);
    void     (*gfx_angle_arc)(int t, int32_t x, int32_t y, uint32_t r, int32_t start16, int32_t sweep16);
    void     (*gfx_rectangle)(int t, int32_t l, int32_t tp, int32_t r, int32_t b);
    void     (*gfx_round_rect)(int t, int32_t l, int32_t tp, int32_t r, int32_t b, int32_t ew, int32_t eh);
    void     (*gfx_ellipse)(int t, int32_t l, int32_t tp, int32_t r, int32_t b);
    void     (*gfx_polygon)(int t, const int32_t *xy, unsigned n);
    void     (*gfx_poly_polygon)(int t, const int32_t *xy, const uint32_t *counts, unsigned ncontours);
    void     (*gfx_pie)(int t, int32_t l, int32_t tp, int32_t r, int32_t b, int32_t xs, int32_t ys, int32_t xe, int32_t ye);
    void     (*gfx_chord)(int t, int32_t l, int32_t tp, int32_t r, int32_t b, int32_t xs, int32_t ys, int32_t xe, int32_t ye);
    void     (*gfx_fill_rect)(int t, int32_t l, int32_t tp, int32_t r, int32_t b, uint32_t color);
    void     (*gfx_path_begin)(int t);
    void     (*gfx_path_end)(int t);
    void     (*gfx_path_move_to)(int t, int32_t x, int32_t y);
    void     (*gfx_path_line_to)(int t, int32_t x, int32_t y);
    void     (*gfx_path_quad_to)(int t, int32_t cx, int32_t cy, int32_t x, int32_t y);
    void     (*gfx_path_cubic_to)(int t, int32_t c1x, int32_t c1y, int32_t c2x, int32_t c2y, int32_t x, int32_t y);
    void     (*gfx_path_arc)(int t, int32_t cx, int32_t cy, int32_t r, int32_t a0_16, int32_t a1_16);
    void     (*gfx_path_close)(int t);
    void     (*gfx_path_fill)(int t);
    void     (*gfx_path_stroke)(int t);
    void     (*gfx_path_fill_stroke)(int t);
    // ビットマップ: 元 s も描く先の番号。key < 0 なら透明色なし（元の値と同じ画素を飛ばす）
    void     (*gfx_bitblt)(int t, int32_t dx, int32_t dy, int32_t w, int32_t h, int s, int32_t sx, int32_t sy, unsigned rop, int32_t key);
    void     (*gfx_stretch_blt)(int t, int32_t dx, int32_t dy, int32_t dw, int32_t dh, int s, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
                                unsigned rop, int32_t key);
    // DIB: bits は fmt（SGX_DIB_*）の w x h（1 行 stride バイト）。palette は INDEX 形式の 0x00RRGGBB（NULL なら番号のまま）
    void     (*gfx_draw_dib)(int t, int32_t dx, int32_t dy, int32_t dw, int32_t dh, const void *bits, unsigned fmt, int32_t w, int32_t h,
                             int32_t stride, const uint32_t *palette, unsigned npal, int32_t sx, int32_t sy, int32_t sw, int32_t sh, unsigned rop);
    // 画像: 番号（16 から）か負（-SGX_ENOMEM・-SGX_EINVAL・-SGX_EIO）。fmt は SGX_GFX_FMT_*。load は PNG / JPEG / GIF / TIFF
    //（fmt は RGB555 か CLUT8。CLUT8 は FB のパレットの近い色）。why が NULL でなければ失敗の理由を指す。プログラムが終わると返す
    int      (*gfx_image_new)(int32_t w, int32_t h, unsigned fmt);
    int      (*gfx_image_load)(const char *path, unsigned fmt, const char **why);
    void     (*gfx_image_free)(int t);
    int      (*gfx_image_info)(int t, int32_t *w, int32_t *h, unsigned *fmt);   // 0 = ある
    void    *(*gfx_image_pixels)(int t, int32_t *stride);                        // 画素の先頭（直に読み書きしてよい）
    // 文字: face は SGX_FONT_*。height は画素。weight は SGX_FW_*。flags は SGX_FF_*。escapement は 0 / 900 / 1800 / 2700
    void     (*gfx_font)(int t, unsigned face, int32_t height, unsigned weight, unsigned flags, unsigned escapement);
    void     (*gfx_text_color)(int t, uint32_t color, unsigned align, int32_t char_extra);   // 色・揃え（SGX_TA_*）・文字間
    void     (*gfx_text_out)(int t, int32_t x, int32_t y, const char *utf8, uint32_t len);
    void     (*gfx_ext_text_out)(int t, int32_t x, int32_t y, unsigned opt, const int32_t *rect, const char *utf8, uint32_t len, const int32_t *dx);
    int32_t  (*gfx_draw_text)(int t, const char *utf8, uint32_t len, int32_t rect[4], unsigned flags);   // 書いた高さ（CALCRECT は rect を直す）
    void     (*gfx_text_extent)(int t, const char *utf8, uint32_t len, int32_t *w, int32_t *h);
    // ---- 5.5（2026-11-13）: メモリの VGM / VGZ を鳴らす（music_play のファイルの代わり。中身はファームが写すので、返ったあと data を返してよい。
    // 止める・一時停止・音量・様子は music_* のまま）。Doom の OPL2 の BGM（MIDI を GENMIDI で YM3812 の VGM に直して渡す）
    int      (*music_play_mem)(const void *data, uint32_t len, unsigned flags);   // 0 = 始めた、負 = SGX_E*。SGX_MUSIC_LOOP
    // ---- 5.6（2026-11-14）: 大きさの決まったファイルを作り、好きな位置へ書く（smf2vgm の出力。gzip の大きさを先に数えてから作る）----
    // create_file は size バイトの 0 のファイルを作る（あれば置き換える）。ra_open で開き、ra_write でその場へ書く。
    // ra_write の offset は 512 の倍数、len も 512 の倍数（ただしファイルの終わりに届く最後の 1 回は端数でよい）。ファイルの大きさは変えない。
    // 流し書きと違って下回りを持ち続けないので、書いている間もほかのファイルを ra_read で読める。S: には書けない（SGX_EROFS）
    int      (*create_file)(const char *path, uint32_t size);                     // 0 = 作った、ほかは SGX_E*
    int      (*ra_write)(sgx_file_t *f, uint32_t offset, const void *src, uint32_t len);   // 0 = 書いた、ほかは SGX_E*
    // ---- 5.7（2026-11-15）: VGM / VGZ を PCM として受け取る（wav2mp3.sgx が MP3 にする。ファームの合成は WAV の書き出しと同じ）----
    // open は 44.1kHz・16 ビット・ステレオの長さ（フレーム）と速さを返す（ファイルを 1 回とおす。ループの所も 1 回）。read は左右に並べた標本で、
    // 作った数（0 = 終わり）。開いている間は music_play などが断られる。close を忘れてもプログラムの終わりに閉じる。失敗の理由は music_info の error
    int      (*music_pcm_open)(const char *path, uint32_t *frames, uint32_t *rate);   // 0 = 開いた、負 = SGX_E*
    int      (*music_pcm_read)(int16_t *stereo, unsigned frames);
    void     (*music_pcm_close)(void);
    // ---- 5.9（2026-11-15）: IME（ファイラーと同じ日本語・English・한국어、辞書・学習も同じ。SGCALC のセルの編集）----
    // ime_key: 打ったキーを先に渡す。1 = IME が使った（commit に確定した UTF-8 が入ることがある）、0 = 使わなかった（プログラムが扱う）。
    // SGX_KEY_IME（Ctrl+Space）でオン・オフ（オフにするとき未確定の字を commit に出す）。ime_draw は画面を描いたあとに呼び、
    // 未確定の字（文節ごと）を col,row から max 桁まで置いて候補の窓を重ねる。使った桁を返す（窓は端末の写しに直接書くので、
    // 呼び手の画面の写しは次に描くとき全部を送り直すこと）。ime_state は状態の短い名前（A / あ / カ / 英 / 한 / 漢 など）
    int      (*ime_key)(int key, char *commit, unsigned n);
    bool     (*ime_on)(void);
    bool     (*ime_composing)(void);
    unsigned (*ime_draw)(unsigned col, unsigned row, unsigned max, uint32_t attr);
    const char *(*ime_state)(void);
    void     (*ime_cancel)(void);
    // ---- 5.10（2026-11-15）: 表計算のマクロ（SGCALC。どのプログラムからも呼べる）----
    // lang（SGX_MACRO_*）のプログラム code を走らせ、終わるまで戻らない。プログラムの Sheet は sheet のコールバックでセルを読み書きする
    // （コールバックは言語のタスクから呼ばれる。そのあいだ呼び手のタスクは macro_run の中で待っている）。print などの出力は捨てる。
    // 0 = 成功、-SGX_EMACRO = 構文・実行の誤り（err に文）、-SGX_EBUSY = 走らせている、-SGX_ENOTSUP = その言語が無い。
    // コールバックの中では画面・キー・ファイルの口を呼ばない
    int      (*macro_run)(unsigned lang, const char *code, uint32_t len, const struct sgx_macro_sheet *sheet, char *err, uint32_t errn);
    // ---- 5.11（2026-11-16）: どこから走ったか・進み具合の窓 ----
    // SGX_RUN_*。POPUP はファイラーのコンテキストメニューの ! で、ファイラーの画面の上の窓で走っている（term_write は画面に出さない
    // ほうがよい。進み具合は progress_box で）。FILER はファイラーから端末の画面で、SHELL はシェルの前景、PIPE は背景・パイプの段
    unsigned (*run_context)(void);
    // 進み具合の窓をテキスト面の真ん中に描く（ファイラーの WAV 書き出しと同じ形・表示の言語）。NULL なら窓の所を空白にする。
    // POPUP のとき以外でも描ける（端末の写しの上に描く）。毎回全体を描くので、0.2〜1 秒に 1 度でよい
    void     (*progress_box)(const struct sgx_progress *p);
};

// ---- run_context（5.11）の値 ----
enum { SGX_RUN_FILER = 0, SGX_RUN_SHELL = 1, SGX_RUN_PIPE = 2, SGX_RUN_CHILD = 3, SGX_RUN_POPUP = 4 };
// ---- progress_box（5.11）----
#define SGX_PB_DONE     0x01u                    // 終わった（「終了しました」と合計の時間。「何かキーを押すと閉じます」）
#define SGX_PB_FAILED   0x02u                    // 誤りで終わった（error に理由）
#define SGX_PB_CANCELED 0x04u                    // 止めた
#define SGX_PB_STOPPING 0x08u                    // 止めている途中
typedef struct sgx_progress {
    const char *title;                            // 1 行目（プログラムの名前と何をしているか。例 "wav2mp3: SC0111.vgz"）
    const char *file;                             // 2 行目（出力のファイル。NULL 可）
    const char *stage;                            // 段（例 "pass 1 of 2"。NULL 可）
    int32_t  pct;                                 // 全体の 0〜100（-1 = 分からない）
    uint32_t elapsed_s;                           // 経過（秒）
    int32_t  left_s;                              // 残りの見込み（秒、-1 = 分からない）
    uint64_t out_bytes;                           // いままでに書いた出力（バイト、0 なら出さない）
    uint64_t out_total;                           // 出力の大きさの見込み（0 = 分からない）
    uint32_t flags;                               // SGX_PB_*
    const char *error;                            // SGX_PB_FAILED の理由
} sgx_progress_t;

// ---- 表計算のマクロ（5.10）の値 ----
enum { SGX_MACRO_RUBY = 1, SGX_MACRO_PYTHON = 2, SGX_MACRO_BASIC = 3 };
enum { SGX_MCELL_EMPTY = 0, SGX_MCELL_NUMBER, SGX_MCELL_TEXT, SGX_MCELL_ERROR };   // ERROR は text に "#DIV/0!" など
typedef struct {
    uint8_t  kind;                                // SGX_MCELL_*
    uint8_t  reserved[7];
    double   num;                                 // kind == NUMBER
    char     text[256];                           // UTF-8、0 終端
} sgx_macro_value_t;
typedef struct sgx_macro_sheet {
    uint32_t size;                                // sizeof(sgx_macro_sheet_t)
    void    *user;
    // 行・列は 0 始まり。0 = 成功、負 = -SGX_E*（範囲外は -SGX_EINVAL）
    int  (*get_cell)(void *user, int row, int col, sgx_macro_value_t *out);
    int  (*set_number)(void *user, int row, int col, double v);
    int  (*set_text)(void *user, int row, int col, const char *text);          // 空の文字列は消す
    int  (*set_formula)(void *user, int row, int col, const char *formula);    // '=' は付けても付けなくてもよい。空は消す
    int  (*clear_cell)(void *user, int row, int col);
    int  (*get_formula)(void *user, int row, int col, char *out, uint32_t n); // セルの生の入力（数式は先頭 '=' 付き）
    int  (*bounds)(void *user, int *r0, int *c0, int *r1, int *c1);           // 1 = 使っている範囲、0 = 空
    void (*recalc)(void *user);
    const char *(*name)(void *user);                                          // シートの名前（NULL 可）
    int  max_rows, max_cols;                                                  // 行・列の数（範囲の確かめ）
} sgx_macro_sheet_t;

// ---- FB のプリミティブ描画（ABI 5.3、SPEC_gfx.md）の値 ----
// 描く先: FB（いまの書き込みページ）・モード 7 のビットマップ・その後ろのページ・gfx_image_new / gfx_image_load の番号（16 から）
#define SGX_GFX_FB        0
#define SGX_GFX_BITMAP    1
#define SGX_GFX_BITMAP_BACK 2
enum { SGX_GFX_FMT_RGB555 = 0, SGX_GFX_FMT_CLUT8 = 1, SGX_GFX_FMT_CLUT4 = 2 };
enum { SGX_PS_SOLID = 0, SGX_PS_DASH, SGX_PS_DOT, SGX_PS_DASHDOT, SGX_PS_DASHDOTDOT, SGX_PS_NULL };   // ペンの線の種類
enum { SGX_CAP_ROUND = 0, SGX_CAP_SQUARE, SGX_CAP_FLAT };
enum { SGX_JOIN_ROUND = 0, SGX_JOIN_BEVEL, SGX_JOIN_MITER };
enum { SGX_BS_SOLID = 0, SGX_BS_NULL, SGX_BS_HORIZONTAL, SGX_BS_VERTICAL, SGX_BS_FDIAGONAL, SGX_BS_BDIAGONAL,
       SGX_BS_CROSS, SGX_BS_DIAGCROSS };                       // ブラシ
enum { SGX_BK_TRANSPARENT = 0, SGX_BK_OPAQUE = 1 };
enum { SGX_R2_COPYPEN = 0, SGX_R2_XORPEN, SGX_R2_MERGEPEN, SGX_R2_MASKPEN, SGX_R2_NOT, SGX_R2_NOTCOPYPEN };   // OR = MERGE、AND = MASK
enum { SGX_FILL_NONZERO = 0, SGX_FILL_EVENODD = 1 };
enum { SGX_SRCCOPY = 0, SGX_SRCPAINT, SGX_SRCAND, SGX_SRCINVERT, SGX_NOTSRCCOPY, SGX_DSTINVERT, SGX_BLACKNESS, SGX_WHITENESS };
enum { SGX_DIB_RGB888 = 0, SGX_DIB_RGBA8888, SGX_DIB_RGB565, SGX_DIB_RGB555, SGX_DIB_GRAY8, SGX_DIB_INDEX8, SGX_DIB_INDEX4,
       SGX_DIB_INDEX1, SGX_DIB_NATIVE };
enum { SGX_FONT_ROM = 0, SGX_FONT_8X8 = 1 };
enum { SGX_FW_NORMAL = 400, SGX_FW_BOLD = 700 };
enum { SGX_FF_ITALIC = 1, SGX_FF_UNDERLINE = 2, SGX_FF_STRIKEOUT = 4 };
enum { SGX_TA_LEFT = 0, SGX_TA_CENTER = 1, SGX_TA_RIGHT = 2, SGX_TA_TOP = 0, SGX_TA_BASELINE = 4, SGX_TA_BOTTOM = 8 };
enum { SGX_ETO_OPAQUE = 2, SGX_ETO_CLIPPED = 4 };
enum { SGX_DT_LEFT = 0, SGX_DT_CENTER = 1, SGX_DT_RIGHT = 2, SGX_DT_TOP = 0, SGX_DT_VCENTER = 4, SGX_DT_BOTTOM = 8,
       SGX_DT_WORDBREAK = 0x10, SGX_DT_SINGLELINE = 0x20, SGX_DT_NOCLIP = 0x100, SGX_DT_CALCRECT = 0x400 };

// ---- SDK のヒープ（2026-11-03）----
// malloc/free は 64KB から始まり、空きが 32KB を切ると 64KB 増える。空きが 128KB になると
// 空いた 64KB を返す。大きな作業場を先に確保したいときは sgx_heap_reserve() を呼ぶ。
// 戻り値は、いま確保してある合計（予約が足りなければ足りた分）。
#ifdef __cplusplus
extern "C" {   // C++ のプログラムからも呼べるように（2026-11-06）
#endif
uint32_t sgx_heap_reserve(uint32_t bytes);
// 置き場をヒープに足す（2026-11-06、SDK だけの関数）。fb_lend_spare で借りたページなどを malloc に使わせる。足した置き場は
// ヒープが縮むときも返さない。0 = 足した。malloc / free / realloc はミューテックスで守る（同じ優先度のタスクどうしでも壊れない）
int sgx_heap_add_region(void *p, uint32_t bytes);
uint32_t sgx_heap_total(void);
uint32_t sgx_heap_free_bytes(void);
uint32_t sgx_heap_min_bytes(void);
#ifdef __cplusplus
}
#endif

#endif
