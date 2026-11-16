# 同梱物のライセンス

| ファイル | 対象 | ライセンス |
|---|---|---|
| `lame-LGPL-2.0.txt` | `third_party/lame/` の LAME 3.100 | LGPL-2.0-or-later（GNU Library General Public License version 2） |
| `lame-LICENSE.txt` | LAME の配布物に付いている説明 | LAME の LICENSE |
| `sdk-MIT.txt` | `sdk/sgx/` の sgnano SGX SDK | MIT |
| `minimp3-CC0.txt` | `third_party/minimp3/`（ホスト試験のみ） | CC0-1.0 |
| `docs-CC-BY-SA-4.0.txt` | `docs/` の SGX ABI の文書 | CC BY-SA 4.0 |

このリポジトリの独自部分（`apps/`・`host/`・`tests/`・CMake など）は LGPL-2.0-or-later です。
全文はルートの `LICENSE` にあります。

LAME は sgnano のファームウェア / `wav2mp3.sgx` がリンクします。対応するソースと
ビルド手順は、このリポジトリの `third_party/lame/`・`apps/wav2mp3/`・`sdk/sgx/`・
`CMakeLists.txt`・`cmake/` にあります。
