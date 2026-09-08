#!/usr/bin/env python3
"""Adjudicate CLIP candidates against painted pixels.

`appearance_invariants.py --only clip` compares a string's *measured advance*
(`measured_text_boxes[].rect.w`) against the box it paints in. On this surface
that comparison is not trustworthy in either direction:

  * `__behavior_pr_y` "1.08kHz   11.0 dB   BAND 19/32" -- measured 225.0px in a
    210.0px box (CLIP reports a 15px overflow), but the painted ink spans
    186.7px, inset 12px on BOTH sides. Nothing is truncated.
  * `__behavior_pr_p` "32 bands" -- measured 68.0px, painted ink 90.7px. The
    measurement UNDER-reports by a third.

So the advance disagrees with the ink by -33%..+21% here, which is an order of
magnitude worse than the trailing-side-bearing effect that was originally
assumed (and recorded as "sub-pixel rounding"). No width tolerance can separate
a false 15px overflow from a true one. CLIP therefore produces *candidates*;
this tool adjudicates them against the render.

It is deliberately ASYMMETRIC, and that asymmetry is the whole point:

  EXONERATED  ink lies inside the node rect with >= --margin px clear on both
              sides. Nothing was cut. This verdict is sound.
  SPILL       ink crosses the node rect edge but stays inside the clip that
              bounds it, so it is DRAWN, not cut. Routine here: the measured
              advance disagrees with painted ink by -33%..+21%, so a rect that
              is tight around intact glyphs is the norm, not a defect.
  CANDIDATE   the node rect reaches past its clip AND ink runs to that clip
              edge -- the only geometry in which a glyph can actually be cut.
              Still not proof: ink legitimately ending at an edge reads the
              same as ink sliced by it. It means "look at the crop".
  NO_INK      the node measures text but paints none above the ink threshold.
              Measured causes on this surface, commonest first: a control in
              its DISABLED state (Spectr's snapshot-recall buttons paint
              "\u25b8 A" at luminance 55 on a 12 ground -- 1.64:1, which WCAG
              1.4.3 exempts as an inactive component); and genuine absence.
              Establish which before calling anything missing.

Nodes that lay out to nothing -- a closed dropdown's items, 182 of the 300 in
one snapshot -- are SKIPped. They correctly paint nothing.

Only the EXONERATED verdict is a conclusion. Never report a CANDIDATE as a
defect without looking at the crop.

Scale is derived from image width / snapshot viewport width and cross-checked
against height, so a snapshot paired with the wrong PNG exits INCONCLUSIVE
rather than silently measuring the wrong pixels.

Ink detection is polarity-agnostic (it measures deviation from the band's
background, not absolute luminance), so it works on light-on-dark and
dark-on-light alike.

exit 0 no candidates | 1 candidates remain | 2 usage
exit 3 inconclusive | 4 the planted control did not fire (tool is broken)
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import Optional

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover - environment guard
    print("ink_extents: Pillow is required (pip install Pillow)", file=sys.stderr)
    sys.exit(3)

DEVIATION = 60  # luminance units away from the band background that count as ink


def band_background(values: list[int]) -> int:
    return sorted(values)[len(values) // 10]


def ink_columns(px, x0: int, x1: int, y0: int, y1: int) -> list[int]:
    """Columns in [x0,x1) whose pixels deviate from the band background.

    Deviation is measured in both directions so light-on-dark and dark-on-light
    text are detected identically.
    """
    highs, lows = [], []
    for x in range(x0, x1):
        col = [px[x, y] for y in range(y0, y1)]
        highs.append(max(col))
        lows.append(min(col))
    if not highs:
        return []
    bg = band_background(highs)
    return [
        x0 + i
        for i in range(len(highs))
        if (highs[i] - bg) > DEVIATION or (bg - lows[i]) > DEVIATION
    ]


class Surface:
    def __init__(self, doc: dict, image: Image.Image, scale: float):
        self.doc = doc
        self.image = image
        self.scale = scale
        self.px = image.convert("L").load()
        self.w, self.h = image.size

    def measure(self, node: dict, band: dict, pad: float = 0.0) -> Optional[tuple[float, float]]:
        """Painted ink extents (design px) across `band`'s rows, padded in x.

        The row band comes from the measured TEXT box, not the node rect. A
        Label's rect is usually the whole control, so scanning its full height
        sweeps the button border in as "ink" and invents an overflow at the
        edge. The text box is the rows the glyphs actually occupy.
        """
        r = node["rect"]
        s = self.scale
        x0 = int((r["x"] - pad) * s)
        x1 = int((r["x"] + r["w"] + pad) * s)
        y0 = int(band["y"] * s)
        y1 = int((band["y"] + band["h"]) * s)
        x0, x1 = max(0, x0), min(self.w, x1)
        y0, y1 = max(0, y0), min(self.h, y1)
        if x1 - x0 < 4 or y1 - y0 < 2:
            return None
        cols = ink_columns(self.px, x0, x1, y0, y1)
        if not cols:
            return None
        return (min(cols) / s, (max(cols) + 1) / s)


def measurable(node: dict) -> Optional[dict]:
    """The single non-zero-width measured text box, if there is exactly one.

    Most labels here carry `w: 0.0` -- the multi-line sentinel that
    `Label::intrinsic_width()` returns. Those are not judged: the snapshot
    records no width to compare against.
    """
    if not node.get("visible"):
        return None
    boxes = [b for b in (node.get("measured_text_boxes") or []) if (b["rect"]["w"] or 0) > 0]
    return boxes[0] if len(boxes) == 1 else None


def classify(surface: Surface, node: dict, margin: float) -> tuple[str, str]:
    """Adjudicate one node's painted ink against the box AND the clip that bounds it.

    The distinction that matters: **only a clip can truncate.** A Label whose own
    rect is narrower than its glyphs still paints them in full when nothing clips
    it -- the rect is simply an under-measure, which is routine here because the
    measured advance under-reports ink by up to a third. Ink crossing the *clip*
    edge is the only thing that can actually cut a glyph.
    """
    r = node["rect"]
    clip = node["clipping"]["rect"]
    box = measurable(node)
    if box is None:
        return "SKIP", ""
    if clip["w"] <= 0 or clip["h"] <= 0 or r["w"] <= 0 or r["h"] <= 0:
        # Collapsed: closed dropdown items and the like lay out to nothing and
        # correctly paint nothing. Not a finding.
        return "SKIP", ""
    # Pad the scan past the box so ink that SPILLS is seen, not silently cropped
    # to the box and misread as a clean fit.
    ext = surface.measure(node, band=box["rect"], pad=margin + 4.0)
    if ext is None:
        return "NO_INK", (f"{node['id']} measures {box['rect']['w']:.1f}px of text "
                          f"but paints none above the ink threshold")
    lo, hi = ext
    left = lo - r["x"]
    right = (r["x"] + r["w"]) - hi
    detail = (
        f"{node['id']}({node.get('kind','?')}) {box['text']!r} "
        f"box={r['w']:.1f} measured={box['rect']['w']:.1f} ink={hi - lo:.1f} "
        f"clear=[{left:.1f},{right:.1f}]"
    )
    # One image pixel is the smallest thing that can be measured; anything under
    # it is quantization, not clearance.
    eps = 1.0 / surface.scale
    # A clip can only cut a glyph if the node's own rect reaches past it. A label
    # sitting wholly inside its clip is unreachable by that clip no matter how its
    # ink lands, so there is nothing for the pixel test to adjudicate.
    reaches_clip = (r["x"] < clip["x"] - eps
                    or r["x"] + r["w"] > clip["x"] + clip["w"] + eps)
    if reaches_clip and (lo <= clip["x"] + eps
                         or hi >= clip["x"] + clip["w"] - eps):
        return "CANDIDATE", detail + f" clip=[{clip['x']:.1f},{clip['x'] + clip['w']:.1f}]"
    if left >= margin and right >= margin:
        return "EXONERATED", detail
    return "SPILL", detail


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("snapshot")
    ap.add_argument("image")
    ap.add_argument("--node", help="only adjudicate node ids containing this substring")
    ap.add_argument("--margin", type=float, default=0.0,
                    help="design px of clear space required on both sides. Default 0: "
                         "ink must simply not cross the box edge. Note the "
                         "quantization floor -- at scale 1.5 one image pixel is "
                         "0.67 design px, so a clearance under ~0.7px is noise, "
                         "not a measurement.")
    ap.add_argument("--plant", action="store_true",
                    help="self-test: paint ink past an exonerated node's edge and "
                         "require this tool to notice")
    args = ap.parse_args()

    try:
        doc = json.load(open(args.snapshot))
        image = Image.open(args.image)
    except Exception as exc:
        print(f"ink_extents: {exc}", file=sys.stderr)
        return 2

    vp = doc.get("viewport") or {}
    if not vp.get("w") or not vp.get("h"):
        print("ink_extents: snapshot has no viewport; cannot derive scale", file=sys.stderr)
        return 3
    scale = image.size[0] / vp["w"]
    hscale = image.size[1] / vp["h"]
    if abs(scale - hscale) > 0.01:
        print(
            f"ink_extents: INCONCLUSIVE -- {args.image} is {image.size[0]}x{image.size[1]} "
            f"but the snapshot viewport is {vp['w']}x{vp['h']} "
            f"(x{scale:.3f} vs x{hscale:.3f}). Wrong PNG for this snapshot?",
            file=sys.stderr,
        )
        return 3

    surface = Surface(doc, image, scale)
    nodes = [n for n in doc.get("nodes", []) if measurable(n)]
    if args.node:
        nodes = [n for n in nodes if args.node in n.get("id", "")]
        if not nodes:
            print(f"ink_extents: no measurable node id contains {args.node!r}", file=sys.stderr)
            return 2

    results = [(classify(surface, n, args.margin), n) for n in nodes]
    total_text = sum(1 for n in doc.get("nodes", []) if n.get("measured_text_boxes"))

    print(f"surface={doc.get('surface','?')}  scale=x{scale:g}  "
          f"adjudicable={len(nodes)}/{total_text} text nodes "
          f"(the rest carry the w=0 multi-line sentinel)")
    buckets: dict[str, int] = {}
    for (verdict, detail), _ in results:
        buckets[verdict] = buckets.get(verdict, 0) + 1
        if verdict in ("CANDIDATE", "NO_INK"):
            print(f"  {verdict}: {detail}")
        elif verdict == "EXONERATED":
            print(f"  ok  {detail}")

    if args.plant:
        # Two controls, because this tool now makes two different claims. A tool
        # that only proves it can see ink has not proven it can see a CLIP.
        planted_any = False

        # (1) ink control -- can it see a spill at all?
        exon = [n for (v, _), n in results if v == "EXONERATED"]
        if not exon:
            print("ink_extents: BROKEN -- nothing was exonerated, so the ink "
                  "control has nothing to flip", file=sys.stderr)
            return 4
        target = max(exon, key=lambda n: n["rect"]["w"])
        # Draw on the GRAYSCALE image. `fill=255` on an RGB image paints red,
        # which converts to L=76 -- under the ink threshold, so the control
        # would report this tool blind when it is not. (The self-test caught
        # exactly that here.)
        planted = image.convert("L")
        r = target["rect"]
        d = ImageDraw.Draw(planted)
        ex = int((r["x"] + r["w"]) * scale)
        ey0 = int((r["y"] + r["h"] * 0.25) * scale)
        ey1 = int((r["y"] + r["h"] * 0.75) * scale)
        d.rectangle([ex + 1, ey0, ex + int(4 * scale), ey1], fill=255)
        verdict, _ = classify(Surface(doc, planted, scale), target, args.margin)
        if verdict == "EXONERATED":
            print(f"ink_extents: BROKEN -- planted ink past {target['id']} "
                  f"still reads EXONERATED; this tool cannot see a spill",
                  file=sys.stderr)
            return 4
        print(f"control: ink -- planting past {target['id']} flips it "
              f"EXONERATED -> {verdict}  OK")
        planted_any = True

        # (2) clip control -- can it see ink reaching a clip edge? Needs a node
        # under a clip TIGHTER than the scan window, or the plant lands outside
        # the region measure() looks at and proves nothing.
        eps = 1.0 / scale
        tight = []
        for (v, _), n in results:
            c = n["clipping"]["rect"]; r = n["rect"]
            if c["w"] <= 0 or r["w"] <= 0:
                continue
            if r["x"] < c["x"] - eps or r["x"] + r["w"] > c["x"] + c["w"] + eps:
                tight.append(n)
        if not tight:
            print("control: clip -- NOT ARMED. No text node's rect reaches past its",
                  "clip in this snapshot, so no clip can cut a glyph here and the",
                  "clip arm is UNPROVEN (not passed).", file=sys.stderr)
        else:
            ct = tight[0]
            cclip = ct["clipping"]["rect"]
            cr = ct["rect"]
            planted2 = image.convert("L")
            d2 = ImageDraw.Draw(planted2)
            cx = int((cclip["x"] + cclip["w"]) * scale)
            cy0 = int((cr["y"] + cr["h"] * 0.25) * scale)
            cy1 = int((cr["y"] + cr["h"] * 0.75) * scale)
            d2.rectangle([cx - int(2 * scale), cy0, cx, cy1], fill=255)
            v2, _ = classify(Surface(doc, planted2, scale), ct, args.margin)
            if v2 != "CANDIDATE":
                print(f"ink_extents: BROKEN -- ink planted at {ct['id']}'s clip "
                      f"edge reads {v2}, not CANDIDATE; the clip test is blind",
                      file=sys.stderr)
                return 4
            print(f"control: clip -- ink at {ct['id']}'s clip edge reads "
                  f"CANDIDATE  OK")

    # Only CANDIDATE blocks. SPILL is ink that is drawn (nothing clips it) and
    # NO_INK is usually a disabled control; neither is evidence of truncation.
    n_cand = buckets.get("CANDIDATE", 0)
    summary = "  ".join(f"{k}={v}" for k, v in sorted(buckets.items()))
    print(("RESOLVED" if n_cand == 0 else "CANDIDATES REMAIN") + f"   {summary}")
    return 1 if n_cand else 0


if __name__ == "__main__":
    sys.exit(main())
