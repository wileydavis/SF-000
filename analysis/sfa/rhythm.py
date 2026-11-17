"""Rhythm, read from the patterns themselves: each snapshot says what every lane's sequencer was playing (its steps
and its length), so nothing has to be guessed from the sound.

For a lane: how long its pattern is, how many hits a bar, what kind of pattern it is, how syncopated.
For a snapshot: which lanes run against each other (a pattern of 12 steps over one of 16 is three against four: they
meet again every 48 steps), and how any two lanes lock together (on each other, between each other, or apart).

The kinds, in the order they are tried:
- FOUR ON THE FLOOR: every beat, nothing else.         - OFFBEATS: the "and" of every beat, nothing else.
- BACKBEAT: beats two and four.                        - STRAIGHT 8THS / 16THS: every one of them.
- EUCLIDEAN k/n: k hits spread as evenly as n steps allow (3/8 is the tresillo), turned any way round.
- EVERY n STEPS: evenly spaced, but not by the beat (every 3 steps is dotted eighths, across the bar).
- SYNCOPATED: most of its weight off the strong steps.  - SPARSE: one or two hits.   - BROKEN: none of these.

Syncopation is Longuet-Higgins and Lee's: a hit on a weak step with nothing on the stronger step that follows it
counts by how much stronger that step is.
"""
from __future__ import annotations

from math import gcd

import numpy as np


def onsets_of(pattern: dict | None) -> tuple[list[int], int]:
    """(the steps that sound, the pattern's length). A step with no velocity is not a hit."""
    if not pattern:
        return [], 16
    n = int(pattern.get("len") or 16)
    hits = sorted({int(s[0]) for s in pattern.get("steps") or [] if len(s) > 1 and float(s[1]) > 0 and 0 <= int(s[0]) < n})
    return hits, n


def played(snap: dict, lane: str) -> tuple[list[int], int] | None:
    """What a lane was playing in a snapshot: notes that came in from outside if there were any, else its sequencer's
    own steps, unless that was muted. None: it has no such lane, or it played nothing."""
    slots = (snap.get("made") or {}).get("slots") or []
    if lane not in slots:
        return None
    i = slots.index(lane)
    p = snap.get("played") or {}
    for x in p.get("in") or []:
        if x.get("lane") == i and (x.get("steps") or x.get("notes")):
            n = int(x.get("len") or 16)
            hits = sorted({int(s[0]) for s in x.get("steps") or [] if len(s) > 1 and float(s[1]) > 0 and 0 <= int(s[0]) < n})
            if not hits and x.get("notes"):
                hits = sorted({int(round(float(nt[0]) * 4)) % n for nt in x["notes"]})
            return hits, n
    mute = p.get("seqMute") or []
    if i < len(mute) and mute[i]:
        return None
    pats = snap.get("patterns") or []
    if i >= len(pats):
        return None
    hits, n = onsets_of(pats[i])
    return (hits, n) if hits else None


