#!/usr/bin/env python3
"""The OVER chip goes out by itself; latching it is a setting you turn on.

THE DEFECT, as reported: "the OVER info stays red on over until you click on
it."  Spectr's output readout splits into two halves on purpose -- the number
holds and falls, the overload latches -- and the latch half was unconditional.
One transient at three in the morning and the chip is red for the rest of the
session, past the fix, past the render, past the next twelve edits, until
somebody happens to click it.  A lamp that is red long after the condition
ended stops being read as a condition; it is read as decoration, which is the
precise way an indicator dies.

WHAT REPLACES IT.  By default the red is a statement about NOW: an over frame
lights it, every further over refreshes it, and OVER_HOLD_MS after the last one
it goes out on its own.  The old behaviour is kept whole behind a setting --
`overLatch`, default off -- for the person who leaves a mix rendering and wants
to know afterwards whether it ever went over.  Both readings are legitimate;
only one of them can be the default, and the one that matches what people
already read a red lamp as is "it is over right now".

WHY A TIMED WINDOW RATHER THAN FOLLOWING THE EDGE.  The obvious implementation
is `hold.over = payload.over === true`.  It is wrong for two independent
reasons, and only the first is about correctness:

  * PERCEIVABILITY.  The publisher ticks at `kPublishPeriodSeconds`.  A
    single-block overload would light the chip for one tick and clear it on the
    next -- a ~33 ms flash nobody sees.  The event most worth reporting is
    exactly the one edge-following hides.  A window makes a transient
    readable, and it is the same shape the number beside it already has.
  * ROBUSTNESS.  A clear that depends on a publication arriving is a clear that
    does not happen when one does not.  `src/ui/native_editor.cpp` publishes
    only a CHANGED reading and wraps `load_script` in a try/catch that logs and
    swallows -- so a dropped falling edge leaves the chip red forever, which is
    this same defect wearing the fix's clothes.

The falling edge IS published today: `moved` in that publisher is
`quantised != peak || level.over != over || level.trim_db != trim`, so `over`
going false is itself a change, and `Spectr::read_output_level()` derives
`over` from the latest meter frame with no latch of its own.  That was checked
rather than assumed -- but the clear is not built on it.  Time is the authority
here and a publication only ever refreshes the window.

WHAT HAPPENS TO THE WORST OVERSHOOT.  `overPeakDb` expires WITH the lamp.  They
are one statement: a cleared chip beside a tooltip still naming a worst
overshoot would assert an overload the chip says is not happening, and the
acceptance suite reads that number out of `data-spectr-output-over-peak`.  So
in auto-clear mode it is scoped to the overload being reported now and a later
over measures afresh.  Latched, it keeps its session-wide "worst since you last
cleared" meaning, which is the only reading a latch can honestly carry.

WHERE THE MODE LIVES.  `latchRef`, mirrored from the prop on every render.  The
ballistic runs out of the FIRST render's closures -- the published-frame
handler is installed by a `[]` effect -- so a prop read inside it would be the
mount-time value for the life of the editor.  A second effect keyed on the
prop commits when the mode changes, because turning the latch off has to
release a chip that is already stuck on and the native side owes this component
no further frame while the signal is steady.

WHAT IS NOT TOUCHED.  `const over = reading.over === true;` stays exactly as it
was, and so does `commitReading`'s body: expiry is retired on the hold ref
before the commit reads it, so there is no second notion of "shown over" for
the render and the probe attribute to disagree about.
`data-spectr-output-over` therefore keeps tracking the lamp for free.

SINGLE WRITER.  Nothing here touches `drawBands`, the `if (G.isSel)` block, or
the mute chip; the edits are confined to `SpectrOutputMeter`, `Chrome`'s one
call to it, `SpectrSettingsToggle`, the FEEDBACK settings group, and the
`tweak-defaults` block.  The new settings row is appended LAST inside its
group, so no sibling is renumbered -- this document's text/layout/paint
bindings address nodes by positional DOM path.  (Measured: all 81 layout, 24
text and 17 paint paths run through `div[0]>div[1]` (the top bar) and
`div[0]>div[3]` (the bottom rail); none reaches the settings overlay, which is
`display:none` at capture.)

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
    rewrite bytes that have nothing to do with this change."""
    return json.dumps(snippet)[1:-1]


