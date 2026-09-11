#!/usr/bin/env python3
"""Measure the contrast a text node actually PAINTS, and hold it to a floor.

The appearance invariants (overlap / fit, plus the collapsed-box sweep) answer
"is the text in the right box". They say nothing about whether a human can read
it. A label
authored at opacity 0.45 sits in a perfectly correct box and still fails.

So this reads the rendered PNG, not the layout JSON's intent: for every text
node it finds the ink, measures peak and mean-ink contrast against the local
background per WCAG relative luminance, and fails any node below the floor for
its painted size.

Two things this deliberately does NOT do:
  * It does not read the authored colour. Opacity composites, antialiasing
    thins strokes, and a scrim over a label changes what reaches the eye --
    only the pixels know the answer.
  * It does not treat "no ink found" as a pass. That is UNMEASURED and is
    reported separately, because an empty crop is exactly how a broken probe
    looks like a clean one.

--plant is the negative control: it dims one high-contrast node in the image
so the detector MUST go red. A detector never shown failing proves nothing.
"""
import argparse, json, sys
from collections import Counter
from PIL import Image

# WCAG 2.x: 4.5:1 for normal text, 3:1 for "large" text
# (>=18.66px regular / >=24px). Painted px, not authored px.
LARGE_PX = 18.66
FLOOR_NORMAL = 4.5
FLOOR_LARGE = 3.0


