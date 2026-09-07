#!/usr/bin/env python3
"""Appearance invariants over a Pulp layout snapshot.

These are INVARIANTS, not comparisons. They need no design file, no baseline and
no reference render, which is exactly why they catch what a screenshot diff
cannot: a pixel diff scores agreement with a source and averages small regions
away, so it structurally cannot see two labels painting on top of each other or
a label wider than the box it paints in.

Input is the JSON emitted by pulp::view::dump_layout_tree (schema
`visual-layout-snapshot-v1`): a pre-order node list carrying, per node, an
absolute `rect`, `visible`, `clipping.rect`, and `measured_text_boxes` holding
each string's intrinsic width/height.

Three invariants:

  OVERLAP  two visible text-bearing boxes must not intersect.
  CLIP     a string's measured width must fit the box it paints in.
  COLLAPSE a visible string must have a box with area to paint into.

Every invariant has a planted negative (`--plant`) that mutates the snapshot so
the corresponding detector MUST go red. A check that cannot be made to fail
proves nothing, so the plants are part of the tool rather than a separate
fixture that can silently stop running.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from typing import Any, Iterable, Optional

SCHEMA = "visual-layout-snapshot-v1"


# ─────────────────────────────────────────────────────────────── geometry ──


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
        return (
            self.x - tol <= other.x
            and self.y - tol <= other.y
            and self.right + tol >= other.right
            and self.bottom + tol >= other.bottom
        )

    def __str__(self) -> str:
        return f"[{self.x:.2f},{self.y:.2f} {self.w:.2f}x{self.h:.2f}]"


def rect_of(obj: Optional[dict], key: str = "rect") -> Optional[Rect]:
    if not isinstance(obj, dict):
        return None
    raw = obj.get(key)
    if not isinstance(raw, dict):
        return None
    try:
        return Rect(
            float(raw.get("x", 0.0)),
            float(raw.get("y", 0.0)),
            float(raw.get("w", 0.0)),
            float(raw.get("h", 0.0)),
        )
    except (TypeError, ValueError):
        return None


# ────────────────────────────────────────────────────────────────── model ──


@dataclass
class Node:
    index: int
    id: str
    kind: str
    rect: Rect
    visible: bool
    overflow: str
    paint: int
    clip_for_children: Optional[Rect]
    texts: list[tuple[str, Rect]] = field(default_factory=list)
    depth: Optional[int] = None
    parent: Optional[int] = None

    @property
    def label(self) -> str:
        name = self.id or f"<{self.kind}>"
        return f"{name}({self.kind})"

    @property
    def text(self) -> str:
        return " / ".join(t for t, _ in self.texts)


@dataclass
class Violation:
    detector: str
    detail: str
    nodes: list[str]

    def __str__(self) -> str:
        return f"{self.detector}: {self.detail}"


def load_nodes(doc: dict) -> list[Node]:
    raw_nodes = doc.get("nodes")
    if not isinstance(raw_nodes, list):
        raise SystemExit("snapshot has no `nodes` array")

    nodes: list[Node] = []
    for i, raw in enumerate(raw_nodes):
        if not isinstance(raw, dict):
            continue
        rect = rect_of(raw)
        if rect is None:
            continue
        texts: list[tuple[str, Rect]] = []
        for box in raw.get("measured_text_boxes") or []:
            if not isinstance(box, dict):
                continue
            text = box.get("text")
            box_rect = rect_of(box)
            if isinstance(text, str) and text.strip() and box_rect is not None:
                texts.append((text, box_rect))
        z = raw.get("z_order") if isinstance(raw.get("z_order"), dict) else {}
        depth = raw.get("depth")
        nodes.append(
            Node(
                index=i,
                id=str(raw.get("id") or ""),
                kind=str(raw.get("kind") or raw.get("type") or "?"),
                rect=rect,
                visible=bool(raw.get("visible", True)),
                overflow=str(raw.get("overflow") or "visible"),
                paint=int(z.get("paint", i) or 0),
                clip_for_children=rect_of(raw.get("clipping")),
                texts=texts,
                depth=int(depth) if isinstance(depth, int) else None,
            )
        )
    return nodes


def resolve_parents(nodes: list[Node]) -> tuple[bool, int]:
    """Link each node to its parent.

    Exact when the snapshot carries `depth` (pre-order + depth determines the
    tree uniquely). Without it, fall back to a containment stack and report that
    the ancestry is inferred, so a suppressed pair is never silently suppressed
    on a guess.
    """
    if all(n.depth is not None for n in nodes) and nodes:
        stack: list[int] = []
        for n in nodes:
            assert n.depth is not None
            del stack[n.depth :]
            n.parent = stack[-1] if stack else None
            stack.append(n.index)
        return True, 0

    stack: list[Node] = []
    inferred = 0
    for n in nodes:
        while stack and not stack[-1].rect.contains(n.rect, tol=1.0):
            stack.pop()
        n.parent = stack[-1].index if stack else None
        inferred += 1
        stack.append(n)
    return False, inferred


def ancestors(nodes: list[Node], index: int) -> Iterable[int]:
    by_index = {n.index: n for n in nodes}
    cur = by_index.get(index)
    seen = 0
    while cur is not None and cur.parent is not None and seen < 512:
        yield cur.parent
        cur = by_index.get(cur.parent)
        seen += 1


def related(nodes: list[Node], a: Node, b: Node) -> bool:
    return b.index in set(ancestors(nodes, a.index)) or a.index in set(
        ancestors(nodes, b.index)
    )


# ────────────────────────────────────────────────────────────── detectors ──


def painted_box(node: Node, text_rect: Rect) -> Rect:
    """The region a string actually paints into.

    The snapshot records a string's INTRINSIC extent at the node origin, so a
    label that does not fit reports a rect wider than its own box. For overlap
    we want what lands on screen, which is the intrinsic extent clipped to the
    node's own box.

    A MULTI-LINE label reports intrinsic width 0 on purpose — Label::
    intrinsic_width() returns 0 for them so the parent's available width drives
    wrapping rather than the single-line advance. Reading that 0 as "paints
    nothing" silently excused 63% of the labels on Spectr's shipping surface
    from every check. A wrapped label paints across its whole box, so that is
    the extent to use.
    """
    if text_rect.w <= 0.0:
        w = node.rect.w  # multi-line: wraps to fill the box
    elif node.rect.w > 0:
        w = min(text_rect.w, node.rect.w)
    else:
        w = text_rect.w
    h = min(text_rect.h, node.rect.h) if node.rect.h > 0 and text_rect.h > 0 else max(
        text_rect.h, 0.0
    )
    return Rect(text_rect.x, text_rect.y, w, h)


def ancestry_is_exact(nodes: list[Node]) -> bool:
    return bool(nodes) and all(n.depth is not None for n in nodes)


def inherited_clip(nodes: list[Node], node: Node) -> Optional[Rect]:
    """The clip every ancestor imposes on this node's own pixels.

    `clipping.rect` on a node is the clip it imposes on its DESCENDANTS, not on
    itself, so a node's on-screen extent is its painted box intersected with
    every ancestor's clip. Without this a label scrolled out of a modal still
    reports a box with area, and a presence check calls it visible — which is
    exactly the mistake that made off-screen modulation chips look mounted.
    """
    if not ancestry_is_exact(nodes):
        # Inferred ancestry cannot answer this. Containment-stack inference is
        # wrong for exactly the nodes that matter here — a row scrolled out of
        # a clipping modal is not contained in its parent, so the stack pops
        # past the clipper and the node reads as unclipped. Refusing is the
        # only honest answer; guessing produced a confident wrong one.
        raise RuntimeError(
            "visibility requires exact ancestry: this snapshot has no depth "
            "sidecar, and inferring ancestry from rect containment is wrong "
            "for any node that escapes its parent's bounds"
        )
    by_index = {n.index: n for n in nodes}
    clip: Optional[Rect] = None
    for ancestor_index in ancestors(nodes, node.index):
        ancestor = by_index.get(ancestor_index)
        if ancestor is None or ancestor.clip_for_children is None:
            continue
        if ancestor.overflow not in ("hidden", "scroll"):
            continue
        clip = ancestor.clip_for_children if clip is None else clip.intersect(
            ancestor.clip_for_children)
    return clip


def on_screen_box(nodes: list[Node], node: Node, text_rect: Rect) -> Rect:
    """What a viewer can actually see of this string."""
    painted = painted_box(node, text_rect)
    clip = inherited_clip(nodes, node)
    return painted if clip is None else painted.intersect(clip)


def effectively_visible(nodes: list[Node], node: Node) -> bool:
    """Visible to a viewer, meaning this node AND every ancestor is visible.

    `dump_layout_tree` emits each node's own `view.visible()` and does not
    inherit it, so a label inside a hidden modal still reports `visible: true`.
    Filtering per-node therefore reports a dismissed panel as still on screen —
    which is exactly how a working dismissal was mis-reported as a defect.
    Requires exact ancestry for the same reason the clip test does.
    """
    if not node.visible:
        return False
    if not ancestry_is_exact(nodes):
        raise RuntimeError(
            "effective visibility requires exact ancestry: this snapshot has "
            "no depth sidecar, and a per-node visible flag does not compose"
        )
    by_index = {n.index: n for n in nodes}
    for ancestor_index in ancestors(nodes, node.index):
        ancestor = by_index.get(ancestor_index)
        if ancestor is not None and not ancestor.visible:
            return False
    return True


def visible_overlap_box(nodes: list[Node], node: Node, text_rect: Rect) -> Rect:
    """What a viewer can see of this string, for overlap purposes.

    Two strings only collide if BOTH are actually on screen. A row clipped away
    by a modal cannot overlap the toolbar behind it, and reporting that it does
    invents a defect out of correct clipping.

    Only `overflow: hidden` ancestors are applied. A `scroll` ancestor is
    deliberately skipped: `dump_layout_tree` records PRE-SCROLL positions, so
    intersecting a scrolled row against its container's clip says "off screen"
    for content the viewer is looking straight at. Clipping on hidden is sound
    because a hidden container does not translate its children.
    """
    painted = painted_box(node, text_rect)
    if not ancestry_is_exact(nodes):
        return painted
    by_index = {n.index: n for n in nodes}
    inside_scroll = False
    clip: Optional[Rect] = None
    for ancestor_index in ancestors(nodes, node.index):
        ancestor = by_index.get(ancestor_index)
        if ancestor is None:
            continue
        if ancestor.overflow == "scroll":
            inside_scroll = True
        if ancestor.overflow == "hidden" and ancestor.clip_for_children is not None:
            clip = (ancestor.clip_for_children if clip is None
                    else clip.intersect(ancestor.clip_for_children))
    if inside_scroll or clip is None:
        return painted
    return painted.intersect(clip)


def text_nodes(nodes: list[Node]) -> list[Node]:
    return [n for n in nodes if n.visible and n.texts]


def detect_overlap(nodes: list[Node], min_area: float) -> tuple[list[Violation], int]:
    """OVERLAP — two visible text boxes must not intersect."""
    candidates = text_nodes(nodes)
    violations: list[Violation] = []
    suppressed = 0
    for i in range(len(candidates)):
        a = candidates[i]
        for j in range(i + 1, len(candidates)):
            b = candidates[j]
            if related(nodes, a, b):
                suppressed += 1
                continue
            for a_text, a_rect in a.texts:
                pa = visible_overlap_box(nodes, a, a_rect)
                if pa.area <= 0:
                    continue
                for b_text, b_rect in b.texts:
                    pb = visible_overlap_box(nodes, b, b_rect)
                    if pb.area <= 0:
                        continue
                    hit = pa.intersect(pb)
                    if hit.area <= min_area:
                        continue
                    violations.append(
                        Violation(
                            "OVERLAP",
                            f"{a.label} {json.dumps(a_text[:40])} {pa} "
                            f"overlaps {b.label} {json.dumps(b_text[:40])} {pb} "
                            f"by {hit.area:.1f}px^2 over {hit}",
                            [a.label, b.label],
                        )
                    )
    return violations, suppressed


# A line box is routinely taller than the glyphs it carries: 13px of text laid
# out with 16px line-height reports a 16px measured height in a 13px box and
# paints perfectly, because the extra is leading rather than ink. Treating that
# as clipping reported 13 false defects on a surface with none, so height only
# counts as a violation once the text needs a genuine EXTRA LINE.
WRAP_RATIO = 1.5


def detect_clip(nodes: list[Node], tol: float, strict_height: bool = False) -> list[Violation]:
    """CLIP — a string's measured width must fit the box it paints in.

    Width is the load-bearing check: a string wider than its box is truncated,
    and nothing about typography excuses it. Height is reported separately as
    WRAP, and only when the overflow is large enough to be another line.
    """
    violations: list[Violation] = []
    for n in text_nodes(nodes):
        for text, rect in n.texts:
            if n.rect.w > 0 and rect.w > n.rect.w + tol:
                violations.append(
                    Violation(
                        "CLIP",
                        f"{n.label} {json.dumps(text[:40])} measures "
                        f"{rect.w:.2f}px wide but paints in a {n.rect.w:.2f}px box "
                        f"(overflows by {rect.w - n.rect.w:.2f}px) rect={n.rect}",
                        [n.label],
                    )
                )
            if n.rect.h <= 0:
                continue  # a zero-height box is COLLAPSE's finding, not CLIP's
            over = rect.h - n.rect.h
            if over <= tol:
                continue
            if rect.w <= 0.0:
                # Multi-line sentinel: Label::intrinsic_width() returns 0 for
                # these, and measured_height is then a computed wrap ESTIMATE
                # for the available width -- not the extent that paints. A
                # header that renders on one line reports two lines' worth here,
                # so asserting WRAP from it invents a defect. Verified against
                # pixels: "SPECTR . ZOOMABLE FILTER BANK" paints on one line
                # while reporting 36px in a 14px box.
                continue
            if not strict_height and rect.h < n.rect.h * WRAP_RATIO:
                continue  # leading, not an extra line
            violations.append(
                Violation(
                    "WRAP",
                    f"{n.label} {json.dumps(text[:40])} measures "
                    f"{rect.h:.2f}px tall but paints in a {n.rect.h:.2f}px box "
                    f"(overflows by {over:.2f}px, {rect.h / n.rect.h:.2f}x — "
                    f"needs another line) rect={n.rect}",
                    [n.label],
                )
            )
    return violations


def detect_collapse(nodes: list[Node]) -> list[Violation]:
    """COLLAPSE — a visible string must have a box with area to paint into."""
    violations: list[Violation] = []
    for n in text_nodes(nodes):
        if n.rect.w > 0.0 and n.rect.h > 0.0:
            continue
        violations.append(
            Violation(
                "COLLAPSE",
                f"{n.label} carries text {json.dumps(n.text[:60])} but its box is "
                f"{n.rect.w:.2f}x{n.rect.h:.2f} — nothing can paint there",
                [n.label],
            )
        )
    return violations


# ─────────────────────────────────────────────────────── planted negatives ──


def plant(doc: dict, which: str) -> str:
    """Mutate the snapshot so a specific detector MUST go red.

    The plant is the control. If a detector stays green under its own plant the
    detector is broken, and the tool says so rather than reporting a pass.
    """
    def measurable(n: Any) -> bool:
        if not isinstance(n, dict) or not n.get("visible", True):
            return False
        boxes = n.get("measured_text_boxes") or []
        if not boxes:
            return False
        r = n.get("rect") or {}
        # A plant on a node with no area cannot redden anything, which would
        # make the control vacuous rather than reassuring.
        return float(r.get("w", 0)) > 1.0 and float(r.get("h", 0)) > 1.0

    nodes = [n for n in doc.get("nodes", []) if measurable(n)]
    if not nodes:
        raise SystemExit("cannot plant: snapshot has no visible text-bearing node")

    if which == "overlap":
        if len(nodes) < 2:
            raise SystemExit("cannot plant overlap: need two text-bearing nodes")
        a, b = nodes[0], nodes[1]
        a["rect"] = dict(b["rect"])
        for box in a["measured_text_boxes"]:
            box["rect"]["x"] = b["rect"]["x"]
            box["rect"]["y"] = b["rect"]["y"]
            box["rect"]["w"] = max(4.0, float(b["rect"]["w"]))
            box["rect"]["h"] = max(4.0, float(b["rect"]["h"]))
        return f"planted OVERLAP: moved {a.get('id') or a.get('kind')} onto {b.get('id') or b.get('kind')}"

    if which == "clip":
        n = nodes[0]
        box = n["measured_text_boxes"][0]
        box["rect"]["w"] = float(n["rect"]["w"]) + 40.0
        return f"planted CLIP: widened {n.get('id') or n.get('kind')} text to box+40px"

    if which == "wrap":
        n = nodes[0]
        box = n["measured_text_boxes"][0]
        box["rect"]["h"] = float(n["rect"]["h"]) * 2.0 + 8.0
        return f"planted WRAP: doubled {n.get('id') or n.get('kind')} text height"

    if which == "collapse":
        n = nodes[0]
        n["rect"]["h"] = 0.0
        return f"planted COLLAPSE: zeroed height of {n.get('id') or n.get('kind')}"

    raise SystemExit(f"unknown plant: {which}")


# ─────────────────────────────────────────────────────────────────── main ──


def merge_depth_sidecar(snapshot_path: str, doc: dict) -> None:
    """Attach depths written alongside the snapshot, if present.

    A length mismatch means the sidecar does not describe this snapshot, so it
    is refused rather than applied to the wrong nodes — a silently misaligned
    depth array would corrupt every ancestor decision downstream.
    """
    import os

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
        print(
            f"warning: depth sidecar has {len(depths)} entries for "
            f"{len(nodes)} nodes — refusing to apply it",
            file=sys.stderr,
        )
        return
    for node, depth in zip(nodes, depths):
        if isinstance(node, dict) and isinstance(depth, int):
            node["depth"] = depth


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("snapshot", help="layout snapshot JSON from dump_layout_tree")
    ap.add_argument(
        "--plant",
        choices=["overlap", "clip", "wrap", "collapse"],
        help="mutate the snapshot so that detector must go red (control)",
    )
    ap.add_argument(
        "--only",
        action="append",
        choices=["overlap", "clip", "wrap", "collapse"],
        help="run only these detectors (repeatable)",
    )
    ap.add_argument(
        "--subtree",
        help="restrict to nodes whose id contains this substring, and their descendants",
    )
    ap.add_argument("--min-overlap-area", type=float, default=1.0)
    ap.add_argument("--width-tolerance", type=float, default=0.5)
    ap.add_argument(
        "--strict-height",
        action="store_true",
        help="also report line-height overshoot that is leading, not an extra line",
    )
    ap.add_argument("--max-report", type=int, default=40)
    ap.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = ap.parse_args()

    with open(args.snapshot, "r", encoding="utf-8") as fh:
        doc = json.load(fh)

    # dump_layout_tree emits pre-order nodes with no depth, so a consumer would
    # have to INFER ancestry from rect containment — which is wrong for any node
    # that escapes its parent's bounds. The capture harness writes a depth
    # sidecar from the same pre-order walk; merging it makes ancestry exact.
    merge_depth_sidecar(args.snapshot, doc)

    schema = doc.get("schema_version")
    if schema != SCHEMA:
        print(
            f"warning: schema is {schema!r}, expected {SCHEMA!r}", file=sys.stderr
        )

    plant_note = plant(doc, args.plant) if args.plant else None

    nodes = load_nodes(doc)
    exact_ancestry, _ = resolve_parents(nodes)

    if args.subtree:
        keep: set[int] = set()
        for n in nodes:
            if args.subtree in n.id:
                keep.add(n.index)
                keep.update(
                    m.index
                    for m in nodes
                    if n.index in set(ancestors(nodes, m.index))
                )
        nodes = [n for n in nodes if n.index in keep]
        if not nodes:
            print(f"no node id contains {args.subtree!r}", file=sys.stderr)
            return 2

    wanted = set(args.only or ["overlap", "clip", "wrap", "collapse"])
    violations: list[Violation] = []
    suppressed = 0
    if "overlap" in wanted:
        v, suppressed = detect_overlap(nodes, args.min_overlap_area)
        violations += v
    if "clip" in wanted or "wrap" in wanted:
        for v in detect_clip(nodes, args.width_tolerance, args.strict_height):
            if v.detector.lower() in wanted:
                violations.append(v)
    if "collapse" in wanted:
        violations += detect_collapse(nodes)

    counted = text_nodes(nodes)
    surface = doc.get("surface", "?")

    if args.json:
        print(
            json.dumps(
                {
                    "surface": surface,
                    "snapshot": args.snapshot,
                    "plant": plant_note,
                    "exact_ancestry": exact_ancestry,
                    "nodes_total": len(nodes),
                    "text_nodes": len(counted),
                    "violations": [
                        {"detector": v.detector, "detail": v.detail, "nodes": v.nodes}
                        for v in violations
                    ],
                },
                indent=2,
            )
        )
    else:
        if plant_note:
            print(f"CONTROL: {plant_note}")
        print(
            f"surface={surface}  nodes={len(nodes)}  text_nodes={len(counted)}  "
            f"ancestry={'exact' if exact_ancestry else 'INFERRED (no depth field)'}"
            + (f"  ancestor_pairs_skipped={suppressed}" if suppressed else "")
        )
        if not counted:
            print(
                "INCONCLUSIVE: no visible text-bearing node in this snapshot — "
                "the detectors had nothing to measure, which is NOT a pass"
            )
            return 3
        by_detector: dict[str, int] = {}
        for v in violations:
            by_detector[v.detector] = by_detector.get(v.detector, 0) + 1
        for v in violations[: args.max_report]:
            print(f"  RED  {v}")
        if len(violations) > args.max_report:
            print(f"  ... {len(violations) - args.max_report} more")
        if violations:
            summary = ", ".join(f"{k}={v}" for k, v in sorted(by_detector.items()))
            print(f"RED    {len(violations)} violation(s)  [{summary}]")
        else:
            print("GREEN  no appearance-invariant violations")

    # A planted run MUST be red. If it is not, the detector is broken and the
    # tool must not report a clean bill of health.
    if plant_note and not violations:
        print(
            "BROKEN: the planted negative did not redden any detector — "
            "this tool cannot be trusted until that is fixed",
            file=sys.stderr,
        )
        return 4

    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
