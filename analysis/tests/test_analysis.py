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


# ---------------------------------------------------------------------------------------------- parts
from sfa.measure import measure_part  # noqa: E402
from sfa.parts import clash, fit, masking, relate_parts, signature, _chroma, _occ  # noqa: E402


def notes(freqs, on_steps=None, length=1.0, bars=4, level=0.3):
    """Tones (Hz, played together) on the sixteenths given (all of them: held), each for `length` of a sixteenth."""
    n = beat_samples() * 4 * bars
    step = beat_samples() // 4
    x = np.zeros(n)
    t = np.arange(int(step * length)) / RATE
    env = np.minimum(1, np.minimum(t * 400, (t[-1] - t) * 400 + 1e-3)) if len(t) else t
    for b in range(bars):
        for s16 in (on_steps if on_steps is not None else range(16)):
            i = (b * 16 + s16) * step
            for f0 in freqs:
                seg = np.sin(2 * np.pi * f0 * (t + i / RATE)) * env * level
                x[i:i + len(seg)] += seg[: n - i]
    return np.stack([x, x], axis=1).astype(np.float32)


C, CS, E, G = 130.81, 138.59, 164.81, 196.0


class Parts(unittest.TestCase):
    def part(self, x, key):
        return measure_part(x, RATE, BPM, 4, key)

    def test_notes_are_heard_as_notes(self):
        p = self.part(notes([C * 2, E * 2, G * 2], length=16), "chord")
        top = sorted(range(12), key=lambda i: -p["chroma"][i])[:3]
        self.assertEqual(sorted(top), [0, 4, 7], "C, E and G")
        self.assertGreater(p["clarity"], 0.3)

    def test_a_chord_in_the_bass_s_key_fits_and_one_a_semitone_off_clashes(self):
        bass = self.part(notes([C / 2], on_steps=[2, 6, 10, 14], length=1.5), "bass")
        same = self.part(notes([C * 2, E * 2, G * 2], on_steps=[0, 8], length=3), "chord")
        off = self.part(notes([CS * 2, CS * 2 * 1.26, CS * 2 * 1.5], on_steps=[0, 8], length=3), "chord")
        self.assertLess(clash(_chroma(bass), _chroma(same)), 0.02)
        self.assertGreater(clash(_chroma(bass), _chroma(off)), 0.05)
        self.assertGreater(fit(bass, same), fit(bass, off) + 0.1)

    def test_a_bass_between_the_kicks_sits_on_them_less_than_one_on_them(self):
        kick = self.part(groove(kick=1.0), "kick")
        off = self.part(notes([55.0], on_steps=[2, 6, 10, 14], length=1.5, level=0.5), "bass")
        on = self.part(notes([55.0], on_steps=[0, 4, 8, 12], length=1.5, level=0.5), "bass")
        self.assertLess(masking(_occ(kick), _occ(off)), masking(_occ(kick), _occ(on)) - 0.2)
        self.assertGreater(fit(kick, off), fit(kick, on))

    def test_the_same_lane_in_two_snapshots_is_one_part(self):
        snap = {"made": {"slots": ["kick", "bass"], "lanes": {"kick": "k1", "bass": "b1"}},
                "values": {"bass.cutoff": 0.5, "bass.level": 0.7, "kick.tune": 0.2},
                "patterns": [{"len": 16, "steps": [[0, 1, 0.5, 1, 0, 3]]}, {"len": 16, "steps": [[2, 1, 0.5, 1, 0, 1]]}]}
        louder = json.loads(json.dumps(snap)); louder["values"]["bass.level"] = 0.9
        other = json.loads(json.dumps(snap)); other["patterns"][1]["steps"][0][0] = 3
        self.assertEqual(signature(snap, "bass"), signature(louder, "bass"), "its level alone does not make it another part")
        self.assertNotEqual(signature(snap, "bass"), signature(other, "bass"), "another pattern does")
        self.assertEqual(signature(snap, "kick"), signature(other, "kick"))

    def test_swaps_and_partners(self):
        """Three snapshots: kick and bass in C; kick and a chord in C; kick and a chord in C sharp. For the first, a
        chord to put with its bass is the one in C; parts found in two snapshots are one."""
        kick = groove(kick=1.0)
        bass = notes([C / 2], on_steps=[2, 6, 10, 14], length=1.5)
        chord_c = notes([C * 2, E * 2, G * 2], on_steps=[0, 8], length=3)
        chord_cs = notes([CS * 2, CS * 2 * 1.26, CS * 2 * 1.5], on_steps=[0, 8], length=3)
        hat = groove(kick=0.0, hats=0.4)

        def snap(uid, lanes):
            m = {"uid": uid, "bpm": BPM, "lanes": {k: self.part(v, k) for k, v in lanes.items()}}
            m["parts"] = {"shares": [0.0 if l in lanes else -60.0 for l in PARTS], "sounding": len(lanes)}
            sj = {"snapshot": {"made": {"slots": list(lanes), "lanes": {k: k for k in lanes}},
                               "values": {f"{k}.x": (0.1 if k != "chord" else (0.2 if uid == "b" else 0.3)) for k in lanes},
                               "patterns": [{"len": 16, "steps": []} for _ in lanes]}}
            return m, sj
        ms, sj = zip(*[snap("a", {"kick": kick, "bass": bass, "closed_hat": hat}), snap("b", {"kick": kick, "chord": chord_c, "closed_hat": hat}),
                       snap("c", {"kick": kick, "chord": chord_cs, "closed_hat": hat})])
        out = relate_parts(list(ms), dict(zip("abc", sj)), [])
        kicks = [p for p in out["parts"].values() if p["lane"] == "kick"]
        self.assertEqual(len(kicks), 1, "one kick, in all three")
        self.assertEqual(sorted(kicks[0]["in"]), ["a", "b", "c"])
        bass_part = next(p for p in out["parts"].values() if p["lane"] == "bass")
        best_chord = bass_part["goes"]["chord"][0]
        self.assertEqual(out["parts"][best_chord[0]]["in"], ["b"], "the chord in C goes with the bass in C")
        self.assertEqual(best_chord[2], 0, "and they have never been together: new")
        self.assertNotIn("kick", out["swap"]["a"], "the only kick there is is its own: nothing to swap")


