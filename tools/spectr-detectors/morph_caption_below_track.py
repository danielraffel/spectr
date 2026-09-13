#!/usr/bin/env python3
"""Assert the morph slider explains itself BESIDE the control, never inside it.

The morph slider is unavailable until both snapshot slots are filled, and the
shipping UI says so.  For a while it said so in the wrong place: the sentence
was painted across the inside of the slider's own 90x16 groove, so a
half-configured control read as

    A ---- SET B ---- B

Nothing was broken -- the text rendered exactly where it was told to -- but a
line of instructional type running through a slider track reads as a rendering
fault rather than as guidance.  Reported as "it seems like a bug the way it's
displayed I like the intent".

The Pulp Design System settles it.  Its disabled state is `disabled . 42` --
OPACITY ONLY, with no instructional text on the control -- and its captions are
mono / 10px / faint.  So the groove now carries nothing but the slider, and the
sentence is a caption under the row.

WHY THIS NEEDS A DETECTOR OF ITS OWN

`morph_row_clearance.py` states its rule on the track's HORIZONTAL neighbours
and says nothing about what is drawn under the row.  `box_intersection.py`
compares text-bearing nodes and would have reported the caption-on-track pair
-- it did, once, as a 5.4x12px finding that was waved through as pre-existing
decoration.  And no pixel comparison can adjudicate this at all: the defect
rendered perfectly, so a diff against the shipped build scores it identical.

The rule is therefore stated on BOXES and on INK, both, because either alone
has already produced a false verdict here:

  NO TEXT     no descendant of the track paints any text at all.  This is
              the design system's rule stated directly -- "no instructional
              text on the control" -- and it needs no knowledge of which node
              the caption is.
  BELOW       the caption's box starts at or under the track's bottom edge,
              and does not intersect the track at all.  This is the same
              defect seen from the caption's side: a caption re-parented out
              of the track but still positioned over it is still on the
              control.
  BOXED       the caption has a real layout box -- non-zero on both axes --
              that CONTAINS its own ink.  `whiteSpace: "nowrap"` alone has
              already made ink measurable on this very row while the layout
              box stayed its old size, which is a persuasive false fix: the
              glyphs appear, and the element still occupies nothing.
  INKED       the caption actually paints non-empty text.  A box with no ink
              is a caption nobody can read.
  ALIGNED     the caption starts on the control group's own leading edge, so
              it reads as that control's caption and not as a stray label.
  UNMOVED     the track is still its authored 90x16.  The caption is out of
              flow precisely so the control does not move to make room for
              it; if the track has been resized, it did.

MEASURED is the positive control: the dump must carry the track, the caption,
and the group's leading label.  A dump missing any of them is UNMEASURED
(exit 2) and never a pass -- a detector pointed at an enabled morph, or at a
surface with no transport bar, reports clean by saying nothing.

Plants (negative controls).  Four, one per claim, each touching ONE thing so
no claim can be passing on another's strength; every one MUST redden the run:
  --plant text-in-groove  give a node inside the track some ink, the way the
                          pre-fix caption did.  Fires NO TEXT, nothing else.
  --plant in-groove       move the caption's box and ink up onto the track at
                          the pre-fix geometry.  Fires BELOW, nothing else.
  --plant nowrap-only     leave the ink exactly where it is and collapse only
                          the layout BOX -- the false fix named above.  Fires
                          BOXED, nothing else.
  --plant drift           slide the caption off the group's leading edge.
                          Fires ALIGNED, nothing else.

THE CAPTION IS RESOLVED STRUCTURALLY, not by a `data-` attribute: the layout
dump carries `id`, `kind`, `rect` and measured text and NO React props, so
`data-spectr-morph-hint` is invisible here.  The caption is "the child of the
morph group that is not the row holding the track", which is a stronger handle
anyway -- it proves the caption is the track's uncle rather than trusting an
attribute that says so.

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
TOL = 0.5


def box(node):
    return L.r(node["rect"])


def ink(node):
    """The union of the node's measured text boxes, or None when it paints none."""
    x0 = y0 = None
    x1 = y1 = None
    for entry in node.get("measured_text_boxes") or []:
        ix, iy, iw, ih = L.r(entry["rect"])
        if iw <= 0.0 or ih <= 0.0:
            continue
        x0 = ix if x0 is None else min(x0, ix)
        y0 = iy if y0 is None else min(y0, iy)
        x1 = ix + iw if x1 is None else max(x1, ix + iw)
        y1 = iy + ih if y1 is None else max(y1, iy + ih)
    if x0 is None:
        return None
    return (x0, y0, x1 - x0, y1 - y0)


