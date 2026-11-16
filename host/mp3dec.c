// MP3 を minimp3 で 16 ビットの PCM（ステレオに並べる）へ戻す（試し用）: mp3dec in.mp3 out.raw -> "rate ch frames"
// SPDX-License-Identifier: LGPL-2.0-or-later
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    (void)argc;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (fread(b, 1, n, f) != (size_t)n) return 1; fclose(f);
    FILE *o = fopen(argv[2], "wb");
    mp3dec_t d; mp3dec_init(&d); mp3dec_frame_info_t fi; short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    long pos = 0, frames = 0, nfr = 0; int rate = 0, ch = 0;
    while (pos < n) {
        int s = mp3dec_decode_frame(&d, b + pos, (int)(n - pos), pcm, &fi);
        if (!fi.frame_bytes) break;
        pos += fi.frame_bytes; nfr++;
        if (s) { rate = fi.hz; ch = fi.channels; fwrite(pcm, 2, (size_t)s * fi.channels, o); frames += s; }
    }
    fclose(o);
    printf("%d %d %ld %ld %ld\n", rate, ch, frames, nfr, pos);
    return 0;
}
