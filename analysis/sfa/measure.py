"""Measuring a snapshot's sound: what it sounds like, as numbers that can be compared.

A snapshot is measured from the best of what the library has of it: its sound without loss (the output and
each lane, return and bus on its own) if SF-000 kept it, otherwise its clip and its stems (Ogg). Every
measurement says which it came from.
"""
from __future__ import annotations

import io
import json

import numpy as np
import soundfile as sf
from scipy import signal

LOSSLESS_MARK = b"SF0LOSSLESS1\n"

# the parts a snapshot can have on their own, in the order SF-000 keeps them (FORMAT.md: the stems)
LANES = ["kick", "closed_hat", "open_hat", "clap", "perc", "bass", "rumble", "lead", "chord", "pad", "voice"]
RETURNS = ["dub", "space", "bus"]
PARTS = LANES + RETURNS

BAND_EDGES = [20, 60, 120, 250, 500, 1000, 2000, 4000, 8000, 12000, 20000]
BAND_NAMES = ["sub", "low", "lowmid", "mid1", "mid2", "mid3", "presence", "high", "air1", "air2"]
SILENT_DB = -60.0


# ---------------------------------------------------------------------------------------------- reading
def read_audio(data: bytes) -> tuple[np.ndarray, int]:
    """(frames x 2, rate). One channel is made two."""
    x, rate = sf.read(io.BytesIO(data), dtype="float32", always_2d=True)
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
    return x[:, :2], int(rate)


def read_lossless(data: bytes) -> tuple[dict, dict[str, tuple[np.ndarray, int]]]:
    """SF-000's lossless file: its head, and each part by its key, with the gain it was written with undone."""
    if not data.startswith(LOSSLESS_MARK):
        raise ValueError("not a lossless file")
    end = data.index(b"\n", len(LOSSLESS_MARK))
    head = json.loads(data[len(LOSSLESS_MARK):end])
    at = end + 1
    parts = {}
    for p in head.get("parts", []):
        n = int(p["bytes"])
        chunk = data[at:at + n]
        at += n
        if len(chunk) != n or not chunk.startswith(b"fLaC"):
            raise ValueError("a part cut short")
        x, rate = read_audio(chunk)
        gain = float(p.get("gain", 1.0)) or 1.0
        parts[p["key"]] = (x / gain, rate)
    if at != len(data):
        raise ValueError("the parts do not add up to the file")
    return head, parts


# ---------------------------------------------------------------------------------------------- loudness
def _biquad_k(rate: int):
    """The two filters of ITU-R BS.1770 (K weighting), made for any rate."""
    # high shelf, +4 dB above about 1.5 kHz
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    A = 10 ** (G / 40.0)
    w0 = 2 * np.pi * fc / rate
    alpha = np.sin(w0) / (2 * Q)
    cw = np.cos(w0)
    b1 = [A * ((A + 1) + (A - 1) * cw + 2 * np.sqrt(A) * alpha), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - 2 * np.sqrt(A) * alpha)]
    a1 = [(A + 1) - (A - 1) * cw + 2 * np.sqrt(A) * alpha, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - 2 * np.sqrt(A) * alpha]
    # high pass at about 38 Hz
    Q2, fc2 = 0.5003270373253953, 38.13547087613982
    w0 = 2 * np.pi * fc2 / rate
    alpha = np.sin(w0) / (2 * Q2)
    cw = np.cos(w0)
    b2 = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]
    a2 = [1 + alpha, -2 * cw, 1 - alpha]
    return (np.array(b1) / a1[0], np.array(a1) / a1[0]), (np.array(b2) / a2[0], np.array(a2) / a2[0])


def loudness(x: np.ndarray, rate: int) -> float:
    """Integrated loudness, LUFS (BS.1770: K weighted, 400 ms blocks, gated)."""
    (b1, a1), (b2, a2) = _biquad_k(rate)
    y = signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)
    block, hop = int(0.4 * rate), int(0.1 * rate)
    if len(y) < block:
        z = np.array([np.mean(y ** 2, axis=0).sum()])
    else:
        n = 1 + (len(y) - block) // hop
        sq = np.cumsum(np.concatenate([np.zeros((1, y.shape[1])), y ** 2]), axis=0)
        z = np.array([((sq[i * hop + block] - sq[i * hop]) / block).sum() for i in range(n)])
    lk = -0.691 + 10 * np.log10(z + 1e-12)
    z = z[lk > -70]
    if len(z) == 0:
        return -70.0
    rel = -0.691 + 10 * np.log10(z.mean() + 1e-12) - 10
    z2 = z[-0.691 + 10 * np.log10(z + 1e-12) > rel]
    return float(-0.691 + 10 * np.log10((z2 if len(z2) else z).mean() + 1e-12))


