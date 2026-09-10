#!/usr/bin/env python3
"""Draw each cursor-probe point onto the capture it was measured from.

A headless capture cannot photograph a cursor — there is no pointer on screen —
so a bare screenshot beside a cursor claim is decoration. Marking the exact
pixel each claim refers to, with the cursor resolved there, makes the picture
carry the claim: a reader can see that "grab over the movable viewport" names a
point that really is on the viewport strip, and not somewhere else.

The probe reports points in root/design space; the capture is in device pixels.
The scale is derived from the probe's own recorded viewport, and a capture whose
aspect disagrees with it is refused rather than annotated at the wrong offsets.
"""

from __future__ import annotations

import argparse
import json
import sys

from PIL import Image, ImageDraw, ImageFont

COLORS = {
    "crosshair": (120, 220, 255),
    "grab": (140, 255, 170),
    "grabbing": (255, 210, 120),
    "horizontal-resize": (255, 150, 210),
    "pointer": (200, 180, 255),
    "default": (170, 170, 170),
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("probe")
    ap.add_argument("capture")
    ap.add_argument("out")
    ap.add_argument("--title", default="")
    args = ap.parse_args()

    with open(args.probe, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    vp = doc["viewport"]
    img = Image.open(args.capture).convert("RGB")

    sx = img.width / float(vp["w"])
    sy = img.height / float(vp["h"])
    if abs(sx - sy) > 0.01:
        print(f"REFUSED: capture {img.width}x{img.height} does not share an "
              f"aspect with the probe viewport {vp['w']}x{vp['h']} "
              f"(sx={sx:.3f} sy={sy:.3f}); annotating it would place every "
              f"marker at the wrong pixel", file=sys.stderr)
        return 2

    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Menlo.ttc", int(15 * sx))
    except OSError:
        font = ImageFont.load_default()

    if args.title:
        d.rectangle([0, 0, img.width, int(34 * sy)], fill=(0, 0, 0))
        d.text((int(8 * sx), int(8 * sy)), args.title, fill=(255, 255, 255), font=font)

    r = int(11 * sx)
    placed: list[tuple[float, float, float, float]] = []
    for p in doc["probes"]:
        x, y = p["point"]["x"] * sx, p["point"]["y"] * sy
        cur = p["resolved_cursor"]
        col = COLORS.get(cur, (255, 80, 80))
        d.ellipse([x - r, y - r, x + r, y + r], outline=col, width=max(2, int(2 * sx)))
        d.line([x - r * 1.8, y, x + r * 1.8, y], fill=col, width=max(1, int(sx)))
        d.line([x, y - r * 1.8, x, y + r * 1.8], fill=col, width=max(1, int(sx)))
        label = f"{p['name']}  {cur} / {p['ns_cursor']}"
        box = d.textbbox((0, 0), label, font=font)
        lw, lh = box[2] - box[0], box[3] - box[1]
        lx = min(max(x - lw / 2, 4), img.width - lw - 4)
        ly = y + r * 2.2
        if ly + lh > img.height - 4:
            ly = y - r * 2.2 - lh
        # CUR-2 and CUR-3 are deliberately the SAME point (you can only grab
        # what you are already over), so their labels always collide. Stacking
        # rather than overprinting keeps both readable — an unreadable label is
        # the same as no label, and this image is the evidence.
        step = lh + int(8 * sy)
        while any(not (lx + lw < ox or ox + ow < lx or ly + lh < oy or oy + oh < ly)
                  for ox, oy, ow, oh in placed):
            ly -= step
            if ly < 0:
                ly = y + r * 2.2 + step * (len(placed) + 1)
                break
        placed.append((lx, ly, lw, lh))
        d.line([x, y + r, x + (lx + lw / 2 - x) * 0.0 + x - x, ly], fill=col, width=1)
        d.rectangle([lx - 4, ly - 3, lx + lw + 4, ly + lh + 5], fill=(0, 0, 0))
        d.text((lx, ly), label, fill=col, font=font)

    img.save(args.out)
    print(f"annotated {len(doc['probes'])} probe points onto {args.out} "
          f"(scale {sx:.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
