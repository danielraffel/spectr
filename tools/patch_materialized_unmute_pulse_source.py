#!/usr/bin/env python3
"""Give the unmute flourish a SOURCE, so a replay cannot fire it.

THE FLOURISH.  `unmutePulseRef` is a per-band value set to 1 and decayed at
`dt * 3.5` in the rAF loop.  While it is above zero the band's spectral edge is
stroked brighter and thicker -- about 285ms of flair that reads as a response
to a tap.  It is set in exactly two places, `commitGain` and its batch sibling
`commitMany`, and only on a genuine muted -> unmuted transition:

    if (wasMuted && !isMuted(value)) {
      renderGainsRef.current[idx] = 0;
      unmutePulseRef.current[idx] = 1;
    }

Reported as distracting when it repeats on every threshold crossing under
playback.  The band dropping to zero is wanted; the extra flair is not.

WHAT THE MEASUREMENT ACTUALLY FOUND, and it is not what the report assumed.
In the SHIPPING NATIVE build every replay source already bypasses both commit
functions, so none of them can fire the pulse:

    optimisticNativeMorph     writes targetGainsRef directly
    applyHostAutomationState  writes targetGainsRef directly
    applyModulationFrame      writes renderGainsRef only

and `setMorph` / `recallSnap` return early down the `nativeOwnsSnapshots`
branch -- which is true whenever `window.pulp.postMessage` exists, i.e. in
every native host -- so their `commitMany` tails are unreachable there.

So the flourish is ALREADY gesture-only on the native surface.  It is gesture
-only BY ACCIDENT: nothing states the rule, nothing tests it, and the property
is an artifact of which branch happens to return first.  The browser fallback
(`nativeOwnsSnapshots === false`, the lane every `.mjs` suite drives) still has
the defect exactly as reported -- `setMorph` walks 64 bands into `commitMany`
and every band crossing the mute threshold pulses.

THE FIX IS THE RULE, WRITTEN DOWN.  `commitGain` and `commitMany` take a
`replay` flag, defaulted false, and skip ONLY the pulse when it is set.  The
two replay entry points pass it.  `renderGainsRef` still goes to 0, so the band
still falls and rises the way the report says it should; only the flair is
withheld.

WHY A FLAG AND NOT `modulationActiveRef`.  That ref was the obvious candidate
-- it already gates neighbouring behaviour one line away in the same loop --
and it is the wrong instrument.  Its own comment says what it is: "True while
the internal LFOs are driving the drawn bank."  It is written in exactly one
place, `applyModulationFrame`, so it is false during a morph replay and false
during host automation.  Gating on it would have looked correct, changed
nothing, and left a test that passes for the wrong reason.

THE MORPH DRAG IS A REPLAY, DELIBERATELY.  Dragging the morph slider by hand is
a gesture, so a source rule keyed on "did a human do this" would pulse on every
pointer sample of the drag -- the exact repetition the report is about.  The
user is SWEEPING BETWEEN TWO CAPTURED STATES, not toggling a band, and the band
states arriving are replayed from snapshots rather than authored.  So morph is
a replay whoever moves it, and the flag lives on the morph path rather than on
a gesture test.

Same for `recallSnap`: recalling a slot is a gesture that REPLAYS 64 authored
values.  The gesture is "recall", not "unmute this band".

OWNERSHIP.  The four patch points here have no other writer.  The morph tail is
the FINAL text of `tools/patch_materialized_morph_viewport.py`, which is
marker-guarded and long since applied -- it returns at its marker before it
looks at a patch point, so it cannot compete for that line.  Its output is
named as this script's predecessor so a regenerated document converges either
way.

NOTE: the document is compiled into the binary by `pulp_add_binary_data`
(CMakeLists.txt `spectr_native_assets`), so a rebuild is REQUIRED before any
native test reflects this patch.  `Encoding binary asset
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
    rewrite bytes that have nothing to do with this change and collide with
    every other lane editing the same artifact."""
    return json.dumps(snippet)[1:-1]


def predecessors(old):
    """An edit's `old` is one text, or several ordered OLDEST FIRST."""
    return (old,) if isinstance(old, str) else tuple(old)


def choose(old, raw):
    """The most advanced predecessor present, or a reason there is none.

    Newest first: an older predecessor is often still a substring of a newer
    one, so "the first candidate that matches" would re-apply from a state the
    document has already moved past."""
    for cand in reversed(predecessors(old)):
        count = raw.count(enc(cand))
        if count == 1:
            return cand, None
        if count > 1:
            return None, ('patch point %r occurs %d times, expected 1'
                          % (cand, count))
    return None, 'no known predecessor text is present'


