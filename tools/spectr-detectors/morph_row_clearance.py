#!/usr/bin/env python3
"""Assert the morph track is laid out CLEAR of its flanking "A"/"B" labels.

The morph control is three flex items -- an "A" label, a 90x16 track, a "B"
label -- and for most of its life the middle one was laid out at the SAME x as
the first.  `A` and `B` were authored as bare `<span>`s, which carry no layout
box on the native runtime: each measured ~0 main size, so Yoga placed it at the
row's content origin and the glyph painted there, overflowing a box that never
grew.  The track, the next item, therefore began ON the "A" glyph rather than
6px past it, and everything painted inside the track landed on top of it -- at
value 0 the thumb covered the label outright.  Reported as "the morph slider is
overlapping the A".

WHY THIS DETECTOR EXISTS AT ALL, given `box_intersection.py`:

  * `box_intersection` compares only nodes that CARRY TEXT.  The thumb is a
    text-free `div`, so the thumb-on-label collision -- the half a user
    actually sees -- was structurally invisible to it and always would be.
  * The half it COULD see, the disabled caption's node box starting on the "A"
    label, it did report: a 5.4x12px pair.  That finding was correct and was
    dismissed as pre-existing, because it was identical in the before and after
    dumps of an unrelated change.  It was not decoration; it was this defect,
    seen through the one node that happened to hold text.

So the rule here is stated on BOXES, not on ink, and it is stated about the
track rather than about any one thing drawn inside it.  That is deliberate:
`slider_thumb_pill_shape.py` already proves the thumb stays within the track at
every value in both its idle and hovered sizes, so a track that clears its
neighbours makes every position of the thumb clear them too -- including the
hovered 26px size, which no capture ever paints (the morph slider is disabled
in every state `Spectr-native-shot` reaches, where the thumb is `opacity: 0`).

Four claims, each able to fail on its own:

  CLEARANCE   the track's box does not come within --min-clearance px of
              either flanking sibling.  The floor is a legibility floor, not
              the design gap: it is well under the 6px the row declares, so a
              deliberate re-spacing does not trip it while an overlap does.
  CONTAINED   every descendant of the track stays inside the track's box, so
              nothing escapes sideways past the clearance proved above.
  UNCRUSHED   the track is still its full width.  The transport row has no
              spare width and has already absorbed an addition by CRUSHING
              this control to 39.5px with both end labels collapsed to zero,
              which reads as a layout that "fits".
  MEASURED    the morph node, its parent and exactly two flanking siblings are
              all present.  A dump without them is UNMEASURED (exit 2), never
              a pass -- a detector pointed at the wrong surface reports clean.

Plants (negative controls; each MUST redden the run):
  --plant overlap  put the track back on the left label, at the exact
                   pre-fix geometry, and drag its descendants with it.
  --plant crush    shrink the track to the 39.5px the row once crushed it to.

Exit: 0 clean, 1 findings, 2 unmeasured / usage error.
"""

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout_common as L  # noqa: E402

MORPH_ID = "spectr-snapshot-morph"
CRUSHED_WIDTH = 39.5


def box(node):
    return L.r(node["rect"])


def painted_extent(node):
    """The node's box UNIONED with the ink it actually paints.

    These labels have already been seen painting WIDER than the box they were
    given -- an unmeasured span reported a 5.406px box around a glyph whose ink
    measured 0.0, and adding `whiteSpace: "nowrap"` to it flipped that to a
    6.0px ink run in the same 5.406px box. Judging clearance from the box alone
    would let a label overflow its own box onto the track and still read clean.
    """
    x0, y0, w, h = box(node)
    x1, y1 = x0 + w, y0 + h
    for entry in node.get("measured_text_boxes") or []:
        ix, iy, iw, ih = L.r(entry["rect"])
        if iw <= 0.0 or ih <= 0.0:
            continue
        x0, y0 = min(x0, ix), min(y0, iy)
        x1, y1 = max(x1, ix + iw), max(y1, iy + ih)
    return (x0, y0, x1 - x0, y1 - y0)


def text_of(dump, index):
    parts = [b.get("text", "") for b in (dump.nodes[index].get("measured_text_boxes") or [])]
    joined = " ".join(p for p in parts if p)
    return joined or "<no text>"


def descendants(dump, index):
    out = []
    for i in range(len(dump.nodes)):
        if i != index and index in dump.ancestors(i):
            out.append(i)
    return out


