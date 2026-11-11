"""Checks the OXI E16 scene SF-000 writes (controllers/e16/SF-000.oxie16, or the path given):
its shape against a scene as the OXI App exports one, every address, and its script (GLIDE RINGS and
LANE FOCUS) running against a mock of the E16's Lua (the mock SF Live Suite tests its own scenes with:
1-based encoders and pages, 14-bit ring values, leds.reset hands a ring back to the firmware,
midi.sendCC with a 0-based channel). If SF-000 --miditest has run, the ring reports it made are
played to the script too. The script part needs lupa (pip install lupa): Lua 5.4.

    python3 tests/e16_scene_test.py [scene.oxie16] [rings.txt]
"""
import json
import os
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "controllers", "e16", "SF-000.oxie16")
rings_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(tempfile.gettempdir(), "sf000_midi_test", "rings.txt")
fails = 0


def check(ok, msg):
    global fails
    print(("ok   " if ok else "FAIL ") + msg)
    fails += 0 if ok else 1


# A blank scene as the OXI App 2.x exports it (from SF Live Suite's make_scene.py): the keys, in order.
HEADER = ["title", "icon", "selectedPreset", "color", "transmitMode", "pcOnEntry", "bankOnEntry", "recentPage", "recentPreset",
          "transmitOnSceneEntry", "transmitOnPageSwitch", "transmitOnPresetLoad", "smartTransmit", "outputOnEntry", "holdMode",
          "acceleration", "code", "pages"]
PAGE = ["title", "output", "channel", "encoders"]
ENCODER = ["name", "abbr", "color", "push_action", "turn_actions", "color2"]
PUSH = ["instrument", "parameter", "type", "display", "mode", "channel", "lower", "upper", "nr1", "nr2", "output", "scriptId"]
TURN = ["instrument", "parameter", "type", "display", "mode", "channel", "lower", "upper", "defaultValue", "nr1", "nr2", "output", "scriptId"]
TURN_OFF = {"instrument": 127, "parameter": 0, "type": 0, "display": 10, "mode": 3, "channel": 0, "lower": 0, "upper": 127,
            "defaultValue": 0, "nr1": 0, "nr2": 0, "output": 12, "scriptId": 0}
PUSH_SWAP = {"instrument": 127, "parameter": 0, "type": 9, "display": 0, "mode": 0, "channel": 0, "lower": 0, "upper": 0,
             "nr1": 17, "nr2": 11, "output": 15, "scriptId": 0}
CODES = ["KCK", "CHH", "OHH", "CLP", "PRC", "BAS", "RMB", "LED", "CRD", "PAD", "VOX"]
WHITE, RED, LIGHT_BLUE, PINK, VIOLET, GREEN = 34, 63, 18, 50, 9, 90
RESERVED = {6, 38, 96, 97, 98, 99, 100, 101} | set(range(120, 128))

raw = open(path, "rb").read()
scene = json.loads(raw)
check(all(b < 128 for b in raw) and b"\n" not in raw and b", " not in raw[:200], f"{os.path.basename(path)}: ASCII, compact, one line ({len(raw)} bytes)")
shape = list(scene) == HEADER and len(scene["pages"]) == 12 and len(scene["icon"]) == 32 and 1 <= len(scene["title"]) <= 7
for page in scene["pages"]:
    shape = shape and list(page) == PAGE and len(page["encoders"]) == 16 and 1 <= len(page["title"]) <= 11
    for e in page["encoders"]:
        shape = shape and list(e) == ENCODER and list(e["push_action"]) == PUSH and len(e["turn_actions"]) == 2
        shape = shape and all(list(t) == TURN for t in e["turn_actions"])
        shape = shape and all(type(v) is int for t in e["turn_actions"] + [e["push_action"]] for v in t.values())
        shape = shape and 1 <= len(e["name"]) <= 8 and 1 <= len(e["abbr"]) <= 4 and e["name"] == e["name"].upper() and 0 <= e["color"] <= 99
check(shape, "every object has the App's keys in the App's order; names of 8 and 4 characters at most")
check(scene["transmitOnSceneEntry"] == 0 and scene["transmitOnPageSwitch"] == 0 and scene["transmitOnPresetLoad"] == 0,
      "the scene sends nothing on entry or page change (the rig tells the E16, not the other way round)")

