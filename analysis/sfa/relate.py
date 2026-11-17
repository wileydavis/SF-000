"""How the snapshots relate: what each is like, how much energy it has, and where it can go.

- energy: how much a snapshot drives a room, 0 to 1, against the rest of the library (its rank among them).
- alike: the snapshots that sound most like it, whatever their energy.
- up / down: snapshots that sound like it but with more energy, or less: a step for the room.
- follows: what was played after it (from the log), and how often.
- grew_from: the chip the set was on when it was taken.
"""
from __future__ import annotations

from collections import Counter, defaultdict

import numpy as np

from .measure import BAND_NAMES, LANES, PARTS

# what is compared, a group at a time; each group counts as much as its weight, however many numbers it has
GROUPS = {
    "spectrum": (1.0, [f"bands.{i}" for i in range(len(BAND_NAMES))] + ["centroid", "rolloff", "flatness"]),
    "rhythm": (1.0, ["onsets", "pulse.0", "pulse.1", "pulse.2"]),
    "space": (0.5, ["width", "movement", "drift"]),
    "lanes": (1.5, [f"shares.{i}" for i in range(len(PARTS))]),
}


def _get(m: dict, name: str):
    src = m.get("parts", {}) if name.startswith("shares") else m.get("mix", {})
    key, _, i = name.partition(".")
    v = src.get(key)
    if v is None:
        return None
    if i:
        v = v[int(i)] if isinstance(v, list) and int(i) < len(v) else None
    return None if v is None else float(v)


def matrix(measured: list[dict]):
    """(values, present, names, weights): one row a snapshot. What one has not got is marked absent, not guessed."""
    names, weights = [], []
    for w, cols in GROUPS.values():
        names += cols
        weights += [w / len(cols)] * len(cols)
    X = np.zeros((len(measured), len(names)))
    M = np.zeros_like(X, dtype=bool)
    for r, m in enumerate(measured):
        for c, n in enumerate(names):
            v = _get(m, n)
            if v is not None and np.isfinite(v):
                X[r, c], M[r, c] = v, True
    # each number on a common scale: its middle and its usual spread across the library
    for c in range(X.shape[1]):
        col = X[M[:, c], c]
        if len(col) >= 2:
            mid = np.median(col)
            spread = np.subtract(*np.percentile(col, [75, 25])) or np.std(col) or 1.0
            X[:, c] = (X[:, c] - mid) / spread
    return X, M, names, np.array(weights)


def distances(X, M, w) -> np.ndarray:
    """Between every two snapshots, over what both have: the weighted mean of squared differences, rooted."""
    n = X.shape[0]
    D = np.zeros((n, n))
    Xm = np.where(M, X, 0.0)
    for i in range(n):
        both = M & M[i]
        diff = (Xm - Xm[i]) ** 2 * both
        num = diff @ w
        den = both.astype(float) @ w
        D[i] = np.sqrt(np.where(den > 0, num / np.maximum(den, 1e-12), np.inf))
    np.fill_diagonal(D, np.inf)
    return D


def energy(measured: list[dict]) -> np.ndarray:
    """A score made of what makes a room move, then each snapshot's rank among them (0 the least, 1 the most)."""
    def col(get):
        v = np.array([get(m) for m in measured], dtype=float)
        ok = np.isfinite(v)
        if ok.sum() < 2:
            return np.zeros(len(v))
        mid = np.median(v[ok])
        spread = np.subtract(*np.percentile(v[ok], [75, 25])) or np.std(v[ok]) or 1.0
        return np.where(ok, (v - mid) / spread, 0.0)

    def high(m):
        b = m.get("mix", {}).get("bands")
        return np.nan if not b else 10 * np.log10(sum(10 ** (x / 10) for x in b[6:]) + 1e-12)

    def low(m):
        b = m.get("mix", {}).get("bands")
        return np.nan if not b else 10 * np.log10(sum(10 ** (x / 10) for x in b[:2]) + 1e-12)

    score = (1.0 * col(lambda m: m.get("mix", {}).get("loudness", np.nan))
             + 1.0 * col(lambda m: m.get("mix", {}).get("onsets", np.nan))
             + 0.6 * col(high)
             + 0.3 * col(low)
             + 0.5 * col(lambda m: (m.get("mix", {}).get("pulse") or [np.nan])[0])
             + 0.5 * col(lambda m: m.get("parts", {}).get("sounding", np.nan)))
    order = np.argsort(np.argsort(score, kind="stable"), kind="stable")
    return order / max(1, len(score) - 1)


def from_log(actions: list[dict]) -> tuple[dict[str, Counter], dict[str, str]]:
    """What was played after what, by hand or by the arrangement, within ten minutes, in the same run; and what each
    snapshot taken was taken from."""
    follows: dict[str, Counter] = defaultdict(Counter)
    grew: dict[str, str] = {}
    sounding = {"recall", "knob", "part", "between"}
    last_by_run: dict[str, tuple[str, int]] = {}
    for a in sorted(actions, key=lambda a: (a.get("t", 0), a.get("k", ""))):
        run = a.get("k", "").rsplit("-", 1)[-1]
        what, u, t = a.get("a"), a.get("u"), int(a.get("t", 0))
        if what == "take" and u:
            if a.get("base"):
                grew[u] = a["base"]
            last_by_run[run] = (u, t)          # (taken while playing: it is what is sounding now)
            continue
        if what in ("stop", "open"):
            last_by_run.pop(run, None)
            continue
        if what not in sounding or not u:
            continue
        before = last_by_run.get(run)
        if before and before[0] != u and t - before[1] <= 600_000:
            follows[before[0]][u] += 1
        last_by_run[run] = (u, t)
    return follows, grew


def relate(measured: list[dict], actions: list[dict], k_alike: int = 8, k_step: int = 4) -> dict:
    """What was found, by snapshot uid."""
    found: dict[str, dict] = {}
    usable = [m for m in measured if m.get("mix") and not m["mix"].get("silent")]
    follows, grew = from_log(actions)
    if usable:
        X, M, names, w = matrix(usable)
        D = distances(X, M, w)
        e = energy(usable)
        uids = [m["uid"] for m in usable]
        for i, m in enumerate(usable):
            order = [j for j in np.argsort(D[i]) if np.isfinite(D[i, j])]
            near = order[: max(k_alike, 30)]
            alike = [[uids[j], round(float(D[i, j]), 3)] for j in order[:k_alike]]
            up = [[uids[j], round(float(D[i, j]), 3), round(float(e[j] - e[i]), 3)] for j in near if e[j] >= e[i] + 0.08][:k_step]
            down = [[uids[j], round(float(D[i, j]), 3), round(float(e[j] - e[i]), 3)] for j in near if e[j] <= e[i] - 0.08][:k_step]
            mix = m["mix"]
            found[m["uid"]] = {
                "energy": round(float(e[i]), 3),
                "alike": alike,
                "up": up,
                "down": down,
                "measured": {
                    "from": m.get("from"),
                    "loudness": round(mix.get("loudness", 0.0), 2),
                    "onsets": round(mix.get("onsets", 0.0), 3),
                    "centroid": round(mix.get("centroid", 0.0), 3),
                    "width": round(mix.get("width", 0.0), 2),
                    "movement": round(mix.get("movement", 0.0), 2),
                    "sounding": (m.get("parts") or {}).get("sounding"),
                },
            }
    for u in set(follows) | set(grew):
        f = found.setdefault(u, {})
        if follows.get(u):
            f["follows"] = [[v, n] for v, n in follows[u].most_common(8)]
        if grew.get(u):
            f["grew_from"] = grew[u]
    return found
