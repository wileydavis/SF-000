"""Checks the OXI E16 scene SF-000 writes (controllers/e16/SF-000.oxie16, or the path given):
its shape against a scene as the OXI App exports one, every address, and its script (GLIDE RINGS,
PAGE FOCUS and NAMES) running against a mock of the E16's Lua (the mock SF Live Suite tests its own
scenes with: 1-based encoders and pages, 14-bit ring values, leds.reset hands a ring back to the
firmware, midi.sendCC with a 0-based channel; and slots.update, slots.reset, page.setTitle and
page.resetTitle as the E16's manual for OS 1.3.0 describes them). The second scene written beside
it (-FX) is checked the same way. If SF-000 --miditest has run, the ring reports it made are played
to the script too. The script part needs lupa (pip install lupa): Lua 5.4.

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
        elif p == 1:
            # GLIDE behind a press on knob 1 of every lane's page: one address for all of them
            ok = ok and push == PUSH_SWAP and t2["type"] == 3 and t2["mode"] == 14 and t2["channel"] == 16 and t2["nr1"] == 30 and e["color2"] == GREEN
        elif p in (13, 14):
            # SEND A pressed is the pointer's wheel, SEND B pressed is OPEN: a CC each, on channel 16, the same from every page
            ok = ok and push["type"] == 2 and push["mode"] == 1 and push["channel"] == 16 and push["nr1"] == 20 + p and (push["upper"], push["lower"]) == (127, 0)
            ok = ok and t2 == TURN_OFF and e["color2"] == 0
        else:
            ok = ok and push["type"] == 0 and t2 == TURN_OFF and e["color2"] == 0
    names.append(", ".join(f'{e["name"]} ({e["abbr"]})' for e in page["encoders"][:12]))
    ok = ok and [e["name"] for e in page["encoders"][12:15]] == ["SEND A", "SEND B", "LOW"]
check(ok, "pages 1-11, a lane each on its own channel: knob p sends CC 11 + p; 13 SEND A (pressed: the wheel, CC 33 on channel 16), 14 SEND B (pressed: OPEN, CC 34), 15 LOW, "
          "16 HIGH with a swap press to MID (CC 28, pink)")
snap = scene["pages"][11]
ok = snap["title"] == "SNAP" and snap["channel"] == 16
for p, e in enumerate(snap["encoders"], 1):
    t, push = e["turn_actions"][0], e["push_action"]
    t2 = e["turn_actions"][1]
    ok = ok and t["type"] == 3 and t["channel"] == 16 and t["nr1"] == 11 + p and e["color"] == VIOLET
    seen[(16, t["nr1"])] = seen.get((16, t["nr1"]), 0) + 1
    if p == 1:
        # GLIDE behind its press, as on a lane's page: the one GLIDE address
        ok = ok and push == PUSH_SWAP and t2["type"] == 3 and t2["mode"] == 14 and t2["channel"] == 16 and t2["nr1"] == 30 and e["color2"] == GREEN
    elif p == 13:
        # pressed, the pointer's wheel, as on a lane's page
        ok = ok and t2 == TURN_OFF and push["type"] == 2 and push["mode"] == 1 and push["channel"] == 16 and push["nr1"] == 33 and (push["upper"], push["lower"]) == (127, 0)
    else:
        ok = ok and t2 == TURN_OFF and push["type"] == 2 and push["mode"] == 1 and push["channel"] == 16 and push["nr1"] == 43 + p and (push["upper"], push["lower"]) == (127, 0) and push["output"] == 0
        seen[(16, push["nr1"])] = seen.get((16, push["nr1"]), 0) + 1
check(ok, "page 12, the snapshot knobs on channel 16: knob p sends CC 11 + p, its press CC 43 + p (127 down, 0 up); but knob 1 pressed swaps to GLIDE (green) and knob 13 pressed is the pointer's wheel (CC 33), as on a lane's page")
check(all(v == 1 for v in seen.values()) and not any(num in RESERVED for _, num in seen) and not ({(16, 119), (16, 31), (16, 33), (16, 34), (16, 35), (16, 36)} & set(seen)),
      f"{len(seen)} addresses, each used once, none on a number MIDI keeps for itself; CC 119 on channel 16 is left for the page, 31 and 33 to 36 for the wheel and the keys")
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
E.names, E.title = {}, nil
page = {
  setTitle = function(t) assert(type(t) == "string" and #t >= 1 and #t <= 15, "title of 1 to 15"); E.title = t end,
  resetTitle = function() E.title = nil end,
}
slots = {
  update = function(i, t) assert(math.type(i) == "integer" and i >= 1 and i <= 16 and type(t) == "string" and #t >= 1 and #t <= 4, "slot name"); E.names[i] = t end,
  reset = function(i) assert(i >= 1 and i <= 16); E.names[i] = nil end,
}
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
    def __init__(self, send=True, script=None, names=True):
        self.lua = lua54.LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(MOCK)
        if not send:
            self.lua.execute("midi = nil")
        if not names:
            self.lua.execute("slots = nil; page = {}")       # firmware before names and titles
        self.lua.execute(script if script is not None else code["code"])
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

    def sent_to(self, p):
        """The page turned from outside (a Program Change): the E16 is on it, and nothing calls onPageChange."""
        self.E.page = p

    def names(self):
        return {i: self.E.names[i] for i in range(1, 17) if self.E.names[i] is not None}

    def name_page(self, page_no, names):
        out = [0xF0, 0x7D, 0x53, 0x46, 4, page_no]
        for n in names:
            out += [ord(c) for c in (n + "    ")[:4]]
        self.sysex(out + [0xF7])

    def title(self, text):
        self.sysex([0xF0, 0x7D, 0x53, 0x46, 5] + [ord(c) for c in text] + [0xF7])

    def turn(self, i):
        self.G.controller.onEncoderTurn(self.lua.table_from({"index": i, "page": self.E.page, "increment": 1, "value": 0}))


e = E16()
check(e.E.rate == 50 and e.sent() == [(0, 15, 119, 1)], "the scene opens on the kick's page and says so: CC 119 on channel 16, value 1")
e.goto(8)
check(e.sent() == [(0, 15, 119, 8)], "turned to page 8: LEAD's number is sent")
e.goto(12)
check(e.sent() == [(0, 15, 119, 12)], "turned to the snapshot page: it says 12 (no lane's number: the active lane stays, and the page is told its values)")
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
quiet.move((3, 12, 500), (3, 13, 500))
check(quiet.rings() == {2: (500, GREEN)}, "firmware without the script's MIDI out: no page number is sent, the rings still work (knob 1 swaps to GLIDE, so it keeps its own ring)")

# PAGE FOCUS, whoever turns the page; and NAMES
e = E16()
e.sent()
e.goto(4)
e.update(3)
check(e.sent() == [(0, 15, 119, 4)], "a page says itself once: the updates that follow a page change do not say it again")
e.sent_to(7)
before = e.sent()
e.update()
e.move((7, 13, 3000))
check(before == [] and e.sent() == [(0, 15, 119, 7)] and e.rings() == {2: (3000, GREEN)},
      "a page turned from outside with no onPageChange (a Program Change): the next update sees it, says it, and the rings are that page's")
blank = ["    "] * 16
e.name_page(7, ["TIME", "FDBK", "-"] + blank[3:12] + ["SEL"] + blank[13:])
e.title("RMB 1 DELAY")
check(e.names() == {1: "TIME", 2: "FDBK", 3: "-", 13: "SEL"} and e.E.title == "RMB 1 DELAY", f"SF-000 names what the page holds, and titles it: {e.names()}, {e.E.title}; four spaces leave a knob its own name")
e.name_page(3, ["XXXX"] * 16)
check(e.names() == {1: "TIME", 2: "FDBK", 3: "-", 13: "SEL"}, "names for a page that is not the one up are not put on it")
e.name_page(7, blank)
e.title("")
check(e.names() == {} and e.E.title is None, "sixteen blanks and no title: the scene's own again")
e.name_page(7, ["AAAA"] + blank[1:])
e.title("X")
e.goto(2)
check(e.names() == {} and e.E.title is None, "a page change takes the names and the title away with it (the E16 would keep them)")
e.sent_to(5)
e.name_page(5, ["BBBB"] + blank[1:])
check(e.names() == {1: "BBBB"} and e.sent()[-1] == (0, 15, 119, 5), "names that come for a page turned from outside, before any update: the page is seen first, then named")
e.sysex([0xF0, 0x7D, 0x53, 0x46, 9, 1, 2, 3, 0xF7])
e.sysex([0xF0, 0x7D, 0x53, 0x46, 3, 1, 0xF7])
check(e.rings() == {} and e.names() == {1: "BBBB"}, "a message of a kind it does not know changes nothing")
old = E16(names=False)
old.goto(2)
old.name_page(2, ["AAAA"] * 16)
old.title("TITLE")
old.move((2, 13, 500))
check(old.rings() == {2: (500, GREEN)} and old.sent()[-1] == (0, 15, 119, 2), "firmware without names and titles: they are not asked of it, and the rest works")

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

# The second scene, written beside it: the same form. The library's map and finders, then a page
# for everything the first scene has none for.
lib_path = os.path.join(os.path.dirname(path), os.path.splitext(os.path.basename(path))[0] + "-FX.oxie16")
if os.path.exists(lib_path):
    lraw = open(lib_path, "rb").read()
    lib = json.loads(lraw)
    encs = [e for p in lib["pages"] for e in p["encoders"]]
    ok = all(b < 128 for b in lraw) and b"\n" not in lraw and list(lib) == HEADER and len(lib["pages"]) == 12 and 1 <= len(lib["title"]) <= 7
    ok = ok and all(list(p) == PAGE and len(p["encoders"]) == 16 and 1 <= len(p["title"]) <= 11 for p in lib["pages"])
    ok = ok and all(list(e) == ENCODER and list(e["push_action"]) == PUSH and all(list(t) == TURN for t in e["turn_actions"]) for e in encs)
    ok = ok and all(1 <= len(e["name"]) <= 8 and 1 <= len(e["abbr"]) <= 4 and e["name"] == e["name"].upper() for e in encs)
    glide_knob = lib["pages"][11]["encoders"][0]         # (the snapshot page's knob 1: GLIDE behind its press, its second turn)
    ok = ok and all(e["turn_actions"][1] == TURN_OFF and e["color2"] == 0 for e in encs if e is not glide_knob)
    ok = ok and lib["transmitOnSceneEntry"] == 0 and lib["transmitOnPageSwitch"] == 0 and lib["transmitOnPresetLoad"] == 0
    check(ok and lib["title"] == "SF-FX" and [p["title"] for p in lib["pages"]] == ["MAP", "FIND", "INS 1", "INS 2", "INS 3", "RETURNS", "MASTER", "SEQ", "CFG 1", "CFG 2", "CFG 3", "SNAP"],
          f"{os.path.basename(lib_path)}: the same form, twelve pages: " + ", ".join(p["title"] for p in lib["pages"]))

    def turn(e):
        t = e["turn_actions"][0]
        return (t["channel"], t["nr1"]) if t["type"] == 3 else None

    def push(e):
        q = e["push_action"]
        return (q["channel"], q["nr1"]) if q["type"] == 2 and q["mode"] == 1 and (q["upper"], q["lower"]) == (127, 0) else None

    pages = lib["pages"]
    the_map, find = pages[0], pages[1]
    ok = all(turn(e) == (15, 12 + i) and push(e) == (15, 44 + i) for i, e in enumerate(the_map["encoders"]))
    ok = ok and [e["name"] for e in find["encoders"][:4]] == ["RECENT", "FITS NOW", "MORE", "LESS"]
    ok = ok and all(turn(e) == (14, 12 + i) and push(e) == (14, 44 + i) for i, e in enumerate(find["encoders"][:4]))
    ok = ok and all(turn(e) is None and e["push_action"]["type"] == 0 for e in find["encoders"][4:12] + [find["encoders"][14]])
    ok = ok and turn(find["encoders"][15]) == (16, 30) and find["encoders"][15]["name"] == "GLIDE" and find["encoders"][15]["push_action"]["type"] == 0
    check(ok, "MAP on channel 15 and FIND on 14 (knob p on CC 11 + p, its press on CC 43 + p), GLIDE as FIND's knob 16")
    ok = True
    for sl in range(3):
        pg = pages[2 + sl]
        ok = ok and pg["channel"] == 13 and all(turn(e) == (13, 8 + sl * 8 + i) and push(e) == (13, 40 + sl * 8 + i) for i, e in enumerate(pg["encoders"][:8]))
        ok = ok and all(turn(e) is None and e["push_action"]["type"] == 0 for e in pg["encoders"][8:12])
    ok = ok and [turn(e) for e in pages[5]["encoders"][:8]] == [(16, 60 + i) for i in range(8)] and all(e["push_action"]["type"] == 0 for e in pages[5]["encoders"][:12])
    ok = ok and [turn(e) for e in pages[6]["encoders"][:4]] == [(16, 68), (16, 69), (16, 70), None] and [e["name"] for e in pages[6]["encoders"][:3]] == ["TAPE", "COMP", "LIMIT"]
    ok = ok and [turn(e) for e in pages[7]["encoders"][:6]] == [(13, 70), (13, 71), (13, 72), (13, 73), (13, 74), None] and pages[7]["encoders"][0]["name"] == "LENGTH"
    for c in range(3):
        pg = pages[8 + c]
        ok = ok and all(turn(e) == (13, (76 + n if n < 20 else 102 + n - 20)) and e["push_action"]["type"] == 0 for n, e in enumerate(pg["encoders"][:12], c * 12))
        ok = ok and [push(e) for e in pg["encoders"][14:]] == [(16, 35), (16, 36)] and all(turn(e) is None for e in pg["encoders"][13:])
    check(ok, "INS 1-3: an insert's eight places on channel 13 (CC 8 + 8 for each slot; pressed, CC 40 + ...); RETURNS and MASTER at the addresses a sequencer has (channel 16, CC 60-70); "
              "SEQ: LENGTH and the generator's four (channel 13, CC 70-74); CFG 1-3: CONFIG's controls, twelve to a page (CC 76-95, then 102-117), with PAGE - and PAGE + on 15 and 16")
    ok = all(turn(pg["encoders"][12]) == (16, 31) and push(pg["encoders"][12]) == (16, 33) and pg["encoders"][12]["name"] == "SELECT"
             and turn(pg["encoders"][13]) is None and push(pg["encoders"][13]) == (16, 34) for pg in pages[1:11])
    check(ok, "on pages 2 to 11, knob 13 is the pointer's wheel (CC 31 on channel 16; pressed, CC 33) and knob 14 pressed is OPEN (CC 34)")
    # page 12: the snapshot knobs, the page the first scene's twelfth is, object for object
    check(pages[11] == scene["pages"][11] and all(turn(e) == (16, 12 + i) for i, e in enumerate(pages[11]["encoders"])),
          "page 12 is the snapshot knobs, exactly as the first scene's page 12 has them (channel 16: knob p on CC 11 + p, its press on CC 43 + p), so they are on the same page in either scene")
    used = {}
    for pg in pages[:11]:
        for e in pg["encoders"]:
            if turn(e) and turn(e) not in ((16, 31),):
                used[turn(e)] = used.get(turn(e), 0) + 1
            if push(e) and push(e)[0] == 13:
                used[push(e)] = used.get(push(e), 0) + 1
    mine = {(t["channel"], t["nr1"]) for p in scene["pages"] for e in p["encoders"] for t in e["turn_actions"] if t["type"] == 3}
    check(all(v == 1 for v in used.values()) and (set(used) & mine) == {(16, 30)} and not any(num in RESERVED or num in (0, 32) for _, num in used)
          and not any(ch == 12 for ch, _ in used) and lib["code"]["scriptName"] == scene["code"]["scriptName"],
          f"its first eleven pages' {len(used)} knob addresses are each one knob's, none is the first scene's but GLIDE's, and nothing is on channel 12 (where the page is turned from outside)")
    try:
        fx = E16(script=lib["code"]["code"])
        said = fx.sent()
        fx.goto(3)
        said += fx.sent()
        fx.sent_to(9)
        fx.update()
        said += fx.sent()
        fx.name_page(9, ["MODL", "ATK"] + ["    "] * 14)
        fx.move((13, 76, 100))
        fx.name_page(9, ["    "] * 16)
        named = fx.names()
        fx.name_page(9, ["MODL", "ATK"] + ["    "] * 14)
        got = fx.names()
        fx.sent()
        fx.goto(12)
        check(said == [(0, 15, 119, 21), (0, 15, 119, 23), (0, 15, 119, 29)] and named == {} and got == {1: "MODL", 2: "ATK"} and fx.rings() == {} and fx.sent() == [(0, 15, 119, 32)],
              "its script says each page as 20 and the page's number (21 the map, 23 INS 1, 29 CFG 1, 32 the snapshot knobs), takes names, and walks no ring (nothing on these pages travels)")
    except NameError:
        pass
else:
    check(False, f"{os.path.basename(lib_path)} is missing")

print("ALL CHECKS PASSED" if fails == 0 else f"{fails} CHECK(S) FAILED")
sys.exit(1 if fails else 0)
