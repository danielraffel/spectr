#!/usr/bin/env python3
"""Appearance invariants over a dump_layout_tree() snapshot.

Two detectors, both asserting the property directly rather than a proxy:

  box-intersection      two painted glyph runs must not overlap
  painted-vs-measured   a glyph run must fit inside the box laid out for it

Both operate on the shipping standalone's own layout dump
(SPECTR_LAYOUT_DUMP=...), so they measure the installed surface.

Two things a naive reading of the dump gets wrong, both of which manufacture
false positives:

  * A node's `rect` is its LAYOUT box, which carries padding and centring slack.
    Two labels whose layout boxes touch can have glyph runs nowhere near each
    other. Adjudicate on `measured_text_boxes[].rect`, never on `rect`.
  * `visible: true` is the node's own flag, not a paint test. A node scrolled
    out of its container is still `visible` while lying wholly outside its
    `clipping.rect`, so it paints nothing. Intersect with the clip first.

A run whose painted width the dump reports as 0.0 cannot be adjudicated at all;
those are counted and reported rather than silently assumed innocent.

Plant flags exist so each detector can be shown failing. A check that cannot be
made to fail proves nothing.

This module is also the shared node model for the sibling invariant scripts
(`content_invariants.py`, `control_invariants.py`): `merge_depth_sidecar`,
`load_nodes`, `resolve_parents`, `effectively_visible`, `painted_box` and
`on_screen_box` answer the questions that need ancestry, which the flat
`load_dump` path deliberately does not.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass, field
from typing import Optional


# ─────────────────────────────────────────────────── shared node model ──
#
# The flat helpers below (`load_dump`, `painted_runs`, …) answer geometry
# questions that need no ancestry. Anything that asks whether a VIEWER can see
# a string needs the tree, because two facts the snapshot emits do not compose
# on their own:
#
#   * `visible` is the node's own `view.visible()`. A label inside a dismissed
#     modal still reports `visible: true`.
#   * `clipping.rect` is the clip a node imposes on its DESCENDANTS, not on
#     itself, so a node's on-screen extent is its painted box intersected with
#     every clipping ancestor.
#
# The pre-order node list carries no depth, so ancestry comes from the
# `.depths.json` sidecar Spectr writes beside every `SPECTR_LAYOUT_DUMP`.
# Ancestry can NOT be inferred from rect containment: a scrolled row routinely
# escapes its parent's bounds, and the containment stack then pops past the
# very clipper the question is about. Every function here that needs ancestry
# therefore refuses rather than guesses when the sidecar is absent.


@dataclass(frozen=True)
class Rect:
    x: float
    y: float
    w: float
    h: float

    @property
    def right(self) -> float:
        return self.x + self.w

    @property
    def bottom(self) -> float:
        return self.y + self.h

    @property
    def area(self) -> float:
        return max(0.0, self.w) * max(0.0, self.h)

    def intersect(self, other: "Rect") -> "Rect":
        x1 = max(self.x, other.x)
        y1 = max(self.y, other.y)
        x2 = min(self.right, other.right)
        y2 = min(self.bottom, other.bottom)
        return Rect(x1, y1, max(0.0, x2 - x1), max(0.0, y2 - y1))

    def contains(self, other: "Rect", tol: float = 0.5) -> bool:
        return (self.x - tol <= other.x
                and self.y - tol <= other.y
                and self.right + tol >= other.right
                and self.bottom + tol >= other.bottom)

    def __str__(self) -> str:
        return f"[{self.x:.2f},{self.y:.2f} {self.w:.2f}x{self.h:.2f}]"


def rect_of(obj, key: str = "rect"):
    if not isinstance(obj, dict):
        return None
    raw = obj.get(key)
    if not isinstance(raw, dict):
        return None
    try:
        return Rect(float(raw.get("x", 0.0)), float(raw.get("y", 0.0)),
                    float(raw.get("w", 0.0)), float(raw.get("h", 0.0)))
    except (TypeError, ValueError):
        return None


@dataclass
class Node:
    index: int
    id: str
    kind: str
    rect: Rect
    visible: bool
    overflow: str
    clip_for_children: Optional[Rect]
    texts: list = field(default_factory=list)
    depth: Optional[int] = None
    parent: Optional[int] = None

    @property
    def label(self) -> str:
        name = self.id or f"<{self.kind}>"
        return f"{name}({self.kind})"

    @property
    def text(self) -> str:
        return " / ".join(t for t, _ in self.texts)


def merge_depth_sidecar(snapshot_path: str, doc: dict) -> None:
    """Attach the depths written beside the snapshot, if present.

    A length mismatch means the sidecar does not describe THIS snapshot, so it
    is refused rather than applied to the wrong nodes: a silently misaligned
    depth array corrupts every ancestor decision downstream, and the corruption
    is invisible in the output.
    """
    base = snapshot_path
    for suffix in (".layout.json", ".json"):
        if base.endswith(suffix):
            base = base[: -len(suffix)]
            break
    sidecar = base + ".depths.json"
    if not os.path.exists(sidecar):
        return
    try:
        with open(sidecar, "r", encoding="utf-8") as fh:
            depths = json.load(fh)
    except (OSError, ValueError):
        return
    nodes = doc.get("nodes")
    if not isinstance(nodes, list) or not isinstance(depths, list):
        return
    if len(depths) != len(nodes):
        print(f"warning: depth sidecar has {len(depths)} entries for "
              f"{len(nodes)} nodes — refusing to apply it", file=sys.stderr)
        return
    for node, depth in zip(nodes, depths):
        if isinstance(node, dict) and isinstance(depth, int):
            node["depth"] = depth


def load_nodes(doc: dict) -> list:
    """Build the node model from an already-parsed snapshot document."""
    raw_nodes = doc.get("nodes")
    if not isinstance(raw_nodes, list):
        raise SystemExit("snapshot has no `nodes` array")

    nodes = []
    for i, raw in enumerate(raw_nodes):
        if not isinstance(raw, dict):
            continue
        rect = rect_of(raw)
        if rect is None:
            continue
        texts = []
        for box in raw.get("measured_text_boxes") or []:
            if not isinstance(box, dict):
                continue
            text = box.get("text")
            box_rect = rect_of(box)
            if isinstance(text, str) and text.strip() and box_rect is not None:
                texts.append((text, box_rect))
        depth = raw.get("depth")
        nodes.append(Node(
            index=i,
            id=str(raw.get("id") or ""),
            kind=str(raw.get("kind") or raw.get("type") or "?"),
            rect=rect,
            visible=bool(raw.get("visible", True)),
            overflow=str(raw.get("overflow") or "visible"),
            clip_for_children=rect_of(raw.get("clipping")),
            texts=texts,
            depth=int(depth) if isinstance(depth, int) else None,
        ))
    return nodes


def resolve_parents(nodes: list):
    """Link each node to its parent; report whether the ancestry is EXACT.

    Exact when every node carries `depth` (pre-order + depth determines the
    tree uniquely). Without it, fall back to a containment stack and return
    False, so a caller that needs a sound answer can refuse instead of
    adjudicating on a guess.
    """
    if nodes and all(n.depth is not None for n in nodes):
        stack = []
        for n in nodes:
            del stack[n.depth:]
            n.parent = stack[-1] if stack else None
            stack.append(n.index)
        return True, 0

    stack_n = []
    inferred = 0
    for n in nodes:
        while stack_n and not stack_n[-1].rect.contains(n.rect, tol=1.0):
            stack_n.pop()
        n.parent = stack_n[-1].index if stack_n else None
        inferred += 1
        stack_n.append(n)
    return False, inferred


def ancestors(nodes: list, index: int):
    by_index = {n.index: n for n in nodes}
    cur = by_index.get(index)
    seen = 0
    while cur is not None and cur.parent is not None and seen < 512:
        yield cur.parent
        cur = by_index.get(cur.parent)
        seen += 1


def ancestry_is_exact(nodes: list) -> bool:
    return bool(nodes) and all(n.depth is not None for n in nodes)


def painted_box(node: Node, text_rect: Rect) -> Rect:
    """The region a string actually paints into.

    The snapshot records a string's INTRINSIC extent at the node origin, so a
    label that does not fit reports a rect wider than its own box.

    A MULTI-LINE label reports intrinsic width 0 on purpose —
    `Label::intrinsic_width()` returns 0 for them so the parent's available
    width drives wrapping rather than the single-line advance. Reading that 0
    as "paints nothing" silently excused most of the labels on Spectr's
    shipping surface from every check; a wrapped label paints across its whole
    box, so that is the extent to use.
    """
    if text_rect.w <= 0.0:
        w = node.rect.w  # multi-line: wraps to fill the box
    elif node.rect.w > 0:
        w = min(text_rect.w, node.rect.w)
    else:
        w = text_rect.w
    if node.rect.h > 0 and text_rect.h > 0:
        h = min(text_rect.h, node.rect.h)
    else:
        h = max(text_rect.h, 0.0)
    return Rect(text_rect.x, text_rect.y, w, h)


def inherited_clip(nodes: list, node: Node):
    """The clip every clipping ancestor imposes on this node's own pixels."""
    if not ancestry_is_exact(nodes):
        raise RuntimeError(
            "visibility requires exact ancestry: this snapshot has no depth "
            "sidecar, and inferring ancestry from rect containment is wrong "
            "for any node that escapes its parent's bounds")
    by_index = {n.index: n for n in nodes}
    clip = None
    for ancestor_index in ancestors(nodes, node.index):
        ancestor = by_index.get(ancestor_index)
        if ancestor is None or ancestor.clip_for_children is None:
            continue
        if ancestor.overflow not in ("hidden", "scroll"):
            continue
        clip = (ancestor.clip_for_children if clip is None
                else clip.intersect(ancestor.clip_for_children))
    return clip


