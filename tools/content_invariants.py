#!/usr/bin/env python3
"""Assert that named strings are present (or absent) and actually paintable.

Presence alone is a weak claim and has already been wrong here: MOD-1 once
shipped its controls mounted behind a display:none ancestor, where every static
source-text check still passed. So a string counts as PRESENT only when its node
is visible AND its box has area — the same bar the appearance invariants apply,
because a label in a 458x0 box is mounted and unreadable at the same time.

`--absent` is not the negation of a missing feature; it is how a disclosure is
proven to be CLOSED, which is half of what a disclosure row asserts.
"""

from __future__ import annotations

import argparse
import json
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appearance_invariants as ai  # noqa: E402


def paintable_texts(snapshot: str, in_tree: bool = False) -> list[tuple[str, str, ai.Rect]]:
    with open(snapshot, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    ai.merge_depth_sidecar(snapshot, doc)
    nodes = ai.load_nodes(doc)
    ai.resolve_parents(nodes)
    out = []
    for n in ai.text_nodes(nodes):
        for text, rect in n.texts:
            # Which box counts depends on whether a scroll container is in
            # play, and the snapshot cannot tell us the difference.
            #
            # dump_layout_tree records PRE-SCROLL layout positions, so a row
            # inside a scrolled container reports the box it would occupy
            # unscrolled. Intersecting that against the container's clip then
            # says "off screen" for content a viewer is looking straight at.
            # Applying the clip test there does not make the check stricter, it
            # makes it wrong.
            #
            # So `--in-tree` asserts the honest thing for a scrolling panel:
            # the string exists with a box that has area, i.e. it is laid out
            # and paintable. Whether it is scrolled into view is settled by the
            # screenshot beside it, not by the tree.
            box = (ai.painted_box(n, rect) if in_tree
                   else ai.on_screen_box(nodes, n, rect))
            if box.area > 0:
                out.append((n.id, text.strip(), n.rect))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("snapshot")
    ap.add_argument("--present", action="append", default=[],
                    help="exact string that must be visible and paintable")
    ap.add_argument("--absent", action="append", default=[],
                    help="exact string that must NOT be paintable")
    ap.add_argument(
        "--in-tree",
        action="store_true",
        help="assert laid-out-and-paintable rather than scrolled-into-view; "
             "required inside a scroll container, where the snapshot records "
             "pre-scroll positions and the clip test is invalid",
    )
    ap.add_argument("--plant", choices=["drop-present", "add-absent"],
                    help="force a failure to prove the check can fail")
    args = ap.parse_args()

    found = paintable_texts(args.snapshot, args.in_tree)
    visible = {t for _, t, _ in found}

    if args.plant == "drop-present" and args.present:
        visible.discard(args.present[0])
        print(f"CONTROL: planted the removal of {args.present[0]!r}")
    elif args.plant == "add-absent" and args.absent:
        visible.add(args.absent[0])
        print(f"CONTROL: planted the appearance of {args.absent[0]!r}")

    print(f"snapshot={os.path.basename(args.snapshot)}  paintable_strings={len(visible)}")

    failures = []
    for want in args.present:
        if want in visible:
            print(f"  ok   present  {want!r}")
        else:
            failures.append(f"{want!r} is not visible with a paintable box")
    for want in args.absent:
        if want not in visible:
            print(f"  ok   absent   {want!r}")
        else:
            failures.append(f"{want!r} is visible but should be hidden")

    for f in failures:
        print(f"  RED  {f}")
    if failures:
        print(f"RED    {len(failures)} violation(s)")
        return 1
    print("GREEN  every asserted string is in the state required")
    if args.plant:
        print("BROKEN: the planted negative did not fail the check", file=sys.stderr)
        return 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
