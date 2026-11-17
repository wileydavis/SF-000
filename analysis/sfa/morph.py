"""The morph between two snapshots: scanned for what is worth hearing in it, and reshaped.

SF-000 itself renders it (run with no screen: `SF-000 --render`), as a stack knob holding the two would play it: the
sound goes from one to the other as the knob turns, with the notes of the one it set out from. Each point rendered is
measured as a snapshot is, and the path through them is looked at:

- how fast the sound changes along it (it is seldom even: linear in the controls is not linear to the ear);
- where it is something neither end is (NEW), where its parts sit together best (FITS), where it changes most
  (EDGE), where it holds (HOLDS), where it has more or less energy than both ends (LIFTS, DROPS).

Reshaped, it is a stack: the two with snapshots made between them, which a stack knob plays through.
- EVEN: the snapshots between are put where the sound has changed by equal amounts, so an even turn is an even change.
- LEAD: some lanes go first (the whole of their change in the first half of the turn), the rest follow.
"""
from __future__ import annotations

import copy
import hashlib
import io
import json
import os
import platform
import shutil
import subprocess
import threading
import time
from pathlib import Path

import numpy as np
import soundfile as sf

from .compose import compose
from .library import Trouble, sf000_library_dirs
from .measure import LANES, measure, read_lossless
from .parts import fit, part_distance
from .relate import GROUPS, energy, matrix

