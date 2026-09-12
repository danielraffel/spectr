#!/usr/bin/env python3
"""Make the two graph axis headings column headers over their own rulers.

Three defects, all of them in `drawRulers` / `drawBands` in the shipping
materialized runtime document, all reported by the user against a real build:

  1. `dB (gain)` was painted LEFT aligned at `inner.x` -- the plot's left edge
     -- while the gain ticks it names are RIGHT aligned at `inner.x - 8`. So
     the heading sat inside the plot, horizontally divorced from its own
     column, overlapping the frequency ruler.
  2. `dBFS (analyzer)` was painted RIGHT aligned at `inner.x + inner.w` -- the
     plot's right edge -- while the analyzer ticks it names are LEFT aligned at
     `inner.x + inner.w + 8`. Same defect, mirrored.
  3. `SPECTRAL \xb7 mask` was painted inside the plot at `inner.x + 8,
     inner.y + 6`: floating text over the graph naming a mode the DSP-mode
     control in the toolbar already names.

The fix is deliberately expressed as the SAME expression the ruler uses, not as
a nudged constant. Each heading now takes the tick column's own anchor and the
tick column's own alignment:

    gain ticks      textAlign "right"  at  inner.x - 8
    dB heading      textAlign "right"  at  inner.x - 8            <- identical
    analyzer ticks  textAlign "left"   at  inner.x + inner.w + 8
    dBFS heading    textAlign "left"   at  inner.x + inner.w + 8  <- identical

Identical anchor plus identical alignment is what makes the edges coincide, and
it keeps coinciding at every window size and band count because neither side
carries a literal of its own. A hard-coded offset would have drifted the moment
`inner` changed, which is every resize.

The vertical position is untouched: both headings stay on `g.inner.y - 8`, the
row they already occupied, which is above the `+24` tick (the gain axis paints
`+24` at `zeroY - halfH`, i.e. the top of the plot).

`resources/editor.html` is deliberately NOT mirrored. It is the browser
bootstrap, not the shipping surface -- the native editor loads the runtime
document -- and `test_import_fidelity.cpp`'s needles pin its pre-patch shape on
purpose. The durable record of this change is this file.

Why a script and not a hand edit: the materialized generator is broken on main
(exits 1, writes 0 patches) and `tools/patch_materialized_editor.py` aborts on
a clean checkout at a stale needle, so the artifact cannot be regenerated. This
edits the `html` payload by exact-text substitution, asserts every patch point
is unique before writing, and re-checks the result, so the change is replayable
after a merge conflict rather than being an opaque artifact diff.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

EDITS = [
    # The gain ruler already set textAlign "right" for its tick loop, so the
    # heading only has to stop overriding it -- but say it explicitly rather
    # than inheriting, so a future reorder of the two blocks cannot silently
    # re-point the heading.
    ('dB heading joins the gain tick column',
     '    ctx.fillStyle = "rgba(255,255,255,0.30)";\n'
     '    ctx.textAlign = "left";\n'
     '    ctx.fillText("dB (gain)", inner.x, g.inner.y - 8);\n',
     '    ctx.fillStyle = "rgba(255,255,255,0.30)";\n'
     '    ctx.textAlign = "right";\n'
     '    ctx.fillText("dB", inner.x - 8, g.inner.y - 8);\n'),

    ('dBFS heading joins the analyzer tick column',
     '    ctx.fillStyle = "rgba(130,220,180,0.38)";\n'
     '    ctx.textAlign = "right";\n'
     '    ctx.fillText("dBFS (analyzer)", inner.x + inner.w, g.inner.y - 8);\n',
     '    ctx.fillStyle = "rgba(130,220,180,0.38)";\n'
     '    ctx.textAlign = "left";\n'
     '    ctx.fillText("dBFS", inner.x + inner.w + 8, g.inner.y - 8);\n'),

    # The whole block goes: the font / align / baseline / fillStyle lines
    # existed only to paint this one string, and `ctx.restore()` follows
    # immediately, so nothing downstream inherits the state they set.
    ('no floating mode label over the plot',
     '    ctx.font = "9px JetBrains Mono, monospace";\n'
     '    ctx.textAlign = "left";\n'
     '    ctx.textBaseline = "top";\n'
     '    ctx.fillStyle = "rgba(200,220,250,0.55)";\n'
     '    const dspLabel = "SPECTRAL \\xB7 mask";\n'
     '    ctx.fillText(dspLabel, inner.x + 8, inner.y + 6);\n',
     ''),
]

FORBIDDEN_AFTER = ('dB (gain)', 'dBFS (analyzer)', 'SPECTRAL', 'dspLabel')

# The headings must carry the tick columns' own anchor expressions verbatim.
# Pinning the TICK lines too is the point: if a future edit moves a ruler's
# inset, this asserts the heading moved with it instead of silently detaching.
REQUIRED_AFTER = (
    'ctx.fillText("dB", inner.x - 8, g.inner.y - 8);',
    'ctx.fillText((db > 0 ? "+" : "") + db, inner.x - 8, y);',
    'ctx.fillText("dBFS", inner.x + inner.w + 8, g.inner.y - 8);',
    'inner.x + inner.w + 8,\n        y\n      );',
)


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for edit in EDITS:
        label, old, new = edit[:3]
        if old and new and old in new:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for edit in EDITS:
        label, old, new = edit[:3]
        expected = edit[3] if len(edit) == 4 else 1
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(old_e) == 0 and (new_e == '' or raw.count(new_e) >= expected):
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
