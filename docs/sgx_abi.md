# sgnano の外部実行ファイル（SGX）の ABI と使い方

> この文書は、sgnano 本体の `docs/sgx_abi.md`（2026-11-16）の写しです。
> このリポジトリでの LAME の配布については [`README.md`](../README.md) の「LAME と LGPL」を見てください。

sgnano のファームとは別に組み、USB ストレージなどに置いて走らせるプログラム（**外部実行ファイル、拡張子 `.sgx`**）の作り方と、
ファームの機能を使うための口（ABI）の説明。版は **ABI 5.2**（2026-11-11。5.1 は 2026-11-11、5.0 は 2026-11-11、4.2 は 2026-11-07、4.1 は 2026-11-06、4.0 は 2026-11-06、3.5 は 2026-11-06、3.4 は 2026-11-04、3.3 は 2026-11-04、3.2 は 2026-11-03、3.1 は 2026-11-02、3.0 は 2026-10-31、2.0 は 2026-10-31、1.8 は 2026-10-30、1.7 は 2026-10-30、1.6 は 2026-10-29、1.5 は 2026-10-29、1.4 は 2026-10-29、1.3 は 2026-10-28、1.2 は 2026-10-28、1.1 は 2026-10-25、1.0 は 2026-10-24）。

- 見出し: `sdk/sgx/include/sgx_api.h`（口の定義はこれがすべて。この文書は使い方と決まりごと）
- 組む道具: `sdk/sgx/`（リンカの台本 `sgx.ld`・始まり `sgx_crt0.c`・libc の口 `sgx_libc.c`・CMake の `sgx.cmake`・像を作る `sgx_link.py`）
- 見本: `apps/hello`（画面・音・ファイル）、`apps/multi`（タスク・同期・spawn）、`apps/doom`（Doom。大きなプログラムの移し方）、`apps/testapp`（テスト用アプリの C 版 mode0〜mode7。テキスト・FB・ローレゾ・ビットマップ・パッド・シーケンサーの使い方、2026-10-25）、`apps/abitest`（Socket / TLS・FFT・ブロックストア・GPIO/PIO2・ADC・MIDI の ABI の試し。`gpiotest.c` は GPIO/PIO2、`adctest.c` / `adctone.c` / `adcmon.c` / `adcrec.c` は ADC（温度・録音を含む）、`midiloop.c` は DIN MIDI のループバック、2026-11-11）、`apps/mem`・`apps/ps`・`apps/more`（sgsh のコマンド、2026-11-02）、`apps/defrag`（S: のデフラグ／エンフラグ。モード 7 の地図と ABI 3.2 の `sysdisk_*`、2026-11-03）、`apps/sgcalc`（表計算。30 行テキストとグラフ、2026-11-04 にサブプロジェクトから取り込み）
- ファームの側: `src/sgx/sgx.c`（読み込み・API の表）、`src/ruby/rbrun.c`（実行の段取り。Ruby と共通）

SDK の見出しとファイル（`sdk/sgx/`）は MIT ライセンス。プログラムのライセンスは自由に選べる（ファームのシンボルには結ばず、
入口に渡される関数の表だけを使うので、たとえば GPL の Doom を外部実行ファイルにしても、ファームに GPL はかからない）。

---

## 1. しくみ

| | |
|---|---|
| 置き場 | PSRAM（キャッシュを通る窓 `0x11...`）。ファームがアリーナから像と `.bss` の大きさを取り、どこにでも置く（再配置） |
| 実行 | M33BEATOS のタスク（core1、優先度 9 = Ruby と同じ。アプリ 8 より低い）。いくつものプログラム・タスクを同時に走らせられる |
| 入口 | `int sgx_main(const sgx_api_t *api, int argc, char **argv)`（`sgx_crt0.c` の `sgx_start` が呼ぶ） |
| ファームの機能 | `api` の関数の表だけ（どこからでも使えるように、`sgx` という名前の大域の変数にも入っている） |
| 走らせ方 | ファイラーで `.sgx` を Enter（引数は `argv[0]` = ファイルのパス）。Doom はファイラーが WAD から `DOOM.SGX -iwad <WAD>` で |
| 画面 | 始めはモード 1（テキスト）。`set_mode` で替える。終わるとファイラーがもとの画面へ戻す |
| 終わり | `sgx_main` から戻るか `exit()`（`api->exit`）。ESC で止まる（`SGX_F_ESC_KEY` のときは Shift+ESC）。0 で終わったらすぐファイラーへ戻る。0 でなければテキストの画面に端末（`term_write` で書いたもの）を出して、何かキーを待ってから戻る |

ファームは走っている間、PSRAM と USB を裏で使う仕事を控える（遠隔コマンドは PING のほかは断る・LAN を止める・CD の再生を止める・
使っていない USB の機器を抜いてもホストを作り直さない）。

## 2. 作り方

### 2.1 CMake（このリポジトリの中で）

`apps/CMakeLists.txt` に足す:

```cmake
sgx_add_program(<target> NAME "<表示名>" [FLAGS <SGX_F_* の値>] [STACK <バイト>]
                SOURCES <...> [INCLUDES <...>] [DEFINES <...>] [OPTIONS <...>])
```

`ninja -C build <target>` で `build/sgx/<target>.sgx`（像）と `<target>.elf`（読み解き用）・`<target>.map` ができる。
コンパイルの引数はファームと同じ（`-mcpu=cortex-m33 -mthumb -mfloat-abi=softfp`）。

| 印（FLAGS） | 意味 |
|---|---|
| `SGX_F_ESC_KEY`（1） | ESC をプログラムのキーとして渡す（ファームは ESC で止めない。止めるのは Shift+ESC）。Doom のようにメニューで ESC を使うもの |
| `SGX_F_SRAM_STACK`（2） | 入口のタスクのスタックを SRAM に（速い。取れなければ PSRAM） |
| `SGX_F_WAIT_KEY`（4） | ふつうに（0 で）終わっても、画面をそのままにして何かキーを待ってからファイラーへ（Ruby と同じ）。文字や絵を出して終わるもの（`hat`・`m640`・`mandel`）。無ければ 0 で終わるとすぐファイラーへ戻り、時間はファイラーの最下行に出る |
| `SGX_F_FULL_TEXT`（8） | テキスト面の 30 行すべてをアプリが使う（0〜29 行目）。ファームの状態行（29 行目）は描かない。`apps/sgcalc` が使う（2026-11-04） |

`STACK` は入口のタスクのスタックの大きさ（既定 16KB、PSRAM）。

### 2.2 いちばん小さいプログラム

```c
#include "sgx_api.h"
#include <stdio.h>
#include <string.h>

int sgx_main(const sgx_api_t *api, int argc, char **argv) {
    char b[80];
    snprintf(b, sizeof b, "hello (ABI %u.%u)\n", api->abi_major, api->abi_minor);
    api->term_write(b, (unsigned)strlen(b));    // テキストの端末へ
    api->key(true);                            // キーを待つ
    return 0;
}
```

### 2.3 libc

【追記 2026-11-06、ABI 4.1 の SDK】**C のファイルの口**（`fopen` / `fread` / `fwrite` / `fgets` / `fprintf` / `fseek` / `ftell` / `fclose`・`open` / `read` / `write` / `lseek` / `close` / `stat` / `remove` / `rename`）が使える
（`sgx_libc.c` の `_open` ほか。読むだけは `ra_open` / `ra_read` で必要な所だけ、書くものはヒープに丸ごと持って `close` で `write_file`。同時に 13 個まで）。
fd 1・2（`printf`）はこれまでどおりログへ。試しは `apps/abitest/fiotest.c`（実機の FAT・exFAT・NTFS で 19/19）。`calloc` が自分を呼び続ける形に組まれていたのも直した

newlib-nano の libc・libm のうち、OS に頼らないもの（文字列・メモリ・数学・`qsort` など）はそのまま使える。OS に頼るものは `sgx_libc.c` が API へ向ける:

| 関数 | 中身 |
|---|---|
| `printf` `puts` `putchar` | UART のログへ（`api->log`）。画面に出すのは `api->term_write` |
| `snprintf` `vsnprintf` `sprintf` `vsprintf` | ファームの書式化（`%d %u %x %s %c %p %f %e %g`、幅・精度） |
| `malloc` `calloc` `realloc` `free` | PSRAM の置き場に先頭適合。64KB から始まり、空きが 32KB を切ると 64KB 増える。空きが 128KB になると空いた 64KB を返す。`sgx_heap_reserve(n)` で最小値を上げられる（`sgx_api.h` の SDK の口） |
| `exit` `_exit` `abort` `assert` | プログラムを終える（`api->exit`） |

**倍精度の計算（`double`）は RP2350 の DCP（倍精度のコプロセッサ）で速く**（ABI 1.1、2026-10-25）: コンパイラが出す `__aeabi_dadd` などと、`sqrt` `sin` `cos` `pow` などの
数学の関数は、`sgx_dmath.S` がファームの DCP の版（pico-sdk の pico_double）へ向ける（`-Wl,--wrap`、並びは `include/sgx_dfuncs.inc`）。書き方は今までどおりの C で、何もしなくてよい。
古いファーム（表が無い）では素のソフトウェアの計算に戻る。DCP の状態はタスクごとに M33BEATOS が保存して戻すので、いくつものタスクで使ってよい。
実機: `hat`（sqrt・cos が多い）4.0 -> 0.75 秒、`m640` 9.3 -> 4.4 秒（残りはほとんど `fb_pset` の呼び出し）。単精度（`float`）はもともと FPU で計算する

`fopen` などのファイルの stdio は無い（`api->read_file`・`write_file`・`ra_*` を使う。Doom は `apps/doom/dg_sys.c` で自分の `fopen` を作っている）。
C++ の静的な初期化（`.init_array`）は入口の前に呼ぶ。

## 3. 実行ファイルの形

```
+-------------------+  0（結ぶ番地 SGX_LINK_BASE）
| sgx_header_t      |  magic "SGX1"・ABI の版・像と .bss の大きさ・入口・スタック・印・名前・再配置の数
| .text / .rodata   |
| .data             |
+-------------------+  image_size
| 再配置の表        |  reloc_count 個の 32 ビット（像の頭からのバイトの位置）
+-------------------+  ファイルの終わり
```

- 番地 0 に結び（`sgx.ld`）、`-Wl,-q` で再配置の情報を残す。`sgx_link.py` が `R_ARM_ABS32`・`R_ARM_TARGET1`（定数の中の絶対番地・関数の表・
  `.init_array`）の位置を集めて表にする。絶対のシンボル（像の大きさなど）と、定まっていない弱いシンボルは表に入れない
- ファームは像を置いた番地 `base` に写し、表の各位置の 32 ビットの語に `base - link_base` を足し、`image_size` から後ろ（表の所も）を
  `.bss` として 0 にし、キャッシュを捨ててから入口を呼ぶ
- **MOVW / MOVT の絶対番地は扱わない**（`sgx_link.py` が止める）。`-mpure-code`・`-mslow-flash-data` を使わないこと（GCC はふつう番地を定数の表から読む）
- 見出しの `abi_major` がファームと違う、`abi_minor` がファームより新しい、大きさや入口がおかしい、のどれかなら読み込まない
  【追記 2026-11-06、4.0】4.x のファームは `abi_major` が 3 のプログラムも読み込む（4.0 は 3.5 の表の末尾に足しただけ。`SGX_ABI_COMPAT_MAJOR`）。
  【追記 2026-11-11、5.0】5.x のファームは `abi_major` が 4 のプログラムも読み込む（5.0 は 4.13 の表の末尾に足しただけ。`SGX_ABI_COMPAT_MAJOR = 4`）
- 【追記 2026-11-06、4.0】ファームはファイルの `.sgx` を**丸ごと別に読まず、置き場へ直に読む**（`sgx_session_start_file`。シェルの子も）。
  以前のファイラーは 1MB を越える `.sgx` を断っていた。いまの上限は像と `.bss` の合計 `SGX_LOAD_MAX`（4MB）と、PSRAM の空き

## 4. 決まりごと

1. **API の関数は、プログラムのタスク（入口のタスクと `task_create` で作ったタスク）からだけ呼ぶ。** 割り込みは持てない
2. **待つ関数は眠る**（ほかのタスクが走る）。待つ間に止める頼み（ESC・`program_stop`・`exit`）が来れば、そのタスクはそこで終わる
   （`sleep_us`・`yield`・`key(true)`・`wait_frames`・`bm_present`・`mutex_lock`・`sem_take`・`queue_*`・`task_join`・`program_wait`）。
   長い計算の途中では `stop_requested()` を見る
3. **メモリの窓**: `psram_alloc`・FB を借りたページ・プログラムの像はキャッシュを通る窓（`0x11...`、速い）。`fb_page` の FB のページは
   キャッシュを通らない窓（`0x15...`、画面にすぐ出る）。SRAM（`sram_alloc`・`bm_vram`・`bm_back`）は少ない（数十 KB まで）