STEPS = 11          # points along a morph, both ends among them
KEYS = ["OFF", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def key_of(snap: dict) -> str:
    """KEY as the snapshot has it (the instrument's own list: g.key, thirteen places)."""
    v = (snap.get("values") or {}).get("g.key")
    return KEYS[max(0, min(12, int(round(float(v) * 12))))] if isinstance(v, (int, float)) and v >= 0 else "OFF"


def tuned_with(notes_of: dict, other: dict) -> tuple[dict, list[str]]:
    """The other end, with its TUNE knobs where the snapshot the notes are of has them, and the lanes that took.

    A stack knob carries the sound over and leaves the notes and the key where it set out from. A lane's TUNE is a
    sound knob, so on the knob it glides: a kick tuned two octaves apart in the two snapshots passes through every
    pitch between, and a point taken part of the way stands on one of them, in no key. That is a glide while the
    knob turns and a wrong note in a snapshot. So in what is scanned and made here the tuning stays with the notes
    it was tuned to: every point is in tune with its own notes."""
    out = copy.deepcopy(other)
    mine, theirs = notes_of.get("values") or {}, out.get("values")
    held = []
    if isinstance(theirs, dict):
        for k, v in mine.items():
            if k.endswith(".tune") and isinstance(v, (int, float)) and v >= 0 and isinstance(theirs.get(k), (int, float)) and abs(theirs[k] - v) > 1e-4:
                theirs[k] = v
                held.append(k[:-5])
    if held:
        # (its own name in the job: the instrument keeps what it has taken by uid, and this is not the snapshot of that uid)
        out["uid"] = f"{other.get('uid', '')}.tuned-{str(notes_of.get('uid', ''))[-10:]}"
    return out, [l for l in LANES if l in held]


# ------------------------------------------------------------------------------------------------- the instrument
def find_sf000(given: str = "") -> Path | None:
    here = Path(__file__).resolve().parents[2]
    for c in ([Path(given)] if given else []) + [
        here / "build" / "SF000_artefacts" / "Release" / "SF-000.app" / "Contents" / "MacOS" / "SF-000",
        here / "build" / "SF000_artefacts" / "Release" / "SF-000",
        Path("/Applications/SF-000.app/Contents/MacOS/SF-000"),
    ]:
        if c.is_file() and os.access(c, os.X_OK):
            return c
    return None


def instrument_running() -> bool:
    """SF-000 being played on this machine (not a render of our own)."""
    try:
        out = subprocess.run(["ps", "-axo", "command="], capture_output=True, text=True).stdout
    except OSError:
        return False
    for line in out.splitlines():
        exe = line.split(" --")[0].strip()
        if os.path.basename(exe) == "SF-000" and " --render " not in line + " ":
            return True
    return False


def refs_in(value) -> set[str]:
    """Every sound file a snapshot names (a lane's sample, an effect's impulse response), as it writes them."""
    out = set()
    if isinstance(value, str):
        if value.startswith("samples/"):
            out.add(value)
    elif isinstance(value, dict):
        for v in value.values():
            out |= refs_in(v)
    elif isinstance(value, list):
        for v in value:
            out |= refs_in(v)
    return out


def sha256_of(f: Path) -> str:
    h = hashlib.sha256()
    with open(f, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# The lanes that have nothing to play without their sound file, and do not fall silent without it: the voice lane sings
# its notes with a choir of its own when it has no phrase (suite/modules/Voice.cpp), which is not that snapshot at all.
NEEDS_ITS_FILE = {"voice"}


def without_files(snap: dict, missing: set[str]) -> tuple[dict, list[str], list[str]]:
    """The snapshot as it can be rendered without those sound files: a lane that would play a stand-in for its file
    is turned down (its level and both sends), so that what is heard is the snapshot less a lane and never something
    that is not in it. Returns it, the lanes left out, and the other lanes that name a missing file."""
    files = snap.get("laneFiles") if isinstance(snap.get("laneFiles"), list) else []
    named = [LANES[i] for i, f in enumerate(files) if i < len(LANES) and f in missing]
    left = [l for l in named if l in NEEDS_ITS_FILE]
    if not left:
        return snap, [], named
    out = copy.deepcopy(snap)
    values = out.setdefault("values", {})
    for l in left:
        i = LANES.index(l)
        for k in ("level", "send_a", "send_b"):
            values[f"{l}.{k}"] = 0.0
        # and no notes for it, written or heard: turned down is not yet silent
        if isinstance(out.get("patterns"), list) and i < len(out["patterns"]) and isinstance(out["patterns"][i], dict):
            out["patterns"][i]["steps"] = []
        played = out.get("played")
        if isinstance(played, dict) and isinstance(played.get("in"), list):
            played["in"] = [e for e in played["in"] if not (isinstance(e, dict) and e.get("lane") == i)]
    return out, left, [l for l in named if l not in left]


class Renderer:
    def __init__(self, lib, store, sf000: Path | None):
        self.lib, self.store, self.sf000 = lib, store, sf000
        self.child = None
        (store.root / "files").mkdir(exist_ok=True)

    # the sound files, fetched once each and kept by their hash
    def _hashes(self, libraries: set[str]) -> dict[str, str]:
        known = self.store.state.setdefault("file_hashes", {})
        for library in libraries:
            listed = self.lib.get_json(f"/api/sessions?library={library}") or {}
            got = (listed.get("sessions") or [None])[0]
            if not got:
                continue
            session = self.lib.get_json(f"/api/sessions/{library}/{got['at']}") or {}
            for s in session.get("samples", []):
                if s.get("file") and s.get("sha256"):
                    known[s["file"]] = s["sha256"]
        self.store.save_state()
        return known

    def _local(self, r: str, sha: str | None) -> Path | None:
        """The same file in SF-000's own library on this machine, if it is there and is that file. SF-000 keeps each
        in a folder named for the first eight digits of its SHA-256, so it can be told even before the web library has
        been sent it (large files go up only while the rig is stopped, one at a time)."""
        for root in sf000_library_dirs():
            f = root / r
            try:
                if not f.is_file():
                    continue
                h = sha256_of(f)
            except OSError:
                continue
            parts = r.split("/")
            if (sha and h == sha) or (not sha and len(parts) == 3 and h.startswith(parts[1])):
                return f
        return None

    def place_files(self, snaps: list[dict], scratch: Path) -> list[str]:
        """The sound files those snapshots name, into the folder the instrument will work in: from the web library, or
        from SF-000's own library on this machine. Returns what could not be had (or cannot be read on this machine)."""
        refs = set()
        for s in snaps:
            refs |= refs_in(s)
        known = self.store.state.get("file_hashes", {})
        if any(r not in known for r in refs):
            libs = {str(s.get("uid", "")).split(".")[0].split("-")[0] for s in snaps} | {u.split("-")[0] for u in self._sources(snaps)}
            known = self._hashes({x for x in libs if len(x) == 16})
        missing = []
        self.from_where = {}
        for r in sorted(refs):
            to = scratch / r
            if r.lower().endswith(".mp3") and platform.system() != "Darwin":
                missing.append(r)                       # (SF-000 built for this machine cannot read mp3: the Mac's can)
                self.from_where[r] = "cannot be read on this machine"
                continue
            if to.exists():
                self.from_where[r] = "kept"
                continue
            sha = known.get(r)
            kept = self.store.root / "files" / sha if sha else None
            src, where = None, ""
            if kept is not None and kept.exists():
                src, where = kept, "kept"
            if src is None:
                src = self._local(r, sha)
                where = "this machine"
            if src is None and kept is not None:
                data = self.lib.get_bytes("/api/files/" + sha)
                if data:
                    self.store._write(kept, data)
                    src, where = kept, "the web library"
            if src is None:
                missing.append(r)
                self.from_where[r] = "not in the web library, not on this machine"
                continue
            to.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, to)
            self.from_where[r] = where
        return missing

    @staticmethod
    def _sources(snaps):
        out = set()
        for s in snaps:
            made = (s.get("ext") or {}).get("sf000analysis") or {}
            for u in [made.get("base"), made.get("a"), made.get("b")] + list((made.get("lanes") or {}).values()):
                if u:
                    out.add(u)
        return out

    def defs_for(self, uids: list[str]) -> dict:
        prints = set()
        for u in uids:
            row = self.store.snapshot(u) or {}
            p = row.get("prints") or []
            prints |= set(p.split(",") if isinstance(p, str) else p)
        prints.discard("")
        out = {}
        prints = sorted(prints)
        for i in range(0, len(prints), 40):
            got = self.lib.get_json("/api/defs?prints=" + ",".join(prints[i:i + 40])) or {}
            out.update(got.get("defs") or {})
        return out

    def render(self, work: Path, snaps: list[dict], defs: dict, points: list[dict], progress=None, stop=None) -> dict:
        """Runs the instrument on the job. Returns what was done without: {"missing": the sound files that could not
        be had, "left": the lanes left out for want of theirs, "named": other lanes that name one}. Raises Trouble."""
        if self.sf000 is None:
            raise Trouble("SF-000 itself is needed to render this, and its program was not found. Build it (./build.sh) in the folder this came in.")
        scratch, out = work / "rig", work / "out"
        scratch.mkdir(parents=True, exist_ok=True)
        out.mkdir(parents=True, exist_ok=True)
        (scratch / "session.json").unlink(missing_ok=True)      # (each render from nothing: the instrument keeps what it took, by uid)
        missing = self.place_files(snaps, scratch)
        left, named = [], []
        if missing:
            quiet = [without_files(sn, set(missing)) for sn in snaps]
            snaps = [q[0] for q in quiet]
            left = [l for l in LANES if any(l in q[1] for q in quiet)]
            named = [l for l in LANES if any(l in q[2] for q in quiet)]
        job = work / "job.json"
        job.write_text(json.dumps({"dir": str(scratch), "out": str(out), "defs": defs, "snapshots": snaps, "points": points}))
        cmd = [str(self.sf000), "--render", str(job)]
        env = dict(os.environ)
        if platform.system() != "Darwin" and not env.get("DISPLAY") and shutil.which("xvfb-run"):
            cmd = ["xvfb-run", "-a"] + cmd
        env["HOME"] = str(scratch)                     # (nothing of the instrument's own settings is read or written)
        env["XDG_CONFIG_HOME"] = str(scratch / ".config")
        self.child = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, env=env)
        done, failed = 0, []
        try:
            for line in self.child.stdout:
                if line.startswith("done "):
                    done += 1
                    if progress:
                        progress(done, len(points))
                elif line.startswith("FAIL"):
                    failed.append(line.strip())
                if stop is not None and stop():
                    self.child.terminate()
                    raise Trouble("SF-000 was opened: the render stopped, to keep out of its way. Ask again when it is closed.")
            code = self.child.wait()
        finally:
            if self.child.poll() is None:
                self.child.kill()
            self.child = None
        if code != 0 or failed or done < len(points):
            raise Trouble("SF-000 could not render it" + (f" ({failed[0]})" if failed else f" (it stopped after {done} of {len(points)})"))
        return {"missing": missing, "left": left, "named": named, "why": {r: self.from_where.get(r, "") for r in missing}}


