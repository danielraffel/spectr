#!/usr/bin/env python3
"""DETECTOR A -- overlapping text.

Asserts the visual property directly: two strings that are both painted must not
occupy the same pixels.  Overlapping text is unreadable, so any intersecting
pair of painted text rects is a defect.

It compares rects taken from `measured_text_boxes` (mode `text`, the ink the
painter draws) and/or the rects of the nodes that carry them (mode `node`, the
layout box those glyphs live in).  Both come from a pulp `dump_layout_tree`
snapshot; see layout_common.py for the emitter semantics this relies on.

SCROLL CAVEAT (enforced, not ignored).  dump_layout_tree reports rects WITHOUT
the scroll offset -- ScrollView applies it at paint time.  Two nodes are
therefore only comparable when they sit in the SAME scroll container: everything
inside one shifts together, so their relative geometry is exact, while a node
inside a scroll container and one outside it are in different spaces and their
absolute distance is meaningless.  Pairs that cross a scroll frame are NOT
reported as findings; they are counted and named under `skipped_cross_frame`.

The scrollbar is not a View and has no node in the snapshot.  ScrollView paints
it directly, so `--scrollbar` synthesizes its rect from the paint formula in
core/view/src/ui_components.cpp (ScrollView::paint):
    x = rect.x + rect.w - bar_width - 2,  full viewport height
bar_width is 4 idle / 8 hovered (ValueAnimation bar_width_{4.0f}, on_mouse_enter
-> 8).  A synthesized region is labelled as such in the output.

Exit: 0 clean, 1 findings, 2 usage/IO error, 3 self-test control failed to fire.
"""

import argparse
import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout_common as L  # noqa: E402

TEXT_KINDS = ("Label", "TextEditor", "TextButton", "HyperlinkButton")


class Item:
    def __init__(self, idx, node, rect, source, text, frame, below_fold, note=""):
        self.idx = idx
        self.node = node
        self.rect = rect
        self.source = source
        self.text = text
        self.frame = frame
        self.below_fold = below_fold
        self.note = note

    @property
    def name(self):
        nid = self.node.get("id") or "<no id>"
        return "#%d %s %s" % (self.idx, self.node.get("kind", "?"), nid)


def collect(dump, mode, keep_multiline_fallback=True):
    items, dropped = [], {"invisible": 0, "zero_area": 0, "clipped_out": 0, "no_text": 0}
    for i, n in enumerate(dump.nodes):
        boxes = n.get("measured_text_boxes") or []
        if not boxes:
            continue
        if not dump.effectively_visible(i):
            dropped["invisible"] += len(boxes)
            continue
        nrect = L.r(n["rect"])
        clip = L.r((n.get("clipping") or {}).get("rect") or {"x": 0, "y": 0, "w": 0, "h": 0})
        frame = dump.scroll_frame(i)
        in_scroll = frame not in (None, "unknown")
        # A zero-WIDTH clip means the node is never painted (a collapsed panel).
        # A zero-HEIGHT clip inside a scroll container only means "currently
        # below the fold"; because the dump is scroll-blind we analyse that
        # content in the container's own space and mark it below_fold.
        if clip[2] <= 0.0:
            dropped["clipped_out"] += len(boxes)
            continue
        below_fold = False
        if clip[3] <= 0.0:
            if not in_scroll:
                dropped["clipped_out"] += len(boxes)
                continue
            below_fold = True
        for b in boxes:
            text = b.get("text") or ""
            if not text.strip():
                dropped["no_text"] += 1
                continue
            brect = L.r(b["rect"])
            if mode == "node":
                rect, source, note = nrect, "node-rect", ""
            else:
                note = ""
                if brect[2] <= 0.0 or brect[3] <= 0.0:
                    if not keep_multiline_fallback or nrect[2] <= 0 or nrect[3] <= 0:
                        dropped["zero_area"] += 1
                        continue
                    # Multi-line Labels report intrinsic_width 0 by design; the
                    # node box is the only footprint available for them.
                    rect, source, note = nrect, "node-rect(multiline-fallback)", "multiline"
                else:
                    rect, source = brect, "text-box"
            if rect[2] <= 0.0 or rect[3] <= 0.0:
                dropped["zero_area"] += 1
                continue
            items.append(Item(i, n, rect, source, text, frame, below_fold, note))
    return items, dropped