4. **取ったものはファームが覚えている**: メモリ・開いたファイル・ミューテックスなど・タスクは、プログラムが終わるとファームがまとめて返す
5. **画面の操作**（`set_mode`・表示・ページの切り替え）はファームがフレームの境目に片付け、終わるまで待つ（関数から戻ったら済んでいる）
6. **ファイルを書き換える関数**（`write_file`・`read_file`・`remove`・`mkdir`・`rename`・`set_attr`）の間、ファームは SRAM を空けるために
   ビットマップの後ろの面（`bm_back`）を一時返して取り直す。**そのあと `bm_back()` を呼び直して番地を確かめる**（変わることがある。中身は最後に `bm_present` したもの）
7. **パス**は `"A:/dir/name"`（ドライブ文字・コロン・スラッシュ）か、プログラムのフォルダ（`program_dir`）からの相対。UTF-8。大文字小文字は区別しない
8. **浮動小数点**は使ってよい（M33BEATOS がタスクごとに退避する）。引数・戻り値に浮動小数点を使う API は無い（softfp でも hardfp でも同じに呼べる）
9. **スタックがあふれたら、そのプログラムだけが終わる**（2026-11-07、ファーム。ABI の表は変わらない）。どのタスクでも、スタックの下限（PSPLIM）を
   越えると UsageFault（STKOF）になり、ファームはそのタスクをスタックの下限の少し上から続けさせて、プログラムを番号 **139** で終わらせる
   （ほかのタスクも止まる、持っていたミューテックスは手放す）。本体は止まらない。以前は core1 がそこで止まり、ウォッチドッグで再起動していた。
   試しは `apps/abitest/stkoftest.c`（`main` / `task`）。深い再帰をするプログラム（POV-Ray の描くスレッドなど）はスタックを大きめに取る
10. **PSRAM（プログラムの像・`.bss`・ヒープ）の上では不可分の命令（LDREX / STREX、LDAEX / STLEX）が成功しない**（RP2350 の排他の見張りは SRAM だけ）。
   `__atomic_*`・`std::atomic` の「成功するまでやり直す」形はその場で回り続ける。守るのは SGX のミューテックスで。C++ の例外は SDK が SRAM から置き場を取る

## 5. 関数の一覧

表の並び（`sgx_api_t`）の順。詳しい引数は `sgx_api.h`。

### 5.1 実行

| 関数 | |
|---|---|
| `exit(code)` | プログラムを終える（どのタスクから呼んでも。戻らない） |
| `stop_requested()` | 止める頼みが来ている |
| `sleep_us(us)` / `yield()` | 眠る / 1ms 譲る |
| `ms()` / `us()` | 起動からの時間 |
| `utc_us()` / `clock_set()` | いまの日時（UTC の µs）/ NTP で合わせてある |
| `random_seed()` | 乱数の種 |
| `log(s, n)` / `vsnprintf(...)` | UART のログ / 書式化 |
| `term_write(s, n)` / `term_getline(buf, max)` | テキストの端末（Ruby の `puts` と同じ。ANSI のエスケープ列）/ 1 行打ってもらう |
| `program_dir()` / `program_path()` | プログラムのフォルダ・ファイル |

### 5.2 メモリ

| 関数 | |
|---|---|
| `psram_alloc(bytes, align)` / `psram_free(p)` / `psram_largest()` | PSRAM（キャッシュの窓） |
| `sram_alloc` / `sram_free` / `sram_largest` | SRAM |
| `fb_borrow(pages, max, &bytes)` / `fb_return()` | FB の 4 ページ（各 600KB）を借りる。FB を使わない画面モード（0・1・5・6・7）の間だけ。返すと黒で消す |

### 5.3 画面

画面モードは `SGX_MODE_*`（0 表示なし・1 テキスト・2 テキスト + RGB555 の FB・3 + ローレゾ + CLUT8・4 + ローレゾ + CLUT4・5 テキスト + ローレゾ・
6 ローレゾだけ・7 テキスト（25 行）+ 320x200x8 のビットマップ）。`set_mode(m | SGX_MODE_NO_TEXT)` はテキスト面を閉じて SRAM を約 52KB 空ける。

| 群 | 関数 |
|---|---|
| 全体 | `mode` `set_mode` `fb_visible` `set_fb_visible` `text_visible` `set_text_visible` `wait_frames(n)` `frame_count` `video_info` `rgb(r, g, b)`（0〜255 → 画素の値） |
| FB（640x480） | `fb_pages` `disp_page` `set_disp_page` `write_page` `set_write_page` `fb_page(p)` `fb_bits`（16・8・4）`fb_palette` `fb_palette_get` |
| FB の描画（書き込みページへ） | `fb_clear` `fb_pset` `fb_pget` `fb_line` `fb_rect` `fb_fill_rect` `fb_triangle` `fb_fill_triangle` `fb_blit(元のページ, sx, sy, w, h, dx, dy, 透明色)` `fb_get_block` `fb_put_block` |
| ローレゾ（320x240） | `lr_ready` `lr_pattern` `lr_map` `lr_palette` `lr_planes` `lr_palettes` `lr_oam` `lr_ctrl` `lr_commit`（オブジェクトとスクロール・表示は影に書いて `lr_commit`） |
| ビットマップ（モード 7） | `bm_vram`（表示の面、いつ書いてもすぐ出る）`bm_palette` `bm_palette_get` `bm_back`（後ろの面）`bm_present`（後ろの面をフレームの境目に表示の面へ写す。写すまで待つ）`bm_wrap_clear` / `bm_wrap_set`（**ABI 4.9**。横方向の循環。§5.21）`bm_page_back` / `bm_page_flip`（**ABI 4.10**。コピーしないページ切替。§5.22）`bm_flush`（**ABI 4.11**。キャッシュ付き PSRAM の書き戻し。`SGX_MODE_BM_CACHED`。§5.23） |
| テキスト面 | `text_cols` `text_rows` `text_clear` `text_put(桁, 行, 字, 属性)` `text_puts` `text_get` `text_palette_get`、端末の `term_attr` `term_set_attr` `term_locate` `term_cursor` `term_clear`、フォント ROM の `font_glyph` |

属性は `SGX_T_*`（色・反転・ブリンク・線（アンダー / 取り消し / アッパー / 左 / 右）・ボールド・回転・倍角。`src/video/text.h` と同じ値）。

### 5.4 入力

| 関数 | |
|---|---|
| `key(wait)` | 届いたキー（ASCII・特殊キー `SGX_KEY_*`）。UART から来たものには `SGX_KEY_UART` が付く（押した知らせだけ） |
| `key_clear()` / `key_down(key)` / `keyboard()` | 捨てる / いま押されているか（USB キーボード。`SGX_KEY_SHIFT` `CTRL` `ALT` も）/ つながっている |
| `mouse` / `tablet` / `pad(i)` | マウス（累計）・ペンタブレット・パッド（F310 の D モードの並び） |

### 5.5 音

| 群 | 関数 |
|---|---|
| 発音（ポートごと、Ruby の `Sound` と同じ） | `snd_ports` `snd_open` `snd_note_on` `snd_note_off` `snd_volume` `snd_expression` `snd_adsr` `snd_wave`（`SGX_WAVE_*`）`snd_bend` `snd_freq_mhz` `snd_all_off` `snd_master` `snd_mode` `snd_set_mode` `snd_level` `snd_phase` |
- シーケンサー（SMF 0・1、SGT v1・v2） | `seq_play(smf, len, loop, &why)` `seq_stop` `seq_pause` `seq_playing` `seq_paused` `seq_position_ms` `seq_length_ms` `seq_tempo` `seq_sfx` `seq_sfx_stop` `seq_sfx_playing` `seq_reserve` |

`seq_play` は 1.5 から **SGT v1 / v2** も受け付ける。SGT は 4 トラックを 4 ポートへ直接割り当て、音色 0〜8（WT + NOISE）と
効果 A/C/D/F/H/P/Q を扱う。SGT の再生中は `seq_sfx` を受けない（4 ポートを使い切るため）。`seq_play` の引数の形は SMF と同じ。
| PCM（混ぜて最後のポートの流しで） | `pcm_open(rate, ch)` `pcm_close` `pcm_play(ch, 標本, 数, SGX_PCM_U8 / S16, rate, 左, 右)` `pcm_set_volume` `pcm_stop` `pcm_playing`。標本はプログラムのメモリに置いたまま、鳴り終わるまで動かさない |
| ストリーム（voice ごと、4.2） | `stream_open(port, rate)` `stream_write(port, stereo, frames)` `stream_space(port)` `stream_close(port)` `stream_underruns(port)`（§5.19） |
| VGM の PCM（5.7） | `music_pcm_open(path, &frames, &rate)` `music_pcm_read(stereo, frames)` `music_pcm_close()`。VGM / VGZ を 44.1kHz・16 ビット・ステレオで 1 回とおす。開いている間は `music_play` などを断る。`wav2mp3.sgx` が MP3 にするための口（LAME は `wav2mp3.sgx` がリンクする。対応ソースは sgnano_wav2mp3 リポジトリ） |

`SGX_WAVE_NOISE`（波形）と `SGX_SND_NOISE`（モード）は 1.4 で足した。ノイズは起動時に RNG から作る 32 サンプルの波形で、
WT と同じ音程・音量・EG で鳴る（4ch 目のドラム用。どのポートでも使える）。ノイズモードの間は `snd_wave` の値は使われない。

### 5.6 ファイル

| 関数 | |
|---|---|
| `strerror(e)` | 誤りの名前（`SGX_E*`） |
| `stat` `list(dir, fn, user)` | 属性と日時・フォルダの一覧（`fn` を項目ごとに呼ぶ） |
| `read_file(path, dst, max, &got)` / `write_file(path, src, len)` | 丸ごと読む・書く（作る・置き換える） |
| `remove` `mkdir` `rename` `set_attr` `writable` | |
| `ra_open` `ra_read(f, 位置, dst, len)` `ra_size` `ra_close` | 大きいファイルを好きな位置から読む（開いている間 SRAM を 16KB。8 個まで） |
| `drives` / `storage(i)` | ドライブの一覧（文字・種類・準備）/ USB ストレージの素性 |

FAT・exFAT・NTFS（読み書き）、CD（読むだけ）、S:（内蔵、読み書き）。

### 5.7 機械の状態

`sys_info`（起動からの秒・SRAM の空き・USB の機器・キーボード）。`video_info` は 5.3。

`double_funcs`（1.1）: 倍精度の計算の関数の表（2.3）。ふつうは直に使わない

### 5.8 タスクとプログラム（協調）

| 群 | 関数 |
|---|---|
| タスク | `task_create(fn, arg, スタック, 優先度 0〜20, sram, 名前)` `task_alive` `task_join(t, timeout)` `task_exit` `task_self` |
| 排他・数・待ち行列 | `mutex_new` `mutex_lock(m, timeout)` `mutex_unlock` `mutex_free`、`sem_new(count, max)` `sem_take` `sem_give` `sem_free`、`queue_new(大きさ, 数)` `queue_send` `queue_recv` `queue_free` |
| プログラム | `spawn(path, argc, argv)`（ほかの `.sgx` を読み込んで走らせる。番号を返す）`program_alive` `program_wait(id, timeout, &code)` `program_stop` `program_id` |
| 受け渡し | `share_set(名前, ptr)` `share_get(名前)`（名前で指し示すものを置く。プログラムの間で共有のメモリを渡すなど） |

- タスクの優先度は 0（ふつう、入口と同じ）〜 20（低い）。待つ時間は µs、`SGX_WAIT_FOREVER` で止める頼みが来るまで
- `spawn` したプログラムは同じ画面・キー・音を分け合う（画面を替えるのは 1 つにまとめること）。同じファイルを何度 spawn してもよい（別々の番地に置く）

### 5.9 ラスタオペレーションリスト（1.2 / 1.3）

表示ラインごとの背景色と、ローレゾ BG0/BG1 の CLUT・スクロール。アプリ時間（自分のタスク）に書く。
`y` は表示ライン 0〜479。書かないラインはグローバルの背景色と `lr_ctrl` の値のまま。

| 関数 | 中身 |
|---|---|
| `raster_clear()` | 全ラインを「指定なし」に戻す |
| `raster_backdrop(y, color)` | そのラインの背景色（`color` は `rgb()` の値）。テキスト・ローレゾ・FB の奥に出る |
| `raster_lowres(y, plane, clut, sx, sy)` | BG `plane`（0 = 手前、1 = 奥）のパレット補正 `clut`（マップのパレット番号に足す）とスクロール補正 `sx` / `sy`（画素） |
| `raster_wave(plane, table, n, phase)` | `table`（1 バイト 1 要素、128 = 0）から全表示ラインの x 補正を一括で書く。ライン y は `table[(phase + y) % n] - 128`。毎フレーム phase を +1 すると波が動く（1.3） |

見本は `apps/raster/raster.c`（`raster.sgx`）。Ruby の `Machine.lowres.raster_*` と同じ機能。
- 入口のプログラム（番号 0、ファイラーから走らせたもの）が終わると、ほかのプログラム・タスクにも止めてもらい（2 秒まで待つ）、全部を返す
- 同時に: プログラム 4 つ・タスク 16・メモリの塊はプログラムごとに 48・開いた大きいファイルはプログラムごとに 8・同期のもの 32・受け渡し 16

### 5.10 Socket / TLS（1.7、2026-10-30）

