# sgnano_wav2mp3

sgnano の外部実行ファイル **`wav2mp3.sgx`** の単体リポジトリです。
WAV / VGM / VGZ を MP3（既定 320kbps の CBR）に変換します。エンコーダは **LAME 3.100** です。

このリポジトリの主な目的は、**LGPL の LAME の対応ソースとビルド手順を配布すること**です。
sgnano のファームウェア / `wav2mp3.sgx` は LAME をリンクします。そのソース・ヘッダ・
ビルドに使う SDK を、このリポジトリにまとめてあります。

- `wav2mp3.sgx` … 実機で動く SGX プログラム
- `libmp3lame.a` / `lame.h` … SGX 用（Cortex-M33 / newlib-nano）の LAME 静的ライブラリとヘッダ
- ホスト用の `w2mhost` / `mp3dec` と、63 項目のホスト試験

## 必要なもの

| 用途 | 必要なもの |
|---|---|
| ホストの試験 | CMake 3.21 以上、Ninja、gcc、Python 3 |
| SGX のビルド | 上記に加えて `arm-none-eabi-gcc`（sgnano は 10.3.1 で確認） |
| 実機 | sgnano ファームウェア SGX ABI 5.6 以上（VGM / VGZ は 5.7 以上、進み具合の窓は 5.11 以上） |

## ビルド

### ホスト（試験用）

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host --output-on-failure
```

`build/host/w2mhost` と `build/host/mp3dec`、ホスト用の `libmp3lame.a` ができます。
CMake を通さずに試験だけ走らせることもできます（その場で gcc で組み立てます）。

```sh
python3 tests/test_wav2mp3.py
```

### SGX（実機用）

```sh
cmake --preset sgx
cmake --build --preset sgx
```

できるもの:

- `build/sgx/sgx/wav2mp3.sgx` … 実機に置くプログラム
- `build/sgx/third_party/lame/libmp3lame.a` … SGX / ファームウェア用の LAME

プリセットを使わない場合:

```sh
cmake -S . -B build/sgx -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build/sgx
```

### インストール（LAME をほかのプロジェクトから使う）

```sh
cmake --install build/sgx --prefix "$HOME/sgnano-mp3lame"
```

`lib/libmp3lame.a`、`include/lame.h`、`lib/cmake/mp3lame/` が入ります。
ほかの CMake プロジェクトからは次のように使えます。

```cmake
find_package(mp3lame CONFIG REQUIRED)
target_link_libraries(my_sgx PRIVATE mp3lame::mp3lame)
target_link_libraries(my_sgx PRIVATE m)
```

CMake を使わない場合は、`libmp3lame.a` と `lame.h` を直接リンクしてください。

```cmake
target_include_directories(my_sgx PRIVATE "${MP3LAME_DIR}/include")
target_link_libraries(my_sgx PRIVATE "${MP3LAME_DIR}/lib/libmp3lame.a" m)
```

## 実機での使い方

`wav2mp3.sgx` を `S:/bin/` に置きます。

```text
wav2mp3 in.wav [out.mp3] [--bitrate kbps] [-q 0..9] [--mono]
wav2mp3 in.vgm|in.vgz [out.mp3] [--bitrate kbps] [-q 0..9] [--mono]
```

- 出力名を省くと、入力と同じフォルダ・同じ名前の `.mp3` になります。
- 既定は 320kbps の CBR、`-q 3`（LAME の既定）、ステレオは joint stereo です。
- 入力は 8 / 16 / 24 / 32 ビット PCM または 32 ビット float、モノラル / ステレオ、
  32 / 44.1 / 48kHz です。88.2 / 96 / 176.4 / 192kHz は半帯域フィルタで 1/2・1/4 に落とします。
- VGM / VGZ はファームウェアの合成（SGX ABI 5.7 の `music_pcm_*`）で 44.1kHz・16 ビット・
  ステレオの PCM として受け取り、1 回とおして MP3 にします。
- ESC で止められます。途中までの出力ファイルは消します。

ファイラーでは `.wav` / `.vgm` / `.vgz` のコンテキストメニューから
「MP3 に変換 (320kbps)」を選ぶと、このプログラムが呼ばれます。

## GitHub Actions と Release

`.github/workflows/build.yml` が次のことをします。

- push / pull request: ホスト試験（63 項目）と SGX のビルド
- `v*` タグ: `wav2mp3.sgx`・`libmp3lame.a`・`lame.h`・ライセンスを GitHub Release に添付

Release の成果物は、sgnano 本体のリリースに同梱したり、ほかの SGX から LAME を使うときに
そのまま使えます。

## フォルダ

| フォルダ | 中身 |
|---|---|
| `apps/wav2mp3/` | `wav2mp3.sgx` の本体（`main_sgx.c`・`w2m.c`・`w2m.h`） |
| `host/` | ホスト用の `w2mhost`（変換）と `mp3dec`（minimp3 で戻して確かめる） |
| `tests/` | ホスト試験（`test_wav2mp3.py`） |
| `third_party/lame/` | LAME 3.100（LGPL-2.0-or-later、エンコーダのみ）。`libmp3lame.a` を作る |
| `third_party/minimp3/` | 試験で MP3 を戻すための minimp3（CC0-1.0） |
| `sdk/sgx/` | sgnano の SGX SDK（MIT）。`sgx_add_program`・リンカスクリプト・libc |
| `cmake/` | ARM のツールチェーン |
| `docs/` | SGX ABI の文書（CC BY-SA 4.0） |
| `LICENSES/` | 同梱物のライセンス全文 |

## LAME と LGPL

- LAME 3.100 は **LGPL-2.0-or-later** です（`third_party/lame/COPYING`、各ソースの頭書き）。
- `wav2mp3.sgx` と、sgnano のファームウェアが LAME をリンクする場合の**対応するソース**は、
  このリポジトリの次のものです。
  - `third_party/lame/`（LAME 本体と `config.h`）
  - `apps/wav2mp3/`（LAME を呼ぶアプリ）
  - `sdk/sgx/`（ビルドに使う SDK）
  - `CMakeLists.txt` / `cmake/`（ビルド手順）
- LAME を差し替えて組み直すには、`third_party/lame/` を新しい LAME に入れ替えて
  `cmake --preset sgx && cmake --build --preset sgx` をやり直します。
- ファームウェアに LAME を静的リンクする場合、LGPL を満たすには、ファームウェア側にも
  LAME を差し替えて再リンクできる手段（対応するソース・ビルド手順、必要ならオブジェクトファイル）が要ります。
  このリポジトリは、そのうち LAME の対応ソースとビルド手順を提供します。
- `libmp3lame.a` はほかの SGX からも使えますが、静的リンクする場合は、その SGX の配布物にも
  LGPL の条件（対応するソースの提供など）がかかります。
- LAME の元の配布元: <https://lame.sourceforge.io/>

## ライセンス

- このリポジトリの独自部分（`apps/`・`host/`・`tests/`・CMake など）: **LGPL-2.0-or-later**
- `third_party/lame/`: **LGPL-2.0-or-later**
- `sdk/sgx/`: **MIT**（`sdk/sgx/LICENSE`）
- `third_party/minimp3/`: **CC0-1.0**（`third_party/minimp3/LICENSE`）
- `docs/`: **CC BY-SA 4.0**（`docs/LICENSE`）

全文は `LICENSE` と `LICENSES/` にあります。
