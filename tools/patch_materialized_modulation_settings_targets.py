#!/usr/bin/env python3
"""Settings > MODULATION shows the same per-LFO targets as the band menu.

The old rows were both-LFO controls from before per-LFO routing: "Target"
(the legacy 4004 lane) and "Destinations" (a both-LFO mask). They are
replaced by the band menu's own list -- spectrModulationRouteList(), so the
same targets in the same order -- with a switch row and a Depth row per
target, for the LFO the "LFO targets" chips select. Both surfaces read and
write the same state through useSpectrModulationState: every edit is a
`param_edit` on the target's own lane (a recordable host gesture; Depth drags
are one bracket via `gestureId`), and the processor's live projection carries
it back to whichever surface is open. The LFO-level Depth rows go, as they did
from the band menu: each target carries its own depth.

The rows are mounted once and never conditionally: the native bridge appends
a late-mounted widget rather than placing it (see
test/test_materialized_modulation_grouping.mjs), so switching between LFO 1 and
LFO 2 changes the rows' VALUES, never which rows exist. A Depth row whose
target is off is dimmed and ignores input, as in the band menu.

The group also gains "Ask before overriding modulation" (default on), the
Setting the override dialog reads.

Replaces the whole SpectrModulationSettings function. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
START = "function SpectrModulationSettings() {"
END = "\nfunction SpectrLatencySettings() {"
MARKER = '"data-spectr-settings-targets-lfo"'

NEW = r'''function SpectrModulationSettings() {
  const listening = !arguments[0] || arguments[0].listening !== false;
  const { value, publish, publishMorphViewport } = useSpectrModulationState(listening);
  // Which LFO's targets the rows below show. Display state only.
  const [lfo, setLfo] = React.useState(1);
  const [askOverride, setAskOverride] = React.useState(globalThis.__spectrAskBeforeOverride !== false);
  const targets = spectrModulationRouteList();
  const lane = (t) => (lfo - 1) * 20 + t;
  const rows = [];
  targets.forEach(([t, key, label]) => {
    const on = value["routeOn" + lfo + "_" + t] === true;
    const stored = value["routeAmt" + lfo + "_" + t];
    const depth = Number.isFinite(stored) ? stored : 0.5;
    rows.push(React.createElement(SpectrSettingsField, { key: key, label }, React.createElement("div", { "data-spectr-settings-target": key, "data-spectr-settings-target-state": on ? "on" : "off" }, React.createElement(SpectrSettingsToggle, { value: on, onChange: (next) => publish("routeOn" + lfo + "_" + t, 4020 + lane(t), next) }))));
    rows.push(React.createElement(SpectrSettingsField, { key: key + "-depth", label: "Depth", hint: label }, React.createElement("div", { "data-spectr-settings-target-depth": key, "aria-disabled": on ? "false" : "true", style: { opacity: on ? 1 : 0.35, pointerEvents: on ? "auto" : "none" } }, React.createElement(SpectrSettingsSlider, { gestureId: on ? 4030 + lane(t) : undefined, value: depth, min: 0, max: 1, step: 0.01, fmt: (v) => Math.round(v * 100) + "%", onChange: (next) => { if (on) publish("routeAmt" + lfo + "_" + t, 4030 + lane(t), Math.round(next * 100) / 100); } }))));
  });
  return /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-tabs": true, style: {} },
    React.createElement(SpectrSettingsGroup, { marker: "modulation", title: "MODULATION", subtitle: "Tempo-synced movement layered over host automation. Each LFO sets the movement; each target has its own depth." },
    /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "LFO", hint: "Enable internal modulation" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: value.enabled, onChange: (next) => publish("enabled", 4000, next) })),
    /* @__PURE__ */ React.createElement(SpectrSettingsField, { hidden: !value.enabled, label: "Shape", hint: "Oscillator waveform" }, /* @__PURE__ */ React.createElement(SpectrSettingsChips, { value: value.shape, onChange: (next) => publish("shape", 4001, next), opts: [[0,"Sin"],[1,"Tri"],[2,"Square"],[3,"Saw"]] })),
    /* @__PURE__ */ React.createElement(SpectrSettingsField, { hidden: !value.enabled, label: "Rate", hint: "Beats per cycle" }, /* @__PURE__ */ React.createElement(SpectrSettingsSlider, { gestureId: 4002, value: value.rate, min: 0.25, max: 16, step: 0.25, onChange: (next) => publish("rate", 4002, next), fmt: (next) => next.toFixed(2) })),
    React.createElement(SpectrSettingsField, { label: "LFO 2", hint: "Enable second modulation source" }, React.createElement(SpectrSettingsToggle, { value: value.lfo2Enabled || false, onChange: (next) => publish("lfo2Enabled", 4010, next) })),
    React.createElement(SpectrSettingsField, { hidden: !value.lfo2Enabled, label: "LFO 2 shape", hint: "Second waveform" }, React.createElement(SpectrSettingsChips, { value: value.lfo2Shape || 0, onChange: (next) => publish("lfo2Shape", 4011, next), opts: [[0,"Sin"],[1,"Tri"],[2,"Square"],[3,"Saw"]] })),
    React.createElement(SpectrSettingsField, { hidden: !value.lfo2Enabled, label: "LFO 2 rate", hint: "Beats per cycle" }, React.createElement(SpectrSettingsSlider, { gestureId: 4012, value: value.lfo2Rate || 4, min: 0.25, max: 16, step: 0.25, onChange: (next) => publish("lfo2Rate", 4012, next), fmt: (next) => next.toFixed(2) })),
    React.createElement(SpectrSettingsField, { label: "LFO targets", hint: "The band menu's Modulation targets" }, React.createElement("div", { "data-spectr-settings-targets-lfo": lfo }, React.createElement(SpectrSettingsChips, { value: lfo, onChange: (next) => setLfo(next), opts: [[1, "LFO 1"], [2, "LFO 2"]] }))),
    ...rows,
    React.createElement(SpectrSettingsField, { label: "Ask before overriding modulation", hint: "Touching a control an LFO drives asks whether to turn its target off" }, React.createElement("div", { "data-spectr-ask-override": askOverride ? "on" : "off" }, React.createElement(SpectrSettingsToggle, { value: askOverride, onChange: (next) => { setAskOverride(next); if (typeof window.spectrSetAskBeforeOverride === "function") window.spectrSetAskBeforeOverride(next); else globalThis.__spectrAskBeforeOverride = next; } }))),
    /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Viewport", hint: "Morph moves the zoom window too" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: value.morphViewport !== false, onChange: (next) => publishMorphViewport(next) })))
  );
}'''


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("settings targets already applied")
        return 0
    start = raw.find(encode(START))
    if start < 0 or raw.count(encode(START)) != 1:
        sys.exit("FAIL: SpectrModulationSettings anchor")
    end = raw.find(encode(END), start)
    if end < 0:
        sys.exit("FAIL: SpectrLatencySettings anchor")
    raw = raw[:start] + encode(NEW) + raw[end:]
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("settings targets applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
