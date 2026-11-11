"""The OXI One MkII project file (.oxipro), as far as it is read. Copied unchanged from SF Live Suite
(controllers/oxi-one/oxipro.py), where the format notes and how each write was checked are kept.

Every field here was seen in projects a ONE saved, and the writes were checked against the ONE's own
files byte for byte (README.md, HOW SFSTART WAS CHECKED). Nothing else in a file is touched.
"""
import struct

BLK = 0x3000                      # the header, then 8 x 16 pattern slots, each this long
SEQS, SLOTS = 8, 16
SIZE = BLK * (1 + SEQS * SLOTS)

NAME, NAME_LEN = 0x04, 12         # the project's name
TEMPO = 0x10                      # u16, BPM x 10
SLOT_TABLE = 0x12                 # 128 entries of 16 bytes: in use, colour, name (14 bytes)
HEADER_CRC = 0x2A40               # CRC32 of bytes 0 .. 0x2A3F

MONO, POLY, CHORD, MULTI, EMPTY = 0, 1, 2, 3, 0xFF
MODES = {MONO: "MONO", POLY: "POLY", CHORD: "CHORD", MULTI: "MULTITRACK"}
OUTS = {0: "ALL", 2: "USB"}
VELOCITY = 0x4B                   # the ONE's default velocity (75)

# Mono: 128 step records of 24 bytes from +4 (byte 0 the note: FF none, 0 the root; byte 1 the
# velocity); the channel (0-31: 1A .. 16B), Out, and the Program Change (FF off, else PC - 1).
M_STEP, M_CH, M_OUT, M_PC = 24, 0x0CBD, 0x0CBE, 0x0CC6
# Multitrack: 128 step records of 81 bytes from +4 (byte 0 the tracks that play, bit i for track
# index i; the velocities at +9 + i). Track index 0 is the ONE's track 8, the bottom row; index 7
# is track 1. The tracks' channels: one byte each, 0 off, 1-32 for 1A .. 16B. Out.
T_STEP, T_VEL, T_CH, T_OUT = 81, 9, 0x288C, 0x2A31
STEPS = 128


