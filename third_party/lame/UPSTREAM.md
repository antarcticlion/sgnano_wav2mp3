# LAME 3.100（エンコーダの部分）

- 出所: Debian の `lame_3.100.orig.tar.gz`（https://deb.debian.org/debian/pool/main/l/lame/）、
  sha256 `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`（LAME の公式の 3.100 と同じ）。2026-11-14。
- ライセンス: **LGPL-2.0-or-later**（`COPYING`・`LICENSE`）。ソースの頭書きは
  「GNU Library General Public License version 2 または（あなたの選ぶ）それ以降」。
- **sgnano のファームウェア / `wav2mp3.sgx` がこのライブラリをリンクする。**
  LGPL の対応ソースと、LAME を差し替えて組み直す手順は、このリポジトリ（sgnano_wav2mp3）で配布する。
- 入れたもの: `libmp3lame/` の `.c` / `.h`（`mpglib_interface.c`・`i386/` と `vector/` の SSE の中身は除く。
  `vector/lame_intrin.h` は宣言だけなので入れた。`fft.c`・`quantize.c` が取り込む）、`include/lame.h`、
  `COPYING`・`LICENSE`・`README`。
- `config.h` は sgnano が書いたもの（configure は走らせない）。SGX（Cortex-M33、newlib-nano）とホストの両方で使う。
- 手直し: 無し（下に足したら書く）。

## ビルド

`third_party/lame/CMakeLists.txt` が静的ライブラリ `libmp3lame.a` を作る。

- ホスト: `cmake --preset host` → `build/host/third_party/lame/libmp3lame.a`
- SGX: `cmake --preset sgx` → `build/sgx/third_party/lame/libmp3lame.a`

`cmake --install build/sgx --prefix <dir>` で、`lib/libmp3lame.a`・`include/lame.h`・
`lib/cmake/mp3lame/`（`find_package(mp3lame)` 用）を出す。
