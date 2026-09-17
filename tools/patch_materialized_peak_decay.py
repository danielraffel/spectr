#!/usr/bin/env python3
"""Let the peak NUMBER fall, and keep the OVER state latched.

The readout welded two facts into one value. Both were held forever, and only
one of them should be.

  * THE NUMBER is a level. A level that only ever rises cannot show that the
    signal is alive, so a held reading and a frozen plug-in look identical.
    Somebody watching `OVER +5.8` with their Output trim pulled to -11.5 asked
    whether it ever updates -- which is the readout failing at the one job a
    meter has, not a discoverability problem to be papered over with a hint.
  * THE OVERLOAD is an event. It must latch until a person clears it: a clip
    indicator that decayed would hide the overshoot it exists to catch, which
    is exactly the thing a falling number cannot carry.

So they are split. The number gets ordinary meter ballistics -- hold, then
fall -- and the latch is untouched. Together they say what neither could
alone: a number that moves proves the readout is live, and a word that stays
proves something happened.

BALLISTICS, and why these two numbers.

  PEAK_HOLD_MS = 2000. This is a number a person READS, not a bar they watch,
  so the hold has to survive a glance: hear something, look over, read a
  five-glyph run. That is well under a second, and 2 s is the short end of the
  1-3 s peak-hold window DAW meters conventionally use. Too fast is strictly
  worse than too slow here -- a hold you miss is a hold that did not exist.

  PEAK_FALL_DB_PER_SEC = 12. The IEC 60268-10 Type I peak-programme-meter
  return rate is 20 dB in 1.7 s, i.e. 11.8 dB/s. Rounded to 12 it is the
  standard broadcast fallback, and slow enough that the digits are legible
  while they move instead of blurring into noise.

  PEAK_FLOOR_DB = -99.9. Digital silence publishes null, so nothing bounds the
  fall from underneath. Below this there is no peak worth printing and the
  readout goes back to "--".

THE CLOCK, and why this is an animation and not a poll. The native side
publishes only a CHANGED reading, so a steady tone or a stopped transport
publishes nothing -- and a decay driven by incoming frames alone would freeze
mid-fall, recreating the very defect being fixed. The fall therefore runs on
requestAnimationFrame, scheduled only while a fall is actually in flight and
self-terminating the moment the hold meets the live level or reaches the
floor. A converged meter schedules nothing and commits nothing.

WHAT THE NUMBER MEANS WHILE OVER IS LATCHED. It tracks the current peak, so
pulling the trim down visibly moves it while the word stays -- you can see you
fixed it. The worst overshoot since the reset is not lost: it is kept and
exposed as `data-spectr-output-over-peak` and in the control's title.

WHAT CLEARS THE LATCH. A click, and nothing else. A latch that clears itself
is a latch you cannot trust. Clearing also drops the hold, after which the
number falls straight back to the live level rather than blanking -- blanking
a meter that has signal under it would be the same lie in the other direction.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts every patch point occurs exactly once before
writing, re-checks the result, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

OLD_STATE = '''  const [reading, setReading] = React.useState({ peakDb: null, over: false });
  const [trim, setTrim] = React.useState(0);
  // The hold lives in a ref, not in state: it is updated on every published
  // frame and must not commit React on the frames where nothing printed
  // changes, which for a rising-only hold is almost all of them.
  const holdRef = React.useRef({ peakDb: null, over: false });
  const localTrimAtRef = React.useRef(0);
'''

NEW_STATE = '''  // METER BALLISTICS. The number holds, then falls; the overload latches.
  // 2000 ms is sized to be READ rather than watched -- hear something, look
  // over, read a five-glyph run -- and is the short end of the 1-3 s
  // peak-hold window DAW meters conventionally use. 12 dB/s is the IEC
  // 60268-10 Type I peak-programme-meter return rate (20 dB in 1.7 s), slow
  // enough that the digits stay legible while they move.
  const PEAK_HOLD_MS = 2000;
  const PEAK_FALL_DB_PER_SEC = 12;
  // Digital silence publishes null, so nothing bounds the fall from
  // underneath. Below this there is no peak worth printing.
  const PEAK_FLOOR_DB = -99.9;
  const [reading, setReading] = React.useState({
    peakDb: null, over: false, overPeakDb: null
  });
  const [trim, setTrim] = React.useState(0);
  // The hold lives in a ref, not in state: it is updated on every published
  // frame and on every frame of a fall, and must not commit React on the
  // frames where nothing PRINTED changes.
  //
  // `holdDb` + `roseAt` are the ballistic, not the displayed value: the fall
  // is recomputed from the value at the last rise rather than by decaying the
  // stored number, which would compound and accelerate. `liveDb` is the last
  // published level and is a fact about the signal, not a held reading -- it
  // survives a reset, or clearing would blank a meter that has signal in it.
  const holdRef = React.useRef({
    holdDb: null, roseAt: 0, liveDb: null, over: false, overPeakDb: null
  });
  const localTrimAtRef = React.useRef(0);
  const pumpRef = React.useRef(0);
  // What the readout SHOWS right now. Pure arithmetic over the ballistic, so
  // the same function answers the frame handler, the fall pump and the tests.
  const shownDb = (hold, nowMs) => {
    if (hold.holdDb === null) return hold.liveDb;
    const since = nowMs - hold.roseAt;
    if (since <= PEAK_HOLD_MS) return hold.holdDb;
    const fallen = hold.holdDb
      - PEAK_FALL_DB_PER_SEC * (since - PEAK_HOLD_MS) / 1000;
    // The fall lands ON the live level and never undercuts it: past the
    // crossing the readout is simply tracking the signal again.
    if (hold.liveDb !== null) return Math.max(hold.liveDb, fallen);
    return fallen > PEAK_FLOOR_DB ? fallen : null;
  };
  // Quantised to the digit the readout actually prints, so movement below the
  // last displayed decimal is not a React commit.
  const printedDb = (db) => db === null ? null : Math.round(db * 10) / 10;
  // ONE INSTANT, passed in. Reading the clock separately for the commit and
  // for the keep-falling decision leaves the fall up to a frame short: the
  // last commit lands just above the live level, the check a moment later sees
  // the crossing and stops, and the readout sits 0.1-0.4 dB high on a signal
  // that is still there. Measured on the shipping standalone at -11.1 against
  // a -11.5 level, and invisible to a test whose clock does not advance
  // between the two calls.
  const commitReading = (nowMs) => {
    const hold = holdRef.current;
    const db = printedDb(shownDb(hold, nowMs));
    setReading((previous) => previous.peakDb === db
      && previous.over === hold.over
      && previous.overPeakDb === hold.overPeakDb
      ? previous
      : { peakDb: db, over: hold.over, overPeakDb: hold.overPeakDb });
  };
  // Is a fall actually in flight? This is what makes the pump below an
  // animation rather than a poll -- it answers false the moment the hold has
  // met the live level, or reached the floor with nothing under it.
  const falling = (hold, nowMs) => {
    if (hold.holdDb === null) return false;
    const db = shownDb(hold, nowMs);
    if (db === null) return false;
    return hold.liveDb === null || db > hold.liveDb;
  };
  const pump = () => {
    pumpRef.current = 0;
    const now = Date.now();
    commitReading(now);
    if (falling(holdRef.current, now)) schedulePump();
  };
  const schedulePump = () => {
    if (pumpRef.current) return;
    if (typeof requestAnimationFrame !== "function") {
      // Without a frame clock the number cannot fall, and a readout that
      // silently stops falling is the defect this exists to remove. Say so.
      console.error("[Spectr] output meter has no frame clock; the peak "
        + "reading cannot decay");
      return;
    }
    pumpRef.current = requestAnimationFrame(pump);
  };
'''

OLD_FRAME = '''      const peak = typeof raw === "number" && isFinite(raw) ? raw : null;
      const hold = holdRef.current;
      if (peak !== null && (hold.peakDb === null || peak > hold.peakDb))
        hold.peakDb = peak;
      if (payload.over === true) hold.over = true;
      setReading((previous) => previous.peakDb === hold.peakDb
        && previous.over === hold.over
        ? previous
        : { peakDb: hold.peakDb, over: hold.over });
'''

NEW_FRAME = '''      const peak = typeof raw === "number" && isFinite(raw) ? raw : null;
      const hold = holdRef.current;
      const now = Date.now();
      hold.liveDb = peak;
      // The hold re-arms against what is CURRENTLY SHOWN, not against the
      // value it last latched. After a fall has begun a smaller transient is
      // still above the readout and must be held too -- re-arming against the
      // old latch would catch only the peaks that beat the session's loudest,
      // which is the frozen behaviour in disguise.
      if (peak !== null) {
        const shown = shownDb(hold, now);
        if (shown === null || peak >= shown) {
          hold.holdDb = peak;
          hold.roseAt = now;
        }
      }
      // THE HALF THAT DOES NOT DECAY. A person who looked away has no other
      // way to learn the signal went over, so this latches until they clear
      // it. The worst overshoot is kept beside it rather than in the printed
      // number, which is now tracking the live level so that pulling the trim
      // down visibly moves it.
      if (payload.over === true) {
        hold.over = true;
        if (peak !== null
            && (hold.overPeakDb === null || peak > hold.overPeakDb))
          hold.overPeakDb = peak;
      }
      commitReading(now);
      if (falling(hold, now)) schedulePump();
'''

OLD_RETURN = '''    return unsubscribe;
  }, []);
'''

NEW_RETURN = '''    return () => {
      if (pumpRef.current && typeof cancelAnimationFrame === "function")
        cancelAnimationFrame(pumpRef.current);
      pumpRef.current = 0;
      if (typeof unsubscribe === "function") unsubscribe();
    };
  }, []);
'''

OLD_RESET = '''  const resetHold = () => {
    holdRef.current = { peakDb: null, over: false };
    setReading({ peakDb: null, over: false });
  };
'''

NEW_RESET = '''  const resetHold = () => {
    const hold = holdRef.current;
    hold.holdDb = null;
    hold.roseAt = 0;
    hold.over = false;
    hold.overPeakDb = null;
    // `liveDb` deliberately survives: it is the signal, not a held reading.
    // Blanking it would leave a meter with audio running showing "--" until
    // the next CHANGED publication, which for a steady level never comes.
    setReading({
      peakDb: printedDb(shownDb(hold, Date.now())), over: false,
      overPeakDb: null
    });
    // The hold is gone, so nothing is falling and any frame queued for one is
    // now pointless work on the header's paint path.
    if (pumpRef.current && typeof cancelAnimationFrame === "function")
      cancelAnimationFrame(pumpRef.current);
    pumpRef.current = 0;
  };
'''

OLD_ATTRS = '''    "data-spectr-output-over": over ? "true" : "false",
    "aria-label": "Output peak level since reset, dBFS. Activate to reset.",
    title: over
      ? "The signal handed to the host reached full scale. Click to reset."
      : "Peak level handed to the host since reset, dBFS. Click to reset.",
'''

NEW_ATTRS = '''    "data-spectr-output-over": over ? "true" : "false",
    // The worst overshoot since the reset. It is no longer what the chip
    // prints -- that number tracks the live level now -- so it is carried
    // here, where the acceptance suite and the title can both read it.
    "data-spectr-output-over-peak": reading.overPeakDb === null
      ? "" : reading.overPeakDb.toFixed(1),
    // The printed level, as an attribute. The runtime's element shim reads
    // `textContent` as empty, so without this no probe driving the shipping
    // app can read the one thing that now has to MOVE -- and "the number did
    // not change" would be indistinguishable from "the number was
    // unreadable". Same idiom as the over flag beside it.
    "data-spectr-output-peak-db": reading.peakDb === null
      ? "" : reading.peakDb.toFixed(1),
    "aria-label": "Output peak level, dBFS. Held briefly, then falls. "
      + (over ? "Overload latched; activate to clear." : "Activate to clear."),
    title: over
      ? "The signal handed to the host reached full scale"
        + (reading.overPeakDb === null
            ? "" : " (worst " + reading.overPeakDb.toFixed(1) + " dBFS)")
        + ". The number tracks the current peak. Click to clear."
      : "Peak level handed to the host, dBFS. Held briefly, then falls. "
        + "Click to clear.",
'''

EDITS = [
    ("the ballistic, its clock and its commit gate are declared",
     OLD_STATE, NEW_STATE),
    ("a published frame re-arms the hold and latches the overload separately",
     OLD_FRAME, NEW_FRAME),
    ("unmount cancels a fall in flight as well as unsubscribing",
     OLD_RETURN, NEW_RETURN),
    ("clearing drops the hold and the latch but not the live level",
     OLD_RESET, NEW_RESET),
    ("the chip carries the worst overshoot and says the number falls",
     OLD_ATTRS, NEW_ATTRS),
]

REQUIRED_AFTER = (
    # The two behaviours, named. These are the load-bearing lines.
    "const PEAK_HOLD_MS = 2000;",
    "const PEAK_FALL_DB_PER_SEC = 12;",
    "if (payload.over === true) {\n        hold.over = true;",
    # The fall's clock, and its self-termination.
    "pumpRef.current = requestAnimationFrame(pump);",
    "    const now = Date.now();\n    commitReading(now);\n"
    "    if (falling(holdRef.current, now)) schedulePump();",
    # Tokens the two earlier scripts in this lane assert; this must not move
    # them.
    'window.pulp.on("output_meter"',
    'window.pulp.postMessage("param_set",',
    '"data-spectr-output-over": over ? "true" : "false",',
    '(over ? "OVER " : "PEAK ") + peakText',
    '"data-spectr-output-trim-label": true,',
    "React.createElement(SpectrOutputMeter, null));",
)

# The rising-only hold, in either of its two spellings: the shipped one and
# the one a later edit would most plausibly write.
FORBIDDEN_AFTER = (
    "if (peak !== null && (hold.peakDb === null || peak > hold.peakDb))",
    "holdRef.current = { peakDb: null, over: false };",
)

REQUIRED_COUNTS = {
    "function SpectrOutputMeter() {": 1,
    "const PEAK_HOLD_MS = 2000;": 1,
    "const shownDb = (hold, nowMs) => {": 1,
    "const falling = (hold, nowMs) => {": 1,
    '"data-spectr-output-over-peak": reading.overPeakDb === null': 1,
    '"data-spectr-output-peak-db": reading.peakDb === null': 1,
    "hold.liveDb = peak;": 1,
}


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()

    # The component this edits must exist exactly once before anything is
    # substituted. Without this an empty or restructured document would make
    # every "already applied" below vacuously true.
    anchor = "function SpectrOutputMeter() {"
    seen = raw.count(escaped(anchor))
    if seen != 1:
        sys.exit("FAIL: %r occurs %d times, expected 1; the output meter is "
                 "not where this script expects it" % (anchor, seen))

    changed = False
    applied = 0
    already = 0
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
            sys.exit("FAIL: %r survives after patching; the hold that never "
                     "falls is the defect this removes" % (token,))
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
