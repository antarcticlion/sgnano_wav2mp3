# minimp3 (vendored)

- Upstream: https://github.com/lieff/minimp3
- Commit: `ea99364f61c14656440e8d77e9c233ccf3124633` (2026-07-27)
- License: CC0 1.0 (see `LICENSE`)
- Vendored date: 2026-11-06

## Local use

- `src/codec/minimp3_impl.c` defines `MINIMP3_IMPLEMENTATION` and includes
  `minimp3.h`.
- Only the MP3 decoder is used. The `minimp3_ex.h` helper API is not vendored.

## Update procedure

1. Replace `minimp3.h` with the new upstream file.
2. Rebuild and run the host codec tests and the QEMU benchmark.
