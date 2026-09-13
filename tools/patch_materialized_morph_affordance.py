#!/usr/bin/env python3
"""Make the morph slider's precondition legible instead of merely enforced.

The morph slider interpolates the 64-band field between snapshot A and
snapshot B.  With either slot empty it has no endpoints and does nothing, and
the shipping UI already ENFORCED that: `disabled` forwards to
`View::setEnabled(false)` on the native host, `onPointerDown` and
`commitFromPointer` return early, and `bank.setMorph` re-checks the same
precondition against its own mirror.  The gate is real and reactive --
`snapshotStatus` is refreshed from `populated` on every accepted native
response, so capturing a slot enables the control and a slot going empty
disables it again.

What it never did was EXPLAIN it.  The disabled state was carried by opacity
alone: a user who had not captured both slots saw a dim unlabelled 90px track
that silently ignored every click, with nothing anywhere saying that a
snapshot was what it was waiting for.  Reported as "i can't figure out how to
see the morph slider actually morph".

So the fix is rendered content -- the control states its own precondition,
naming the slot that is actually missing.

WHERE that sentence is rendered is the second half of the fix, and the first
attempt got it wrong.  The reason was painted INSIDE the slider's own 90x16
groove, so a half-configured control read as `A ---- SET B ---- B`: a line of
instructional text inside a track, which looks like a rendering fault rather
than guidance.  Reported as "it seems like a bug the way it's displayed I like
the intent".

The Pulp Design System settles both halves and this script implements it
without reinterpretation.  Its disabled state is `disabled . 42` -- OPACITY
ONLY, no instructional text on the control -- and its caption treatment is
mono / 10px / faint.  So the dim is now ONE expression on the row (dimming the
"A" label, the groove, the fill and the thumb together, at the .42 the
guideline names) and the sentence moved OUT of the groove to a caption below
the row.

The caption reuses the transport row's OWN dim-caption treatment rather than
inventing one: `fontSize: 10` at `opacity: 0.55` over the bar's inherited
`var(--mono)` / `rgba(255,255,255,0.7)` -- which is exactly what the
"SNAPSHOT" label four items to its left already does, and which resolves to
about #636568, within a hair of Spectr's own `--dim: #6b7380` token.  The
design system's `--font-mono` and `--text-faint` are spelled in the app's
vocabulary, not imported.

TWO CONSTRAINTS SHAPE THE IMPLEMENTATION, both learned by getting it wrong:

1.  A `title` tooltip is not an affordance here.  The materialized runtime
    classifies `title` as a DOM *semantic* prop (`isDomSemanticProp`,
    alongside `role`/`name`/`aria-*`/`data-*`) and writes it into the shim's
    attribute map for test probes.  Nothing renders it and the native host has
    no hover-tooltip path, so a twelfth `title:` would have looked like an
    affordance in the diff and shown the user nothing.

2.  THE CAPTION IS OUT OF FLOW ON BOTH AXES, and the vertical half is not
    belt-and-braces.  Horizontally the reason is history: a first attempt
    added a visible "MORPH" caption and a reason caption as new flex siblings
    -- about 93px -- and the transport row absorbed it by crushing the control
    instead of growing, rendering the 90x16 track at 39.5px with both flanking
    labels collapsed to zero.  Vertically it is a state-change artefact.  An
    in-flow caption turns the control's box from 20px tall to about 35px, and
    the transport bar centres its items, so the TRACK would ride 7.5px up --
    off the centreline its 26px button neighbours sit on -- and then JUMP BACK
    DOWN the moment the second slot was captured and the caption unmounted.  A
    control that moves when it becomes usable is worse than the defect being
    fixed.  Measured on the shipping build: the group is x=758.188 y=822.500
    w=116.750 h=20.000 inside a 56px bar at y=804, and the buttons beside it
    are h=26 at y=819.500.

    So the caption is `position: "absolute"` under the row: it costs the row
    no width AND no height, the track keeps its exact y, and nothing moves
    when the control becomes enabled.  Being out of flow also keeps it out of
    the wrapper's measured size, which is what lets the group stay 116.750
    wide while the caption's own box runs wider.

    It is anchored at `left: 0` with `whiteSpace: "nowrap"` rather than
    centred in a fixed width, so it grows RIGHTWARD into the 343px flexible
    spacer that follows the group and can never reach back over the
    `recall B` button to its left.

THE DIM IS ONE EXPRESSION, ON THE ROW.  It was briefly three -- one per
painted child of the track -- because the caption lived inside the track and
would otherwise have been dimmed away by the wrapper that dimmed the control.
With the caption a SIBLING of the row rather than a descendant of the track,
that constraint is gone: `opacity: hasBoth ? 1 : 0.42` on the row dims the
"A" label, the groove, the fill and the thumb uniformly, and the caption
below is untouched.  The thumb no longer hides itself at `opacity: 0` either
-- "opacity only" means the control still looks like a slider while it is
unavailable, not that half of it disappears.

THE THUMB IS A PILL, and this script is the only writer of that style.

The design source drew this control as a native `<input type="range">`, which
the native editor cannot host, so it was reimplemented here as a custom
`div[role=slider]` with a hand-drawn thumb.  That reimplementation picked a
circle, and the circle was incidental to it -- no design asked for one.  The
thumb is 22x14 idle and 26x16 hovered, fully rounded, so it reads as a capsule
along the axis it is dragged on.

The travel changed with the shape, and that part is a correctness fix rather
than a taste one.  The circle was positioned `left: ratio%` with a FIXED
`marginLeft` of half its width, so at either end it hung half outside the
track: at ratio 0 it sat at x -7..7 of a 0..90 track, straight on top of the
flanking "A" label, and the same 7px past "B" at the other end.  The pill
insets its own travel instead -- `marginLeft: -(width * ratio)` -- so its left
edge runs 0 -> 90-width and the painted thumb is inside the painted track at
every value, which is also how a real range input behaves.  Vertically it is
inside too: 14px at top 1 and 16px at top 0 in a 16px track, where the circle
grew to 18px at top -1 and overhung by 1px on hover.

Deliberately NOT changed, because each is load-bearing for an existing test:
`id="spectr-snapshot-morph"`, `data-spectr-morph`, `data-spectr-morph-state`,
the 90x16 track geometry, `data-spectr-morph-thumb-state`, and the pointer
protocol -- `test/test_native_state_parity.cpp` resolves the widget by id and
asserts that geometry, and it drives the control in its DEFAULT (no snapshots)
state, so the thumb must keep rendering while disabled.  Enablement SEMANTICS
are untouched: this patch changes what the user is told and what the thumb
looks like, never what the control permits, so the browser-lane tests in
`test/test_editor_analyzer_browser.mjs` that assert `morph.disabled` against
`resources/editor.html` keep measuring what they did.

The settings sliders carry the SAME pill, written by
`tools/patch_materialized_slider_pill.py`.  The split is not arbitrary: the
morph thumb lives inside the contiguous track block this script replaces
wholesale, so a second writer reaching into that block would break this
script's replay.  One writer per patch point; two patch points.

`resources/editor.html` is deliberately not mirrored: it is the browser
bootstrap, not the shipping surface, and `test_import_fidelity.cpp`'s
kPatchNeedles pin its pre-patch shape on purpose.  The durable record of this
change is this file.

Applied as a surgical edit because the materialized generator does not run on
this checkout (`tools/patch_materialized_editor.py` aborts with missing
needles and writes nothing).  This script edits the `html` payload of the
shipping runtime document by exact-text substitution, asserts every patch
point is unique before writing, and re-checks the result.

NOTE: the document is compiled into the binary by `pulp_add_binary_data`
(CMakeLists.txt `spectr_native_assets`), so a rebuild is REQUIRED before any
native test or `Spectr-native-shot` capture reflects this patch.  Running the
native lane without rebuilding measures the previous document and reads as a
pass.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# The call site stops collapsing the two slots into one boolean: the reason
# text has to name the missing slot, and `hasBoth` cannot carry that.
CALLSITE_OLD = (
    'React.createElement(MorphSlider, '
    '{ bankRef, hasBoth: snapshotStatus.A && snapshotStatus.B })')
CALLSITE_NEW = (
    'React.createElement(MorphSlider, '
    '{ bankRef, hasA: snapshotStatus.A, hasB: snapshotStatus.B })')

# `hasBoth` keeps its exact former meaning and every downstream use of it is
# untouched, so the enablement rule is provably the same rule.
SIGNATURE_OLD = (
    'function MorphSlider({ bankRef, hasBoth }) {\n'
    '  const [v, setV] = useStateChrome(0);')
SIGNATURE_NEW = (
    'function MorphSlider({ bankRef, hasA, hasB }) {\n'
    '  const hasBoth = Boolean(hasA && hasB);\n'
    '  const [v, setV] = useStateChrome(0);')

# The control becomes a two-level group: an outer box that owns the margin and
# anchors the caption, and the ORIGINAL flex row inside it holding "A", the
# track and "B".  The nesting is what keeps `morph_row_clearance.py` pointed at
# something it understands -- that detector resolves the track's parent and
# requires EXACTLY the two flanking labels as siblings, so hanging the caption
# off the row itself would have made it report UNMEASURED (exit 2) rather than
# a verdict.  As a child of the outer box instead, the caption is the track's
# uncle and the row is untouched.
#
# Three recognised spellings, so this converges from any prior state: the
# pristine generated wrapper (which dimmed everything including any caption),
# the wrapper that gave up its dim when the caption moved inside the track, and
# the nested form below.  The trailing `}, ` is part of the anchor so the
# replacement stops short of the "A" label, which is a separate patch point.
WRAPPER_OLDS = [
    ('React.createElement("div", { style: { display: "flex", '
     'alignItems: "center", gap: 6, marginLeft: 6, '
     'opacity: hasBoth ? 1 : 0.35 } }, '),
    ('React.createElement("div", { style: { display: "flex", '
     'alignItems: "center", gap: 6, marginLeft: 6 } }, '),
    # The nested form before it centred the row inside itself.
    ('React.createElement("div", { style: { position: "relative", '
     'marginLeft: 6, flexShrink: 0 } }, '
     '/* @__PURE__ */ React.createElement("div", { style: { display: "flex", '
     'alignItems: "center", gap: 6, opacity: hasBoth ? 1 : 0.42 } }, '),
]
# `justifyContent: "center"` is NOT decoration. The outer box resolves to
# 20.000px tall while the row inside it is 16.000, and a Yoga column defaults
# to `flex-start`, so without it the row pins to the top of the outer box and
# the 90x16 TRACK RIDES 2.000px UP -- measured 824.500 -> 822.500, off the
# 832.500 centreline its 26px button neighbours sit on. Centring the row
# inside the outer box puts the track back at exactly the y it shipped at, so
# this change moves the control not at all.
WRAPPER_NEW = (
    'React.createElement("div", { style: { position: "relative", '
    'marginLeft: 6, flexShrink: 0, justifyContent: "center" } }, '
    '/* @__PURE__ */ React.createElement("div", { style: { display: "flex", '
    'alignItems: "center", gap: 6, opacity: hasBoth ? 1 : 0.42 } }, ')

# The caption, below the row rather than inside the groove.
#
# `top: 20` is the outer box's own height (measured 20.000 on the shipping
# build), so the caption starts flush under the row and 2px under the painted
# track, and its 13px line box ends 4.5px above the transport bar's bottom
# edge.  Both numbers are read off a capture, not chosen: the group sits at
# y=822.500 h=20.000 in a bar spanning y=804..860.
#
# The treatment is the transport bar's own faint-caption treatment, not a new
# one -- `fontSize: 10` at `opacity: 0.55` over the bar's inherited
# `var(--mono)` and `rgba(255,255,255,0.7)`, which is what the "SNAPSHOT"
# label in the same row uses.  `fontFamily` and `lineHeight` are pinned rather
# than inherited for the same reason the menu-item caption helper pins them:
# an unpinned nested box resolves its line box against a different multiplier
# than a bare text child and measures taller than its siblings.
CAPTION = (
    'hasBoth ? null : /* @__PURE__ */ React.createElement("div", '
    '{ "data-spectr-morph-hint": true, style: { position: "absolute", '
    'left: 0, top: 20, whiteSpace: "nowrap", pointerEvents: "none", '
    'fontFamily: "var(--mono)", fontSize: 10, lineHeight: "13px", '
    'opacity: 0.55 } }, '
    'hasA ? "SET B TO MORPH" : (hasB ? "SET A TO MORPH" '
    ': "SET A + B TO MORPH"))')

# Closing the two boxes the wrapper edit opened, and hanging the caption off
# the outer one.  Anchored on the function's own tail rather than on the "B"
# label, because the label is a separate patch point and an anchor that spans
# two of them cannot be replayed independently.
CLOSE_OLD = ', "B"));\n}\n\nwindow.Chrome = Chrome;'
CLOSE_NEW = ', "B")), ' + CAPTION + ');\n}\n\nwindow.Chrome = Chrome;'

# ...re-applied to the three painted children it actually describes, and the
# reason caption appended inside the track: absolutely positioned, so it is out
# of flex flow and costs the transport row no width at all.
def track_text(name):
    return open(os.path.join(REPO, "tools", name), encoding="utf-8").read()


# THE FLANKING "A"/"B" LABELS HAVE TO BE BOXES, not bare inline spans.
#
# They were authored as `<span>`, and a span carries no layout box here: the
# native runtime gives it no measured width, so Yoga lays it out at ~0 main
# size AT THE ROW'S CONTENT ORIGIN and the glyph simply paints there,
# overflowing a box that never grew.  The 90px track -- the next flex item --
# therefore started at the SAME x as the "A" label instead of 6px past it, and
# everything painted inside the track landed on top of that glyph: the 22px
# pill at value 0 covered the "A" completely (measured: label box
# 758.188..763.594, thumb 758.188..780.188), and the disabled caption's ink
# started on it too.
#
# That second one is why this was mistaken for cosmetics.  `box_intersection`
# only compares nodes that CARRY TEXT, so the thumb -- a text-free div -- was
# structurally invisible to it and only the caption half of the same defect
# ever surfaced, as a 5.4x12px "A"-vs-caption pair that looked pre-existing and
# was waved through as such.  It was not pre-existing decoration; it was this.
#
# `whiteSpace: "nowrap"` alone does NOT fix it.  It makes the span's INK
# measurable (0.0 -> 6.0px) while its layout box stays 5.406 and the track
# stays put -- measured, and a persuasive false fix.  The element has to become
# a box.  With that, the row flows as the style always said it did:
# A 758.188..764.188, gap 6, track 770.188..860.188, gap 6, B 866.188..872.188.
#
# `flexShrink: 0` on both labels pins the fix under contention.  The transport
# row has no spare width, and an earlier attempt to add siblings to it was
# absorbed by CRUSHING the control -- a 90x16 track rendered at 39.5px with
# both end labels collapsed to zero.  Labels that cannot shrink cannot collapse
# that way again, and the track carries the same guard in its own block so the
# control is never the give.
def label_box(letter):
    return ('React.createElement("span", { style: { fontSize: 9, '
            'color: "rgba(255,255,255,0.5)" } }, "%s")' % letter,
            'React.createElement("div", { style: { fontSize: 9, '
            'color: "rgba(255,255,255,0.5)", whiteSpace: "nowrap", '
            'flexShrink: 0 } }, "%s")' % letter)


LABEL_A_OLD, LABEL_A_NEW = label_box("A")
LABEL_B_OLD, LABEL_B_NEW = label_box("B")

# The track block has THREE recognised spellings, and this script owns all of
# them so the style stays single-writer:
#   *_before  the pristine generated block (circle thumb, no caption)
#   *_circle  the affordance block as first shipped (circle thumb + caption)
#   *_pill    the pill thumb before the track refused to be the item that
#             shrinks when the transport row tightens
#   *_hinted  the pill thumb with the reason caption still painted INSIDE the
#             groove, and the dim split across the three painted children
#   *_after   the block this script now installs -- no text in the track, and
#             no per-child dim, because the row carries it
# Listing every intermediate as an alternative patch point is what lets the
# edit stay idempotent across a shape change: a document already carrying an
# older spelling is upgraded in place rather than reported as unpatchable.
TRACK_OLDS = [track_text("morph_affordance_track_before.txt"),
              track_text("morph_affordance_track_circle.txt"),
              track_text("morph_affordance_track_pill.txt"),
              track_text("morph_affordance_track_hinted.txt")]
TRACK_NEW = track_text("morph_affordance_track_after.txt")

EDITS = [
    ('morph slider receives both slots, not their conjunction',
     [CALLSITE_OLD], CALLSITE_NEW),
    ('hasBoth is derived, so the enablement rule is unchanged',
     [SIGNATURE_OLD], SIGNATURE_NEW),
    ('the control nests one level so the caption can hang below the row, '
     'and the row carries the whole disabled dim',
     WRAPPER_OLDS, WRAPPER_NEW),
    ('the flanking "A" label is a box, so the track starts past it',
     [LABEL_A_OLD], LABEL_A_NEW),
    ('the flanking "B" label is a box, so the track ends before it',
     [LABEL_B_OLD], LABEL_B_NEW),
    ('the track paints a pill inside itself and nothing else -- no '
     'instructional text in the groove, no per-child dim',
     TRACK_OLDS, TRACK_NEW),
    ('the caption says why the control is unavailable, below the row',
     [CLOSE_OLD], CLOSE_NEW),
]

# No reader may still expect a `hasBoth` prop from outside, and the wrapper
# must not dim the caption it now contains.
FORBIDDEN_AFTER = (
    'marginLeft: 6, opacity: hasBoth ? 1 : 0.35 } }',
    'function MorphSlider({ bankRef, hasBoth })',
    'MorphSlider, { bankRef, hasBoth:',
    # The circle this control was reimplemented with, and the overhanging
    # travel that came with it.
    'width: grown ? 18 : 14, height: grown ? 18 : 14',
    'marginLeft: grown ? -9 : -7',
    # The inline spans that carried no layout box, so the track began at the
    # "A" label's own x and painted over it.
    LABEL_A_OLD,
    LABEL_B_OLD,
    # THE DEFECT THIS PATCH POINT EXISTS FOR: instructional text painted
    # inside the slider's own groove, which reads as a rendering fault. Both
    # the shape of that node and the wording it carried are refused, so a
    # regression cannot creep back under a reworded string.
    ('"data-spectr-morph-hint": true, style: { position: "absolute", '
     'left: 0, top: 1, width: "100%", height: 14'),
    'hasA ? "SET B" : (hasB ? "SET A" : "SET A + B")',
    # The dim split across the track's painted children, and the .35 it used.
    # The row carries one .42 now, which is the value the design system names.
    'opacity: hasBoth ? 1 : 0.35',
    # The thumb erasing itself while disabled. "Opacity only" means the
    # control still looks like a slider, not that half of it vanishes.
    'opacity: hasBoth ? 1 : 0, background: "#fff"',
)
REQUIRED_AFTER = (
    '"data-spectr-morph-hint": true',
    ('hasA ? "SET B TO MORPH" : (hasB ? "SET A TO MORPH" '
     ': "SET A + B TO MORPH")'),
    # ...in the design system's caption treatment, spelled in the app's own
    # vocabulary: the transport bar's `var(--mono)` at 10px, faint.
    ('fontFamily: "var(--mono)", fontSize: 10, lineHeight: "13px", '
     'opacity: 0.55'),
    # One dim, on the row, at the value the guideline names.
    'opacity: hasBoth ? 1 : 0.42',
    # ...and the row stays centred in the outer box, so the track keeps its y.
    'marginLeft: 6, flexShrink: 0, justifyContent: "center"',
    'const hasBoth = Boolean(hasA && hasB);',
    # The gate itself survives verbatim -- this patch explains the rule, it
    # does not relax it.
    '"data-spectr-morph-state": hasBoth ? "enabled" : "disabled"',
    'disabled: !hasBoth',
    'if (!hasBoth) return;',
    'if (!hasBoth || !track || !track.getBoundingClientRect) return;',
    # Geometry the native parity test resolves by id and asserts, and the
    # thumb it drives in the DEFAULT (disabled) state.
    'id: "spectr-snapshot-morph"',
    'width: 90, height: 16',
    # The pill, and the travel that keeps it inside the 90px track: `left` is
    # the ratio as a percentage and the negative margin is the same ratio of
    # the thumb's own width, so the thumb's left edge runs 0 -> 90 - width
    # instead of overhanging both ends by half its width.
    'width: grown ? 26 : 22, height: grown ? 16 : 14',
    'marginLeft: -((grown ? 26 : 22) * ratio)',
    '"data-spectr-morph-thumb-state": grown ? "hover" : "idle"',
    # The flanking labels take real width in the flex row, so the track is
    # laid out past them instead of on top of them, and neither label can
    # collapse to zero if the transport row tightens.
    LABEL_A_NEW,
    LABEL_B_NEW,
    # ...and the control is never the item that gives when it does.
    'width: 90, height: 16, flexShrink: 0',
)


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for label, olds, new in EDITS:
        for old in olds:
            if old and old in new:
                sys.exit('FAIL %s: patch point survives its own replacement'
                         % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    for label, olds, new in EDITS:
        new_e = escaped(new)
        if raw.count(new_e) >= 1:
            print('already applied ', label)
            continue
        # Exactly one of the recognised spellings must be present, exactly
        # once. Two matching spellings would mean the block is duplicated and
        # a blind replace would edit only one of them.
        hits = [old for old in olds if raw.count(escaped(old)) == 1]
        if len(hits) != 1:
            counts = ', '.join(str(raw.count(escaped(old))) for old in olds)
            sys.exit('FAIL %s: recognised patch points occur [%s] times, '
                     'expected exactly one of them once' % (label, counts))
        raw = raw.replace(escaped(hits[0]), new_e)
        changed = True
        print('applied         ', label)

    # Stronger than the old half-patched guard, and version-agnostic: every
    # edit must be in its FINAL state once, whether this run put it there or a
    # previous one did.
    for label, _olds, new in EDITS:
        count = raw.count(escaped(new))
        if count != 1:
            sys.exit('FAIL %s: final text occurs %d times after patching, '
                     'expected 1' % (label, count))

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change needed')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