# -- 1. the component takes the mode ---------------------------------------

SIG = "function SpectrOutputMeter() {\n"
SIG_NEW = "function SpectrOutputMeter({ latchOver = false } = {}) {\n"

# -- 2. the overload's own window ------------------------------------------

FALL_RATE = "  const PEAK_FALL_DB_PER_SEC = 12;\n"
FALL_RATE_NEW = (
    "  const PEAK_FALL_DB_PER_SEC = 12;\n"
    "  // THE OVERLOAD'S OWN WINDOW. By default the red says something about\n"
    "  // NOW: an over frame lights it, every further over refreshes it, and\n"
    "  // OVER_HOLD_MS after the last one it goes out by itself. Not\n"
    "  // edge-following -- the native publisher does publish the falling edge\n"
    "  // (`over` sits in its CHANGED predicate), but a single-block overload\n"
    "  // would then light the chip for one ~33ms tick and nobody would see\n"
    "  // the event most worth reporting. A window makes a transient readable,\n"
    "  // and it does not depend on a publication arriving to end.\n"
    "  //\n"
    "  // The peak hold's own window, deliberately: while the chip is red the\n"
    "  // number beside it is still holding the overshoot that made it red, so\n"
    "  // the lamp and the reading it explains expire together rather than at\n"
    "  // two unrelated times. `latchOver` opts out of expiry entirely.\n"
    "  const OVER_HOLD_MS = PEAK_HOLD_MS;\n"
)

# -- 3. the ballistic carries the moment of the last over -------------------

HOLD_REF = (
    "  const holdRef = React.useRef({\n"
    "    holdDb: null, roseAt: 0, liveDb: null, over: false, overPeakDb: null\n"
    "  });\n"
)
HOLD_REF_NEW = (
    "  const holdRef = React.useRef({\n"
    "    holdDb: null, roseAt: 0, liveDb: null, over: false, overPeakDb: null,\n"
    "    // When the most recent over frame arrived. The window is measured\n"
    "    // from the LAST one, so a sustained overload keeps the chip red for\n"
    "    // as long as it lasts instead of blinking every OVER_HOLD_MS.\n"
    "    overAt: 0\n"
    "  });\n"
)

# -- 4. the mode, reachable from the ballistic ------------------------------

PUMP_REF = "  const pumpRef = React.useRef(0);\n"
PUMP_REF_NEW = (
    "  const pumpRef = React.useRef(0);\n"
    "  // The mode has to be reachable from the BALLISTIC, and the ballistic\n"
    "  // runs out of the first render's closures over refs -- the\n"
    "  // published-frame handler is installed by a `[]` effect, so a prop read\n"
    "  // inside it would be the mount-time value for the life of the editor.\n"
    "  // Mirrored on every render rather than through an effect, so a flip is\n"
    "  // in force before the next published frame rather than after it.\n"
    "  const latchRef = React.useRef(latchOver === true);\n"
    "  latchRef.current = latchOver === true;\n"
)

# -- 5. expiry, beside the fall it mirrors ----------------------------------

PRINTED = ("  const printedDb = (db) => db === null ? null "
           ": Math.round(db * 10) / 10;\n")
