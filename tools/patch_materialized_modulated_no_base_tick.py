#!/usr/bin/env python3
"""A modulated knob or Morph slider shows the value playing and nothing else.

WHAT WAS WRONG. patch_materialized_modulated_single_indicator.py kept a short
tick on the ring (knobs) and on the track (Morph) at the BASE value while an
LFO drives the control. In a DAW it read as a second, stuck indicator -- a
knob parked at 100 % showed a white mark at the top of the ring while the
violet needle swept below it -- and users asked which of the two was the
sound.

WHAT THIS DOES. The needle and arc (knobs) and the thumb and fill (Morph)
already sit at the value playing, in violet. The base is no longer marked;
it is still what a drag, a wheel notch or an arrow key edits, what the
readout prints during a drag, and what host automation records. Nothing
else changes: the paths and refs stay, and an empty "base" path is a no-op
for spectrDrawIf.

Raw-text surgery on the escaped document, applied after
patch_materialized_modulated_single_indicator.py. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

KNOB_OLD = r'''    let tickD = "";
    if (played !== null) {
      const b = at(base) * Math.PI / 180, r1 = r0 - 2.2, r2 = r0 + 2.2;
      tickD = "M " + (c0 + r1 * Math.sin(b)).toFixed(2) + " " + (c0 - r1 * Math.cos(b)).toFixed(2)
        + " L " + (c0 + r2 * Math.sin(b)).toFixed(2) + " " + (c0 - r2 * Math.cos(b)).toFixed(2);
    }
'''
KNOB_NEW = r'''    // One indicator only (tools/patch_materialized_modulated_no_base_tick.py):
    // the base is not marked while an LFO drives the knob. The needle and arc
    // show the value playing and the readout prints the base during a drag.
    const tickD = "";
'''
MORPH_OLD = r'''      baseD = "M " + xb.toFixed(2) + " 3 L " + xb.toFixed(2) + " 13";
'''
MORPH_NEW = r'''      // One indicator only: no base tick on the track (see the knob above).
      void xb;
'''

REPLACEMENTS = [
    ("knob base tick removed", KNOB_OLD, KNOB_NEW),
    ("morph base tick removed", MORPH_OLD, MORPH_NEW),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    applied = 0
    for label, old, new in REPLACEMENTS:
        if raw.count(encode(new)) == 1 and raw.count(encode(old)) == 0:
            print(f"{label}: already applied")
            continue
        count = raw.count(encode(old))
        if count != 1:
            print(f"{label}: anchor found {count} times (expected 1)", file=sys.stderr)
            return 1
        raw = raw.replace(encode(old), encode(new), 1)
        applied += 1
        print(f"{label}: applied")
    if applied:
        PATH.write_text(raw, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
