"""Checks an OXI One MkII project (.oxipro) against SF-000: README.md, THE PROJECT. Reads, never writes.

    python3 controllers/oxi-one/check_project.py PROJECT.oxipro

Prints each sequencer as the ONE has it (mode, channel, Out, the slots in use and what is in them,
any Program Change) and what doesn't match the card. A part's channel is its lane's number in SF-000
(1 KICK ... 11 VOICE), the same number its E16 page has. SF-000 listens to every port and goes by
the channel alone, so a spare sequencer must not sit on a lane's channel on any port. Exit 1 if
anything doesn't match. (From SF Live Suite's check_project.py.)
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oxipro as X  # noqa: E402

LANES = ["KICK", "CLOSED HAT", "OPEN HAT", "CLAP", "PERC", "BASS", "RUMBLE", "LEAD", "CHORD", "PAD", "VOICE"]
CHANNEL = {name: i + 1 for i, name in enumerate(LANES)}            # a lane's number is its channel
# The sequencers that play one lane each. Mono channels are stored 0-based (0 is 1A, 16 is 1B).
MONO_PARTS = {s: (part, CHANNEL[part] - 1) for s, part in ((2, "BASS"), (3, "LEAD"), (4, "CHORD"), (5, "PAD"), (6, "VOICE"))}
SPARE = (7, 8)
# Where the SF-000 project puts the spare sequencers: 12A and 13A, channels SF-000 takes no notes on.
SPARE_CHANNELS = {7: 12 - 1, 8: 13 - 1}
# Drum tracks (track channels are stored 1-based: 1 is 1A).
DRUM_PARTS = {CHANNEL[part]: part for part in ("KICK", "CLOSED HAT", "OPEN HAT", "CLAP", "PERC")}
NEEDED_DRUMS = ("KICK", "CLOSED HAT", "OPEN HAT", "CLAP")
# Sequencer 1's tracks, index 0 (the ONE's track 8, the bottom row) first: the kick on the bottom row.
DRUM_ROWS = ["KICK", "PERC", "CLAP", "OPEN HAT", "CLOSED HAT", None, None, None]


def drum_index(p, part, slot=1):
    """The track index (0 is the ONE's track 8) whose channel feeds `part`, or None."""
    for i, v in enumerate(p.tracks(1, slot)):
        if DRUM_PARTS.get(v) == part:
            return i
    return None


def read(p):
    """(lines, problems): the project as the ONE has it, and what doesn't match the card."""
    lines, problems = [], []
    bad = p.bad_crcs()
    lines.append(f"{p.name()}: tempo {p.tempo():g} (used only when the ONE runs on its own clock); "
                 + ("every checksum verifies" if not bad else "CHECKSUMS FAIL: " + ", ".join(bad)))
    problems += [f"checksum fails: {b}" for b in bad]
    for s in range(1, X.SEQS + 1):
        part = "DRUMS" if s == 1 else "SPARE" if s in SPARE else MONO_PARTS[s][0]
        slots = [q for q in range(1, X.SLOTS + 1) if p.kind(s, q) != X.EMPTY]
        if not slots:
            if s not in SPARE:
                problems.append(f"sequencer {s} ({part}): no pattern at all")
            continue
        kind = p.kind(s, 1)
        ch = X.channel_name(p.field(s, 1, X.M_CH)) if kind == X.MONO else "-"
        lines.append(f"{s}  {part:<8} {X.MODES.get(kind, kind):<10} {ch:<4} Out {X.OUTS.get(p.out(s, 1), p.out(s, 1))}")
        for q in slots:
            k = p.kind(s, q)
            if k != kind:
                problems.append(f"sequencer {s} slot {q}: {X.MODES.get(k, k)}, slot 1 is {X.MODES.get(kind, kind)}")
                continue
            n = p.notes(s, q)
            what = (f"{sum(len(v) for v in n.values())} hits" if k == X.MULTI else f"{len(n)} note" + "s" * (len(n) != 1)) if n else "empty"
            pc = f", PC {p.program(s, q)} (SF-000 ignores it)" if k == X.MONO and p.program(s, q) is not None else ""
            lines.append(f"     slot {q} {p.slot_name(s, q)!r}: {what}{pc}")
            if X.OUTS.get(p.out(s, q)) != "USB" and s not in SPARE:
                problems.append(f"sequencer {s} slot {q}: Out {X.OUTS.get(p.out(s, q), p.out(s, q))}, not USB")
        if s == 1:
            if kind != X.MULTI:
                problems.append(f"sequencer 1: {X.MODES.get(kind, kind)}, not MULTITRACK")
                continue
            names = [f"{8 - i} {DRUM_PARTS.get(v, '?')} {X.track_channel_name(v)}" if v else f"{8 - i} off"
                     for i, v in enumerate(p.tracks(1, 1))]
            lines.append("     tracks, bottom row up: " + ", ".join(names))
            for q in slots:
                if p.tracks(1, q) != p.tracks(1, 1):
                    problems.append(f"sequencer 1 slot {q}: its tracks' channels differ from slot 1's")
            for need in NEEDED_DRUMS:
                if drum_index(p, need) is None:
                    problems.append(f"sequencer 1: no track on {need}'s channel")
            for i, v in enumerate(p.tracks(1, 1)):
                if v and v not in DRUM_PARTS:
                    problems.append(f"sequencer 1 track {8 - i}: {X.track_channel_name(v)}, a channel no drum lane listens to")
        elif s in SPARE:
            for q in slots:
                if p.kind(s, q) == X.MONO and p.field(s, q, X.M_CH) % 16 + 1 <= len(LANES):
                    v = p.field(s, q, X.M_CH)
                    problems.append(f"sequencer {s} slot {q} (spare): on {X.channel_name(v)}, and SF-000 plays {LANES[v % 16]} "
                                    f"from channel {v % 16 + 1} on any port")
        else:
            want = MONO_PARTS[s][1]
            if kind != X.MONO:
                problems.append(f"sequencer {s} ({part}): {X.MODES.get(kind, kind)}, not MONO")
            elif p.field(s, 1, X.M_CH) != want:
                problems.append(f"sequencer {s} ({part}): channel {ch}, not {X.channel_name(want)}")
            for q in slots:
                if p.kind(s, q) == X.MONO and p.field(s, q, X.M_CH) != p.field(s, 1, X.M_CH):
                    problems.append(f"sequencer {s} slot {q}: channel differs from slot 1's")
    return lines, problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("project")
    a = ap.parse_args()
    with open(a.project, "rb") as f:
        p = X.Project(f.read())
    lines, problems = read(p)
    print("\n".join(lines))
    if problems:
        print("\nDOESN'T MATCH THE CARD:")
        print("\n".join(f"  {x}" for x in problems))
        sys.exit(1)
    print("\nMatches the card.")


if __name__ == "__main__":
    main()