def crc32(buf):
    """OXI's CRC32 (OXI-Desktop's crc32.h): poly 0x04C11DB7, init 0xFFFFFFFF, over little-endian
    32-bit words, MSB first, no final XOR."""
    c = 0xFFFFFFFF
    for (w,) in struct.iter_unpack("<I", bytes(buf)):
        c ^= w
        for _ in range(32):
            c = ((c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if c & 0x80000000 else (c << 1) & 0xFFFFFFFF
    return c


def channel_name(v):
    """A Mono channel byte (0-based)."""
    return f"{v % 16 + 1}{'ABCDEF'[v // 16]}" if v < 96 else f"?{v:#04x}"


def track_channel_name(v):
    """A multitrack track's channel byte (1-based, 0 off)."""
    return "off" if v == 0 else channel_name(v - 1)


class Project:
    def __init__(self, data):
        if len(data) != SIZE:
            raise ValueError(f"not an OXI One MkII project: {len(data)} bytes, not {SIZE}")
        self.d = bytearray(data)

    # ---- places
    def at(self, seq, slot):
        """Where sequencer seq's pattern slot (both from 1) starts."""
        return BLK * (1 + SLOTS * (seq - 1) + (slot - 1))

    def entry(self, seq, slot):
        return SLOT_TABLE + 16 * (SLOTS * (seq - 1) + (slot - 1))

    def kind(self, seq, slot):
        return self.d[self.at(seq, slot)]

    def crc_at(self, seq, slot):
        """A pattern's CRC sits after its length (u16 at +2), rounded up to 4."""
        return (struct.unpack_from("<H", self.d, self.at(seq, slot) + 2)[0] + 3) & ~3

    # ---- reading
    def name(self):
        return self.d[NAME:NAME + NAME_LEN].rstrip(b"\0").decode("latin-1")

    def tempo(self):
        return struct.unpack_from("<H", self.d, TEMPO)[0] / 10

    def in_use(self, seq, slot):
        return self.d[self.entry(seq, slot)] == 1

    def slot_name(self, seq, slot):
        e = self.entry(seq, slot)
        return self.d[e + 2:e + 16].rstrip(b"\0").decode("latin-1")

    def field(self, seq, slot, off):
        return self.d[self.at(seq, slot) + off]

    def out(self, seq, slot):
        return self.field(seq, slot, M_OUT if self.kind(seq, slot) == MONO else T_OUT)

    def program(self, seq, slot):
        """A Mono pattern's Program Change as the ONE shows it (1-128), or None for OFF."""
        v = self.field(seq, slot, M_PC)
        return None if v == 0xFF else v + 1

    def tracks(self, seq, slot):
        """A multitrack pattern's track channel bytes, index 0 (track 8, the bottom row) first."""
        a = self.at(seq, slot) + T_CH
        return list(self.d[a:a + 8])

    def notes(self, seq, slot):
        """Mono: {step: (note, velocity)}. Multitrack: {track index: [steps]}. Steps from 1."""
        a = self.at(seq, slot)
        if self.kind(seq, slot) == MONO:
            return {k + 1: (self.d[a + 4 + M_STEP * k], self.d[a + 5 + M_STEP * k])
                    for k in range(STEPS) if self.d[a + 4 + M_STEP * k] != 0xFF}
        out = {}
        for k in range(STEPS):
            m = self.d[a + 4 + T_STEP * k]
            for i in range(8):
                if m >> i & 1:
                    out.setdefault(i, []).append(k + 1)
        return out

    def bad_crcs(self):
        bad = [] if crc32(self.d[:HEADER_CRC]) == struct.unpack_from("<I", self.d, HEADER_CRC)[0] else ["header"]
        for s in range(1, SEQS + 1):
            for p in range(1, SLOTS + 1):
                if self.kind(s, p) != EMPTY:
                    a, end = self.at(s, p), self.crc_at(s, p)
                    if crc32(self.d[a:a + end]) != struct.unpack_from("<I", self.d, a + end)[0]:
                        bad.append(f"sequencer {s} slot {p}")
        return bad

    # ---- writing (only what was checked against the ONE's own saves)
    def copy_slot(self, seq, src, dst):
        self.d[self.at(seq, dst):self.at(seq, dst) + BLK] = self.d[self.at(seq, src):self.at(seq, src) + BLK]

    def clear(self, seq, slot):
        """No notes, as the ONE writes an empty pattern: every step empty at the default velocity."""
        a = self.at(seq, slot)
        if self.kind(seq, slot) == MONO:
            for k in range(STEPS):
                self.d[a + 4 + M_STEP * k:a + 6 + M_STEP * k] = bytes([0xFF, VELOCITY])
        elif self.kind(seq, slot) == MULTI:
            for k in range(STEPS):
                o = a + 4 + T_STEP * k
                self.d[o] = 0
                self.d[o + T_VEL:o + T_VEL + 8] = bytes([VELOCITY] * 8)
        else:
            raise ValueError(f"sequencer {seq} slot {slot}: mode {self.kind(seq, slot)} is not read here")

    def set_note(self, seq, slot, step, note=0, velocity=VELOCITY):
        o = self.at(seq, slot) + 4 + M_STEP * (step - 1)
        self.d[o:o + 2] = bytes([note, velocity])

    def set_trigger(self, seq, slot, step, index, velocity=VELOCITY):
        o = self.at(seq, slot) + 4 + T_STEP * (step - 1)
        self.d[o] |= 1 << index
        self.d[o + T_VEL + index] = velocity

    def set_program(self, seq, slot, pc):
        self.d[self.at(seq, slot) + M_PC] = 0xFF if pc is None else pc - 1

    # The channels: read in every save, where the ONE showed them; written here with no save of the
    # ONE's to compare yet (README.md, HOW SFSTART WAS CHECKED). One byte each, in the pattern.
    def set_channel(self, seq, slot, ch):
        """A Mono pattern's channel, 0-based: 0-15 port A ch 1-16, 16-31 port B."""
        assert self.kind(seq, slot) == MONO and 0 <= ch < 32, (seq, slot, ch)
        self.d[self.at(seq, slot) + M_CH] = ch

    def set_track_channels(self, seq, slot, chans):
        """A multitrack pattern's tracks' channels, index 0 (track 8, the bottom row) first: 0 off,
        1-32 for 1A .. 16B."""
        assert self.kind(seq, slot) == MULTI and len(chans) == 8 and all(0 <= c <= 32 for c in chans), (seq, slot, chans)
        a = self.at(seq, slot) + T_CH
        self.d[a:a + 8] = bytes(chans)

    def empty_slot(self, seq, slot):
        a = self.at(seq, slot)
        self.d[a:a + BLK] = bytes([EMPTY]) + bytes(BLK - 1)
        self.d[self.entry(seq, slot)] = 0

    def set_slot(self, seq, slot, name, colour_from=1):
        e = self.entry(seq, slot)
        self.d[e] = 1
        self.d[e + 1] = self.d[self.entry(seq, colour_from) + 1]
        self.d[e + 2:e + 16] = name.encode("ascii")[:14].ljust(14, b"\0")

    def set_name(self, name):
        self.d[NAME:NAME + NAME_LEN] = name.encode("ascii")[:NAME_LEN].ljust(NAME_LEN, b"\0")

    def finish(self):
        """Every CRC again: each pattern in use, then the header."""
        for s in range(1, SEQS + 1):
            for p in range(1, SLOTS + 1):
                if self.kind(s, p) != EMPTY:
                    a, end = self.at(s, p), self.crc_at(s, p)
                    struct.pack_into("<I", self.d, a + end, crc32(self.d[a:a + end]))
        struct.pack_into("<I", self.d, HEADER_CRC, crc32(self.d[:HEADER_CRC]))
        return bytes(self.d)
