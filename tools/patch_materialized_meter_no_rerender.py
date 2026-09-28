#!/usr/bin/env python3
"""The output meter's number moves without re-rendering the editor.

Measured with Perfetto on the shipping standalone with audio running: the
native side publishes `output_meter` on every 0.1 dB change, 20-27 times a
second, and every publication committed React to print the new number. This
document is a captured import, so each commit re-applies the whole document --
1.5-4.5 ms on average, 30-47 ms at worst -- and the >100 ms stalls were runs of
those commits back to back.

The chip's React state is now the OVERLOAD only: its colour, word, title and
aria text change on an overload edge, which is rare. The printed level is
written straight into the readout's own nodes through refs (its text, and the
`data-spectr-output-peak-db` probe attribute), at most once per
READOUT_INTERVAL_MS (10 Hz) -- a changing number cannot be read faster, and a
reading that arrives inside the interval waits for the next slot on the fall
pump's frame clock rather than being lost. A render (an overload edge, a trim
change) prints the same level from the same ref, so the two writers cannot
disagree. The label span gets a fixed width, so a digit change cannot resize
the chip.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const READOUT_INTERVAL_MS = "

EDITS = [
    (
        "the chip's state is the overload only",
        '''  const [reading, setReading] = React.useState({
    peakDb: null, over: false, overPeakDb: null
  });
''',
        '''  // The chip's React state is the OVERLOAD only: its colour, word, title
  // and aria text change on an overload edge, which is rare. The printed
  // level changes on nearly every published frame and is written straight
  // into the readout's own nodes (writeLevel below) -- in this captured
  // import every React commit re-applies the whole document, and a meter
  // publishing ~25 times a second paid that on each one. See
  // tools/patch_materialized_meter_no_rerender.py.
  const [reading, setReading] = React.useState({
    over: false, overPeakDb: null
  });
  const chipRef = React.useRef(reading);
  // What the readout PRINTS, when it was last written, and whether a changed
  // reading is waiting for the next write slot.
  const levelRef = React.useRef({
    printedDb: null, writtenAt: -Infinity, pending: false
  });
  const peakButtonRef = React.useRef(null);
  const peakLabelRef = React.useRef(null);
  // How often the printed number may change: 10 Hz is as fast as a moving
  // number can be read, and each write is text the frame has to lay out.
  const READOUT_INTERVAL_MS = 100;
''',
    ),
    (
        "one formatter for the render and the direct write",
        '''  const commitReading = (nowMs) => {''',
        '''  const peakTextOf = (db) => db === null
    ? "--"
    : (db > 0 ? "+" : "") + db.toFixed(1);
  const chipLabel = (over, peakText) => (over ? "OVER " : "PEAK ") + peakText;
  // The level, written without a render: the label's text and the probe
  // attribute, from the same number a render would print.
  const writeLevel = (db, nowMs) => {
    const level = levelRef.current;
    level.printedDb = db;
    level.writtenAt = nowMs;
    level.pending = false;
    const label = peakLabelRef.current;
    if (label) label.textContent = chipLabel(chipRef.current.over, peakTextOf(db));
    const button = peakButtonRef.current;
    if (button && typeof button.setAttribute === "function")
      button.setAttribute("data-spectr-output-peak-db",
        db === null ? "" : db.toFixed(1));
  };
  const commitReading = (nowMs, force = false) => {''',
    ),
    (
        "only an overload edge renders",
        '''    const db = printedDb(shownDb(hold, nowMs));
    setReading((previous) => previous.peakDb === db
      && previous.over === hold.over
      && previous.overPeakDb === hold.overPeakDb
      ? previous
      : { peakDb: db, over: hold.over, overPeakDb: hold.overPeakDb });
  };
''',
        '''    const db = printedDb(shownDb(hold, nowMs));
    const level = levelRef.current;
    const chip = chipRef.current;
    if (chip.over !== hold.over || chip.overPeakDb !== hold.overPeakDb) {
      // An overload edge re-renders the chip, and that render prints the
      // level from levelRef, so the level is current as of this commit.
      chipRef.current = { over: hold.over, overPeakDb: hold.overPeakDb };
      level.printedDb = db;
      level.writtenAt = nowMs;
      level.pending = false;
      setReading(chipRef.current);
      return;
    }
    if (db === level.printedDb) {
      level.pending = false;
      return;
    }
    // Inside the interval the reading waits; the pump comes back for it.
    if (!force && nowMs - level.writtenAt < READOUT_INTERVAL_MS) {
      level.pending = true;
      return;
    }
    writeLevel(db, nowMs);
  };
''',
    ),
    (
        "the pump comes back for a waiting reading",
        '''    const now = Date.now();
    commitReading(now);
    if (falling(holdRef.current, now)
        || overExpiring(holdRef.current)) schedulePump();
''',
        '''    const now = Date.now();
    commitReading(now);
    if (falling(holdRef.current, now)
        || overExpiring(holdRef.current)
        || levelRef.current.pending) schedulePump();
''',
    ),
    (
        "a published frame schedules the waiting write",
        '''      commitReading(now);
      if (falling(hold, now) || overExpiring(hold)) schedulePump();
''',
        '''      commitReading(now);
      if (falling(hold, now) || overExpiring(hold)
          || levelRef.current.pending) schedulePump();
''',
    ),
    (
        "a click clears through the same writer, at once",
        '''    setReading({
      peakDb: printedDb(shownDb(hold, Date.now())), over: false,
      overPeakDb: null
    });
''',
        '''    // Shown at once rather than on the next write slot: a click is a
    // person waiting to see it take.
    commitReading(Date.now(), true);
''',
    ),
    (
        "the render prints the level the direct writer last wrote",
        '''  const peakText = reading.peakDb === null
    ? "--"
    : (reading.peakDb > 0 ? "+" : "") + reading.peakDb.toFixed(1);
''',
        '''  const printedPeakDb = levelRef.current.printedDb;
  const peakText = peakTextOf(printedPeakDb);
''',
    ),
    (
        "the probe attribute reads the same level",
        '''    "data-spectr-output-peak-db": reading.peakDb === null
      ? "" : reading.peakDb.toFixed(1),''',
        '''    "data-spectr-output-peak-db": printedPeakDb === null
      ? "" : printedPeakDb.toFixed(1),''',
    ),
    (
        "the chip is reachable by ref",
        '''    "data-spectr-output-peak": true,
''',
        '''    "data-spectr-output-peak": true,
    ref: peakButtonRef,
''',
    ),
    (
        "the label is reachable by ref and cannot resize the chip",
        '''  }, /* @__PURE__ */ React.createElement("span", { className: "tnum", style: {
    lineHeight: 1, whiteSpace: "nowrap"
  } }, (over ? "OVER " : "PEAK ") + peakText)),''',
        '''  }, /* @__PURE__ */ React.createElement("span", {
    "data-spectr-output-peak-label": true,
    ref: peakLabelRef,
    className: "tnum",
    // The chip's whole content box (96 - 2 x 8 padding - 2 x 1 border), so
    // the number's width is fixed however its digits change and a new
    // reading never resizes anything around it.
    style: {
      lineHeight: 1, whiteSpace: "nowrap", width: 78, minWidth: 78,
      flexShrink: 0, textAlign: "center"
    }
  }, chipLabel(over, peakText))),''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("meter no-rerender already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("meter no-rerender applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