TCP と TLS 1.2 のクライアント。下回りは Ruby の `TCPSocket` / `SSLSocket` と同じ（`src/net/netsock.c`・`src/net/tlsclient.c`、mbedTLS 3.6.2）。
ソケットは 1 プログラムにつき 8 個まで。プログラムが終わるとファームが全部閉じる。

| 関数 | 中身 |
|---|---|
| `sock_open(host, port, timeout_ms)` | TCP でつなぐ。`host` は名前か IPv4 の文字列。失敗は NULL |
| `sock_open_tls(host, port, verify, ca_pem, timeout_ms)` | TCP + TLS 1.2。`verify=true` なら `ca_pem`（PEM の CA 証明書、0 終わり）が要る |
| `sock_start_tls(s, host, verify, ca_pem, timeout_ms)` | 開いている TCP を TLS へ替える。0 = 成功、-1 = 失敗（失敗したら閉じている） |
| `sock_write(s, buf, len, timeout_ms)` | 全部送る。送れたバイト数、-1 = 失敗 |
| `sock_read(s, buf, len, timeout_ms)` | >0 = バイト数、0 = 相手が閉じた、-1 = 時間切れ、-2 = エラー |
| `sock_close(s)` | 閉じる（二重に呼んでもよい） |
| `sock_closed(s)` / `sock_error(s)` / `sock_available(s)` / `sock_peer(s, ip, n, &port)` | 状態・理由・読めるバイト数・相手 |

- `timeout_ms` は接続と読み書きの待ち。`SGX_WAIT_FOREVER` も使える
- 失敗の理由は `sock_error(NULL)`（開けなかったとき）か `sock_error(s)`（開いたあと）で取る
- TLS の `host` は SNI と証明書の名前。IP で TCP をつないでから `sock_start_tls` にホスト名を渡す使い方もできる
- 証明書の確認は `verify=true` のときだけ。CA は PEM をそのまま渡す（ファイルから読むなら `read_file`）。`verify=false` は CA 不要
- 見本: `apps/abitest/sockettest.c`（TCP・TLS・CA で確認・start_tls・名前が引けない場合）

### 5.11 FFT（1.7、2026-10-30）

| 関数 | 中身 |
|---|---|
| `fft(re, im, n, inverse)` | 複素 FFT（その場）。`n` は 2〜4096 の 2 のべき乗。`inverse=true` で逆変換（1/n の掛け算はしない）。0 = 成功、-`SGX_EINVAL` = n が悪い、-`SGX_ENOMEM` = 作業場が取れない |
| `fft_real(in, out, n)` | 実数の入力 `in[0..n-1]` を FFT し、振幅 `out[0..n/2]` を返す（窓は掛けない。`out` は長さ n 以上） |

### 5.12 メモリとタスクの一覧（3.1、2026-11-02）

`mem`・`ps` のような、機械の状態を見るコマンドのための口。どちらも待たない。

| 関数 | 中身 |
|---|---|
| `mem_info(sgx_mem_info_t *o)` | SRAM / PSRAM のアリーナの大きさ・空き・最大の連続した空き、アプリが借りている量、sgsh のヒープとバックログ |
| `task_info(sgx_task_info_t *out, unsigned max)` | タスクの一覧を `max` 件まで書き、総数を返す。並びは作った順。`state` は `SGX_TASK_READY` / `BLOCKED` / `SLEEP` / `DEAD`、`current` は 1 つだけ 1 |

```c
sgx_mem_info_t m;
api->mem_info(&m);
sgx_task_info_t t[32];
unsigned n = api->task_info(t, 32);
```

- `re` / `im` / `in` / `out` は長さ `n` の `float`。作業場（回転子）はファームが PSRAM に取って返す
- 逆変換の結果は n 倍になっている（`ir[i] / n` が元の信号）。振幅は `sqrt(re² + im²)`
- 見本: `apps/abitest/ffttest.c`（256 点・1024 点の正弦波、逆変換の往復、実数入力の振幅）

### 5.13 S: のクラスタ地図とデフラグ（3.2、2026-11-03）

S: のディスク（FAT12、4KB クラスタ）を、SGX のプログラムから直接見て動かす口。Ruby の `SystemDrive` と同じ
`src/storage/sysdisk.c` を使う。デフラグは 1 回の `step` で 4KB のブロックを 1 つ書き、その間に地図を描き直せる。

| 関数 | 中身 |
|---|---|
| `sysdisk_clusters()` / `sysdisk_free_clusters()` / `sysdisk_free_bytes()` | データのクラスタ数・空きクラスタ数・空きバイト数 |
| `sysdisk_cluster_map(out, max)` | 1 クラスタ 1 バイトの地図（0 = 空き、1 = 使用）を `out` に書く。`max` はクラスタ数以上。足りなければ false |
| `sysdisk_defrag_start_dir(&files, &clusters, back)` | ファイルを寄せる。`back=false` は低いクラスタ側、`true` は高いクラスタ側。フォルダは動かさない |
| `sysdisk_defrag_step(&done, &total, &done_files, &files, &finished)` | 1 歩進める。`finished=true` で終わり。書き込みの間は `done` が進まないことがある |
| `sysdisk_enfrag_start` / `sysdisk_enfrag_step` / `sysdisk_enfrag_seed` | 2 クラスタ以上のファイルの鎖を、空きクラスタからランダムに選んだ所へ切り裂く（デフラグの試験用） |
| `sysdisk_zero_start` / `sysdisk_zero_step` | 空きクラスタを 0 で書く（消す） |
| `sysdisk_work_active()` / `sysdisk_work_abort()` | 作業中か・途中でやめる |

- どの関数も、S: が使えなければ false / 0。`defrag_start_dir` は作業場（FAT の写しと 4KB の受け場）を SRAM に取るので、失敗することがある
- デフラグ中は表示を隠さない（`sysdisk_work_live(true)` をファームが設定する）。モード 7 のビットマップは SRAM を走査するので、
  フラッシュの書き込みと画面の取得がぶつからない。Ruby の `SystemDrive` は従来どおり表示を隠す
- 見本: `apps/defrag/defrag.c`（`defrag.sgx` と `enfrag.sgx`。モード 7 の地図、`D` で前詰め／後ろ詰め、`Z` で 0 クリア）。
  地図は `fb_borrow` で借りた PSRAM のページへ描き、`wait_frames(1)` の直後に SRAM の表示面へ写す（表示中の面へ直接描かない）

### 5.14 組み込みアプリの訳とシェルからのトラッカー（3.3 / 3.4、2026-11-04）

- `translate(const char *en)`（3.3）: 組み込みアプリの UI の英語の文を、いまの表示言語へ。訳が無ければ `en` をそのまま返す。【追記 2026-11-06】返す文の受け場はタスクごと（同じタスクの次の `translate` まで有効）。以前は全体で 1 つで、同時に走るプログラムが取り合った。
  戻り値はファームの 256 バイトの静的 Ring（次の呼び出しで上書きされる）なので、受け取ったらすぐ描くこと。
  アプリは `SGX_F_LOCALIZE`（0x10）を付けて組む（SGCALC / TRACKER）。`text_puts` もこの印があるときは自動で訳す。
- `request_tracker(const char *path)`（3.4）: シェルから組み込み TRACKER を開く。いまのシェルは終わり、
  ファイラーが言語の後片づけのあとに tracker を開く。`path` は `.SGT` / `.SMF` の絶対パス（NULL / 空なら空の画面）。
  0 = 受け付けた。シェルの中で SGX の子を走らせると画面が混ざるため、この口を使う。

### 5.16 ブロックストア（4.0、2026-11-06）

ファイル 1 つを決まった大きさのブロックの並びとして、PSRAM のキャッシュに必要な分だけ載せて読み書きする。PSRAM に収まらない大きなデータ
（メッシュ・テクスチャ・チェックポイント）をストレージに置くため。ブロックは生のポインタでなく、ストアの番号とブロックの番号で指す。

| 関数 | 中身 |
|---|---|
| `int store_open(const sgx_store_config_t *cfg)` | ストアを開く。番号（0 以上）か `-SGX_E*` |
| `int store_close(int id)` | 書いたブロックを書き戻してキャッシュを返す。取り出し中のブロックがあれば `-SGX_EBUSY`（閉じない）。書き戻せなくても閉じる（戻り値で知らせる） |
| `void *store_get(int id, uint32_t block)` | ブロックを取り出す（中身の番地、キャッシュを通る窓）。載っていなければ読む（眠る）。失敗は NULL（理由は `store_error`） |
| `int store_release(void *p, int dirty)` | 取り出したブロックを返す。`p` はブロックの中のどこでもよい。`dirty` = 書いた。読むだけのストアの `dirty` は `-SGX_EROFS` |
| `int store_prefetch(int id, uint32_t block, uint32_t count)` | 先に読んでおく（取り出さない）。読めた（もう載っていた）数か `-SGX_E*` |
| `int store_flush(int id)` | 書いたブロックをすべて書き戻す。戻ったら機器に書けている |
| `int store_stats(int id, sgx_store_stats_t *out)` | 数（載っている・取り出し中・書いた・当たり・外れ・追い出し・書き戻し・誤り・読み書きのバイト・最長の待ち） |
| `int store_error(int id)` | 最後の誤り（`-SGX_E*`、無ければ 0） |

`sgx_store_config_t`: `path`（`"A:/dir/name"` かプログラムのフォルダからの相対）、`block_size`（512〜256KB の 2 のべき乗）、
`block_count`（0 = 既存のファイルの大きさから。`CREATE` とは組めない）、`cache_blocks`（1〜256。PSRAM に `block_size x cache_blocks` を取る）、
`flags`、`reserved[3]`（0）。

| 印 | 意味 |
|---|---|
| `SGX_STORE_READONLY`（1） | 読むだけ。`CREATE`・`WRITEBACK` とは組めない |
| `SGX_STORE_WRITEBACK`（2） | 書いたブロックを書き戻す（`READONLY` でなければいつもそうする。書き方をはっきりさせる印） |
| `SGX_STORE_CREATE`（4） | ファイルが無ければ 0 で埋めて作る（`block_size x block_count`。USB メモリで 1MB に約 2 秒）。あれば中身はそのまま。小さければ `-SGX_EINVAL`（作り直さない。先に消す） |
| `SGX_STORE_RAW`（8）・`SGX_STORE_COMPRESS`（0x10） | 予約（4.0 では `-SGX_ENOTSUP`） |

- **置き場**: USB ストレージ（フロッピーも）の FAT12/16/32・exFAT・NTFS。S: は読むだけ（書くストアは `-SGX_EROFS`）。CD は使えない（`-SGX_ENOTSUP`）。
  ファイルの読み出し専用の属性は、書くストアを `-SGX_EACCES` で断る
- **書き方**: ファイルの中のセクタを**その場で書き換える**（置き場の一覧から機器の LBA へ）。ファイルの大きさ・置き場・ディレクトリの項目・日時は変えない。
  開いている間、ほかのプログラム・シェルからそのファイルを書き換えない・消さない・動かさない（一覧が古くなる）
- **書き戻すとき**: 追い出すとき・`store_flush`・`store_close`・**プログラムがふつうに終わったとき**（入口が戻った・`exit`・ESC で止めた）。
  **`SIGKILL`（止める頼みに 2 秒答えない）で終わったときは書き戻さず捨てる**。大事な区切りでは `store_flush` を呼ぶ
- **追い出し**: 取り出されていないブロックのうち、いちばん前に使ったもの（LRU）。キャッシュがすべて取り出し中なら `store_get` は NULL・`-SGX_EBUSY`
- 同じブロックを続けて `store_get` すると同じ番地を返し、取り出しの数が増える（同じ数だけ `store_release`）
- 媒体が抜けた・読み書きに失敗したら、そのストアは使えなくなる（以後の `store_get` は NULL・`-SGX_EIO`）。閉じて開き直す
- 1 回の実行で同時に開けるのは 8（`SGX_STORE_MAX`）。どのタスクから呼んでもよい（中で順番に並べる。ファイルの口と同じ `usb_lock`）。待つ間は眠る
- 速さの目安（USB メモリ、FB を出していない）: 4KB のブロックの外れ 1 回 数 ms、64KB で数十 ms。当たり率を上げること（ブロックの大きさ・先読み）
- 見本と試し: `apps/abitest/storetest.c`（`storetest.sgx A:/_s4`）
- 実機の速さ（2026-11-06、HDD、シェルから、FB なし）: 4KB のブロック 64 個の書き戻しつきの読み書きで外れ 1 回あたり最長 約 32ms。
  64KB x 32（2MB）でキャッシュ 4 の乱数の読み書き 200 回が約 28 秒（外れ 157 回・読み 10MB・書き 6.4MB、1 回の待ちは最長 約 190ms）。
  作る（0 で埋める）のは 2MB で FAT 2.3 秒・exFAT 3.3 秒・NTFS 3.3 秒

### 5.17 FB の余りのページを借りる（4.0、2026-11-06）

| 関数 | 中身 |
|---|---|
| `unsigned fb_lend_spare(int keep, void **pages, unsigned max, uint32_t *page_bytes)` | FB を表示しているモード（2・3・4）と、4.3 からはモード 7 で、`keep` のページを残して残りのページ（各 600KB、いまは 3 ページ = 1.8MB）を PSRAM として借りる。表示と書き込みのページは `keep` にする。借りたページ数（だめなら 0）。貸している間の `set_mode` は、4.3 からモード 0・1・2・7 へなら通る（3・4 は断る） |