# ---------------------------------------------------------------------------------------------- making a chip
import re  # noqa: E402

from sfa.compose import compose  # noqa: E402


class Making(unittest.TestCase):
    def snap(self, uid, tag, slots=("kick", "bass", "chord")):
        return {
            "id": 1, "uid": uid, "name": "SNAP " + tag, "time": 1,
            "made": {"app": "0.1.0 x", "slots": list(slots), "lanes": {s: f"{s}-{tag}" for s in slots}, "fx": {"DELAY": f"dly-{tag}"}, "returns": {"dub": "d"}},
            "profile": {"v": 1, "energy": 0.5},
            "values": {**{f"{s}.cut": float(len(tag)) for s in slots}, **{f"{s}.i1.c2": 0.1 for s in slots}, "g.key": 3, "ret.dub_time": 0.4, "bass.only_" + tag: 1},
            "patterns": [{"len": 16, "steps": [[i, 1, 0.5, 1, 0, 3]]} for i, _ in enumerate(slots)],
            "inserts": [["DELAY" if s == "bass" else "NONE", "NONE", "NONE"] for s in slots],
            "gen": [-1] * len(slots),
            "setup": {"lanes": [{"res": len(tag)} for _ in slots], "inserts": [[{}, {}, {}] for _ in slots], "returns": [{}, {}]},
            "played": {"seqMute": [0] * len(slots), "in": []},
            "laneFiles": [f"{s}-{tag}.wav" if s == "bass" else "" for s in slots],
        }

    def test_a_lane_is_taken_whole_and_the_rest_is_the_base_s(self):
        base, src = self.snap("aaaaaaaaaaaaaaaa-1", "A"), self.snap("bbbbbbbbbbbbbbbb-2", "BB", slots=("bass", "kick", "chord"))
        src["patterns"][0] = {"len": 16, "steps": [[3, 1, 0.5, 1, 7, 1]]}
        out = compose(base, {"bass": src, "chord": None}, "a with b", "cccccccccccccccc", when_ms=0x1a2b)
        self.assertTrue(re.match(r"^[0-9a-f]{16}-[0-9A-Za-z_.-]{1,64}$", out["uid"]))
        self.assertEqual(out["uid"], "cccccccccccccccc-1a2b")
        self.assertEqual(out["name"], "A WITH B")
        i = base["made"]["slots"].index("bass")
        self.assertEqual(out["patterns"][i], src["patterns"][0], "its pattern, from where it is in the other snapshot")
        self.assertEqual(out["values"]["bass.cut"], 2.0)
        self.assertIn("bass.only_BB", out["values"])
        self.assertNotIn("bass.only_A", out["values"], "none of the base's own left behind")
        self.assertEqual(out["made"]["lanes"]["bass"], "bass-BB")
        self.assertEqual(out["made"]["fx"]["DELAY"], "dly-A", "an effect the base has keeps the base's print")
        self.assertEqual(out["setup"]["lanes"][i], {"res": 2})
        self.assertEqual(out["laneFiles"][i], "bass-BB.wav")
        self.assertEqual(out["values"]["kick.cut"], 1.0, "the other lanes are the base's")
        self.assertEqual(out["values"]["g.key"], 3)
        c = base["made"]["slots"].index("chord")
        self.assertEqual(out["patterns"][c]["steps"], [], "a lane taken away is silent")
        self.assertEqual(out["played"]["seqMute"][c], 1)
        self.assertNotIn("profile", out)
        self.assertEqual(out["ext"]["sf000analysis"], {"base": "aaaaaaaaaaaaaaaa-1", "lanes": {"bass": "bbbbbbbbbbbbbbbb-2", "chord": None}, "made": 0x1a2b})
        self.assertEqual(base["values"]["bass.cut"], 1.0, "the base itself is not changed")

    def test_a_lane_from_another_build_says_which_lists_it_was_taken_on(self):
        # (FORMAT.md, `lists`: a model's place and a model's own knobs mean what the build that took them made of them)
        old, new = self.snap("aaaaaaaaaaaaaaaa-1", "A"), self.snap("bbbbbbbbbbbbbbbb-2", "BB", slots=("bass", "kick", "chord"))
        new["lists"] = 2
        out = compose(old, {"bass": new}, "x", "cccccccccccccccc", when_ms=1)
        self.assertEqual(out["listsBy"], [1, 2, 1], "kick and chord the base's, bass the newer snapshot's, by the base's places")
        self.assertEqual(out["lists"], 1)
        back = compose(new, {"kick": old}, "x", "cccccccccccccccc", when_ms=1)
        self.assertEqual(back["listsBy"], [2, 1, 2])
        again = compose(out, {"kick": new, "chord": new}, "x", "cccccccccccccccc", when_ms=1)
        self.assertEqual(again.get("lists"), 2, "all of now: one figure")
        self.assertNotIn("listsBy", again)
        same = compose(old, {"bass": self.snap("dddddddddddddddd-3", "D")}, "x", "cccccccccccccccc", when_ms=1)
        self.assertNotIn("lists", same, "all from before: as snapshots from before are")
        self.assertNotIn("listsBy", same)