# ------------------------------------------------------------------------------------------------- reading a render
def read_point(out: Path, key: str, keep_ogg: Path | None = None) -> tuple[dict, dict]:
    """(what was measured, the snapshot as it was taken). The output is kept as Ogg beside it, to be listened to."""
    info = json.loads((out / f"{key}.json").read_text())
    data = (out / f"{key}.lossless").read_bytes()
    m = measure({"uid": key, "bpm": info.get("bpm")}, data, None, None)
    if keep_ogg is not None:
        _, parts = read_lossless(data)
        mix, rate = parts["mix"]
        buf = io.BytesIO()
        sf.write(buf, np.clip(mix, -1, 1), rate, format="OGG", subtype="VORBIS")
        keep_ogg.write_bytes(buf.getvalue())
    (out / f"{key}.lossless").unlink()          # (measured and kept as Ogg: ten megabytes a point are not kept)
    return m, info["snapshot"]


def inner_fit(m: dict) -> float:
    """How well a sound's own parts sit together: the mean fit of every two of its lanes that sound."""
    lanes = [p for k, p in (m.get("lanes") or {}).items() if k in LANES and not p.get("silent") and p.get("level", -99) > -45]
    if len(lanes) < 2:
        return 1.0
    scores = [fit(lanes[i], lanes[j]) for i in range(len(lanes)) for j in range(i + 1, len(lanes))]
    return float(np.mean(scores))