def level_db(x: np.ndarray) -> float:
    return float(10 * np.log10(np.mean(x.astype(np.float64) ** 2) + 1e-12))


# ---------------------------------------------------------------------------------------------- the output
def _stft_power(mono: np.ndarray, rate: int):
    n_fft = 2048 if rate <= 50000 else 4096
    hop = n_fft // 4
    f, t, Z = signal.stft(mono, fs=rate, window="hann", nperseg=n_fft, noverlap=n_fft - hop, boundary=None, padded=False)
    return f, (np.abs(Z) ** 2).T, hop      # frames x bins


def onset_curve(f: np.ndarray, P: np.ndarray) -> np.ndarray:
    """How much the sound rises from one moment to the next: in 24 bands from 30 Hz to 16 kHz, each band's level in dB
    (nothing counted below 70 dB under the loudest), its rises only, averaged over the bands."""
    edges = np.geomspace(30, 16000, 25)
    E = np.stack([P[:, (f >= lo) & (f < hi)].sum(axis=1) for lo, hi in zip(edges[:-1], edges[1:])], axis=1)
    dB = 10 * np.log10(E + 1e-12)
    dB = np.maximum(dB, dB.max() - 70)
    return np.maximum(0, dB[1:] - dB[:-1]).mean(axis=1)


def _peaks(rise: np.ndarray, rate_frames: float) -> np.ndarray:
    """Onsets: a rise of 1.5 dB at least (over the bands, on average) and well above what is usual around it, 90 ms
    apart at least (a sixteenth at 165 BPM)."""
    if len(rise) < 3:
        return np.array([], dtype=int)
    med = signal.medfilt(rise, int(0.3 * rate_frames) | 1)
    found, _ = signal.find_peaks(rise, height=np.maximum(1.5, med + 0.75), distance=max(1, int(0.09 * rate_frames)))
    return found


