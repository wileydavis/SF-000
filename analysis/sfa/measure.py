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


def measure(row: dict, lossless: bytes | None, clip: bytes | None, stems: dict[str, bytes] | None) -> dict:
    """A snapshot measured from the best of what there is of it."""
    bpm = row.get("bpm") or None
    out = {"uid": row["uid"], "bpm": bpm}
    if lossless:
        head, parts = read_lossless(lossless)
        mix, rate = parts.pop("mix")
        out["from"] = "lossless"
        out["mix"] = measure_mix(mix, rate, bpm)
        out["parts"] = measure_parts({k: v for k, (v, _) in parts.items()})
        return out
    if clip:
        x, rate = read_audio(clip)
        out["from"] = "clip"
        out["mix"] = measure_mix(x, rate, bpm)
        if stems:
            out["from"] = "clip+stems"
            out["parts"] = measure_parts({k: read_audio(v)[0] for k, v in stems.items()})
        return out
    out["from"] = "none"
    return out
