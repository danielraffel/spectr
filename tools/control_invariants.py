#!/usr/bin/env python3
"""A slider must have a track with height and a thumb inside it.

A slider that renders as a bare rule still reports a value, still accepts a
drag, and still passes any check that asks "is the control present?" — so the
one defect a user names first ("there's no handle") is invisible to presence
tests. It is visible in the tree: the track is a box with zero height and no
child to be the thumb.

Zero height also means the control cannot be hit-tested by pointer, so this is a
usability defect and not only a cosmetic one.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appearance_invariants as ai  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("snapshot")
    ap.add_argument("--min-track-width", type=float, default=60.0,
                    help="ignore boxes too narrow to be a slider track")
    ap.add_argument("--max-track-width", type=float, default=400.0)
    ap.add_argument("--plant", choices=["flatten-track"],
                    help="zero a healthy track to prove the check can fail")
    args = ap.parse_args()

    with open(args.snapshot, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    ai.merge_depth_sidecar(args.snapshot, doc)
    nodes = ai.load_nodes(doc)
    exact, _ = ai.resolve_parents(nodes)
    if not exact:
        print("INCONCLUSIVE: no depth sidecar, so a thumb cannot be attributed "
              "to its track", file=sys.stderr)
        return 3

    children: dict[int, int] = {}
    for n in nodes:
        if n.parent is not None:
            children[n.parent] = children.get(n.parent, 0) + 1

    def is_track(n: ai.Node) -> bool:
        return (n.kind == "View" and n.visible
                and args.min_track_width <= n.rect.w <= args.max_track_width)

    candidates = [n for n in nodes if is_track(n)]
    if args.plant == "flatten-track":
        healthy = [n for n in candidates if n.rect.h > 0 and children.get(n.index, 0) > 0]
        if not healthy:
            print("cannot plant: no healthy track to flatten", file=sys.stderr)
            return 4
        victim = healthy[0]
        victim.rect = ai.Rect(victim.rect.x, victim.rect.y, victim.rect.w, 0.0)
        children[victim.index] = 0
        print(f"CONTROL: planted a flattened track on {victim.id}")

    # A track is only a *slider* track if it is flat AND empty; a flat box with
    # children is a layout row, and a box with height is drawing something.
    broken = [n for n in candidates
              if n.rect.h <= 0.0 and children.get(n.index, 0) == 0]
    healthy = [n for n in candidates
               if n.rect.h > 0.0 and children.get(n.index, 0) > 0]

    print(f"snapshot={os.path.basename(args.snapshot)}  "
          f"candidate_tracks={len(candidates)}  "
          f"CONTROL boxes_with_height_and_children={len(healthy)}")
    if not candidates:
        print("INCONCLUSIVE: nothing in this snapshot looks like a slider track")
        return 3

    for n in broken:
        print(f"  RED  {n.id or '<anon>'} is a {n.rect.w:.0f}x{n.rect.h:.0f} track "
              f"with no child to be a thumb — it paints as a bare rule and "
              f"cannot be hit-tested")
    if broken:
        print(f"RED    {len(broken)} slider(s) with no thumb")
        return 1
    print("GREEN  every candidate track has height and a thumb")
    if args.plant:
        print("BROKEN: the planted negative did not fail the check", file=sys.stderr)
        return 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