def look(points: list[dict], library: list[dict], twin: dict | None = None, smooth: bool = True) -> dict:
    """The path through the points (each as measured, in order), on the library's own scales.

    No two renders of the same thing measure quite the same (hats are noise, oscillators drift), so `twin`, the first
    point rendered a second time, says how far apart two of the same are: the noise. Steps along the path are taken
    from the path smoothed a little (each point with a quarter of each neighbour; the ends as they are) with the noise
    taken out, and nothing is called worth hearing that is not well clear of it."""
    usable = [m for m in library if m.get("mix") and not m["mix"].get("silent")]
    extra = [twin] if twin else []
    X, M, names, w = matrix(usable + points + extra)
    # Along a morph the notes do not change, so its rhythm is left out of how far apart two points are: what is compared
    # is the sound (its spread, its texture, its width, its lanes). The count of hits is also the one thing two renders
    # of the same disagree about.
    steady = set(GROUPS["rhythm"][1]) | {"drift"}
    w = np.array([0.0 if name in steady else x for name, x in zip(names, w)])
    n0, n = len(usable), len(points)
    P = (np.where(M, X, 0.0) * np.sqrt(w / w.sum()))[n0:]            # (so that plain distance here is the weighted one)
    noise = float(np.sqrt(((P[n] - P[0]) ** 2).sum())) if twin else 0.0
    P = P[:n]
    S = P.copy()
    if smooth and n >= 4:
        S[1:-1] = 0.25 * P[:-2] + 0.5 * P[1:-1] + 0.25 * P[2:]
    # (two smoothed neighbours differ by noise an eighth as strong, in power, as two renders of the same do; unsmoothed, by all of it)
    floor2 = (noise ** 2) / 8.0 if smooth and n >= 4 else noise ** 2
    steps = [float(np.sqrt(max(0.0, float(np.sum((S[i + 1] - S[i]) ** 2)) - floor2))) for i in range(n - 1)]
    length = float(sum(steps))
    chord = float(np.sqrt(((P[-1] - P[0]) ** 2).sum()))
    # how far each point is from simply being between the two ends
    off = []
    d = P[-1] - P[0]
    for i in range(n):
        t = float(np.clip(((S[i] - P[0]) * d).sum() / ((d * d).sum() + 1e-12), 0, 1))
        off.append(float(np.sqrt(((S[i] - (P[0] + t * d)) ** 2).sum())))
    e = energy(usable + points)[n0:]
    fits = [inner_fit(m) for m in points]
    # its texture, each quality on the library's own scale (so that 1 is the spread of the middle half of the library), and
    # how far two renders of the same differ in it
    touch = {}
    for name in TOUCH:
        c = names.index(name)
        col = X[n0:n0 + n, c]
        touch[name] = {"v": [float(v) for v in col], "noise": float(abs(X[n0 + n, c] - X[n0, c])) if twin else 0.0}
    return {"steps": steps, "length": length, "chord": chord, "off": off, "energy": [float(x) for x in e], "fit": fits, "noise": noise, "touch": touch}


