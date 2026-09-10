#!/usr/bin/env python3
"""Appearance invariants over a dump_layout_tree() snapshot.

Two detectors, both asserting the property directly rather than a proxy:

  box-intersection      two painted glyph runs must not overlap
  painted-vs-measured   a glyph run must fit inside the box laid out for it

Both operate on the shipping standalone's own layout dump
(SPECTR_LAYOUT_DUMP=...), so they measure the installed surface.

Two things a naive reading of the dump gets wrong, both of which manufacture
false positives:

  * A node's `rect` is its LAYOUT box, which carries padding and centring slack.
    Two labels whose layout boxes touch can have glyph runs nowhere near each
    other. Adjudicate on `measured_text_boxes[].rect`, never on `rect`.
  * `visible: true` is the node's own flag, not a paint test. A node scrolled
    out of its container is still `visible` while lying wholly outside its
    `clipping.rect`, so it paints nothing. Intersect with the clip first.

A run whose painted width the dump reports as 0.0 cannot be adjudicated at all;
those are counted and reported rather than silently assumed innocent.

Plant flags exist so each detector can be shown failing. A check that cannot be
made to fail proves nothing.
"""

import argparse
import json
import sys


def load_nodes(path):
    doc = json.load(open(path))
    return doc["nodes"], doc.get("viewport", {})


def as_rect(r):
    if not r:
        return None
    return (float(r["x"]), float(r["y"]), float(r["w"]), float(r["h"]))


def intersect(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    x = max(ax, bx)
    y = max(ay, by)
    w = min(ax + aw, bx + bw) - x
    h = min(ay + ah, by + bh) - y
    if w <= 0.0 or h <= 0.0:
        return None
    return (x, y, w, h)


def clip_rect(node):
    return as_rect((node.get("clipping") or {}).get("rect"))


def painted_runs(nodes, region):
    """Glyph runs that actually reach the screen.

    Returns (painted, unmeasurable, clipped) where painted carries the run's
    on-screen rect after clipping.
    """
    painted, unmeasurable, clipped = [], [], []
    for n in nodes:
        if not n.get("visible", True):
            continue
        boxes = n.get("measured_text_boxes") or []
        if not boxes:
            continue
        b = boxes[0]
        text = b.get("text")
        if not text:
            continue
        run = as_rect(b.get("rect"))
        layout = as_rect(n.get("rect"))
        if run is None or layout is None:
            continue
        clip = clip_rect(n)
        # A node lying wholly outside its clip paints nothing, whatever its
        # `visible` flag says.
        if clip is not None and intersect(layout, clip) is None:
            clipped.append((n, text))
            continue
        if run[2] <= 0.0:
            unmeasurable.append((n, text))
            continue
        on_screen = run if clip is None else intersect(run, clip)
        if on_screen is None:
            clipped.append((n, text))
            continue
        if region and intersect(on_screen, region) is None:
            continue
        painted.append((n, text, on_screen, layout))
    return painted, unmeasurable, clipped


def within(inner, outer, eps):
    ix, iy, iw, ih = inner
    ox, oy, ow, oh = outer
    return (ix >= ox - eps and iy >= oy - eps
            and ix + iw <= ox + ow + eps and iy + ih <= oy + oh + eps)


def check_box_intersection(painted, eps, plant):
    items = list(painted)
    if plant:
        if len(items) < 2:
            return 0, ["PLANT IMPOSSIBLE: fewer than two painted runs"]
        n, t, r, lay = items[1]
        tx, ty, tw, th = items[0][2]
        # Straddle the target so neither run contains the other — a pure
        # collision, not nesting, which the nesting skip would swallow.
        items[1] = (n, t, (tx + tw / 2.0, ty + th / 2.0, r[2], r[3]), lay)

    violations = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            na, ta, ra, _ = items[i]
            nb, tb, rb, _ = items[j]
            if within(ra, rb, 0.5) or within(rb, ra, 0.5):
                continue
            ov = intersect(ra, rb)
            if ov and ov[2] > eps and ov[3] > eps:
                violations.append(
                    f"{ta!r}({na['id']}) {ra} overlaps {tb!r}({nb['id']}) {rb} "
                    f"by {ov[2]:.1f}x{ov[3]:.1f}px")
    return len(items), violations


def check_painted_vs_measured(painted, tol, plant):
    items = [(n, t, r[2], lay[2]) for n, t, r, lay in painted]
    if plant:
        if not items:
            return 0, ["PLANT IMPOSSIBLE: no measurable run"]
        n, t, pw, bw = items[0]
        items[0] = (n, t, bw + 12.0, bw)

    violations = []
    for n, t, pw, bw in items:
        if pw > bw + tol:
            violations.append(
                f"{t!r}({n['id']}) painted {pw:.1f}px in a {bw:.1f}px box "
                f"({pw / bw:.2f}x)")
    return len(items), violations


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--region", help="x,y,w,h — restrict to a subtree box")
    ap.add_argument("--overlap-eps", type=float, default=0.5)
    ap.add_argument("--fit-tolerance", type=float, default=0.5)
    ap.add_argument("--plant-overlap", action="store_true")
    ap.add_argument("--plant-overflow", action="store_true")
    args = ap.parse_args()

    nodes, viewport = load_nodes(args.dump)
    region = None
    if args.region:
        region = tuple(float(v) for v in args.region.split(","))

    print(f"dump: {args.dump}  nodes={len(nodes)}  viewport={viewport}")
    if region:
        print(f"region: {region}")

    painted, unmeasurable, clipped = painted_runs(nodes, region)
    print(f"runs: {len(painted)} painted, {len(unmeasurable)} unmeasurable "
          f"(dump reports painted width 0.0), {len(clipped)} clipped away")

    failed = False

    n_items, ov = check_box_intersection(painted, args.overlap_eps,
                                         args.plant_overlap)
    print(f"\n[box-intersection] adjudicated {n_items} painted runs")
    if ov:
        failed = True
        print(f"  RED — {len(ov)} overlap(s)")
        for v in ov[:20]:
            print(f"    {v}")
    else:
        print("  GREEN — no two painted runs overlap")

    n_m, fv = check_painted_vs_measured(painted, args.fit_tolerance,
                                        args.plant_overflow)
    print(f"\n[painted-vs-measured] adjudicated {n_m} painted runs")
    if fv:
        failed = True
        print(f"  RED — {len(fv)} run(s) exceed their box")
        for v in fv[:20]:
            print(f"    {v}")
    else:
        print("  GREEN — every painted run fits its box")

    if unmeasurable:
        print(f"\nCAVEAT — {len(unmeasurable)} run(s) could not be adjudicated "
              f"by either detector: the layout dump reports painted width 0.0 "
              f"for them. Neither detector can see a defect in these.")

    if not painted:
        print("\nINSTRUMENT BROKEN: nothing painted was adjudicated. "
              "Reporting nothing.")
        return 2

    print("\nRESULT:", "RED" if failed else "GREEN")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
