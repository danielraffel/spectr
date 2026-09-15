#!/usr/bin/env python3
"""A selected muted band highlights its MUTE BUTTON, not the glyph inside it.

THE DEFECT, as reported: select a run of bands, press `m`, and the selection
mark lands as "a small bright box around an icon sitting inside a larger
unhighlighted button".  The screenshot shows four adjacent bands, two muted and
two not, and the muted ones carry a bright sliver straight across the speaker
glyph in the middle of an otherwise unmarked chip.

WHY THE MARK COLLAPSES ONTO THE GLYPH, which is not a styling choice anywhere.
`drawBands` outlines the band BODY:

    ctx.strokeRect(
      Math.round(G.cx - G.innerW / 2) - 2.5,
      Math.round(Math.min(G.topY, G.botY)) - 2.5,
      Math.round(G.innerW) + 5,
      Math.round(Math.abs(G.botY - G.topY)) + 5
    );

and a muted band HAS no body.  Its painted value is the sentinel `-Infinity`,
which the painter's own normalisation turns into zero:

    const effectiveGains = rg.map((v) => Number.isFinite(v) ? clamp(v, -1.02, 1.02) : 0);

so `gval` is 0, `topY` and `botY` are both `zeroY`, and the rect degenerates to
a box 5px tall (the two 2.5 outsets and nothing between them) centred exactly on
the zero line.  The mute chip is centred on the same line and is 18..26px tall,
and the speaker glyph sits at its middle -- so the 5px sliver lands on the
glyph, inside a chip it does not touch.  Nothing is drawn in the wrong place;
the mark is measuring a body that is not there.

THE FIX.  When the chip is painted, the selection outlines the CHIP.  A muted
band's button is the only thing it has that a selection can be about, and the
user's ask was exactly that: distinguish the button as selected.

  * a `rgba(255,255,255,0.12)` tint over the chip, so the button reads lifted
    rather than merely outlined.  Deliberately low: the glyph is drawn before
    this and is composited under it, and at 0.12 its contrast against the chip
    stays above 8:1.
  * the existing `rgba(255,255,255,0.85)` 1px selection stroke, at the same 2.5
    outset it already uses, around the chip's box instead of the body's.

An UNSELECTED muted neighbour keeps the chip it always had -- `rgba(18,22,28,
0.92)` fill, `rgba(255,255,255,0.28)` border, no tint and no ring -- so the two
are distinguished by a mark that is present or absent, not by a shade.  A
selected UNMUTED band keeps its body rect unchanged, in the same stroke colour,
so the selection language is one language.

ONE DEFINITION OF THE CHIP BOX.  The ring has to land on the chip to the pixel,
and the chip's geometry was four inline `const`s inside the painter's own loop.
Copying them into the selection branch would have created a second definition
that a later chip restyle could silently leave behind -- a ring measuring a box
the chip no longer has.  So the box becomes `muteChipRect(i, g)`, and BOTH the
chip painter and the selection read it.  That is the whole reason this script
touches the chip loop at all; the painter's output is byte-identical.

WHAT THIS DOES NOT CHANGE.  `muteStyle: "collapse"` paints no chip, so there is
no button to outline and the body rect stands -- which is the honest mark for a
band whose only representation is its absence.  The `continue` above this block
(`G.targetMuted && effectiveGains[i] <= -1.01`) is untouched: it can only fire
while `rg[i]` sits in [-1.02, -1.01], and the collapse snaps through that to
`-Infinity` within the same tick, so it is not a window a selection can be lost
in.

SINGLE WRITER.  No other patch script edits `if (G.isSel)`; the chip loop and
the body/edge rects belong to `patch_materialized_editor.py` only as far as the
`G.edge` block, which is a different rect and is not touched here.

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


# -- 1. one definition of the mute chip's box ------------------------------

DRAWBANDS = '  function drawBands(ctx, g) {\n'
CHIP_RECT = (
    '  // The mute chip\'s box, defined ONCE. The chip painter below and the\n'
    '  // selection ring that outlines it both read this, so a chip restyle\n'
    '  // cannot leave the ring measuring a box the chip no longer has.\n'
    '  function muteChipRect(i, g) {\n'
    '    const h = Math.min(26, Math.max(18, g.inner.h * 0.12));\n'
    '    return {\n'
    '      x: g.inner.x + i * (g.bandW + g.bandGap) + 0.5,\n'
    '      y: g.zeroY - h / 2,\n'
    '      w: g.bandW - 1,\n'
    '      h\n'
    '    };\n'
    '  }\n'
)

# -- 2. the painter reads it instead of recomputing it ---------------------
#
# Same numbers, same order, same names downstream: `chipX/chipY/chipW/chipH`
# are referenced five more times below this and all of them keep working.

CHIP_CONSTS = (
    '        const chipH = Math.min(26, Math.max(18, inner.h * 0.12));\n'
    '        const chipY = zeroY - chipH / 2;\n'
    '        const chipX = x + 0.5;\n'
    '        const chipW = bandW - 1;\n'
)
CHIP_CONSTS_NEW = (
    '        const chipBox = muteChipRect(i, g);\n'
    '        const chipH = chipBox.h;\n'
    '        const chipY = chipBox.y;\n'
    '        const chipX = chipBox.x;\n'
    '        const chipW = chipBox.w;\n'
)

# -- 3. the selection outlines the button when there is one ----------------

SEL = (
    '      if (G.isSel) {\n'
    '        ctx.strokeStyle = "rgba(255,255,255,0.85)";\n'
    '        ctx.lineWidth = 1;\n'
    '        ctx.strokeRect(\n'
    '          Math.round(G.cx - G.innerW / 2) - 2.5,\n'
    '          Math.round(Math.min(G.topY, G.botY)) - 2.5,\n'
    '          Math.round(G.innerW) + 5,\n'
    '          Math.round(Math.abs(G.botY - G.topY)) + 5\n'
    '        );\n'
    '      }\n'
)
SEL_NEW = (
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


EDITS = [
    ('the mute chip has one box, read by its painter and by the selection',
     (DRAWBANDS, CHIP_RECT + DRAWBANDS),
     "function muteChipRect(i, g) {"),

    ('the chip painter reads that box instead of recomputing it',
     (CHIP_CONSTS, CHIP_CONSTS_NEW),
     "const chipBox = muteChipRect(i, g);"),

    ('a selected muted band outlines its mute button, not its missing body',
     (SEL, SEL_NEW),
     'if (G.targetMuted && muteStyle === "cutout") {'),
]

# Asserted present after every run. The chip's own paint is the control: if a
# future restructure moved it, the ring would be measuring nothing and this
# script would have silently become decorative.
REQUIRED_AFTER = (
    "function muteChipRect(i, g) {",
    "const chipBox = muteChipRect(i, g);",
    'if (G.targetMuted && muteStyle === "cutout") {',
    "roundRect(ctx, sel.x - 2.5, sel.y - 2.5, sel.w + 5, sel.h + 5, 5);",
    'ctx.fillStyle = "rgba(18,22,28,0.92)";',
    "roundRect(ctx, chipX, chipY, chipW, chipH, 3);",
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. Every edit is anchored inside
    # the band painter and its mute chip; a document without both is one this
    # script must refuse rather than no-op into "already applied".
    control = (raw.count(enc("function drawBands(ctx, g) {"))
               + raw.count(enc('if (muteStyle === "cutout") {'))
               + raw.count(enc("      if (G.isSel) {")))
    print("control: %d painter anchors (drawBands + cutout chip + isSel)"
          % control)
    if control != 3:
        print("FAIL: expected 3 anchors, found %d -- wrong document" % control,
              file=sys.stderr)
        return 1

    applied, already = [], []
    for label, (find, replace), done in EDITS:
        # `done` alone decides. The first edit's replacement CONTAINS its own
        # needle (an insertion before an anchor leaves the anchor in place), so
        # a rule that also required `find == 0` would re-apply it every run.
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
        if raw.count(enc(token)) == 0:
            print("FAIL: %r is absent after patching" % (token,), file=sys.stderr)
            return 1

    # Parse to adjudicate, never to write: a broken payload here is an editor
    # that does not load at all, and the artifact is one logical line so a human
    # diff will not catch it.
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