def text_of(node):
    parts = [b.get("text", "") for b in (node.get("measured_text_boxes") or [])]
    return "".join(p for p in parts if p)


def descendants(dump, index):
    return [i for i in range(len(dump.nodes))
            if i != index and index in dump.ancestors(i)]


def plant(raw, dump, hint, track, kind):
    nodes = L.flatten(raw["nodes"])
    hbox, tbox = box(dump.nodes[hint]), box(dump.nodes[track])
    if kind == "text-in-groove":
        kids = descendants(dump, track)
        if not kids:
            sys.exit("plant 'text-in-groove': the track has no descendants to "
                     "put ink on, so this control proves nothing")
        victim = kids[0]
        if nodes[victim].get("measured_text_boxes"):
            sys.exit("plant 'text-in-groove': that node ALREADY paints text, "
                     "so this control changes nothing")
        nodes[victim]["measured_text_boxes"] = [{
            "text": "SET A + B",
            "rect": {"x": tbox[0] + 19.0, "y": tbox[1] + 1.0,
                     "w": 52.0, "h": 12.0},
        }]
    elif kind == "in-groove":
        # The pre-fix geometry: the caption spanned the track and sat 1px in.
        dx = tbox[0] - hbox[0]
        dy = (tbox[1] + 1.0) - hbox[1]
        if abs(dx) < 0.001 and abs(dy) < 0.001:
            sys.exit("plant 'in-groove': the caption is ALREADY on the track, "
                     "so this control changes nothing and proves nothing")
        for rect in (nodes[hint]["rect"],
                     (nodes[hint].get("clipping") or {}).get("rect")):
            if rect:
                rect["x"] += dx
                rect["y"] += dy
        for entry in nodes[hint].get("measured_text_boxes") or []:
            entry["rect"]["x"] += dx
            entry["rect"]["y"] += dy
    elif kind == "nowrap-only":
        # Ink untouched, layout box collapsed: the element renders its glyphs
        # and occupies nothing, which is exactly how this failed before.
        if nodes[hint]["rect"]["w"] <= 0.001:
            sys.exit("plant 'nowrap-only': the caption's box is ALREADY "
                     "collapsed, so this control proves nothing")
        nodes[hint]["rect"]["w"] = 0.0
    elif kind == "drift":
        nodes[hint]["rect"]["x"] += 24.0
        for entry in nodes[hint].get("measured_text_boxes") or []:
            entry["rect"]["x"] += 24.0
    raw["nodes"] = nodes
    return raw


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="a SPECTR_LAYOUT_DUMP .layout.json")
    ap.add_argument("--depths", help="explicit .depths.json sidecar")
    ap.add_argument("--expect-track", type=float, nargs=2, default=(90.0, 16.0),
                    metavar=("W", "H"))
    ap.add_argument("--plant", choices=("text-in-groove", "in-groove",
                                        "nowrap-only", "drift"),
                    help="negative control -- the run MUST come back red")
    args = ap.parse_args()

    dump = L.Dump(args.dump, args.depths)
    if dump.depths is None or dump.parent is None:
        print("UNMEASURED: no .depths.json sidecar next to %s, so the morph "
              "group's parentage is unknown." % args.dump, file=sys.stderr)
        return 2

    track = next((i for i, n in enumerate(dump.nodes)
                  if n.get("id") == MORPH_ID), None)
    if track is None:
        print("UNMEASURED: %s carries no %r node -- this is a dump of a "
              "surface with no morph row, which is not a clean one."
              % (args.dump, MORPH_ID), file=sys.stderr)
        return 2

    # The group's leading edge: the "A" label, i.e. the first flanking sibling
    # of the track. Resolved rather than assumed, so a restructured row reports
    # UNMEASURED instead of a verdict about the wrong node.
    row = dump.parent[track]
    if row is None:
        print("UNMEASURED: %r has no parent in the dump." % MORPH_ID,
              file=sys.stderr)
        return 2
    flanking = [i for i, p in enumerate(dump.parent) if p == row and i != track]
    if len(flanking) != 2:
        print("UNMEASURED: the morph row has %d flanking sibling(s), expected "
              "the 'A' and 'B' labels -- the row was restructured."
              % len(flanking), file=sys.stderr)
        return 2
    lead = min(flanking, key=lambda i: box(dump.nodes[i])[0])

    # THE NO-TEXT CLAIM IS EVALUATED FIRST, and deliberately so: it needs only
    # the track, so it can return a verdict on a document whose morph control
    # is not the two-level group the caption claims below understand. The
    # pre-fix capture is exactly such a document -- its caption lived INSIDE
    # the track, so the group has no caption sibling at all -- and reporting
    # the shipped defect as UNMEASURED would have made the one real red
    # fixture unusable.
    kids = descendants(dump, track)
    inked_kids = [i for i in kids if ink(dump.nodes[i]) is not None]
    if not kids:
        print("UNMEASURED: the track has no descendants, so the no-text claim "
              "would pass vacuously.", file=sys.stderr)
        return 2
    in_groove = ["NO TEXT: %s paints %r inside the control -- the disabled "
                 "state is opacity only, with no instructional text on the "
                 "track" % (dump.label(i), text_of(dump.nodes[i]))
                 for i in inked_kids]

    outer = dump.parent[row]
    captions = ([i for i, p in enumerate(dump.parent) if p == outer and i != row]
                if outer is not None else [])
    if len(captions) != 1:
        if in_groove:
            print("== morph caption below the track ==")
            print("dump      : %s" % args.dump)
            print("surface   : %s   nodes %d" % (dump.surface, len(dump.nodes)))
            print("  track       %s" % L.fmt_rect(box(dump.nodes[track])))
            print("no text   : %d descendant(s) of the track, %d painting text"
                  % (len(kids), len(inked_kids)))
            print("")
            for line in in_groove:
                print("FAIL: %s" % line, file=sys.stderr)
            return 1
        print("UNMEASURED: the morph group has %d child/children beside the "
              "row, expected exactly the caption. A morph with BOTH slots "
              "filled correctly renders none -- that is a dump of the ENABLED "
              "control, not a verdict about the disabled one."
              % len(captions), file=sys.stderr)
        return 2
    hint = captions[0]

    if args.plant:
        raw = plant(json.load(open(args.dump)), dump, hint, track, args.plant)
        scratch_dir = tempfile.mkdtemp(prefix="morph-caption-plant-")
        scratch = os.path.join(scratch_dir, "planted.layout.json")
        json.dump(raw, open(scratch, "w"))
        json.dump(dump.depths,
                  open(os.path.join(scratch_dir, "planted.depths.json"), "w"))
        dump = L.Dump(scratch, None)

    tbox = box(dump.nodes[track])
    hbox = box(dump.nodes[hint])
    hink = ink(dump.nodes[hint])
    lbox = box(dump.nodes[lead])

    print("== morph caption below the track ==")
    print("dump      : %s%s" % (args.dump,
                                "   PLANT=%s" % args.plant if args.plant else ""))
    print("surface   : %s   nodes %d" % (dump.surface, len(dump.nodes)))
    print("  group lead  %s" % L.fmt_rect(lbox))
    print("  track       %s" % L.fmt_rect(tbox))
    print("  caption box %s" % L.fmt_rect(hbox))
    print("  caption ink %s  %r"
          % (L.fmt_rect(hink) if hink else "<none>", text_of(dump.nodes[hint])))

    # Re-read under the plant, which may have put ink inside the track.
    kids = descendants(dump, track)
    inked_kids = [i for i in kids if ink(dump.nodes[i]) is not None]
    print("no text   : %d descendant(s) of the track, %d painting text"
          % (len(kids), len(inked_kids)))
    bad = ["NO TEXT: %s paints %r inside the control -- the disabled state is "
           "opacity only, with no instructional text on the track"
           % (dump.label(i), text_of(dump.nodes[i])) for i in inked_kids]

    # BELOW -- the same defect, from the caption's side.
    gap = hbox[1] - (tbox[1] + tbox[3])
    print("below     : caption top is %.3fpx under the track's bottom edge" % gap)
    if gap < -TOL:
        bad.append("BELOW: the caption starts %.3fpx ABOVE the track's bottom "
                   "edge -- it is painted on the control, not beside it" % -gap)
    # `L.intersect` returns an (x-overlap, y-overlap) PAIR, which is a tuple
    # and therefore always truthy -- testing it directly reports every caption
    # as overlapping, including one measurably 2px clear of the track.
    ox, oy = L.intersect(hbox, tbox)
    if ox > TOL and oy > TOL:
        bad.append("BELOW: the caption's box overlaps the track's by "
                   "%.3fx%.3fpx (%s vs %s)"
                   % (ox, oy, L.fmt_rect(hbox), L.fmt_rect(tbox)))

    # BOXED -- a real box, containing its own ink.
    if hbox[2] <= TOL or hbox[3] <= TOL:
        bad.append("BOXED: the caption's layout box is %s -- it paints but "
                   "occupies nothing" % L.fmt_rect(hbox))
    elif hink is not None:
        if (hink[0] < hbox[0] - TOL or hink[0] + hink[2] > hbox[0] + hbox[2] + TOL
                or hink[1] < hbox[1] - TOL
                or hink[1] + hink[3] > hbox[1] + hbox[3] + TOL):
            bad.append("BOXED: the caption's ink %s escapes its layout box %s "
                       "-- the box never grew to hold the text"
                       % (L.fmt_rect(hink), L.fmt_rect(hbox)))

    # INKED.
    if hink is None:
        bad.append("INKED: the caption paints no measurable text")
    elif not text_of(dump.nodes[hint]).strip():
        bad.append("INKED: the caption's text is empty")

    # ALIGNED.
    drift = abs(hbox[0] - lbox[0])
    print("aligned   : caption leading edge is %.3fpx off the group's" % drift)
    if drift > 1.0:
        bad.append("ALIGNED: the caption starts %.3fpx off the control "
                   "group's leading edge (%.3f vs %.3f)"
                   % (drift, hbox[0], lbox[0]))

    # UNMOVED.
    want_w, want_h = args.expect_track
    print("unmoved   : track is %.3fx%.3f (expected %.2fx%.2f)"
          % (tbox[2], tbox[3], want_w, want_h))
    if abs(tbox[2] - want_w) > TOL or abs(tbox[3] - want_h) > TOL:
        bad.append("UNMOVED: the track is %.3fx%.3f, not %.2fx%.2f -- the row "
                   "resized the control to make room for the caption"
                   % (tbox[2], tbox[3], want_w, want_h))

    print("")
    if bad:
        for line in bad:
            print("FAIL: %s" % line, file=sys.stderr)
        return 1
    print("PASS: the morph caption sits below the track in a real box that "
          "holds its own ink, on the control group's leading edge, and the "
          "control did not move to make room for it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
