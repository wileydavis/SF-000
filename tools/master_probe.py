#!/usr/bin/env python3
"""SF-000: a test signal for measuring what a mix-bus processor does.

Writes a 48 kHz, 24-bit stereo WAV made of plain signals at known levels. Played through a
processor at the settings in use and recorded back, the pair says what the processor does:
its tone at each level (sweeps), how hard it holds each band (noise at stepped levels), how
fast (tone steps), what it adds (harmonics of the sweeps, two tones), what it does to the
sides (middle only, sides only), and how long it takes (clicks).

    python3 tools/master_probe.py out.wav

The signal is the same every time it is made (the noise is seeded), so the file need not be
kept: the analysis makes it again. SEGMENTS lists what is where, in seconds.
"""
import sys, struct
import numpy as np

SR = 48000
SEGMENTS = []          # (name, start seconds, length seconds, level dBFS)

def db(x): return 10.0 ** (x / 20.0)

def fade(x, ms=10.0):
    n = int(SR * ms / 1000.0)
    w = np.ones(len(x)); r = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))
    w[:n] = r; w[-n:] = r[::-1]
    return x * w

def sweep(seconds, peak_db, f0=20.0, f1=20000.0):
    t = np.arange(int(SR * seconds)) / SR
    k = np.log(f1 / f0)
    return fade(db(peak_db) * np.sin(2 * np.pi * f0 * seconds / k * (np.exp(t / seconds * k) - 1.0)), 20.0)

def pink(seconds, rms_db, rng):
    n = int(SR * seconds)
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / SR); f[0] = f[1]
    spec /= np.sqrt(f)
    spec[f < 20.0] = 0.0
    x = np.fft.irfft(spec, n)
    return fade(x / np.sqrt(np.mean(x * x)) * db(rms_db), 20.0)

def step(freq, low_db, high_db):
    # half a second low, a second high, a second and a half low: how fast it takes hold and lets go
    t = np.arange(int(SR * 3.0)) / SR
    env = np.where((t >= 0.5) & (t < 1.5), db(high_db), db(low_db))
    return fade(env * np.sin(2 * np.pi * freq * t), 5.0)

def build():
    rng = np.random.default_rng(20261115)
    left, right = [], []
    at = 0.0
    def put(name, l, r, level, gap=0.6):
        nonlocal at
        SEGMENTS.append((name, at, len(l) / SR, level))
        left.append(l); right.append(r); at += len(l) / SR
        g = np.zeros(int(SR * gap)); left.append(g); right.append(g); at += gap
    z = np.zeros(SR); left.append(z); right.append(z); at += 1.0
    c = np.zeros(int(SR * 3.0)); c[::SR // 2] = db(-6.0)
    put("clicks", c, c, -6.0)
    for lv in (-30.0, -18.0, -12.0, -6.0):
        s = sweep(5.0, lv); put("sweep", s, s, lv)
    for lv in (-36.0, -30.0, -24.0, -18.0, -15.0):     # (the loudest: its peaks are just under full scale)
        p = pink(3.0, lv, rng); put("pink", p, p, lv)
    for f in (100.0, 1000.0, 6000.0):
        s = step(f, -30.0, -10.0); put("step %d" % f, s, s, -10.0)
    a, b = pink(3.0, -23.0, rng), pink(3.0, -23.0, rng)
    put("wide (left and right apart)", a, b, -23.0)
    m = pink(3.0, -20.0, rng); put("middle only", m, m, -20.0)
    s = pink(3.0, -20.0, rng); put("sides only", s, -s, -20.0)
    t = np.arange(int(SR * 3.0)) / SR
    two = fade(db(-12.0) * 0.5 * (np.sin(2 * np.pi * 60.0 * t) + np.sin(2 * np.pi * 7000.0 * t)), 20.0)
    put("two tones (60 Hz and 7 kHz)", two, two, -12.0)
    return np.concatenate(left), np.concatenate(right)

def write(path, l, r):
    x = np.clip(np.stack([l, r], axis=1), -1.0, 1.0)
    i = np.round(x * 8388607.0).astype(np.int32).reshape(-1)
    raw = bytearray(len(i) * 3)
    raw[0::3] = (i & 0xFF).astype(np.uint8).tobytes()
    raw[1::3] = ((i >> 8) & 0xFF).astype(np.uint8).tobytes()
    raw[2::3] = ((i >> 16) & 0xFF).astype(np.uint8).tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(raw)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 2, SR, SR * 6, 6, 24) + b"data" + struct.pack("<I", len(raw)))
        f.write(raw)

if __name__ == "__main__":
    l, r = build()
    if len(sys.argv) > 1: write(sys.argv[1], l, r)
    print("%.1f s, peak %.1f dBFS" % (len(l) / SR, 20 * np.log10(max(np.abs(l).max(), np.abs(r).max()))))
    for name, start, length, level in SEGMENTS: print("  %6.2f s  %4.1f s  %6.1f dB  %s" % (start, length, level, name))
