"""The loop: fetch what is new, measure it, relate everything, send what was found, wait, again.

Started by hand when the instrument is not being played (start.command). It stands aside while SF-000 is running
on this machine, and takes up where it left off when it is started again.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import signal as sig
import subprocess
import sys
import time
import traceback
from pathlib import Path

from . import MEASURE_VERSION, VERSION
from .library import Library, Trouble
from .measure import measure
from .parts import relate_parts
from .relate import relate
from .report import write_report


def data_home() -> Path:
    if platform.system() == "Darwin":
        return Path.home() / "Library" / "Application Support" / "SF-000 Analysis"
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "sf000-analysis"


def say(*words):
    print(time.strftime("%H:%M:%S"), *words, flush=True)


def sf000_running() -> bool:
    """SF-000 playing on this machine: the analysis keeps out of its way (the disk, the processors, the line)."""
    if not shutil.which("pgrep"):
        return False
    try:
        return subprocess.run(["pgrep", "-x", "SF-000"], capture_output=True).returncode == 0
    except OSError:
        return False


class Store:
    """What is kept here: each snapshot as the library has it, its clip (to listen to), what was measured, the log."""

    def __init__(self, root: Path, keep_lossless: bool):
        self.root = root
        self.keep_lossless = keep_lossless
        for d in ("snapshots", "clips", "measured", "log", "found", "parts"):
            (root / d).mkdir(parents=True, exist_ok=True)
        if keep_lossless:
            (root / "lossless").mkdir(exist_ok=True)
        self.state_file = root / "state.json"
        self.state = json.loads(self.state_file.read_text()) if self.state_file.exists() else {}

    def save_state(self):
        self._write(self.state_file, json.dumps(self.state, indent=1))

    @staticmethod
    def _write(path: Path, data):
        tmp = path.with_suffix(path.suffix + ".part")
        tmp.write_bytes(data if isinstance(data, bytes) else data.encode())
        os.replace(tmp, path)

    @staticmethod
    def safe(uid: str) -> str:
        return "".join(c if c.isalnum() or c in "-_" else "_" for c in uid)

    def measured(self, uid: str):
        f = self.root / "measured" / (self.safe(uid) + ".json")
        return json.loads(f.read_text()) if f.exists() else None

    def keep_measured(self, uid: str, m: dict):
        self._write(self.root / "measured" / (self.safe(uid) + ".json"), json.dumps(m))

    def keep_snapshot(self, uid: str, value: dict):
        self._write(self.root / "snapshots" / (self.safe(uid) + ".json"), json.dumps(value))

    def clip_file(self, uid: str) -> Path:
        return self.root / "clips" / (self.safe(uid) + ".ogg")

    def part_file(self, uid: str, key: str) -> Path:
        return self.root / "parts" / self.safe(uid) / (self.safe(key) + ".ogg")

    def keep_part(self, uid: str, key: str, data: bytes):
        f = self.part_file(uid, key)
        f.parent.mkdir(parents=True, exist_ok=True)
        self._write(f, data)

    def snapshot(self, uid: str):
        f = self.root / "snapshots" / (self.safe(uid) + ".json")
        return json.loads(f.read_text()) if f.exists() else None

    def log_file(self, library: str) -> Path:
        return self.root / "log" / (library + ".jsonl")


class Analysis:
    def __init__(self, lib: Library, store: Store, anyway: bool = False):
        self.lib = lib
        self.store = store
        self.anyway = anyway
        self.stopping = False
        self.seen = None

    def held(self) -> bool:
        return not self.anyway and sf000_running()

    # -------------------------------------------------------------------------------- fetch and measure
    def measure_new(self, rows: list[dict], budget_s: float) -> int:
        """Each snapshot not yet measured, or measured from less than there is now (lossless having arrived), or by
        an older way of measuring. As many as fit in the time given; the rest next time."""
        started, done = time.time(), 0
        order = {"none": 0, "clip": 1, "clip+stems": 2, "lossless": 3}
        for row in sorted(rows, key=lambda r: r.get("taken_at") or r.get("received_at") or 0):
            if self.stopping or time.time() - started > budget_s or self.held():
                break
            uid = row["uid"]
            best = "lossless" if row.get("lossless") else ("clip+stems" if row.get("has_clip") and row.get("stems") else ("clip" if row.get("has_clip") else "none"))
            had = self.store.measured(uid)
            if had and had.get("v") == MEASURE_VERSION and order.get(had.get("from"), 0) >= order[best]:
                continue
            if best == "none" and had:
                continue
            try:
                one = self.lib.snapshot(uid)
                if one is not None:
                    self.store.keep_snapshot(uid, one)
                clip = None
                if row.get("has_clip"):
                    cf = self.store.clip_file(uid)
                    if cf.exists():
                        clip = cf.read_bytes()
                    else:
                        clip = self.lib.clip(uid)
                        if clip:
                            self.store._write(cf, clip)
                lossless = self.lib.lossless(uid) if row.get("lossless") else None
                if lossless and self.store.keep_lossless:
                    self.store._write(self.store.root / "lossless" / (self.store.safe(uid) + ".sf0lossless"), lossless)
                stems = None
                if not lossless and row.get("stems") and clip:
                    stems = {}
                    for key in row["stems"]:
                        data = self.lib.stem(uid, key)
                        if data:
                            stems[key] = data
                m = measure(row, lossless, clip, stems, keep_part=lambda key, data, u=uid: self.store.keep_part(u, key, data))
            except Trouble:
                raise
            except Exception as e:          # (a file that cannot be read: said, kept as not measurable, not tried again until there is more)
                m = {"uid": uid, "from": "none", "error": f"{type(e).__name__}: {e}"}
                say("could not measure", row.get("name") or uid, "-", m["error"])
            m["v"] = MEASURE_VERSION
            m["name"] = row.get("name", "")
            self.store.keep_measured(uid, m)
            done += 1
        return done

    def fetch_log(self, libraries: set[str]) -> int:
        """Each library's log, from where it was last taken to. A line already here (by its own name) is not kept twice."""
        if self.seen is None:
            self.seen = {a.get("k") for a in self.all_actions()}
        new = 0
        pos = self.store.state.setdefault("log", {})
        for library in sorted(libraries):
            after = int(pos.get(library, 0))
            while True:
                got = self.lib.actions(library, max(0, after - 1))       # (from a millisecond before: lines at the same time are not missed)
                fresh = [a for a in got if a.get("k") and a["k"] not in self.seen]
                if fresh:
                    with self.store.log_file(library).open("a") as f:
                        for a in fresh:
                            f.write(json.dumps(a, separators=(",", ":")) + "\n")
                            self.seen.add(a["k"])
                    new += len(fresh)
                if not got:
                    break
                top = max(int(a.get("t", 0)) for a in got)
                if len(got) < 5000 or top <= after:
                    after = max(after, top)
                    break
                after = top
            pos[library] = after
        self.store.save_state()
        return new

    def all_actions(self) -> list[dict]:
        out = []
        for f in (self.store.root / "log").glob("*.jsonl"):
            for line in f.read_text().splitlines():
                try:
                    out.append(json.loads(line))
                except ValueError:
                    pass
        return out

    # -------------------------------------------------------------------------------- one pass
    def once(self, budget_s: float = 600.0, send: bool = True) -> dict:
        rows = self.lib.snapshots()
        here = {r["uid"] for r in rows}
        libraries = {r["library_id"] for r in rows if r.get("library_id")}
        measured_now = self.measure_new(rows, budget_s)
        new_lines = self.fetch_log(libraries)
        measured = [m for m in (self.store.measured(r["uid"]) for r in rows) if m]
        actions = self.all_actions()
        found_snaps = relate(measured, actions)
        found_snaps = {u: v for u, v in found_snaps.items() if u in here}
        snaps = {r["uid"]: self.store.snapshot(r["uid"]) or {} for r in rows}
        pr = relate_parts([m for m in measured if m["uid"] in here], snaps, actions)
        for uid, lanes in pr["swap"].items():
            if lanes:
                found_snaps.setdefault(uid, {})["swap"] = lanes
        for uid, lanes in pr["add"].items():
            found_snaps.setdefault(uid, {})["add"] = lanes
        found = {
            "at": int(time.time() * 1000),
            "app": f"sf000-analysis {VERSION}",
            "measure": MEASURE_VERSION,
            "counts": {
                "snapshots": len(rows),
                "measured": sum(1 for m in measured if m.get("mix") and not m["mix"].get("silent")),
                "lossless": sum(1 for m in measured if m.get("from") == "lossless" and m.get("mix") and not m["mix"].get("silent")),
                "log_lines": len(actions),
                "parts": len(pr["parts"]),
            },
            "snapshots": found_snaps,
            "parts": pr["parts"],
        }
        self.store._write(self.store.root / "found" / "latest.json", json.dumps(found))
        sent = None
        what = hashlib.sha256(json.dumps({k: v for k, v in found.items() if k != "at"}, sort_keys=True).encode()).hexdigest()
        if send and what != self.store.state.get("sent"):          # (only what differs from what it last sent)
            status, answer = self.lib.send_found(found)
            if status == 404:
                sent = "the web library has no place for it yet"
            elif status == 200:
                sent = "the same as before" if answer.get("same") else "sent"
                self.store.state["sent"] = what
                self.store.save_state()
            else:
                sent = f"not taken ({status}: {answer.get('error', '')})"
        write_report(self.store.root, rows, measured, found, snaps)
        found["_pass"] = {"measured": measured_now, "log": new_lines, "sent": sent}
        return found

    def loop(self, every_s: float, budget_s: float, send: bool):
        while not self.stopping:
            if self.held():
                say("SF-000 is running: standing aside.")
                while self.held() and not self.stopping:
                    time.sleep(20)
                if self.stopping:
                    break
                say("SF-000 has stopped: carrying on.")
            try:
                found = self.once(budget_s, send)
                p = found["_pass"]
                c = found["counts"]
                say(f"{c['measured']} of {c['snapshots']} snapshots measured ({c['lossless']} without loss), {c['log_lines']} lines of the log."
                    f" This pass: {p['measured']} measured, {p['log']} new lines." + (f" Findings {p['sent']}." if p["sent"] else ""))
                if p["measured"] and not self.held():
                    continue        # (more may be waiting: straight on)
            except Trouble as e:
                say(str(e), "- trying again later.")
            except Exception:
                say("something went wrong; trying again later:")
                traceback.print_exc()
            for _ in range(int(every_s)):
                if self.stopping:
                    break
                time.sleep(1)