def euclid(k: int, n: int) -> list[int]:
    """k hits spread as evenly as n steps allow."""
    return sorted({(i * n) // k for i in range(k)}) if 0 < k <= n else []


def _turned(hits: list[int], n: int, target: list[int]) -> bool:
    s = set(target)
    return any({(h + r) % n for h in hits} == s for r in range(n))


def weights(n: int) -> list[int]:
    """How strong each step of a pattern is: the bar line most, then the half bar, the beats, the eighths, the sixteenths."""
    return [0 if i % 16 == 0 else -1 if i % 8 == 0 else -2 if i % 4 == 0 else -3 if i % 2 == 0 else -4 for i in range(n)]


def syncopation(hits: list[int], n: int) -> float:
    """Longuet-Higgins and Lee, a hit: 0 is none (four on the floor), 1 and over is plenty."""
    if not hits:
        return 0.0
    w = weights(n)
    on = set(hits)
    total = 0
    for h in hits:
        # the next step that is stronger than this one: if nothing sounds there, this hit is syncopated by the difference
        for d in range(1, n + 1):
            j = (h + d) % n
            if w[j] > w[h]:
                if j not in on:
                    total += w[j] - w[h]
                break
    return round(total / len(hits), 3)


def kind(hits: list[int], n: int) -> str:
    k = len(hits)
    if k == 0:
        return "SILENT"
    bar = [h % 16 for h in hits]
    per_bar = n // 16 if n % 16 == 0 else 0
    same_every_bar = per_bar >= 1 and all(sorted(h % 16 for h in hits if h // 16 == b) == sorted(set(bar)) for b in range(per_bar))
    if n % 16 == 0 and same_every_bar:
        one = sorted(set(bar))
        if one == [0, 4, 8, 12]:
            return "FOUR ON THE FLOOR"
        if one == [2, 6, 10, 14]:
            return "OFFBEATS"
        if one == [4, 12]:
            return "BACKBEAT"
        if one == list(range(0, 16, 2)):
            return "STRAIGHT 8THS"
        if one == list(range(16)):
            return "STRAIGHT 16THS"
    if k <= 2:
        return "SPARSE"
    # as evenly spread as its length allows, turned any way round
    if 3 <= k < n and _turned(hits, n, euclid(k, n)):
        if n % k != 0:                     # (it does not divide: a true Euclidean pattern, 3 in 8, 5 in 16)
            m = gcd(k, n)
            if n // m <= 4:                # (three of every four sixteenths, the gallop: said as it is)
                return f"{k // m} OF EVERY {n // m}"
            return f"EUCLIDEAN {k // m}/{n // m}"
        if n // k not in (1, 2, 4, 8, 16):  # (it divides, but not by the beat: every third step is a dotted eighth across the bar)
            return f"EVERY {n // k} STEPS"
    if syncopation(hits, n) >= 1.0:
        return "SYNCOPATED"
    return "BROKEN"


def lane_rhythm(snap: dict, lane: str) -> dict | None:
    got = played(snap, lane)
    if not got:
        return None
    hits, n = got
    return {"len": n, "hits": hits, "per_bar": round(len(hits) * 16 / n, 2), "kind": kind(hits, n), "sync": syncopation(hits, n)}


def against(lengths: list[int]) -> dict | None:
    """Lanes whose patterns are of different lengths run against each other. The lengths, as a ratio in its lowest
    terms, and how many steps (and bars) pass before they all meet again."""
    distinct = sorted(set(lengths))
    if len(distinct) < 2:
        return None
    g = 0
    cycle = 1
    for x in distinct:
        g = gcd(g, x)
        cycle = cycle * x // gcd(cycle, x)
    return {"lengths": distinct, "ratio": ":".join(str(x // g) for x in distinct), "steps": cycle, "bars": round(cycle / 16, 2)}


def lock(a: dict, b: dict) -> dict:
    """How two lanes' hits sit against each other over the stretch in which both repeat: `on`, the share of the
    sparser one's hits that land on the other's; `between`, the share that land where the other is silent with a
    hit of the other's next to them (they answer each other)."""
    cycle = a["len"] * b["len"] // gcd(a["len"], b["len"])
    cycle = min(cycle, 16 * 64)
    ha = {h + r * a["len"] for r in range(cycle // a["len"]) for h in a["hits"]}
    hb = {h + r * b["len"] for r in range(cycle // b["len"]) for h in b["hits"]}
    small, big = (ha, hb) if len(ha) <= len(hb) else (hb, ha)
    if not small:
        return {"on": 0.0, "between": 0.0}
    on = len(small & big) / len(small)
    between = len([h for h in small if h not in big and (((h + 1) % cycle) in big or ((h - 1) % cycle) in big)]) / len(small)
    return {"on": round(on, 2), "between": round(between, 2)}


def snapshot_rhythm(snap: dict, lanes: list[str]) -> dict:
    """Every lane that plays, and what they do together."""
    per = {l: r for l in lanes for r in [lane_rhythm(snap, l)] if r}
    out = {"lanes": {l: {k: v for k, v in r.items() if k != "hits"} for l, r in per.items()}}
    ag = against([r["len"] for r in per.values()])
    if ag:
        ag["by"] = {str(n): [l for l, r in per.items() if r["len"] == n] for n in ag["lengths"]}
        out["against"] = ag
    if per:
        # all of it together: how many of the sixteen steps of a bar anything lands on, and its syncopation
        union = sorted({h % 16 for r in per.values() for h in r["hits"]})
        out["filled"] = len(union)
        out["sync"] = round(sum(r["sync"] * len(r["hits"]) for r in per.values()) / max(1, sum(len(r["hits"]) for r in per.values())), 3)
    return out


# ------------------------------------------------------------------------------------------------- what is heard
# The steps are half of a rhythm. The rest is made by the sound: a delay's echoes, a density knob, ghost notes, a
# reverb that swells into the next hit. So the rhythm is also taken from the sound itself (measure.heard_rhythm: how
# sharply it rises at each of 96 places in the bar) and set against what was written.
SLOTS = 96
PER_STEP = SLOTS // 16          # 6 places a sixteenth; a triplet eighth is 16 places, a triplet sixteenth 8


def heard_hits(p96: list[float], l96: list[int] | None = None) -> list[tuple[int, float]]:
    """(place, strength 0 to 1 of the strongest) for each hit heard in the bar: a rise of 1.5 dB or more that is the
    most within a sixteenth either side of it, and a sixth of the strongest at least. Its strength is how loud the
    sound gets just after it against the loudest hit (given the levels; without them, how sharply it rises)."""
    p = np.array(p96 or [0.0] * SLOTS, dtype=float)
    if p.size != SLOTS or p.max() < 1.5:
        return []
    top = p.max()
    out = []
    for i in range(SLOTS):
        around = [p[(i + d) % SLOTS] for d in range(-PER_STEP + 1, PER_STEP)]
        if p[i] >= 1.5 and p[i] >= top / 6 and p[i] >= max(around) and (p[i] > p[(i - 1) % SLOTS] or p[i] > p[(i + 1) % SLOTS]):
            if out and i - out[-1][0] < PER_STEP - 1 and out[-1][1] * top >= p[i]:
                continue                 # (a plateau: the first of it is the hit)
            out.append((i, round(float(p[i] / top), 2)))
    if l96 is not None and len(l96) == SLOTS and out:
        lv = [max(l96[(i + d) % SLOTS] for d in range(0, 4)) for i, _ in out]
        out = [(i, round(float(10 ** ((x - max(lv)) / 20.0)), 2)) for (i, _), x in zip(out, lv)]
    return out


def place_of(slot: int) -> tuple[str, int]:
    """Where a place in the bar falls: on a sixteenth ('16', which one), on a triplet ('3', which eighth-triplet or
    sixteenth-triplet place, of 12 a bar), or off both ('off', the nearest sixteenth)."""
    s16 = int(round(slot / PER_STEP)) % 16
    d16 = abs(slot - round(slot / PER_STEP) * PER_STEP)
    t = int(round(slot / 8)) % 12
    dt = abs(slot - round(slot / 8) * 8)
    if d16 <= 1:
        return "16", s16
    if dt <= 1:
        return "3", t
    return "off", s16


def period(p96: list[float]) -> float | None:
    """How often the heard rhythm repeats within the bar, in sixteenths (3: every dotted eighth, as a delay set there
    echoes), if it does so strongly; a delay's echoes show here as a period that the steps do not have."""
    p = np.array(p96 or [], dtype=float)
    if p.size != SLOTS or p.max() < 1.5:
        return None
    p = p - p.mean()
    den = float((p * p).sum()) + 1e-12
    best, lag = 0.0, None
    for L in range(PER_STEP, SLOTS // 2 + 1):
        r = float((p * np.roll(p, L)).sum()) / den
        if r > best + 0.02:
            best, lag = r, L
    return round(lag / PER_STEP, 2) if lag and best >= 0.5 else None


def heard(p96: list[float], written: tuple[list[int], int] | None = None, l96: list[int] | None = None) -> dict | None:
    """What is heard in the bar, and how much of it the sound added to what was written.

    - hits: how many a bar;   - ghosts: how many of them are quiet (under a third of the strongest);
    - added: how many are not on a written step (echoes, density, anything the sound makes itself);
    - triplets: how many fall on a triplet and not on a sixteenth;   - off: how many fall on neither (swung, or free);
    - period: what it repeats at, in sixteenths, if it does;   - kind and sync: of what is heard on the sixteenths."""
    hits = heard_hits(p96, l96)
    if not hits:
        return None
    wrote = None
    if written and written[0]:
        w, n = written
        cycle = n * 16 // gcd(n, 16)
        wrote = {(h + r * n) % 16 for r in range(cycle // n) for h in w}      # (every sixteenth of the bar it ever lands on)
    on16, ghosts, added, trip, off = [], 0, 0, 0, 0
    for slot, strength in hits:
        where, at = place_of(slot)
        if strength < 0.33:
            ghosts += 1
        if where == "16":
            on16.append(at)
            if wrote is not None and at not in wrote:
                added += 1
        else:
            trip += where == "3"
            off += where == "off"
            if wrote is not None:
                added += 1
    on16 = sorted(set(on16))
    out = {"hits": len(hits), "ghosts": ghosts, "triplets": int(trip), "off": int(off), "kind": kind(on16, 16) if on16 else "OFF THE GRID",
           "sync": syncopation(on16, 16), "steps": on16}
    if wrote is not None:
        out["added"] = added
    per = period(p96)
    if per:
        out["period"] = per
    return out


def rhythm_distance(a: list[float], b: list[float]) -> float:
    """How unlike two heard rhythms are, 0 the same to 1 nothing in common: by where in the bar they rise, each
    place blurred a little into its neighbours so that a hit a hair early still meets one on time."""
    x, y = np.array(a or [0.0] * SLOTS, dtype=float), np.array(b or [0.0] * SLOTS, dtype=float)
    if x.size != SLOTS or y.size != SLOTS or x.max() <= 0 or y.max() <= 0:
        return 1.0
    blur = lambda v: 0.25 * np.roll(v, 1) + 0.5 * v + 0.25 * np.roll(v, -1)
    x, y = blur(np.maximum(x - 1.0, 0)), blur(np.maximum(y - 1.0, 0))
    return round(float(1 - (x * y).sum() / (np.sqrt((x * x).sum() * (y * y).sum()) + 1e-12)), 3)


def describe(snap: dict, measured: dict, lanes: list[str]) -> dict:
    """A snapshot's rhythm, written and heard: each lane's pattern and what is heard of that lane, which lanes run
    against each other, all of it together as it is heard, and what the dub return's echoes do."""
    out = snapshot_rhythm(snap or {}, lanes) if snap else {"lanes": {}}
    parts = measured.get("lanes") or {}
    wrote_all = set()
    for lane in lanes:
        w = played(snap, lane) if snap else None
        if w:
            n = w[1]
            cycle = n * 16 // gcd(n, 16)
            wrote_all |= {(h + r * n) % 16 for r in range(cycle // n) for h in w[0]}
        m = parts.get(lane)
        if m and not m.get("silent") and m.get("p96"):
            h = heard(m["p96"], w, m.get("l96"))
            if h:
                h.pop("steps", None)
                out["lanes"].setdefault(lane, {})["heard"] = h
    mix = measured.get("mix") or {}
    if mix.get("p96"):
        h = heard(mix["p96"], (sorted(wrote_all), 16) if wrote_all else None, mix.get("l96"))
        if h:
            h.pop("steps", None)
            out["heard"] = h
    for ret in ("dub", "space"):
        m = parts.get(ret)
        if m and not m.get("silent") and m.get("p96"):
            h = heard(m["p96"], None, m.get("l96"))
            if h:
                out[ret] = {k: h[k] for k in ("hits", "period", "triplets", "off") if k in h}
    return out
