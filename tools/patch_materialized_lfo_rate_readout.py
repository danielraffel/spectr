#!/usr/bin/env python3
"""The LFO Rate readout fits on one line at every rate.

WHAT WAS WRONG. The Rate slider's readout printed `<rate> beats`, and the
readout column is sized for the widest value the other rows produce ("16
beats", "100%"). "0.25 beats" is wider than that column and wrapped onto two
lines, so the row grew by a line as the slider crossed 0.5 and shrank back
above it -- the panel jumped under the pointer.

WHAT THIS DOES. The sub-beat rates read as fractions ("1/4 beat", "1/2
beat"), 1 reads as "1 beat", and every rate at or above 2 keeps "N beats".
Every string is at most as wide as "16 beats", so the column never wraps and
the slider keeps the width the other rows use.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

OLD = r'''fmt: (i) => spectrModulationRates[i] + " beats",'''
NEW = r'''fmt: (i) => spectrFormatModulationRate(spectrModulationRates[i]),'''

# The rates table appears twice; the copy inside the LFO panel component is
# the one followed by its render, and it is the scope the formatter runs in.
HELPER_ANCHOR = r'''const spectrModulationRates = [0.25, 0.5, 1, 2, 4, 8, 16];
  return ['''
HELPER = r'''const spectrModulationRates = [0.25, 0.5, 1, 2, 4, 8, 16];
  // One line at every rate: the readout column fits "16 beats", and a
  // sub-beat rate spelled as a decimal ("0.25 beats") is wider than that and
  // wrapped (tools/patch_materialized_lfo_rate_readout.py).
  const spectrFormatModulationRate = (beats) =>
    beats === 0.25 ? "1/4 beat"
      : beats === 0.5 ? "1/2 beat"
      : beats === 1 ? "1 beat"
      : beats + " beats";
  return ['''


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if raw.count(encode(NEW)) == 1 and raw.count(encode(HELPER)) == 1:
        print("lfo rate readout: already applied")
        return 0
    for label, old in (("formatter", OLD), ("helper anchor", HELPER_ANCHOR)):
        count = raw.count(encode(old))
        if count != 1:
            print(f"lfo rate readout: {label} found {count} times (expected 1)", file=sys.stderr)
            return 1
    raw = raw.replace(encode(HELPER_ANCHOR), encode(HELPER), 1)
    raw = raw.replace(encode(OLD), encode(NEW), 1)
    PATH.write_text(raw, encoding="utf-8")
    print("lfo rate readout: applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
