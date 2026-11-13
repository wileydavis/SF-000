#!/usr/bin/env python3
"""The wear on the dark instrument's faceplates, made from a photograph of scratched black metal.

    python3 tools/wear.py photo.png assets/wear.png

The photograph is not kept in the repository: only what this makes of it. What it makes is a
grey sheet (white: scratched through) that repeats without a seam in both directions, so that
any plate, of any shape, can take any window of it, turned any of four ways. It is made several
times the photograph's size; each scratch is smoothed along its own length (which takes out the
photograph's stair-steps) and drawn up to its crest across it, so that it stays a clean hairline
when the eye comes close.
"""
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

OUT_W = 4096


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def seamless(m, axis, band):
    """Half a turn along one axis, and the two laid over each other: at the sheet's own ends
    only the turned one shows (it runs straight on there); its join is in the middle, where
    only the first shows."""
    n = m.shape[axis]
    turned = np.roll(m, n // 2, axis=axis)
    x = np.arange(n, dtype=np.float32)
    d = np.minimum(x, n - 1 - x) / n            # 0 at the ends, 0.5 in the middle
    w = smooth(band * 0.35, band, d)
    w = w[:, None] if axis == 0 else w[None, :]
    return np.maximum(m * w, turned * (1.0 - w))


def main():
    src, dst = sys.argv[1], sys.argv[2]
    g = np.asarray(Image.open(src).convert("L")).astype(np.float32) / 255.0

    # the plate's own black is taken away: what is left is what has been done to it
    lo = float(np.percentile(g, 50)) + 0.016
    hi = float(np.percentile(g, 99.93))
    m = np.clip((g - lo) / (hi - lo), 0.0, 1.0)

    # the photograph's own worn border is not a plate's (each plate wears its own edges)
    h, w = m.shape
    m = m[int(h * 0.015):int(h * 0.985), int(w * 0.02):int(w * 0.935)]

    m = seamless(m, 1, 0.16)
    m = seamless(m, 0, 0.16)

    # larger, so that a hairline has room to be drawn finer than the photograph had it
    h, w = m.shape
    out_h = int(round(OUT_W * h / w / 8.0)) * 8
    pad = 24
    big = np.pad(m, pad, mode="wrap")
    k = OUT_W / w
    im = Image.fromarray((big * 65535.0).astype(np.uint16).astype(np.int32), mode="I")
    im = im.resize((int(round(big.shape[1] * k)), int(round(big.shape[0] * k))), Image.LANCZOS)
    big = np.clip(np.asarray(im).astype(np.float32) / 65535.0, 0.0, 1.0)
    p = int(round(pad * k))
    big = big[p:p + out_h, p:p + OUT_W]
    if big.shape != (out_h, OUT_W):
        big = np.asarray(Image.fromarray(big, mode="F").resize((OUT_W, out_h), Image.BILINEAR))

    # which way each mark runs, and how surely it runs any way at all (a scratch does; a speck of dust does not)
    yy, xx = np.mgrid[0:out_h, 0:OUT_W].astype(np.float32)
    base = ndimage.gaussian_filter(big, k * 0.5, mode="wrap")
    gx = ndimage.sobel(base, axis=1, mode="wrap")
    gy = ndimage.sobel(base, axis=0, mode="wrap")
    jxx = ndimage.gaussian_filter(gx * gx, k * 2.2, mode="wrap")
    jyy = ndimage.gaussian_filter(gy * gy, k * 2.2, mode="wrap")
    jxy = ndimage.gaussian_filter(gx * gy, k * 2.2, mode="wrap")
    turn = 0.5 * np.arctan2(2.0 * jxy, jxx - jyy)                 # the way it rises: across the mark
    sure = np.sqrt((jxx - jyy) ** 2 + 4.0 * jxy ** 2) / (jxx + jyy + 1e-9)
    sure = smooth(0.30, 0.65, sure).astype(np.float32)
    ax, ay = np.cos(turn).astype(np.float32), np.sin(turn).astype(np.float32)     # across
    lx, ly = -ay, ax                                                                 # along
    del base, gx, gy, jxx, jyy, jxy, turn

    def at(img, dx, dy):
        return ndimage.map_coordinates(img, [yy + dy, xx + dx], order=1, mode="wrap")

    # smoothed along its own length: the photograph's stair-steps go, the scratch stays
    reach = k * 5.0
    acc = np.zeros_like(big)
    tot = 0.0
    for t in np.linspace(-reach, reach, 15):
        wt = float(np.exp(-2.0 * (t / reach) ** 2))
        acc += wt * at(big, lx * t, ly * t)
        tot += wt
    big = big + (acc / tot - big) * sure
    del acc

    # drawn up to its crest: across a scratch, what is beside the brightest of it gives way
    crest = big.copy()
    for t in np.linspace(-k * 2.4, k * 2.4, 11):
        crest = np.maximum(crest, at(big, ax * t, ay * t))
    line = smooth(0.80, 0.975, big / (crest + 1e-4))
    r = max(3, int(round(k * 1.6)) | 1)
    dot = smooth(0.55, 0.92, big / (ndimage.maximum_filter(big, size=r, mode="wrap") + 1e-4))
    fine = big * (dot + (line - dot) * sure)
    fine = ndimage.gaussian_filter(fine, 0.6, mode="wrap")
    fine = np.clip(fine * 1.5, 0.0, 1.0)

    Image.fromarray((fine * 255.0 + 0.5).astype(np.uint8), mode="L").save(dst, optimize=True)
    print(dst, OUT_W, out_h, "mean", round(float(fine.mean()), 4))


if __name__ == "__main__":
    main()
