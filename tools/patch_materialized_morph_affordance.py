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

Deliberately NOT changed, because each is load-bearing for an existing test:
`id="spectr-snapshot-morph"`, `data-spectr-morph`, `data-spectr-morph-state`,
the 90x16 track geometry, the 14px/18px thumb sizes and
`data-spectr-morph-thumb-state`, and the pointer protocol --
`test/test_native_state_parity.cpp` resolves the widget by id and asserts that
geometry, and it drives the control in its DEFAULT (no snapshots) state, so
the thumb must keep rendering while disabled.  Enablement SEMANTICS are
untouched: this patch changes what the user is told, never what the control
permits, so the browser-lane tests in `test/test_editor_analyzer_browser.mjs`
that assert `morph.disabled` against `resources/editor.html` keep measuring
what they did.

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
TRACK_OLD = open(os.path.join(REPO, "tools",
                              "morph_affordance_track_before.txt"),
                 encoding="utf-8").read()
TRACK_NEW = open(os.path.join(REPO, "tools",
                              "morph_affordance_track_after.txt"),
                 encoding="utf-8").read()

EDITS = [
    ('morph slider receives both slots, not their conjunction',
     CALLSITE_OLD, CALLSITE_NEW),
    ('hasBoth is derived, so the enablement rule is unchanged',
     SIGNATURE_OLD, SIGNATURE_NEW),
    ('the wrapper gives up the dim', WRAPPER_OLD, WRAPPER_NEW),
    ('the track dims its own paint and names the slot it waits for',
     TRACK_OLD, TRACK_NEW),
]

# No reader may still expect a `hasBoth` prop from outside, and the wrapper
# must not dim the caption it now contains.
FORBIDDEN_AFTER = (
    'marginLeft: 6, opacity: hasBoth ? 1 : 0.35 } }',
    'function MorphSlider({ bankRef, hasBoth })',
    'MorphSlider, { bankRef, hasBoth:',
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
    'width: grown ? 18 : 14',
    '"data-spectr-morph-thumb-state": grown ? "hover" : "idle"',
)


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for label, old, new in EDITS:
        if old and old in new:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(old_e) == 0 and raw.count(new_e) >= 1:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e)
        changed = True
        applied += 1
        print('applied         ', label)

    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')

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
