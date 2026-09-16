#!/usr/bin/env python3
"""Label the Output trim, and give it a track a person can aim with.

Two defects in the same header cluster, both about reaching the control
rather than about what it computes.

1. NO ON-SCREEN LABEL. Every other control in the header is labelled --
   LIVE, PRECISION, BARS, RESPONSE, BOTH, the band chip, the zoom readout.
   The trim carried only `aria-label` and `title`, neither of which is
   visible in a DAW, so a user met an unlabelled slider beside a meter and
   had to guess what it moved. The word is OUTPUT because that is the name
   of the host parameter it writes (`kOutputTrim`, param 2, exposed to the
   host as "Output"): somebody who automates it in their DAW sees the same
   word in both places.

2. NO FINE ADJUSTMENT. The track was 58pt under the runtime's fixed ~20pt
   thumb -- a 34% thumb -- and the runtime maps a press to a value across
   the FULL declared track width, so the control resolved

       (24 - -24) / 58 = 0.83 dB per point

   Measured through the shipping standalone, a 25pt drag from the track
   centre moved the trim 20.5 dB. At 2x that is one 0.5 dB step roughly
   every device pixel, which is not adjustment, it is a coin toss.

   Spectr's own Settings sliders already solve this with the same fixed
   thumb: `SSlider` lays out a 200pt row as 156pt track + 8pt gap + 36pt
   readout. So this takes that track width rather than inventing one:

       (24 - -24) / 156 = 0.31 dB per point

   -- 2.7x finer, and 1.6pt of travel per 0.5 dB step.

GEOMETRY, and why 156 is the largest round number that fits. The cluster is
absolutely positioned in the header's empty flex gap, measured at
x=281.7..669.5 from a SPECTR_LAYOUT_DUMP of the authored 1320x860 box, with
the LIVE/PRECISION group's painted left edge at x=687.5. Laid out at gap 14:

    282 +  96 peak + 14 + ~41 OUTPUT + 14 + 156 track + 14 + 34 readout
        = ends at ~651, i.e. inside the measured gap with ~18pt of spacer to
          spare and ~36pt of clear header before LIVE -- twice the header's
          own 18pt inter-group gap.

A longer track would eat that clearance; 200pt would end at 695 and run
under the LIVE button.

WHERE THE LABEL GOES. It is inserted as the SECOND child of the cluster,
between the PEAK button and the track, because that is what it labels.
That renumbers the two children after it, which in this document is
normally the dangerous move: `text_bindings`, `layout_bindings` and
`paint_bindings` address their nodes by positional DOM path, and inserting
a child anywhere but last silently re-points every later sibling -- that is
how "BARS" once lost its captured 48pt basis and remeasured at 20pt. It is
safe HERE and only here: no binding path enters this cluster at all. Every
captured header path stops at `div[0]/div[1]/div[0..6]`, and the cluster is
appended after all of them as a child nothing captured. Checked, not
assumed: `native buttons are tappable across their whole painted bounds`
and a before/after layout dump both agree that no pre-existing header node
moved.

Why a script and not a hand edit: the shipping document is one minified
line, so two hand edits to it always conflict and neither is replayable.
This substitutes exact text, asserts every patch point occurs exactly once
before writing, re-checks the result, and reports "already applied" on a
second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# The PEAK button's own declarations, so the three parts of the cluster
# cannot drift apart. `letterSpacing: 0.8` is the button's too -- the header's
# other word labels all carry it.
LABEL_ELEMENT = (
    '    /* @__PURE__ */ React.createElement("span", {\n'
    '      "data-spectr-output-trim-label": true,\n'
    '      // Declares its own face, size, tracking and colour. A control that\n'
    '      // declares no type inherits the document body default, which is\n'
    '      // how the readout beside it shipped 47% taller than every other\n'
    '      // readout in this header.\n'
    '      style: { fontFamily: "var(--mono)", fontSize: 10, '
    'letterSpacing: 0.8, color: "rgba(255,255,255,0.72)", lineHeight: 1, '
    'whiteSpace: "nowrap", flexShrink: 0 }\n'
    '    }, "OUTPUT"),\n'
)

TRACK_ANCHOR = '(over ? "OVER " : "PEAK ") + peakText)),\n'

OLD_TRACK_STYLE = 'style: { width: 58, flexShrink: 0, accentColor: "hsl(200,80%,60%)" }'
NEW_TRACK_STYLE = (
    'style: { width: 156, flexShrink: 0, accentColor: "hsl(200,80%,60%)" }'
)

OLD_GEOMETRY = (
    "  // The header's own flex spacer is empty from x=281.7 to x=669.5 (measured\n"
    "  // from a SPECTR_LAYOUT_DUMP of the authored 1320x860 box), so this sits at\n"
    "  // 282 and is 200pt wide, ending at 482 -- inside that gap with ~187pt to\n"
    "  // spare, and out of the flow that cannot absorb it.\n"
)
NEW_GEOMETRY = (
    "  // The header's own flex spacer is empty from x=281.7 to x=669.5 (measured\n"
    "  // from a SPECTR_LAYOUT_DUMP of the authored 1320x860 box), so this sits at\n"
    "  // 282 and runs 96 peak + 14 + ~41 OUTPUT + 14 + 156 track + 14 + 34\n"
    "  // readout, ending at ~651 -- inside that gap, with ~36pt of clear header\n"
    "  // before the LIVE button at x=687.5, and out of the flow that cannot\n"
    "  // absorb it. A longer track would eat that clearance.\n"
)

EDITS = [
    ("the trim carries a visible OUTPUT label",
     TRACK_ANCHOR,
     TRACK_ANCHOR + LABEL_ELEMENT),

    ("the track is long enough to aim with",
     OLD_TRACK_STYLE,
     NEW_TRACK_STYLE),

    ("the cluster's own comment states the geometry it now has",
     OLD_GEOMETRY,
     NEW_GEOMETRY),
]

REQUIRED_AFTER = (
    '"data-spectr-output-trim-label": true,',
    '}, "OUTPUT"),',
    NEW_TRACK_STYLE,
    # The label must sit BEFORE the track, or it labels the readout.
    '}, "OUTPUT"),\n    /* @__PURE__ */ React.createElement("input", {\n'
    '      "data-spectr-output-trim": true,',
)

FORBIDDEN_AFTER = (OLD_TRACK_STYLE,)

REQUIRED_COUNTS = {
    '"data-spectr-output-trim-label": true,': 1,
    '"data-spectr-output-trim": true,': 1,
    '"data-spectr-output-trim-readout": true,': 1,
    NEW_TRACK_STYLE: 1,
}


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()

    # The cluster this edits must exist exactly once before anything is
    # substituted. Without this an empty or restructured document would make
    # every "already applied" below vacuously true.
    cluster = '"data-spectr-output-cluster": true'
    seen = raw.count(escaped(cluster))
    if seen != 1:
        sys.exit("FAIL: %r occurs %d times, expected 1; the output cluster is "
                 "not where this script expects it" % (cluster, seen))

    changed = False
    applied = 0
    already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1 and raw.count(old_e) <= raw.count(new_e):
            print("already applied ", label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        applied += 1
        print("applied         ", label)

    if already and applied:
        sys.exit("FAIL: the document is half patched; refusing to write")

    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit("FAIL: %r is absent after patching" % (token,))
    for token in FORBIDDEN_AFTER:
        if raw.count(escaped(token)):
            sys.exit("FAIL: %r survives after patching; the 58pt track is the "
                     "defect this removes" % (token,))
    for token, want in REQUIRED_COUNTS.items():
        got = raw.count(escaped(token))
        if got != want:
            sys.exit("FAIL: %r appears %d times after patching, expected %d"
                     % (token, got, want))

    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")

    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
