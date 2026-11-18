"""Making a chip of a combination: one snapshot's whole rig, with some of its lanes taken from other snapshots.

What a lane is in a snapshot (FORMAT.md) is copied whole from the snapshot it is taken from: its controls (every
`<lane>.*` value, its effects' cells among them), its pattern (or the notes that came into it), its effects, its
generator, its setup, its sound file, its effects' files, and the print of its definition. Everything that is not a
lane's (the returns, the drum bus, the chassis, the key, POCKETS for the rig) is the base's. A lane taken away is
silenced: no pattern, no generator, its sequencer muted.

The chip is a new snapshot, under a library of the analysis's own (so its name is never one SF-000 could give), and
says in `ext.sf000analysis` what it was made of.
"""
from __future__ import annotations

import copy
import time

from .measure import LANES


def _slots(snap: dict) -> list[str]:
    return list((snap.get("made") or {}).get("slots") or [])


def _at(lst, i, default=None):
    return lst[i] if isinstance(lst, list) and i < len(lst) else default


def _put(lst, i, value, fill):
    while len(lst) <= i:
        lst.append(copy.deepcopy(fill))
    lst[i] = value


def _lists(snap: dict, i: int) -> int:
    """Which lists of models (and which knobs of theirs) a snapshot's lane was taken on: FORMAT.md, `lists`."""
    by = snap.get("listsBy")
    if isinstance(by, list) and i < len(by) and isinstance(by[i], int):
        return by[i]
    v = snap.get("lists", 1)
    return v if isinstance(v, int) else 1


def compose(base: dict, picks: dict[str, dict | None], name: str, library_id: str, when_ms: int | None = None) -> dict:
    """base: a snapshot (as SF-000 keeps it). picks: lane -> the snapshot to take it from, or None to take it away."""
    out = copy.deepcopy(base)
    slots = _slots(out)
    lists = [_lists(base, i) for i in range(len(slots))]
    made = out.setdefault("made", {})
    vals = out.setdefault("values", {})
    took = {}
    for lane, src in picks.items():
        if lane not in LANES or lane not in slots:
            continue
        i = slots.index(lane)
        if src is None:
            _silence(out, i, lane)
            took[lane] = None
            continue
        sslots = _slots(src)
        if lane not in sslots:
            continue
        j = sslots.index(lane)
        lists[i] = _lists(src, j)           # (a lane's model and knobs mean what they meant where it was taken)
        # its controls: every one of the lane's, and none of the base's left behind
        for k in [k for k in vals if k.startswith(lane + ".")]:
            del vals[k]
        for k, v in (src.get("values") or {}).items():
            if k.startswith(lane + "."):
                vals[k] = v
        for key, fill in (("patterns", {"len": 16, "steps": []}), ("inserts", ["NONE", "NONE", "NONE"]), ("gen", -1), ("laneFiles", "")):
            if key in src or key in out:
                _put(out.setdefault(key, []), i, copy.deepcopy(_at(src.get(key), j, fill)), fill)
        for key in ("lanes", "inserts"):
            s_list = (src.get("setup") or {}).get(key)
            if s_list is not None:
                _put(out.setdefault("setup", {}).setdefault(key, []), i, copy.deepcopy(_at(s_list, j, {})), {})
        out["insertFiles"] = [x for x in out.get("insertFiles", []) if x.get("lane") != i] + \
                             [dict(copy.deepcopy(x), lane=i) for x in src.get("insertFiles", []) if x.get("lane") == j]
        # what played it: its own sequencer, muted or not, and notes that came into it from outside
        sp, op = src.get("played") or {}, out.setdefault("played", {})
        mute = op.setdefault("seqMute", [0] * len(slots))
        _put(mute, i, _at(sp.get("seqMute"), j, 0), 0)
        op["in"] = [x for x in op.get("in", []) if x.get("lane") != i] + [dict(copy.deepcopy(x), lane=i) for x in sp.get("in", []) if x.get("lane") == j]
        # what it is made with: its definition, and its effects'
        lanes_made = made.setdefault("lanes", {})
        if ((src.get("made") or {}).get("lanes") or {}).get(lane):
            lanes_made[lane] = src["made"]["lanes"][lane]
        for fx in _at(src.get("inserts"), j, []) or []:
            p = ((src.get("made") or {}).get("fx") or {}).get(fx)
            if p and fx not in made.setdefault("fx", {}):
                made["fx"][fx] = p
        took[lane] = src.get("uid")
    # a new snapshot
    when = int(when_ms or time.time() * 1000)
    out["id"] = when
    out["time"] = when
    out["uid"] = f"{library_id}-{when:x}"
    out["name"] = name.strip().upper()[:24] or "MADE"
    out.pop("profile", None)                    # (what it sounds like is for it to be played and heard)
    # which lists each lane was taken on: one figure where they agree, and lane by lane where a lane came from a
    # snapshot of another build (SF-000 reads `listsBy` over `lists`)
    out.pop("lists", None)
    out.pop("listsBy", None)
    if lists and all(v == lists[0] for v in lists):
        if lists[0] != 1:
            out["lists"] = lists[0]
    elif lists:
        out["lists"] = min(lists)
        out["listsBy"] = lists
    ext = out.setdefault("ext", {})
    ext["sf000analysis"] = {"base": base.get("uid"), "lanes": took, "made": when}
    return out


def _silence(out: dict, i: int, lane: str):
    _put(out.setdefault("patterns", []), i, {"len": 16, "steps": []}, {"len": 16, "steps": []})
    if "gen" in out:
        _put(out["gen"], i, -1, -1)
    op = out.setdefault("played", {})
    _put(op.setdefault("seqMute", [0] * len(_slots(out))), i, 1, 0)
    op["in"] = [x for x in op.get("in", []) if x.get("lane") != i]
    if isinstance(out.get("laneFiles"), list) and i < len(out["laneFiles"]):
        out["laneFiles"][i] = ""
