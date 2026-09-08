#!/usr/bin/env python3
"""SET-2: a fixed header must not move while the content scrolls beneath it.

Measured on pixels, deliberately. The layout snapshot this project's other
detectors read is SCROLL-BLIND -- scrolling the settings body 715px leaves every
rect in the tree byte-identical -- so a tree-based check cannot see this
property at all and a green one would be measuring nothing.

Two captures of the same surface at different scroll offsets:
  premise  the content band MUST differ, or no scroll happened and the run
           proves nothing (exit 3, not a pass)
  assert   the header band must be unchanged

The planted negative shifts the second header crop, standing in for a header
that scrolled with the content. A run whose plant stays green is broken.
"""
import argparse, sys
from PIL import Image, ImageChops


def band(im, scale, x0, y0, x1, y1, dx=0):
    """Crop a logical-coordinate band, optionally displaced by dx logical px."""
    s = scale
    return im.crop((int((x0 + dx) * s), int(y0 * s),
                    int((x1 + dx) * s), int(y1 * s))).convert("RGB")


def diff_stats(a, b):
    """(fraction of differing pixels, max per-channel delta)."""
    if a.size != b.size:
        return 1.0, 255
    d = ImageChops.difference(a, b)
    bbox = d.getbbox()
    if bbox is None:
        return 0.0, 0
    # Per-channel extrema, then a count of non-black pixels in the grayscale max.
    mx = max(hi for _, hi in d.getextrema())
    flat = d.convert("L")
    hist = flat.histogram()
    total = a.size[0] * a.size[1]
    nonzero = total - hist[0]
    return nonzero / float(total), mx


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("unscrolled")
    ap.add_argument("scrolled")
    ap.add_argument("--scale", type=float, default=2.0,
                    help="PNG pixels per logical px")
    ap.add_argument("--header", default="400,90,920,205",
                    help="logical x0,y0,x1,y1 of the fixed header band")
    ap.add_argument("--content", default="400,215,920,620",
                    help="logical x0,y0,x1,y1 of the scrolling content band")
    ap.add_argument("--tolerance", type=float, default=0.001,
                    help="max fraction of header pixels allowed to differ")
    ap.add_argument("--plant", type=float, default=0.0, metavar="PX",
                    help="displace the second header crop by PX logical px")
    a = ap.parse_args()

    hx = [float(v) for v in a.header.split(",")]
    cx = [float(v) for v in a.content.split(",")]
    im1, im2 = Image.open(a.unscrolled), Image.open(a.scrolled)

    c1 = band(im1, a.scale, *cx)
    c2 = band(im2, a.scale, *cx)
    cfrac, cmax = diff_stats(c1, c2)
    print(f"content band {cx}  differs in {cfrac*100:.2f}% of pixels "
          f"(max delta {cmax})")
    if cfrac <= 0.005:
        print("PREMISE UNPROVEN: the content band did not move, so no scroll "
              "occurred here and nothing can be concluded about the header.")
        return 3

    h1 = band(im1, a.scale, *hx)
    h2 = band(im2, a.scale, *hx, dx=a.plant)
    if a.plant:
        print(f"CONTROL: planted header motion of {a.plant:g} logical px")
    hfrac, hmax = diff_stats(h1, h2)
    print(f"header  band {hx}  differs in {hfrac*100:.2f}% of pixels "
          f"(max delta {hmax})  tolerance={a.tolerance*100:.2f}%")

    red = hfrac > a.tolerance
    if a.plant and not red:
        print("BROKEN: the planted header motion did not redden the detector")
        return 4
    if red:
        print(f"RED    the header moved with the content "
              f"({hfrac*100:.2f}% of its pixels changed)")
        return 1
    print("GREEN  the header held still while the content scrolled beneath it")
    return 0


sys.exit(main())