def plant(raw, dump, morph, kind, left):
    """Rewrite the dump in memory so the rule under test has to fire."""
    nodes = L.flatten(raw["nodes"])
    if kind == "overlap":
        # The pre-fix geometry exactly: the track's left edge on the left
        # label's left edge, everything drawn inside it carried along.
        dx = L.r(dump.nodes[left]["rect"])[0] - L.r(dump.nodes[morph]["rect"])[0]
        if abs(dx) < 0.001:
            sys.exit("plant 'overlap': the track is ALREADY on the left label, "
                     "so this control changes nothing and proves nothing")
        for i in [morph] + descendants(dump, morph):
            for rect in (nodes[i]["rect"], (nodes[i].get("clipping") or {}).get("rect")):
                if rect:
                    rect["x"] += dx
            for group in ("measured_text_boxes", "hit_regions"):
                for entry in nodes[i].get(group) or []:
                    entry["rect"]["x"] += dx
    elif kind == "crush":
        if abs(nodes[morph]["rect"]["w"] - CRUSHED_WIDTH) < 0.001:
            sys.exit("plant 'crush': the track is ALREADY crushed, so this "
                     "control changes nothing and proves nothing")
        nodes[morph]["rect"]["w"] = CRUSHED_WIDTH
    raw["nodes"] = nodes
    return raw


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="a SPECTR_LAYOUT_DUMP .layout.json")
    ap.add_argument("--depths", help="explicit .depths.json sidecar")
    ap.add_argument("--min-clearance", type=float, default=2.0,
                    help="px of background required between the track and each "
                         "flanking label (default 2.0; the row declares 6)")
    ap.add_argument("--expect-track-width", type=float, default=90.0)
    ap.add_argument("--track-width-tol", type=float, default=0.5)
    ap.add_argument("--plant", choices=("overlap", "crush"),
                    help="negative control -- the run MUST come back red")
    args = ap.parse_args()

    dump = L.Dump(args.dump, args.depths)
    if dump.depths is None or dump.parent is None:
        print("UNMEASURED: no .depths.json sidecar next to %s, so the morph "
              "row's parentage is unknown and no verdict is possible."
              % args.dump, file=sys.stderr)
        return 2

    morph = next((i for i, n in enumerate(dump.nodes)
                  if n.get("id") == MORPH_ID), None)
    if morph is None:
        print("UNMEASURED: %s carries no %r node -- this dump is of a surface "
              "that has no morph row, which is not the same as a clean one."
              % (args.dump, MORPH_ID), file=sys.stderr)
        return 2
    wrapper = dump.parent[morph]
    if wrapper is None:
        print("UNMEASURED: %r has no parent in the dump." % MORPH_ID, file=sys.stderr)
        return 2
    siblings = [i for i, p in enumerate(dump.parent) if p == wrapper and i != morph]
    if len(siblings) != 2:
        print("UNMEASURED: the morph row has %d flanking sibling(s), expected "
              "the 'A' and 'B' labels -- the row was restructured, so this "
              "detector is measuring something it does not understand."
              % len(siblings), file=sys.stderr)
        return 2

    centre = box(dump.nodes[morph])[0] + box(dump.nodes[morph])[2] / 2.0
    ordered = sorted(siblings, key=lambda i: box(dump.nodes[i])[0] + box(dump.nodes[i])[2] / 2.0)
    left, right = ordered
    if not (box(dump.nodes[left])[0] + box(dump.nodes[left])[2] / 2.0 < centre
            < box(dump.nodes[right])[0] + box(dump.nodes[right])[2] / 2.0):
        print("UNMEASURED: the two siblings do not straddle the track, so "
              "'left' and 'right' are not meaningful here.", file=sys.stderr)
        return 2

    if args.plant:
        raw = plant(json.load(open(args.dump)), dump, morph, args.plant, left)
        # A scratch directory, NOT next to the input: the committed fixtures
        # this runs against live in the repo, and a control that writes beside
        # them dirties the worktree every time the self-test runs.
        scratch_dir = tempfile.mkdtemp(prefix="morph-row-clearance-plant-")
        scratch = os.path.join(scratch_dir, "planted.layout.json")
        json.dump(raw, open(scratch, "w"))
        json.dump(dump.depths, open(os.path.join(scratch_dir, "planted.depths.json"), "w"))
        dump = L.Dump(scratch, None)

    track = box(dump.nodes[morph])
    lbox = painted_extent(dump.nodes[left])
    rbox = painted_extent(dump.nodes[right])

    print("== morph row clearance ==")
    print("dump      : %s%s" % (args.dump, "   PLANT=%s" % args.plant if args.plant else ""))
    print("surface   : %s   nodes %d" % (dump.surface, len(dump.nodes)))
    print("control   : %s straddled by %r and %r"
          % (MORPH_ID, text_of(dump, left), text_of(dump, right)))
    print("  left  label %s" % L.fmt_rect(lbox))
    print("  track       %s" % L.fmt_rect(track))
    print("  right label %s" % L.fmt_rect(rbox))

    bad = []
    left_gap = track[0] - (lbox[0] + lbox[2])
    right_gap = rbox[0] - (track[0] + track[2])
    print("clearance : left %.3fpx, right %.3fpx (floor %.2f)"
          % (left_gap, right_gap, args.min_clearance))
    for name, gap in (("left", left_gap), ("right", right_gap)):
        if gap < args.min_clearance:
            bad.append("CLEARANCE: the track comes within %.3fpx of the %s label "
                       "(floor %.2f)%s"
                       % (gap, name, args.min_clearance,
                          " -- it OVERLAPS it" if gap < 0 else ""))

    kids = descendants(dump, morph)
    print("contained : %d descendant(s) of the track" % len(kids))
    if not kids:
        print("UNMEASURED: the track has no descendants, so the containment "
              "claim below would pass vacuously.", file=sys.stderr)
        return 2
    for i in kids:
        kb = box(dump.nodes[i])
        if kb[2] <= 0.0:
            continue
        if kb[0] < track[0] - 0.5 or kb[0] + kb[2] > track[0] + track[2] + 0.5:
            bad.append("CONTAINED: %s escapes the track horizontally (%s vs %s)"
                       % (dump.label(i), L.fmt_rect(kb), L.fmt_rect(track)))

    print("width     : %.3fpx (expected %.2f +/- %.2f)"
          % (track[2], args.expect_track_width, args.track_width_tol))
    if abs(track[2] - args.expect_track_width) > args.track_width_tol:
        bad.append("UNCRUSHED: the track is %.3fpx wide, not %.2f -- the row "
                   "absorbed something by resizing the control"
                   % (track[2], args.expect_track_width))

    print("")
    if bad:
        for line in bad:
            print("FAIL: %s" % line, file=sys.stderr)
        return 1
    print("PASS: the morph track clears both flanking labels, nothing drawn "
          "inside it escapes, and it is still its full width.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