- 借りている間は、借りたページへの `set_disp_page` / `set_write_page`・`fb_page`（NULL）・`fb_blit` の元は効かず、`set_mode` は断る（モードを替えるとファームが FB の全ページへ描くことがあるため）
- 返すのは `fb_return`（黒で消す）か、プログラムの終わり。FB を使わないモードで全ページを借りるのは今までどおり `fb_borrow`
- 番地はキャッシュを通る窓（`psram_alloc` と同じ扱い）。1 ページだけ表示して PSRAM を多く使うプログラム（POV-Ray）のため

### 5.18 PSRAM の空きの目安（2026-11-06、実機）

| 走らせ方 | 走っている間の PSRAM の空き |
|---|---|
| ファイラーから（1.5MB の像を置いたあと） | 約 2.0MB（いちばん大きい塊 約 1.95MB）。像と合わせて約 3.5MB |
| シェル sgsh の子として | 約 0.9MB（シェルのヒープ 2MB が取ってある）。大きいプログラムはファイラーから走らせる |
| 上のどちらでも、モード 2 で `fb_lend_spare` | +1.8MB |

FB の 4 ページ（2.4MB）は起動のときに取ってある。ほかに exFAT・NTFS を開いているとそのキャッシュ（合わせて約 2.2MB）、LAN のヒープ 96KB。

### 5.19 voice ごとのストリーム（4.2、2026-11-07）

PT8211 のポートごとに独立したリング（4KB = 44.1kHz で約23ms）を持ち、BGM・FM・効果音を別のボイスへ流せる。
`pcm_*`（最後のポート固定・1 本だけ）とは別の口。開閉はアプリ時間に片づける。

| 関数 | 中身 |
|---|---|
| `int stream_open(unsigned port, unsigned rate_hz)` | そのポートをストリーム用に開く。0 = 成功、負 = `SGX_E*`。既にほかのプログラム・古いストリーム・PCM が使っていれば `-SGX_EBUSY` |
| `int stream_write(unsigned port, const int16_t *stereo, unsigned frames)` | 「左, 右」の 16bit 符号付きを書く。戻り値は書けたフレーム数（負 = `SGX_E*`）。ノンブロッキング |
| `unsigned stream_space(unsigned port)` | 書けるフレーム数。0 なら空き待ち |
| `void stream_close(unsigned port)` | 止めて、そのポートを波形（WT）へ戻す |
| `unsigned stream_underruns(unsigned port)` | DMA に追い越された回数 |

- 開いている間、そのポートは `sndseq` の BGM から予約される（`seq_reserve`）。`stream_close` かプログラムの終わりで返す。
- `stream_write` は空きが足りなければ **一部だけ**書く。呼び手は `stream_space` を見て、足りない分を次に回す。
- `pcm_*` は従来どおり最後のポート。`stream_open` が最後のポートを先に取っていたら `pcm_open` は失敗する。
- `stream_open` の `rate_hz` は 8000〜50000。PT8211 の上限は 50kHz（FM の 49.716kHz 用）。
- 旧 `audio_stream_*`（CD・ADC）は排他のまま。CD を鳴らすとほかのストリームは止まる。

### 5.20 音楽の口（4.4 / 4.5 / 4.6 / 4.7 / 4.9、2026-11-07〜10）

ファイルを 1 曲、**ファームの裏のタスク**（M33BEATOS の `music`、優先度 9）で鳴らす。形式は中身から見分ける。
中身は `src/audio/music/`（`music.c`・VGM のエンジン `vgm_engine.c`・`vgm_ymfm.cpp`・`vgm_pcm.c`）と `third_party/ymfm`、サブプロジェクト sgnano-audio のデコーダ。

| 形式 | 鳴らし方 |
|---|---|
| MP3（minimp3）・Ogg Vorbis（stb_vorbis）・WAV（PCM 8 / 16 / 24 / 32 ビット、`WAVE_FORMAT_EXTENSIBLE` の 24-in-32 を含む。モノ / ステレオ） | ポート 0 のストリームへ本来の速さ（8000〜96000Hz）で。WAV も先読みのタスクが 512KB の環を埋め、音楽のタスクは環から読む（96kHz/24bit = 576KB/s でも数百 MB を流せる）。24 ビットは PT8211 が 16 ビットなので上位 16 ビットで出力 |
| VGM / VGZ（YM2203 x1〜2・YM2151・YM2149・AY-3-8910 x1〜2・YM2612・YM2610/B・YM2608・SN76489 x1〜2・NES APU・MSM5205・OKIM6258・OKIM6295・SegaPCM・YM3526・YM3812・YMF262・C140・C352・FDS・HuC6280・K051649・K054539・RF5C68・RF5C164・YM2612 x2） | **1 つのステレオ出力（1 I2S）に全チップを混ぜる**（2026-11-10）。44.1kHz。`music_info.voices` は 1、`music_voice_chip(0)` に全チップのビットが入る。PSG/SSG/DCSG は従来どおり 1 つのグループ。ファイルは丸ごと読む（12MB まで、`.vgz` は展開する）。外部 VGMPLAY の `-local` だけはアプリ内で 4 ポートのまま |

| 関数 | 中身 |
|---|---|
| `int music_play(const char *path, unsigned flags)` | 鳴らし始める（鳴っていれば止めてから）。パスはプログラムのフォルダからの相対か `A:/...`。0 = 始めた、負 = `SGX_E*`（理由は `music_info` の `error`）。`SGX_MUSIC_LOOP` でくり返す（VGM はループの点からいつまでも、ほかは頭から）。無ければ VGM は 2 回まわして終わる |
| `void music_stop(void)` | 止めて、ポートを返す |
| `void music_pause(bool pause)` | 一時停止・再開 |
| `void music_volume(unsigned vol)` | 0〜127（既定 127） |
| `void music_info(sgx_music_info_t *out)` | 様子: `state`（`SGX_MUSIC_IDLE` / `PLAYING` / `PAUSED` / `ENDED` / `ERROR`）・`format`・`voices`・`loops`・`rate`・`chips`（`SGX_VGM_*`）・`pos_ms`・`len_ms`・`load_pct`・`underruns`・GD3 の `title` / `game` / `system` / `author`（UTF-8）・`error` |
| `int music_seek(uint32_t ms)` | 頭からの位置へ飛ぶ（MP3 / OGG / WAV）。VGM は `-SGX_ENOTSUP` |
| `int music_settings_reload(void)` | **ABI 4.5**。`S:/SystemSettings/Music.cfg`（`ym2203_ssg=0..200`、`sn76489=0..200`、`vgm_gain=0..100`）を読み直す。`vgm_gain` は 1 本に混ぜた VGM 全体の固定ゲイン（**2026-11-10、既定 80**）。鳴っている VGM にも次の塊から効く。0 = 読めた、負 = `SGX_E*`（読めない。今の値のまま）。ファイルが無ければ既定 76 / 50 / 80（見本は起動時にファイラーが書く）。読み取りだけで S: には書かない |
| `unsigned music_ssg_pct(void)` | **ABI 4.5**。いまの YM2203 SSG の大きさ（%） |
| `unsigned music_sn_pct(void)` | **ABI 4.6**。いまの SN76489 の大きさ（%） |
| `uint32_t music_voice_chip(unsigned port)` | **ABI 4.6**。そのボイス（ポート 0〜`music_info` の `voices`-1）が鳴らしているチップ（`SGX_VGM_*`）。2026-11-10 から VGM は `voices=1` で、`music_voice_chip(0)` に全チップのビットが立つ（複数ビット）。鳴っていなければ 0 |
| — | 2 台目を使うときは `SGX_VGM_AY8910x2` / `SGX_VGM_YM2149x2` / `SGX_VGM_SN76489x2` も立つ（表示用。`chips` と `music_voice_chip` の両方）。VGMPLAY は `AY-3-8910 x2` のように台数で出す。内蔵 `fvgm` は `chips:` に全部を並べる |
| `int music_render_wav(const char *in, const char *out)` | **ABI 4.7**。VGM / VGZ を 96kHz/24bit ステレオ PCM の WAV に書き出す（`out` は S:/A:/G: などの絶対パス）。0 = 終わった、負 = `SGX_E*`。曲の長さの 1〜3 倍かかる。実機のプレイヤーは ALT+F8（同じフォルダに `.wav`） |
| `unsigned music_render_progress(void)` | **ABI 4.7**。書き出し中の進み（0〜100） |
| `unsigned music_tap(int16_t *out, unsigned frames)` | **ABI 4.9**。鳴らした PCM の直近を「左, 右」の 16bit で `out` へ写す。戻り値は写したフレーム数。`frames` は 1〜4096。最新の `frames` フレームだけを返す（鳴り始めなどで足りなければその分だけ）。音量を掛けたあとの音。表示の列を作るのに使う（§5.21） |

- **音楽の MIX の設定**: `S:/SystemSettings/Music.cfg` の `ym2203_ssg=`（0〜200、100 = 元の大きさ、既定 76）と
  `sn76489=`（0〜200、既定 50）。前者は YM2203 の内蔵 SSG（3 声）だけに掛かり、独立した YM2149 チップには掛からない。
  後者は SN76489（DCSG。メガドライブなどの PSG）に掛かる。2026-11-08 の Sonic の実曲では SN76489 の RMS が YM2612 の
  約 3.5 倍あったので、既定を半分にした。どちらも `music_settings_reload` でいつでも読み直せる。
  ファイルは無いときに起動時のファイラー（アプリ時間）が見本を書く。編集はファイラーのテキストエディタでよい。
  VGMPLAY は再生中の `m` キーで `music_settings_reload()` を呼び、両方の % を出す。

- **2026-11-12: OPL / OPL2 / OPL3 と YM2608（OPNA）**。VGM の `SGX_VGM_YM3526`（OPL）・`SGX_VGM_YM3812`（OPL2）・
  `SGX_VGM_YMF262`（OPL3）・`SGX_VGM_YM2608`（OPNA）を追加した（`chips` のビットは 0x40000 / 0x20000 / 0x80000 / 0x10000。
  関数の表は変わらないので ABI の版は上げない）。YM2608 の FM と SSG は ymfm、ADPCM-A のリズムは同梱の
  **Tetorica OPNA rhythm sounds**（BSD-3-Clause、8KB。`third_party/opna-rhythm/`）で鳴らす。Yamaha の純正 ROM ではないので音は違う。
  ADPCM-B（Delta-T）は VGM のデータブロック 0x81 の ROM をそのまま使う（無ければ ADPCM-B は鳴らない）。
  OPL3 は 2 つのレジスタの口（0x5E / 0x5F）をステレオで混ぜる。YM3812 の 2 台目（bit31）と hard-pan は未対応。
- **2026-11-12: C140 / FDS / HuC6280 / K051649**。VGM の `SGX_VGM_C140`（Namco C140）・`SGX_VGM_FDS`（ファミコンの FDS）・
  `SGX_VGM_HUC6280`（PC エンジン）・`SGX_VGM_K051649`（コナミ SCC）を追加した（`chips` のビットは 0x100000 / 0x200000 /
  0x400000 / 0x800000。関数の表は変わらないので ABI の版は上げない）。C140 は VGM のデータブロック 0x8D の PCM ROM を
  そのまま読み、24 声・8 ビット PCM・μ-law・ループを鳴らす（C219 はまだ鳴らさない）。FDS は 0x84 の bit31 で開き、
  0xB4 の 0x20-0x3F を FDS のレジスタ、0x40-0x7F を波形 RAM として使う。HuC6280 は 0xB9、K051649 は 0xD2。
  どれも libvgm（BSD-3-Clause）の core を 44.1kHz で直接作る形に写した（`src/audio/music/vgm_extra.c`）。
- **2026-11-13: K054539 / C352 / OKIM6295**。`SGX_VGM_K054539`（コナミ K054539）・`SGX_VGM_C352`（Namco C352）・
  `SGX_VGM_OKIM6295`（OKI M6295）を追加した（`chips` のビットは 0x2000000 / 0x4000000 / 0x1000000）。
  K054539 は 0xD3（ROM は 0x8C）、C352 は 0xE1（番地もデータも上位バイトが先。ROM は 0x92）、
  OKIM6295 は 0xB8（ROM は 0x8B）。どれも ROM は VGM のデータブロックをそのまま読む。
  実装は `src/audio/music/vgm_extra2.c`（libvgm の core から）。2 台目（bit30）は未対応で、1 台にまとめる。
- **2026-11-13: RF5C68 / RF5C164・YM2612 の 2 台目**。`SGX_VGM_RF5C68`（0x10000000、セガ System 18・FM TOWNS）・
  `SGX_VGM_RF5C164`（0x20000000、メガ CD）・`SGX_VGM_YM2612x2`（0x8000000、YM2612 のクロックの bit30）を足した。
  関数の表は変えていないので ABI の版は 5.2 のまま（古いプログラムは知らないビットを出さないだけ）。
  RF5C68 は 0xB0 / 0xC1、RF5C164 は 0xB1 / 0xC2、RAM はデータブロック 0xC0 / 0xC1 と命令 0x68（PCM の RAM へ写す）。
  実装は `src/audio/music/vgm_rf5c.c`。