seen, ok, names = {}, True, []
for n, page in enumerate(scene["pages"][:11], 1):
    ok = ok and page["title"] == CODES[n - 1] and page["channel"] == n
    for p, e in enumerate(page["encoders"], 1):
        t, t2, push = e["turn_actions"][0], e["turn_actions"][1], e["push_action"]
        ok = ok and t["type"] == 3 and t["mode"] == 14 and t["channel"] == n and t["nr1"] == 11 + p and t["lower"] == 0 and t["upper"] == 127
        ok = ok and e["color"] == (WHITE if p <= 8 else RED if p <= 11 else LIGHT_BLUE)
        seen[(n, t["nr1"])] = seen.get((n, t["nr1"]), 0) + 1
        if p == 16:
            ok = ok and push == PUSH_SWAP and t2["type"] == 3 and t2["mode"] == 14 and t2["channel"] == n and t2["nr1"] == 28 and e["color2"] == PINK
            ok = ok and t["defaultValue"] == 64 and t2["defaultValue"] == 64 and e["name"] == "HIGH/MID"
            seen[(n, 28)] = seen.get((n, 28), 0) + 1
        else:
            ok = ok and push["type"] == 0 and t2 == TURN_OFF and e["color2"] == 0
    names.append(", ".join(f'{e["name"]} ({e["abbr"]})' for e in page["encoders"][:12]))
    ok = ok and [e["name"] for e in page["encoders"][12:15]] == ["SEND A", "SEND B", "LOW"]
check(ok, "pages 1-11, a lane each on its own channel: knob p sends CC 11 + p; 13 SEND A, 14 SEND B, 15 LOW, 16 HIGH with a swap press to MID (CC 28, pink)")
snap = scene["pages"][11]
ok = snap["title"] == "SNAP" and snap["channel"] == 16
for p, e in enumerate(snap["encoders"], 1):
    t, push = e["turn_actions"][0], e["push_action"]
    ok = ok and t["type"] == 3 and t["channel"] == 16 and t["nr1"] == 11 + p and e["turn_actions"][1] == TURN_OFF and e["color"] == VIOLET
    ok = ok and push["type"] == 2 and push["mode"] == 1 and push["channel"] == 16 and push["nr1"] == 43 + p and (push["upper"], push["lower"]) == (127, 0) and push["output"] == 0
    seen[(16, t["nr1"])] = seen.get((16, t["nr1"]), 0) + 1
    seen[(16, push["nr1"])] = seen.get((16, push["nr1"]), 0) + 1
check(ok, "page 12, the snapshot knobs on channel 16: knob p sends CC 11 + p, its press CC 43 + p (127 down, 0 up)")
check(all(v == 1 for v in seen.values()) and not any(num in RESERVED for _, num in seen) and (16, 119) not in seen,
      f"{len(seen)} addresses, each used once, none on a number MIDI keeps for itself; CC 119 on channel 16 is left for the page's lane")
for n, row in enumerate(names, 1):
    print(f"       page {n:2d} {CODES[n - 1]}: {row}")
print("       page 12 SNAP: " + " ".join(e["name"] for e in snap["encoders"]))

code = scene["code"]
check(code["scriptName"] == "sf_glide_rings" and code["code"].count("\n") == 1 and code["code"].endswith("\n") and len(code["code"].encode()) <= 7000
      and "@" not in code["fullScript"] and "\r\n" in code["fullScript"],
      f"the script is inside: one line of {len(code['code'])} bytes as the App uploads it, and its source")

try:
    from lupa import lua54
except ImportError:
    print("     (lupa is not installed: the script was not run)")
    print("ALL CHECKS PASSED" if fails == 0 else f"{fails} CHECK(S) FAILED")
    sys.exit(1 if fails else 0)

MOCK = """
E = {rings = {}, page = 1, rate = 0, sent = {}}
leds = {
  updateByIndex = function(i, v, c)
    assert(math.type(i) == "integer" and i >= 1 and i <= 16, "led index " .. tostring(i))
    assert(math.type(v) == "integer" and v >= 0 and v <= 16383, "led value " .. tostring(v))
    assert(c >= 0 and c <= 100, "led color")
    E.rings[i] = {v = v, c = c}
  end,
  reset = function(i) assert(i >= 1 and i <= 16); E.rings[i] = nil end,
}
controller = {getPage = function() return E.page end}
page = {}
system = {setUpdateRate = function(ms) E.rate = ms end}
midi = {
  sendCC = function(out, ch, cc, v)
    for _, x in ipairs({out, ch, cc, v}) do assert(math.type(x) == "integer", "sendCC: integers") end
    assert(out >= 0 and out <= 15 and ch >= 0 and ch <= 15 and cc >= 0 and cc <= 127 and v >= 0 and v <= 127, "sendCC range")
    E.sent[#E.sent + 1] = {out, ch, cc, v}
  end,
}
"""


