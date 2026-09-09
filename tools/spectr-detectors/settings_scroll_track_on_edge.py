#!/usr/bin/env python3
"""Assert the settings scroll track paints on the panel's edge, not inset over content.

The property under test is where the track is actually *painted* on the shipping
surface. Layout is used only to locate the panel and the scroll viewport; the
verdict is read from pixels, because an inset track is a paint position, and a
scroll viewport whose right edge is correct can still be drawn short of it.

Usage:
  settings_scroll_track_on_edge.py --app <Spectr binary> [--out-dir DIR] [--eval FILE]
  settings_scroll_track_on_edge.py --shot PNG --layout LAYOUT.json

Exit codes: 0 pass, 1 fail, 2 harness/instrument error.
"""

import argparse
import json
import os
import subprocess
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - environment guard
    print("scroll-track-on-edge: Pillow is required", file=sys.stderr)
    sys.exit(2)

# How far inside the settings panel's own right edge the scroll track is allowed
# to paint, in root pixels. The track is 8 root px wide and the panel border sits
# ~1 px inside its rect, so a track resting on the edge lands under ~12.
MAX_INSET_ROOT_PX = 12.0

# A track column must outrun the flat panel gutter by at least this much (0-255).
TRACK_OVER_GUTTER = 20.0

# The track is 8 root px wide. Narrower bright runs in this gutter are content
# glyphs and widget edges, not a track; accepting them lets a capture that has no
# track at all report a confident (and wrong) position instead of no verdict.
MIN_TRACK_ROOT_PX = 6.0


def capture(app, name, out_dir, extra_env):
    png = os.path.join(out_dir, name + ".png")
    dump = os.path.join(out_dir, name + ".layout.json")
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


def locate(dump_path):
    """Return (scroll_viewport_rect, settings_panel_rect, viewport_w) from layout."""
    doc = json.load(open(dump_path))
    nodes = doc.get("nodes", [])
    scrollers = [n for n in nodes
                 if (n.get("overflow") or "") == "scroll" and isinstance(n.get("rect"), dict)]
    if len(scrollers) != 1:
        raise RuntimeError("expected exactly one scrolling node, found %d -- the "
                           "settings panel is probably not open" % len(scrollers))
    sv = scrollers[0]["rect"]

    def contains(outer, inner):
        return (outer["x"] <= inner["x"] + 0.5
                and outer["y"] <= inner["y"] + 0.5
                and outer["x"] + outer["w"] >= inner["x"] + inner["w"] - 0.5
                and outer["y"] + outer["h"] >= inner["y"] + inner["h"] - 0.5)

    # The panel is the tightest box that both contains the scroll viewport and is
    # strictly wider than it -- which excludes the viewport's own content wrapper.
    holders = [n["rect"] for n in nodes
               if isinstance(n.get("rect"), dict)
               and n["rect"]["w"] > sv["w"] + 0.5
               and contains(n["rect"], sv)]
    if not holders:
        raise RuntimeError("no node encloses the scroll viewport -- the layout dump "
                           "does not describe the surface this detector expects")
    panel = min(holders, key=lambda r: r["w"] * r["h"])
    return sv, panel, doc["viewport"]["w"]