# ---------------------------------------------------------------------------------------------- morphs
from sfa.morph import even_places, lane_change, look, tags, unevenness, refs_in, brand  # noqa: E402


class Morphing(unittest.TestCase):
    def path(self, fs):
        """A morph made here whose shape is known: hiss comes in over a kick, all of it in the first third of the turn."""
        return [{"uid": f"p{i}", "from": "lossless", "mix": measure_mix(groove(kick=1.0, noise_hi=0.06 * min(1.0, f * 3), hats=0.3, seed=1), RATE, BPM)} for i, f in enumerate(fs)]

    def library(self):
        return [{"uid": f"l{i}", "from": "clip", "mix": measure_mix(groove(kick=0.5 + 0.1 * i, hats=0.1 * i, noise_hi=0.01 * i, tone=0.05 * i, seed=i), RATE, BPM)} for i in range(6)]

    def test_a_morph_that_changes_all_at_first_is_found_uneven_and_evened(self):
        fs = [i / 10 for i in range(11)]
        pts = self.path(fs)
        path = look(pts, self.library(), twin=pts[0])
        self.assertEqual(path["noise"], 0.0)
        self.assertGreater(sum(path["steps"][:4]), 4 * sum(path["steps"][5:]), "nearly all of the change is in the first part")
        self.assertGreater(unevenness(path["steps"]), 2.0)
        places = even_places(fs, path["steps"], 3)
        self.assertLess(places[-1], 0.45, "so the snapshots between all go in the first part of the turn")
        self.assertEqual(places, sorted(places))
        even = look([pts[0]] + self.path(places) + [pts[-1]], self.library(), twin=pts[0], smooth=False)
        self.assertLess(unevenness(even["steps"]), 1.5, "and through them the change is even")

    def test_the_key_a_snapshot_is_in(self):
        from sfa.morph import key_of
        self.assertEqual((key_of({"values": {"g.key": 4 / 12}}), key_of({"values": {"g.key": 0.0}}), key_of({})), ("D#", "OFF", "OFF"))

    def test_nothing_is_called_worth_hearing_that_is_within_the_noise(self):
        fs = [i / 10 for i in range(11)]
        pts = self.path(fs)
        noisy = {"uid": "t", "from": "lossless", "mix": measure_mix(groove(kick=1.0, hats=0.3, noise_hi=0.03, seed=9), RATE, BPM)}
        path = look(pts, self.library(), twin=noisy)
        self.assertGreater(path["noise"], 0.0)
        self.assertNotIn("NEW", [t for ts in tags(path) for t in ts])

    def test_the_lanes_that_change_most_are_named_first(self):
        part = lambda x, k: measure_part(x, RATE, BPM, 4, k)
        a = {"lanes": {"kick": part(groove(kick=1.0), "kick"), "bass": part(notes([C / 2], on_steps=[2, 6, 10, 14]), "bass")}}
        b = {"lanes": {"kick": part(groove(kick=1.0), "kick"), "bass": part(notes([CS], on_steps=[0, 3, 6, 9, 12]), "bass"),
                       "chord": part(notes([C * 2, E * 2, G * 2], on_steps=[0, 8], length=3), "chord")}}
        change = lane_change(a, b)
        self.assertEqual([c[0] for c in change], ["chord", "bass", "kick"], "one that comes in, one that changes, one that stays")
        self.assertLess(change[-1][1], 0.05)

    def test_sound_files_named_and_a_chip_branded(self):
        snap = {"uid": "x", "laneFiles": ["", "samples/ab12cd34/hat.wav"], "insertFiles": [{"lane": 1, "files": ["samples/ffff0000/room.wav"]}], "name": "samples/not-a-file"}
        self.assertEqual(refs_in({k: v for k, v in snap.items() if k != "name"}), {"samples/ab12cd34/hat.wav", "samples/ffff0000/room.wav"})
        chip = brand(snap, "between", "c" * 16, 0x99, {"a": "u1", "b": "u2", "f": 0.4})
        self.assertEqual((chip["uid"], chip["name"], chip["id"]), ("c" * 16 + "-99", "BETWEEN", 0x99))
        self.assertEqual(chip["ext"]["sf000analysis"], {"a": "u1", "b": "u2", "f": 0.4, "made": 0x99})
        self.assertEqual(snap["uid"], "x")

    def test_a_lane_without_its_sound_file_is_left_out_not_stood_in_for(self):
        from sfa.morph import without_files
        lanes = ["kick", "closed_hat", "open_hat", "clap", "perc", "bass", "rumble", "lead", "chord", "pad", "voice"]
        files = [""] * 11
        files[0], files[10] = "samples/aaaaaaaa/kick.wav", "samples/bbbbbbbb/hands.mp3"
        snap = {"uid": "x", "laneFiles": files, "values": {"voice.level": 0.4, "voice.send_a": 0.3, "voice.send_b": 0.1, "kick.level": 0.7},
                "patterns": [{"len": 16, "steps": [[0, 0.7, 1.0, 1, 0, 3]]} for _ in lanes], "played": {"seqMute": [0] * 11, "in": [{"lane": 10, "len": 16, "steps": [[4, 0.7, 1.0, 1, 2, 3]]}, {"lane": 5, "len": 16, "steps": []}]}}
        out, left, named = without_files(snap, {"samples/bbbbbbbb/hands.mp3", "samples/aaaaaaaa/kick.wav"})
        self.assertEqual((out["patterns"][10]["steps"], len(out["patterns"][0]["steps"]), [e["lane"] for e in out["played"]["in"]]), ([], 1, [5]))      # (no notes for it, written or heard)
        self.assertEqual(len(snap["patterns"][10]["steps"]), 1)
        self.assertEqual((left, named), (["voice"], ["kick"]))           # (the voice would sing its notes instead: out. The kick may not use its file: said)
        self.assertEqual([out["values"][k] for k in ("voice.level", "voice.send_a", "voice.send_b", "kick.level")], [0.0, 0.0, 0.0, 0.7])
        self.assertEqual(snap["values"]["voice.level"], 0.4)
        same, left, named = without_files(snap, {"samples/cccccccc/other.wav"})
        self.assertIs(same, snap)
        self.assertEqual((left, named), ([], []))

    def test_a_sound_file_is_taken_from_this_machine_only_if_it_is_that_file(self):
        import hashlib, tempfile
        from pathlib import Path
        from unittest import mock
        from sfa import morph
        with tempfile.TemporaryDirectory() as d:
            root, data = Path(d), b"the phrase"
            sha = hashlib.sha256(data).hexdigest()
            good, bad = f"samples/{sha[:8]}/hands.mp3", "samples/00000000/hands.mp3"
            for r in (good, bad):
                (root / r).parent.mkdir(parents=True)
                (root / r).write_bytes(data)
            r = morph.Renderer.__new__(morph.Renderer)
            with mock.patch.object(morph, "sf000_library_dirs", return_value=[root]):
                self.assertEqual(r._local(good, None), root / good)       # (its folder is named for what is in it)
                self.assertEqual(r._local(good, sha), root / good)
                self.assertIsNone(r._local(bad, None))                    # (another file under that name)
                self.assertIsNone(r._local(good, "f" * 64))               # (not the one the library has under that name)
                self.assertIsNone(r._local("samples/11111111/none.wav", None))