- 鳴らしている間、使っているポートは `stream_open` が `-SGX_EBUSY` で断る。シーケンサーの BGM からも予約される。プログラムが終わると止まる。
- 開くのも音楽のタスクの中（minimp3 は開くときに最初のフレームを解くので大きなスタックが要る）。呼び手のスタックは小さくてよい（MPLAY は 8KB）。`music_play` は開き終えるまで待つ（10 秒まで）。
- MP3 / OGG をストリームで開くとデコーダは長さを知らないので、`len_ms` は読んだバイト数と経過から見積もった値（鳴らすほど正しくなる）。
- 置き場: 作業場・デコーダの状態・タスクのスタックは PSRAM（キャッシュの窓）。VGM はチップの状態（YM2151 約 7KB・YM2203 約 3.4KB・
  YM2612 約 5.2KB・YM2610/B 約 5.8KB）・混ぜ場（5KB）・スタック（8KB）を SRAM から取り、取れなければ PSRAM。
  合成の熱い関数と正弦・指数・音程の表はファームの SRAM（`ld/placement.cmake`、約 15KB）。
- 実機の負荷（2026-11-08、作るのにかかった時間 / 曲の時間）: Power Drift（YM2151 + SegaPCM）74%・Out Run 55%・Darius（YM2203 x2）56%・
  The Ninja Warriors（YM2610B）52〜68%・Sonic（YM2612 + SN76489）65%・YM2612 単独 41%・SN76489 13%・AY-3-8910 21%・NES APU 6%・
  MSM5205 4%・MP3 32%・OGG 35%・WAV 19%。YM2610 は ymfm の 13 ビット出力が libvgm の半分だったので、`G_YM2610 = 2` で合わせた。
  YM2612 は libvgm の 0.35 倍だったので `G_YM2612 = 3`、OKIM6258 は 2 倍でクリップしていたので `G_OKI = 1`。
  Alcon（2xAY-3-8910）は PSG の 1 ボイスにまとめて鳴る。
  2026-11-12 の追加: Phelios（YM2151 + C140）39%、Zelda no Densetsu（FDS）11%、The Tower of Druaga（HuC6280）6%、
  Metal Gear 2（AY-3-8910 + K051649）27%（いずれも underruns 1）。
  2026-11-13 の追加: Salamander 2（K054539）4〜5%、OKIM6295 5%、C352 34%（underruns 1）。
- 見本: `apps/mplay`（MP3 / OGG / WAV）・`apps/vgmplay`（VGM / VGZ、`-local` はプログラムの中のエンジン、`-bench` は全力で 20 秒ぶん作って測る）。

### 5.21 スペクトログラム（4.9、2026-11-10）

音楽 CD の画面（ファイラーの `fcdviz.c`）と同じ 128x128 のスペクトログラムを、ほかのプログラムからも出せるようにした。

| 関数 | 中身 |
|---|---|
| `unsigned music_tap(int16_t *out, unsigned frames)` | 鳴らした PCM の直近を「左, 右」の 16bit で `out` へ写す。戻り値は写したフレーム数。`frames` は 1〜4096。最新の `frames` フレームだけを返す。音量を掛けたあとの音 |
| `void bm_wrap_clear(void)` | モード 7 のビットマップの横循環をやめる（普通の行に戻す） |
| `void bm_wrap_set(unsigned x, unsigned w, unsigned off)` | `x` から `w` 画素を、`off` だけずらして読む（`off` は `w` で割った余り）。2 か所まで。表示だけがずれ、VRAM の中身は動かない |

- 列は 128 本の循環バッファとして書く。新しい列を右端に書き、`bm_wrap_set` の `off` を 1 ずつ進めると、VRAM をずらさずにスクロールする（CD と同じ）
- `mplay` は 44.1kHz で 256 点、96kHz で 512 点、192kHz で 1024 点の Hann 窓 FFT を使い、128 bin で約 0〜22kHz を出す。列は約 60Hz（16.7ms）に 1 本
- 使う前に `size` で `music_tap` / `bm_wrap_set` の欄があるか確かめる（4.8 までのファームには無い）。無いときの `mplay` はテキストのプレイヤーに戻る

### 5.22 ページ切替ダブルバッファ（4.10、2026-11-10）

モード 7 のビットマップを 2 枚持ち、表示面をフレームの境目で入れ替える。`bm_back` / `bm_present` の
64KB コピーとは別の口（コピーしないので、PSRAM のビットマップでも 60Hz を保てる）。

| 関数 | 中身 |
|---|---|
| `uint8_t *bm_page_back(void)` | 表示していない面（2 枚目）を返す。初回に 64KB を確保して表示面を写す。取れなければ NULL |
| `void bm_page_flip(void)` | 表裏をフレームの境目で入れ替える。戻ったら表示面が変わっている。呼び手は `bm_page_back()` を呼び直して、新しい後ろの面へ次を描く |

- 描くのは常に後ろの面。表示面には書かない（書くと走査中にラインが乱れる）。
- 列を 1 本足す場合、後ろの面に書いて `bm_page_flip()`、新しい後ろの面にも同じ列を書いて 2 枚をそろえる。
- `mplay` は 4.10 があればこれを使い、無ければ `bm_vram()` に直に描く（従来の動き）。
- `bm_back` / `bm_present` の意味は変えない（Doom・hello は今までどおりコピーで動く）。

### 5.23 キャッシュ付き PSRAM ビットマップ（4.11、2026-11-10）

テキスト面（約 52KB）を開いたままモード 7 の 64KB ビットマップを使うための口。ビットマップを PSRAM の
**キャッシュ窓**に取り、アプリは普通に書ける。表示（`bm8_line`）は同じメモリの**非キャッシュの別名**を読むので、
アプリは書き終えた範囲を `bm_flush` で PSRAM へ書き戻す。

| 関数 / 定数 | 中身 |
|---|---|
| `SGX_MODE_BM_CACHED`（0x200） | `set_mode(SGX_MODE_TEXT_BM8 | SGX_MODE_BM_CACHED)` のように足す。次のモード 7 のビットマップをキャッシュ付き PSRAM に取る |
| `void bm_flush(unsigned x, unsigned y, unsigned w, unsigned h)` | その範囲のキャッシュ行を PSRAM へ書き戻す。キャッシュ付きでない面では何もしない |

- `mplay` は 4.11 があればこれを使い、テキスト面の UI（CD と同じ 80x25）を残したままスペクトログラムを出す。
- 4.11 が無いファームでは、テキスト面を閉じてビットマップを SRAM に載せる従来の経路（`SGX_MODE_NO_TEXT`）に戻る。
- `bm_flush` は列を書くたびに 2 か所（左右）呼ぶ。64KB 全体の書き戻しは開いたときの 1 回だけ。

### 5.24 mplay 用の小さいテキスト面（4.12、2026-11-10）

モード 7 で 64KB ビットマップを **SRAM** に置き、テキスト面も残すためのモード指定。テキスト面を
25 行・グリフ 19 行にし、文字コードと 32bit 属性は PSRAM、表示に使う省略属性（下位 13bit）と
グリフだけ SRAM に置く。大きい面（30 行・グリフ 30 行）より **約 24KB** 少ない。

| 定数 | 中身 |
|---|---|
| `SGX_MODE_LR_SMALL`（0x800、5.4） | ローレゾ面（モード 3〜6）はパターン 16KB でよい。足さないとき、VGM の合成の区画（SRAM のオーバーレイ）を借りずに開く形は 32KB・16 本以上で、テキスト面のあるモード 3〜6 では区画を借りてしまい、`music_play` の VGM は `no SRAM for the VGM engine` で失敗する。VGM を同時に鳴らすなら足す |
| `SGX_MODE_TEXT_SMALL`（0x400） | `set_mode(SGX_MODE_TEXT_BM8 | SGX_MODE_TEXT_SMALL)` のように足す。テキスト面を 25 行・グリフ 19 行にする |

- `mplay` は 4.12 があればこれを足す。4.11 以前はこのビットを無視する（30 行の面のまま）。
- 25 行はモード 7 の 320x200 を 2 倍した 400 ドット（25x16）にちょうど合う。mplay の UI は 0〜24 行を使う。
- グリフは面を開くときに 19 行ぶんのプールを取り、文字を置いた行から順に配る（mplay が使うのは最大 18 行）。表示中にアリーナを触らない。
- モード 7 を出るとき（`set_mode(SCREEN_TEXT)` など）は、大きい面に戻す。表示を止めてから入れ替える。
- ビットマップは `bm8_open` がまず SRAM を試し、入らなければ `SGX_MODE_BM_CACHED` の PSRAM へ落ちる。

### 5.25 MIDI（4.13、2026-11-11）

C ボードの UART1 GP42(TX)/GP43(RX)、31250 baud、8N1 の DIN MIDI と、USB MIDI ホスト。
P ボードでは `midi_available()` が 0 で、読み書きは 0。使う前に `size` でこの欄があるか確かめる。

| 関数 | 中身 |
|---|---|
| `unsigned midi_available(void)` | DIN MIDI が使える（C のみ 1） |
| `unsigned midi_read(uint8_t *buf, unsigned max)` | 受け取ったバイトを読む |
| `unsigned midi_write(const uint8_t *buf, unsigned len)` | 選んだ出力へ書く（書けたバイト数） |
| `unsigned midi_available_bytes(void)` | 読めるバイト数 |
| `unsigned midi_usb_available(void)` | USB MIDI ホストが接続中 |
| `void midi_set_route(unsigned in_mask, unsigned out_mask)` | bit0 = DIN、bit1 = USB |
| `unsigned midi_input_route(void)` / `midi_output_route(void)` | いまの経路 |

`src/audio/sndseq.c` の SMF プレイヤーは、`sndseq_midi_out(true)` で BGM のチャンネル
イベントを MIDI OUT へも送る（`sndseq_midi_channel(src, dst)` でチャンネルを移せる）。
ファイラーの SMF プレイヤーは `m` キーで「内部のみ → 両方 → 外部のみ」を切り替える。

### 5.26 GPIO と PIO2（5.0、2026-11-11）

C ボード・P ボードの GPIO をアプリから使い、**PIO2 をユーザーに開放**した。PIO0 は映像、PIO1 は音源
（PT8211 の voice 0〜3）が使っている。PIO2 は音源が 4 声のとき空いている（5 声以上にすると音源が使うので
`pio2_available()` が 0 になる）。PIO2 は **1 つのプログラムが占有**し、そのプログラムが終わるとファームが
SM・命令メモリ・FIFO を片づける。

#### GPIO（SIO）

ピンは 0〜47。空きピンは基板で違う。**C ボードは GP34〜41**（GPIO A0〜A7。GP42/43 は MIDI、
GP44/45 はコンソール、GP46/47 は ADC）。**P ボードは GP42/43・GP46/47**（GP44/45 はコンソール）。
PSRAM（GP0）・VGA（GP1〜17）・音源（GP18〜41、P ボード）・基板判別（GP30/31）など、ファームが使っているピンは触らないこと。

| 関数 | 中身 |
|---|---|
| `int gpio_init(unsigned pin)` | SIO の入力、プル無しにする。0 = 成功、負 = `-SGX_E*` |
| `int gpio_set_dir(unsigned pin, bool out)` | 出力（true）／入力（false） |
| `int gpio_set_pull(unsigned pin, int pull)` | `SGX_GPIO_PULL_NONE` / `_UP` / `_DOWN` |
| `int gpio_set_function(unsigned pin, unsigned func)` | `SGX_GPIO_FUNC_*`（SIO = 5、PIO2 = 8、`SGX_GPIO_FUNC_NULL` = 0x1F） |
| `void gpio_put(unsigned pin, bool value)` | 出力の値 |
| `bool gpio_get(unsigned pin)` | 入力の値（パッドのレベル） |

`gpio_set_function` は io_bank0 の機能の番号をそのまま受ける。PIO2 のピンに使うときは `pio2_gpio_init()` を使うほうが確実。

#### PIO2

PIO2 の GPIOBASE は 16 なので、**PIO2 が使えるピンは 16〜47**。命令は pioasm（RP2350、絶対ピン番号）が
出した 16 ビットの語をそのまま渡す。`pioasm -o hex` の 1 行 4 桁がそのまま 1 語になる。

