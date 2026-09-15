#!/usr/bin/env python3
"""A band going silent collapses to the zero line; it does not plunge past the floor.

THE DEFECT, as reported: "when muting i am not sure we should draw that box
under the 0 horizontal line -- its an animation flash and is just too much".
The screenshots show a tall outlined box sweeping from the zero line down to
the bottom of the plot and vanishing.

WHAT IS ACTUALLY DRAWN, measured frame by frame out of the painter's own
command stream (band 5, 24 bands, 1000x600, zeroY 295.5, plot floor 480):

    frame   rg[5]       selection stroke          body fill
    0      -0.191       40.0t  y293.5..333.5      35.8t  y295.5..331.3
    5      -0.925      176.0t  y293.5..469.5     171.2t  y295.5..466.7
    10     -1.009      191.0t  y293.5..484.5     186.7t  y295.5..482.2
    11     -Infinity     5.0t  y293.5..298.5     (gone)

Eleven frames, ~185 ms, reaching 4.5 px PAST the floor of the plot -- then
gone. That is the flash, and both marks are drawn honestly: they are reading a
value that really is sweeping to the bottom of the range.

WHY THE VALUE SWEEPS DOWN, which is the part that is not a design decision.
`renderGainsRef` is ramped toward -1.02 purely so that the NEXT line can
notice it crossed -1.01 and snap it to the `-Infinity` sentinel:

    if (!Number.isFinite(rg[i])) rg[i] = -1.02;
    rg[i] = smooth(rg[i], -1.02, dt * 26);
    if (!Number.isFinite(rg[i]) || rg[i] < -1.01) rg[i] = -Infinity;

-1.02 is a TRIPWIRE for the state machine, not a gain the band is travelling
to. Nothing about a band going silent means "-102 dB-of-full-scale deep"; the
band is going AWAY. But the painter cannot tell a tripwire from a gain, so it
spends eleven frames drawing the trip.

The settled state proves the intent: once `rg` reaches the sentinel,
`effectiveGains` normalises a non-finite value to ZERO, `topY == botY ==
zeroY`, and the muted band's body has no height at all. So the animation's only
job is to get from "audible bar" to "nothing" -- and it was doing it by
plunging to the bottom of the range instead of shrinking to the line.

THE FIX. Ramp toward 0 and trip on arrival instead:

    if (!Number.isFinite(rg[i])) rg[i] = 0;
    rg[i] = smooth(rg[i], 0, dt * 26);
    if (!Number.isFinite(rg[i]) || Math.abs(rg[i]) < 0.004) rg[i] = -Infinity;

Same smoothing rate, same ~12-frame duration, opposite direction: the bar
shrinks into the zero line and is replaced by the mute chip, which is already
drawn from the first frame of the transition because it keys off
`targetGainsRef`. Only the DIRECTION changes, because the direction is the
entire complaint -- the duration was never what was reported.

The 0.004 threshold is sub-pixel by construction: 0.004 x halfH is 0.74 px at
the measured geometry, and 0.004 x 24 dB is 0.096 dB. A band muted while
already flat trips on the first frame and never animates, which is correct --
a flat band has no height to collapse. Under the old ramp that same band drew
the full plunging box out of nothing, which is likely the starkest form of what
was reported.

THIS IS PAINT ONLY, AND THAT WAS CHECKED RATHER THAN ASSUMED. Both
`processing_state_set` publication sites read `targetGainsRef`, never
`renderGainsRef` -- `const current = targetGainsRef.current;` in each -- so no
value that reaches C++ or the DSP passes through this ramp. Muting still mutes
at the same instant; only what is painted on the way changes.

WHAT THIS ALSO FIXES, WITHOUT BEING ABOUT IT. The selection outline was the
`outlined` half of the report, and for the shipped `muteStyle: "cutout"` it was
already fixed by routing a muted band's selection onto its mute chip. Under
`muteStyle: "collapse"` there is no chip, so that selection deliberately still
falls through to the body rect -- which means it still plunged. Collapsing the
value fixes it there too, because the rect it reads no longer has any depth.

WHAT THIS DELIBERATELY DOES NOT DO. `bodyA *= Math.max(0, 1 + effectiveGains[i])`
faded the body as it plunged; with the value collapsing toward 0 that
multiplier stays ~1 and simply stops doing anything. It is left in place: the
shrink to zero height IS the disappearance, and a fade on top of it would be a
second animation for one event. The three `effectiveGains[i] <= -1.01` guards
are likewise left alone -- they could only ever fire in the last frame or two
of the old plunge, and the settled state (which normalises to 0) never reached
them either.

NOTE: the document is compiled into the binary by `pulp_add_binary_data`
(CMakeLists.txt `spectr_native_assets`), so a rebuild is REQUIRED before any
native test reflects this patch. `Encoding binary asset
materialized-document.runtime.json` in the build log is the proof; "Built
target" is not.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def enc(snippet):
    """The document stores the page as a JSON string, so every needle is
    escaped the way the file stores it. Raw-text surgery, never a load/dump
    round trip: this file's escaping is not uniform, so re-serialising would
    rewrite bytes that have nothing to do with this change."""
    return json.dumps(snippet)[1:-1]


RAMP = (
    '          if (!isMuted(rg[i])) {\n'
    '            if (!Number.isFinite(rg[i])) rg[i] = -1.02;\n'
    '            rg[i] = smooth(rg[i], -1.02, dt * 26);\n'
    '            if (!Number.isFinite(rg[i]) || rg[i] < -1.01) rg[i] = -Infinity;\n'
    '          }\n'
)
RAMP_NEW = (
    '          if (!isMuted(rg[i])) {\n'
    '            // A band going silent collapses INTO the zero line. The old\n'
    '            // ramp travelled to -1.02, which is a tripwire for the snap on\n'
    '            // the line below rather than a gain the band is going to, and\n'
    '            // the painter cannot tell those apart: it spent eleven frames\n'
    '            // drawing a box from the zero line to past the bottom of the\n'
    '            // plot. The settled state already paints a muted band at zero\n'
    '            // height, so shrinking to the line is the same destination by\n'
    '            // the honest route. Same rate, opposite direction.\n'
    '            //\n'
    '            // 0.004 is sub-pixel: 0.004 * halfH is under a pixel at any\n'
    '            // plot size this editor uses. A band muted while already flat\n'
    '            // trips on its first frame and never animates, which is right.\n'
    '            //\n'
    '            // Paint only. Both processing_state_set publications read\n'
    '            // targetGainsRef, so nothing here reaches C++ or the DSP.\n'
    '            if (!Number.isFinite(rg[i])) rg[i] = 0;\n'
    '            rg[i] = smooth(rg[i], 0, dt * 26);\n'
    '            if (!Number.isFinite(rg[i]) || Math.abs(rg[i]) < 0.004) rg[i] = -Infinity;\n'
    '          }\n'
)


EDITS = [
    ('a muting band collapses to the zero line instead of plunging past the floor',
     (RAMP, RAMP_NEW),
     'rg[i] = smooth(rg[i], 0, dt * 26);'),
]

# Asserted present after every run. The first three are the new ramp; the rest
# are the things this must NOT have disturbed -- the sentinel it still reaches,
# the normalisation that keeps the settled state at zero height, and the chip
# and selection routing landed alongside it.
REQUIRED_AFTER = (
    "if (!Number.isFinite(rg[i])) rg[i] = 0;",
    "rg[i] = smooth(rg[i], 0, dt * 26);",
    "if (!Number.isFinite(rg[i]) || Math.abs(rg[i]) < 0.004) rg[i] = -Infinity;",
    "const effectiveGains = rg.map((value) => Number.isFinite(value) "
    "? clamp(value, -1.02, 1.02) : 0);",
    "function muteChipRect(i, g) {",
    'if (G.targetMuted && muteStyle === "cutout") {',
    'ctx.fillStyle = "rgba(18,22,28,0.92)";',
    "const current = targetGainsRef.current;",
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. The ramp lives inside the rAF
    # draw loop and is meaningful only beside the sentinel predicate and the
    # painter that reads it; a document without all three is one this script
    # must refuse rather than no-op into "already applied".
    control = (raw.count(enc("const draw = (now) => {"))
               + raw.count(enc("function isMuted(g) {"))
               + raw.count(enc("function drawBands(ctx, g) {")))
    print("control: %d anchors (draw loop + isMuted + drawBands)" % control)
    if control != 3:
        print("FAIL: expected 3 anchors, found %d -- wrong document" % control,
              file=sys.stderr)
        return 1

    applied, already = [], []
    for label, (find, replace), done in EDITS:
        if raw.count(enc(done)) >= 1:
            already.append(label)
            continue
        found = raw.count(enc(find))
        if found != 1:
            print("FAIL: %r matched %d times, expected exactly 1"
                  % (label, found), file=sys.stderr)
            return 1
        raw = raw.replace(enc(find), enc(replace), 1)
        applied.append(label)

    for token in REQUIRED_AFTER:
        if raw.count(enc(token)) == 0:
            print("FAIL: %r is absent after patching" % (token,), file=sys.stderr)
            return 1

    # The old ramp must be GONE, not merely outnumbered: a document carrying
    # both would plunge on whichever ran.
    for token in ("rg[i] = smooth(rg[i], -1.02, dt * 26);",
                  "if (!Number.isFinite(rg[i])) rg[i] = -1.02;"):
        if raw.count(enc(token)):
            print("FAIL: the plunging ramp %r is still present" % (token,),
                  file=sys.stderr)
            return 1

    # Parse to adjudicate, never to write: a broken payload here is an editor
    # that does not load at all, and the artifact is one logical line so a human
    # diff will not catch it.
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        print("FAIL: the patched document no longer carries an html payload",
              file=sys.stderr)
        return 1

    if not applied:
        print("already applied: %d edit(s), nothing written" % len(already))
        return 0

    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied %d edit(s), %d already present; %d -> %d bytes"
          % (len(applied), len(already), before, len(raw)))
    for label in applied:
        print("  + " + label)
    return 0


if __name__ == "__main__":
    sys.exit(main())