PRINTED_NEW = PRINTED + (
    "  // Is an overload report still waiting to go out by itself? This is what\n"
    "  // carries the auto-clear to its expiry when nothing further will be\n"
    "  // published: the falling edge of `over` does arrive, but it arrives\n"
    "  // when the window STARTS, never when it ends. Latched, nothing is\n"
    "  // pending -- only a click ends it.\n"
    "  const overExpiring = (hold) => hold.over && !latchRef.current;\n"
    "  // Retire an expired overload report. The lamp and the worst overshoot\n"
    "  // go together: they are one statement, and a cleared chip beside a\n"
    "  // tooltip still naming a worst overshoot would assert an overload the\n"
    "  // chip says is not happening. So in auto-clear mode `overPeakDb` is\n"
    "  // scoped to the overload being reported NOW and a later over measures\n"
    "  // afresh; latched, it keeps its session-wide \"worst since you last\n"
    "  // cleared\" meaning, which is the only reading a latch can carry.\n"
    "  const expireOver = (hold, nowMs) => {\n"
    "    if (!overExpiring(hold)) return;\n"
    "    if (nowMs - hold.overAt <= OVER_HOLD_MS) return;\n"
    "    hold.over = false;\n"
    "    hold.overAt = 0;\n"
    "    hold.overPeakDb = null;\n"
    "  };\n"
)

# -- 5b. two comments the change makes untrue -------------------------------
#
# Both stated the latch as unconditional. Left alone they would be the only
# description of this half of the component, and wrong.

BALLISTICS = ("  // METER BALLISTICS. The number holds, then falls; "
              "the overload latches.\n")
BALLISTICS_NEW = (
    "  // METER BALLISTICS. The number holds, then falls. The overload gets a\n"
    "  // hold of its own and then goes out, unless `latchOver` is on, in\n"
    "  // which case it stays until a person clears it.\n"
)

LATCH_COMMENT = (
    "      // THE HALF THAT DOES NOT DECAY. A person who looked away has no other\n"
    "      // way to learn the signal went over, so this latches until they clear\n"
    "      // it. The worst overshoot is kept beside it rather than in the printed\n"
    "      // number, which is now tracking the live level so that pulling the trim\n"
    "      // down visibly moves it.\n"
)
LATCH_COMMENT_NEW = (
    "      // THE HALF THAT DOES NOT FALL WITH THE NUMBER. Every over frame\n"
    "      // (re)opens the report and stamps its window; `expireOver` decides\n"
    "      // when it closes, which under `latchOver` is never and otherwise is\n"
    "      // OVER_HOLD_MS after the last one. The worst overshoot is kept beside\n"
    "      // it rather than in the printed number, which is now tracking the live\n"
    "      // level so that pulling the trim down visibly moves it.\n"
)

# -- 6. one place decides what is shown -------------------------------------

COMMIT = (
    "  const commitReading = (nowMs) => {\n"
    "    const hold = holdRef.current;\n"
    "    const db = printedDb(shownDb(hold, nowMs));\n"
)
COMMIT_NEW = (
    "  const commitReading = (nowMs) => {\n"
    "    const hold = holdRef.current;\n"
    "    // The one place that decides what is SHOWN, so it is also the one\n"
    "    // place an expired overload is retired. The render below then keeps\n"
    "    // reading the flag straight off the hold and there is no second\n"
    "    // notion of \"shown over\" for the probe attribute to disagree with.\n"
    "    expireOver(hold, nowMs);\n"
    "    const db = printedDb(shownDb(hold, nowMs));\n"
)

# -- 7. the pump carries the clear, and still self-terminates ---------------
#
# `commitReading` above has already retired an expired report by the time
# these run, so once the chip goes out `overExpiring` answers false and the
# animation stops -- no permanent per-frame work in the header.

PUMP_LINE = (
    "    commitReading(now);\n"
    "    if (falling(holdRef.current, now)) schedulePump();\n"
    "  };\n"
)
PUMP_LINE_NEW = (
    "    commitReading(now);\n"
    "    if (falling(holdRef.current, now)\n"
    "        || overExpiring(holdRef.current)) schedulePump();\n"
    "  };\n"
)

HANDLER_LINE = (
    "      commitReading(now);\n"
    "      if (falling(hold, now)) schedulePump();\n"
)
HANDLER_LINE_NEW = (
    "      commitReading(now);\n"
    "      if (falling(hold, now) || overExpiring(hold)) schedulePump();\n"
)

# -- 8. an over stamps the window -------------------------------------------