| 関数 | 中身 |
|---|---|
| `int pio2_available(void)` | このプログラムが PIO2 を使える（1）か。ほかのプログラムが使っていれば 0 |
| `int pio2_load(const uint16_t *insns, unsigned n)` | 命令を載せ、置いた位置（0〜31）を返す。負 = `-SGX_E*` |
| `int pio2_insn(unsigned i, unsigned insn)` | 配列を渡せない言語用。載せる前の命令を 1 語ずつ置く |
| `int pio2_load_staged(unsigned n)` | `pio2_insn` で置いた n 語を載せる |
| `int pio2_gpio_init(unsigned pin)` | ピンを PIO2 の入力にする（16〜47） |
| `int pio2_sm_claim(unsigned sm)` / `pio2_sm_unclaim(sm)` | SM（0〜3）をこのプログラムのものにする／返す |
| `int pio2_sm_config(sm, offset, div_int, div_frac8, wrap_top, wrap_bottom, fifo_join)` | SM を初期設定する。`offset` から実行、分周は `div_int + div_frac8/256`、wrap は絶対番地、`fifo_join` は `SGX_PIO_FIFO_JOIN_*` |
| `int pio2_sm_pins(sm, out_base, out_count, set_base, set_count, in_base, sideset_base, sideset_count, sideset_opt, sideset_pindirs, jmp_pin)` | ピンの割り当てを変える（`pio2_sm_config` のあとで） |
| `int pio2_sm_shift(sm, out_dir, out_auto, out_thresh, in_dir, in_auto, in_thresh)` | シフトの向き（`SGX_PIO_SHIFT_*`）・自動プル／プッシュ・しきい値 |
| `int pio2_sm_clkdiv(sm, div_int, div_frac8)` | 分周を後から変える |
| `int pio2_sm_enable(sm, bool on)` | SM を動かす／止める |
| `void pio2_sm_restart(sm)` / `pio2_sm_clear_fifos(sm)` / `pio2_sm_drain_tx(sm)` | 再開・FIFO を空にする・TX FIFO を吐く |
| `void pio2_sm_exec(sm, unsigned insn)` | 命令を 1 つその場で実行する |
| `int pio2_sm_put(sm, uint32_t value)` | TX FIFO へ。1 = 入った、0 = 一杯（待たない） |
| `int pio2_sm_get(sm, uint32_t *value)` | RX FIFO から。1 = 取れた、0 = 空（待たない） |
| `unsigned pio2_sm_tx_level(sm)` / `pio2_sm_rx_level(sm)` | FIFO の段数 |
| `bool pio2_sm_tx_full(sm)` / `pio2_sm_rx_empty(sm)` | FIFO の状態 |
| `int pio2_sm_set_pins(sm, base, values, count)` | base から count 本の出力値をまとめて書く（bit0 = base） |
| `int pio2_sm_set_pindirs(sm, base, dirs, count)` | 同じく向きを書く（1 = 出力） |
| `uint32_t pio2_irq_flags(void)` / `void pio2_irq_clear(uint32_t mask)` | PIO2 の IRQ フラグ（下位 8 ビット）を読む／消す |

使い方の順は、`pio2_sm_claim` → `pio2_load` → `pio2_sm_config` → 必要なら `pio2_sm_pins` / `pio2_sm_shift`
→ `pio2_sm_enable`。`pio2_sm_config` はピン割り当てとシフトを既定（ピンなし、右シフト、しきい値 32）に戻すので、
ピンを使うときはそのあとで `pio2_sm_pins` を呼ぶ。SM を返したら `pio2_sm_unclaim`。プログラムが終わればファームが
自動で片づける。

```
// 例: ピンを使わない FIFO の往復（pioasm: pull block / mov isr, osr / push block）
const uint16_t prog[] = { 0x80A0, 0xA0C7, 0x8020 };
int off = pio2_load(prog, 3);
pio2_sm_claim(0);
pio2_sm_config(0, off, 1, 0, off + 2, off, SGX_PIO_FIFO_JOIN_NONE);
pio2_sm_enable(0, true);
pio2_sm_put(0, 0x12345678);
uint32_t v;
while (!pio2_sm_get(0, &v)) { }      // 1 = 取れた
```

- 割り込みハンドラはアプリに渡していない。IRQ は `pio2_irq_flags` をポーリングする（PIO2 の IRQ は SM の
  `irq` 命令で立てる）。
- ピンの機能・向き・プルは、プログラムが終わっても残る。必要ならアプリの終わりに戻す（`gpio_init` など）。
  PIO2 の SM・命令メモリ・FIFO はファームが片づける。
- `pio2_sm_set_pins` / `pio2_sm_set_pindirs` は PIO の命令を 1 つ借りて書くので、SM を止めてから呼ぶ
  （動かす前の初期化か、`pio2_sm_enable(sm, false)` のあと）。
- DMA はアプリからは使えない（必要な処理は CPU で FIFO を回す）。PIO の DMA が要る場合はファームに口を足す。
- 5 声以上の音源設定では PIO2 を音源が使うため、`pio2_available()` は 0 になる。
- 実機の確認: `apps/abitest/gpiotest.c`。`gpiotest.sgx 34` のように空きピンを渡すと SIO の回り込みを見る
  （省くと GPIO は飛ばす）。PIO2 の pull/push の FIFO はピンを使わずに確かめる。

### 5.27 ADC（5.1、2026-11-11）と温度センサ（5.2）

ADC を単発で読む口と、96kHz のステレオ入力をボイス 0 で鳴らす口。96kHz の連続取り込みは
ファイラーの Shift+F10 と同じ仕組み（`src/audio/adcin.c`）。**ABI 5.2** で RP2350 内蔵の
温度センサ（ADC のチャンネル 8）を読む `adc_temperature_mc` を足した。

| ボード | 単発で読めるピン | 連続取り込みの組（l, r） |
|---|---|---|
| P | GP40, GP41（ADC0/1） | (40, 41) |
| C | GP40, GP41（A6/A7、ADC0/1）、GP46, GP47（ADC6/7） | (40, 41)、(46, 47) |

- **ADC は 1 つ**。`adc_start` の間は `adc_read` が `-SGX_EBUSY` になる。`adc_stop` で返る。
- C ボードの GP46/47 は、5V フルスイングのオペアンプ出力を 1/2 に分圧した入力（最大 2.5V、
  センター 1.25V）。ADC の基準は 3.3V なので、最大は約 3103/4095（75.7% FS）。
- `adc_start(pin_l, pin_r)` の `pin_l` は低い側（偶数チャンネル）、`pin_r = pin_l + 1`。
  `0, 0` は基板の既定（P: 40/41、C: 46/47）。C ボードで A6/A7 を使うときは `adc_start(40, 41)`。
- `adc_start` はボイス 0 のストリームを使う。CD などが音源を使っているときは `-SGX_EBUSY`。
- `adc_read` は 0〜4095（12 ビット）。`adc_can_read(pin)` で先に確かめられる。

| 関数 | 中身 |
|---|---|
| `int adc_read(unsigned pin)` | 0〜4095、負 = `-SGX_E*`（`-EINVAL` = 使えないピン、`-EBUSY` = 連続取り込み中） |
| `int adc_start(unsigned pin_l, unsigned pin_r)` | 96kHz のステレオ取り込みを始める。0 = 始めた、負 = `-SGX_E*`。`0, 0` は基板の既定 |
| `void adc_stop(void)` | 止める |
| `bool adc_active(void)` | 動いているか |
| `void adc_stats(sgx_adc_stats_t *out)` | 取り込んだ数・捨てた数・途切れ・FIFO あふれ・振れ幅・直流分。振れ幅は読むと 0 に戻る |
| `unsigned adc_pins(uint8_t *out, unsigned max)` | 使えるピンを `out` に書き、総数を返す（C: 40,41,46,47 / P: 40,41） |
| `bool adc_can_read(unsigned pin)` | そのピンを単発で読めるか |
| `int adc_capture(unsigned pin_l, unsigned pin_r, uint16_t *out, unsigned frames)` | 96kHz の生の 1 ブロック（L,R 交互、12 ビット）。音源には触らない。`frames` は 1〜`SGX_ADC_CAPTURE_MAX`（960000 = 10 秒）。0 = できた、負 = `-SGX_E*` |
| `int adc_temperature_mc(int32_t *out)` | **ABI 5.2**。RP2350 内蔵温度センサを読み、摂氏の 1/1000 を `out` に書く。0 = 読めた、`-SGX_EBUSY` = 連続取り込み中 |

```
// C ボードの A6/A7 を 96kHz で鳴らす
if (adc_start(40, 41) == 0) {
    // ... 好きなだけ待つ ...
    sgx_adc_stats_t st;
    adc_stats(&st);
    adc_stop();
}
// 単発
int v = adc_read(46);      // C の専用 L（ADC6）
// 生の 1 ブロック（音質の測定用）
uint16_t buf[4096 * 2];
adc_capture(46, 47, buf, 4096);
```

- `adc_read` はそのピンを ADC の入力に切り替える（デジタルのプルは外れる）。GPIO として
  同時には使えない。使い終わったら `gpio_init(pin)` で戻す。
- C ボードの GP40/41 は GPIO A6/A7 でもある。ADC として使う間はデジタル入力には使えない。
- **温度センサ**: RP2350 の内蔵センサを `adc_temperature_mc` で読める。ADC は 1 つなので、
  `adc_start` の間は `-SGX_EBUSY`。ハード RTC は無い（時刻は NTP + 起動からの経過）。
- 実機の確認: `apps/abitest/adctest.sgx`。C ボードで単発・連続取り込み・A6/A7・温度を確認する。
  GP46/47 の DAC→ADC ループバックは再ハンダ後に 1007.8Hz を確認（信号 3.8% FS、SNR 15.3dB、THD+N 20.8%）。
  信号が小さいためノイズ律速で、ADC の限界値ではない。

### 5.28 FB のプリミティブ描画（5.3、2026-11-13）

`SPEC_gfx.md` の描画を `gfx_*` で出す（`sdk/sgx/include/sgx_api.h` の末尾）。描く先 `t` は `SGX_GFX_FB`（いまの書き込みページ）・
`SGX_GFX_BITMAP`（モード 7）・`SGX_GFX_BITMAP_BACK`（後ろのページ）・`gfx_image_new` / `gfx_image_load` が返す番号（16 から）。
状態（ペン・ブラシ・背景・ROP・塗りの規則・アンチエイリアス・切り抜き・行列・フォント・文字の色）は描く先ごとに持ち、プログラムが
終わると既定へ戻り、画像は返す。色はその面の値（`gfx_rgb` で作れる）。点は `int32_t` の (x, y) の対。パスの座標は 24.8 の固定小数点、
行列の a〜d は 16.16、角度は 1/16 度。実装は `src/gfx/`（ファームはフラッシュ・作業場は PSRAM のキャッシュの窓）と `src/gfx/gfx_sys.c`。
見本は `apps/gfxdemo`。

### 5.29 どこから走ったか・進み具合の窓（5.11、2026-11-16）

- `run_context()` は `SGX_RUN_FILER`（ファイラーから、端末の画面）・`SGX_RUN_SHELL`（シェルの前景）・`SGX_RUN_PIPE`（シェルの背景・パイプの段）・`SGX_RUN_CHILD`（ほかのプログラムの spawn）・`SGX_RUN_POPUP`（ファイラーのコンテキストメニューの `!`）を返す。
- `POPUP` のときは、ファイラーの画面が端末の写しに取り込まれた状態で始まる（画面モードもファイラーのまま、FB は隠す、29 行目の状態の行は出さない）。`term_write` は画面に出るので使わない。進み具合は `progress_box`、文は `log`（UART）へ。
- `progress_box(const sgx_progress_t *p)` はテキスト面の真ん中（4, 9 から 72 x 10）に、ファイラーの WAV 書き出しと同じ形の窓を描く（表示の言語で）。`pct`（-1 = 分からない）・`elapsed_s`・`left_s`（-1 = 分からない）・`out_bytes` / `out_total`・`stage`、`flags` は `SGX_PB_DONE` / `FAILED` / `CANCELED` / `STOPPING`。`NULL` なら窓の所を空ける。
- SDK の部品 `sdk/sgx/include/sgx_progress.h`（ヘッダだけ）が両方をまとめる: シェルの前景とファイラーの端末では 1 行を `\r` で書き替え（`37%  1:23  left ~4:10  out 12.3 MB / 33.1 MB  (段)`）、背景・パイプの段では 10% ごとに 1 行、`POPUP` では窓。終わりの `sgxp_end` は窓なら結果を出して何かキーを待つ（止めたときは待たない）。5.10 以前のファームでは端末の形。使っているのは `wav2mp3`・`smf2vgm`。

## 6. Doom（apps/doom）

- `DOOM.SGX` を WAD の入ったフォルダ（`DoomFiles` / `Doom2Files`）か、その親のフォルダに置く。ファイラーで WAD を Enter すると
  `DOOM.SGX -iwad <WAD の絶対パス>` で走る。`DOOM.SGX` を直に Enter したときは、自分のフォルダから知っている名前の IWAD を探す（無ければ知らせて終わる）。
  Doom の設定・セーブは WAD のフォルダ。Doom の誤り（I_Error）は端末にも出る
- 画面はモード 7（`SGX_MODE_NO_TEXT`）・2 枚の面（`bm_back` / `bm_present`）、音楽は `seq_*`、効果音は `pcm_*`、WAD は `ra_*`、
  ゾーンは `psram_alloc` と `fb_borrow`。`SGX_F_ESC_KEY`（ESC は Doom のメニュー、止めるのは Shift+ESC）
- doomgeneric の libc の名前は `apps/doom/dg_port.h` で向け直している（ソースは `third_party/doomgeneric/`、手直しは `SGNANO_PATCHES.md`）

## 7. 版の決まり

- `sgx_api_t` は **後ろに足すだけ**（並びを変えない・消さない）。足したら `SGX_ABI_MINOR` を上げる。プログラムは `api->size` を見てから新しい関数を使える
- 並びや意味を変えるときは `SGX_ABI_MAJOR` を上げる（古いプログラムは読み込まない）
- 【2026-11-06】4.0 は例外: 末尾に足しただけで major を上げた（ブロックストアで扱いの変わる大きな版の区切りとして。ユーザー指示の「ABI 4.0」）。
  ファームは 3.x のプログラムも読み込む。4.0 で組んだものは 3.x のファームでは読み込まない