class Texture(unittest.TestCase):
    def tone(self, am_hz=0.0, depth=0.0, sweep=0.0, noise=0.0, hz=220.0, bars=4):
        n = beat_samples() * 4 * bars
        t = np.arange(n) / RATE
        x = sum(np.sin(2 * np.pi * hz * k * t) / k for k in range(1, 9)) * 0.1          # (a saw-like tone)
        if depth:
            x = x * (1 + depth * np.sin(2 * np.pi * am_hz * t))
        if sweep:
            # a low-pass that opens slowly over the whole clip: each harmonic comes in later
            x = sum(np.sin(2 * np.pi * hz * k * t) / k * np.clip(t / t[-1] * 8 * sweep - (k - 1), 0, 1) for k in range(1, 9)) * 0.1
        if noise:
            x = x + noise * np.random.default_rng(3).standard_normal(n)
        return np.stack([x, x], axis=1).astype(np.float32)

    def test_grit_resonance_noise_and_life_are_each_heard(self):
        plain = measure_mix(self.tone(), RATE, BPM)
        gritty = measure_mix(self.tone(am_hz=70, depth=0.8), RATE, BPM)
        slow = measure_mix(self.tone(am_hz=2, depth=0.8), RATE, BPM)
        hiss = measure_mix(self.tone(noise=0.05), RATE, BPM)
        alive = measure_mix(self.tone(sweep=1.0), RATE, BPM)
        self.assertGreater(gritty["rough"], plain["rough"] + 15, "a flutter at 70 a second is grit")
        self.assertGreater(gritty["rough"], slow["rough"] + 10, "a tremolo at 2 a second is not")
        self.assertGreater(plain["contrast"], hiss["contrast"] + 10, "harmonics stand above their valleys; hiss fills them")
        self.assertGreater(hiss["flatness"], plain["flatness"] + 10)
        self.assertGreater(alive["evolves"], plain["evolves"] + 0.15, "a filter opening over four bars: it does not stand still")
        self.assertLess(plain["evolves"], 0.05, "a held tone does")


