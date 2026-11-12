#!/usr/bin/env python3
"""The LFO Rate runs from 1/1024 to 4096 beats, in powers of two, and its
readout is one token that cannot wrap.

WHAT CHANGES
  - The rate list the band menu's Rate row and Settings' Rate sliders step
    through is 1/1024 .. 4096 (23 stops, powers of two) instead of 1/4 .. 16.
  - The readout prints the stop with no unit ("1/1024", "1/4", "1", "4096"):
    a single token, so the readout column cannot wrap it, whatever the stop.
    The unit moves to the section head, which reads "LFO n PARAMETERS (RATE
    IN BEATS)".
  - Settings' two Rate sliders step through the same list by index (they
    were linear 0.25 .. 16 in 0.25 steps, which cannot reach either end of
    the new range); their readout is the same formatter.

Idempotent raw-text surgery on the materialized document; re-running is a
no-op once applied.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DOC = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"

def encode(text):
    return json.dumps(text)[1:-1]

RATES = "[1 / 1024, 1 / 512, 1 / 256, 1 / 128, 1 / 64, 1 / 32, 1 / 16, 1 / 8, 1 / 4, 1 / 2, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]"
FORMATTER = """  // One token at every stop: "1/1024" .. "4096", no unit (the section head
  // says beats), so the readout column can never wrap it
  // (tools/patch_materialized_lfo_rate_range.py).
  const spectrFormatModulationRate = (beats) =>
    beats >= 1 ? String(Math.round(beats)) : "1/" + Math.round(1 / beats);
"""

EDITS = [
    # 1. the band menu's own copy of the list
    ("""  const spectrModulationRates = [0.25, 0.5, 1, 2, 4, 8, 16];
  const spectrModulationDepths = [0, 0.25, 0.5, 0.75, 1];
""",
     """  const spectrModulationRates = """ + RATES + """;
  const spectrModulationDepths = [0, 0.25, 0.5, 0.75, 1];
"""),
    # 2. the shared head's list and formatter
    ("""  const spectrModulationRates = [0.25, 0.5, 1, 2, 4, 8, 16];
  // One line at every rate: the readout column fits "16 beats", and a
  // sub-beat rate spelled as a decimal ("0.25 beats") is wider than that and
  // wrapped (tools/patch_materialized_lfo_rate_readout.py).
  const spectrFormatModulationRate = (beats) =>
    beats === 0.25 ? "1/4 beat"
      : beats === 0.5 ? "1/2 beat"
      : beats === 1 ? "1 beat"
      : beats + " beats";
  return [""",
     """  const spectrModulationRates = """ + RATES + """;
""" + FORMATTER + """  return ["""),
    # 3. the unit, once, in the section head
    ("""React.createElement(Divider, { label: "LFO " + modulationSource + " PARAMETERS" }),""",
     """React.createElement(Divider, { label: "LFO " + modulationSource + " PARAMETERS (RATE IN BEATS)" }),"""),
    # 4./5. Settings: the same stops by index, the same readout
    ("""    nested("lfo1-rate", {}, "Rate", /* @__PURE__ */ React.createElement(SpectrSettingsSlider, { gestureId: 4002, value: value.rate, min: 0.25, max: 16, step: 0.25, onChange: (next) => publish("rate", 4002, next), fmt: (next) => next.toFixed(2) }), !value.enabled),""",
     """    nested("lfo1-rate", {}, "Rate", /* @__PURE__ */ React.createElement(SpectrSettingsSlider, { gestureId: 4002, value: spectrSettingsRateIndex(value.rate), min: 0, max: spectrSettingsRates.length - 1, step: 1, onChange: (i) => publish("rate", 4002, spectrSettingsRates[i]), fmt: (i) => spectrSettingsRateText(spectrSettingsRates[i]) }), !value.enabled),"""),
    ("""    nested("lfo2-rate", {}, "Rate", React.createElement(SpectrSettingsSlider, { gestureId: 4012, value: value.lfo2Rate || 4, min: 0.25, max: 16, step: 0.25, onChange: (next) => publish("lfo2Rate", 4012, next), fmt: (next) => next.toFixed(2) }), !value.lfo2Enabled),""",
     """    nested("lfo2-rate", {}, "Rate", React.createElement(SpectrSettingsSlider, { gestureId: 4012, value: spectrSettingsRateIndex(value.lfo2Rate || 4), min: 0, max: spectrSettingsRates.length - 1, step: 1, onChange: (i) => publish("lfo2Rate", 4012, spectrSettingsRates[i]), fmt: (i) => spectrSettingsRateText(spectrSettingsRates[i]) }), !value.lfo2Enabled),"""),
    # 6. the Settings helpers, scoped inside the Settings tab component
    ("""  const rows = [];
  targets.forEach(([t, key, label]) => {
    const on = value["routeOn" + lfo + "_" + t] === true;""",
     """  // The Rate stops and readout Settings shares with the band menu
  // (tools/patch_materialized_lfo_rate_range.py).
  const spectrSettingsRates = """ + RATES + """;
  const spectrSettingsRateText = (beats) =>
    beats >= 1 ? String(Math.round(beats)) : "1/" + Math.round(1 / beats);
  const spectrSettingsRateIndex = (beats) => {
    let index = 0;
    spectrSettingsRates.forEach((rate, i) => {
      if (Math.abs(rate - beats) < Math.abs(spectrSettingsRates[index] - beats)) index = i;
    });
    return index;
  };
  const rows = [];
  targets.forEach(([t, key, label]) => {
    const on = value["routeOn" + lfo + "_" + t] === true;"""),
]

def main():
    raw = DOC.read_text(encoding="utf-8")
    applied = 0
    for old, new in EDITS:
        o, n = encode(old), encode(new)
        if raw.count(n) == 1:
            continue
        if raw.count(o) != 1:
            print(f"lfo rate range: anchor not unique ({raw.count(o)}): {old[:70]!r}", file=sys.stderr)
            return 1
        raw = raw.replace(o, n)
        applied += 1
    if applied:
        DOC.write_text(raw, encoding="utf-8")
        print(f"lfo rate range: applied {applied} edit(s)")
    else:
        print("lfo rate range: already applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