def texture(mono: np.ndarray, rate: int, f: np.ndarray, P: np.ndarray, hop: int, bpm: float | None) -> dict:
    """What a sound is like to the touch, apart from its notes and its loudness.

    - rough: how much its level flutters between 20 and 150 times a second (grit, growl, beating), in eight bands, as a
      share of each band's level (dB).
    - contrast: how far its spectrum's peaks stand above its valleys, an octave at a time (dB): resonant and
      ringing, against diffuse and washed.
    - moves: how far its brightness swings within the bar, the same every bar (octaves): a filter that opens and
      shuts with the pattern.
    - evolves: how far its brightness wanders from one bar to the next, where a loop would stand still (octaves): a
      slow sweep, a drifting delay, anything alive."""
    out = {}
    # rough: a short window (5 ms), so that flutter up to 150 Hz can be seen
    n = 256 if rate <= 50000 else 512
    ff, _, Z = signal.stft(mono, fs=rate, window="hann", nperseg=n, noverlap=n - n // 4, boundary=None, padded=False)
    env_rate = rate / (n // 4)
    E = np.abs(Z)
    edges = np.geomspace(60, 12000, 9)
    num = den = 0.0
    for lo, hi in zip(edges[:-1], edges[1:]):
        env = E[(ff >= lo) & (ff < hi)].sum(axis=0)
        if env.size < 64 or env.mean() <= 0:
            continue
        spec = np.abs(np.fft.rfft((env - env.mean()) * np.hanning(env.size))) ** 2
        fr = np.fft.rfftfreq(env.size, 1 / env_rate)
        weight = float(env.mean() ** 2)
        num += weight * float(spec[(fr >= 20) & (fr <= 150)].sum()) / (float(spec[1:].sum()) + 1e-18) * float(np.var(env)) / (float(env.mean() ** 2) + 1e-18)
        den += weight
    out["rough"] = float(10 * np.log10(num / den + 1e-9)) if den > 0 else -90.0
    # contrast: per octave, the strongest fifth of the bins against the weakest fifth
    total = P.mean(axis=0)
    cs = []
    for lo in (100, 200, 400, 800, 1600, 3200, 6400):
        band = np.sort(total[(f >= lo) & (f < 2 * lo)])
        if band.size >= 10:
            k = max(1, band.size // 5)
            cs.append(10 * np.log10((band[-k:].mean() + 1e-18) / (band[:k].mean() + 1e-18)))
    out["contrast"] = float(np.mean(cs)) if cs else 0.0
    # moves, evolves: the brightness of each sixteenth, the same place in every bar against the bars one another
    out["moves"], out["evolves"] = 0.0, 0.0
    if bpm:
        sel = (f >= 40) & (f < 16000)
        per = rate / hop * 60.0 / bpm / 4.0                      # frames a sixteenth
        bars = int(P.shape[0] / (per * 16))
        if bars >= 2 and per >= 1:
            cen = np.zeros((bars, 16))
            lev = np.zeros((bars, 16))
            for b in range(bars):
                for s16 in range(16):
                    a, z = int(round((b * 16 + s16) * per)), int(round((b * 16 + s16 + 1) * per))
                    p = P[a:z, sel].sum(axis=0)
                    lev[b, s16] = p.sum()
                    cen[b, s16] = np.log2((f[sel] * p).sum() / (p.sum() + 1e-18) + 1e-9)
            w = lev / (lev.sum() + 1e-18)                        # (a sixteenth with nothing in it says nothing)
            mean_bar = (cen * lev).sum(axis=0) / (lev.sum(axis=0) + 1e-18)
            wb = lev.sum(axis=0) / (lev.sum() + 1e-18)
            centre = float((mean_bar * wb).sum())
            out["moves"] = float(np.sqrt(((mean_bar - centre) ** 2 * wb).sum()))
            out["evolves"] = float(np.sqrt((((cen - mean_bar) ** 2) * w).sum()))
    return out


def measure_mix(x: np.ndarray, rate: int, bpm: float | None) -> dict:
    """What the output sounds like."""
    x = x.astype(np.float64)
    mono = x.mean(axis=1)
    side = (x[:, 0] - x[:, 1]) / 2
    seconds = len(mono) / rate
    beats = seconds * (bpm or 128.0) / 60.0
    out = {"seconds": round(seconds, 3)}
    if np.max(np.abs(mono)) < 1e-5:
        out["silent"] = True
        return out

    out["loudness"] = loudness(x, rate)
    if out["loudness"] < -60:          # (nothing to speak of: as good as silent)
        out["silent"] = True
        return out
    rms = np.sqrt(np.mean(mono ** 2)) + 1e-12
    out["crest"] = float(20 * np.log10(np.max(np.abs(mono)) / rms))
    out["width"] = float(10 * np.log10((np.mean(side ** 2) + 1e-12) / (np.mean(((x[:, 0] + x[:, 1]) / 2) ** 2) + 1e-12)))

    f, P, hop = _stft_power(mono, rate)
    total = P.mean(axis=0)
    all_power = total[(f >= 20) & (f < 20000)].sum() + 1e-18
    bands = []
    for lo, hi in zip(BAND_EDGES[:-1], BAND_EDGES[1:]):
        share = total[(f >= lo) & (f < hi)].sum() / all_power
        bands.append(max(SILENT_DB, float(10 * np.log10(share + 1e-12))))
    out["bands"] = bands
    sel = (f >= 20) & (f < 20000)
    out["centroid"] = float(np.log2((f[sel] * total[sel]).sum() / (total[sel].sum() + 1e-18) + 1e-9))      # octaves above 1 Hz
    cum = np.cumsum(total[sel]) / (total[sel].sum() + 1e-18)
    out["rolloff"] = float(np.log2(f[sel][min(len(cum) - 1, int(np.searchsorted(cum, 0.85)))] + 1e-9))
    fl = (f >= 40) & (f < 16000)
    frames = P[:, fl] + 1e-18
    flat = np.exp(np.mean(np.log(frames), axis=1)) / np.mean(frames, axis=1)
    out["flatness"] = float(10 * np.log10(np.mean(flat) + 1e-12))
    out.update(texture(mono, rate, f, P, hop, bpm))

    # rhythm: onsets, and how strongly the sound moves with the beat, the eighth and the sixteenth
    flux = onset_curve(f, P)
    fr = rate / hop
    onsets = _peaks(flux, fr)
    out["onsets"] = float(len(onsets) / max(beats, 1e-6))          # per beat
    if bpm and len(flux) > 8:
        env = flux - flux.mean()
        spec = np.abs(np.fft.rfft(env * np.hanning(len(env)))) ** 2
        freqs = np.fft.rfftfreq(len(env), 1 / fr)
        whole = spec[1:].sum() + 1e-18
        def at(mult):
            target = bpm / 60.0 * mult
            near = np.abs(freqs - target) <= max(freqs[1] * 1.5, target * 0.02)
            return float(spec[near].sum() / whole) if near.any() else 0.0
        out["pulse"] = [at(1), at(2), at(4)]
    else:
        out["pulse"] = [0.0, 0.0, 0.0]

    # movement: how much the sound changes over the bars (by the beat: each band's level, how far it wanders)
    if bpm:
        per = max(1, int(round(fr * 60.0 / bpm)))
        nb = P.shape[0] // per
        if nb >= 4:
            levels = []
            for lo, hi in zip(BAND_EDGES[:-1], BAND_EDGES[1:]):
                b = P[: nb * per, (f >= lo) & (f < hi)].sum(axis=1).reshape(nb, per).mean(axis=1)
                levels.append(10 * np.log10(b + 1e-12))
            levels = np.array(levels)
            out["movement"] = float(np.mean(np.std(levels, axis=1)))
            half = nb // 2
            out["drift"] = float(np.mean(np.abs(levels[:, half:].mean(axis=1) - levels[:, :half].mean(axis=1))))
    out.setdefault("movement", 0.0)
    out.setdefault("drift", 0.0)
    if bpm:
        out["p96"], out["l96"] = heard_rhythm(mono, rate, max(1, int(round(seconds * bpm / 240.0))), levels=True)
    return out


TONAL = {"bass", "rumble", "lead", "chord", "pad", "voice"}
GRID_BANDS = [(20, 120), (120, 300), (300, 800), (800, 2000), (2000, 6000), (6000, 20000)]      # where two parts can sit on each other


def chroma(mono: np.ndarray, rate: int) -> tuple[list[float], float]:
    """Which of the twelve notes a sound is made of (summed over its time, each a share), and how clearly (0: noise,
    every note alike; 1: one note alone). Spectral peaks from 27 Hz to 5 kHz, each placed at its own pitch (a window
    long enough to tell a low C from a C sharp, and each peak's frequency found between the bins)."""
    n = 8192 if rate <= 50000 else 16384
    if len(mono) < n:
        return [0.0] * 12, 0.0
    f, t, Z = signal.stft(mono, fs=rate, window="hann", nperseg=n, noverlap=n - n // 4, boundary=None, padded=False)
    mag = np.abs(Z).T                                     # frames x bins
    lo, hi = np.searchsorted(f, 27.0), np.searchsorted(f, 5000.0)
    m = mag[:, lo - 1:hi + 1] + 1e-12
    centre = m[:, 1:-1]
    peak = (centre > m[:, :-2]) & (centre >= m[:, 2:]) & (centre > centre.max(axis=1, keepdims=True) * 0.05)
    la, lb, lc = np.log(m[:, :-2]), np.log(centre), np.log(m[:, 2:])
    shift = 0.5 * (la - lc) / np.where(np.abs(la - 2 * lb + lc) < 1e-12, 1e-12, la - 2 * lb + lc)
    bins = np.arange(lo, hi)[None, :] + np.clip(shift, -0.5, 0.5)
    freq = bins * (f[1] - f[0])
    c = np.zeros(12)
    fr, bn = np.nonzero(peak)
    if len(fr) == 0:
        return [0.0] * 12, 0.0
    pc = np.mod(np.round(12 * np.log2(freq[fr, bn] / 440.0)) + 9, 12).astype(int)       # 0 = C
    np.add.at(c, pc, centre[fr, bn])
    c = c / c.sum()
    entropy = -(c[c > 0] * np.log(c[c > 0])).sum() / np.log(12)
    return [round(float(x), 4) for x in c], round(float(1 - entropy), 4)


def grid(mono: np.ndarray, rate: int, bars: int) -> list[list[float]]:
    """Where in the bar a part's sound is: for each sixteenth of a bar, its energy in six bands from sub to air
    (averaged over its bars: what loops), as power. The clip starts on a bar line, so a sixteenth here is a step."""
    n = len(mono)
    steps = 16 * max(1, bars)
    per = n // steps
    if per < 64:
        return [[0.0] * 16 for _ in GRID_BANDS]
    f, t, Z = signal.stft(mono[: per * steps], fs=rate, nperseg=min(2048, per), noverlap=min(2048, per) // 2, boundary=None, padded=False)
    P = np.abs(Z) ** 2
    frames = P.shape[1]
    out = []
    for lo, hi in GRID_BANDS:
        e = P[(f >= lo) & (f < hi)].sum(axis=0)
        cell = np.zeros(steps)
        idx = np.minimum(steps - 1, (np.arange(frames) * steps) // max(1, frames))
        np.add.at(cell, idx, e)
        cnt = np.bincount(idx, minlength=steps).clip(min=1)
        cell = cell / cnt
        out.append([float(x) for x in cell.reshape(-1, 16).mean(axis=0)])
    return out


SLOTS = 96          # places in a bar the heard rhythm is kept at: a sixteenth is six of them, a triplet eighth sixteen


def heard_rhythm(mono: np.ndarray, rate: int, bars: int, levels: bool = False):
    """The rhythm that is heard, whatever made it (steps, a delay's echoes, a density knob, ghost notes): how sharply
    the sound rises at each of 96 places in the bar, the bars laid over one another (the mean of them). In dB of rise,
    averaged over 24 bands from 30 Hz to 16 kHz, so a quiet hit in one band still shows. The clip starts on a bar line.
    With `levels`, also how loud the sound is at each place (dB under the loudest place): a rise says a hit is there,
    this says whether it is a ghost."""
    n, hop = 1024, 128
    if len(mono) < n * 4 or bars < 1:
        return ([0.0] * SLOTS, [-90] * SLOTS) if levels else [0.0] * SLOTS
    f, _, Z = signal.stft(mono, fs=rate, window="hann", nperseg=n, noverlap=n - hop, boundary=None, padded=False)
    power = (np.abs(Z) ** 2).T
    rise = onset_curve(f, power)
    # (a hit is seen rising when it comes into the window's strong middle: that is when it is)
    when = (np.arange(len(rise)) + 1) * hop + n * 0.62
    per_bar = len(mono) / bars
    slot = np.floor((when % per_bar) / per_bar * SLOTS + 0.5).astype(int) % SLOTS
    bar = np.minimum(bars - 1, (when // per_bar).astype(int))
    grid = np.zeros((bars, SLOTS))
    np.maximum.at(grid, (bar, slot), rise)
    seen = np.ones((bars, SLOTS), dtype=bool)
    seen[0, : int(np.ceil(when[0] / per_bar * SLOTS))] = False       # (the very start of the clip: nothing before it to rise from)
    if bars < 2:
        seen[:] = True
    p96 = [round(float(v), 2) for v in (grid * seen).sum(axis=0) / np.maximum(1, seen.sum(axis=0))]
    if not levels:
        return p96
    loud = np.zeros((bars, SLOTS))
    np.maximum.at(loud, (bar, slot), power[1:].sum(axis=1))
    loud = (loud * seen).sum(axis=0) / np.maximum(1, seen.sum(axis=0))
    return p96, [int(round(float(v))) for v in np.maximum(-90, 10 * np.log10(loud / (loud.max() + 1e-18) + 1e-9))]


def measure_part(x: np.ndarray, rate: int, bpm: float | None, bars: int, key: str) -> dict:
    """One lane, a return or the bus, on its own: how loud, where in the spectrum, which notes, where in the bar."""
    x = x.astype(np.float64)
    mono = x.mean(axis=1)
    level = level_db(x)
    out = {"level": round(level, 2)}
    if level < -70:
        out["silent"] = True
        return out
    f, P, hop = _stft_power(mono, rate)
    total = P.mean(axis=0)
    all_power = total[(f >= 20) & (f < 20000)].sum() + 1e-18
    out["bands"] = [round(max(SILENT_DB, float(10 * np.log10(total[(f >= lo) & (f < hi)].sum() / all_power + 1e-12))), 2) for lo, hi in zip(BAND_EDGES[:-1], BAND_EDGES[1:])]
    sel = (f >= 20) & (f < 20000)
    out["centroid"] = round(float(np.log2((f[sel] * total[sel]).sum() / (total[sel].sum() + 1e-18) + 1e-9)), 3)
    out["chroma"], out["clarity"] = chroma(mono, rate)
    fl = (f >= 40) & (f < 16000)
    frames = P[:, fl] + 1e-18
    out["flatness"] = round(float(10 * np.log10(np.mean(np.exp(np.mean(np.log(frames), axis=1)) / np.mean(frames, axis=1)) + 1e-12)), 2)
    out.update({k: round(v, 3) for k, v in texture(mono, rate, f, P, hop, bpm).items()})
    out["grid"] = [[round(v, 9) for v in row] for row in grid(mono, rate, bars)]
    rise = onset_curve(f, P)
    beats = len(mono) / rate * (bpm or 128.0) / 60.0
    out["hits"] = round(float(len(_peaks(rise, rate / hop)) / max(beats, 1e-6) * 4), 2)       # a bar
    g = np.array(out["grid"]).sum(axis=0)
    out["held"] = round(float((g > g.max() * 0.25).mean()) if g.max() > 0 else 0.0, 3)      # how much of the bar it sounds in
    out["tonal"] = key in TONAL
    out["p96"], out["l96"] = heard_rhythm(mono, rate, bars, levels=True)
    return out


def measure_parts(parts: dict[str, np.ndarray]) -> dict:
    """How loud each lane, return and the bus is, as a share of all of them together (dB), and how many sound."""
    level = {k: float(np.mean(v.astype(np.float64) ** 2)) for k, v in parts.items()}
    total = sum(level.get(k, 0.0) for k in LANES) + 1e-18
    shares = {}
    for k in PARTS:
        e = level.get(k, 0.0)
        shares[k] = max(SILENT_DB, float(10 * np.log10(e / total + 1e-12))) if e > 1e-10 else SILENT_DB
    sounding = sum(1 for k in LANES if shares[k] > -40)
    return {"shares": [round(shares[k], 2) for k in PARTS], "sounding": sounding}


def _bars(seconds: float, bpm, said) -> int:
    if said:
        return int(said)
    return max(1, int(round(seconds * (bpm or 128.0) / 240.0)))


def measure(row: dict, lossless: bytes | None, clip: bytes | None, stems: dict[str, bytes] | None, keep_part=None) -> dict:
    """A snapshot measured from the best of what there is of it. keep_part(key, ogg bytes), if given, is handed each
    lane, return and the bus as Ogg, to be listened to on its own or with another snapshot's."""
    bpm = row.get("bpm") or None
    out = {"uid": row["uid"], "bpm": bpm}
    audio = None
    if lossless:
        head, parts = read_lossless(lossless)
        mix, rate = parts.pop("mix")
        out["from"] = "lossless"
        out["mix"] = measure_mix(mix, rate, bpm)
        audio = {k: v for k, (v, _) in parts.items()}
        bars = _bars(len(mix) / rate, bpm, head.get("bars"))
    elif clip:
        x, rate = read_audio(clip)
        out["from"] = "clip"
        out["mix"] = measure_mix(x, rate, bpm)
        bars = _bars(len(x) / rate, bpm, row.get("bars"))
        if stems:
            out["from"] = "clip+stems"
            audio = {k: read_audio(v)[0] for k, v in stems.items()}
    else:
        out["from"] = "none"
        return out
    out["bars"] = bars
    if audio:
        out["parts"] = measure_parts(audio)
        out["lanes"] = {}
        for k, v in audio.items():
            out["lanes"][k] = measure_part(v, rate, bpm, bars, k)
            if keep_part is not None and not out["lanes"][k].get("silent"):
                if stems and k in stems:
                    keep_part(k, stems[k])
                else:
                    buf = io.BytesIO()
                    sf.write(buf, np.clip(v, -1, 1), rate, format="OGG", subtype="VORBIS")
                    keep_part(k, buf.getvalue())
    return out
    out["from"] = "none"
    return out
