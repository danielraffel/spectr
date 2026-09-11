#!/usr/bin/env python3
"""Detector B -- PAINTED-vs-MEASURED WIDTH (clipped / truncated text).

WHAT IT ASSERTS
    For every measurable text run in a `visual-layout-snapshot-v1` dump:

        painted_width  <=  the horizontal room the run actually has

    `painted_width` is `measured_text_boxes[].rect.w`, which the emitter fills
    from `Label::intrinsic_width()` -- the real shaped advance of the string
    under this label's font family / size / letter-spacing / text-transform
    cascade (core/view/src/widgets/label.cpp).  It is the SAME measurement the
    paint path uses, so it is the painted width, not a proxy for it.

    "Room" is evaluated twice, because two different things can eat glyphs:
      * the node's own layout box   -> rect.w
      * the effective clip rectangle -> clipping.rect (already the inherited
        clip intersected with the node's own rect when the node itself declares
        overflow hidden|scroll)

WHY A NODE BOX THAT IS TOO SMALL IS NOT AUTOMATICALLY TRUNCATION
    A View clips its own children only when overflow is hidden|scroll
    (View::apply_overflow_and_clip_path).  A Label with the default
    overflow:visible therefore PAINTS past its own box; the glyphs are only
    destroyed when some ancestor clip cuts them, or when the label opted into
    CSS text-overflow:ellipsis (Label::paint -> truncate_to_width).
    The dump does not carry the ellipsis flag, so this detector reports the two
    outcomes it CAN prove separately:

        CLIPPED   pixels are provably lost -- the run cannot fit inside the
                  effective clip rect at all, or does not fit at its measured
                  origin.  A human sees a chopped glyph or a missing word.
        OVERSET   the run is wider than its own layout box but the clip is wide
                  enough to paint it.  A human sees either an ellipsis (if the
                  label set text-overflow) or text spilling onto a neighbour.
                  Detector A (box_intersection.py) is what proves the spill
                  actually collides with something.

ALIGNMENT BLINDNESS (read this before trusting a CLIPPED verdict)
    `measured_text_boxes[].rect.x` is the NODE origin, not the ink origin.  The
    emitter writes `abs.x` and then overwrites only the width.  Text alignment
    is not in the dump, so where an over-wide run actually sits inside its box
    is unknown.  This detector evaluates all three CSS placements
    (left / center / right) and reports:
        cut_min  -- glyph px lost under the friendliest placement
        cut_max  -- glyph px lost under the harshest placement
    `cut_min > 0` means loss is GUARANTEED (no placement fits).  `cut_max > 0`
    with `cut_min == 0` means loss is placement-dependent -- reported at lower
    confidence.  Left-cut is a real outcome, not a theoretical one: a
    centre-aligned run that overflows loses glyphs off BOTH ends, which is how
    "dB (gain)" renders as "B (gain)".

WHAT IS NOT MEASURABLE (state this whenever you report a clean run)
    * `Label::intrinsic_width()` returns 0 for empty text AND for any
      multi_line_ label.  A 0-width box carries no painted-width signal, so
      multi-line labels are UNMEASURABLE here, not clean.
    * TextEditor / TextButton / HyperlinkButton boxes are written as
      `add_text_box(..., abs.width, abs.height)` -- the box IS the node rect, so
      `painted == node` tautologically.  Excluded.
    * A node whose effective clip has zero WIDTH was never painted (a closed
      panel that Yoga collapsed to 0).  Excluded, counted as `not_painted`.
    * The node rect includes padding, so a label with horizontal padding has
      less text room than rect.w suggests: this detector UNDER-reports.

SCROLL CAVEAT
    dump_layout_tree reports descendant rects of a ScrollView in UNSCROLLED
    CONTENT space (ScrollView::layout_children expands to content_size; the
    scroll offset is applied at paint time via canvas.translate).  This detector
    is unaffected on the X axis in the common vertical-scroll case, and every
    comparison it makes is between a node and its OWN box/clip -- both of which
    live in the same space -- so the WIDTH verdict stays exact.  What scroll
    does break is the y-extent of the clip: a run below the fold reports a
    zero-height clip overlap.  Those are reported separately as `below_fold`
    rather than silently mixed into the width findings.

EXIT CODES
    0 clean   1 findings   2 usage/IO   3 self-test control failed to fire
"""

import argparse
import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from layout_common import Dump, fmt_rect, overlap_1d, r  # noqa: E402

MEASURABLE_KINDS = ("Label",)
TAUTOLOGICAL_KINDS = ("TextEditor", "TextButton", "HyperlinkButton")