OVER_SET = (
    "      if (payload.over === true) {\n"
    "        hold.over = true;\n"
)
OVER_SET_NEW = (
    "      if (payload.over === true) {\n"
    "        hold.over = true;\n"
    "        hold.overAt = now;\n"
)

# -- 9. the click still clears, in both modes -------------------------------

RESET = (
    "    hold.over = false;\n"
    "    hold.overPeakDb = null;\n"
)
RESET_NEW = (
    "    hold.over = false;\n"
    "    // Cleared by hand in BOTH modes. Auto-clear shortens the wait; it\n"
    "    // does not take away the way a person ends the report early.\n"
    "    hold.overAt = 0;\n"
    "    hold.overPeakDb = null;\n"
)

# -- 10. a mode flip commits on its own -------------------------------------

UNSUB = (
    '      if (typeof unsubscribe === "function") unsubscribe();\n'
    "    };\n"
    "  }, []);\n"
)
UNSUB_NEW = UNSUB + (
    "  // Turning the latch OFF has to release a chip that is already stuck\n"
    "  // on. The native side publishes only a CHANGED reading, so a steady\n"
    "  // signal owes this component no further frame: the mode change drives\n"
    "  // its own commit rather than waiting for one that may never come.\n"
    "  React.useEffect(() => {\n"
    "    const now = Date.now();\n"
    "    commitReading(now);\n"
    "    if (overExpiring(holdRef.current)) schedulePump();\n"
    "  }, [latchOver]);\n"
)

# -- 11. the chip says which mode it is in ----------------------------------

ARIA = (
    '    "aria-label": "Output peak level, dBFS. Held briefly, then falls. "\n'
    '      + (over ? "Overload latched; activate to clear." '
    ': "Activate to clear."),\n'
)
ARIA_NEW = (
    '    "aria-label": "Output peak level, dBFS. Held briefly, then falls. "\n'
    "      + (over\n"
    "          ? (latchOver\n"
    '              ? "Overload latched; activate to clear."\n'
    '              : "Overload; clears itself. Activate to clear now.")\n'
    '          : "Activate to clear."),\n'
)

TITLE = (
    '        + ". The number tracks the current peak. Click to clear."\n'
)
TITLE_NEW = (
    '        + ". The number tracks the current peak. "\n'
    "        + (latchOver\n"
    '            ? "Held until you clear it. Click to clear."\n'
    '            : "Clears itself once the signal is back under full scale. "\n'
    '              + "Click to clear now.")\n'
)

# -- 12. Chrome hands the meter the setting ---------------------------------
#
# `=== true`, not `!== false`: an absent key must resolve to auto-clear, so a
# stored settings blob written before this existed opens un-latched rather
# than inheriting the behaviour being retired.

RENDER_SITE = "React.createElement(SpectrOutputMeter, null)"
RENDER_SITE_NEW = ("React.createElement(SpectrOutputMeter, "
                   "{ latchOver: settings.overLatch === true })")

# -- 13. the setting itself, in the idiom its neighbours use ----------------

DEFAULTS = '  "statusInfo": true,\n  "showBuildInfo": true,\n'
DEFAULTS_NEW = ('  "statusInfo": true,\n  "showBuildInfo": true,\n'
                '  "overLatch": false,\n')

TOGGLE_SIG = ("function SpectrSettingsToggle({ value, onChange, "
              "statusInfo = false, buildInfo = false }) {\n")
TOGGLE_SIG_NEW = ("function SpectrSettingsToggle({ value, onChange, "
                  "statusInfo = false, buildInfo = false, "
                  "overLatch = false }) {\n")

TOGGLE_ATTRS = (
    '      "data-spectr-build-info-toggle": buildInfo ? "true" : void 0,\n'
    '      "data-spectr-build-info-state": buildInfo ? value ? "on" : "off" '
    ': void 0,\n'
)
TOGGLE_ATTRS_NEW = TOGGLE_ATTRS + (
    '      "data-spectr-over-latch-toggle": overLatch ? "true" : void 0,\n'
    '      "data-spectr-over-latch-state": overLatch ? value ? "on" : "off" '
    ': void 0,\n'
)

