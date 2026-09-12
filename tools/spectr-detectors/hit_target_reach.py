#!/usr/bin/env python3
"""Assert a control's hit target covers what it paints, and is big enough to hit.

THE INVARIANT

    Three separate reports -- a Settings toggle whose knob did nothing, SNAPSHOT
    A/B that "won't tap", a hover-grown slider thumb that is hard to grab -- are
    one property: **the region a POINTER reaches must match the region the EYE
    sees**.  This asserts both halves of it on a `visual-layout-snapshot-v1`
    dump:

      REACH   every control's hit rect contains every pixel it paints, its own
              children included.  A slider thumb is `pointerEvents: none`, so the
              TRACK owns the press -- and the hovered 18px thumb sits 9px past
              each end of travel and 1px above and below a 16px track.  Half of
              what the user aims at was outside the only pressable rect.

      SIZE    no control's hit rect is under MIN design px in either axis.  The
              authored box is 1320x860 and the shipping standalone paints it into
              990x645, so every design number here is 0.75 of what the pointer
              sees: a 26px-tall button is 19.5pt, and a 16px-tall slider track is
              12pt.  38 design px is 28.5pt, the HIG figure for a comfortable
              pointer target.

      NEIGHBOURS  no two controls' hit rects overlap.  Growing one target must
              not steal presses from the one beside it, and an invisible hit area
              is exactly the kind of change where that goes unnoticed.

WHAT IT CANNOT SEE

    Whether the press, once delivered, does anything.  A control can satisfy
    every rule here and still have no handler, or a disabled one -- the SNAPSHOT
    recall buttons refuse a press by design while their slot is empty, and that
    is invisible to a geometry assertion.  Reach is necessary, never sufficient.

    It also reads a DUMP, so it says nothing about `pointerEvents` routing: a
    child that swallows a press has a hit rect that looks perfectly nested.  That
    half is proved by `Spectr-native-shot`'s SPECTR_HIT_PROBE, which resolves who
    actually owns a point.

    Controls are found STRUCTURALLY, never by generated node id: a toggle is a
    40x20-ish node owning one square child, a slider track is a node 14-20 tall
    and at least 80 wide owning a square child 12-20 across.  A population found
    by guessed ids cannot report a control it was never told about.

    hit_target_reach.py DUMP [--min 38] [--plant]

`--plant` is the negative control: it shrinks every hit rect back onto its
painted rect, which is the exact state this lane found.  A detector that still
passes under `--plant` is measuring nothing, so the plant run MUST fail.

Exit: 0 clean, 1 findings, 2 usage/IO error, 3 the population came back empty
(the detector is measuring the wrong surface, not reporting an absence).
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout_common as L  # noqa: E402

MIN_DEFAULT = 38.0

# One control is knowingly out of this detector's scope, and it is named here
# rather than quietly skipped by a size or shape filter that would also hide a
# real regression. `spectr-snapshot-morph`'s track style is the exact literal
# that tools/patch_materialized_morph_affordance.py matches on; editing it would
# make that script's replay exit 1 at a patch point it can no longer find, and
# those scripts are what let concurrent branches merge. Its numbers are PRINTED
# on every run, so the gap is a standing statement rather than an absence.
KNOWN_GAPS = {
    "spectr-snapshot-morph":
        "track style is tools/patch_materialized_morph_affordance.py's patch "
        "point; a second writer there breaks that script's replay",
}
# Design box -> shipping window. Reported alongside every design number so a
# reader never has to remember the factor.
WINDOW_SCALE = 990.0 / 1320.0


def hit_rect(node):
    regions = node.get("hit_regions") or []
    if not regions:
        return None
    return L.r(regions[0]["rect"])


def square_child(dump, i):
    """The one square child that makes a node a toggle or a slider track."""
    if dump.parent is None:
        return None
    for j, node in enumerate(dump.nodes):
        if dump.parent[j] != i:
            continue
        rect = L.r(node["rect"])
        if abs(rect[2] - rect[3]) < 0.51 and 12.0 <= rect[2] <= 20.0:
            return j
    return None


# The four SNAPSHOT buttons carry AUTHORED ids, not generated ones, so naming
# them is not the "guessed id list" hazard -- and the population is asserted
# below, so a rename shrinks the census loudly instead of silently.
SNAPSHOT_BUTTONS = ("spectr-snapshot-capture-a", "spectr-snapshot-capture-b",
                    "spectr-snapshot-recall-a", "spectr-snapshot-recall-b")


def classify(dump, i):
    rect = L.r(dump.nodes[i]["rect"])
    w, h = rect[2], rect[3]
    if hit_rect(dump.nodes[i]) is None:
        return None
    if not dump.effectively_visible(i):
        return None
    if (dump.nodes[i].get("id") or "") in SNAPSHOT_BUTTONS:
        return "button"
    if square_child(dump, i) is None:
        return None
    if 36.0 <= w <= 44.0 and 18.0 <= h <= 22.0:
        return "toggle"
    if 14.0 <= h <= 20.0 and 80.0 <= w <= 200.0:
        return "slider"
    return None


def contains(outer, inner, tol=0.01):
    return (inner[0] >= outer[0] - tol
            and inner[1] >= outer[1] - tol
            and inner[0] + inner[2] <= outer[0] + outer[2] + tol
            and inner[1] + inner[3] <= outer[1] + outer[3] + tol)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", help="a visual-layout-snapshot-v1 .layout.json")
    ap.add_argument("--depths", help="explicit .depths.json sidecar")
    ap.add_argument("--min", type=float, default=MIN_DEFAULT,
                    help="minimum hit extent in design px (default %.0f)"
                         % MIN_DEFAULT)
    ap.add_argument("--plant", action="store_true",
                    help="negative control: shrink every hit rect onto its "
                         "painted rect, which must make this fail")
    args = ap.parse_args()

    try:
        dump = L.Dump(args.dump, args.depths)
    except (OSError, ValueError) as exc:
        sys.exit("cannot read %s: %s" % (args.dump, exc))
    if dump.schema != "visual-layout-snapshot-v1":
        sys.exit("%s: unexpected schema %r" % (args.dump, dump.schema))
    if dump.parent is None:
        sys.exit("%s: no .depths.json sidecar, so ancestry is unknown and "
                 "neighbour overlap cannot be told from nesting" % args.dump)

    if args.plant:
        for node in dump.nodes:
            regions = node.get("hit_regions") or []
            for region in regions:
                region["rect"] = dict(node["rect"])

    controls = []
    for i in range(len(dump.nodes)):
        kind = classify(dump, i)
        if kind is not None:
            controls.append((i, kind))

    # A population of zero is not a clean run. It is the detector pointed at the
    # wrong surface -- and that reads identically to "everything is fine".
    if not controls:
        print("CONTROL FAILED: no toggle or slider track in this dump. The "
              "detector is measuring the wrong surface, not reporting an "
              "absence.")
        return 3
    kinds = {}
    for _, kind in controls:
        kinds[kind] = kinds.get(kind, 0) + 1
    # If the transport row is in this dump at all, all four SNAPSHOT buttons
    # must be. A partial census is a renamed or dropped control, which is a
    # finding -- not a smaller clean run.
    present = {dump.nodes[i].get("id") for i, k in controls if k == "button"}
    if present and len(present) != len(SNAPSHOT_BUTTONS):
        print("CONTROL FAILED: %d of %d SNAPSHOT buttons found (%s). The "
              "population moved; this census is not comparable."
              % (len(present), len(SNAPSHOT_BUTTONS), ", ".join(sorted(present))))
        return 3
    print("control: %d control(s) found -- %s"
          % (len(controls), ", ".join("%s x%d" % kv for kv in sorted(kinds.items()))))

    findings = []
    gaps = []

    for i, kind in controls:
        node = dump.nodes[i]
        paint = L.r(node["rect"])
        hit = hit_rect(node)
        name = dump.label(i)
        node_id = node.get("id") or ""

        if node_id in KNOWN_GAPS:
            gaps.append("%-8s %s hit %.1fx%.1f (%.1fx%.1f pt) -- %s"
                        % (kind, name, hit[2], hit[3],
                           hit[2] * WINDOW_SCALE, hit[3] * WINDOW_SCALE,
                           KNOWN_GAPS[node_id]))
            continue

        if not contains(hit, paint):
            findings.append("REACH  %-8s %s hit %s does not contain its own "
                            "painted box %s"
                            % (kind, name, L.fmt_rect(hit), L.fmt_rect(paint)))

        # The painted thumb is the pixels the user actually aims at, and it
        # leaves the track by design. It is pointerEvents:none, so the TRACK's
        # rect is the only thing that can accept that press.
        j = square_child(dump, i)
        if j is not None:
            thumb = L.r(dump.nodes[j]["rect"])
            if not contains(hit, thumb):
                findings.append(
                    "REACH  %-8s %s hit %s does not contain its painted thumb "
                    "%s" % (kind, name, L.fmt_rect(hit), L.fmt_rect(thumb)))

        if hit[2] < args.min - 0.01 or hit[3] < args.min - 0.01:
            findings.append(
                "SIZE   %-8s %s hit is %.1fx%.1f design px (%.1fx%.1f pt at "
                "990x645); the floor is %.0f (%.1f pt)"
                % (kind, name, hit[2], hit[3], hit[2] * WINDOW_SCALE,
                   hit[3] * WINDOW_SCALE, args.min, args.min * WINDOW_SCALE))

    # Neighbours. Only pairs in the SAME scroll frame are comparable -- the dump
    # reports rects without the scroll offset, so a node inside a scroll
    # container and one outside it are in different spaces.
    skipped = 0
    compared = 0
    for a in range(len(controls)):
        ia, ka = controls[a]
        for b in range(a + 1, len(controls)):
            ib, kb = controls[b]
            if dump.is_related(ia, ib):
                continue
            compared += 1
            if dump.scroll_frame(ia) != dump.scroll_frame(ib):
                skipped += 1
                continue
            ra, rb = hit_rect(dump.nodes[ia]), hit_rect(dump.nodes[ib])
            ox, oy = L.intersect(ra, rb)
            if ox > 0.01 and oy > 0.01:
                findings.append(
                    "NEIGHBOUR %s %s and %s %s overlap by %.1fx%.1f px"
                    % (ka, dump.label(ia), kb, dump.label(ib), ox, oy))
    if skipped:
        print("note: %d pair(s) skipped as cross-scroll-frame (not comparable)"
              % skipped)
    # A neighbour check that compared nothing is not a clean neighbour check.
    print("control: %d control pair(s) actually compared for overlap" % compared)
    if compared == 0 and len(controls) > 1:
        print("CONTROL FAILED: more than one control, yet no pair was "
              "comparable. The overlap arm proved nothing.")
        return 3
    if gaps:
        print("\n%d KNOWN GAP(S) -- measured, named, NOT adjudicated here:"
              % len(gaps))
        for line in gaps:
            print("  " + line)

    if findings:
        print("\n%d finding(s):" % len(findings))
        for line in findings:
            print("  " + line)
        return 1
    print("OK: every control's hit rect contains its paint, clears %.0f design "
          "px (%.1f pt), and overlaps no neighbour."
          % (args.min, args.min * WINDOW_SCALE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