def candidates(dump, tol):
    """Yield (index, node, box, painted_w, node_rect, clip_rect) for measurable runs."""
    for i, n in enumerate(dump.nodes):
        for b in n.get("measured_text_boxes") or []:
            yield i, n, b


def analyse(dump, tol):
    findings = []
    stats = {
        "text_runs": 0,
        "measured": 0,
        "unmeasurable_zero_width": 0,
        "tautological_kind": 0,
        "not_painted": 0,
        "invisible": 0,
        "below_fold": 0,
    }
    for i, n, b in candidates(dump, tol):
        stats["text_runs"] += 1
        kind = n.get("kind", "")
        if kind in TAUTOLOGICAL_KINDS:
            stats["tautological_kind"] += 1
            continue
        if kind not in MEASURABLE_KINDS:
            stats["tautological_kind"] += 1
            continue
        painted_w = float(b.get("rect", {}).get("w", 0.0))
        if painted_w <= 0.0:
            stats["unmeasurable_zero_width"] += 1
            continue
        if not dump.effectively_visible(i):
            stats["invisible"] += 1
            continue

        nx, ny, nw, nh = r(n.get("rect") or {})
        cx, cy, cw, ch = r((n.get("clipping") or {}).get("rect") or {})
        if cw <= 0.0:
            # Effective clip has no width: this subtree was never painted.
            stats["not_painted"] += 1
            continue

        stats["measured"] += 1

        # Vertical: a run whose clip does not overlap it in y is below (or above)
        # the fold of a scroll container -- report separately, never as width.
        y_vis = overlap_1d(ny, nh, cy, ch)
        below = y_vis <= 0.0
        if below:
            stats["below_fold"] += 1

        # Horizontal placements of an over-wide run inside its own box.
        left = nx
        right = nx + nw - painted_w
        center = nx + (nw - painted_w) * 0.5
        cuts = {}
        for name, ox in (("left", left), ("center", center), ("right", right)):
            visible = overlap_1d(ox, painted_w, cx, cw)
            cuts[name] = max(0.0, painted_w - visible)
        cut_min = min(cuts.values())
        cut_max = max(cuts.values())

        box_deficit = painted_w - nw

        if cut_min > tol:
            cls, conf = "CLIPPED", "guaranteed"
        elif cut_max > tol:
            cls, conf = "CLIPPED", "placement-dependent"
        elif box_deficit > tol:
            cls, conf = "OVERSET", "box-only"
        else:
            continue

        findings.append(
            {
                "index": i,
                "label": dump.label(i),
                "text": b.get("text", ""),
                "painted_w": painted_w,
                "node_rect": (nx, ny, nw, nh),
                "clip_rect": (cx, cy, cw, ch),
                "box_deficit": box_deficit,
                "cut_min": cut_min,
                "cut_max": cut_max,
                "cuts": cuts,
                "class": cls,
                "confidence": conf,
                "below_fold": below,
                "scroll_frame": dump.scroll_frame(i),
            }
        )

    findings.sort(key=lambda f: (-f["cut_min"], -f["box_deficit"]))
    return findings, stats


def report(dump, findings, stats, tol, quiet=False):
    print("dump      : %s" % dump.path)
    print("depths    : %s" % (dump.depths_path or "(none -- ancestry unavailable)"))
    print("viewport  : %s" % json.dumps(dump.viewport))
    print("tolerance : %.2f px" % tol)
    print(
        "measured  : %d of %d text runs "
        "(skipped: %d zero-width/multi-line, %d tautological kind, "
        "%d never painted, %d invisible)"
        % (
            stats["measured"],
            stats["text_runs"],
            stats["unmeasurable_zero_width"],
            stats["tautological_kind"],
            stats["not_painted"],
            stats["invisible"],
        )
    )
    print("below-fold: %d measured runs sit outside their scroll clip in y" % stats["below_fold"])
    print("")
    if not findings:
        print("NO TRUNCATION/OVERSET FOUND (%d runs measured)." % stats["measured"])
        print("This is only meaningful next to a positive control: run --self-test.")
        print("")
        print(
            "COVERAGE: %d of %d text runs adjudicated (%d unmeasurable), 0 finding(s)"
            % (stats["measured"], stats["text_runs"],
               stats["text_runs"] - stats["measured"])
        )
        return
    print("%d FINDING(S):" % len(findings))
    for f in findings:
        print("")
        print(
            "  [%s / %s] %s  %r"
            % (f["class"], f["confidence"], f["label"], f["text"])
        )
        print(
            "     painted=%.1f px   node box w=%.1f (deficit %+.1f)   clip %s"
            % (f["painted_w"], f["node_rect"][2], f["box_deficit"], fmt_rect(f["clip_rect"]))
        )
        print("     node   %s" % fmt_rect(f["node_rect"]))
        print(
            "     glyph px lost: left-align %.1f  center %.1f  right-align %.1f"
            "   -> min %.1f / max %.1f"
            % (
                f["cuts"]["left"],
                f["cuts"]["center"],
                f["cuts"]["right"],
                f["cut_min"],
                f["cut_max"],
            )
        )
        if f["below_fold"]:
            print("     NOTE below the fold in its scroll frame (%s) -- width verdict still valid" % f["scroll_frame"])
    # Repeat the coverage at the END as well as the top. This output is
    # routinely read through a `tail`, which drops the header -- and the header
    # is the only place that says most of the surface was never adjudicated.
    print("")
    print(
        "COVERAGE: %d of %d text runs adjudicated (%d unmeasurable), %d finding(s)"
        % (stats["measured"], stats["text_runs"],
           stats["text_runs"] - stats["measured"], len(findings))
    )


