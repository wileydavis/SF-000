"""The parts: each lane of each snapshot on its own, and how parts from different snapshots go together.

- A **part** is one lane of one snapshot (its bass, its chord...). Parts that are the same thing (the same lane with
  the same sound, pattern and effects) in several snapshots are one part, found in each of them.
- **alike**: parts of the same lane that sound alike.
- **fit** between two parts of different lanes: how well they would sound together. Two things count:
  - *clash of notes*: what the two would add of seconds, sevenths and tritones to each other (tonal lanes only);
  - *masking*: how much they sit on each other, in the same sixteenths of the bar and the same part of the spectrum.
- **swap**: for a snapshot and one of its lanes, the same lane from other snapshots that would fit the rest of it.
- **goes with**: for a part, the parts of other lanes that fit it best, and whether they have ever been together.
- **chosen**: parts the log says were put together by a recall of some lanes only.
"""
from __future__ import annotations

import hashlib
import json
from collections import Counter, defaultdict

import numpy as np

from .measure import LANES
from .rhythm import heard, lane_rhythm, played

# how much each interval between two notes rubs: a semitone or a major seventh most, then the tritone, then a tone
ROUGH = np.zeros(12)
ROUGH[[1, 11]] = 1.0
ROUGH[6] = 0.5
ROUGH[[2, 10]] = 0.2
ROUGH_M = np.array([[ROUGH[(j - i) % 12] for j in range(12)] for i in range(12)])
DRUMS = {"kick", "closed_hat", "open_hat", "clap", "perc"}


# ------------------------------------------------------------------------------------------------- identity
def signature(snap: dict, lane: str) -> str | None:
    """What makes a lane the thing it is in a snapshot: its controls, its pattern (or what played it), its effects
    and their settings, its setup. Two lanes with the same signature are the same part."""
    slots = (snap.get("made") or {}).get("slots") or []
    if lane not in slots:
        return None
    i = slots.index(lane)
    vals = snap.get("values") or {}
    mine = {k: round(float(v), 4) for k, v in vals.items() if k.startswith(lane + ".") and not k.endswith(".level")
            and isinstance(v, (int, float))}
    played = snap.get("played") or {}
    came_in = [x for x in played.get("in", []) if x.get("lane") == i]
    pick = lambda key: (snap.get(key) or [None] * len(slots))[i] if len(snap.get(key) or []) > i else None
    what = {
        "def": ((snap.get("made") or {}).get("lanes") or {}).get(lane),
        "values": mine,
        "pattern": came_in or pick("patterns"),
        "muted": (played.get("seqMute") or [0] * len(slots))[i] if len(played.get("seqMute") or []) > i else 0,
        "inserts": pick("inserts"),
        "gen": pick("gen"),
        "setup": ((snap.get("setup") or {}).get("lanes") or [None] * len(slots))[i] if len((snap.get("setup") or {}).get("lanes") or []) > i else None,
        "file": pick("laneFiles"),
        "key": vals.get("g.key"),
    }
    return hashlib.sha256(json.dumps(what, sort_keys=True, default=str).encode()).hexdigest()[:16]


# ------------------------------------------------------------------------------------------------- measures
def _chroma(p: dict) -> np.ndarray:
    c = np.array(p.get("chroma") or [0.0] * 12, dtype=float)
    return c * float(p.get("clarity", 0.0)) if p.get("tonal") else np.zeros(12)


def rough(c: np.ndarray) -> float:
    """How much a set of notes rubs, 0 to 1 (by their shares)."""
    s = c.sum()
    if s <= 0:
        return 0.0
    c = c / s
    return float(c @ ROUGH_M @ c)


def _occ(p: dict) -> np.ndarray:
    """Where a part is in the bar and the spectrum, as power: 6 bands x 16 sixteenths."""
    g = np.array(p.get("grid") or [[0.0] * 16] * 6, dtype=float)
    return g if g.shape == (6, 16) else np.zeros((6, 16))


def masking(a: np.ndarray, b: np.ndarray) -> float:
    """How much of the quieter of two parts the louder sits on, 0 to 1: in each band and sixteenth, the smaller of
    the two, against the whole of the smaller part. Compared in dB so that a part a little under another counts too."""
    sa, sb = a.sum(), b.sum()
    if sa <= 0 or sb <= 0:
        return 0.0
    small, big = (a, b) if sa <= sb else (b, a)
    # (a cell of the small part counts as covered by as much as the big one is not more than 12 dB under it)
    ratio = np.clip((10 * np.log10(big + 1e-15) - 10 * np.log10(small + 1e-15) + 12) / 12, 0, 1)
    return float((small * ratio).sum() / small.sum())


def clash(a: np.ndarray, b: np.ndarray) -> float:
    """What two parts' notes add of rubbing to each other, 0 to 1 (0 if either has no clear notes)."""
    if a.sum() <= 0 or b.sum() <= 0:
        return 0.0
    both = a / a.sum() + b / b.sum()
    return max(0.0, rough(both) - 0.5 * (rough(a) + rough(b)))


