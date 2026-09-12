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
naming the slot that is actually missing: "SET A + B", "SET A", or "SET B".

TWO CONSTRAINTS SHAPE THE IMPLEMENTATION, both learned by getting it wrong:

1.  A `title` tooltip is not an affordance here.  The materialized runtime
    classifies `title` as a DOM *semantic* prop (`isDomSemanticProp`,
    alongside `role`/`name`/`aria-*`/`data-*`) and writes it into the shim's
    attribute map for test probes.  Nothing renders it and the native host has
    no hover-tooltip path, so a twelfth `title:` would have looked like an
    affordance in the diff and shown the user nothing.

2.  THE TRANSPORT ROW HAS NO WIDTH TO GIVE.  A first attempt added a visible
    "MORPH" caption and a reason caption as new flex siblings -- about 93px --
    and the row absorbed it by crushing the control instead of growing: the
    90x16 track rendered at 39.5px, both flanking "A"/"B" labels collapsed to
    zero width, and the thumb (which overhangs its track by 7px) landed on top
    of the new caption.  `flexShrink: 0` on the additions made it worse by
    protecting them and sacrificing the slider.  So this patch adds NO width:
    the caption is absolutely positioned inside the existing 90x16 track,
    which takes it out of flex flow entirely.  The thumb sits at x -7..7 of
    that track and the centred caption at roughly 19..71, so they do not
    collide.

The dim moves off the wrapper and onto the track's three painted children, so
the caption explaining a dimmed control is not itself dimmed away.  The
flanking "A"/"B" labels keep their normal 0.5 alpha.

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

DIM = 'opacity: hasBoth ? 1 : 0.35, '

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

# The wrapper gives up the dim so the caption it now contains stays readable.
WRAPPER_OLD = (
    '{ style: { display: "flex", alignItems: "center", gap: 6, '
    'marginLeft: 6, opacity: hasBoth ? 1 : 0.35 } }')
WRAPPER_NEW = (
    '{ style: { display: "flex", alignItems: "center", gap: 6, '
    'marginLeft: 6 } }')

# ...re-applied to the three painted children it actually describes, and the
# reason caption appended inside the track: absolutely positioned, so it is out
# of flex flow and costs the transport row no width at all.
def track_text(name):
    return open(os.path.join(REPO, "tools", name), encoding="utf-8").read()


# The track block has THREE recognised spellings, and this script owns all of
# them so the style stays single-writer:
#   *_before  the pristine generated block (circle thumb, no caption)
#   *_circle  the affordance block as first shipped (circle thumb + caption)
#   *_after   the block this script now installs (pill thumb + caption)
# Listing the intermediate as an alternative patch point is what lets the edit
# stay idempotent across the shape change: a document already carrying the
# circle spelling is upgraded in place rather than reported as unpatchable.
TRACK_OLDS = [track_text("morph_affordance_track_before.txt"),
              track_text("morph_affordance_track_circle.txt")]
TRACK_NEW = track_text("morph_affordance_track_after.txt")

EDITS = [
    ('morph slider receives both slots, not their conjunction',
     [CALLSITE_OLD], CALLSITE_NEW),
    ('hasBoth is derived, so the enablement rule is unchanged',
     [SIGNATURE_OLD], SIGNATURE_NEW),
    ('the wrapper gives up the dim', [WRAPPER_OLD], WRAPPER_NEW),
    ('the track dims its own paint, names the slot it waits for, and '
     'draws a pill thumb that stays inside it',
     TRACK_OLDS, TRACK_NEW),
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
)
REQUIRED_AFTER = (
    '"data-spectr-morph-hint": true',
    'hasA ? "SET B" : (hasB ? "SET A" : "SET A + B")',
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
