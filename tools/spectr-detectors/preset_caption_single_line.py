#!/usr/bin/env python3
"""Assert the bottom-bar preset caption renders on one line inside its trigger.

The trigger's box is baked by the materializer and does not grow with the
selected name, so a caption that is allowed to wrap breaks at its space and
paints its second line below the button's border.  This drives the shipping
standalone, selects a two-word factory preset, and reads the caption's own
measured text box out of the layout dump.

Exit codes: 0 pass, 1 fail, 2 no verdict (the instrument could not measure).
"""

import argparse
import json
import os
import subprocess
import sys

# One rail caption occupies a single 13px line box; a wrapped caption reports
# twice that.  The bound sits between the two rather than on either.
ONE_LINE_MAX_H = 20.0
BOTTOM_BAR_MIN_Y = 780.0
CARET = "▾"
# The factory preset this detector selects, and the two neighbouring rail
# captions that share the trigger's line box.  The neighbours are the control:
# they must measure as one line in the very same dump, so a subject that
# measures as one line cannot be an artefact of the instrument.
EXPECTED_PRESET = "DOWNWA"
CONTROL_CAPTIONS = ("SCULPT", "PEAK")
PRESET_SELECTOR = ('[data-spectr-menu-root="pattern"] button,'
                   '[data-spectr-pattern-menu-id="factory:tilt"]')


def capture(app, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    png = os.path.join(out_dir, "preset_caption.png")
    dump = os.path.join(out_dir, "preset_caption.layout.json")
    # Remove any artifact from a previous run BEFORE launching. The out-dir
    # default is a fixed path, and the only liveness check below is
    # os.path.exists -- so a launch that produces nothing silently adjudicates
    # the PREVIOUS run's capture and reports a confident verdict about code
    # that is no longer under test.
    for stale in (png, dump):
        if os.path.exists(stale):
            os.remove(stale)
    env = dict(os.environ)
    env.update({
        "PULP_HEADLESS": "1",
        "PULP_SCREENSHOT": png,
        "PULP_FRAMES": "90",
        "SPECTR_CLICK": PRESET_SELECTOR,
        "SPECTR_LAYOUT_DUMP": dump,
    })
    log = os.path.join(out_dir, "preset_caption.log")
    with open(log, "w") as fh:
        rc = subprocess.call([app], env=env, stdout=fh, stderr=subprocess.STDOUT)
    if rc != 0 or not os.path.exists(dump):
        sys.exit("no verdict: the standalone exited %d; see %s" % (rc, log))
    return dump


def text_boxes(nodes):
    for node in nodes:
        for box in node.get("measured_text_boxes") or []:
            yield node, box


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app")
    ap.add_argument("--out-dir", default="/tmp/spectr-preset-caption")
    ap.add_argument("--layout", help="evaluate an archived layout dump instead")
    args = ap.parse_args()

    dump = args.layout or capture(args.app, args.out_dir)
    nodes = json.load(open(dump))["nodes"]

    captions = [(n, b) for n, b in text_boxes(nodes)
                if CARET in (b.get("text") or "")
                and b["rect"]["y"] > BOTTOM_BAR_MIN_Y]

    controls = []
    for name in CONTROL_CAPTIONS:
        hit = [b for _n, b in captions if (b.get("text") or "").startswith(name)]
        if len(hit) != 1:
            print("no verdict: the control caption %r measured %d times in %s "
                  "-- the bottom bar did not lay out as expected"
                  % (name, len(hit), dump))
            return 2
        controls.append((name, hit[0]))
    tall = [n for n, b in controls if b["rect"]["h"] > ONE_LINE_MAX_H]
    if tall:
        print("no verdict: control caption(s) %s already exceed one line, so the "
              "height bound cannot discriminate" % ", ".join(tall))
        return 2

    subject = [(n, b) for n, b in captions
               if (b.get("text") or "").startswith(EXPECTED_PRESET)]
    if len(subject) != 1:
        print("no verdict: the selected preset caption (%r) measured %d times "
              "-- the preset was never applied" % (EXPECTED_PRESET, len(subject)))
        return 2

    node, box = subject[0]
    rect = box["rect"]
    text = box["text"]
    # The trigger is the tightest box that contains the caption.
    holders = [n for n in nodes
               if n["rect"]["w"] < 300
               and n["rect"]["x"] <= rect["x"] + 0.5
               and n["rect"]["y"] <= rect["y"] + 0.5
               and n["rect"]["x"] + n["rect"]["w"] >= rect["x"] + 0.5
               and n["rect"]["h"] >= ONE_LINE_MAX_H]
    holders.sort(key=lambda n: n["rect"]["w"] * n["rect"]["h"])
    trigger = holders[0] if holders else None

    print("caption   %-22r w=%.1f h=%.1f" % (text, rect["w"], rect["h"]))
    if trigger:
        print("trigger   %-22s w=%.1f h=%.1f"
              % (trigger["id"] or "(anonymous)", trigger["rect"]["w"],
                 trigger["rect"]["h"]))
    for name, b in controls:
        print("control   %-22r w=%.1f h=%.1f" % (b["text"], b["rect"]["w"], b["rect"]["h"]))

    failures = []
    if rect["h"] > ONE_LINE_MAX_H:
        failures.append("the caption occupies %.1fpx, more than the %.1fpx a "
                        "single line box takes -- it wrapped"
                        % (rect["h"], ONE_LINE_MAX_H))
    if trigger is not None:
        overshoot = (rect["y"] + rect["h"]) - (trigger["rect"]["y"] + trigger["rect"]["h"])
        if overshoot > 0.5:
            failures.append("the caption paints %.1fpx below the trigger's "
                            "bottom edge" % overshoot)

    if failures:
        for f in failures:
            print("FAIL:", f)
        return 1
    print("PASS: the preset caption is one line inside its trigger.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