def synth_scrollbars(dump, bar_width):
    bars = []
    for i, n in enumerate(dump.nodes):
        if n.get("overflow") not in L.SCROLL_OVERFLOWS:
            continue
        if not dump.effectively_visible(i):
            continue
        vx, vy, vw, vh = L.r(n["rect"])
        if vw <= 0 or vh <= 0:
            continue
        overflows = False
        for j, m in enumerate(dump.nodes):
            if j == i or dump.parent is None or i not in dump.ancestors(j):
                continue
            mx, my, mw, mh = L.r(m["rect"])
            if my + mh > vy + vh + 0.5 or mx + mw > vx + vw + 0.5:
                overflows = True
                break
        if not overflows:
            continue
        rect = (vx + vw - bar_width - 2.0, vy, bar_width, vh)
        bars.append(
            Item(i, n, rect, "synthesized-scrollbar(w=%g)" % bar_width,
                 "<scrollbar track>", i, False, "synthesized")
        )
    return bars


INK_EVIDENCE_NOTE = (
    "EVIDENCE CLASSES -- read before quoting a finding as 'glyphs are covered'.\n"
    "  ink : BOTH rects are real ink extents -- a shaped-text box (rect.w from\n"
    "        Label::intrinsic_width) or the synthesized scrollbar strip, which is\n"
    "        a literally painted rectangle.  An overlap here means PIXELS collide.\n"
    "  box : at least one rect is a node-rect fallback (a multi-line label, whose\n"
    "        intrinsic_width() is 0 by construction, or --mode node).  The WIDGET\n"
    "        BOXES overlap; the glyphs inside them may not.  A left-aligned label\n"
    "        in a 462px-wide row box whose ink stops after 71px is a box-class\n"
    "        overlap and NOT visible occlusion -- verified against pixels on\n"
    "        2026-09-08, where 'APPEARANCE' ink ended at x=498.7 while its row box\n"
    "        reached the scrollbar at x=883.  Box findings are LEADS, not defects:\n"
    "        confirm them in the PNG before reporting one to a human."
)


def evidence_of(p, q):
    """'ink' only when neither side is a node-rect stand-in for real glyphs."""
    for it in (p, q):
        if it.source.startswith("synthesized"):
            continue          # a painted rect, not a stand-in
        if it.source != "text-box":
            return "box"
    return "ink"


def find_pairs(dump, items, min_overlap, bars):
    findings, skipped_cross, skipped_rel = [], [], 0
    pool = items + bars
    for a in range(len(pool)):
        for b in range(a + 1, len(pool)):
            p, q = pool[a], pool[b]
            if p.source.startswith("synthesized") and q.source.startswith("synthesized"):
                continue
            # The scrollbar is painted in the ScrollView's own viewport space
            # while its content is in unscrolled content space, so only the
            # x-axis relationship is scroll-independent.  Vertical scroll cannot
            # move content out from under a full-height bar track, so an x-range
            # overlap is a real finding regardless of scroll position.
            bar_case = p.source.startswith("synthesized") or q.source.startswith("synthesized")
            if bar_case:
                bar, other = (p, q) if p.source.startswith("synthesized") else (q, p)
                if other.frame != bar.idx:
                    continue
                ox, _ = L.intersect(bar.rect, other.rect)
                if ox <= min_overlap:
                    continue
                findings.append((bar, other, ox, other.rect[3],
                                 "x-only (scroll-independent)", evidence_of(bar, other)))
                continue
            if dump.is_related(p.idx, q.idx):
                skipped_rel += 1
                continue
            if p.frame != q.frame:
                skipped_cross.append((p, q))
                continue
            ox, oy = L.intersect(p.rect, q.rect)
            if ox > min_overlap and oy > min_overlap:
                findings.append((p, q, ox, oy, "xy", evidence_of(p, q)))
    return findings, skipped_cross, skipped_rel


