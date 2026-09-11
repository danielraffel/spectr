#!/usr/bin/env python3
"""COLLAPSE — a string the runtime measured must reach a non-empty on-screen box.

The gap this closes
-------------------
`appearance_invariants.py` used to carry four detectors (OVERLAP, CLIP, WRAP,
COLLAPSE).  The rewrite that landed as "assert painted glyph geometry, not
layout boxes" replaced them with two -- box-intersection and
painted-vs-measured -- and WRAP and COLLAPSE got no successor.  What survives
today only catches a collapse where some other check happens to NAME the thing
it wants: `content_invariants.py` counts a required string as present only if
its box has area, and `control_invariants.py` catches a zero-height slider
track.  `reachability_census.py` reports ZERO, but its population is hit-test
LEAVES -- so a collapsed container is outside it by construction.

Nothing swept the surface for a collapsed box, and a collapsed Settings body is
the SET-1/SET-2 defect itself.  A pixel diff cannot catch it either: a region
that paints nothing renders as plausible background.

The rule, in the painted-geometry idiom
---------------------------------------
The two shipping detectors both ask a question *about the runs that paint*:

    box-intersection     of the runs that paint, do any collide?
    painted-vs-measured  of the runs that paint, does each fit its box?

Both therefore discard a run that paints nothing -- `appearance_invariants`
counts those as "clipped away" and reports reduced COVERAGE, never a finding.
On the real SET-1 defect that bucket goes from 37 runs to 73.  **The discarded
bucket is this detector's population.**  The question here is the one neither
sibling can ask:

    collapsed-box        does each measured run paint at ALL?

This is painted geometry throughout -- `measured_text_boxes[].rect` intersected
with the clipping ancestry -- not a layout-box proxy.  A layout box is read
only to ATTRIBUTE a run that already failed, never to decide that it failed.

Separating a defect from a legitimate zero
------------------------------------------
Three things legitimately reach zero on-screen pixels, and each is excluded on
a stated ground rather than by a tuned threshold:

  * A closed modal or an undisclosed group.  Excluded as HIDDEN: the node or an
    ancestor reports `visible: false`.  (Spectr's disclosure groups do not even
    reach this path -- an undisclosed LFO section removes its nodes from the
    tree entirely: MOD-2 dumps 300 nodes disabled against 366 enabled.)
  * A row scrolled out of its container.  Excluded as SCROLLED, and honestly
    UNMEASURED rather than passed: `dump_layout_tree` records PRE-SCROLL
    positions, so such a row's coordinates carry no screen position to judge.
    That is the same structural limit `reachability_census` documents.
  * A flex spacer.  Not excluded -- never in population.  The population is
    measured STRINGS, and a spacer carries no `measured_text_boxes`, so the
    sibling-count heuristic a node-based sweep needs does not arise here.

What remains is a run that paints nothing while every ancestor claims to be
visible and nothing scrolled it away.  It is attributed to the outermost box in
its ancestry that is degenerate (a side at or below --epsilon), and reported.

Every exclusion is COUNTED and printed.  A green run that adjudicated almost
nothing is a vacuous run, and this refuses one outright: with no adjudicable
population it exits 2 (no verdict), never 0.

Exit codes: 0 clean, 1 at least one COLLAPSED run, 2 the instrument is unusable
(no depth sidecar, no measured strings, nothing adjudicable), 4 a planted
defect failed to surface.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import appearance_invariants as ai  # noqa: E402


def load(path: str):
    """Parse a snapshot into the shared node model with exact ancestry."""
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    ai.merge_depth_sidecar(path, doc)
    nodes = ai.load_nodes(doc)
    exact, _ = ai.resolve_parents(nodes)
    if not exact:
        print("no verdict: this snapshot has no depth sidecar, and a "
              "collapse verdict needs exact ancestry -- inferring it from "
              "rect containment is wrong for any node that escapes its "
              "parent's bounds, which is every scrolled row", file=sys.stderr)
        raise SystemExit(2)
    return doc, nodes


def degenerate_ancestry(nodes, by_index, node, eps: float):
    """Ancestors of `node`, outermost first, whose own box has a zero side."""
    chain = [by_index[i] for i in ai.ancestors(nodes, node.index)
             if i in by_index]
    return [a for a in reversed(chain)
            if a.rect.w <= eps or a.rect.h <= eps]


def has_scroll_ancestor(nodes, by_index, node) -> bool:
    return any(by_index[i].overflow == "scroll"
               for i in ai.ancestors(nodes, node.index) if i in by_index)


def ancestor_ids(nodes, by_index, node):
    return {by_index[i].id for i in ai.ancestors(nodes, node.index)
            if i in by_index and by_index[i].id}


def plant(nodes, by_index, which: str):
    """Mutate one healthy node so the rule MUST fire.

    A detector that cannot be made to fail proves nothing about the run that
    passed, so an impossible plant is an error, never a quiet success.
    """
    if which == "self":
        # A real leaf collapse zeroes the node's box AND the clip its parent
        # imposes, because the parent's clip follows the collapsed layout. A
        # node with no clipping ancestor would keep painting, so it is not a
        # faithful victim -- skip to the next rather than reporting a plant
        # that did not model the defect.
        for n in nodes:
            if not (n.texts and n.rect.h > 1.0 and n.visible
                    and ai.effectively_visible(nodes, n)
                    and ai.on_screen_box(nodes, n, n.texts[0][1]).area > 0.25):
                continue
            clipper = next(
                (by_index[i] for i in ai.ancestors(nodes, n.index)
                 if i in by_index and by_index[i].clip_for_children is not None
                 and by_index[i].overflow in ("hidden", "scroll")), None)
            if clipper is None:
                continue
            n.rect = ai.Rect(n.rect.x, n.rect.y, n.rect.w, 0.0)
            clipper.clip_for_children = ai.Rect(
                clipper.clip_for_children.x, n.rect.y,
                clipper.clip_for_children.w, 0.0)
            return (f"zeroed the box of {n.label} and the clip its ancestor "
                    f"{clipper.label} imposes")
        print("cannot plant 'self': no healthy painting string sits under a "
              "clipping ancestor, so no faithful leaf collapse is available "
              "in this snapshot -- report nothing", file=sys.stderr)
        raise SystemExit(2)

    # "container": collapse an ancestor that currently holds painting strings.
    best = None
    for cand in nodes:
        if cand.clip_for_children is None or cand.overflow not in ("hidden", "scroll"):
            continue
        if cand.rect.h <= 1.0:
            continue
        covered = [n for n in nodes
                   if n.texts and cand.index in set(ai.ancestors(nodes, n.index))
                   and ai.effectively_visible(nodes, n)
                   and ai.on_screen_box(nodes, n, n.texts[0][1]).area > 0.25]
        if covered and (best is None or len(covered) > len(best[1])):
            best = (cand, covered)
    if best is None:
        print("cannot plant 'container': no clipping ancestor currently "
              "holds a painting string -- report nothing", file=sys.stderr)
        raise SystemExit(2)
    cand, covered = best
    cand.rect = ai.Rect(cand.rect.x, cand.rect.y, cand.rect.w, 0.0)
    cand.clip_for_children = ai.Rect(cand.clip_for_children.x,
                                     cand.clip_for_children.y,
                                     cand.clip_for_children.w, 0.0)
    return f"collapsed {cand.label} to zero height over {len(covered)} string(s)"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("snapshot", help="a *.layout.json dump_layout_tree snapshot")
    ap.add_argument("--epsilon", type=float, default=0.5,
                    help="a box side at or below this is degenerate (px). A "
                         "sub-pixel side cannot paint a glyph. Default 0.5.")
    ap.add_argument("--min-area", type=float, default=0.25,
                    help="on-screen area (px^2) a run must reach to count as "
                         "painting. Default 0.25.")
    ap.add_argument("--allow-under", action="append", default=[],
                    help="ancestor id whose subtree is excused (a deliberately "
                         "collapsed panel). Excused runs are still printed.")
    ap.add_argument("--plant", choices=["container", "self"],
                    help="mutate one healthy node so the rule MUST fire")
    ap.add_argument("--quiet", action="store_true",
                    help="print the summary and findings only")
    args = ap.parse_args()

    doc, nodes = load(args.snapshot)
    by_index = {n.index: n for n in nodes}

    population = [(n, t, r) for n in nodes for (t, r) in n.texts]
    if not population:
        print("no verdict: this snapshot carries no measured text box, so "
              "nothing could ever be reported", file=sys.stderr)
        return 2

    if args.plant:
        print("CONTROL: planted -- " + plant(nodes, by_index, args.plant))

    paints = 0
    collapsed, excused, hidden, scrolled, unattributed = [], [], [], [], []
    for node, text, rect in population:
        if not ai.effectively_visible(nodes, node):
            hidden.append((node, text))
            continue
        box = ai.on_screen_box(nodes, node, rect)
        if box.area > args.min_area:
            paints += 1
            continue
        degen = degenerate_ancestry(nodes, by_index, node, args.epsilon)
        self_degen = node.rect.w <= args.epsilon or node.rect.h <= args.epsilon
        if not degen and not self_degen:
            if has_scroll_ancestor(nodes, by_index, node):
                scrolled.append((node, text))
            else:
                unattributed.append((node, text))
            continue
        blame = degen[0] if degen else node
        row = (node, text, blame)
        if ancestor_ids(nodes, by_index, node) & set(args.allow_under):
            excused.append(row)
        else:
            collapsed.append(row)

    surface = doc.get("surface") or os.path.basename(args.snapshot)
    adjudicated = paints + len(collapsed) + len(excused)
    print(f"[collapsed-box] surface={surface} strings={len(population)} "
          f"painting={paints} COLLAPSED={len(collapsed)} excused={len(excused)}")
    print(f"  excluded: hidden={len(hidden)} (node or ancestor not visible) "
          f"scrolled={len(scrolled)} (pre-scroll coords, unmeasurable) "
          f"unattributed={len(unattributed)} (paints nothing, no degenerate box)")

    if not args.quiet:
        for node, text in sorted(unattributed, key=lambda r: r[0].index):
            print(f"  unmeasured UNATTRIBUTED {node.label} "
                  f"{node.rect} {text[:44]!r}")
        for node, text, blame in sorted(excused, key=lambda r: r[0].index):
            print(f"  excused    {node.label} {node.rect} "
                  f"under {blame.label} {text[:44]!r}")
    for node, text, blame in sorted(collapsed, key=lambda r: r[0].index):
        origin = ("its own box" if blame.index == node.index
                  else f"collapsed ancestor {blame.label} {blame.rect}")
        print(f"  FINDING    COLLAPSED {node.label} {node.rect} "
              f"{text[:44]!r} -- {origin}")

    if adjudicated == 0:
        print("no verdict: every measured string was excluded, so a clean "
              "result here would assert nothing", file=sys.stderr)
        return 2

    print(f"COVERAGE: adjudicated {adjudicated} of {len(population)} measured "
          f"strings; {len(hidden) + len(scrolled) + len(unattributed)} excluded "
          f"on the grounds counted above.")

    if args.plant and not collapsed:
        print("BROKEN: the planted collapse did not surface -- do not trust a "
              "clean run of this detector", file=sys.stderr)
        return 4
    if collapsed:
        print(f"RESULT: RED -- {len(collapsed)} measured string(s) reach zero "
              f"on-screen pixels behind a collapsed box")
        return 1
    print("RESULT: GREEN -- every adjudicable string reaches a paintable box")
    return 0


if __name__ == "__main__":
    sys.exit(main())