def column_profile(img, x0, x1, y0, y1):
    px = img.load()
    prof = []
    for x in range(x0, x1):
        vals = [px[x, y] for y in range(y0, y1, 3)]
        prof.append(sum(vals) / float(len(vals)))
    return prof


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app")
    ap.add_argument("--out-dir", default="/tmp/spectr-scroll-track")
    ap.add_argument("--eval")
    ap.add_argument("--shot")
    ap.add_argument("--layout")
    args = ap.parse_args()

    try:
        if args.shot and args.layout:
            png, dump = args.shot, args.layout
        elif args.app:
            os.makedirs(args.out_dir, exist_ok=True)
            extra = {"PULP_SCRIPT_EVAL_FILE": args.eval} if args.eval else {}
            # The track only materialises once a pointer has been inside the body,
            # so probe the geometry first and then hover the viewport's centre.
            _, probe_dump = capture(args.app, "probe", args.out_dir, extra)
            sv, _panel, _vw = locate(probe_dump)
            point = "%d,%d" % (sv["x"] + sv["w"] / 2, sv["y"] + sv["h"] / 2)
            extra = dict(extra)
            extra["SPECTR_HOVER"] = point
            png, dump = capture(args.app, "scroll_track", args.out_dir, extra)
        else:
            raise RuntimeError("pass --app, or both --shot and --layout")

        sv, panel, root_w = locate(dump)
        img = Image.open(png).convert("L")
        scale = img.width / float(root_w)

        panel_right = panel["x"] + panel["w"]
        # Scan the gutter between the content column and the panel edge, over the
        # vertical middle of the scroll viewport where the track always exists.
        x0 = int((sv["x"] + sv["w"] * 0.5) * scale)
        x1 = min(img.width, int(panel_right * scale))
        y0 = int((sv["y"] + 20) * scale)
        y1 = int((sv["y"] + sv["h"] - 20) * scale)
        if x1 - x0 < 10 or y1 - y0 < 10:
            raise RuntimeError("the scan window collapsed (%dx%d px) -- the panel "
                               "geometry is not what this detector expects"
                               % (x1 - x0, y1 - y0))

        prof = column_profile(img, x0, x1, y0, y1)

        # Positive control: the flat panel gutter must exist. It is the most common
        # value in the scan and must repeat over a real run of columns; if it does
        # not, the scan is looking at content or off the panel and no absence of a
        # track here means anything.
        buckets = {}
        for v in prof:
            buckets[round(v, 1)] = buckets.get(round(v, 1), 0) + 1
        gutter, gutter_cols = max(buckets.items(), key=lambda kv: kv[1])
        min_gutter_cols = max(6, int(6 * scale))
        print("control  panel gutter level %.1f over %d of %d scanned columns"
              % (gutter, gutter_cols, len(prof)))
        if gutter_cols < min_gutter_cols:
            raise RuntimeError("no flat panel gutter found (widest level %.1f spans "
                               "only %d columns) -- the scan is not on the panel, so "
                               "a missing track would prove nothing" % (gutter, gutter_cols))

        # Subject: the rightmost bright run in the gutter is the scroll track.
        runs = []
        start = None
        for i, v in enumerate(prof):
            bright = v >= gutter + TRACK_OVER_GUTTER
            if bright and start is None:
                start = i
            elif not bright and start is not None:
                runs.append((start, i))
                start = None
        if start is not None:
            runs.append((start, len(prof)))
        runs = [r for r in runs if (r[1] - r[0]) / scale >= MIN_TRACK_ROOT_PX]
        if not runs:
            raise RuntimeError("no scroll track found in the panel gutter -- the "
                               "hover that materialises it may not have landed")
        run = runs[-1]
        track_left = (x0 + run[0]) / scale
        track_right = (x0 + run[1]) / scale
        inset = panel_right - track_right

        print("subject  scroll track root x %.1f..%.1f (width %.1f)"
              % (track_left, track_right, track_right - track_left))
        print("subject  settings panel right edge %.1f, track inset %.1f px"
              % (panel_right, inset))

        # Invariant: the track must not sit over the content column.
        doc = json.load(open(dump))
        content_right = 0.0
        for n in doc.get("nodes", []):
            r = n.get("rect")
            if not isinstance(r, dict) or r["w"] <= 0:
                continue
            # Only boxes wholly inside the scroll viewport's visible band count as
            # content the track could sit on top of. Anything as wide as the
            # viewport is the viewport or its own content wrapper, not a widget.
            if (r["x"] >= sv["x"] - 0.5
                    and r["x"] + r["w"] <= sv["x"] + sv["w"] + 0.5
                    and r["y"] >= sv["y"] - 0.5
                    and r["y"] + r["h"] <= sv["y"] + sv["h"] + 0.5
                    and r["w"] < sv["w"] - 0.5):
                content_right = max(content_right, r["x"] + r["w"])
        print("invariant content column right edge %.1f" % content_right)

        failures = []
        if inset > MAX_INSET_ROOT_PX:
            failures.append("the scroll track paints %.1fpx inside the settings panel "
                            "edge, more than the %.1fpx a track resting on the edge "
                            "takes -- it is indented into the content gutter"
                            % (inset, MAX_INSET_ROOT_PX))
        if content_right > track_left + 0.5:
            failures.append("the scroll track starts at %.1f but content extends to "
                            "%.1f -- the track overlaps widgets"
                            % (track_left, content_right))

        if failures:
            for f in failures:
                print("FAIL: " + f)
            return 1
        print("PASS: the settings scroll track rests on the panel edge, clear of content.")
        return 0

    except Exception as exc:  # noqa: BLE001 - the harness reports, never guesses
        print("no verdict: %s" % exc)
        return 2


if __name__ == "__main__":
    sys.exit(main())