def run(path, args):
    dump = L.Dump(path, args.depths)
    if dump.depths is None:
        print("WARNING: no .depths.json sidecar next to %s -- ancestry and "
              "scroll-frame filtering are OFF, results are unfiltered." % path,
              file=sys.stderr)
    items, dropped = collect(dump, args.mode)
    bars = synth_scrollbars(dump, args.bar_width) if args.scrollbar else []
    findings, cross, rel = find_pairs(dump, items, args.min_overlap, bars)

    print("== DETECTOR A  box-intersection ==")
    print("dump      : %s" % path)
    print("depths    : %s" % (dump.depths_path or "MISSING"))
    print("surface   : %s   viewport %sx%s   nodes %d"
          % (dump.surface, dump.viewport.get("w"), dump.viewport.get("h"), len(dump.nodes)))
    print("mode      : %s   min-overlap %.2fpx   scrollbar-synthesis %s"
          % (args.mode, args.min_overlap, ("ON w=%g" % args.bar_width) if args.scrollbar else "off"))
    print("compared  : %d painted text rects (%d dropped: %s)"
          % (len(items), sum(dropped.values()),
             ", ".join("%s=%d" % kv for kv in dropped.items() if kv[1]) or "none"))
    print("skipped   : %d ancestor/descendant pairs, %d cross-scroll-frame pairs "
          "(not comparable -- see SCROLL CAVEAT)" % (rel, len(cross)))
    print("")
    if not findings:
        print("RESULT: 0 intersecting pairs.")
    else:
        n_ink = sum(1 for f in findings if f[5] == "ink")
        print("RESULT: %d intersecting pair(s) -- RED  (%d ink-class, %d box-class)"
              % (len(findings), n_ink, len(findings) - n_ink))
        print("")
        print(INK_EVIDENCE_NOTE)
        print("")
        for p, q, ox, oy, axis, ev in findings:
            print("  OVERLAP %.1f x %.1f px  [%s]  evidence=%s%s"
                  % (ox, oy, axis, ev,
                     "" if ev == "ink" else "  <- widget boxes, NOT proven glyph occlusion"))
            for it in (p, q):
                bf = "  (below-fold in content space)" if it.below_fold else ""
                print("    %-34s %-38s %r%s"
                      % (it.name, L.fmt_rect(it.rect) + " <" + it.source + ">",
                         it.text[:44], bf))
            print("")
    if args.json:
        out = {
            "dump": path, "mode": args.mode, "findings": [
                {"evidence": f[5],
                 "a": {"node": p.name, "rect": p.rect, "source": p.source, "text": p.text},
                 "b": {"node": q.name, "rect": q.rect, "source": q.source, "text": q.text},
                 "overlap_w": ox, "overlap_h": oy, "axis": axis}
                for f in findings
                for p, q, ox, oy, axis in [f[:5]]],
            "compared": len(items), "dropped": dropped,
            "skipped_cross_frame": len(cross), "skipped_related": rel,
        }
        with open(args.json, "w") as fh:
            json.dump(out, fh, indent=1)
        print("json -> %s" % args.json)
    return findings