def serve(root: Path, port: int):
    """The page and what it plays, from this machine only: a page opened from the disk may not read the parts it mixes."""
    import functools
    import http.server
    import threading

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass

    try:
        httpd = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Quiet, directory=str(root)))
    except OSError:
        return None          # (one is running already, from another window: that one serves the same folder)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="sf000-analysis", description="Studies the SF-000 snapshot library away from the instrument.")
    ap.add_argument("--library", default="", help="the web library's address (otherwise SF-000's SET UP's)")
    ap.add_argument("--data", default="", help=f"where to keep what it fetches and finds (otherwise {data_home()})")
    ap.add_argument("--once", action="store_true", help="one pass, then stop")
    ap.add_argument("--every", type=float, default=300, help="seconds between passes (300)")
    ap.add_argument("--budget", type=float, default=600, help="seconds of measuring a pass at most (600)")
    ap.add_argument("--anyway", action="store_true", help="do not stand aside while SF-000 is running")
    ap.add_argument("--keep-lossless", action="store_true", help="keep each snapshot's lossless sound here too (about 10 MB each)")
    ap.add_argument("--no-send", action="store_true", help="find, but do not send what was found to the web library")
    ap.add_argument("--port", type=int, default=8765, help="where the page is served on this machine (8765)")
    ap.add_argument("--no-open", action="store_true", help="do not open the page in the browser when it starts")
    args = ap.parse_args(argv)

    try:
        os.nice(10)         # (it gives way to anything else this machine is doing)
    except (OSError, AttributeError):
        pass
    root = Path(args.data) if args.data else data_home()
    try:
        lib = Library.from_sf000(args.library)
    except Trouble as e:
        say(str(e))
        return 2
    store = Store(root, args.keep_lossless)
    a = Analysis(lib, store, args.anyway)

    def stop(*_):
        if a.stopping:
            raise SystemExit(130)
        a.stopping = True
        say("stopping after this snapshot (again to stop at once)...")
    sig.signal(sig.SIGINT, stop)
    sig.signal(sig.SIGTERM, stop)

    say(f"SF-000 Analysis {VERSION}: {lib.url}, keeping what it finds in {root}")
    page = f"http://localhost:{args.port}/report.html"
    if not args.once:
        serve(root, args.port)
        say(f"listen to what it finds: {page}")
        if platform.system() == "Darwin" and not args.no_open:
            threading_open = __import__("threading").Timer(20.0, lambda: subprocess.run(["open", page]))
            threading_open.daemon = True
            threading_open.start()
    if args.once:
        if a.held():
            say("SF-000 is running: not now (or --anyway).")
            return 1
        found = a.once(args.budget, not args.no_send)
        p, c = found["_pass"], found["counts"]
        say(f"{c['measured']} of {c['snapshots']} snapshots measured, {p['measured']} this pass, {p['log']} new lines of the log." + (f" Findings {p['sent']}." if p["sent"] else ""))
        return 0
    a.loop(args.every, args.budget, not args.no_send)
    say("stopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
