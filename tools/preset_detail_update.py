#!/usr/bin/env python3
"""PRE-5: selecting a preset must update the detail pane's name AND artwork.

Measured on pixels. The materialized runtime does not expose its rendered text
through DOM text nodes -- a probe for the string "SELECT A PATTERN" reads false
on a capture that visibly contains it -- so a textContent-based check is a
broken instrument here, not a strict one, and its negatives mean nothing.

Two captures of the open Preset Manager with DIFFERENT rows selected:
  control  an invariant strip (the manager title bar) MUST be unchanged, or the
           two captures are not the same aligned surface and the run proves
           nothing (exit 3, not a pass)
  assert   the detail pane MUST differ -- a new name and new artwork

The planted negative compares a capture against itself, standing in for a
selection that changed nothing. A run whose plant goes green is broken.
"""
import argparse, sys
from PIL import Image, ImageChops

# Logical (design-space) boxes, scaled by --scale at read time.
TITLE_BOX  = (270, 180, 1040, 213)   # "PRESET MANAGER - N user . N factory"
DETAIL_BOX = (600, 220, 1040, 533)   # detail pane: name badge + artwork


def crop(im, box, scale):
    x0, y0, x1, y1 = box
    return im.crop((int(x0 * scale), int(y0 * scale),
                    int(x1 * scale), int(y1 * scale))).convert("RGB")


def diff_stats(a, b, threshold):
    if a.size != b.size:
        return 100.0, 255
    d = ImageChops.difference(a, b).convert("L")
    px = d.getdata()
    n = len(px)
    return 100.0 * sum(1 for v in px if v > threshold) / n, max(px)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("before", help="capture with preset A selected")
    ap.add_argument("after", help="capture with preset B selected")
    ap.add_argument("--scale", type=float, default=1.5,
                    help="PNG px per design px (1980/1320 = 1.5)")
    ap.add_argument("--threshold", type=int, default=12,
                    help="per-channel delta counted as a changed pixel")
    ap.add_argument("--min-detail-change", type=float, default=2.0,
                    help="percent of detail-pane pixels that must change")
    ap.add_argument("--max-control-change", type=float, default=0.5,
                    help="percent of title-strip pixels allowed to change")
    ap.add_argument("--plant", action="store_true",
                    help="compare BEFORE against itself; must go RED")
    a = ap.parse_args()

    before = Image.open(a.before)
    after = Image.open(a.before if a.plant else a.after)

    ctl, ctl_max = diff_stats(crop(before, TITLE_BOX, a.scale),
                              crop(after, TITLE_BOX, a.scale), a.threshold)
    det, det_max = diff_stats(crop(before, DETAIL_BOX, a.scale),
                              crop(after, DETAIL_BOX, a.scale), a.threshold)

    print(f"control title strip : {ctl:6.2f}% changed (max delta {ctl_max})")
    print(f"detail pane         : {det:6.2f}% changed (max delta {det_max})")

    if not a.plant and ctl > a.max_control_change:
        print("PREMISE UNMET  the title strip moved: these are not the same "
              "aligned surface, so the detail diff proves nothing")
        return 3

    if det >= a.min_detail_change:
        print("GREEN  detail pane updated: name and artwork both changed")
        return 0
    print("RED    detail pane did not update on selection")
    return 1


if __name__ == "__main__":
    sys.exit(main())