def make_control(path, out_prefix, depths_path):
    """Positive control: clone the dump and translate one real text node on top
    of another real text node in the SAME scroll frame.  Nothing else changes,
    so a detector that does not go RED on this file cannot go RED on anything.
    """
    dump = L.Dump(path, depths_path)
    if dump.depths is None:
        raise SystemExit("control needs the .depths.json sidecar")
    items, _ = collect(dump, "text")
    # Prefer two REAL ink rects so the control exercises the high-confidence
    # ink-class path, not only the node-rect fallback path.  Fall back to any
    # comparable pair if the dump has fewer than two measurable runs.
    anchor = donor = None
    for want_ink in (True, False):
        for i in range(len(items)):
            for j in range(i + 1, len(items)):
                a, b = items[i], items[j]
                if a.frame != b.frame or dump.is_related(a.idx, b.idx):
                    continue
                if want_ink and not (a.source == "text-box" and b.source == "text-box"):
                    continue
                anchor, donor = a, b
                break
            if anchor:
                break
        if anchor:
            break
    if anchor is None:
        raise SystemExit("control could not find two comparable text rects")
    raw = json.load(open(path))
    nodes = L.flatten(raw["nodes"])
    tgt = copy.deepcopy(nodes[donor.idx])
    ax, ay = anchor.rect[0], anchor.rect[1]
    dx, dy = ax - donor.rect[0], ay - donor.rect[1]
    for key_rect in (tgt["rect"], tgt["clipping"]["rect"]):
        key_rect["x"] += dx
        key_rect["y"] += dy
    for b in tgt.get("measured_text_boxes", []):
        b["rect"]["x"] += dx
        b["rect"]["y"] += dy
    for h in tgt.get("hit_regions", []):
        h["rect"]["x"] += dx
        h["rect"]["y"] += dy
    # Keep the clip permissive so the injected node is unambiguously painted.
    tgt["clipping"]["rect"] = dict(raw["nodes"][0]["clipping"]["rect"])
    nodes[donor.idx] = tgt
    raw["nodes"] = nodes
    dump_out = out_prefix + ".layout.json"
    depth_out = out_prefix + ".depths.json"
    json.dump(raw, open(dump_out, "w"), indent=1)
    json.dump(dump.depths, open(depth_out, "w"))
    print("control written: moved %s onto %s (dx=%.1f dy=%.1f)"
          % (donor.name, anchor.name, dx, dy))
    print("  -> %s\n  -> %s" % (dump_out, depth_out))
    return dump_out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump", help="path to a SPECTR_LAYOUT_DUMP .layout.json")
    ap.add_argument("--depths", help="explicit .depths.json sidecar")
    ap.add_argument("--mode", choices=("text", "node"), default="text",
                    help="text = measured_text_boxes ink rects (default); "
                         "node = the rects of the nodes carrying text")
    ap.add_argument("--min-overlap", type=float, default=0.5,
                    help="px of overlap on BOTH axes before a pair counts (default 0.5)")
    ap.add_argument("--scrollbar", action="store_true",
                    help="synthesize ScrollView scrollbar rects and test content against them")
    ap.add_argument("--bar-width", type=float, default=8.0,
                    help="scrollbar width: 4 idle, 8 hovered (default 8)")
    ap.add_argument("--json", help="write findings as JSON")
    ap.add_argument("--self-test", metavar="PREFIX", nargs="?", const="",
                    help="build a positive control from this dump and prove the "
                         "detector goes RED on it")
    args = ap.parse_args()

    if args.self_test is not None:
        prefix = args.self_test or (os.path.splitext(args.dump)[0] + ".controlA")
        print("== POSITIVE CONTROL (detector A) ==")
        cpath = make_control(args.dump, prefix, args.depths)
        print("")
        ctl_args = copy.copy(args)
        ctl_args.self_test = None
        ctl_args.depths = None
        ctl_args.json = None
        found = run(cpath, ctl_args)
        print("")
        if not found:
            print("CONTROL FAILED TO FIRE -- the detector is broken; no clean "
                  "run from it can be believed.")
            return 3
        print("CONTROL FIRED (%d finding) -- the detector can go RED." % len(found))
        return 0

    findings = run(args.dump, args)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