# ---------------------------------------------------------------------------------------------- rhythm
from sfa.rhythm import against, euclid, kind, lane_rhythm, lock, snapshot_rhythm, syncopation  # noqa: E402


class Rhythm(unittest.TestCase):
    def test_the_kinds_of_pattern(self):
        self.assertEqual(kind([0, 4, 8, 12], 16), "FOUR ON THE FLOOR")
        self.assertEqual(kind([0, 4, 8, 12, 16, 20, 24, 28], 32), "FOUR ON THE FLOOR")
        self.assertEqual(kind([2, 6, 10, 14], 16), "OFFBEATS")
        self.assertEqual(kind([4, 12], 16), "BACKBEAT")
        self.assertEqual(kind(list(range(16)), 16), "STRAIGHT 16THS")
        self.assertEqual(kind([0, 3, 6], 8), "EUCLIDEAN 3/8")                   # the tresillo
        self.assertEqual(kind([0, 3, 6, 8, 11, 14], 16), "EUCLIDEAN 3/8", "the tresillo twice in a bar")
        self.assertEqual(kind([2, 5, 8, 12, 15], 16), kind(euclid(5, 16), 16), "turned round, it is the same pattern")
        self.assertEqual(kind(euclid(5, 16), 16), "EUCLIDEAN 5/16")
        self.assertEqual(kind(list(range(0, 48, 3)), 48), "EVERY 3 STEPS")
        self.assertEqual(kind([7], 16), "SPARSE")
        self.assertEqual(kind([3, 7, 11, 15], 16), "SYNCOPATED", "every hit a sixteenth before the beat, and no beat")
        self.assertEqual(kind([0, 1, 4, 9, 12], 16), "BROKEN")

    def test_syncopation_is_none_on_the_beat_and_plenty_off_it(self):
        self.assertEqual(syncopation([0, 4, 8, 12], 16), 0.0)
        self.assertEqual(syncopation(list(range(16)), 16), 0.0, "every step: nothing is left empty to be missed")
        self.assertGreater(syncopation([2, 6, 10, 14], 16), 0.9)
        self.assertGreater(syncopation([3, 7, 11, 15], 16), syncopation([2, 6, 10, 14], 16))

    def test_lengths_that_run_against_each_other(self):
        self.assertIsNone(against([16, 16, 32, 16][:2]))
        a = against([16, 12, 16])
        self.assertEqual((a["ratio"], a["steps"], a["bars"]), ("3:4", 48, 3.0))
        self.assertEqual(against([16, 20])["ratio"], "4:5")
        self.assertEqual(against([16, 32])["ratio"], "1:2")

    def test_how_two_lanes_lock(self):
        kick = {"len": 16, "hits": [0, 4, 8, 12]}
        off = {"len": 16, "hits": [2, 6, 10, 14]}
        same = {"len": 16, "hits": [0, 8]}
        after = {"len": 16, "hits": [1, 5, 9, 13]}
        self.assertEqual(lock(kick, off), {"on": 0.0, "between": 0.0})
        self.assertEqual(lock(kick, same)["on"], 1.0)
        self.assertEqual(lock(kick, after), {"on": 0.0, "between": 1.0})
        twelve = {"len": 12, "hits": [0, 3, 6, 9]}
        self.assertAlmostEqual(lock(kick, twelve)["on"], 4 / 12, places=2, msg="over three bars they meet on four of twelve")

    def test_a_snapshot_s_rhythm_is_read_from_what_it_played(self):
        snap = {"made": {"slots": ["kick", "closed_hat", "bass", "lead"]},
                "patterns": [{"len": 16, "steps": [[s, 1, 0.5, 1, 0, 3] for s in (0, 4, 8, 12)]},
                             {"len": 16, "steps": [[s, 1, 0.5, 1, 0, 3] for s in (2, 6, 10, 14)]},
                             {"len": 12, "steps": [[0, 1, 0.5, 1, 0, 2], [3, 0.8, 0.5, 1, 3, 2], [7, 0, 0.5, 1, 3, 2]]},
                             {"len": 16, "steps": [[0, 1, 0.5, 1, 0, 3]]}],
                "played": {"seqMute": [0, 0, 0, 1], "in": []}}
        r = snapshot_rhythm(snap, ["kick", "closed_hat", "bass", "lead", "pad"])
        self.assertEqual(r["lanes"]["kick"]["kind"], "FOUR ON THE FLOOR")
        self.assertEqual(r["lanes"]["closed_hat"]["kind"], "OFFBEATS")
        self.assertEqual(r["lanes"]["bass"]["len"], 12)
        self.assertEqual(lane_rhythm(snap, "bass")["hits"], [0, 3], "a step with no velocity is not a hit")
        self.assertNotIn("lead", r["lanes"], "its sequencer was muted")
        self.assertEqual(r["against"]["ratio"], "3:4")
        self.assertEqual(r["against"]["by"], {"12": ["bass"], "16": ["kick", "closed_hat"]})