def fit(a: dict, b: dict) -> float:
    """How well two parts would sound together, 0 to 1."""
    m = masking(_occ(a), _occ(b))
    c = clash(_chroma(a), _chroma(b))
    return max(0.0, 1.0 - 0.6 * m - 0.4 * min(1.0, c / 0.12))


def part_distance(a: dict, b: dict) -> float:
    """How unlike two parts of the same lane sound: their spectrum, their notes, their place in the bar, their texture."""
    ba, bb = np.array(a.get("bands") or [0] * 10), np.array(b.get("bands") or [0] * 10)
    spec = np.mean(np.abs(ba - bb)) / 10.0 + abs(a.get("centroid", 0) - b.get("centroid", 0)) / 2.0
    ca, cb = np.array(a.get("chroma") or [0] * 12), np.array(b.get("chroma") or [0] * 12)
    notes = 0.5 * np.abs(ca - cb).sum() if a.get("tonal") else 0.0
    ga, gb = _occ(a).sum(axis=0), _occ(b).sum(axis=0)
    ga, gb = ga / (ga.max() + 1e-15), gb / (gb.max() + 1e-15)
    rhythm = float(np.mean(np.abs(ga - gb)))
    touch = (abs(a.get("rough", -60) - b.get("rough", -60)) / 20.0 + abs(a.get("contrast", 0) - b.get("contrast", 0)) / 20.0
             + abs(a.get("flatness", -30) - b.get("flatness", -30)) / 20.0 + abs(a.get("evolves", 0) - b.get("evolves", 0)) * 2.0)
    return round(float(spec + notes + rhythm + touch), 3)


# ------------------------------------------------------------------------------------------------- the log
def chosen_from_log(actions: list[dict], slots_of) -> Counter:
    """Parts put together by hand: a recall (or a stack knob's chip) of some lanes only, over the chip the set was on.
    Each lane it brought, with each lane it left of the one under it: ((uid, lane), (uid, lane)) -> how often."""
    out = Counter()
    for a in actions:
        if a.get("a") not in ("recall", "knob", "part") or not a.get("u") or not a.get("base"):
            continue
        mask = int(a.get("parts", 0) or 0)
        slots = slots_of(a["u"]) or LANES
        brought = [slots[i] for i in range(len(slots)) if mask & (1 << i)]
        if not brought or len(brought) >= len(slots):
            continue
        base_slots = slots_of(a["base"]) or LANES
        kept = [l for l in base_slots if l not in brought]
        for x in brought:
            for y in kept:
                out[tuple(sorted([(a["u"], x), (a["base"], y)]))] += 1
    return out


# ------------------------------------------------------------------------------------------------- relating
def _varied(ranked, k, dist, apart=0.25):
    """The best k of a ranked list, none of them near another already taken (two near the same are one choice)."""
    out = []
    for item in ranked:
        if all(dist(item, o) >= apart for o in out):
            out.append(item)
        if len(out) >= k:
            break
    return out