- 見出し（`sgx_header_t`）も同じ

## 8. 履歴

- 2026-11-16: **5.11**。`run_context()` と `progress_box(p)`（`sgx_progress_t`、`SGX_RUN_*`・`SGX_PB_*`）を表の末尾に追加（§5.29）。ファイラーのコンテキストメニューの `!` で、ファイラーの画面の上の窓で走らせる。5.10 以前のプログラムはそのまま動く。
- 2026-11-15: **5.10**。表計算のマクロ `macro_run(lang, code, len, sheet, err, errn)` を表の末尾に追加（`SGX_MACRO_RUBY` / `PYTHON` / `BASIC`）。ファームが言語をシェルの子と同じ口で走らせ（言語の `exit` / `END` はマクロだけを終わらせる、出力は捨てる）、終わるまで戻らない。言語の `Sheet` は `sgx_macro_sheet_t` のコールバック（`get_cell`・`set_number`・`set_text`・`set_formula`・`clear_cell`・`get_formula`・`bounds`・`recalc`・`name`、行・列は 0 始まり）でセルに触る。コールバックは言語のタスクから呼ばれ、その間に取ったメモリは呼び手のプログラムの持ち物になる。コールバックの中では画面・キー・ファイルの口を呼ばない。誤りは `-SGX_EMACRO`（新しい誤りの番号、文は `err`）、走らせている間は `-SGX_EBUSY`。SGCALC R01.007 が使う。5.9 以前のプログラムはそのまま動く。
- 2026-11-15: **5.9**。IME を表の末尾に追加: `ime_key(key, commit, n)`（キーを先に渡す。1 = IME が使った、確定した UTF-8 は commit）・`ime_on`・`ime_composing`・`ime_draw(col, row, max, attr)`（未確定の字を文節ごとに置き、候補の窓を重ねる。窓は端末の写しに直接書くので、呼び手の画面の写しは次に全部を送り直す）・`ime_state`・`ime_cancel`。ファイラーと同じ IME（日本語・English・한국어、辞書・学習も同じ）で、Ctrl+Space（`SGX_KEY_IME` 0x99）でオン・オフ。キーの定数 `SGX_KEY_MENU`（0x9B、メニューキー）と `SGX_KEY_GUI_L` / `SGX_KEY_GUI_R`（0x9C / 0x9D、左右の GUI キーを押した瞬間に 1 回届く）も足した。SGCALC のセルの編集で使う。5.8 以前のプログラムはそのまま動く。
- 2026-11-15: **5.8**。`set_mode` の印 `SGX_MODE_NO_LR`（0x1000）: モード 3〜5 でもローレゾ面を開かない（テキスト + FB だけ）。ローレゾ面の 50〜60KB の SRAM をテキスト面などに回す。SGCALC はローレゾ面を使わないのにモード 3 で 60KB を取られ、テキスト面の字の形が 17 行分しか取れず、メニューとファンクションキーが出なかった。表は 5.7 と同じ。
- 2026-11-15: **5.7**。`music_pcm_open(path, &frames, &rate)` / `music_pcm_read(stereo, frames)` / `music_pcm_close()` を表の末尾に追加。VGM / VGZ をファームの合成で 44.1kHz・16 ビット・ステレオの PCM として渡す（WAV の書き出しと同じ合成。ファイルを 1 回とおし、ループの所も 1 回）。開いている間は `music_play` / `music_play_mem` / `music_render_wav` を断る。`wav2mp3.sgx` が LAME で MP3 にするため（LAME は `wav2mp3.sgx` がリンクする。対応ソースは sgnano_wav2mp3 リポジトリ）。5.6 以前のプログラムはそのまま動く。
- 2026-11-11: **5.2**。RP2350 内蔵温度センサ `adc_temperature_mc` を追加（§5.27）。5.1 の表の末尾に足しただけなので、5.1 のプログラムはそのまま動く。
  `apps/` の SGX はすべて 5.2 で組み直した（`build/sgx/*.sgx`）。S: の空きが 4KB のため、更新版は
  **A:/testapp** に置いた（`api.rb` / `api.py` / `api.bas` を含む）。4.x 以降のプログラムは 5.2 のファームで動く。
- 2026-11-11: **5.1**。ADC（単発の `adc_read` と、ピンを選べる連続取り込み `adc_start` / `adc_stop` / `adc_stats` / `adc_pins`）を追加（§5.27）。5.0 の表の末尾に足しただけなので、5.0 のプログラムはそのまま動く。
- 2026-11-11: **5.0**。GPIO と PIO2（ユーザー開放）を追加（§5.26）。`gpio_*` と `pio2_*` を表の末尾に足した。4.x の表の末尾に足しただけなので、ファームは 4.x のプログラムも読み込む（`SGX_ABI_COMPAT_MAJOR = 4`）。
- 2026-11-11: **4.13**。MIDI（`midi_available` / `midi_read` / `midi_write` / `midi_available_bytes` / `midi_usb_available` / `midi_set_route` / `midi_input_route` / `midi_output_route`）を追加（§5.25）。4.12 以前のプログラムはそのまま動く。
- 2026-11-14: **5.6**。`create_file(path, size)`（size バイトの 0 のファイルを作る。あれば置き換える）と `ra_write(f, offset, src, len)`（`ra_open` で開いたファイルのその場へ書く。offset と len は 512 の倍数、ただしファイルの終わりに届く最後の 1 回は端数でよい。大きさは変えない。S: は `SGX_EROFS`）。流し書きと違って下回りを持ち続けないので、書きながらほかのファイルを `ra_read` で読める。SMF2VGM.SGX（サウンドフォントを読みながら VGZ を書く。gzip の大きさは先に 1 回数える）のため。表の末尾に足したので 5.5 以前のプログラムはそのまま動く。
- 2026-11-13: **5.5**。`music_play_mem(data, len, flags)`（メモリの VGM / VGZ を鳴らす。既定はファームが中身を写す。`SGX_MUSIC_NOCOPY`（0x0100）なら写さずに使い、呼び手は `music_stop` か次の曲まで返さない）。止める・一時停止・音量・様子は `music_*` のまま。Doom の OPL2 の BGM（MIDI を GENMIDI で YM3812 の VGM に直して渡す）のため。表の末尾に足したので 5.4 以前のプログラムはそのまま動く。
- 2026-11-13: **5.4**。`SGX_MODE_LR_SMALL`（ローレゾ面は 16KB でよい。VGM の合成のオーバーレイの区画を借りずに開く）。表は 5.3 と同じ。
- 2026-11-10: **4.12**。`SGX_MODE_TEXT_SMALL`（モード 7 の 25 行テキスト面）を追加（§5.24）。`MPLAY.SGX` がテキスト面を残したまま 64KB ビットマップを SRAM に置くため。4.11 以前のプログラムはそのまま動く。
- 2026-11-10: **4.11**。`bm_flush`（キャッシュ付き PSRAM ビットマップの書き戻し）と `SGX_MODE_BM_CACHED` を追加（§5.23）。`MPLAY.SGX` がテキスト面を残したまま OGG/MP3 のスペクトログラムを出すため。4.10 以前のプログラムはそのまま動く。
- 2026-11-10: **4.10**。`bm_page_back` / `bm_page_flip`（モード 7 のページ切替ダブルバッファ）を表の末尾に追加（§5.22）。`MPLAY.SGX` のスペクトログラムのラインノイズ対策。4.9 以前のプログラムはそのまま動く。
- 2026-11-10: **4.9**。`music_tap`（鳴らした PCM の直近）と `bm_wrap_clear` / `bm_wrap_set`（モード 7 の横循環）を表の末尾に追加（§5.21）。`MPLAY.SGX` が音楽 CD と同じスペクトログラムを出す。4.8 以前のプログラムはそのまま動く。
- 2026-11-09: **4.8**。`mouse_warp`（システムカーソルを画面座標へ動かす）を表の末尾に追加（§5.3）。4.7 以前のプログラムはそのまま動く。
- 2026-11-09: **4.7**。`music_render_wav` / `music_render_progress`（VGM/VGZ → 96kHz/24bit WAV）を表の末尾に追加（§5.20）。4.6 以前のプログラムはそのまま動く。
- 2026-11-09: **4.6**。`music_sn_pct`（SN76489 の MIX）と `music_voice_chip`（ボイスごとのチップ）を表の末尾に追加（§5.20）。4.5 以前のプログラムはそのまま動く。
- 2026-11-08: **4.5**。音楽の MIX 設定 `music_settings_reload` / `music_ssg_pct` を表の末尾に追加（§5.20）。`S:/SystemSettings/Music.cfg` の `ym2203_ssg=` を読み直す。4.4 以前のプログラムはそのまま動く。
- 2026-11-08: **4.4**。音楽の口 `music_play` / `music_stop` / `music_pause` / `music_volume` / `music_info` / `music_seek` と `sgx_music_info_t`・`SGX_MUSIC_*`・`SGX_VGM_*` を表の末尾に追加（§5.20）。4.3 以前のプログラムはそのまま動く。

- 2026-11-07: **4.3**。表は 4.2 と同じで、決まりだけを緩めた: `fb_lend_spare` はモード 7（テキスト + 320x200x8 のビットマップ）でも貸す。貸している間も、FB を RGB555 のまま使うか使わないモード（0・1・2・7）へは `set_mode` で替えてよい（モード 3・4 は今までどおり断る）。SGX からの切り替えはテストパターンを描かないので、貸したページには触らない。POV-Ray が描いている間モード 7 に縮めて出し（FB を出していると、取得の間 core1 のタスクが待たされて 20 倍遅い）、終わったらモード 2 で FB を出すため。使う側は `abi_minor >= 3` を見る。
- 2026-11-07: **4.2**。voice ごとのストリーム `stream_open` / `stream_write` / `stream_space` / `stream_close` / `stream_underruns` を表の末尾に追加（§5.19）。
  4.1 以前のプログラムはそのまま動く。ファームは音声のリングを voice ごとに持つ（4KB x 4、SRAM）。
- 2026-11-06: **4.1**。オーバーレイの `ovl_*`（§10）を表の末尾に追加。`fb_lend_spare` は番地の小さい順に返す。SDK の `calloc` が自分を呼び続ける形に組まれていたのを直した
  （GCC が malloc + memset(0) を calloc に置き換える。sgnano_povray が実機で見つけた。モード 2 で `store_open` が止まると見えていたのは、その直前の `calloc` だった）。4.0 で組んだものはそのまま動く
- 2026-11-06: **4.0**。FB の余りのページを借りる `fb_lend_spare`（§5.17）。ブロックストア（`store_open` / `store_close` / `store_get` / `store_release` / `store_prefetch` / `store_flush` / `store_stats` / `store_error`、
  `sgx_store_config_t`・`sgx_store_stats_t`・`SGX_STORE_*`）と `SGX_EBUSY` を表の末尾に追加（§5.16）。**3.x で組んだ `.sgx` はそのまま動く**（`SGX_ABI_COMPAT_MAJOR`）。
  ファームの側: `.sgx` をファイルから置き場へ直に読む（ファイラー・シェルの 1MB の上限と、像の 2 倍の PSRAM を使っていたのをやめた）。
  シェルの子の起動の誤りは理由ごとの番号（PSRAM が足りない = `SGX_ENOMEM`）。exFAT・NTFS の書き込みの作業場を PSRAM から取ったとき返し損ねていた（`appmem.c`）のも直した。
  sgnano_povray（POV-Ray 3.7 の移植）の素案 `SPEC_sgx4_overlay_store.md` から決めた。素案のオーバーレイ（固定スロットへのコードの入れ替え）と `.SGX4` の入れ物は
  4.0 には入れない（POV-Ray のコードが約 1.4MB と測れて、常駐で収まるため。要るときに 4.x で足す）。§9
- 2026-11-06: **3.5**。stderr へ書く `term_write_err` を表の末尾に追加（§5.15）。3.4 で組んだ `.sgx` はそのまま動く
- 2026-11-04: **3.4**。シェルから組み込みトラッカーを開く `request_tracker` を表の末尾に追加。3.3 で組んだ `.sgx` はそのまま動く
- 2026-11-04: **3.3**。組み込みアプリの UI の訳 `translate` と `SGX_F_LOCALIZE`（0x10）を追加。3.2 で組んだ `.sgx` はそのまま動く
- 2026-11-03: **3.2**。S: のクラスタ地図とデフラグ・エンフラグ・0 クリア（`sysdisk_*`）を表の末尾に追加。`apps/defrag`（`defrag.sgx` / `enfrag.sgx`）を追加。
  3.1 で組んだ `.sgx` はそのまま動く（3.2 で組んだものは 3.1 以前のファームでは読み込まない）
