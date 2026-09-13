#!/usr/bin/env python3
"""Assert every pattern-menu item caption shares one line box AND one leading
edge.

THE RULE, and the half of it that was missing

    A caption whose measured line box disagrees with its siblings renders with
    a different height, and a caption that is not drawn as a BOX falls back to
    its owner's left edge -- so one row sits flush-left against an otherwise
    uniform column.

    That sentence was this detector's docstring from the day it landed, and
    only the first clause was ever implemented.  The code compared `rect.h`
    and nothing else; `rect.x` appeared once, as a 100px-wide band filter
    (370..470) used to decide WHICH boxes to look at.  So a caption sitting
    9px left of its column stayed inside the band, was collected, reported
    h=14.00 like everything else, and the detector printed PASS.

    That is exactly what shipped: SAVE CURRENT... and MANAGE... both painted
    their ink at x 385.09 while the eight factory rows painted theirs at
    394.09, and this detector was green through the whole of it.  A user
    reported it by eye.  The leading-edge comparison below is the missing
    clause.

WHY A ROW MAY BE INDENTED BUT NEVER OUTDENTED

    The default preset's row publishes a leading marker glyph before its
    caption, so that one caption legitimately starts further RIGHT than the
    column.  An indent past the column is therefore allowed only for a row
    that carries such a marker; a caption LEFT of the column is the defect
    this exists to catch and is never allowed.

THE 1.00 px RESIDUAL, stated rather than hidden

    With both captions drawn as boxes, SAVE CURRENT... and MANAGE... measure
    395.09 against the factory column's 394.09.  That 1.00px is not this row's
    styling: those two rows are nested one level deeper than the factory rows,
    inside the grouping div that carries the separator, and that div's own box
    sits 1.00px right of its siblings.  It is NOT the div's `borderTop` --
    removing the border was measured and moved the captions not at all
    horizontally (it shifted y by 1 and left x at 395.09), which also matches
    `apply_border_widths` in Pulp's yoga_layout.cpp: a per-side border with no
    uniform shorthand resolves the other three edges to 0.  The tolerance
    below admits that 1.00px and nothing near the 9.00px class of defect.

WHAT IT CANNOT SEE

    Text-box geometry only.  A caption at the right x in the wrong colour, the
    wrong face or the wrong case passes here.

Usage:
    menu_item_caption_uniformity.py [APP]                  # drive the app
    menu_item_caption_uniformity.py DUMP.layout.json       # committed fixture
    ... [--plant left-fallback|tall-caption]               # must redden

Exit codes: 0 pass, 1 fail, 2 inconclusive (the probe could not measure).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

# Anchored to the repo this script lives in, never to the caller's cwd. A
# cwd-relative default silently resolves somewhere else -- or nowhere -- and
# this detector's "app not built" branch then reports INCONCLUSIVE about a
# checkout it never looked at.
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(REPO, "build-now", "Spectr.app", "Contents", "MacOS", "Spectr")
TRIGGER = '[data-spectr-menu-root="pattern"] button'
# The item column lives inside these design-space bounds when the menu is open.
# COL_X1 bounds the CAPTION column specifically -- the leading edge every item
# label starts from (measured: x 394..407). A row may also carry a trailing-edge
# affordance, and the MANAGE row does: a keyboard-shortcut chip at x 554, drawn
# at its own smaller type by design. That chip is not an item caption, and the
# leading-edge rule below is about captions. Including the chip would make a
# deliberate chip read as a broken caption. `CHIP_BAND` is the control: the chip
# must still be VISIBLE to the probe, so an empty trailing band means the menu
# changed shape rather than that the chip is fine.
COL_X0, COL_X1, COL_Y0, COL_Y1 = 370.0, 470.0, 470.0, 820.0
CHIP_BAND = (COL_X1, 620.0)
# Eight factory patterns plus SAVE CURRENT... and MANAGE... Raised from 7 so a
# narrowing of the column cannot quietly drop a caption and still pass.
MIN_ITEMS = 10
REQUIRED_CAPTIONS = ("SAVE CURRENT…", "MANAGE…")
# A leading glyph that legitimately indents the caption that follows it.
MARKERS = ("★",)
# Design px. Admits the measured 1.00px nesting residual documented above and
# nothing approaching the 9.00px fall-back-to-the-row-edge defect.
TOL = 1.25
# Same row iff the text boxes' baselines land within this many px of each other.
ROW_EPS = 4.0


def run(app, dump):
    env = dict(os.environ)
    env.update(
        PULP_HEADLESS="1",
        PULP_FRAMES="90",
        PULP_SCREENSHOT=dump + ".png",
        SPECTR_CLICK=TRIGGER,
        SPECTR_LAYOUT_DUMP=dump,
    )
    subprocess.run([app], env=env, capture_output=True, timeout=180)
    with open(dump) as fh:
        return json.load(fh)


def boxes_between(doc, x0, x1, keep_empty=False):
    out = []
    for node in doc["nodes"]:
        for box in node.get("measured_text_boxes") or []:
            r = box["rect"]
            if not (x0 < r["x"] < x1 and COL_Y0 < r["y"] < COL_Y1):
                continue
            if r["w"] <= 0 and not keep_empty:  # star glyph / zero-width chrome
                continue
            out.append((node["id"], box["text"], round(r["h"], 3),
                        round(r["x"], 3), round(r["y"], 3), round(r["w"], 3)))
    return out


def captions(doc):
    return boxes_between(doc, COL_X0, COL_X1)


def markers(doc):
    """Leading marker glyphs, which the width filter above drops (they measure
    zero-width). Their Y is what licenses an indent on the row they lead."""
    return [b for b in boxes_between(doc, COL_X0, COL_X1, keep_empty=True)
            if b[1].strip() in MARKERS]


def plant(doc, mode):
    """Mutate a healthy dump so the rule below HAS to fire."""
    hits = 0
    for node in doc["nodes"]:
        for box in node.get("measured_text_boxes") or []:
            text = box["text"].strip()
            r = box["rect"]
            if mode == "left-fallback":
                # The shipped defect, exactly: the two rows that were not
                # drawing their caption as a box painted at their row's own
                # left edge, 9px left of the factory column.
                if text in REQUIRED_CAPTIONS:
                    r["x"] -= 9.0
                    hits += 1
            elif mode == "tall-caption":
                if text == "COMB":
                    r["h"] += 2.1
                    hits += 1
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("target", nargs="?", default=APP,
                    help="app binary to drive, or a captured .layout.json")
    ap.add_argument("--plant", choices=("left-fallback", "tall-caption"),
                    help="mutate the input so the rule must fire")
    args = ap.parse_args()

    target = args.target
    if target.endswith(".json"):
        if not os.path.exists(target):
            print("INCONCLUSIVE: no dump at %s" % target)
            print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
            return 2
        with open(target) as fh:
            doc = json.load(fh)
        print("CONTROL source = captured dump %s" % os.path.basename(target))
    else:
        if not os.path.exists(target):
            print("INCONCLUSIVE: app not built at %s" % target)
            print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
            return 2
        with tempfile.TemporaryDirectory() as tmp:
            doc = run(target, os.path.join(tmp, "menu.json"))
        print("CONTROL source = live capture from %s" % os.path.basename(target))

    if args.plant:
        hits = plant(doc, args.plant)
        print("CONTROL planted %r on %d box(es)" % (args.plant, hits))
        if hits == 0:
            print("INCONCLUSIVE: the plant matched nothing, so a clean run below "
                  "would prove the rule is unreachable rather than satisfied")
            print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
            return 2

    total = len(doc["nodes"])
    print("CONTROL total layout nodes = %d" % total)
    if total < 310:
        print("INCONCLUSIVE: menu did not open (node count %d)" % total)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    items = captions(doc)
    print("CONTROL menu item captions measured = %d" % len(items))
    trailing = boxes_between(doc, *CHIP_BAND)
    print("CONTROL trailing-edge affordances in view = %d %r"
          % (len(trailing), [t[1] for t in trailing]))
    if not trailing:
        print("INCONCLUSIVE: nothing measured in the trailing band -- the caption "
              "column was narrowed to exclude it, so an empty band means the probe "
              "is aimed at a menu that no longer has the shape this bound assumes")
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2
    seen = {t[1] for t in items}
    absent = [c for c in REQUIRED_CAPTIONS if c not in seen]
    if absent:
        print("INCONCLUSIVE: required captions absent from the column: %r" % absent)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2
    if len(items) < MIN_ITEMS:
        print("INCONCLUSIVE: only %d captions measured, need >= %d"
              % (len(items), MIN_ITEMS))
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    marker_ys = [m[4] for m in markers(doc)]
    print("CONTROL leading marker glyphs = %d at y=%r"
          % (len(marker_ys), marker_ys))

    # The column is the x the MOST captions agree on. Using the minimum instead
    # would let a single fallen-back caption redefine the column and make the
    # eight correct rows read as the outliers.
    xs = {}
    for it in items:
        xs.setdefault(it[3], []).append(it[1])
    column = max(xs, key=lambda k: (len(xs[k]), -k))
    print("CONTROL caption column = %.2f (%d of %d captions)"
          % (column, len(xs[column]), len(items)))

    print("  %-24s %8s %8s %8s" % ("caption", "ink_x", "d(col)", "h"))
    for _id, text, h, x, y, _w in sorted(items, key=lambda v: (v[4], v[3])):
        print("  %-24s %8.2f %+8.2f %8.2f" % (repr(text)[:24], x, x - column, h))

    failures = []

    for _id, text, _h, x, y, _w in items:
        delta = x - column
        if delta < -TOL:
            failures.append(
                "%r starts at x=%.2f, %.2f px LEFT of the caption column %.2f -- "
                "a caption that is not drawn as a box falls back to its row's own "
                "left edge" % (text, x, -delta, column))
        elif delta > TOL:
            led = any(abs(y - my) <= ROW_EPS for my in marker_ys)
            if not led:
                failures.append(
                    "%r starts at x=%.2f, %.2f px right of the caption column "
                    "%.2f and its row publishes no leading marker glyph"
                    % (text, x, delta, column))

    heights = {}
    for _id, text, h, _x, _y, _w in items:
        heights.setdefault(h, []).append(text)
    if len(heights) != 1:
        majority = max(heights, key=lambda k: len(heights[k]))
        detail = "; ".join(
            "h=%.2f (%s): %s" % (h, "majority" if h == majority else "OUTLIER",
                                 ", ".join(heights[h]))
            for h in sorted(heights))
        failures.append("caption line boxes disagree across %d distinct heights "
                        "-- %s" % (len(heights), detail))

    if failures:
        print("FAIL:")
        for f in failures:
            print("  " + f)
        return 1
    print("PASS: all %d captions share line box h=%.2f and start on the caption "
          "column %.2f (tol %.2f)"
          % (len(items), next(iter(heights)), column, TOL))
    return 0


if __name__ == "__main__":
    sys.exit(main())