def _srgb(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def rel_lum(rgb):
    r, g, b = rgb
    return 0.2126 * _srgb(r) + 0.7152 * _srgb(g) + 0.0722 * _srgb(b)


def contrast(a, b):
    la, lb = rel_lum(a), rel_lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


def text_nodes(nodes):
    for n in nodes:
        if not n.get("visible", True):
            continue
        boxes = n.get("measured_text_boxes") or []
        for b in boxes:
            t = (b.get("text") or "").strip()
            if t:
                yield n, t, b


def measure(im, rect, scale, ink_delta=0.004):
    """Return (bg, peak, mean_ink, ink_count, ink_run_h) for a rect in design px.

    ink_run_h is the tallest CONTIGUOUS run of image rows that carry ink,
    in device pixels. It is the discriminator for the WCAG large-text
    exemption -- the node's box height is not, because a two-line hint has a
    box twice the height of its glyphs and would claim the 3:1 large-text
    floor while painting 9.5px type. Ascender+descender is roughly 1.0x the
    font size, so a single ink run is a LOWER bound on the painted size:
    using it can only ever withhold the exemption too conservatively, never
    grant it wrongly.
    """
    px = im.load()
    W, H = im.size
    x0, y0 = int(rect["x"] * scale), int(rect["y"] * scale)
    x1, y1 = int((rect["x"] + rect["w"]) * scale), int((rect["y"] + rect["h"]) * scale)
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(W, x1), min(H, y1)
    if x1 <= x0 or y1 <= y0:
        return None, None, None, 0, 0
    samples = [px[x, y] for y in range(y0, y1) for x in range(x0, x1)]
    if not samples:
        return None, None, None, 0, 0
    bg = Counter(samples).most_common(1)[0][0]
    bl = rel_lum(bg)
    ink = [p for p in samples if abs(rel_lum(p) - bl) > ink_delta]
    if not ink:
        return bg, None, None, 0, 0
    peak = max(ink, key=lambda p: abs(rel_lum(p) - bl))
    n = len(ink)
    mean = (
        int(sum(p[0] for p in ink) / n),
        int(sum(p[1] for p in ink) / n),
        int(sum(p[2] for p in ink) / n),
    )
    run = best = 0
    for y in range(y0, y1):
        lit = any(abs(rel_lum(px[x, y]) - bl) > ink_delta for x in range(x0, x1))
        run = run + 1 if lit else 0
        best = max(best, run)
    return bg, contrast(peak, bg), contrast(mean, bg), n, best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("layout")
    ap.add_argument("image")
    ap.add_argument("--scale", type=float, required=True,
                    help="painted px per design px for this capture")
    ap.add_argument("--floor", type=float, default=None,
                    help="override the WCAG floor for every node")
    ap.add_argument("--plant", action="store_true",
                    help="negative control: dim the highest-contrast node")
    ap.add_argument("--min-ink", type=int, default=4,
                    help="fewer ink pixels than this is UNMEASURED, not a pass")
    ap.add_argument("--within", default=None,
                    help="restrict to nodes fully inside this design-space "
                         "rect 'x,y,w,h'. Use it to scope a modal: text behind "
                         "a scrim is dimmed ON PURPOSE and is not a defect, so "
                         "measuring it reddens the detector for the wrong "
                         "reason.")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    nodes = json.load(open(a.layout))["nodes"]
    im = Image.open(a.image).convert("RGB")

    within = None
    if a.within:
        wx, wy, ww, wh = (float(v) for v in a.within.split(","))
        within = (wx, wy, wx + ww, wy + wh)

    rows = []
    skipped_outside = 0
    for n, text, box in text_nodes(nodes):
        rect = box.get("rect") or n.get("rect")
        if not rect or rect.get("w", 0) <= 0:
            rect = n.get("rect")
        if not rect:
            continue
        if within is not None:
            nr = n.get("rect") or rect
            cx, cy = nr["x"] + nr["w"] / 2.0, nr["y"] + nr["h"] / 2.0
            if not (within[0] <= cx <= within[2] and within[1] <= cy <= within[3]):
                skipped_outside += 1
                continue
        bg, peak, mean, cnt, run = measure(im, rect, a.scale)
        rows.append({
            "id": n.get("id"), "text": text,
            "painted_h": rect.get("h", 0) * a.scale,
            "ink_run_h": run,
            "bg": bg, "peak": peak, "mean_ink": mean, "ink_px": cnt,
        })

    if a.plant:
        lit = [r for r in rows if r["peak"] is not None]
        if not lit:
            print("PLANT FAILED: nothing to dim", file=sys.stderr)
            return 2
        tgt = max(lit, key=lambda r: r["peak"])
        # Dim that node's pixels toward its background.
        node = next(n for n, t, b in text_nodes(nodes) if n.get("id") == tgt["id"])
        r = node["rect"]
        px = im.load()
        x0, y0 = int(r["x"] * a.scale), int(r["y"] * a.scale)
        x1, y1 = int((r["x"] + r["w"]) * a.scale), int((r["y"] + r["h"]) * a.scale)
        bg = tgt["bg"]
        for y in range(max(0, y0), min(im.size[1], y1)):
            for x in range(max(0, x0), min(im.size[0], x1)):
                p = px[x, y]
                px[x, y] = tuple(int(bg[i] + (p[i] - bg[i]) * 0.18) for i in range(3))
        print(f"PLANTED: dimmed {tgt['id']!r} ({tgt['text'][:30]!r}) "
              f"from {tgt['peak']:.2f}:1")
        for row in rows:
            if row["id"] == tgt["id"]:
                _, pk, mn, c, rn = measure(im, node["rect"], a.scale)
                row["peak"], row["mean_ink"] = pk, mn
                row["ink_px"], row["ink_run_h"] = c, rn

    fails, unmeasured = [], []
    for r in rows:
        if r["ink_px"] < a.min_ink:
            unmeasured.append(r)
            continue
        # Measured ink, not the box: a wrapped two-line hint has a 23px box
        # around 9.5px glyphs and must not inherit the large-text exemption.
        floor = a.floor if a.floor is not None else (
            FLOOR_LARGE if r["ink_run_h"] >= LARGE_PX else FLOOR_NORMAL)
        r["floor"] = floor
        if r["peak"] < floor:
            fails.append(r)

    if a.json:
        print(json.dumps({"fails": fails, "unmeasured": unmeasured,
                          "measured": len(rows) - len(unmeasured)}, indent=1))
    else:
        print(f"measured {len(rows) - len(unmeasured)} text node(s), "
              f"{len(unmeasured)} UNMEASURED (no ink)"
              + (f", {skipped_outside} outside --within" if within else ""))
        for r in unmeasured:
            print(f"  UNMEASURED ({r['ink_px']} ink px) {r['text'][:52]!r}")
        for r in sorted(fails, key=lambda r: r["peak"]):
            print(f"  CONTRAST {r['peak']:5.2f}:1 (floor {r['floor']}, "
                  f"mean-ink {r['mean_ink']:.2f}:1, ink {r['ink_run_h']:.0f}px "
                  f"in a {r['painted_h']:.1f}px box) "
                  f"{r['text'][:52]!r}")
    if fails:
        print(f"RED {len(fails)} contrast violation(s)")
        return 1
    print("GREEN 0 contrast violations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
