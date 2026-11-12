#!/usr/bin/env python3
"""The app's icon, from the instrument's own picture of a 4 x 4 module.

    build/.../SF-000 --icon /tmp/icon_raw.png      (on Linux: under xvfb-run, as the 3D checks)
    python3 tools/icon.py /tmp/icon_raw.png assets/icon.png

The raw picture is the module from straight above, as the 3D stage draws it. This cuts it to
the plate, gives it the rounded square a Mac icon has (824 of 1024, the corners a superellipse)
and a soft shadow under it.
"""
import sys
from PIL import Image, ImageDraw, ImageFilter

raw, out = sys.argv[1], sys.argv[2]
src = Image.open(raw).convert("RGB")
W = src.size[0]
# the plate: where the picture stops being the case's colour, from each side along the middle
case = src.getpixel((4, W // 2))
def edge(points):
    for p in points:
        c = src.getpixel(p)
        if max(abs(c[i] - case[i]) for i in range(3)) > 18:
            return p
    return points[-1]
left = edge([(x, W // 2 + 40) for x in range(W)])[0]
right = edge([(x, W // 2 + 40) for x in range(W - 1, 0, -1)])[0]
top = edge([(W // 2 + 40, y) for y in range(W)])[1]
bottom = edge([(W // 2 + 40, y) for y in range(W - 1, 0, -1)])[1]
inset = 10                                  # inside the plate's own edge and corner
plate = src.crop((left + inset, top + inset, right - inset, bottom - inset))
S, BODY = 1024, 824
body = plate.resize((BODY * 2, BODY * 2), Image.LANCZOS)
# the shape: |x|^n + |y|^n = 1, n = 5
n, M = 5.0, BODY * 2
mask = Image.new("L", (M, M), 0)
px = mask.load()
for y in range(M):
    v = abs((y + 0.5) / M * 2 - 1) ** n
    if v >= 1.0:
        continue
    half = (1.0 - v) ** (1.0 / n)
    x0, x1 = int(round((1 - half) * M / 2)), int(round((1 + half) * M / 2))
    for x in range(x0, x1):
        px[x, y] = 255
mask = mask.resize((BODY, BODY), Image.LANCZOS)
body = body.resize((BODY, BODY), Image.LANCZOS)
icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))
shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
off = (S - BODY) // 2
shadow.paste((0, 0, 0, 110), (off, off + 10), mask)
icon = Image.alpha_composite(icon, shadow.filter(ImageFilter.GaussianBlur(14)))
layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
layer.paste(body.convert("RGBA"), (off, off), mask)
icon = Image.alpha_composite(icon, layer)
icon.save(out)
print("wrote", out, icon.size)