def on_screen_box(nodes: list, node: Node, text_rect: Rect) -> Rect:
    """What a viewer can actually see of this string."""
    painted = painted_box(node, text_rect)
    clip = inherited_clip(nodes, node)
    return painted if clip is None else painted.intersect(clip)


def effectively_visible(nodes: list, node: Node) -> bool:
    """Visible to a viewer: this node AND every ancestor is visible.

    `dump_layout_tree` emits each node's own `view.visible()` and does not
    inherit it, so a label inside a hidden modal still reports `visible: true`.
    Filtering per node therefore reports a dismissed panel as still on screen.
    """
    if not node.visible:
        return False
    if not ancestry_is_exact(nodes):
        raise RuntimeError(
            "effective visibility requires exact ancestry: this snapshot has "
            "no depth sidecar, and a per-node visible flag does not compose")
    by_index = {n.index: n for n in nodes}
    for ancestor_index in ancestors(nodes, node.index):
        ancestor = by_index.get(ancestor_index)
        if ancestor is not None and not ancestor.visible:
            return False
    return True


def text_nodes(nodes: list) -> list:
    """Strings a viewer can actually see.

    Without exact ancestry the composed answer is unavailable, so this falls
    back to the per-node flag — which is weaker, and the caller is expected to
    say so rather than present it as the same measurement.
    """
    if not ancestry_is_exact(nodes):
        return [n for n in nodes if n.visible and n.texts]
    return [n for n in nodes if n.texts and effectively_visible(nodes, n)]


