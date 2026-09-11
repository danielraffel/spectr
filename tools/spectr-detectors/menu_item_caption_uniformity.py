#!/usr/bin/env python3
"""Assert every pattern-menu item caption shares one line box.

A caption whose measured line box disagrees with its siblings renders with a
different height and falls back to its owner's left edge, so one row sits
flush-left against an otherwise uniform column.

Exit codes: 0 pass, 1 fail, 2 inconclusive (the probe could not measure).
"""
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
COL_X0, COL_X1, COL_Y0, COL_Y1 = 370.0, 620.0, 470.0, 820.0
MIN_ITEMS = 7


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


def captions(doc):
    out = []
    for node in doc["nodes"]:
        for box in node.get("measured_text_boxes") or []:
            r = box["rect"]
            if not (COL_X0 < r["x"] < COL_X1 and COL_Y0 < r["y"] < COL_Y1):
                continue
            if r["w"] <= 0:  # star glyph / zero-width chrome
                continue
            out.append((node["id"], box["text"], round(r["h"], 3)))
    return out


def main():
    app = sys.argv[1] if len(sys.argv) > 1 else APP
    if not os.path.exists(app):
        print("INCONCLUSIVE: app not built at %s" % app)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2
    with tempfile.TemporaryDirectory() as tmp:
        doc = run(app, os.path.join(tmp, "menu.json"))

    total = len(doc["nodes"])
    print("CONTROL total layout nodes = %d" % total)
    if total < 310:
        print("INCONCLUSIVE: menu did not open (node count %d)" % total)
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    items = captions(doc)
    print("CONTROL menu item captions measured = %d" % len(items))
    if len(items) < MIN_ITEMS:
        print("INCONCLUSIVE: only %d captions measured, need >= %d"
              % (len(items), MIN_ITEMS))
        print("RESULT: UNMEASURED (exit 2) -- nothing was adjudicated. Not a pass.")
        return 2

    heights = {}
    for _id, text, h in items:
        heights.setdefault(h, []).append(text)
    for _id, text, h in items:
        print("  %-24s h=%.2f" % (repr(text), h))

    if len(heights) == 1:
        print("PASS: all %d captions share line box h=%.2f"
              % (len(items), next(iter(heights))))
        return 0

    majority = max(heights, key=lambda k: len(heights[k]))
    print("FAIL: caption line boxes disagree across %d distinct heights"
          % len(heights))
    for h in sorted(heights):
        tag = "majority" if h == majority else "OUTLIER"
        print("  h=%.2f (%s): %s" % (h, tag, ", ".join(heights[h])))
    return 1


if __name__ == "__main__":
    sys.exit(main())