# Appended LAST inside the FEEDBACK group. Position is not cosmetic: this
# document's bindings address nodes by positional DOM path, so a row inserted
# anywhere but the end renumbers every later sibling.
FEEDBACK_TAIL = (
    'React.createElement(SpectrSettingsToggle, { buildInfo: true, '
    'value: settings.showBuildInfo !== false, '
    'onChange: (v) => persist({ showBuildInfo: v }) }))), '
    'React.createElement(SpectrModulationSettings, null)'
)
FEEDBACK_TAIL_NEW = (
    'React.createElement(SpectrSettingsToggle, { buildInfo: true, '
    'value: settings.showBuildInfo !== false, '
    'onChange: (v) => persist({ showBuildInfo: v }) })), '
    '/* @__PURE__ */ React.createElement(SpectrSettingsField, '
    # A noun phrase, like every other row in this panel (Theme, Bloom,
    # Mute style, Minimap, Status info). The hint is the user's own
    # words for the behaviour it restores.
    '{ label: "OVER latch", '
    'hint: "Stay red until you click it" }, '
    '/* @__PURE__ */ React.createElement(SpectrSettingsToggle, '
    '{ overLatch: true, value: settings.overLatch === true, '
    'onChange: (v) => persist({ overLatch: v }) }))), '
    'React.createElement(SpectrModulationSettings, null)'
)


EDITS = [
    ("the meter takes the latch mode as a prop", (SIG, SIG_NEW),
     "function SpectrOutputMeter({ latchOver = false } = {}) {"),
    ("the overload gets its own hold window", (FALL_RATE, FALL_RATE_NEW),
     "const OVER_HOLD_MS = PEAK_HOLD_MS;"),
    ("the ballistic records when the last over arrived",
     (HOLD_REF, HOLD_REF_NEW), "    overAt: 0\n  });"),
    ("the mode is mirrored into a ref the ballistic can read",
     (PUMP_REF, PUMP_REF_NEW), "latchRef.current = latchOver === true;"),
    ("an expired overload report is retired, lamp and worst overshoot alike",
     (PRINTED, PRINTED_NEW), "const expireOver = (hold, nowMs) => {"),
    ("the ballistics note stops claiming an unconditional latch",
     (BALLISTICS, BALLISTICS_NEW), "hold of its own and then goes out, unless"),
    ("the latch note describes the window it now opens",
     (LATCH_COMMENT, LATCH_COMMENT_NEW),
     "// THE HALF THAT DOES NOT FALL WITH THE NUMBER."),
    ("the commit retires an expired report before reading it",
     (COMMIT, COMMIT_NEW), "    expireOver(hold, nowMs);"),
    ("the frame pump carries the auto-clear to its expiry",
     (PUMP_LINE, PUMP_LINE_NEW), "|| overExpiring(holdRef.current)) schedulePump();"),
    ("a published frame keeps the pump alive while a report is pending",
     (HANDLER_LINE, HANDLER_LINE_NEW),
     "if (falling(hold, now) || overExpiring(hold)) schedulePump();"),
    ("every over frame refreshes the window", (OVER_SET, OVER_SET_NEW),
     "        hold.overAt = now;"),
    # The `done` token is the COMMENT, not the code. `expireOver` above sets
    # the same two fields at the same indentation, so a code-shaped token
    # would read as "already applied" the moment that edit landed -- and this
    # one would be silently skipped, leaving the manual clear half-written.
    ("a click still clears in both modes", (RESET, RESET_NEW),
     "// Cleared by hand in BOTH modes."),
    ("flipping the latch off releases a chip already stuck on",
     (UNSUB, UNSUB_NEW), "  }, [latchOver]);"),
    ("the accessible name says which mode the chip is in", (ARIA, ARIA_NEW),
     '"Overload; clears itself. Activate to clear now."'),
    ("the tooltip says which mode the chip is in", (TITLE, TITLE_NEW),
     '"Clears itself once the signal is back under full scale. "'),
    ("Chrome hands the meter the setting", (RENDER_SITE, RENDER_SITE_NEW),
     "React.createElement(SpectrOutputMeter, { latchOver: "
     "settings.overLatch === true })"),
    ("the setting is declared, defaulting to auto-clear",
     (DEFAULTS, DEFAULTS_NEW), '"overLatch": false,'),
    ("the settings toggle can mark itself for a probe",
     (TOGGLE_SIG, TOGGLE_SIG_NEW), "buildInfo = false, overLatch = false }) {"),
    ("the toggle exposes its state as an attribute",
     (TOGGLE_ATTRS, TOGGLE_ATTRS_NEW), '"data-spectr-over-latch-toggle"'),
    ("the FEEDBACK group gains the row, appended last",
     (FEEDBACK_TAIL, FEEDBACK_TAIL_NEW), 'label: "OVER latch"'),
]

