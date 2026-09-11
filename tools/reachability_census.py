#!/usr/bin/env python3
"""Reachability census over a layout snapshot: does every control stay inside
the design viewport?

Population is the runtime's own hit-testing accounting, not a guessed selector
list.  A node carries `hit_regions` when it accepts input; containers carry one
too, so the *controls* are the hit-test LEAVES -- a node with a hit region and
no descendant that has one.  That population includes canvases and slider views,
which a `<button>`-tag census cannot see.

Reports OFFSCREEN (rect leaves the design viewport), ZERO (degenerate rect) and
HIDDEN (not visible).  Exits 1 on any finding that is not excused by --allow-under,
2 if the instrument is unusable.

ONE STRUCTURAL LIMIT, and it decides most of the population on Spectr's settings
surface.  `dump_layout_tree` records PRE-SCROLL positions: a ScrollView lays its
children out against an expanded content box and applies the scroll offset at
PAINT time, so a row inside a scroll container reports the box it would occupy
unscrolled.  Its absolute y is therefore not a screen position, and comparing it
against the design viewport height reports "offscreen" for content the user
reaches by scrolling.  The tell is that an unscrolled and a scrolled capture of
the same surface produce byte-identical rects for those rows.

So a hit-test leaf under an `overflow: scroll` ancestor is classified SCROLLABLE
rather than OFFSCREEN.  That is an honest UNMEASURED, not a pass: this census
cannot say whether such a row is reachable, only that its coordinates carry no
screen position to judge.  The count is printed on the summary line so a clean
run cannot be read as full coverage.  Pass --strict-scroll to adjudicate them
anyway (and get the old, wrong, answer).
"""
import argparse, json, sys


def load(stem):
    nodes = json.load(open(f"{stem}.layout.json"))["nodes"]
    raw = json.load(open(f"{stem}.depths.json"))
    depths = raw["depths"] if isinstance(raw, dict) and "depths" in raw else raw
    if len(depths) != len(nodes):
        sys.exit(f"instrument unusable: {len(depths)} depths vs {len(nodes)} nodes")
    return nodes, depths


def hit_leaves(nodes, depths):
    has = [bool(n.get("hit_regions")) for n in nodes]
    out = []
    for i, _ in enumerate(nodes):
        if not has[i]:
            continue
        leaf = True
        for j in range(i + 1, len(nodes)):
            if depths[j] <= depths[i]:
                break
            if has[j]:
                leaf = False
                break
        if leaf:
            out.append(i)
    return out


def scroll_ancestor(nodes, depths, i):
    """Index of the nearest strict ancestor with overflow 'scroll', else None."""
    cur = depths[i]
    for j in range(i - 1, -1, -1):
        if depths[j] < cur:
            if (nodes[j].get("overflow") or "") == "scroll":
                return j
            cur = depths[j]
        if cur == 0:
            break
    return None


def ancestors(nodes, depths, i):
    out, cur = [], depths[i]
    for j in range(i - 1, -1, -1):
        if depths[j] < cur:
            out.append(nodes[j].get("id") or "<anon>")
            cur = depths[j]
        if cur == 0:
            break
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stem", help="path without the .layout.json suffix")
    ap.add_argument("--design", default="1320x860")
    ap.add_argument("--plant", choices=["offscreen", "zero", "hidden"],
                    help="mutate one healthy, non-scrolled leaf so the census "
                         "MUST report it; a census that cannot be made to fail "
                         "proves nothing about the census that passed")
    ap.add_argument("--strict-scroll", action="store_true",
                    help="adjudicate rows inside a scroll container against the "
                         "design viewport anyway; their snapshot coordinates are "
                         "pre-scroll, so this reports content the user can reach "
                         "by scrolling as offscreen")
    ap.add_argument("--allow-under", action="append", default=[],
                    help="ancestor id whose subtree is excused (e.g. a closed "
                         "scroll panel); excused rows are still printed")
    args = ap.parse_args()
    dw, dh = (float(v) for v in args.design.lower().split("x"))

    nodes, depths = load(args.stem)
    leaves = hit_leaves(nodes, depths)
    if not leaves:
        sys.exit("instrument unusable: zero hit-test leaves -- the snapshot "
                 "carries no hit_regions, so nothing could ever be reported")

    if args.plant:
        victim = None
        for i in leaves:
            n = nodes[i]
            r = n["rect"]
            if (n.get("visible") and r["w"] > 0 and r["h"] > 0
                    and 0 <= r["x"] and r["x"] + r["w"] <= dw
                    and 0 <= r["y"] and r["y"] + r["h"] <= dh
                    and scroll_ancestor(nodes, depths, i) is None):
                victim = i
                break
        if victim is None:
            sys.exit("cannot plant: no healthy non-scrolled leaf to mutate -- "
                     "the control is unavailable, so report nothing")
        vn = nodes[victim]
        if args.plant == "offscreen":
            vn["rect"]["x"] = dw + 40.0
        elif args.plant == "zero":
            vn["rect"]["w"] = 0.0
        else:
            vn["visible"] = False
        print("CONTROL: planted %s on %s"
              % (args.plant, vn.get("id") or "<anon>"))

    findings, excused, unmeasured = [], [], []
    for i in leaves:
        n = nodes[i]
        r = n["rect"]
        kind = None
        if not n.get("visible"):
            kind = "HIDDEN"
        elif r["w"] <= 0 or r["h"] <= 0:
            kind = "ZERO"
        elif (r["x"] < -0.5 or r["y"] < -0.5
              or r["x"] + r["w"] > dw + 0.5 or r["y"] + r["h"] > dh + 0.5):
            kind = "OFFSCREEN"
        if not kind:
            continue
        row = (kind, n.get("id") or "<anon>",
               f"[{r['x']:.1f},{r['y']:.1f} {r['w']:.1f}x{r['h']:.1f}]")
        if (kind == "OFFSCREEN" and not args.strict_scroll
                and scroll_ancestor(nodes, depths, i) is not None):
            unmeasured.append(("SCROLLABLE",) + row[1:])
            continue
        anc = set(ancestors(nodes, depths, i))
        (excused if anc & set(args.allow_under) else findings).append(row)

    ok = len(leaves) - len(findings) - len(excused) - len(unmeasured)
    print(f"[reachability] population={len(leaves)} (hit-test leaves) "
          f"reachable={ok} excused={len(excused)} "
          f"unmeasured_inside_scroll={len(unmeasured)} findings={len(findings)}")
    for row in sorted(unmeasured):
        print(f"  unmeasured {row[0]:9} {row[1]} {row[2]}")
    for row in sorted(excused):
        print(f"  excused  {row[0]:9} {row[1]} {row[2]}")
    for row in sorted(findings):
        print(f"  FINDING  {row[0]:9} {row[1]} {row[2]}")
    if args.plant and not findings:
        print("BROKEN: the planted defect did not surface -- do not trust a "
              "clean run of this census", file=sys.stderr)
        return 4
    if unmeasured:
        print(f"COVERAGE: {ok + len(findings) + len(excused)} of {len(leaves)} "
              f"leaves adjudicated; {len(unmeasured)} sit inside a scroll "
              f"container and carry no screen position to judge.")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