- 2026-11-02: **3.1**。`mem_info` と `task_info` を表の末尾に追加（`mem` / `ps` のコマンド。`apps/mem`・`apps/ps`）。3.0 で組んだ `.sgx` はそのまま動く
- 2026-10-31（ABI は 3.0 のまま）: SDK の `sgx_libc.c` に newlib の再入用の確保の口（`_malloc_r` / `_free_r` / `_realloc_r` / `_calloc_r`）を足した。
  `strdup`・`asprintf` などが NULL を返していた（sgbasic の `LOAD` が失敗）。ファームの側: 止める頼み（ESC）から 2 秒たっても終わらないプログラムは、
  タスクに `SIGKILL` を送って終わらせる（終了コード 137）。`term_getline` に UART から打てなかった（キーの印が残っていた）のを直した
- 2026-10-31: **3.0**（ユーザー判断で B 案）。ローレゾのマップを 1 ます 32 ビットから **16 ビット**へ（マップ 2 面が 32KB -> 16KB、モード 3 の SRAM を空けるため）。
  `lr_map` は `uint16_t *`。ます = bit0-9 タイル（0〜1023 = パターンの先頭 64KB）・bit10 横反転・bit11 縦反転・bit12-15 パレット（0〜15）（`SGX_LR_ME_*`、`SGX_LR_ME_TILES` / `SGX_LR_ME_PALS`）。
  オブジェクトはこれまでどおりパターン全体・`lr_palettes()` 本。表の並びは 2.0 と同じ。**2.x で組んだ `.sgx` は 3.0 のファームでは読み込まない**（古い形で書くとマップの外を壊すので）。
  `apps/` のすべてと `sgnano_tracker` を 3.0 で組み直した。sgbasic は別に作業しているので、あとで合わせる（ユーザー判断）
- 2026-10-31: **2.0**（ユーザー判断で 1.x を区切った）。表の並び・意味は 1.8 と同じで、`SGX_ABI_MAJOR` を 2、`SGX_ABI_MINOR` を 0 にした。
  ファームは major の違うプログラムを読み込まないので、**1.x で組んだ `.sgx` は 2.0 のファームでは動かない（組み直す）**。2.0 で組んだものも 1.x のファームでは動かない。
  `sgx_libc.c` は倍精度の表（`double_funcs`）を「major が 2 以上か minor が 1 以上」で使う（2.0 で minor が 0 に戻るため）。
  `~/Project` の SGX アプリ（`apps/` のすべてと `sgnano_tracker`）を 2.0 で組み直した
- 2026-10-30: 1.8。ローレゾのオブジェクトのアルファを削除。`attr` bit15 と `attr2` bit8-15 は予約（0 にすること）
- 2026-10-30: 1.7。`sock_open` / `sock_open_tls` / `sock_start_tls` / `sock_write` / `sock_read` / `sock_close` / `sock_closed` / `sock_error` / `sock_available` / `sock_peer`（TCP / TLS 1.2）と、`fft` / `fft_real`（2〜4096 点）を表の末尾に追加。見本は `apps/abitest`。1.7 で組んだものは 1.6 以前のファームでは読み込まない（1.6 で組んだものは 1.7 のファームで動く）
- 2026-10-29: 1.6。ローレゾのオブジェクトにアルファを追加（2026-10-30 の 1.8 で削除。いまは不透明のみ）
- 2026-10-29: 1.5。`seq_play` が SMF だけでなく **SGT v1 / v2**（SGNO TRACKER の曲）も再生できるようになった。SGT は 4 トラックを 4 ポートへ割り当て、音色 0〜8（WT + NOISE）と効果 A/C/D/F/H/P/Q を扱う。SGT の効果音（`seq_sfx`）は未対応。1.5 で組んだものは 1.4 以前のファームでは読み込まない（1.4 で組んだものは 1.5 のファームで動く）
- 2026-10-29: 1.4。`SGX_WAVE_NOISE` / `SGX_SND_NOISE`（RNG から作る白色ノイズの波形とモード。WT と同じ音程・EG。4ch 目のドラム用）を足した。1.4 で組んだものは 1.3 以前のファームでは読み込まない（1.3 で組んだものは 1.4 のファームで動く）
- 2026-10-28: 1.3。`raster_wave`（サイン波テーブルから全ラインのスクロールを一括で）を表の末尾に追加。1.3 で組んだものは 1.2 以前のファームでは読み込まない（1.2 で組んだものは 1.3 のファームで動く）
- 2026-10-28: 1.2。`raster_clear` / `raster_backdrop` / `raster_lowres`（ラスタオペレーションリスト）を表の末尾に追加。見本 `apps/raster`。1.2 で組んだものは 1.1 以前のファームでは読み込まない（1.1 で組んだものは 1.2 のファームで動く）
- 2026-11-04: `SGX_F_FULL_TEXT`（8）を追加（フラグのみ。ABI の表は 3.2 のまま）。30 行テキストを使う SGX アプリ向け。
- 2026-10-25: 1.1。`double_funcs`（倍精度の計算を DCP で）、`SGX_F_WAIT_KEY`。見本にテスト用アプリの C 版（`apps/testapp`）・`hat`・`m640`・`mandel`。1.1 で組んだものは 1.0 のファームでは読み込まない（1.0 で組んだものは 1.1 のファームで動く）
- 2026-10-24: 1.0。Doom をファームから外して外部実行ファイルにした。再配置（ユーザー指示: 複数のタスクの協調があるので）・タスク・同期・spawn

### 5.15 stderr（3.5、2026-11-06）

| 関数 | 中身 |
|---|---|
| `void term_write_err(const char *s, unsigned n)` | stderr へ書く。シェル sgsh から走らせたときは stderr の行き先（`2>`・パイプでは stdout と分かれる）、ファイラーから走らせたときは `term_write` と同じ画面の端末 |

- 末尾に足しただけなので、3.0〜3.4 のプログラムはそのまま動く。使う前に `size` でこの欄があるか確かめる（`offsetof(sgx_api_t, term_write_err)`）
- SG BASIC は誤りの知らせ（`?SN ERROR IN 120` など）をこれで書く（`SgTarget.write_err`。ホスト版はこれまでどおり stdout）

## 9. ABI 4.0 の決め方（2026-11-06）

sgnano_povray の素案（`~/Project/sgnano_povray/SPEC_sgx4_overlay_store.md`）を読み、次のように決めた。

| 素案 | 4.0 | わけ |
|---|---|---|
| `.SGX4` の入れ物（常駐の像・モジュール・ストアの表） | 入れない。`.sgx`（`SGX1`）のまま | モジュールが要らなければ見出しを変える理由がない。大きな像の二重取りは、ファームが `.sgx` を直に読むようにして解いた |
| オーバーレイ（`overlay_*`、固定スロット） | 入れない（4.x で足せる） | POV-Ray 3.7 の `source/` は ARM の `-Os` で約 1.4MB（libc 込みで 2MB 前後の見込み）。`SGX_LOAD_MAX` 4MB の中で常駐できる |
| ブロックストア `store_*` | **入れる** | メッシュ・テクスチャ・チェックポイントを PSRAM の外へ。素案の形をほぼそのまま |
| `store_pin` / `store_unpin` | 入れない | `store_get` が取り出し（pin）、`store_release` が返す（unpin）なので同じことになる |
| `sgx4_store_config_t.lba` / `SGX_STORE_RAW` | 印だけ予約（`-SGX_ENOTSUP`） | 区画に生で書くのは、ほかのドライブ（カードリーダーなど）を壊す危険が大きい。ファイルの置き場の一覧の中だけを書く |
| 作るときに中身を書かない（`usbfat_allocate`） | 0 で埋めて作る | 初めの中身が決まる（0）。FAT・exFAT・NTFS で同じ流し書きの道を使える。代わりに 1MB に約 2 秒かかる |
| `SGX4_EBUSY` / `EBUS` / `ECORRUPT` / `ETIMEDOUT`（64〜） | `SGX_EBUSY`（14）だけ | 抜けた・時間切れは今までの `SGX_EIO` / `SGX_ENODEV`。番号は `SGX_E*` の続き |
| ブロックの大きさ 4KB〜256KB | 512 B〜256KB | セクタの単位まで小さくできる |
| `FB_PAGES=1` の別の組み方 | しない。代わりに `fb_lend_spare`（§5.17） | 組み方を分けずに、1 ページだけ表示するプログラムが残り 3 ページ（1.8MB）を借りられるようにした。実機の空きは §5.18 |

## 10. オーバーレイ（SDK の部品 2026-11-06、ABI 4.1 で表にも入れた）

SDK の部品（`sdk/sgx/include/sgx_overlay.h`・`sgx_overlay.c`・`sgx_ovl.ld`・`sgx_ovl_pack.py`）としても、**ABI 4.1 の表の `ovl_*`** としても使える
（2026-11-06 ユーザー指示: sgnano_povray の試しのあと ABI に組み込む）。中身は同じ `sgx_overlay.c`（ファームは `src/sgx/sgx_ovl_fw.c` が取り込む）。

| 口 | 使い分け |
|---|---|
| ABI 4.1 の `ovl_open(path, host)`・`ovl_add_slot(bytes)`・`ovl_add_slot_mem(base, bytes)`・`ovl_bind` ほか | ファームがパックのファイル（`rbfs_ra_*`）とスロットの PSRAM を受け持つ。プログラムごとにパック 1 つ、終われば閉じて返す。ふつうはこちら |
| SDK の `sgx_ovl_open(&o, &io, host)` ほか | 読み出しの口とスロットをプログラムが持つ（パックを別の所から読む・4.0 のファームで動かす・読み出しを差し替えて試す） |

大きなプログラムを「常駐のコア」と「入れ替えるモジュール」に分け、モジュールをパック（1 つのファイル）から決まった置き場（スロット）へ読み込んで、
関数の表を通して呼ぶ。MMU が無いので、モジュールは番地 0 に結び、読み込むときに再配置の表の語へスロットの番地を足す。

| 段 | 中身 |
|---|---|
| モジュール | `SGX_OVL_MODULE(init)` で見出しを置き、`sgx_add_overlay_module`（`sgx_ovl.ld`、libc は結ばない）で組む。`init(ctx, iface, size)` が関数の表 `iface` を埋める。常駐の機能は `ctx->host` の表から使う（常駐のシンボルに結ぶとパックの道具が断る） |
| パック | `sgx_add_overlay_pack(<target> OUTPUT x.ovl MODULES 名前=モジュール ...)`。頭の表（名前・位置・像・.bss・再配置の数）と各モジュールの像と表 |
| 常駐 | `sgx_overlay.c` を SOURCES に足す。`sgx_ovl_open`（読み出しの口 `read` は `ra_read` などで）・`sgx_ovl_add_slot`（`psram_alloc` や `fb_lend_spare` のページ）・`sgx_ovl_bind`（読み込んで init）・`sgx_ovl_pin` / `unpin`・`sgx_ovl_gen`（読み込み直したか） |

- 読み込みの確かめ: パックと像の magic・版、像 + .bss がスロットに入るか（`-SGX_ENOMEM`）、再配置の位置が像の中で 4 の倍数か、入口がスロットの中の Thumb の番地か（`-SGX_EINVAL`）。
  途中で失敗したらスロットは空にする（半端な像を呼ばせない）。pin のあいだは替えない（`-SGX_EBUSY`）。書いたあと DSB / ISB（`io.sync` で足せる）
- パックの道具が断るもの: MOVW / MOVT の絶対番地・モジュールの外のシンボルへの絶対の参照・見出しの無い像・名前の重なり
- 決まり: モジュールを替えると、前のモジュールの関数・定数・データの番地はすべて無効（C++ の仮想関数の表も）。生きている間は pin する。
  モジュールには静的な初期化（`.init_array`）・例外の表は無い。同じモジュールを 2 つのスロットへ入れると、データは別々
- 試し: **`scripts/test_overlay.py`（QEMU mps2-an505）**。`apps/ovltest/gen_modules.py` が大きな定数の表を持つモジュール 14 個（合わせて 1,007KB、最大 124KB）と、
  入らないもの 2 つ（像 208KB・像 60KB + .bss 80KB）を作り、128KB のスロット 2 つへ 300 回入れ替えながら呼ぶ（`apps/ovltest/ovl_driver.c`、パックはセミホスティングでホストのファイルから）。
  読み込み直したときの checksum（像が全部正しく、.bss が 0）・apply の値（関数ポインタの表・定数・.data の数え・常駐への呼び戻し）・回数・常駐の数え・わざと起こす誤り 16 通り・パックの道具の断り 3 通りを Python の計算と照らし合わせる。
  2026-11-06: 276 回の読み込み（20MB 読み・再配置 7,554 語）ですべて一致。読み込み側にわざと入れた誤り（.bss を 0 にしない・番地を直さない・pin を見ない）はどれも見つけた
- 実機の見本: `apps/ovltest`（`build/sgx/ovltest.sgx` と `ovltest.ovl` を同じフォルダに置き、シェルから走らせて UART のログを `scripts/test_overlay.py --check-log` で照らし合わせる）。
  SDK の道と ABI 4.1 の道を続けて回す。**2026-11-06 実機（HDD の exFAT、シェルから）**: どちらも 300 回・276 回の読み込みで Python の計算と一致。SDK の道 37.4 秒、ABI の道 44.8 秒（どちらも約 20MB 読む）。
  1 回目は 211 回目の読み込みの途中で HDD（JMS578）が答えなくなった（積み残し 28・36 と同じ形）。リセットのあとの 2 回目は通った
