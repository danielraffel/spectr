#!/usr/bin/env python3
"""Progressive disclosure for target Depth, and a Modulation submenu that fits.

THE BAND MENU'S MODULATION SUBMENU

  Each target is one compact row, "Name ... On/Off". Its Depth row appears
  only while that target is ON -- revealed at once below the row, no
  animation, indented under it, its percentage in the slider's own readout --
  and several enabled targets show their Depth rows together. A target that
  is off has no Depth row at all (it used to keep a dimmed, inert one). The
  list's scroll offset lives in a ref, so switching a target never moves the
  list: the row under the pointer stays put and its Depth row opens beneath.

  The panel is capped at 560 design px (it used to take the whole editor
  height, 736 px, whenever it could), so opened near the top of the window it
  sits beside its entry and the target list scrolls inside it.

SETTINGS > MODULATION

  Grouped under sub-headings -- LFO 1, LFO 2, TARGETS (with the LFO 1 / LFO 2
  selector on the heading line) and OPTIONS -- with one compact row per
  target and, while it is on, an indented Depth row that no longer repeats
  the target's name.

Idempotent like the other patch_materialized_* scripts. Run after
tools/patch_materialized_modulation_level_targets.py and
tools/patch_materialized_menu_wheel_containment.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrProgressiveDepth"


def escaped(value):
    return json.dumps(value, ensure_ascii=False)[1:-1]


EDITS = [
    ('a slider row can be indented under the row it belongs to',
     '  const SliderRow = ({ key, action, label, value, min, max, step, fmt, disabled, onChange, gestureId }) => {\n',
     '  const SliderRow = ({ key, action, label, value, min, max, step, fmt, disabled, onChange, gestureId, indent, hidden }) => {\n'),
    ('the indented slider row is laid out under its switch',
     '      style: {\n'
     '        display: "flex", alignItems: "center", gap: 10, width: "100%",\n'
     '        padding: "6px 12px",\n'
     '        color: disabled ? "rgba(255,255,255,0.25)" : "rgba(255,255,255,0.88)",\n',
     '      "data-spectr-disclosed": hidden ? "0" : "1",\n'
     '      style: {\n'
     '        display: hidden ? "none" : "flex", alignItems: "center", gap: 10, width: "100%",\n'
     '        padding: indent ? "6px 12px 6px 26px" : "6px 12px",\n'
     '        color: disabled ? "rgba(255,255,255,0.25)" : indent ? "rgba(255,255,255,0.7)" : "rgba(255,255,255,0.88)",\n'),
    ('a hidden slider row reaches for no press',
     '        style: { position: "relative", flex: 1, height: 16, cursor: disabled ? "default" : "pointer", hitSlop: "6 6" }\n',
     '        style: { position: "relative", flex: 1, height: 16, cursor: disabled ? "default" : "pointer", hitSlop: hidden ? "0 0" : "6 6" }\n'),
    ('the submenu is capped so its targets scroll inside it',
     '  const modulationPanelMax = Math.max(120, menuMaxHeight - 44);\n',
     '  // Capped (__spectrProgressiveDepth): opened anywhere, it sits beside its\n'
     '  // entry and the target list scrolls inside it, rather than the panel\n'
     '  // taking the whole editor height.\n'
     '  const modulationPanelMax = Math.max(120, Math.min(menuMaxHeight - 44, 560));\n'),
    ('the submenu is placed against its capped height',
     '  const modulationTop = submenuTopFor(modulationH, 760, entryOffsets.modulation);\n',
     '  const modulationTop = Math.max(52, submenuTopFor(\n'
     '    modulationH === null ? null : Math.min(modulationH, modulationPanelMaxFor()),\n'
     '    560, entryOffsets.modulation));\n'),
    ('the cap is known where the placement is computed',
     '  const macrosTop = submenuTopFor(\n',
     '  const modulationPanelMaxFor = () => Math.max(120, Math.min(menuMaxHeight - 44, 560));\n'
     '  const macrosTop = submenuTopFor(\n'),
]

# The row builder is lifted into a named function so the flatMap above can
# filter the absent Depth rows; the original body stays where it was and is
# reached through it.
ROW_FN_OLD = ('  const modulationWheel = (event) => {\n')
ROW_FN_NEW = ('  const spectrModulationTargetRows = (target, key, label) => {\n'
              '    const lfo = modulationSource;\n'
              '    const on = modulation["routeOn" + lfo + "_" + target] === true;\n'
              '    const stored = modulation["routeAmt" + lfo + "_" + target];\n'
              '    const amount = Number.isFinite(stored) ? stored : 0.5;\n'
              '    const onLane = globalThis.spectrRouteLane ? globalThis.spectrRouteLane(lfo, target)\n'
              '      : (target >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + target;\n'
              '    const depthLane = onLane + 10;\n'
              '    const rows = [\n'
              '      Item({\n'
              '        key, action: "modulation-target-" + key, label,\n'
              '        sub: on ? "On" : "Off", toggle: on, checked: on,\n'
              '        disabled: !modulationReady, keepOpen: true,\n'
              '        onClick: () => publishModulation("routeOn" + lfo + "_" + target, onLane, !on)\n'
              '      })];\n'
              '    // Always mounted, hidden while off: the bridge appends a row that\n'
              '    // mounts late instead of placing it, so a conditional row would\n'
              '    // land at the end of the list. Hidden rows are disabled, so the\n'
              '    // keyboard cursor skips them too.\n'
              '    rows.push(SliderRow({\n'
              '      key: key + "-depth", action: "modulation-target-depth-" + key, label: "Depth",\n'
              '      value: amount, min: 0, max: 1, step: 0.01, indent: true, hidden: !on,\n'
              '      fmt: (v) => Math.round(v * 100) + "%",\n'
              '      disabled: !modulationReady || !on,\n'
              '      gestureId: depthLane,\n'
              '      onChange: (v) => publishModulation("routeAmt" + lfo + "_" + target, depthLane,\n'
              '        Math.round(v * 100) / 100)\n'
              '    }));\n'
              '    if (typeof globalThis.spectrModulationExtraRows === "function")\n'
              '      rows.push(...globalThis.spectrModulationExtraRows({ target, key, lfo, Item, on,\n'
              '        modulation, publishModulation, ready: modulationReady }));\n'
              '    return rows;\n'
              '  };\n'
              '  const modulationWheel = (event) => {\n')

SPANS = [
    ('the submenu lists each target once, its Depth only while on',
     '        spectrModulationRouteTargets.flatMap(([target, key, label]) => {\n',
     '                Math.round(v * 100) / 100)\n'
     '            })\n'
     '          ];\n'
     '        })),\n',
     '        // One compact row per target; its Depth row only while it is on\n'
     '        // (__spectrProgressiveDepth).\n'
     '        spectrModulationRouteTargets.flatMap(([target, key, label]) =>\n'
     '          spectrModulationTargetRows(target, key, label))),\n'),
]

SETTINGS_START = ('  const rows = [];\n'
                  '  targets.forEach(([t, key, label]) => {\n'
                  '    const on = value["routeOn" + lfo + "_" + t] === true;\n')
SETTINGS_END = ('    /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Viewport", '
                'hint: "Morph moves the zoom window too" }, /* @__PURE__ */ React.createElement('
                'SpectrSettingsToggle, { value: value.morphViewport !== false, onChange: (next) => '
                'publishMorphViewport(next) })))\n'
                '  );\n'
                '}\n')
SETTINGS_NEW = r'''  // Grouped and progressively disclosed (__spectrProgressiveDepth): LFO 1,
  // LFO 2, TARGETS and OPTIONS under sub-headings; one compact row per
  // target and, only while it is on, an indented Depth row that does not
  // repeat the target's name.
  const subhead = (label, extra) => React.createElement("div", {
    key: "subhead-" + label, "data-spectr-settings-subhead": label,
    style: { display: "flex", alignItems: "center", justifyContent: "space-between",
             marginTop: 6, paddingTop: 10, borderTop: "1px solid rgba(178,200,224,0.14)",
             fontSize: 9, fontWeight: 600, letterSpacing: 1.8, color: "rgba(178,200,224,0.88)" }
  }, React.createElement("span", null, label), extra || null);
  // Always mounted, hidden while closed (display: none): the bridge appends a
  // row that mounts late instead of placing it. 28 tall so a slider's 11 pt
  // press reach stays inside its own row rather than taking the bottom of the
  // switch above (the all-controls first-press sweep).
  const nested = (key, attrs, label, control, hidden) => React.createElement("div", Object.assign({
    key, "data-spectr-disclosed": hidden ? "0" : "1", "data-spectr-nested-label": label,
    style: { display: hidden ? "none" : "flex", alignItems: "center", gap: 14, marginLeft: 12,
                  paddingLeft: 12, borderLeft: "1px solid rgba(120,180,255,0.3)", minHeight: 28 }
  }, attrs), React.createElement("div", { style: { width: 112, flexShrink: 0, fontSize: 10,
                                                   color: "rgba(255,255,255,0.72)", letterSpacing: 0.5 } }, label),
    React.createElement("div", { style: { flex: 1, display: "flex", justifyContent: "flex-end" } }, control));
  const rows = [];
  targets.forEach(([t, key, label]) => {
    const on = value["routeOn" + lfo + "_" + t] === true;
    const stored = value["routeAmt" + lfo + "_" + t];
    const depth = Number.isFinite(stored) ? stored : 0.5;
    rows.push(React.createElement(SpectrSettingsField, { key: key, label }, React.createElement("div", { "data-spectr-settings-target": key, "data-spectr-settings-target-state": on ? "on" : "off" }, React.createElement(SpectrSettingsToggle, { value: on, onChange: (next) => publish("routeOn" + lfo + "_" + t, lane(t), next) }))));
    rows.push(nested(key + "-depth", { "data-spectr-settings-depth-row": key }, "Depth", React.createElement(SpectrSettingsSlider, { target: key, disabled: !on, gestureId: on ? lane(t) + 10 : undefined, value: depth, min: 0, max: 1, step: 0.01, fmt: (v) => Math.round(v * 100) + "%", onChange: (next) => { if (on) publish("routeAmt" + lfo + "_" + t, lane(t) + 10, Math.round(next * 100) / 100); } }), !on));
    if (typeof globalThis.spectrSettingsExtraRows === "function")
      rows.push(...globalThis.spectrSettingsExtraRows({ target: t, key, lfo, value, nested, publish, on }));
  });
  return /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-tabs": true, style: {} },
    React.createElement(SpectrSettingsGroup, { marker: "modulation", title: "MODULATION", subtitle: "Two tempo-synced LFOs layered over host automation. Each LFO sets a movement; each target it drives has its own Depth." },
    subhead("LFO 1"),
    /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "LFO 1", hint: "Enable the first LFO" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: value.enabled, onChange: (next) => publish("enabled", 4000, next) })),
    nested("lfo1-shape", {}, "Shape", /* @__PURE__ */ React.createElement(SpectrSettingsChips, { value: value.shape, onChange: (next) => publish("shape", 4001, next), opts: [[0,"Sin"],[1,"Tri"],[2,"Square"],[3,"Saw"]] }), !value.enabled),
    nested("lfo1-rate", {}, "Rate", /* @__PURE__ */ React.createElement(SpectrSettingsSlider, { gestureId: 4002, value: value.rate, min: 0.25, max: 16, step: 0.25, onChange: (next) => publish("rate", 4002, next), fmt: (next) => next.toFixed(2) }), !value.enabled),
    subhead("LFO 2"),
    React.createElement(SpectrSettingsField, { label: "LFO 2", hint: "Enable the second LFO" }, React.createElement(SpectrSettingsToggle, { value: value.lfo2Enabled || false, onChange: (next) => publish("lfo2Enabled", 4010, next) })),
    nested("lfo2-shape", {}, "Shape", React.createElement(SpectrSettingsChips, { value: value.lfo2Shape || 0, onChange: (next) => publish("lfo2Shape", 4011, next), opts: [[0,"Sin"],[1,"Tri"],[2,"Square"],[3,"Saw"]] }), !value.lfo2Enabled),
    nested("lfo2-rate", {}, "Rate", React.createElement(SpectrSettingsSlider, { gestureId: 4012, value: value.lfo2Rate || 4, min: 0.25, max: 16, step: 0.25, onChange: (next) => publish("lfo2Rate", 4012, next), fmt: (next) => next.toFixed(2) }), !value.lfo2Enabled),
    subhead("TARGETS", React.createElement("div", { "data-spectr-settings-targets-lfo": lfo }, React.createElement(SpectrSettingsChips, { value: lfo, onChange: (next) => setLfo(next), opts: [[1, "LFO 1"], [2, "LFO 2"]] }))),
    React.createElement("div", { key: "targets-hint", style: { fontSize: 9.5, opacity: 0.6, marginTop: -4, fontFamily: "var(--sans)" } }, "What LFO " + lfo + " moves. Switch a target on to set its Depth -- the same list as the band menu's Modulation."),
    ...rows,
    subhead("OPTIONS"),
    React.createElement(SpectrSettingsField, { label: "Ask before overriding modulation", hint: "Touching a control an LFO drives asks whether to turn its target off" }, React.createElement("div", { "data-spectr-ask-override": askOverride ? "on" : "off" }, React.createElement(SpectrSettingsToggle, { value: askOverride, onChange: (next) => { setAskOverride(next); if (typeof window.spectrSetAskBeforeOverride === "function") window.spectrSetAskBeforeOverride(next); else globalThis.__spectrAskBeforeOverride = next; } }))),
    /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Viewport", hint: "Morph moves the zoom window too" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: value.morphViewport !== false, onChange: (next) => publishMorphViewport(next) })))
  );
}
'''


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 3:
        print('already applied  progressive Depth rows and a submenu that fits')
        return 0
    if raw.count(escaped(MARKER)):
        sys.exit('FAIL: the document is half patched by this script')
    edits = EDITS + [('the target rows come from one builder', ROW_FN_OLD, ROW_FN_NEW)]
    for label, old, new in edits:
        if raw.count(escaped(old)) != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, raw.count(escaped(old))))
    for label, old, new in edits:
        raw = raw.replace(escaped(old), escaped(new), 1)
        print('applied         ', label)
    for label, first, last, new in SPANS:
        start, end = escaped(first), escaped(last)
        if raw.count(start) != 1 or raw.count(end) != 1:
            sys.exit('FAIL %s: block markers occur %d / %d times'
                     % (label, raw.count(start), raw.count(end)))
        i = raw.index(start)
        j = raw.index(end, i) + len(end)
        raw = raw[:i] + escaped(new) + raw[j:]
        print('applied         ', label)
    start, end = escaped(SETTINGS_START), escaped(SETTINGS_END)
    if raw.count(start) != 1 or raw.count(end) != 1:
        sys.exit('FAIL Settings > MODULATION: block markers occur %d / %d times'
                 % (raw.count(start), raw.count(end)))
    i = raw.index(start)
    j = raw.index(end) + len(end)
    if j <= i:
        sys.exit('FAIL Settings > MODULATION: markers out of order')
    raw = raw[:i] + escaped(SETTINGS_NEW) + raw[j:]
    print('applied          Settings > MODULATION grouped, Depth rows disclosed')
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