class E16:
    def __init__(self, send=True):
        self.lua = lua54.LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(MOCK)
        if not send:
            self.lua.execute("midi = nil")
        self.lua.execute(code["code"])
        self.G = self.lua.globals()
        self.E = self.G.E
        self.G.page.onInit()

    def sent(self):
        out = [tuple(m[k] for k in range(1, 5)) for m in self.E.sent.values()]
        self.lua.execute("E.sent = {}")
        return out

    def rings(self):
        return {i: (self.E.rings[i].v, self.E.rings[i].c) for i in range(1, 17) if self.E.rings[i] is not None}

    def sysex(self, data):
        self.G.controller.onSysex(self.lua.table_from(list(data)))

    def move(self, *knobs):
        out = [0xF0, 0x7D, 0x53, 0x46, 1]
        for ch, num, v in knobs:
            out += [ch - 1, num, v >> 7, v & 127]
        self.sysex(out + [0xF7])

    def arrive(self, *knobs):
        out = [0xF0, 0x7D, 0x53, 0x46, 2]
        for ch, num in knobs:
            out += [ch - 1, num]
        self.sysex(out + [0xF7])

    def update(self, n=1):
        for _ in range(n):
            self.G.system.update()

    def goto(self, p):
        prev, self.E.page = self.E.page, p
        self.G.page.onPageChange(prev, p)

    def turn(self, i):
        self.G.controller.onEncoderTurn(self.lua.table_from({"index": i, "page": self.E.page, "increment": 1, "value": 0}))


e = E16()
check(e.E.rate == 50 and e.sent() == [(0, 15, 119, 1)], "the scene opens on the kick's page and says so: CC 119 on channel 16, value 1")
e.goto(8)
check(e.sent() == [(0, 15, 119, 8)], "turned to page 8: LEAD's number is sent")
e.goto(12)
check(e.sent() == [], "turned to the snapshot page: nothing is sent, the active lane stays")
e.goto(1)
e.sent()
e.move((1, 13, 9000), (1, 21, 7000))
check(e.rings() == {2: (9000, GREEN), 10: (7000, GREEN)}, f"the kick's knobs 2 and 10 travelling: their rings green, where the sound is ({e.rings()})")
e.move((9, 13, 4000), (1, 27, 5000), (1, 28, 5000))
check(e.rings() == {2: (9000, GREEN), 10: (7000, GREEN)}, "another lane's knobs, and knob 16 (HIGH or MID: the script can't tell which is up): no ring touched")
e.turn(10)
e.move((1, 21, 7100))
check(e.rings() == {2: (9000, GREEN)}, "knob 10 turned by hand: its ring is the hand's for a moment")
e.update(17)
e.move((1, 21, 7200), (1, 13, 9100))
check(e.rings() == {2: (9100, GREEN), 10: (7200, GREEN)}, "0.85 s later it walks green again")
e.arrive((1, 13))
check(e.rings() == {10: (7200, GREEN)}, "knob 2 arrived: its ring is the knob's own again")
e.update(25)
check(e.rings() == {}, "no report for 1.2 s: the last green ring lets go by itself")
e.move((1, 13, 100))
e.goto(9)
check(e.rings() == {}, "a page change hands every ring back")
quiet = E16(send=False)
quiet.goto(3)
quiet.move((3, 12, 500))
check(quiet.rings() == {1: (500, GREEN)}, "firmware without the script's MIDI out: no page number is sent, the rings still work")

if os.path.exists(rings_path):
    msgs = [bytes.fromhex(line) for line in open(rings_path).read().split() if line]
    lit, peak = set(), 0
    for page_no in range(1, 13):
        r = E16()
        r.goto(page_no)
        for m in msgs:
            r.sysex(m)
            now = r.rings()
            peak = max(peak, len(now))
            lit |= {(page_no, i) for i in now}
            assert all(c == GREEN for _, c in now.values())
        check_left = r.rings()
        r.update(25)
        assert r.rings() == {}
    lanes_lit = sorted({p for p, _ in lit})
    check(len(msgs) > 5 and len(lit) > 5 and 12 not in lanes_lit and not any(i == 16 for _, i in lit),
          f"SF-000's own reports of a recall with GLIDE on ({len(msgs)} messages): {len(lit)} rings walk green on pages {lanes_lit}, never knob 16, never the snapshot page")
else:
    print(f"     ({rings_path} is not there: run SF-000 --miditest first to play its ring reports to the script)")

print("ALL CHECKS PASSED" if fails == 0 else f"{fails} CHECK(S) FAILED")
sys.exit(1 if fails else 0)
