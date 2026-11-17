"""The web library, reached as SF-000 reaches it: the address and the Access service token that SET UP keeps."""
from __future__ import annotations

import json
import os
import platform
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


def sf000_home() -> Path:
    """Where SF-000 keeps this machine's own settings (online.json among them)."""
    if platform.system() == "Darwin":
        return Path.home() / "Library" / "Application Support" / "SF-000"
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "SF-000"


class Trouble(Exception):
    """The library did not answer, or answered with something other than what was asked for."""

    def __init__(self, message: str, status: int = 0):
        super().__init__(message)
        self.status = status


class Library:
    def __init__(self, url: str, client_id: str = "", secret: str = ""):
        self.url = url.rstrip("/")
        self.headers = {"User-Agent": "sf000-analysis"}
        if client_id and secret:
            self.headers["CF-Access-Client-Id"] = client_id
            self.headers["CF-Access-Client-Secret"] = secret
        self.context = ssl.create_default_context()

    @staticmethod
    def from_sf000(url: str = "") -> "Library":
        """The library SF-000's SET UP names, with its token. An address given here is used instead (for a library
        run on this machine to try things on, which asks for no token)."""
        setup = {}
        f = sf000_home() / "online.json"
        if f.exists():
            try:
                setup = json.loads(f.read_text())
            except ValueError:
                setup = {}
        if url:
            same = setup.get("url", "").rstrip("/") == url.rstrip("/")
            return Library(url, setup.get("id", "") if same else "", setup.get("secret", "") if same else "")
        if not setup.get("url"):
            raise Trouble(f"SF-000's SET UP has no web library address on this machine ({f}). Fill it in there, or give one with --library.")
        return Library(setup["url"], setup.get("id", ""), setup.get("secret", ""))

    # ------------------------------------------------------------------ asking
    def request(self, method: str, path: str, body: bytes | None = None, kind: str = "application/json", timeout: float = 60.0, tries: int = 3):
        """(status, bytes, headers). No answer is tried again twice, a little later each time."""
        last = None
        for attempt in range(tries):
            req = urllib.request.Request(self.url + path, data=body, method=method, headers=dict(self.headers))
            if body is not None:
                req.add_header("Content-Type", kind)
            try:
                with urllib.request.urlopen(req, timeout=timeout, context=self.context if self.url.startswith("https") else None) as r:
                    return r.status, r.read(), dict(r.headers)
            except urllib.error.HTTPError as e:
                data = e.read() if hasattr(e, "read") else b""
                if e.code in (401, 403):
                    raise Trouble("the web library did not let this machine in (the Access token in SF-000's SET UP?)", e.code)
                if e.code >= 500 and attempt + 1 < tries:
                    last = Trouble(f"the web library answered {e.code}", e.code)
                    time.sleep(2 + 4 * attempt)
                    continue
                return e.code, data, dict(e.headers or {})
            except (urllib.error.URLError, TimeoutError, ConnectionError, OSError) as e:
                last = Trouble(f"no answer from the web library ({getattr(e, 'reason', e)})")
                if attempt + 1 < tries:
                    time.sleep(2 + 4 * attempt)
        raise last or Trouble("no answer from the web library")

    def get_json(self, path: str, timeout: float = 60.0):
        status, data, _ = self.request("GET", path, timeout=timeout)
        if status == 404:
            return None
        if status != 200:
            raise Trouble(f"{path}: the web library answered {status}", status)
        return json.loads(data)

    def get_bytes(self, path: str, timeout: float = 300.0) -> bytes | None:
        status, data, _ = self.request("GET", path, timeout=timeout)
        if status == 404:
            return None
        if status != 200:
            raise Trouble(f"{path}: the web library answered {status}", status)
        return data

    def post_json(self, path: str, value, timeout: float = 120.0):
        status, data, _ = self.request("POST", path, json.dumps(value, separators=(",", ":")).encode(), timeout=timeout)
        try:
            answer = json.loads(data) if data else {}
        except ValueError:
            answer = {}
        return status, answer

    # ------------------------------------------------------------------ what it holds
    def snapshots(self) -> list[dict]:
        out, offset = [], 0
        while True:
            page = self.get_json(f"/api/snapshots?limit=5000&offset={offset}") or {}
            got = page.get("snapshots", [])
            out += got
            if len(got) < 5000:
                return out
            offset += len(got)

    def snapshot(self, uid: str):
        return self.get_json("/api/snapshots/" + urllib.parse.quote(uid, safe=""))

    def clip(self, uid: str):
        return self.get_bytes("/api/clips/" + urllib.parse.quote(uid, safe=""))

    def stem(self, uid: str, key: str):
        return self.get_bytes(f"/api/stems/{urllib.parse.quote(uid, safe='')}?key={urllib.parse.quote(key, safe='')}")

    def lossless(self, uid: str):
        return self.get_bytes("/api/lossless/" + urllib.parse.quote(uid, safe=""), timeout=900.0)

    def actions(self, library: str, after: int, limit: int = 5000) -> list[dict]:
        page = self.get_json(f"/api/actions?library={library}&after={after}&limit={limit}")
        return (page or {}).get("actions", [])

    def send_snapshot(self, snap: dict):
        return self.post_json("/api/snapshots", {"snapshot": snap, "defs": {}})

    def stack(self, sid: str):
        return self.get_json("/api/stacks/" + urllib.parse.quote(sid, safe=""))

    def make_stack(self, value: dict):
        return self.post_json("/api/stacks", value)

    def change_stack(self, sid: str, value: dict):
        status, data, _ = self.request("PATCH", "/api/stacks/" + urllib.parse.quote(sid, safe=""), json.dumps(value).encode())
        try:
            return status, json.loads(data) if data else {}
        except ValueError:
            return status, {}

    def latest_found(self):
        return self.get_json("/api/analysis/latest")

    def send_found(self, found: dict):
        return self.post_json("/api/analysis", found, timeout=300.0)
