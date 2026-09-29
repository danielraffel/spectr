#!/usr/bin/env python3
"""Close the gap between the Output trim's track and the number it sets.

The trim readout was a 34pt box with its text RIGHT-aligned, so a short value
such as "0.0" (~18pt of ink) sat ~16pt inside the far end of its box: the
visible gap from the track's right end to the first digit was ~30pt, twice
the cluster's 14pt rhythm, and the number read as belonging to PEAK beside it
rather than to the slider.

The box is now LEFT-aligned, so the first glyph starts where the box starts,
14pt (the cluster's flex gap) after the track. It stays a FIXED width, sized
for the widest value the trim can print -- "-24.0" / "+24.0", five 10pt
JetBrains Mono glyphs at ~6.03pt each, 30.2pt -- at 31pt, so a changing
value never resizes the box and PEAK, 14pt after the box, never moves.

Why a script: the shipping document is one minified line and the materialized
generator cannot rebuild it. Each edit asserts its patch point occurs exactly
once; a second run reports "already applied" and writes nothing.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

EDITS = [
    ("the readout is a left-aligned box sized for its widest value",
     '"data-spectr-output-trim-readout": true,\n'
     '      className: "tnum",\n'
     '      style: { width: 34, textAlign: "right", ',
     '"data-spectr-output-trim-readout": true,\n'
     '      className: "tnum",\n'
     '      // Left-aligned so the number starts 14pt after the track, in a\n'
     '      // box fixed at the widest value ("-24.0", ~30.2pt) so PEAK never\n'
     '      // moves as the digits change.\n'
     '      style: { width: 31, textAlign: "left", '),
    ("the cluster's geometry comment states the readout's width",
     "  // ~41 OUTPUT + 14 + 156 track + 14 + 34 readout + 14 + 96 peak, ending at\n"
     "  // ~741 -- ~98pt clear of the divider, and out of the flow that cannot\n",
     "  // ~41 OUTPUT + 14 + 156 track + 14 + 31 readout + 14 + 96 peak, ending at\n"
     "  // ~738 -- ~101pt clear of the divider, and out of the flow that cannot\n"),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()
    applied = already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1 and raw.count(old_e) == 0:
            print("already applied ", label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        applied += 1
        print("applied         ", label)
    if already and applied:
        sys.exit("FAIL: the document is half patched; refusing to write")
    if not applied:
        print("no change needed")
        return 0
    if not isinstance(json.loads(raw).get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
