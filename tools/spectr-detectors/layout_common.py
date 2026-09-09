"""Shared reader for pulp `dump_layout_tree` snapshots (schema visual-layout-snapshot-v1).

Semantics are taken from core/view/src/layout_snapshot.cpp, which is the emitter:

  node.rect            absolute rect, accumulated as parent_abs + child.bounds().
                       ScrollView applies its scroll offset at PAINT time
                       (canvas.translate(-sx,-sy) in ScrollView::paint_all), and
                       lays children out against an EXPANDED content box, so the
                       rects of anything inside a scroll container are in
                       UNSCROLLED CONTENT SPACE, not screen space.
  node.clipping.rect   inherited_clip, intersected with the node's own rect when
                       its overflow is hidden|scroll.  View::paint_all clips a
                       hidden/scroll view's OWN painted box to (0,0,w,h) before
                       calling paint(), so this rect is also the clip that
                       constrains the node's own pixels.
  measured_text_boxes  Label      -> rect.w = Label::intrinsic_width(), the
                                    natural shaped-text advance the painter
                                    will draw (0 for multi-line labels, which
                                    deliberately report no intrinsic width).
                       TextEditor,
                       TextButton,
                       Hyperlink  -> rect.w = the node's own width.  Comparing
                                    that against the node width is a tautology;
                                    these kinds carry NO painted-width signal.
                       rect.x/y   = the NODE origin, not the ink origin.  Text
                                    alignment is not represented, so a centered
                                    or right-aligned string's ink sits to the
                                    right of the reported x.

The pre-order node list carries no depth, so the `.depths.json` sidecar written
next to the dump by Spectr's SPECTR_LAYOUT_DUMP hook is required to recover
ancestry.  Ancestry cannot be inferred from rect containment: a scrolled row
routinely escapes its parent's bounds.
"""

import json
import os

SCROLL_OVERFLOWS = ("scroll",)
CLIP_OVERFLOWS = ("hidden", "scroll")


class Dump:
    def __init__(self, path, depths_path=None):
        self.path = path
        with open(path) as fh:
            raw = json.load(fh)
        self.schema = raw.get("schema_version")
        self.surface = raw.get("surface")
        self.viewport = raw.get("viewport") or {}
        self.nodes = flatten(raw.get("nodes") or [])
        self.depths, self.depths_path = load_depths(path, depths_path, len(self.nodes))
        self.parent = build_parents(self.depths) if self.depths else None

    # -- ancestry ---------------------------------------------------------
    def ancestors(self, i):
        if self.parent is None:
            return []
        out = []
        p = self.parent[i]
        while p is not None:
            out.append(p)
            p = self.parent[p]
        return out

    def is_related(self, i, j):
        """True when one node is an ancestor of the other (or they are equal)."""
        if i == j:
            return True
        if self.parent is None:
            return False
        return j in self.ancestors(i) or i in self.ancestors(j)

    def scroll_frame(self, i):
        """Index of the nearest STRICT ancestor with overflow 'scroll', else None.

        Two nodes share a coordinate space only when this matches: everything
        inside one scroll container shifts together, so sibling geometry inside
        it stays exact even though its absolute position does not.
        """
        if self.parent is None:
            return "unknown"
        for a in self.ancestors(i):
            if self.nodes[a].get("overflow") in SCROLL_OVERFLOWS:
                return a
        return None

    def effectively_visible(self, i):
        if not self.nodes[i].get("visible", True):
            return False
        if self.parent is None:
            return True
        return all(self.nodes[a].get("visible", True) for a in self.ancestors(i))

    def label(self, i):
        n = self.nodes[i]
        nid = n.get("id") or ""
        return "#%d %s%s" % (i, n.get("kind", "?"), (" '%s'" % nid) if nid else " <no id>")


def flatten(nodes):
    """Accept the flat pre-order list the emitter writes, or a nested tree."""
    out = []

    def walk(seq):
        for n in seq:
            out.append(n)
            kids = n.get("children")
            if kids is None:
                kids = n.get("nodes")
            if isinstance(kids, list):
                walk(kids)

    walk(nodes)
    return out


def load_depths(dump_path, explicit, node_count):
    cands = []
    if explicit:
        cands.append(explicit)
    else:
        if dump_path.endswith(".layout.json"):
            cands.append(dump_path[: -len(".layout.json")] + ".depths.json")
        cands.append(dump_path + ".depths.json")
        base, _ = os.path.splitext(dump_path)
        cands.append(base + ".depths.json")
    for c in cands:
        if os.path.exists(c):
            with open(c) as fh:
                d = json.load(fh)
            if len(d) == node_count:
                return d, c
            raise SystemExit(
                "depths sidecar %s has %d entries but the dump has %d nodes"
                % (c, len(d), node_count)
            )
    return None, None


def build_parents(depths):
    parent = [None] * len(depths)
    stack = {}
    for i, d in enumerate(depths):
        stack[d] = i
        parent[i] = stack.get(d - 1) if d > 0 else None
    return parent


def r(rect):
    return (
        float(rect.get("x", 0.0)),
        float(rect.get("y", 0.0)),
        float(rect.get("w", 0.0)),
        float(rect.get("h", 0.0)),
    )


def overlap_1d(a0, aw, b0, bw):
    lo = max(a0, b0)
    hi = min(a0 + aw, b0 + bw)
    return max(0.0, hi - lo)


def intersect(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return overlap_1d(ax, aw, bx, bw), overlap_1d(ay, ah, by, bh)


def fmt_rect(rect):
    x, y, w, h = rect
    return "x=%.1f y=%.1f w=%.1f h=%.1f" % (x, y, w, h)