# the qualities of a sound's texture, and what a point with more or less of one than both ends is called
TOUCH = {"rough": ("ROUGHER", "SMOOTHER"), "contrast": ("MORE RESONANT", "MORE DIFFUSE"), "flatness": ("NOISIER", "PURER"),
         "evolves": ("MORE ALIVE", "STILLER"), "moves": ("MOVES MORE", "MOVES LESS"), "centroid": ("BRIGHTER", "DARKER"), "width": ("WIDER", "NARROWER")}


def tags(path: dict) -> list[list[str]]:
    """What is worth hearing at each point of a path."""
    n = len(path["off"])
    out = [[] for _ in range(n)]
    steps, off, e, fits = path["steps"], path["off"], path["energy"], path["fit"]
    scale = max(path["chord"], 1e-6)
    noise = path.get("noise", 0.0)
    inner = range(1, n - 1)
    # NEW: furthest from between, if that is far enough to hear (a sixth of the way from one end to the other)
    peaks = [i for i in inner if off[i] >= off[i - 1] and off[i] >= off[i + 1] and off[i] > max(scale / 6, 1.5 * noise)]
    for i in sorted(peaks, key=lambda i: -off[i])[:2]:
        out[i].append("NEW")
    # FITS: its parts sit together better than at either end
    best = max(inner, key=lambda i: fits[i], default=None)
    if best is not None and fits[best] > max(fits[0], fits[-1]) + 0.015:
        out[best].append("FITS")
    # EDGE: the step into it is the largest, and twice the usual
    if steps:
        usual = float(np.median(steps)) + 1e-9
        big = int(np.argmax(steps))
        if steps[big] > 2.0 * usual and steps[big] > noise and 0 < big + 1 < n - 1:
            out[big + 1].append("EDGE")
        # HOLDS: the middle of the longest run of small steps (under half the mean)
        mean = float(np.mean(steps)) + 1e-9
        run, best_run = [], []
        for i, s in enumerate(steps):
            run = run + [i] if s < 0.5 * mean else []
            if len(run) > len(best_run):
                best_run = run
        if len(best_run) >= 3:
            mid = best_run[len(best_run) // 2]
            if 0 < mid < n - 1:
                out[mid].append("HOLDS")
    # its texture: the point with the most (or the least) of a quality, if that is more than both ends have by a
    # margin worth hearing (0.35 of the library's spread) and well clear of the noise
    for name, (more, less) in TOUCH.items():
        t = (path.get("touch") or {}).get(name)
        if not t or n < 3:
            continue
        v, margin = t["v"], max(0.35, 2.0 * t["noise"])
        i_hi = max(inner, key=lambda i: v[i])
        i_lo = min(inner, key=lambda i: v[i])
        if v[i_hi] > max(v[0], v[-1]) + margin:
            out[i_hi].append(more)
        if v[i_lo] < min(v[0], v[-1]) - margin:
            out[i_lo].append(less)
    hi, lo = max(e[0], e[-1]), min(e[0], e[-1])
    top = max(inner, key=lambda i: e[i], default=None)
    if top is not None and e[top] > hi + 0.08:
        out[top].append("LIFTS")
    bottom = min(inner, key=lambda i: e[i], default=None)
    if bottom is not None and e[bottom] < lo - 0.08:
        out[bottom].append("DROPS")
    return out


def even_places(fs: list[float], steps: list[float], count: int) -> list[float]:
    """Where along a morph `count` snapshots go so that the sound changes by as much between each two of them."""
    s = np.concatenate([[0.0], np.cumsum(steps)])
    if s[-1] <= 0:
        return [float((j + 1) / (count + 1)) for j in range(count)]
    targets = [s[-1] * (j + 1) / (count + 1) for j in range(count)]
    return [round(float(np.interp(t, s, fs)), 4) for t in targets]


def unevenness(steps: list[float]) -> float:
    """The largest step over the mean one: 1 is perfectly even."""
    return float(max(steps) / (np.mean(steps) + 1e-12)) if steps else 1.0


def lane_change(first: dict, last: dict) -> list[list]:
    """Which lanes change most from one end to the other, most first."""
    a, b = first.get("lanes") or {}, last.get("lanes") or {}
    out = []
    for lane in LANES:
        pa, pb = a.get(lane), b.get(lane)
        sa, sb = not pa or pa.get("silent"), not pb or pb.get("silent")
        if sa and sb:
            continue
        d = 3.0 if sa != sb else part_distance(pa, pb) + abs(pa.get("level", 0) - pb.get("level", 0)) / 12.0
        out.append([lane, round(float(d), 3)])
    return sorted(out, key=lambda x: -x[1])


def brand(snap: dict, name: str, library_id: str, when: int, how: dict) -> dict:
    """A snapshot the instrument rendered, made a chip of the analysis's own."""
    out = copy.deepcopy(snap)
    out["id"], out["time"] = when, when
    out["uid"] = f"{library_id}-{when:x}"
    out["name"] = (name.strip().upper() or "MORPH")[:24]
    out.setdefault("ext", {})["sf000analysis"] = dict(how, made=when)
    return out


# ------------------------------------------------------------------------------------------------- the jobs
class Morphs:
    """Scans asked for from the page, one at a time, each in a folder of its own (morph/<id>/)."""

    def __init__(self, analysis, sf000: Path | None):
        self.a = analysis
        self.r = Renderer(analysis.lib, analysis.store, sf000)
        self.root = analysis.store.root / "morph"
        self.root.mkdir(exist_ok=True)
        self.lock = threading.Lock()
        self.busy = None

    def _file(self, sid: str) -> Path:
        return self.root / sid / "scan.json"

    def get(self, sid: str):
        f = self._file(sid)
        return json.loads(f.read_text()) if f.exists() else None

    def _put(self, scan: dict):
        d = self.root / scan["id"]
        d.mkdir(parents=True, exist_ok=True)
        self.a.store._write(d / "scan.json", json.dumps(scan))

    def _ends(self, a: str, b: str, notes: str) -> tuple[dict, dict, list[str]]:
        """The two snapshots as a morph here goes between them: the tuning with the notes (tuned_with)."""
        sa, sb = self._snap(a), self._snap(b)
        if notes == "b":
            sa, held = tuned_with(sb, sa)
        else:
            sb, held = tuned_with(sa, sb)
        return sa, sb, held

    def _snap(self, uid: str) -> dict:
        s = (self.a.store.snapshot(uid) or {}).get("snapshot")
        if not s:
            got = self.a.lib.snapshot(uid)
            if got:
                self.a.store.keep_snapshot(uid, got)
                s = got.get("snapshot")
        if not s:
            raise Trouble("that snapshot is not here yet")
        return s

    def _library(self) -> list[dict]:
        out = []
        for f in (self.a.store.root / "measured").glob("*.json"):
            try:
                out.append(json.loads(f.read_text()))
            except ValueError:
                pass
        return out

    def _guard(self):
        if not self.a.anyway and instrument_running():
            raise Trouble("SF-000 is running: close it first. A render uses the processors it plays with.")

    def _start(self, scan: dict, work):
        with self.lock:
            if self.busy is not None:
                raise Trouble("it is rendering another already: wait for that one")
            self.busy = scan["id"]

        def run():
            try:
                work()
            except Trouble as e:
                scan["status"], scan["error"] = "failed", str(e)
            except Exception as e:
                scan["status"], scan["error"] = "failed", f"{type(e).__name__}: {e}"
            finally:
                self._put(scan)
                with self.lock:
                    self.busy = None
        threading.Thread(target=run, daemon=True).start()

    def _stop(self):
        return not self.a.anyway and instrument_running()

    # ---- a scan
    def scan(self, a: str, b: str, notes: str = "a") -> dict:
        if a == b:
            raise Trouble("two different snapshots")
        self._guard()
        notes = "b" if notes == "b" else "a"
        sid = f"{self.a.store.safe(a)[-10:]}_{self.a.store.safe(b)[-10:]}_{notes}"
        scan = {"id": sid, "a": a, "b": b, "notes": notes, "status": "rendering", "done": 0, "of": STEPS + 1, "started": int(time.time() * 1000), "shapes": {}}
        self._put(scan)

        def work():
            sa, sb, scan["held"] = self._ends(a, b, notes)
            scan["keys"] = [key_of(sa), key_of(sb)]
            fs = [round(i / (STEPS - 1), 4) for i in range(STEPS)]
            points = [{"key": f"p{i:02d}", "morph": [0, 1, f], "notes": notes} for i, f in enumerate(fs)]
            points.append({"key": "twin", "morph": [0, 1, 0.0], "notes": notes})        # (the first again: how alike two renders of the same are)
            wd = self.root / sid

            def progress(done, of):
                scan["done"] = done
                self._put(scan)
            got = self.r.render(wd, [sa, sb], self.r.defs_for([a, b]), points, progress, self._stop)
            scan["missing"], scan["left"], scan["named"], scan["why"] = got["missing"], got["left"], got["named"], got["why"]
            scan["status"] = "measuring"
            self._put(scan)
            measured = []
            for p in points:
                m, snap = read_point(wd / "out", p["key"], wd / f"{p['key']}.ogg")
                (wd / f"{p['key']}.snapshot.json").write_text(json.dumps(snap))
                self.a.store._write(wd / f"{p['key']}.measured.json", json.dumps(m))
                measured.append(m)
            twin = measured.pop()
            points.pop()
            self.a.store._write(wd / "twin.measured.json", json.dumps(twin))
            path = look(measured, self._library(), twin)
            scan["noise"] = round(path["noise"], 3)
            tg = tags(path)
            scan["points"] = [{"key": p["key"], "f": f, "tags": tg[i], "energy": round(path["energy"][i], 3), "off": round(path["off"][i], 3),
                               "fit": round(path["fit"][i], 3), "loudness": round(measured[i].get("mix", {}).get("loudness", -70), 1),
                               "touch": {k: round(path["touch"][k]["v"][i], 2) for k in ("rough", "contrast", "flatness", "evolves")}}
                              for i, (p, f) in enumerate(zip(points, fs))]
            scan["steps"] = [round(s, 4) for s in path["steps"]]
            scan["length"], scan["chord"] = round(path["length"], 3), round(path["chord"], 3)
            scan["uneven"] = round(unevenness(path["steps"]), 2)
            scan["lanes"] = lane_change(measured[0], measured[-1])
            scan["status"] = "done"
        self._start(scan, work)
        return scan

    # ---- a reshaping: the snapshots between, rendered and measured, for a stack
    def reshape(self, sid: str, kind: str, count: int = 3, lanes: list[str] | None = None) -> dict:
        scan = self.get(sid)
        if not scan or scan.get("status") != "done":
            raise Trouble("scan it first")
        self._guard()
        a, b, notes = scan["a"], scan["b"], scan["notes"]
        sa, sb = self._snap(a), self._snap(b)
        wd = self.root / sid
        if kind == "lead":
            lanes = [l for l in (lanes or []) if l in LANES]
            if not lanes:
                raise Trouble("choose the lanes that go first")
            shape_id = "lead_" + "_".join(lanes)
            # between the two: the lanes that lead are the far end's already, everything else is still the near end's. So
            # the first half of the turn changes those lanes alone, and the second half the rest.
            mid = compose(sa, {l: sb for l in lanes}, "x", "0" * 16, when_ms=1)
            mid["uid"] = "0" * 16 + "-lead"
            snaps = [sa, sb, mid]
            points = [{"key": f"{shape_id}_0", "recall": 2}]
            how = [{"lead": lanes, "a": a, "b": b, "notes": notes}]
        else:
            count = max(1, min(7, int(count)))
            shape_id = f"even_{count}"
            places = even_places([p["f"] for p in scan["points"]], scan["steps"], count)
            snaps = list(self._ends(a, b, notes)[:2])
            points = [{"key": f"{shape_id}_{i}", "morph": [0, 1, f], "notes": notes} for i, f in enumerate(places)]
            how = [{"a": a, "b": b, "f": f, "notes": notes} for f in places]
        shape = {"id": shape_id, "kind": kind, "status": "rendering", "lanes": lanes if kind == "lead" else None}
        scan["shapes"][shape_id] = shape
        self._put(scan)

        def work():
            try:
                self.r.render(wd, snaps, self.r.defs_for([a, b]), points, None, self._stop)
                ends = [json.loads((wd / f"{k}.measured.json").read_text()) for k in ("p00", f"p{STEPS - 1:02d}")]
                between = []
                for p in points:
                    m, snap = read_point(wd / "out", p["key"], wd / f"{p['key']}.ogg")
                    (wd / f"{p['key']}.snapshot.json").write_text(json.dumps(snap))
                    between.append(m)
                twin = json.loads((wd / "twin.measured.json").read_text())
                path = look([ends[0]] + between + [ends[1]], self._library(), twin, smooth=False)
                shape["chips"] = [{"key": p["key"], "how": h, "energy": round(path["energy"][i + 1], 3)} for i, (p, h) in enumerate(zip(points, how))]
                shape["steps"] = [round(s, 4) for s in path["steps"]]
                shape["uneven"] = round(unevenness(path["steps"]), 2)
                # (what an even turn through the plain morph gives, at as many places: to see what the reshaping bought)
                n = len(points) + 1
                plain = np.interp([j / n for j in range(n + 1)], [p["f"] for p in scan["points"]], np.concatenate([[0.0], np.cumsum(scan["steps"])]))
                shape["plain_uneven"] = round(unevenness(list(np.diff(plain))), 2)
                shape["status"] = "done"
            except Exception as e:          # (the scan itself is as good as it was)
                shape["status"], shape["error"] = "failed", str(e)
        self._start(scan, work)
        return shape

    # ---- chips and stacks made of them
    def _send(self, snap: dict, name: str, how: dict, ogg: Path | None) -> dict:
        when = max(int(time.time() * 1000), int(self.a.store.state.get("last_made", 0)) + 1)
        self.a.store.state["last_made"] = when
        chip = brand(snap, name, self.a.library_id(), when, how)
        status, answer = self.a.lib.send_snapshot(chip)
        if status != 200 or not answer.get("ok"):
            raise Trouble(f"the web library did not take it ({status}: {answer.get('error', '')})")
        if ogg is not None and ogg.exists():
            self.a.lib.request("PUT", "/api/clips/" + chip["uid"], ogg.read_bytes(), kind="audio/ogg", timeout=120.0)
        self.a.store.keep_snapshot(chip["uid"], {"uid": chip["uid"], "name": chip["name"], "snapshot": chip})
        self.a.store.state.setdefault("made", []).append({"uid": chip["uid"], "name": chip["name"], "how": how, "at": when})
        self.a.store.save_state()
        return chip

    def make_point(self, sid: str, key: str, name: str) -> dict:
        scan = self.get(sid)
        wd = self.root / sid
        f = wd / f"{key}.snapshot.json"
        if not scan or not f.exists():
            raise Trouble("that point is not here")
        p = next((p for p in scan.get("points", []) if p["key"] == key), None)
        how = {"a": scan["a"], "b": scan["b"], "f": p["f"] if p else None, "notes": scan["notes"]}
        chip = self._send(json.loads(f.read_text()), name, how, wd / f"{key}.ogg")
        stack = self.a.made_stack(chip["uid"])
        return {"ok": True, "uid": chip["uid"], "name": chip["name"], "stack": stack}

    def make_stack(self, sid: str, shape_id: str, name: str) -> dict:
        scan = self.get(sid)
        shape = (scan or {}).get("shapes", {}).get(shape_id)
        if not shape or shape.get("status") != "done":
            raise Trouble("render it first")
        wd = self.root / sid
        name = (name.strip().upper() or "MORPH")[:20]
        uids = []
        for i, c in enumerate(shape["chips"]):
            snap = json.loads((wd / f"{c['key']}.snapshot.json").read_text())
            chip = self._send(snap, f"{name} {i + 1}", c["how"], wd / f"{c['key']}.ogg")
            uids.append(chip["uid"])
        items = [scan["a"]] + uids + [scan["b"]]
        status, answer = self.a.lib.make_stack({"name": name, "kind": "stack", "items": items, "load": True})
        sid2 = answer.get("id") or (answer.get("stack") or {}).get("id")
        if status != 200 or not sid2:
            raise Trouble(f"the web library did not take the stack ({status}: {answer.get('error', '')})")
        return {"ok": True, "stack": sid2, "name": name, "chips": len(items)}