# ---------------------------------------------------------------------------------------------- the rhythm that is heard
from sfa.measure import heard_rhythm  # noqa: E402
from sfa.rhythm import heard, heard_hits, period, rhythm_distance  # noqa: E402


def clicks(steps16, level=1.0, bars=4, extra=()):
    """Short hits on the sixteenths given, every bar; `extra`: (place in beats, level) hits anywhere, for echoes, ghosts, triplets."""
    n = beat_samples() * 4 * bars
    x = np.zeros(n)
    t = np.arange(int(0.05 * RATE)) / RATE
    hit = np.sin(2 * np.pi * 180 * t) * np.exp(-t * 60) + 0.3 * np.random.default_rng(1).standard_normal(len(t)) * np.exp(-t * 200)
    for b in range(bars):
        for s16 in steps16:
            i = int((b * 4 + s16 / 4) * beat_samples())
            x[i:i + len(hit)] += level * hit[: n - i]
        for beats, lv in extra:
            i = int((b * 4 + beats) * beat_samples())
            x[i:i + len(hit)] += lv * hit[: n - i]
    return x


class Heard(unittest.TestCase):
    def test_hits_are_heard_where_they_are(self):
        p = heard_rhythm(clicks([0, 4, 8, 12]), RATE, 4)
        self.assertEqual([s for s, _ in heard_hits(p)], [0, 24, 48, 72])
        h = heard(p, ([0, 4, 8, 12], 16))
        self.assertEqual((h["hits"], h["added"], h["ghosts"], h["kind"], h["period"]), (4, 0, 0, "FOUR ON THE FLOOR", 4.0))

    def test_a_delay_s_echoes_are_heard_as_added_and_at_their_own_period(self):
        # a hit on the one, and its echoes every three sixteenths, each quieter: what a dub delay at a dotted eighth does
        echoes = [(3 * k / 4, 0.6 ** k) for k in range(1, 5)]
        p, l = heard_rhythm(clicks([0], extra=echoes), RATE, 4, levels=True)
        h = heard(p, ([0], 16), l)
        self.assertEqual(h["steps"], [0, 3, 6, 9, 12])
        self.assertEqual((h["hits"], h["added"]), (5, 4), "one written, four the delay made")
        self.assertGreaterEqual(h["ghosts"], 2, "the later echoes are quiet")
        self.assertEqual(h["period"], 3.0, "it repeats every three sixteenths: three against the beat")

    def test_ghost_notes_and_triplets(self):
        p, l = heard_rhythm(clicks([0, 8], extra=[(1.75, 0.12), (3.75, 0.12)]), RATE, 4, levels=True)
        h = heard(p, ([0, 8], 16), l)
        self.assertEqual((h["hits"], h["ghosts"], h["added"], h["triplets"]), (4, 2, 2, 0))
        trip = heard_rhythm(clicks([], extra=[(k / 3, 1.0) for k in range(12)]), RATE, 4)
        t = heard(trip, None)
        self.assertEqual(t["hits"], 12)
        self.assertEqual(t["triplets"], 8, "twelve to the bar: four of them are the beats, eight fall between sixteenths")
        self.assertNotIn("added", t, "with nothing written to set it against, nothing is called added")

    def test_rhythms_alike_and_not(self):
        four = heard_rhythm(clicks([0, 4, 8, 12]), RATE, 4)
        four_quiet = heard_rhythm(clicks([0, 4, 8, 12], level=0.4), RATE, 4)
        offs = heard_rhythm(clicks([2, 6, 10, 14]), RATE, 4)
        self.assertLess(rhythm_distance(four, four_quiet), 0.1)
        self.assertGreater(rhythm_distance(four, offs), 0.8)
        self.assertIsNone(period([0.0] * 96))
