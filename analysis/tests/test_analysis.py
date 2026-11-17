"""The analysis checked on sounds made here, whose answers are known. python3 -m unittest discover -s tests"""
from __future__ import annotations

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from sfa.main import Analysis, Store  # noqa: E402
from sfa.measure import LOSSLESS_MARK, PARTS, measure, measure_mix, read_lossless  # noqa: E402
from sfa.relate import from_log, relate  # noqa: E402

RATE = 48000
BPM = 128.0


def beat_samples():
    return int(RATE * 60 / BPM)


def groove(bars=4, kick=1.0, hats=0.0, hat_every=2, tone=0.0, tone_hz=110.0, noise_hi=0.0, seed=0, width=0.0):
    """Four bars of a made-up groove: a kick on the beat, hats on the eighths or sixteenths, a held tone, some hiss."""
    rng = np.random.default_rng(seed)
    n = beat_samples() * 4 * bars
    t = np.arange(n) / RATE
    x = np.zeros(n)
    b = beat_samples()
    k = np.arange(int(0.25 * RATE)) / RATE
    kick_shape = np.sin(2 * np.pi * (50 + 120 * np.exp(-k * 30)) * k) * np.exp(-k * 12)
    kick_shape *= np.minimum(1.0, (len(k) - np.arange(len(k))) / (0.02 * RATE))      # (faded out: no click where it ends)
    for i in range(0, n, b):
        seg = kick_shape[: n - i]
        x[i:i + len(seg)] += kick * seg
    if hats:
        step = b // hat_every
        h = rng.standard_normal(int(0.04 * RATE))
        h = np.diff(np.concatenate([[0], h])) * np.exp(-np.arange(len(h)) / RATE * 90) * np.linspace(1, 0, len(h))
        for i in range(step // 2 if hat_every == 2 else 0, n, step):
            seg = h[: n - i]
            x[i:i + len(seg)] += hats * seg
    if tone:
        x += tone * np.sin(2 * np.pi * tone_hz * t)
    if noise_hi:
        x += noise_hi * np.diff(np.concatenate([[0], rng.standard_normal(n)]))
    left = x * (1 + width)
    right = x * (1 - width)
    return np.stack([left, right], axis=1).astype(np.float32) * 0.5


def flac(x):
    buf = io.BytesIO()
    sf.write(buf, x, RATE, format="FLAC", subtype="PCM_24")
    return buf.getvalue()


def ogg(x):
    buf = io.BytesIO()
    sf.write(buf, x, RATE, format="OGG", subtype="VORBIS")
    return buf.getvalue()


def lossless_file(parts: dict[str, np.ndarray], gains: dict[str, float] | None = None) -> bytes:
    gains = gains or {}
    blobs = [(k, flac(v * gains.get(k, 1.0)), gains.get(k, 1.0)) for k, v in parts.items()]
    head = {"rate": RATE, "bars": 4, "bits": 24, "parts": [{"key": k, "bytes": len(b), "gain": g} for k, b, g in blobs]}
    return LOSSLESS_MARK + json.dumps(head).encode() + b"\n" + b"".join(b for _, b, _ in blobs)


class Reading(unittest.TestCase):
    def test_lossless_file_is_read_back_whole_with_its_gain_undone(self):
        kick = groove(kick=1.0)
        bass = groove(kick=0.0, tone=1.6)          # over full scale: written at a quarter
        data = lossless_file({"mix": kick + bass, "kick": kick, "bass": bass}, {"bass": 0.25})
        head, parts = read_lossless(data)
        self.assertEqual(list(parts), ["mix", "kick", "bass"])
        self.assertLess(np.max(np.abs(parts["bass"][0] - bass)), 2e-6 / 0.25)
        self.assertEqual(parts["mix"][1], RATE)

    def test_a_file_cut_short_or_of_another_kind_is_refused(self):
        data = lossless_file({"mix": groove()})
        with self.assertRaises(ValueError):
            read_lossless(data[:-100])
        with self.assertRaises(ValueError):
            read_lossless(b"RIFF" + data[4:])


class Measuring(unittest.TestCase):
    def test_what_is_measured_moves_the_way_the_sound_does(self):
        plain = measure_mix(groove(kick=1.0), RATE, BPM)
        busy = measure_mix(groove(kick=1.0, hats=0.6, hat_every=4), RATE, BPM)
        bright = measure_mix(groove(kick=1.0, noise_hi=0.05), RATE, BPM)
        wide = measure_mix(groove(kick=1.0, tone=0.3, width=0.6), RATE, BPM)
        self.assertGreater(busy["onsets"], plain["onsets"] * 2.5, "sixteenth hats: many more hits a beat")
        self.assertAlmostEqual(plain["onsets"], 1.0, delta=0.15, msg="a kick alone: one hit a beat")
        self.assertGreater(bright["centroid"], plain["centroid"] + 2, "hiss: higher by octaves")
        self.assertGreater(wide["width"], plain["width"] + 10)
        self.assertGreater(plain["pulse"][0], 0.05, "it moves with the beat")

    def test_loudness_is_in_lufs(self):
        # a 1 kHz sine peaking at -20 dBFS in both channels reads -20 LUFS (the reference BS.1770 is made to)
        t = np.arange(RATE * 4) / RATE
        x = np.stack([np.sin(2 * np.pi * 1000 * t)] * 2, axis=1) * 10 ** (-20 / 20)
        m = measure_mix(x.astype(np.float32), RATE, BPM)
        self.assertAlmostEqual(m["loudness"], -20.0, delta=0.5)

    def test_lanes_say_what_is_sounding(self):
        parts = {"mix": groove(kick=1.0, tone=0.4) * 0}
        kick, bass = groove(kick=1.0), groove(kick=0.0, tone=0.4)
        data = lossless_file({"mix": kick + bass, "kick": kick, "bass": bass})
        m = measure({"uid": "x", "bpm": BPM}, data, None, None)
        self.assertEqual(m["from"], "lossless")
        self.assertEqual(m["parts"]["sounding"], 2)
        shares = dict(zip(PARTS, m["parts"]["shares"]))
        self.assertEqual(shares["pad"], -60.0)
        self.assertGreater(shares["kick"], -10)
        del parts

    def test_a_clip_and_its_stems_when_there_is_no_lossless(self):
        kick, hats = groove(kick=1.0), groove(kick=0.0, hats=0.5)
        m = measure({"uid": "x", "bpm": BPM}, None, ogg(kick + hats), {"kick": ogg(kick), "closed_hat": ogg(hats)})
        self.assertEqual(m["from"], "clip+stems")
        self.assertEqual(m["parts"]["sounding"], 2)
        self.assertEqual(measure({"uid": "y"}, None, None, None)["from"], "none")


class Relating(unittest.TestCase):
    def library(self):
        """Three families: kick and sub; kick, hats and hiss; a pad-like tone with no kick. Each in three energies."""
        out = []
        for fam, make in enumerate([
            lambda e, s: groove(kick=0.6 + 0.4 * e, tone=0.25, tone_hz=55, seed=s),
            lambda e, s: groove(kick=0.6 + 0.4 * e, hats=0.25 + 0.5 * e, hat_every=2 if e < 0.5 else 4, noise_hi=0.01 + 0.02 * e, seed=s),
            lambda e, s: groove(kick=0.0, tone=0.2 + 0.3 * e, tone_hz=330, width=0.5, hats=0.03 + 0.04 * e, seed=s),
        ]):
            for lvl, e in enumerate([0.0, 0.5, 1.0]):
                m = {"uid": f"f{fam}-e{lvl}", "from": "clip", "mix": measure_mix(make(e, fam * 10 + lvl), RATE, BPM)}
                out.append(m)
        return out

    def test_alike_is_the_same_family_and_up_is_more_energy(self):
        found = relate(self.library(), [])
        for u, f in found.items():
            fam = u.split("-")[0]
            self.assertTrue(f["alike"][0][0].startswith(fam), f"{u}: the nearest is of its family ({f['alike'][:3]})")
            for v, _, de in f["up"]:
                self.assertGreater(found[v]["energy"], f["energy"])
            for v, _, de in f["down"]:
                self.assertLess(found[v]["energy"], f["energy"])
        for fam in ("f0", "f1"):
            self.assertLess(found[f"{fam}-e0"]["energy"], found[f"{fam}-e2"]["energy"], "a family's busier, louder one has more energy")
        self.assertGreater(found["f1-e2"]["energy"], found["f2-e0"]["energy"], "kick and sixteenths over a quiet tone")

    def test_what_follows_what_comes_from_the_log(self):
        A, B, C, D = "a", "b", "c", "d"
        log = [
            {"k": "1-0001-aaaa", "t": 1000, "a": "open"},
            {"k": "1-0002-aaaa", "t": 2000, "a": "recall", "u": A},
            {"k": "1-0003-aaaa", "t": 3000, "a": "recall", "u": B},
            {"k": "1-0004-aaaa", "t": 4000, "a": "take", "u": C, "base": B},
            {"k": "1-0005-aaaa", "t": 5000, "a": "knob", "u": A},
            {"k": "1-0006-aaaa", "t": 6000, "a": "add", "u": D, "to": "knob"},          # (put on a knob: not played)
            {"k": "1-0007-aaaa", "t": 7000, "a": "stop"},
            {"k": "1-0008-aaaa", "t": 8000, "a": "recall", "u": B},                    # (after STOP: follows nothing)
            {"k": "1-0009-aaaa", "t": 8500, "a": "recall", "u": D},
            {"k": "1-0001-bbbb", "t": 9000, "a": "recall", "u": C},                    # (another run)
            {"k": "1-000a-aaaa", "t": 8000 + 700_000, "a": "recall", "u": A},          # (more than ten minutes on)
        ]
        follows, grew = from_log(log)
        self.assertEqual(dict(follows[A]), {B: 1})
        self.assertEqual(dict(follows[B]), {D: 1})
        self.assertEqual(dict(follows[C]), {A: 1})
        self.assertNotIn(D, follows)
        self.assertEqual(grew, {C: B})


class FakeLibrary:
    def __init__(self, lines):
        self.lines = lines
        self.asked = 0

    def actions(self, library, after, limit=5000):
        self.asked += 1
        return [a for a in self.lines if a["t"] > after][:limit]


class Fetching(unittest.TestCase):
    def test_the_log_is_taken_once_however_often_it_is_asked_for(self):
        with tempfile.TemporaryDirectory() as d:
            lines = [{"k": f"x-{i:04x}-r", "t": 1000 + i // 3, "a": "recall", "u": "a"} for i in range(30)]
            lib = FakeLibrary(lines[:20])
            a = Analysis(lib, Store(Path(d), False))
            self.assertEqual(a.fetch_log({"lib"}), 20)
            self.assertEqual(a.fetch_log({"lib"}), 0)
            lib.lines = lines
            self.assertEqual(a.fetch_log({"lib"}), 10)
            a2 = Analysis(lib, Store(Path(d), False))          # (started again another day)
            self.assertEqual(a2.fetch_log({"lib"}), 0)
            self.assertEqual(sorted(x["k"] for x in a2.all_actions()), sorted(x["k"] for x in lines))


if __name__ == "__main__":
    unittest.main()
