# sgnano SGX SDK（写し）

- 出所: sgnano 本体の `sdk/sgx/`（2026-11-16、SGX ABI 5.11 相当）
- ライセンス: MIT（`LICENSE`）
- ここにあるのは、`wav2mp3.sgx` を単体で組むための写しです。

## このリポジトリでの変更

- `sgx_add_program` に `LIBS` を足しました（2026-11-17）。LAME の静的ライブラリ
  `libmp3lame.a` を `-lm` / `-lc` / `-lgcc` より前に置くためです（`powf` / `log10f` の解決）。
  指定しないときの動きは元の SDK と同じです。

```cmake
sgx_add_program(wav2mp3 ... LIBS mp3lame ...)
```