# ──────────────────────────────────────────────── flat painted-run model ──


def load_dump(path):
    doc = json.load(open(path))
    return doc["nodes"], doc.get("viewport", {})


def as_rect(r):
    if not r:
        return None
    return (float(r["x"]), float(r["y"]), float(r["w"]), float(r["h"]))


def intersect(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    x = max(ax, bx)
    y = max(ay, by)
    w = min(ax + aw, bx + bw) - x
    h = min(ay + ah, by + bh) - y
    if w <= 0.0 or h <= 0.0:
        return None
    return (x, y, w, h)


def clip_rect(node):
    return as_rect((node.get("clipping") or {}).get("rect"))


def painted_runs(nodes, region):
    """Glyph runs that actually reach the screen.

    Returns (painted, unmeasurable, clipped) where painted carries the run's
    on-screen rect after clipping.
    """
    painted, unmeasurable, clipped = [], [], []
    for n in nodes:
        if not n.get("visible", True):
            continue
        boxes = n.get("measured_text_boxes") or []
        if not boxes:
            continue
        b = boxes[0]
        text = b.get("text")
        if not text:
            continue
        run = as_rect(b.get("rect"))
        layout = as_rect(n.get("rect"))
        if run is None or layout is None:
            continue
        clip = clip_rect(n)
        # A node lying wholly outside its clip paints nothing, whatever its
        # `visible` flag says.
        if clip is not None and intersect(layout, clip) is None:
            clipped.append((n, text))
            continue
        if run[2] <= 0.0:
            unmeasurable.append((n, text))
            continue
        on_screen = run if clip is None else intersect(run, clip)
        if on_screen is None:
            clipped.append((n, text))
            continue
        if region and intersect(on_screen, region) is None:
            continue
        painted.append((n, text, on_screen, layout))
    return painted, unmeasurable, clipped


def within(inner, outer, eps):
    ix, iy, iw, ih = inner
    ox, oy, ow, oh = outer
    return (ix >= ox - eps and iy >= oy - eps
            and ix + iw <= ox + ow + eps and iy + ih <= oy + oh + eps)


def check_box_intersection(painted, eps, plant):
    items = list(painted)
    if plant:
        if len(items) < 2:
            return 0, ["PLANT IMPOSSIBLE: fewer than two painted runs"]
        n, t, r, lay = items[1]
        tx, ty, tw, th = items[0][2]
        # Straddle the target so neither run contains the other — a pure
        # collision, not nesting, which the nesting skip would swallow.
        items[1] = (n, t, (tx + tw / 2.0, ty + th / 2.0, r[2], r[3]), lay)

    violations = []
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            na, ta, ra, _ = items[i]
            nb, tb, rb, _ = items[j]
            if within(ra, rb, 0.5) or within(rb, ra, 0.5):
                continue
            ov = intersect(ra, rb)
            if ov and ov[2] > eps and ov[3] > eps:
                violations.append(
                    f"{ta!r}({na['id']}) {ra} overlaps {tb!r}({nb['id']}) {rb} "
                    f"by {ov[2]:.1f}x{ov[3]:.1f}px")
    return len(items), violations


def check_painted_vs_measured(painted, tol, plant):
    items = [(n, t, r[2], lay[2]) for n, t, r, lay in painted]
    if plant:
        if not items:
            return 0, ["PLANT IMPOSSIBLE: no measurable run"]
        n, t, pw, bw = items[0]
        items[0] = (n, t, bw + 12.0, bw)

    violations = []
    for n, t, pw, bw in items:
        if pw > bw + tol:
            violations.append(
                f"{t!r}({n['id']}) painted {pw:.1f}px in a {bw:.1f}px box "
                f"({pw / bw:.2f}x)")
    return len(items), violations


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--region", help="x,y,w,h — restrict to a subtree box")
    ap.add_argument("--overlap-eps", type=float, default=0.5)
    ap.add_argument("--fit-tolerance", type=float, default=0.5)
    ap.add_argument("--plant-overlap", action="store_true")
    ap.add_argument("--plant-overflow", action="store_true")
    args = ap.parse_args()

    nodes, viewport = load_dump(args.dump)
    region = None
    if args.region:
        region = tuple(float(v) for v in args.region.split(","))

    print(f"dump: {args.dump}  nodes={len(nodes)}  viewport={viewport}")
    if region:
        print(f"region: {region}")

    painted, unmeasurable, clipped = painted_runs(nodes, region)
    print(f"runs: {len(painted)} painted, {len(unmeasurable)} unmeasurable "
          f"(dump reports painted width 0.0), {len(clipped)} clipped away")

    failed = False

    n_items, ov = check_box_intersection(painted, args.overlap_eps,
                                         args.plant_overlap)
    print(f"\n[box-intersection] adjudicated {n_items} painted runs")
    if ov:
        failed = True
        print(f"  RED — {len(ov)} overlap(s)")
        for v in ov[:20]:
            print(f"    {v}")
    else:
        print("  GREEN — no two painted runs overlap")

    n_m, fv = check_painted_vs_measured(painted, args.fit_tolerance,
                                        args.plant_overflow)
    print(f"\n[painted-vs-measured] adjudicated {n_m} painted runs")
    if fv:
        failed = True
        print(f"  RED — {len(fv)} run(s) exceed their box")
        for v in fv[:20]:
            print(f"    {v}")
    else:
        print("  GREEN — every painted run fits its box")

    if unmeasurable:
        print(f"\nCAVEAT — {len(unmeasurable)} run(s) could not be adjudicated "
              f"by either detector: the layout dump reports painted width 0.0 "
              f"for them. Neither detector can see a defect in these.")

    if not painted:
        print("\nINSTRUMENT BROKEN: nothing painted was adjudicated. "
              "Reporting nothing.")
        return 2

    # The verdict line carries its own coverage on purpose. "GREEN" alone, read
    # off a tail of this output, says nothing about how much of the surface was
    # actually adjudicated -- and on a real Spectr dump most runs are not.
    total = len(painted) + len(unmeasurable) + len(clipped)
    print(f"\nCOVERAGE: adjudicated {len(painted)} of {total} text runs "
          f"({len(unmeasurable)} unmeasurable, {len(clipped)} clipped away)")
    print("RESULT:", "RED" if failed else "GREEN",
          f"over {len(painted)}/{total} runs")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
