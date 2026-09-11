#!/usr/bin/env python3
"""Assert a settings chip stays painted after the pointer leaves it.

The property under test is visibility of the *shipping* surface, not layout:
a chip whose pointer has moved away must still paint with roughly the same
contrast against the settings-modal background that it had before any pointer
ever touched it. Layout is deliberately not consulted for the verdict -- the
failure this guards against leaves every rect, every `visible` flag and every
clip box untouched and simply stops painting the subtree.

Usage:
  hover_leave_persistence.py --app <Spectr binary> [--out-dir DIR] [--eval FILE]

Exit codes: 0 pass, 1 fail, 2 harness/instrument error.
"""

import argparse
import json
import os
import subprocess
import sys

try:
    from PIL import Image, ImageStat
except ImportError:  # pragma: no cover - environment guard
    print("hover-leave-persistence: Pillow is required", file=sys.stderr)
    sys.exit(2)

# Settings chips in the Appearance row, in left-to-right order. These ids are
# emitted by the materialized runtime and appear in the layout dump.
CHIPS = ["__behavior_pr_19", "__behavior_pr_1a", "__behavior_pr_1b", "__behavior_pr_1c"]

# A patch of settings-modal background with no widget in it, in root space.
MODAL_BG_RECT = (620.0, 240.0, 250.0, 12.0)

# Fraction of the untouched contrast a chip must retain once the pointer has
# left it. A vanished chip scores ~0.0; a merely tinted one scores near 1.0.
CONTRAST_FLOOR = 0.5


def capture(app, name, out_dir, extra_env):
    png = os.path.join(out_dir, name + ".png")
    dump = os.path.join(out_dir, name + ".layout.json")
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
        "SPECTR_OPEN_SETTINGS": "1",
        "SPECTR_LAYOUT_DUMP": dump,
    })
    env.update(extra_env)
    log = os.path.join(out_dir, name + ".log")
    with open(log, "wb") as fh:
        subprocess.run([app], env=env, stdout=fh, stderr=subprocess.STDOUT, timeout=180)
    if not os.path.exists(png):
        raise RuntimeError("no screenshot produced for '%s' (see %s)" % (name, log))
    if not os.path.exists(dump):
        raise RuntimeError("no layout dump produced for '%s' (see %s)" % (name, log))
    return png, dump


def chip_rects(dump_path):
    doc = json.load(open(dump_path))
    by_id = {}
    for node in doc.get("nodes", []):
        nid = node.get("id")
        rect = node.get("rect")
        if nid in CHIPS and isinstance(rect, dict):
            by_id[nid] = (rect["x"], rect["y"], rect["w"], rect["h"])
    missing = [c for c in CHIPS if c not in by_id]
    if missing:
        raise RuntimeError("layout dump is missing chips %s -- the probe is "
                           "pointed at the wrong surface" % missing)
    return by_id


def luminance(img, rect, scale):
    x, y, w, h = rect
    box = (int(x * scale), int(y * scale), int((x + w) * scale), int((y + h) * scale))
    return ImageStat.Stat(img.crop(box).convert("L")).mean[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--out-dir", default="/tmp/spectr-hover-leave")
    ap.add_argument("--eval", dest="eval_file", default=None,
                    help="JS evaluated in the editor before the hover, used to "
                         "stage a candidate fix without rebuilding the SDK")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    extra = {}
    if args.eval_file:
        extra["SPECTR_EVAL"] = open(args.eval_file).read()

    base_png, base_dump = capture(args.app, "base", args.out_dir, extra)
    rects = chip_rects(base_dump)

    # Walk the pointer across every chip in turn. Each step leaves the chip
    # before it, so by the last step every chip except the final one has been
    # left at least once.
    seq = ";".join("%d,%d" % (r[0] + r[2] / 2, r[1] + r[3] / 2)
                   for r in (rects[c] for c in CHIPS))
    hov = dict(extra)
    hov["SPECTR_HOVER"] = seq
    hover_png, _ = capture(args.app, "hover_walk", args.out_dir, hov)

    base_img = Image.open(base_png)
    hover_img = Image.open(hover_png)
    scale = base_img.width / 1320.0

    base_bg = luminance(base_img, MODAL_BG_RECT, scale)
    hover_bg = luminance(hover_img, MODAL_BG_RECT, scale)

    # Positive control: the instrument must be able to see a chip at all.
    # If no chip is distinguishable from the modal background even in the
    # untouched baseline, the rects or the surface are wrong -- report nothing.
    base_contrast = {c: abs(luminance(base_img, rects[c], scale) - base_bg) for c in CHIPS}
    if max(base_contrast.values()) < 5.0:
        print("hover-leave-persistence: CONTROL FAILED -- no chip is visible in "
              "the untouched baseline (max contrast %.2f). The measurement is "
              "not pointed at the chips; no verdict." % max(base_contrast.values()),
              file=sys.stderr)
        return 2

    failures = []
    print("chip                     base   hover   base-c  hover-c  retained")
    for c in CHIPS[:-1]:  # the last chip is still hovered, never left
        b = luminance(base_img, rects[c], scale)
        h = luminance(hover_img, rects[c], scale)
        bc = abs(b - base_bg)
        hc = abs(h - hover_bg)
        retained = hc / bc if bc > 0 else 0.0
        flag = "" if retained >= CONTRAST_FLOOR else "   <-- ERASED"
        print("%-22s %6.2f %6.2f %8.2f %8.2f %8.2f%s" % (c, b, h, bc, hc, retained, flag))
        if retained < CONTRAST_FLOOR:
            failures.append((c, retained))

    print("\nbaseline: %s\nhover:    %s" % (base_png, hover_png))
    if failures:
        print("\nFAIL: %d chip(s) stopped painting after the pointer left them: %s"
              % (len(failures), ", ".join(c for c, _ in failures)))
        return 1
    print("\nPASS: every chip the pointer left is still painted.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - harness errors are exit 2
        print("hover-leave-persistence: %s" % exc, file=sys.stderr)
        sys.exit(2)
