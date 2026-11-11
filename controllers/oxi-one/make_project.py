"""Builds SF000.oxipro, the OXI One MkII project for SF-000, from SF Live Suite's startup project.

    python3 controllers/oxi-one/make_project.py --base SFSTART.oxipro [--out SF000.oxipro]

The base is SF START (SF Live Suite, controllers/oxi-one/SFSTART.oxipro), or any project made from it:
its sequencers' modes, channels, Out and four scenes are already what SF-000 wants, because a lane's
number in SF-000 is the Live track's number it had in the suite. This script changes two things and
leaves every other byte as it was:

  - the project's name (a write checked against the ONE's own saves in the suite);
  - the spare sequencers 7 and 8, from 7B and 16A to 12A and 13A. SF-000 listens to every port and
    goes by the channel alone, so 7B would have played RUMBLE. (The channel byte is the one the ONE
    showed in every save; as in the suite, no save of the ONE's own has yet been compared with a
    write to it.)

Then every checksum is written again.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oxipro as X  # noqa: E402
import check_project as C  # noqa: E402

NAME = "SF000"


def build(base):
    p = X.Project(base)
    for s, ch in C.SPARE_CHANNELS.items():
        for q in range(1, X.SLOTS + 1):
            if p.kind(s, q) == X.MONO:
                p.set_channel(s, q, ch)
    p.set_name(NAME)
    return p.finish()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base", required=True, help="SF START, or a project made from it")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), NAME + ".oxipro"))
    a = ap.parse_args()
    with open(a.base, "rb") as f:
        base = f.read()
    data = build(base)
    lines, problems = C.read(X.Project(data))
    if problems:
        raise SystemExit("The result doesn't check:\n  " + "\n  ".join(problems))
    changed = [i for i in range(len(data)) if data[i] != base[i]]
    with open(a.out, "wb") as f:
        f.write(data)
    print("\n".join(lines))
    print(f"\n{len(changed)} bytes differ from the base")
    print(f"-> {a.out}")


if __name__ == "__main__":
    main()
