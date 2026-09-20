#!/usr/bin/env python3
"""A selected band is marked by a tint across its body, not only a hairline.

THE DEFECT, as reported: "I suspect the rubberband highlight color when we
select the band might need to be a little bit more obvious.  I know we're just
making a little bit more light around the edges and I do see that but it's not
the most obvious."

That is an exact description of what the painter does.  `drawBands` marks a
selected band with one hairline:

    ctx.strokeStyle = "rgba(255,255,255,0.85)";
    ctx.lineWidth = 1;

and nothing else.  Three separate things make that hairline hard to read, and
only the third is about the colour being dim:

  * IT IS THE SAME INK AS THE SPECTRUM CURVE.  The average/peak traces are
    drawn over the band field in near-white, so a white 1px rectangle is
    competing with a white 1px curve crossing it.  The mark is not just faint,
    it is ambiguous -- there is nothing that says "selection" rather than
    "curve".
  * AN EDGE IS ONLY VISIBLE WHERE IT MEETS A CONTRAST.  A run of adjacent
    selected bands has no unselected neighbour BETWEEN its members, so the
    interior verticals of the run land band-on-band and mostly vanish.  What
    survives is the outer boundary of the run -- which is why a wide selection
    reads as a thin box around a group rather than as N selected bands.
  * 0.85 alpha on a 1px line, over a band body that is itself bright at high
    gain, is a low-contrast mark at exactly the moment the body is loudest.

THE FIX, in the order the three causes above demand:

  * A TINT ACROSS THE INTERIOR.  This is the substantive change.  It is the
    only cue that survives when adjacent selected bands merge their edges,
    because it is an area rather than a boundary, and it is fully contained
    inside the band's own rect so it cannot bleed onto an unselected
    neighbour.  `rgba(150,210,255,0.14)` -- deliberately low, because the band
    body encodes GAIN by brightness and a heavy tint would corrupt the reading
    it sits on top of.
  * A DISTINCT HUE.  The mark moves off white and into the blue the marquee
    already uses (`rgba(180,220,255,0.55)` in `drawMarquee`), so the rubber
    band that MAKES a selection and the selection it made are one visual
    language, and neither is confusable with the white spectrum curve.  The
    hue is fixed rather than theme-derived on purpose: it stays distinguishable
    on the amber themes (`sodium`) as well as the cool ones, which a
    theme-tinted mark would not.
  * A 2px EDGE at full alpha instead of 1px at 0.85.

GEOMETRY IS UNCHANGED.  Both branches keep the rects they already had, at the
same 2.5px outset.  The stroke widening from 1px to 2px extends the painted
mark 0.5px further out than before and nothing else moves, so this cannot
reflow anything or reach a neighbouring band: `innerW` is `bandW * 0.78` (0.72
at the edges), leaving margin on each side before `bandGap` even begins.  A
wide soft halo was considered for the "glow" reading and REJECTED for exactly
that reason -- at a legible width it reaches across the gap and lands on the
unselected neighbour, which makes the boundary of a selection mushier, not
clearer.

ONE DEFINITION OF THE MARK.  The muted-chip branch and the band-body branch
previously carried their own fill and stroke calls, which is how the chip
branch ended up with a tint (`rgba(255,255,255,0.12)`) that the body branch
never had.  Both now call `paintSelection`, so the two cannot drift again and
a future restyle has one place to change.

WHAT THIS DOES NOT CHANGE.  `drawSelection(ctx, g)` is an EMPTY function called
from `renderAll` on the overlay canvas; it is dead and stays dead -- the
selection has always been painted inside `drawBands`, and moving it would
change the compositing order it currently depends on (the mark is drawn after
the band body, in the same pass).  Mute-chip geometry, `muteChipRect`, the
collapse mute style, and the `continue` for fully-collapsed bands are all
untouched.

SINGLE WRITER.  `patch_materialized_mute_selection_highlight.py` established
the `if (G.isSel)` block's current shape and is the only other script that
edits it.  This one REPLACES that block wholesale and must therefore run AFTER
it; the guard below asserts that script's output is present before writing, so
running them out of order fails loudly instead of silently producing a mark
with no mute branch.

NOTE: the document is compiled into the binary by `pulp_add_binary_data`
(CMakeLists.txt `spectr_native_assets`), so a rebuild is REQUIRED before any
native test reflects this patch.  `Encoding binary asset
materialized-document.runtime.json` in the build log is the proof; "Built
target" is not.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def enc(snippet):
    """The document stores the page as a JSON string, so every needle is
    escaped the way the file stores it. Raw-text surgery, never a load/dump
    round trip: this file's escaping is not uniform, so re-serialising would
    rewrite bytes that have nothing to do with this change."""
    return json.dumps(snippet)[1:-1]


DRAWBANDS = '  function drawBands(ctx, g) {\n'

# -- 1. one definition of the selection mark -------------------------------

PAINT_SELECTION = (
    '  // The selection mark, defined ONCE so the muted-chip branch and the\n'
    '  // band-body branch cannot drift apart. Two passes:\n'
    '  //   * a tint across the interior -- the only cue that survives when\n'
    '  //     adjacent selected bands merge their edges, since an edge is\n'
    '  //     visible only where it meets a contrast and a run of selected\n'
    '  //     bands has none between its members. Kept low: the body encodes\n'
    '  //     gain by brightness and a heavy tint corrupts that reading.\n'
    '  //   * a 2px edge in the marquee\'s own hue, so the rubber band that\n'
    '  //     makes a selection and the selection it made are one language,\n'
    '  //     and neither is confusable with the white spectrum curve drawn\n'
    '  //     across the same field. The hue is fixed, not theme-derived, so\n'
    '  //     it stays distinguishable on the amber themes too.\n'
    '  // `r` of 0 means a square rect; the chip branch passes its corner\n'
    '  // radius so the ring follows the button it marks.\n'
    '  function paintSelection(ctx, x, y, w, h, r) {\n'
    '    ctx.save();\n'
    '    ctx.fillStyle = "rgba(150,210,255,0.14)";\n'
    '    ctx.strokeStyle = "rgba(190,232,255,0.98)";\n'
    '    ctx.lineWidth = 2;\n'
    '    if (r > 0) {\n'
    '      roundRect(ctx, x, y, w, h, r);\n'
    '      ctx.fill();\n'
    '      roundRect(ctx, x, y, w, h, r);\n'
    '      ctx.stroke();\n'
    '    } else {\n'
    '      ctx.fillRect(x, y, w, h);\n'
    '      ctx.strokeRect(x, y, w, h);\n'
    '    }\n'
    '    ctx.restore();\n'
    '  }\n'
)

# -- 2. both branches call it ----------------------------------------------

SEL_OLD = (
    '      if (G.isSel) {\n'
    '        ctx.strokeStyle = "rgba(255,255,255,0.85)";\n'
    '        ctx.lineWidth = 1;\n'
    '        // A muted band has no body to outline: its painted value is the\n'
    '        // `-Infinity` sentinel, which normalises to 0, so topY and botY\n'
    '        // are both zeroY and this rect would collapse to a 5px sliver --\n'
    '        // landing on the speaker glyph at the middle of the mute chip.\n'
    '        // When the chip is painted it is the band\'s button, so that is\n'
    '        // what the selection marks.\n'
    '        if (G.targetMuted && muteStyle === "cutout") {\n'
    '          const sel = muteChipRect(i, g);\n'
    '          ctx.fillStyle = "rgba(255,255,255,0.12)";\n'
    '          roundRect(ctx, sel.x, sel.y, sel.w, sel.h, 3);\n'
    '          ctx.fill();\n'
    '          roundRect(ctx, sel.x - 2.5, sel.y - 2.5, sel.w + 5, sel.h + 5, 5);\n'
    '          ctx.stroke();\n'
    '        } else {\n'
    '          ctx.strokeRect(\n'
    '            Math.round(G.cx - G.innerW / 2) - 2.5,\n'
    '            Math.round(Math.min(G.topY, G.botY)) - 2.5,\n'
    '            Math.round(G.innerW) + 5,\n'
    '            Math.round(Math.abs(G.botY - G.topY)) + 5\n'
    '          );\n'
    '        }\n'
    '      }\n'
)
SEL_NEW = (
    '      if (G.isSel) {\n'
    '        // A muted band has no body to outline: its painted value is the\n'
    '        // `-Infinity` sentinel, which normalises to 0, so topY and botY\n'
    '        // are both zeroY and this rect would collapse to a 5px sliver --\n'
    '        // landing on the speaker glyph at the middle of the mute chip.\n'
    '        // When the chip is painted it is the band\'s button, so that is\n'
    '        // what the selection marks.\n'
    '        if (G.targetMuted && muteStyle === "cutout") {\n'
    '          const sel = muteChipRect(i, g);\n'
    '          paintSelection(ctx, sel.x - 2.5, sel.y - 2.5,\n'
    '                         sel.w + 5, sel.h + 5, 5);\n'
    '        } else {\n'
    '          paintSelection(ctx,\n'
    '            Math.round(G.cx - G.innerW / 2) - 2.5,\n'
    '            Math.round(Math.min(G.topY, G.botY)) - 2.5,\n'
    '            Math.round(G.innerW) + 5,\n'
    '            Math.round(Math.abs(G.botY - G.topY)) + 5,\n'
    '            0\n'
    '          );\n'
    '        }\n'
    '      }\n'
)

EDITS = [
    ('the selection mark has one definition, shared by both branches',
     (DRAWBANDS, PAINT_SELECTION + DRAWBANDS),
     "function paintSelection(ctx, x, y, w, h, r) {"),

    ('a selected band is tinted across its body, not only outlined',
     (SEL_OLD, SEL_NEW),
     "paintSelection(ctx, sel.x - 2.5, sel.y - 2.5,"),
]

# Asserted present after every run.
REQUIRED_AFTER = (
    "function paintSelection(ctx, x, y, w, h, r) {",
    'ctx.fillStyle = "rgba(150,210,255,0.14)";',
    'ctx.strokeStyle = "rgba(190,232,255,0.98)";',
    "paintSelection(ctx, sel.x - 2.5, sel.y - 2.5,",
    # The body branch must still reach the painter. Without this a refactor
    # that dropped the else could leave ONLY muted bands marked, which is the
    # failure this change exists to prevent the inverse of.
    "Math.round(G.cx - G.innerW / 2) - 2.5,",
    # The mark must no longer be white-on-white. If this reappears, some other
    # writer has reinstated the hairline this script replaced.
    'ctx.strokeStyle = "rgba(255,255,255,0.85)";',
)
# ...and that last one must be GONE, not present. Kept separate because the
# assertion is the opposite direction.
REQUIRED_ABSENT = (
    'ctx.strokeStyle = "rgba(255,255,255,0.85)";',
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. Both anchors must exist: the
    # band painter, and the mute-branch shape that the earlier selection patch
    # established. A document missing the latter means this script is running
    # out of order, which must fail rather than no-op.
    painter = raw.count(enc("function drawBands(ctx, g) {"))
    mute_branch = raw.count(enc('if (G.targetMuted && muteStyle === "cutout") {'))
    print("control: %d band painter, %d mute selection branch"
          % (painter, mute_branch))
    if painter != 1 or mute_branch != 1:
        print("FAIL: expected exactly 1 of each -- wrong document, or "
              "patch_materialized_mute_selection_highlight.py has not run",
              file=sys.stderr)
        return 1

    applied, already = [], []
    for label, (find, replace), done in EDITS:
        # `done` alone decides. The first edit's replacement CONTAINS its own
        # anchor (an insertion before it leaves it in place), so a rule that
        # also required `find == 0` would re-apply it every run.
        if raw.count(enc(done)) >= 1:
            already.append(label)
            continue
        found = raw.count(enc(find))
        if found != 1:
            print("FAIL: %r matched %d times, expected exactly 1"
                  % (label, found), file=sys.stderr)
            return 1
        raw = raw.replace(enc(find), enc(replace), 1)
        applied.append(label)

    for token in REQUIRED_AFTER:
        if token in REQUIRED_ABSENT:
            continue
        if raw.count(enc(token)) == 0:
            print("FAIL: %r is absent after patching" % (token,),
                  file=sys.stderr)
            return 1
    for token in REQUIRED_ABSENT:
        if raw.count(enc(token)) != 0:
            print("FAIL: the replaced hairline %r is still present -- another "
                  "writer has reinstated it" % (token,), file=sys.stderr)
            return 1

    # Parse to adjudicate, never to write: a broken payload here is an editor
    # that does not load at all, and the artifact is one logical line so a
    # human diff will not catch it.
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        print("FAIL: the patched document no longer carries an html payload",
              file=sys.stderr)
        return 1

    if not applied:
        print("already applied: %d edit(s), nothing written" % len(already))
        return 0

    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied %d edit(s), %d already present; %d -> %d bytes"
          % (len(applied), len(already), before, len(raw)))
    for label in applied:
        print("  + " + label)
    return 0


if __name__ == "__main__":
    sys.exit(main())
