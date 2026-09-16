#!/usr/bin/env python3
"""Stop the band readout printing an index from one band layout against
another layout's divisor.

A user screenshot read

    155.5kHz   24.0 dB   BAND 42/32

on a bank whose bands had been hovered as one of 64. Both halves are the same
defect. The hovered index 41 was measured under a 64-band layout; `N` -- the
divisor in the label AND the divisor in the centre-frequency formula -- had
already become 32. `bandCenterFreq` carries no clamp, so the same wrong divisor
extrapolated the index past the top of a 20Hz-20kHz view:

    N=32, i=41  ->  155473 Hz      (what the user saw)
    N=64, i=41  ->    1763 Hz      (the band actually under the pointer)

The divergence is not a typo, it is two stores for one fact. `N` is
`settings.bandCount`, React state that a host automation lane, a preset recall
or a live-state resync rewrites with NO pointer event in between (band count is
host parameter 3003). `hover` / `hoverRef` keep the index they last measured
across that rewrite, and the status effect is declared `[hoverBand, N, onStatus]`
-- so a band-count change deliberately RE-RUNS it, carrying the stale index into
the new divisor. `findBand` clamps to `N - 1` and so cannot produce 41 under
N=32; the index outliving its layout is the only path that can, which is why
clamping the index here would be the wrong fix: it would name a band the
pointer was never over.

So a hover reading now carries the `n` it was taken under, and every consumer
refuses one that does not match the current `N`. Refusing shows nothing (the
effect already publishes "" for a negative index) until the pointer moves,
which is honest; a clamped index would have been a confident wrong answer. The
match is on `n` and not merely on range because band 11 of 64 and band 11 of 32
are different frequencies -- an in-range stale index is wrong too, just less
visibly.

`bandCenterFreq` and `bandFreqRange` additionally clamp their index into
[0, N-1] so the arithmetic itself cannot return a frequency outside the view
under any caller, present or future. That is belt and braces on top of the
refusal above, not the fix: an unclamped extrapolation resurfaces wherever a
new caller forgets.

Why a script and not a hand edit: the materialized generator is broken on main
(exits 1, writes 0 patches) and `tools/patch_materialized_editor.py` aborts on
a clean checkout at a stale needle, so the artifact cannot be regenerated. This
edits the `html` payload by exact-text substitution, asserts every patch point
is unique before writing, and re-checks the result, so the change is replayable
after a merge conflict rather than being an opaque one-line diff.

`resources/editor.html` is deliberately NOT mirrored: it is the browser
bootstrap, not the shipping surface -- the native editor loads the runtime
document -- and `test_import_fidelity.cpp` pins its pre-patch shape on purpose.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

GUARD = (
    "  // A hover reading is only meaningful for the band layout it was\n"
    "  // measured under. `N` is `settings.bandCount`, a separate store from\n"
    "  // the native `n_visible` projection, and band count is host parameter\n"
    "  // 3003 -- so automation, a preset recall or a live-state resync can\n"
    "  // change it with no pointer event, leaving a stale index behind while\n"
    "  // every consumer below reads the NEW `N`. Refuse the mismatch rather\n"
    "  // than clamp it: a clamped index names a band the pointer was never\n"
    "  // over, and the matching `n` (not merely a range check) is what makes\n"
    "  // band 11 of 64 stop reporting itself as band 11 of 32.\n"
    "  const hoverBandOf = (h) => h && !h.mini && h.n === N"
    " && h.band >= 0 && h.band < N ? h.band : -1;\n"
)

EDITS = [
    ('every hover reading carries the N it was measured under',
     '  const hoverBand = hover && !hover.mini ? hover.band : -1;\n'
     '  const updatePointerHover = (next) => {\n'
     '    hoverRef.current = next;\n'
     '    if (!pointerRef.current || !pointerRef.current.mode) setHover(next);\n',
     GUARD +
     '  const hoverBand = hoverBandOf(hover);\n'
     '  const updatePointerHover = (next) => {\n'
     '    const stamped = next ? { ...next, n: N } : next;\n'
     '    hoverRef.current = stamped;\n'
     '    if (!pointerRef.current || !pointerRef.current.mode) setHover(stamped);\n'),

    ('the live label refuses a reading from another layout',
     '  const liveHoverLabel = (current) => {\n'
     '    if (!current || current.mini) return "";\n'
     '    const band = current.band;\n',
     '  const liveHoverLabel = (current) => {\n'
     '    const band = hoverBandOf(current);\n'
     '    if (band < 0) return "";\n'),

    ('the per-frame status write refuses one too',
     '    const pointer = pointerRef.current;\n'
     '    if (!current || current.mini) return;\n',
     '    const pointer = pointerRef.current;\n'
     '    if (hoverBandOf(current) < 0) return;\n'),

    ('the mute-brush press stamps its reading',
     '\n      hoverRef.current = { band, x, y };\n',
     '\n      hoverRef.current = { band, x, y, n: N };\n'),

    ('the gain press stamps its reading',
     '\n    hoverRef.current = { band, x, y };\n',
     '\n    hoverRef.current = { band, x, y, n: N };\n'),

    ('a centre frequency can never leave the view',
     '  const bandCenterFreq = (i) => {\n'
     '    const a = view.lmin + (i + 0.5) / N * (view.lmax - view.lmin);\n',
     '  const bandCenterFreq = (i) => {\n'
     '    // Belt and braces on the refusal above: an index outside this bank\n'
     '    // has no centre frequency, so the arithmetic refuses to invent one\n'
     '    // past the edges of the view rather than extrapolating 155kHz out\n'
     '    // of a 20Hz-20kHz range for whichever caller forgets next.\n'
     '    const a = view.lmin + (clamp(i, 0, N - 1) + 0.5) / N'
     ' * (view.lmax - view.lmin);\n'),

    ('nor a band edge frequency',
     '  const bandFreqRange = (i) => {\n'
     '    const a = view.lmin + i / N * (view.lmax - view.lmin);\n'
     '    const b = view.lmin + (i + 1) / N * (view.lmax - view.lmin);\n',
     '  const bandFreqRange = (i) => {\n'
     '    const j = clamp(i, 0, N - 1);\n'
     '    const a = view.lmin + j / N * (view.lmax - view.lmin);\n'
     '    const b = view.lmin + (j + 1) / N * (view.lmax - view.lmin);\n'),
]

# The pre-fix shapes, by their load-bearing fragments. None may survive.
FORBIDDEN_AFTER = (
    'const hoverBand = hover && !hover.mini ? hover.band : -1;',
    'const a = view.lmin + (i + 0.5) / N * (view.lmax - view.lmin);',
    'hoverRef.current = { band, x, y };',
    # The ref and the React copy must be the SAME stamped object. Writing the
    # stamp to the ref and handing the bare `next` to setHover leaves the state
    # copy with no `n`, so hoverBandOf rejects every reading and the readout
    # disappears entirely -- a silent, total regression this pin exists to stop.
    'if (!pointerRef.current || !pointerRef.current.mode) setHover(next);',
)

REQUIRED_AFTER = (
    'const hoverBandOf = (h) => h && !h.mini && h.n === N'
    ' && h.band >= 0 && h.band < N ? h.band : -1;',
    'const hoverBand = hoverBandOf(hover);',
    'const band = hoverBandOf(current);',
    'if (hoverBandOf(current) < 0) return;',
    'const stamped = next ? { ...next, n: N } : next;',
    'hoverRef.current = stamped;',
    'if (!pointerRef.current || !pointerRef.current.mode) setHover(stamped);',
    'const a = view.lmin + (clamp(i, 0, N - 1) + 0.5) / N * (view.lmax - view.lmin);',
)

# The label and the centre frequency must keep reading the SAME `N` they always
# did -- this change is about which INDEX reaches them, not about introducing a
# second divisor. Pinning both sites asserts that.
REQUIRED_COUNTS = {
    'BAND ${hoverBand + 1}/${N}': 1,
    '"   BAND " + (band + 1) + "/" + N': 1,
}


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for edit in EDITS:
        label, old, new = edit[:3]
        if old and new and old in new and 'hoverRef.current = { band, x, y }' not in old:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for edit in EDITS:
        label, old, new = edit[:3]
        expected = edit[3] if len(edit) == 4 else 1
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(old_e) == 0 and raw.count(new_e) >= expected:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != expected:
            sys.exit('FAIL %s: patch point occurs %d times, expected %d'
                     % (label, count, expected))
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
    for token, want in REQUIRED_COUNTS.items():
        got = raw.count(escaped(token))
        if got != want:
            sys.exit('FAIL: %r appears %d times after patching, expected %d'
                     % (token, got, want))

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