# Asserted present after every run. The last four are CONTROLS on code this
# script relies on but does not write: if a future restructure moved them this
# patch would still "apply" while meaning nothing.
REQUIRED_AFTER = (
    "const OVER_HOLD_MS = PEAK_HOLD_MS;",
    "const overExpiring = (hold) => hold.over && !latchRef.current;",
    "const expireOver = (hold, nowMs) => {",
    "    expireOver(hold, nowMs);",
    "        hold.overAt = now;",
    "// Cleared by hand in BOTH modes.",
    "// THE HALF THAT DOES NOT FALL WITH THE NUMBER.",
    '"overLatch": false,',
    'label: "OVER latch"',
    "React.createElement(SpectrOutputMeter, { latchOver: "
    "settings.overLatch === true })",
    # controls
    "const over = reading.over === true;",
    '"data-spectr-output-over": over ? "true" : "false",',
    "const PEAK_HOLD_MS = 2000;",
    "onClick: resetHold,",
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. Every edit is anchored inside
    # the output meter, Chrome's one call to it, or the settings surface; a
    # document without all four is one this script must refuse rather than
    # no-op into "already applied".
    anchors = {
        "output meter": enc("function SpectrOutputMeter("),
        "peak hold window": enc("const PEAK_HOLD_MS = 2000;"),
        "over latch site": enc("        hold.over = true;"),
        "settings toggle": enc("function SpectrSettingsToggle({"),
        "feedback group": enc('title: "FEEDBACK"'),
        "tweak defaults": enc('"showBuildInfo": true,'),
    }
    wrong = []
    for label, needle in anchors.items():
        count = raw.count(needle)
        print("control: %-18s %d" % (label, count))
        # Per anchor, never a total: a sum is satisfied by one anchor
        # appearing twice while another is gone, which is precisely the
        # restructured document this refuses to write into.
        if count != 1:
            wrong.append("%s=%d" % (label, count))
    if wrong:
        print("FAIL: expected exactly 1 of each anchor, got %s -- wrong "
              "document" % ", ".join(wrong), file=sys.stderr)
        return 1

    applied, already = [], []
    for label, (find, replace), done in EDITS:
        # `done` alone decides. Several replacements CONTAIN their own needle
        # (an insertion after an anchor leaves the anchor in place), so a rule
        # that also required `find == 0` would re-apply them every run.
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
            print("FAIL: %r is absent after patching" % (token,),
                  file=sys.stderr)
            return 1

    # Parse to adjudicate, never to write: a broken payload here is an editor
    # that does not load at all, and the artifact is one logical line so a
    # human diff will not catch it.
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        print("FAIL: the patched document no longer carries an html payload",
              file=sys.stderr)
        return 1
    # The bindings address nodes by positional DOM path. This patch adds one
    # settings row, appended last inside its group inside an overlay no
    # binding path enters -- so the counts must be untouched, and a change
    # here means something renumbered.
    counts = {"text_bindings": 24, "layout_bindings": 81, "paint_bindings": 17}
    for key, expected in counts.items():
        actual = len(document.get(key) or [])
        if actual != expected:
            print("FAIL: %s is %d, expected %d -- the binding table moved"
                  % (key, actual, expected), file=sys.stderr)
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
