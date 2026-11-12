#!/usr/bin/env python3
"""The app's icon: the instrument's own tag. White condensed letters on the tag's black, nothing else.

    python3 tools/icon.py assets/icon.png [one|two]

"two" (the default) stacks SF over 000, which still reads at the size of a Dock or a list;
"one" is the tag as it is on the case, SF-000 on a single line. The shape is a Mac icon's:
a rounded square 824 of 1024 across, its corners a superellipse, on nothing.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1]
lines = ["SF-000"] if len(sys.argv) > 2 and sys.argv[2] == "one" else ["SF", "000"]
here = os.path.dirname(os.path.abspath(__file__))
font_file = os.path.join(here, "..", "assets", "fonts", "BarlowCondensed-SemiBold.ttf")
BLACK, WHITE, EDGE = (0x2B, 0x2B, 0x28), (0xE8, 0xE5, 0xE1), (0x4D, 0x51, 0x4E)     # the instrument's own
S, BODY, K = 1024, 824, 4                   # drawn K times the size and brought down, for clean edges

def squircle(size, inset=0.0, n=5.0):
    """A filled superellipse |x|^n + |y|^n = 1, inset from the square's edge by that many pixels."""
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    r = size / 2.0 - inset
    for y in range(size):
        v = abs((y + 0.5 - size / 2.0) / r) ** n
        if v < 1.0:
            half = r * (1.0 - v) ** (1.0 / n)
            d.line([(size / 2.0 - half, y), (size / 2.0 + half, y)], fill=255)
    return m

B = BODY * K
body = Image.new("RGB", (B, B), BLACK)
# the tag's edge: one thin line just inside, as a stamped plate has
edge = Image.new("RGB", (B, B), EDGE)
ring = Image.composite(edge, body, squircle(B, 26 * K))
body = Image.composite(body, ring, squircle(B, 32 * K))
# the letters, as wide as the plate allows, the lines to a common left edge
d = ImageDraw.Draw(body)
want = B * (0.74 if len(lines) == 1 else 0.55)
size = 100
while True:
    f = ImageFont.truetype(font_file, size + 8)
    if max(d.textbbox((0, 0), t, font=f)[2] - d.textbbox((0, 0), t, font=f)[0] for t in lines) > want:
        break
    size += 8
f = ImageFont.truetype(font_file, size)
boxes = [d.textbbox((0, 0), t, font=f) for t in lines]
cap = d.textbbox((0, 0), "0", font=f)
cap_h = cap[3] - cap[1]
gap = cap_h * 0.24
total = cap_h * len(lines) + gap * (len(lines) - 1)
width = max(b[2] - b[0] for b in boxes)
x0 = (B - width) / 2.0
y = (B - total) / 2.0
for t, b in zip(lines, boxes):
    d.text((x0 - b[0], y - cap[1]), t, font=f, fill=WHITE)
    y += cap_h + gap
body = body.resize((BODY, BODY), Image.LANCZOS)
mask = squircle(BODY * K).resize((BODY, BODY), Image.LANCZOS)
icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))
icon.paste(body, ((S - BODY) // 2, (S - BODY) // 2), mask)
icon.save(out)
print("wrote", out, icon.size)
