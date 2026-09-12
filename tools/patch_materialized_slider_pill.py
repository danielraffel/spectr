#!/usr/bin/env python3
"""Give the four settings sliders the same pill thumb the morph slider has.

Rate, Depth, LFO 2 rate and LFO 2 depth all render through one component,
`SpectrSettingsSlider`, so one patch point covers every settings slider in the
product.

WHY A PILL.  The design source drew every slider in Spectr -- the settings
sliders (`SSlider`) and the home morph slider (`MorphSlider`) alike -- as a
native `<input type="range">` tinted with `accentColor: hsl(200,80%,60%)`.  The
native editor cannot host a range input, so both were reimplemented as custom
`div[role=slider]` nodes with a hand-drawn thumb, and that reimplementation
picked a circle.  Nothing chose the circle as a design; it was incidental.  The
pill restores the intent and is a better grab target in the axis the control is
actually dragged along.

WHAT THE TRAVEL FIX IS FOR, which is correctness rather than taste.  The circle
was positioned `left: ratio%` with a FIXED `marginLeft` of half its width, so
it hung half outside the track at both ends: 7px past the left edge at the
minimum and 7px past the right edge at the maximum.  The pill insets its own
travel -- `marginLeft: -(width * ratio)` -- so its left edge runs
`0 -> trackWidth - width` and the painted thumb is inside the painted track at
every value.  That is also how a real range input behaves.  Vertically the same:
14px at top 1 and 16px at top 0 inside a 16px track, where the circle grew to
18px at top -1 and overhung a pixel top and bottom on hover.

HOVER GROWTH GROWS BOTH AXES, deliberately.  `tools/spectr-detectors/
slider_thumb_hover_growth.py` asserts the thumb is strictly larger in width AND
height under hover, so a pill that grew only along its long axis would satisfy
a reader's idea of "it got bigger" while leaving that detector asserting half
of what it says.  22x14 -> 26x16 grows in both, keeps the shape unmistakably a
pill in both states, and still fits the 16px track.

SINGLE WRITER.  This script owns the settings thumb style; nothing else may
write it.  The morph thumb is owned by
`tools/patch_materialized_morph_affordance.py`, because that thumb sits inside
the contiguous track block that script replaces wholesale and a second writer
reaching into the block would break its replay.  Two patch points, one writer
each.

`tools/patch_materialized_editor.py` carries a much older spelling of this
component (a 4px-tall track and a `top: -5` thumb) among its needles.  That
script already aborts on this checkout without writing, and the spelling it
looks for has not existed in the shipping document for some time, so it is not
a live writer of this patch point.

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

RATIO = '((value - min) / ((max - min) || 1))'

THUMB_OLD = (
    'React.createElement("div", { "data-spectr-setting-slider-thumb": true, '
    '"data-spectr-setting-slider-thumb-state": hovered ? "hover" : "idle", '
    'style: { position: "absolute", top: hovered ? -1 : 1, '
    'width: hovered ? 18 : 14, height: hovered ? 18 : 14, '
    'borderRadius: hovered ? 9 : 7, pointerEvents: "none", background: "#fff", '
    'border: "1px solid rgba(0,0,0,0.35)", marginLeft: hovered ? -9 : -7, '
    'left: (100 * (value - min) / ((max - min) || 1)) + "%" } })')
THUMB_NEW = (
    'React.createElement("div", { "data-spectr-setting-slider-thumb": true, '
    '"data-spectr-setting-slider-thumb-state": hovered ? "hover" : "idle", '
    'style: { position: "absolute", top: hovered ? 0 : 1, '
    'width: hovered ? 26 : 22, height: hovered ? 16 : 14, '
    'borderRadius: hovered ? 8 : 7, pointerEvents: "none", background: "#fff", '
    'border: "1px solid rgba(0,0,0,0.35)", '
    'marginLeft: -((hovered ? 26 : 22) * ' + RATIO + '), '
    'left: (100 * (value - min) / ((max - min) || 1)) + "%" } })')

EDITS = [
    ('the settings thumb is a pill that stays inside its track',
     [THUMB_OLD], THUMB_NEW),
]

# The circle spelling, and the fixed half-width overhang that came with it.
FORBIDDEN_AFTER = (
    '"data-spectr-setting-slider-thumb": true, '
    '"data-spectr-setting-slider-thumb-state": hovered ? "hover" : "idle", '
    'style: { position: "absolute", top: hovered ? -1 : 1, '
    'width: hovered ? 18 : 14',
)
# Everything the surrounding component and its tests still resolve by.
REQUIRED_AFTER = (
    '"data-spectr-setting-slider": true',
    '"data-spectr-setting-slider-thumb": true',
    '"data-spectr-setting-slider-thumb-state": hovered ? "hover" : "idle"',
    'width: hovered ? 26 : 22, height: hovered ? 16 : 14',
    'marginLeft: -((hovered ? 26 : 22) * ' + RATIO + ')',
    'style: { position: "relative", flex: 1, height: 16, cursor: "pointer" }',
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
        if raw.count(escaped(new)) >= 1:
            print('already applied ', label)
            continue
        hits = [old for old in olds if raw.count(escaped(old)) == 1]
        if len(hits) != 1:
            counts = ', '.join(str(raw.count(escaped(old))) for old in olds)
            sys.exit('FAIL %s: recognised patch points occur [%s] times, '
                     'expected exactly one of them once' % (label, counts))
        raw = raw.replace(escaped(hits[0]), escaped(new))
        changed = True
        print('applied         ', label)

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
