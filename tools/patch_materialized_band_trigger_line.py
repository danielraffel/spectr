#!/usr/bin/env python3
"""Centre the bands trigger caption inside the trigger's border, on the header's line.

In the top bar, the `32 bands ▾` caption painted one point below the captions
of the BARS / RESPONSE / BOTH control beside it. The trigger's box sits on the
same line as the segmented control (both span 10.5..32.5); its caption did
not. It painted the same way at any band count, because the caption's line
box is installed by the runtime rather than captured.

`applySpectrToolbarOpticalCentering` in the vendored runtime.js gives the
caption an explicit box through `centerBandText(owner, label, width,
textWidth, height, lineTop)` and centres a 13px line in it. That box is placed
at the caption's own origin, which is inside the trigger's 1px border, so
native layout puts it at trigger top + 1. It was sized to the trigger's
BORDER box, 22px, and centred there with a line top of 4.5. Measured on the
shipping editor, the caption's label sat at y=11.5 with height 22, one pixel
past the trigger's bottom edge, and its line landed at 16..29 against 15..28
for BOTH. Skia raster glyph ink rows at 1320x860 (2x):

    BOTH, RESPONSE   17.5 .. 25.0
    32 bands ▾       18.5 .. 26.0    22px box, line top 4.5
    32 bands ▾       17.5 .. 25.0    20px box, line top 3.5 (this patch)

The box the caption can occupy is the trigger's CONTENT box: 22px less the
top and bottom border, 20px. Centring the 13px line there gives a line top of
(20 - 13) / 2 = 3.5, which puts the line at trigger top + 1 + 3.5 = 15, the
line the segmented captions use. This is not an offset tuned to a raster; it
is the same centring the call already performs, applied to the box the text
actually lives in.

History, because 20 / 3.5 is not new. The caption box has always been
sized to equal the trigger's own height and was moved in lockstep with it:
20 / 3.5 while the trigger was 20px, 24 / 5.5 when 04d9ac3 gave it the full
24px rail, and 22 / 4.5 when d9d9021 (squashed into a36797a, "band selector
painted rail alignment") settled the trigger's PAINTED rail at 22px with a
-1.5px lift to match the 22px segmented buttons. Those commits were about
the trigger's border box, and this script leaves every part of it alone:
width 92, height 22 and the -1.5 transform are untouched, so the rail
alignment a36797a established -- and the test that checks the trigger's top
and bottom against BOTH's -- still hold. Only the caption box changes, from
"the trigger's height" to "the trigger's height inside its border". The
lockstep rule was already off by the border at 20px, too: a 20px trigger's
content box is 18px.

runtime.js is a checked-in artifact and `tools/patch_materialized_editor.py`,
the mirror that would normally carry an edit like this, does not run on this
checkout, so this script edits runtime.js by exact-text substitution. The
mirror's own caption edit now produces the same text, so running it later
keeps this result rather than restoring 22 / 4.5. This script
asserts the patch point occurs exactly once, re-checks the result, and reports
"already applied" on a second run.

Exit codes: 0 applied or already applied, 1 the patch point is missing or
ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

OLD = ('        nativeTextOwner(bandTrigger), liveBandCount + " bands \\u25BE",\n'
       '        92, 73.03125, 22, 4.5),\n')
# The same text tools/patch_materialized_editor.py now produces, so the two
# recipes agree on the result.
NEW = ('        nativeTextOwner(bandTrigger), liveBandCount + " bands \\u25BE",\n'
       '        // The caption sits inside the trigger\'s 1px border, so it centres\n'
       '        // in the 20px inner area, not the 22px border box.\n'
       '        92, 73.03125, 20, 3.5),\n')

CONTEXT = 'g5.__spectrBandCountCenteringReceipt__ = bandCountReceipt;'


def main():
    source = open(PATH, encoding="utf-8").read()

    context_count = source.count(CONTEXT)
    if context_count != 1:
        sys.exit("FAIL: %r occurs %d times, expected 1; the band centring "
                 "pass is not where this script expects it"
                 % (CONTEXT, context_count))

    if source.count(NEW) == 1 and source.count(OLD) == 0:
        print("already applied  the bands trigger caption centres in its "
              "content box")
        return 0

    count = source.count(OLD)
    if count != 1:
        sys.exit("FAIL: the bands trigger centring call occurs %d times, "
                 "expected 1" % count)

    source = source.replace(OLD, NEW, 1)
    if source.count(NEW) != 1 or source.count(OLD) != 0:
        sys.exit("FAIL: the bands trigger centring call was not rewritten")

    open(PATH, "w", encoding="utf-8").write(source)
    print("applied          the bands trigger caption centres in its content box")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
