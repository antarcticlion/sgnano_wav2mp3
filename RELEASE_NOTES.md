# sgnano_wav2mp3

WAV / VGM / VGZ を MP3 にする、sgnano の外部実行ファイル `wav2mp3.sgx` のリリースです。

## 同梱物

| ファイル | 中身 |
|---|---|
| `wav2mp3.sgx` | 実機用の SGX プログラム（S:/bin に置く） |
| `libmp3lame.a` | SGX / ファームウェア用の LAME 3.100（Cortex-M33、newlib-nano） |
| `lame.h` | LAME の公開ヘッダ |
| `SHA256SUMS` | 上記の SHA-256 |
| `LICENSES-*` | 同梱物のライセンス全文 |
| `sgnano_wav2mp3-*-src.tar.gz` | 対応するソース一式（LGPL のソース配布） |

ソースとビルド手順は、このタグのリポジトリにもあります。

## 使い方

```text
wav2mp3 in.wav [out.mp3] [--bitrate kbps] [-q 0..9] [--mono]
wav2mp3 in.vgm|in.vgz [out.mp3] [--bitrate kbps] [-q 0..9] [--mono]
```

- 既定は 320kbps の CBR、`-q 3`、ステレオは joint stereo。
- 出力名を省くと、入力と同じフォルダ・同じ名前の `.mp3`。
- VGM / VGZ はファームウェアの合成（SGX ABI 5.7 以上）で MP3 にします。
- sgnano のファイラーでは `.wav` / `.vgm` / `.vgz` のメニューから「MP3 に変換」で呼べます。

## ライセンス

`wav2mp3.sgx` は LAME 3.100（LGPL-2.0-or-later）を静的にリンクしています。
対応するソースとビルド手順は、このリポジトリの `third_party/lame/`・`apps/wav2mp3/`・
`sdk/sgx/`・`CMakeLists.txt`・`cmake/` にあります。LAME を差し替えて組み直す手順は
`README.md` の「LAME と LGPL」を見てください。
