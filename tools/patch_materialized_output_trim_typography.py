#!/usr/bin/env python3
"""Give the Output-trim readout the type its own cluster already uses.

The readout beside the Output trim declared `width`, `textAlign`,
`whiteSpace` and `flexShrink` -- and no `fontFamily`, `fontSize` or `color`.
Its sibling PEAK button, 14pt away in the same component, declares all three
(`var(--mono)`, 10, `rgba(255,255,255,0.72)`). So the number inherited the
document body default and rendered in a different face, larger and brighter
than every other readout in the header.

Measured from a Skia raster of the shipping native editor at the authored
1320x860 box (`Spectr-native-shot --backend=skia`), glyph ink height:

    PEAK -2.5    7.5pt        LIVE / BARS / BOTH   7.5pt
    1.00x zoom   7.5pt        32 bands             7.5pt
    SPECTR       9.0pt        Output trim readout  11.0pt

-- 47% taller than every other readout in the row and taller than the brand
wordmark, at relative luminance 0.676 against 0.188 for the `1.00x zoom`
readout it sits parallel to. The largest and brightest non-brand element in
the header was the one control with no on-screen label.

It also overflowed. The box is 34pt wide; at the trim extremes the inherited
face painted `+12.0` as a 37pt run inside it -- the exact "a glyph run must
fit the box laid out for it" defect `tools/appearance_invariants.py` exists
to reject, which that suite cannot see here because it adjudicates a
checked-in Settings dump and never sees the header. At `var(--mono)` 10 the
widest value the control can print (`-24.0`, 5 glyphs) measures ~30pt and
fits with slack.

This is a type declaration, not a restyle: it makes the readout match the
treatment its own component already chose for the other half of the cluster.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts the patch point occurs exactly once before
writing, re-checks the result, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 the patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# The sibling button's declarations, so the two halves of the cluster cannot
# drift apart again without this file changing too.
OLD = ('style: { width: 34, textAlign: "right", whiteSpace: "nowrap", '
       'flexShrink: 0 }')
NEW = ('style: { width: 34, textAlign: "right", whiteSpace: "nowrap", '
       'flexShrink: 0, fontFamily: "var(--mono)", fontSize: 10, '
       'color: "rgba(255,255,255,0.72)" }')

# Present only on the node being patched, so a future style object that
# happens to share OLD's text cannot be hit by mistake.
CONTEXT = '"data-spectr-output-trim-readout": true'

REQUIRED_AFTER = (
    CONTEXT,
    'fontFamily: "var(--mono)", fontSize: 10, color: "rgba(255,255,255,0.72)" }',
)

REQUIRED_COUNTS = {CONTEXT: 1}


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()

    context_count = raw.count(escaped(CONTEXT))
    if context_count != 1:
        sys.exit("FAIL: %r occurs %d times, expected 1; the Output trim "
                 "readout is not where this script expects it"
                 % (CONTEXT, context_count))

    if raw.count(escaped(NEW)) == 1:
        print("already applied  the Output trim readout carries its own type")
        return 0

    count = raw.count(escaped(OLD))
    if count != 1:
        sys.exit("FAIL: the readout style occurs %d times, expected 1" % count)

    raw = raw.replace(escaped(OLD), escaped(NEW), 1)

    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit("FAIL: %r is absent after patching" % (token,))
    for token, want in REQUIRED_COUNTS.items():
        got = raw.count(escaped(token))
        if got != want:
            sys.exit("FAIL: %r appears %d times after patching, expected %d"
                     % (token, got, want))

    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")

    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied          the Output trim readout carries its own type")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