def self_test(dump_path, depths_path, out_prefix, tol, shrink):
    """Positive control: shrink one real, currently-fitting label's box + clip.

    Picks a measurable Label whose text CURRENTLY fits, narrows its node rect
    and its clip rect so the shaped run provably cannot fit, writes the mutated
    dump, and re-runs the WHOLE detector on it.  If the detector does not report
    that node as CLIPPED, the detector is broken.
    """
    dump = Dump(dump_path, depths_path)
    victim = None
    for i, n in enumerate(dump.nodes):
        if n.get("kind") not in MEASURABLE_KINDS:
            continue
        for b in n.get("measured_text_boxes") or []:
            pw = float(b.get("rect", {}).get("w", 0.0))
            _, _, nw, _ = r(n.get("rect") or {})
            _, _, cw, _ = r((n.get("clipping") or {}).get("rect") or {})
            if pw > 20.0 and cw > 0.0 and pw <= nw + tol and dump.effectively_visible(i):
                victim = (i, pw)
                break
        if victim:
            break
    if victim is None:
        print("CONTROL BROKEN: no fitting measurable label to shrink in %s" % dump_path)
        return 3

    vi, pw = victim
    with open(dump_path) as fh:
        raw = json.load(fh)
    flat_raw = raw["nodes"]
    node = flat_raw[vi]
    target_w = max(1.0, pw - shrink)
    node["rect"]["w"] = target_w
    node.setdefault("clipping", {}).setdefault("rect", {})
    node["clipping"]["rect"] = {
        "x": node["rect"]["x"],
        "y": node["rect"]["y"],
        "w": target_w,
        "h": node["rect"]["h"],
    }

    out_dump = out_prefix + ".layout.json"
    with open(out_dump, "w") as fh:
        json.dump(raw, fh)
    if dump.depths_path:
        with open(dump.depths_path) as fh:
            d = fh.read()
        with open(out_prefix + ".depths.json", "w") as fh:
            fh.write(d)

    orig_w = float(dump.nodes[vi]["rect"]["w"])
    print("CONTROL: narrowed %s box+clip from %.1f to %.1f px "
          "while its shaped run needs %.1f px"
          % (dump.label(vi), orig_w, target_w, pw))
    print("CONTROL dump: %s" % out_dump)
    print("")
    ctl = Dump(out_dump)
    findings, stats = analyse(ctl, tol)
    report(ctl, findings, stats, tol)
    hit = [f for f in findings if f["index"] == vi and f["class"] == "CLIPPED"]
    print("")
    if hit:
        print("CONTROL FIRED: %s reported CLIPPED, %.1f px of glyphs lost."
              % (hit[0]["label"], hit[0]["cut_min"]))
        print("-> the detector can go RED.")
        return 0
    print("CONTROL DID NOT FIRE -- the detector is broken. Do not trust a clean run.")
    return 3


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump", help="path to a *.layout.json snapshot")
    ap.add_argument("--depths", help="explicit .depths.json sidecar")
    ap.add_argument("--tolerance", type=float, default=0.5,
                    help="px slack; matches LayoutTreeTolerance::text_box_px (default 0.5)")
    ap.add_argument("--self-test", metavar="PREFIX",
                    help="run the positive control, writing PREFIX.layout.json")
    ap.add_argument("--shrink", type=float, default=12.0,
                    help="px to remove from the victim box in --self-test")
    args = ap.parse_args(argv)

    if not os.path.exists(args.dump):
        print("no such dump: %s" % args.dump, file=sys.stderr)
        return 2

    if args.self_test:
        return self_test(args.dump, args.depths, args.self_test, args.tolerance, args.shrink)

    dump = Dump(args.dump, args.depths)
    findings, stats = analyse(dump, args.tolerance)
    report(dump, findings, stats, args.tolerance)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