# (label, old-or-predecessors, new)
EDITS = [
    ("commitGain takes a source",
     "const commitGain = (idx, value, deferReact = false) => {",
     "const commitGain = (idx, value, deferReact = false, replay = false) => {"),

    # The band still drops to zero and rises -- that half is wanted. Only the
    # flair is withheld, and only when the caller says the change is replayed.
    ("commitGain withholds the flourish from a replay",
     "    if (wasMuted && !isMuted(value)) {\n"
     "      renderGainsRef.current[idx] = 0;\n"
     "      unmutePulseRef.current[idx] = 1;\n"
     "    }",
     "    if (wasMuted && !isMuted(value)) {\n"
     "      renderGainsRef.current[idx] = 0;\n"
     "      // The rise from zero belongs to the band; the flourish belongs to\n"
     "      // the gesture. A replayed crossing gets the first and not the\n"
     "      // second, so a sweep or a recall stops flashing every band it\n"
     "      // carries back across the mute threshold.\n"
     "      if (!replay) unmutePulseRef.current[idx] = 1;\n"
     "    }"),

    ("commitMany takes a source",
     "const commitMany = (map, deferReact = false) => {",
     "const commitMany = (map, deferReact = false, replay = false) => {"),

    ("commitMany withholds the flourish from a replay",
     "      if (wasMuted && !isMuted(v)) {\n"
     "        renderGainsRef.current[k] = 0;\n"
     "        unmutePulseRef.current[k] = 1;\n"
     "      }",
     "      if (wasMuted && !isMuted(v)) {\n"
     "        renderGainsRef.current[k] = 0;\n"
     "        if (!replay) unmutePulseRef.current[k] = 1;\n"
     "      }"),

    # The morph tail. The predecessor is patch_materialized_morph_viewport's
    # final text; that script is marker-guarded and returns before it inspects
    # a patch point, so naming its output here takes over the line rather than
    # competing for it.
    ("a morph sweep is a replay",
     "        morphViewport(s.A, s.B, v, deferReact);\n"
     "        commitMany(map, deferReact);",
     "        morphViewport(s.A, s.B, v, deferReact);\n"
     "        commitMany(map, deferReact, true);"),

    # Recall is a gesture that replays 64 authored values. The gesture is
    # "recall", not "unmute this band", so the bands it carries back across the
    # threshold are not each of them a tap.
    ("a snapshot recall is a replay",
     "          map.set(i, snap.values[i]);\n"
     "        commitMany(map);",
     "          map.set(i, snap.values[i]);\n"
     "        commitMany(map, false, true);"),
]

# Proof the patch landed, independent of the substitution bookkeeping.
FORBIDDEN_AFTER = (
    # An ungated pulse in either commit function is the defect itself.
    "      renderGainsRef.current[idx] = 0;\n"
    "      unmutePulseRef.current[idx] = 1;\n",
    "        renderGainsRef.current[k] = 0;\n"
    "        unmutePulseRef.current[k] = 1;\n",
    # A replay path that commits without declaring itself one.
    "        morphViewport(s.A, s.B, v, deferReact);\n"
    "        commitMany(map, deferReact);",
    "          map.set(i, snap.values[i]);\n"
    "        commitMany(map);",
)

REQUIRED_AFTER = (
    "const commitGain = (idx, value, deferReact = false, replay = false) => {",
    "const commitMany = (map, deferReact = false, replay = false) => {",
    "if (!replay) unmutePulseRef.current[idx] = 1;",
    "if (!replay) unmutePulseRef.current[k] = 1;",
    "commitMany(map, deferReact, true);",
    "commitMany(map, false, true);",
    # The half of the behaviour that must SURVIVE: the band still drops to
    # zero under a replay, so it still falls and rises. A patch that withheld
    # this too would be fixing the wrong half of the report.
    "      renderGainsRef.current[idx] = 0;\n",
    "        renderGainsRef.current[k] = 0;\n",
    # A gesture still pulses. Nothing here may gate the pulse on anything but
    # the source, so these two call shapes must remain flag-free.
    "commitGain(b, isMuted(cur) ? 0 : -Infinity);",
)


def main():
    for label, old, new in EDITS:
        if not new:
            sys.exit('FAIL %s: empty replacement has no applied marker' % label)
        for cand in predecessors(old):
            if cand and cand in new and cand == new:
                sys.exit('FAIL %s: replacement is identical to its patch point'
                         % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    for label, old, new in EDITS:
        new_e = enc(new)
        if raw.count(new_e) >= 1:
            print('already applied ', label)
            continue
        cand, why = choose(old, raw)
        if cand is None:
            sys.exit('FAIL %s: %s' % (label, why))
        raw = raw.replace(enc(cand), new_e)
        changed = True
        print('applied         ', label)

    # Version-agnostic: every edit must be in its FINAL state exactly once,
    # whether this run put it there or a previous one did.
    for label, _old, new in EDITS:
        count = raw.count(enc(new))
        if count != 1:
            sys.exit('FAIL %s: final text occurs %d times after patching, '
                     'expected 1' % (label, count))

    for token in FORBIDDEN_AFTER:
        count = raw.count(enc(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        if raw.count(enc(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str) or not document['html']:
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change needed')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
