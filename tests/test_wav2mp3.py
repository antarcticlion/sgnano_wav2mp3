#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""wav2mp3（apps/wav2mp3 の w2m.c + LAME 3.100）のホスト試験（2026-11-14）。

- 速さ（32k / 44.1k / 48k と 2 倍・4 倍）・チャンネル・ビット数（8 / 16 / 24 / 32 / float）・長さを変えた WAV を作って変換する。
- 出力の大きさが前もって計算した値と同じか（w2mhost が違えば失敗を返す）。
- minimp3 で戻せるか: 速さ（2 倍・4 倍は半分・1/4）・チャンネル・フレームの数・ファイルの終わりまで読めるか。
- 戻した音が元の正弦波と同じ高さ（FFT の山）と、だいたい同じ大きさか。

  tests/test_wav2mp3.py [--w2mhost PATH] [--mp3dec PATH]

--w2mhost / --mp3dec を省くと、その場で gcc で組み立てる（CMake を通さない素の試験）。
"""
import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
ok_all = True


def check(ok, what):
    global ok_all
    print(('  OK  ' if ok else '  NG  ') + what)
    if not ok:
        ok_all = False


def build(tmp):
    gcc = shutil.which('gcc')
    if not gcc:
        sys.exit('gcc not found: install gcc, or pass --w2mhost and --mp3dec')
    lame = os.path.join(ROOT, 'third_party/lame')
    w2m = os.path.join(tmp, 'w2mhost')
    subprocess.check_call([gcc, '-O2', '-w', '-DHAVE_CONFIG_H',
                           '-I' + lame, '-I' + lame + '/include', '-I' + lame + '/libmp3lame',
                           '-I' + os.path.join(ROOT, 'apps/wav2mp3'), '-o', w2m,
                           os.path.join(ROOT, 'host/w2mhost.c'), os.path.join(ROOT, 'apps/wav2mp3/w2m.c')] +
                          [os.path.join(lame, 'libmp3lame', f) for f in sorted(os.listdir(os.path.join(lame, 'libmp3lame'))) if f.endswith('.c')] +
                          ['-lm'])
    dec = os.path.join(tmp, 'mp3dec')
    subprocess.check_call([gcc, '-O2', '-w', '-I' + os.path.join(ROOT, 'third_party/minimp3'), '-o', dec,
                           os.path.join(ROOT, 'host/mp3dec.c'), '-lm'])
    return w2m, dec


def write_wav(path, rate, ch, bits, frames, freq=1000.0, amp=0.5, fmt=1):
    data = bytearray()
    for i in range(frames):
        for c in range(ch):
            f = freq * (1.0 + 0.5 * c)                      # 右は 1.5 倍の高さ
            x = amp * math.sin(2 * math.pi * f * i / rate)
            if fmt == 3:
                data += struct.pack('<f', x)
            elif bits == 8:
                data += bytes((max(0, min(255, int(round(x * 127 + 128)))),))
            elif bits == 16:
                data += struct.pack('<h', int(round(x * 32767)))
            elif bits == 24:
                v = int(round(x * 8388607)) & 0xFFFFFF
                data += bytes((v & 0xFF, (v >> 8) & 0xFF, v >> 16))
            else:
                data += struct.pack('<i', int(round(x * 2147483647)))
    block = ch * bits // 8
    fmtc = struct.pack('<HHIIHH', fmt, ch, rate, rate * block, block, bits)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmtc)) + fmtc + b'data' + struct.pack('<I', len(data)) + bytes(data)
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)


def peak_hz(x, rate):
    n = 1
    while n * 2 <= len(x) and n < 16384:
        n *= 2
    seg = x[len(x) // 2 - n // 2: len(x) // 2 + n // 2]
    best, bk = 0.0, 0
    for k in range(1, n // 2):                              # 粗い DFT（山の近くだけ見れば足りるが、短いので全部）
        if k % 1 != 0:
            continue
    # numpy が無くても動くよう、ゲルツェルで 50Hz 刻みに 200..5000Hz を探す
    for f in range(200, 5001, 25):
        w = 2 * math.pi * f / rate
        c = 2 * math.cos(w)
        s1 = s2 = 0.0
        for v in seg[::1]:
            s0 = v + c * s1 - s2
            s2, s1 = s1, s0
        p = s1 * s1 + s2 * s2 - c * s1 * s2
        if p > best:
            best, bk = p, f
    return bk


def run_case(w2m, dec, tmp, rate, ch, bits, frames, fmt=1, extra=(), label=''):
    wav = os.path.join(tmp, 'in.wav')
    mp3 = os.path.join(tmp, 'out.mp3')
    raw = os.path.join(tmp, 'out.raw')
    write_wav(wav, rate, ch, bits, frames, fmt=fmt)
    r = subprocess.run([w2m, wav, mp3] + list(extra), capture_output=True, text=True)
    name = label or '%d Hz %d ch %d bit%s %d frames %s' % (rate, ch, bits, ' float' if fmt == 3 else '', frames, ' '.join(extra))
    if r.returncode:
        check(False, '%s: %s' % (name, r.stderr.strip()))
        return
    size = os.path.getsize(mp3)
    d = subprocess.run([dec, mp3, raw], capture_output=True, text=True).stdout.split()
    hz, dch, dframes, nfr, used = int(d[0]), int(d[1]), int(d[2]), int(d[3]), int(d[4])
    out_rate = rate
    while out_rate > 48000:
        out_rate //= 2
    want_ch = 1 if (ch == 1 or '--mono' in extra) else 2
    good = (used == size and (frames == 0 or (hz == out_rate and dch == want_ch)))
    msg = '%s: %d bytes (as predicted), %d frames decode to %d Hz %d ch' % (name, size, nfr, hz, dch)
    if good and frames >= 20000:
        pcm = open(raw, 'rb').read()
        s = struct.unpack('<%dh' % (len(pcm) // 2), pcm)
        left = [v / 32768.0 for v in s[0::dch]]
        f = peak_hz(left, hz)
        rms = math.sqrt(sum(v * v for v in left[len(left) // 4: 3 * len(left) // 4]) / max(1, len(left) // 2))
        if want_ch == 1 and ch == 2:                        # ステレオをモノラルに混ぜた: 1000Hz と 1500Hz が 0.25 ずつ（RMS は 0.25）
            good = good and (abs(f - 1000) <= 25 or abs(f - 1500) <= 25) and 0.20 < rms < 0.30
        else:
            good = good and abs(f - 1000) <= 25 and 0.30 < rms < 0.40      # 0.5 の正弦波の RMS は 0.354
        msg += ', peak %d Hz, rms %.3f' % (f, rms)
        if want_ch == 2:
            right = [v / 32768.0 for v in s[1::dch]]
            fr = peak_hz(right, hz)
            good = good and abs(fr - 1500) <= 25
            msg += ', right %d Hz' % fr
    check(good, msg)


def run_pcm_case(w2m, dec, tmp, rate, frames, extra=(), label=''):
    """w2m_open_pcm（VGM の合成の代わり）: 正弦波を読ませて MP3 にし、戻して確かめる。"""
    mp3 = os.path.join(tmp, 'pcm.mp3')
    raw = os.path.join(tmp, 'pcm.raw')
    r = subprocess.run([w2m, '--pcm', str(rate), str(frames), mp3] + list(extra),
                       capture_output=True, text=True)
    name = label or 'pcm %d Hz %d frames %s' % (rate, frames, ' '.join(extra))
    if r.returncode:
        check(False, '%s: %s' % (name, (r.stderr or r.stdout).strip()))
        return
    size = os.path.getsize(mp3)
    d = subprocess.run([dec, mp3, raw], capture_output=True, text=True).stdout.split()
    hz, dch, dframes, nfr, used = int(d[0]), int(d[1]), int(d[2]), int(d[3]), int(d[4])
    out_rate = rate
    while out_rate > 48000:
        out_rate //= 2
    want_ch = 1 if ('--mono' in extra or rate == 0) else 2
    good = (used == size and hz == out_rate and dch == want_ch and nfr > 0)
    msg = '%s: %d bytes (as predicted), %d frames decode to %d Hz %d ch' % (name, size, nfr, hz, dch)
    if good and frames >= 20000:
        pcm = open(raw, 'rb').read()
        s = struct.unpack('<%dh' % (len(pcm) // 2), pcm)
        left = [v / 32768.0 for v in s[0::dch]]
        f = peak_hz(left, hz)
        rms = math.sqrt(sum(v * v for v in left[len(left) // 4: 3 * len(left) // 4]) / max(1, len(left) // 2))
        if want_ch == 1:
            good = good and (abs(f - 1000) <= 25 or abs(f - 1500) <= 25) and 0.20 < rms < 0.30
        else:
            good = good and abs(f - 1000) <= 25 and 0.30 < rms < 0.40
        msg += ', peak %d Hz, rms %.3f' % (f, rms)
        if want_ch == 2:
            right = [v / 32768.0 for v in s[1::dch]]
            fr = peak_hz(right, hz)
            good = good and abs(fr - 1500) <= 25
            msg += ', right %d Hz' % fr
    check(good, msg)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--w2mhost', help='w2mhost の場所（省くと gcc で組み立てる）')
    ap.add_argument('--mp3dec', help='mp3dec の場所（省くと gcc で組み立てる）')
    args = ap.parse_args()
    if bool(args.w2mhost) != bool(args.mp3dec):
        sys.exit('--w2mhost と --mp3dec は両方指定するか、両方省いてください')
    with tempfile.TemporaryDirectory() as tmp:
        w2m, dec = (args.w2mhost, args.mp3dec) if args.w2mhost else build(tmp)
        # 速さ（2 倍・4 倍は自前の半帯域フィルタで落とす）
        for rate in (32000, 44100, 48000, 64000, 88200, 96000, 176400, 192000):
            run_case(w2m, dec, tmp, rate, 2, 16, rate // 2)
        # ビット数・形
        for bits, fmt in ((8, 1), (24, 1), (32, 1), (32, 3)):
            run_case(w2m, dec, tmp, 96000, 2, bits, 48000, fmt=fmt)
        run_case(w2m, dec, tmp, 44100, 1, 16, 30000)
        run_case(w2m, dec, tmp, 96000, 2, 24, 50000, extra=('--mono',))
        # 長さの端（フレームの数の計算）
        for n in (0, 1, 575, 576, 577, 1151, 1152, 1153, 2303, 2305, 4608, 12345):
            for rate in (44100, 48000, 96000):
                run_case(w2m, dec, tmp, rate, 2, 16, n)
        # ビットレート・品質
        for br in ('128', '192', '256', '320'):
            run_case(w2m, dec, tmp, 44100, 2, 16, 30000, extra=('--bitrate', br))
        for q in ('0', '5', '7', '9'):
            run_case(w2m, dec, tmp, 48000, 2, 16, 30000, extra=('-q', q))
        # w2m_open_pcm（VGM / VGZ の合成の代わり。SGX ABI 5.7）
        for rate in (44100, 48000):
            run_pcm_case(w2m, dec, tmp, rate, rate)
        run_pcm_case(w2m, dec, tmp, 44100, 12345)
        run_pcm_case(w2m, dec, tmp, 48000, 30000, extra=('--mono',))
        run_pcm_case(w2m, dec, tmp, 96000, 48000)
    print('all OK' if ok_all else 'FAILED')
    return 0 if ok_all else 1


if __name__ == '__main__':
    sys.exit(main())
