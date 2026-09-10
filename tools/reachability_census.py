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

    findings, excused = [], []
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
        anc = set(ancestors(nodes, depths, i))
        (excused if anc & set(args.allow_under) else findings).append(row)

    ok = len(leaves) - len(findings) - len(excused)
    print(f"[reachability] population={len(leaves)} (hit-test leaves) "
          f"reachable={ok} excused={len(excused)} findings={len(findings)}")
    for row in sorted(excused):
        print(f"  excused  {row[0]:9} {row[1]} {row[2]}")
    for row in sorted(findings):
        print(f"  FINDING  {row[0]:9} {row[1]} {row[2]}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