def relate_parts(measured: list[dict], snaps: dict[str, dict], actions: list[dict], k: int = 5) -> dict:
    """{'parts': {part id: {...}}, 'swap': {uid: {lane: [[uid, fit, ...]]}}}"""
    # every part that sounds, by what it is
    parts: dict[str, dict] = {}
    of_snap: dict[str, dict[str, str]] = defaultdict(dict)       # uid -> lane -> part id
    for m in measured:
        uid = m["uid"]
        for lane, p in (m.get("lanes") or {}).items():
            if lane not in LANES or p.get("silent") or (m.get("parts") and dict(zip(LANES, m["parts"]["shares"])).get(lane, -60) < -40):
                continue
            sig = signature(snaps.get(uid, {}).get("snapshot") or {}, lane) if snaps.get(uid) else None
            pid = f"{lane}:{sig or uid}"
            if pid not in parts:
                snap = (snaps.get(uid) or {}).get("snapshot") or {}
                wr = lane_rhythm(snap, lane) if snap else None
                hd = heard(p.get("p96"), played(snap, lane) if snap else None, p.get("l96")) if p.get("p96") else None
                rh = {k: wr[k] for k in ("len", "kind", "sync", "per_bar")} if wr else {}
                if hd:
                    rh["heard"] = {k: v for k, v in hd.items() if k != "steps"}
                parts[pid] = {"lane": lane, "in": [], "m": p, "bpm": m.get("bpm"), "rhythm": rh}
            parts[pid]["in"].append(uid)
            of_snap[uid][lane] = pid
    ids = list(parts)
    by_lane: dict[str, list[str]] = defaultdict(list)
    for pid in ids:
        by_lane[parts[pid]["lane"]].append(pid)

    # which parts have been together in a snapshot, and which were put together by hand
    together = Counter()
    for uid, lanes in of_snap.items():
        ps = sorted(set(lanes.values()))
        for i in range(len(ps)):
            for j in range(i + 1, len(ps)):
                together[(ps[i], ps[j])] += 1
    slots_of = lambda u: ((snaps.get(u, {}).get("snapshot") or {}).get("made") or {}).get("slots")
    chosen = Counter()
    for ((u1, l1), (u2, l2)), n in chosen_from_log(actions, slots_of).items():
        a, b = of_snap.get(u1, {}).get(l1), of_snap.get(u2, {}).get(l2)
        if a and b and a != b:
            chosen[tuple(sorted([a, b]))] += n

    fits: dict[tuple, float] = {}

    def f(a, b):
        key = (a, b) if a < b else (b, a)
        if key not in fits:
            fits[key] = fit(parts[a]["m"], parts[b]["m"])
        return fits[key]

    out_parts = {}
    for pid in ids:
        p = parts[pid]
        lane = p["lane"]
        alike = sorted(((part_distance(p["m"], parts[q]["m"]), q) for q in by_lane[lane] if q != pid))[:k]
        goes = {}
        for other, qs in by_lane.items():
            if other == lane:
                continue
            ranked = []
            for q in qs:
                pair = tuple(sorted([pid, q]))
                score = f(pid, q) + 0.05 * min(chosen[pair], 4)
                ranked.append((score, q, together[pair], chosen[pair]))
            ranked.sort(key=lambda x: -x[0])
            best = _varied(ranked, 3, lambda x, y: part_distance(parts[x[1]]["m"], parts[y[1]]["m"]))
            goes[other] = [[q, round(s, 3), t, c] for s, q, t, c in best]
        m = p["m"]
        out_parts[pid] = {
            "lane": lane,
            "in": p["in"],
            "level": m.get("level"),
            "hits": m.get("hits"),
            "held": m.get("held"),
            "notes": _notes(m) if m.get("tonal") else None,
            "rhythm": p.get("rhythm") or None,
            "alike": [[q, d] for d, q in alike],
            "goes": goes,
        }

    # swaps: for each snapshot and lane it has, the same lane of other snapshots that fits the rest of it
    swap, add = {}, {}
    for uid, lanes in of_snap.items():
        rest_all = list(lanes.items())
        mine = {}
        for lane, own in lanes.items():
            rest = [pid for l, pid in rest_all if l != lane]
            if not rest:
                continue
            rest_occ = sum(_occ(parts[r]["m"]) for r in rest)
            rest_ch = sum(_chroma(parts[r]["m"]) for r in rest)
            ranked = []
            for q in by_lane[lane]:
                if q == own or part_distance(parts[own]["m"], parts[q]["m"]) < 0.25:          # (the same, or as good as: not a swap)
                    continue
                cand = parts[q]["m"]
                score = max(0.0, 1.0 - 0.6 * masking(_occ(cand), rest_occ) - 0.4 * min(1.0, clash(_chroma(cand), rest_ch) / 0.12))
                score += 0.05 * min(4, sum(chosen[tuple(sorted([q, r]))] for r in rest))
                ranked.append((score, q))
            ranked.sort(key=lambda x: -x[0])
            best = _varied(ranked, k, lambda x, y: part_distance(parts[x[1]]["m"], parts[y[1]]["m"]))
            # (one each from the snapshots that have it: the first named)
            if best:
                mine[lane] = [[q, parts[q]["in"][0], round(s, 3), part_distance(parts[own]["m"], parts[q]["m"])] for s, q in best]
        swap[uid] = mine
        # and what it has not got: a lane from another snapshot that would go over all of it
        whole_occ = sum(_occ(parts[r]["m"]) for r in lanes.values())
        whole_ch = sum(_chroma(parts[r]["m"]) for r in lanes.values())
        more = {}
        for lane, qs in by_lane.items():
            if lane in lanes:
                continue
            ranked = sorted(((max(0.0, 1.0 - 0.6 * masking(_occ(parts[q]["m"]), whole_occ) - 0.4 * min(1.0, clash(_chroma(parts[q]["m"]), whole_ch) / 0.12)), q) for q in qs),
                            key=lambda x: -x[0])
            best = _varied(ranked, 3, lambda x, y: part_distance(parts[x[1]]["m"], parts[y[1]]["m"]))
            if best:
                more[lane] = [[q, parts[q]["in"][0], round(s, 3)] for s, q in best]
        if more:
            add[uid] = more
    return {"parts": out_parts, "swap": swap, "add": add}


NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def _notes(m: dict) -> str:
    """The notes a part is most made of, strongest first (those with a share of 12 % or more)."""
    c = m.get("chroma") or []
    if not c or m.get("clarity", 0) < 0.08:
        return ""
    order = sorted(range(12), key=lambda i: -c[i])
    return " ".join(NOTE_NAMES[i] for i in order if c[i] >= 0.12)[:20]
