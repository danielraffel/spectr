#!/usr/bin/env python3
"""Bands and Preset targets, Hold for Length, and dropdowns that follow an LFO.

TARGETS

  * The target list gains **Bands** (ModulationTarget 11) and **Preset** (12),
    after Length, in the band menu's Modulation submenu and in Settings >
    MODULATION alike. Their lanes are the third block, 4100 / 4110 (+20 for
    LFO 2): `globalThis.spectrRouteLane(lfo, target)` is now the one place
    the editor computes a target's lane (Settings, the submenu and the
    override dialog's "Turn off" all use it).
  * Freeze gains **Hold for Length** (lane 4140), shown under the Freeze
    target wherever its Depth is: on, each engage the Freeze target makes
    latches for exactly the effective Length, then releases. Like the Depth
    rows it is always mounted and hidden while the target is off (a row that
    mounts late is appended by the bridge, not placed).

DROPDOWNS THAT FOLLOW THE LFO (the violet cue of a modulated knob)

  * LENGTH, while an LFO drives Length, shows the length the current freeze
    took (frozen) or the next engage would take (live), in violet; its menu
    still shows and edits your own LENGTH.
  * BANDS, while an LFO drives Bands, shows the band count playing.
  * The preset label, while an LFO drives Preset, shows the preset it is
    nearest.
  All three are read from the processor's `freeze_display` message, which is
  published only when one of them changes, so they cost nothing per frame.

PRESET NEIGHBOURHOOD

  The editor resolves the presets around the current one (menu order:
  factory, then user; four each way) at the current band count -- exactly
  what applying each would write, in dB (a band a preset floors goes at
  -24 dB) -- and sends their names and gains to the processor
  (`preset_modulation_set`) whenever the current preset, the band count or
  the user library changes.

ASKING

  Picking a band count or a preset while an LFO drives that target asks the
  override question, as LENGTH and the knobs do.

Idempotent like the other patch_materialized_* scripts. Run after
tools/patch_materialized_modulation_progressive_depth.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrStructuralTargets"

INK = "rgb(205,180,255)"
RIM = "rgba(190,150,255,0.6)"


def escaped(value):
    return json.dumps(value, ensure_ascii=False)[1:-1]


EDITS = [
    ('one place computes a target\'s host lane',
     '  globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};\n',
     '  globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};\n'
     '  // A target\'s on/off lane (__spectrStructuralTargets); Depth is +10.\n'
     '  // Targets 0..7 at 4020, the level targets 8..10 at 4060, Bands and\n'
     '  // Preset (11, 12) at 4100; +20 for LFO 2.\n'
     '  globalThis.spectrRouteLane = (lfo, t) => t >= 11\n'
     '    ? 4100 + (lfo - 1) * 20 + (t - 11)\n'
     '    : (t >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + t;\n'),
    ('the target list gains Bands and Preset',
     '    output: [10, "Output"], a: [1, "Snapshot A"], b: [2, "Snapshot B"]};\n'
     '  const order = ["bank", "band-shift", "band-spread", "intensity", "mix", "morph",\n'
     '    "freeze", "length", "output", "a", "b"];\n',
     '    output: [10, "Output"], a: [1, "Snapshot A"], b: [2, "Snapshot B"],\n'
     '    bands: [11, "Bands"], preset: [12, "Preset"]};\n'
     '  const order = ["bank", "band-shift", "band-spread", "intensity", "mix", "morph",\n'
     '    "freeze", "length", "bands", "preset", "output", "a", "b"];\n'),
    ('every target\'s routing is read from a frame',
     '      for (let t = 0; t < 11; t++) {\n',
     '      for (let t = 0; t < 13; t++) {\n'),
    ('Hold for Length is read from a frame',
     '  spectrReadModulationRoutes(modulation, next);\n',
     '  spectrReadModulationRoutes(modulation, next);\n'
     '  if (typeof modulation.freeze_hold_for_length === "boolean")\n'
     '    next.holdForLength = modulation.freeze_hold_for_length;\n'),
    ('Settings writes each target\'s own lane',
     '  const lane = (t) => (t >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + t;  // level targets: own block\n',
     '  // The shared helper, inline fallback for a sandbox that lifts this alone.\n'
     '  const lane = (t) => globalThis.spectrRouteLane ? globalThis.spectrRouteLane(lfo, t)\n'
     '    : t >= 11 ? 4100 + (lfo - 1) * 20 + (t - 11) : (t >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + t;\n'),
    ('"Turn off" writes the target\'s own lane',
     '            { id: (r.target >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + r.target, value: 0 },\n',
     '            { id: globalThis.spectrRouteLane ? globalThis.spectrRouteLane(lfo, r.target)\n'
     '                : r.target >= 11 ? 4100 + (lfo - 1) * 20 + (r.target - 11)\n'
     '                : (r.target >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + r.target, value: 0 },\n'),
    ('freeze_display carries the modulated LENGTH, BANDS and preset',
     '      store.lengthLfos = Array.isArray(p.length_lfos) ? p.length_lfos.map(Number) : [];\n'
     '      if (typeof store.paint === "function") store.paint();\n',
     '      store.lengthLfos = Array.isArray(p.length_lfos) ? p.length_lfos.map(Number) : [];\n'
     '      // What the closed dropdowns show while an LFO moves them\n'
     '      // (__spectrStructuralTargets).\n'
     '      const shown = {\n'
     '        lengthIndex: Number.isFinite(Number(p.length_index)) ? Number(p.length_index) : -1,\n'
     '        bands: Number(p.bands) || 0,\n'
     '        presetDriven: p.preset_driven === true,\n'
     '        presetName: typeof p.preset_name === "string" ? p.preset_name : ""\n'
     '      };\n'
     '      // An unset display is the unmodulated one, so the first publish of\n'
     '      // an unmodulated session notifies nobody: a notify re-renders the\n'
     '      // toolbar, and the editor-open gate allows no commit after mount.\n'
     '      const was = store.display\n'
     '        || { lengthIndex: -1, bands: 0, presetDriven: false, presetName: "" };\n'
     '      store.display = shown;\n'
     '      if (typeof store.paint === "function") store.paint();\n'
     '      if (was.lengthIndex !== shown.lengthIndex || was.bands !== shown.bands\n'
     '          || was.presetDriven !== shown.presetDriven || was.presetName !== shown.presetName)\n'
     '        spectrFreezeNotify(store);\n'),
    ('LENGTH shows the length the freeze uses',
     '  const label = editorOpen ? "Custom" : length ? length.label : "";\n',
     '  // While an LFO drives Length, the closed control shows the length the\n'
     '  // current freeze took (frozen) or the next one takes (live); the menu\n'
     '  // keeps showing and editing the user\'s own LENGTH.\n'
     '  const shownLength = store.display && store.display.lengthIndex >= 0\n'
     '    && presets[store.display.lengthIndex] ? presets[store.display.lengthIndex] : null;\n'
     '  const lengthModulated = !!shownLength && !editorOpen;\n'
     '  const label = editorOpen ? "Custom" : shownLength ? shownLength.label\n'
     '    : length ? length.label : "";\n'),
    ('a modulated LENGTH reads violet',
     '      style: { fontSize, letterSpacing: 0, whiteSpace: "nowrap", flexShrink: 0,\n'
     '               color: capped ? "hsl(35,90%,75%)" : "rgba(255,255,255,0.88)" }\n',
     '      "data-spectr-length-modulated": lengthModulated ? "1" : "",\n'
     '      style: { fontSize, letterSpacing: 0, whiteSpace: "nowrap", flexShrink: 0,\n'
     '               color: capped ? "hsl(35,90%,75%)" : lengthModulated ? "' + INK + '" : "rgba(255,255,255,0.88)" }\n'),
    ('a modulated LENGTH has a violet rim',
     '        border: "1px solid " + (menuOpen || editorOpen ? "rgba(255,255,255,0.18)" : "rgba(255,255,255,0.08)"),\n',
     '        border: "1px solid " + (menuOpen || editorOpen ? "rgba(255,255,255,0.18)"\n'
     '          : lengthModulated ? "' + RIM + '" : "rgba(255,255,255,0.08)"),\n'),
    ('BANDS shows the band count playing',
     '/* @__PURE__ */ React.createElement("span", { className: "tnum", style: { lineHeight: 1, whiteSpace: "nowrap" } }, settings.bandCount + " BANDS \\u25BE")',
     'React.createElement(SpectrBandsLabel, { count: settings.bandCount })'),
    ('a band count picked while an LFO drives Bands asks first',
     '      onClick: () => {\n'
     '        setSettings((s) => ({ ...s, bandCount: n }));\n',
     '      onClick: () => spectrOverrideModulated("Bands", 11, spectrLfosDriving(11), () => {\n'
     '        setSettings((s) => ({ ...s, bandCount: n }));\n'),
    ('...and closes the same way',
     '          window.parent.postMessage({ type: "__edit_mode_set_keys", edits: { bandCount: n } }, "*");\n'
     '        } catch {\n'
     '        }\n'
     '        setBandsMenu(false);\n'
     '      },\n',
     '          window.parent.postMessage({ type: "__edit_mode_set_keys", edits: { bandCount: n } }, "*");\n'
     '        } catch {\n'
     '        }\n'
     '        setBandsMenu(false);\n'
     '      }),\n'),
    ('the preset label shows the preset the LFO is nearest',
     'selectedPatternName.length > 6 ? selectedPatternName.slice(0, 6) + "… \\u25BE" : selectedPatternName + " \\u25BE")))',
     'spectrPresetLabelText(selectedPatternName, spectrPresetShown))))'),
    ('...in violet, read where the toolbar is drawn',
     '  const [helpOpen, setHelpOpen] = useStateChrome(false);\n',
     '  // The Preset target\'s nearest preset (__spectrStructuralTargets): a\n'
     '  // freeze_display change re-renders the toolbar, which is rare.\n'
     '  const spectrPresetShown = useSpectrModulatedDisplay();\n'
     '  const spectrPresetDriven = spectrPresetShown.presetDriven === true && !!spectrPresetShown.presetName;\n'
     '  const [helpOpen, setHelpOpen] = useStateChrome(false);\n'),
    ('...with the label span carrying the cue',
     '"data-spectr-selected-preset": true, title: selectedPatternName, style: { marginLeft: 6,',
     '"data-spectr-selected-preset": true, "data-spectr-preset-modulated": spectrPresetDriven ? "1" : "", '
     '"data-spectr-preset-shown": spectrPresetDriven ? spectrPresetShown.presetName : selectedPatternName, '
     'title: selectedPatternName, style: { color: spectrPresetDriven ? "' + INK + '" : undefined, marginLeft: 6,'),
    ('a preset picked while an LFO drives Preset asks first, and moves the centre',
     '  const applyPattern = useAppC((p) => {\n'
     '    const b = bankRef.current;\n'
     '    if (!b) return;\n'
     '    const gains = window.Spectr.resolveGains(p, b.N);\n'
     '    b.setGains(gains);\n'
     '    setSelectedPatternId(p.id);\n',
     '  const applyPattern = useAppC((p) => spectrOverrideModulated("Preset", 12, spectrLfosDriving(12), () => {\n'
     '    const b = bankRef.current;\n'
     '    if (!b) return;\n'
     '    const gains = window.Spectr.resolveGains(p, b.N);\n'
     '    b.setGains(gains);\n'
     '    setSelectedPatternId(p.id);\n'),
    ('...closing the wrapped apply',
     '    fireStatus(`APPLIED "${p.name}"`);\n'
     '  }, [fireStatus]);\n',
     '    fireStatus(`APPLIED "${p.name}"`);\n'
     '  }), [fireStatus]);\n'
     '  // The Preset target\'s neighbourhood follows the current preset, the\n'
     '  // band count and the library (__spectrStructuralTargets).\n'
     '  useAppE(() => {\n'
     '    if (selectedPatternId) spectrSendPresetNeighbourhood(selectedPatternId,\n'
     '      settings.bandCount, userPatterns);\n'
     '  }, [selectedPatternId, settings.bandCount, userPatterns]);\n'),
]

HELPERS_ANCHOR = '// The LFOs (1, 2) driving `target` in a normalized modulation frame: on, and\n'
HELPERS = r'''// ── __spectrStructuralTargets ──────────────────────────────────────────
// Freeze's Hold for Length, under the Freeze target wherever its Depth is.
// Mounted always and hidden with the target (rows that mount late are
// appended, not placed); disabled while hidden so the keyboard skips it.
globalThis.spectrModulationExtraRows = ({ key, Item, on, modulation, publishModulation, ready }) => {
  if (key !== "freeze") return [];
  const hold = modulation && modulation.holdForLength === true;
  return [React.createElement("div", { key: "freeze-hold-row", "data-spectr-disclosed": on ? "1" : "0",
      style: { display: on ? "flex" : "none", flexDirection: "column", paddingLeft: 14 } },
    Item({
      key: "freeze-hold", action: "modulation-freeze-hold", label: "Hold for Length",
      sub: hold ? "On" : "Off", toggle: hold, checked: hold, disabled: !ready || !on, keepOpen: true,
      onClick: () => publishModulation("holdForLength", 4140, !hold)
    }))];
};
globalThis.spectrSettingsExtraRows = ({ key, value, nested, publish, on }) => {
  if (key !== "freeze") return [];
  const hold = value && value.holdForLength === true;
  return [nested("freeze-hold", { "data-spectr-settings-freeze-hold": hold ? "on" : "off" },
    "Hold for Length", React.createElement(SpectrSettingsToggle, {
      value: hold, onChange: (next) => publish("holdForLength", 4140, next) }), !on)];
};
// What the closed dropdowns show while an LFO moves them; re-renders only
// when the processor's freeze_display says one of them moved.
function useSpectrModulatedDisplay() {
  const store = spectrFreezeStore();
  const [, setRevision] = React.useState(0);
  React.useEffect(() => {
    const sync = () => setRevision((n) => n + 1);
    store.listeners.push(sync);
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  return store.display || {};
}
// "32 BANDS", or the count the Bands target plays, in violet.
function SpectrBandsLabel({ count }) {
  const shown = useSpectrModulatedDisplay();
  const driven = shown.bands > 0;
  // The toolbar's own style, written as it was (a contract marker in
  // test_import_fidelity.cpp keys on this literal).
  const base = { style: { lineHeight: 1, whiteSpace: "nowrap" } };
  return React.createElement("span", {
    className: "tnum", "data-spectr-bands-shown": String(driven ? shown.bands : count),
    "data-spectr-bands-modulated": driven ? "1" : "",
    style: driven ? Object.assign({}, base.style, { color: "INK" }) : base.style
  }, (driven ? shown.bands : count) + " BANDS ▾");
}
// The preset label's text: the preset the Preset target is nearest while it
// plays one, else the chosen preset. Plain text, so the toolbar's optical
// centering measures it exactly as before.
function spectrPresetLabelText(name, shown) {
  const driven = shown && shown.presetDriven === true && !!shown.presetName;
  const text = driven ? shown.presetName : name;
  return text.length > 6 ? text.slice(0, 6) + "… ▾" : text + " ▾";
}
// The presets around `id` in menu order (factory, then user), four each way,
// resolved at `bandCount` -- what applying each would write -- sent to the
// processor for the Preset target.
function spectrSendPresetNeighbourhood(id, bandCount, userPatterns) {
  const factory = (window.Spectr && window.Spectr.FACTORY_PATTERNS) || [];
  const list = [...factory, ...(userPatterns || [])];
  const at = list.findIndex((p) => p && p.id === id);
  if (at < 0 || !window.pulp || typeof window.pulp.postMessage !== "function") return;
  const reach = 4, names = [], gains = [];
  for (let k = -reach; k <= reach; k++) {
    const p = list[at + k];
    names.push(p ? String(p.name || "") : "");
    let row = [];
    if (p && k !== 0) {
      // Editor gains are in 24 dB units; a band a preset floors (-Infinity)
      // is sent at the bottom of the range.
      try { row = Array.from(window.Spectr.resolveGains(p, bandCount) || []).map((g) => {
        const v = Number(g);
        return Number.isFinite(v) ? Math.max(-24, Math.min(24, v * 24)) : (v < 0 ? -24 : 0);
      }); }
      catch (error) { row = []; }
    }
    gains.push(row.slice(0, 64));
  }
  Promise.resolve(window.pulp.postMessage("preset_modulation_set", {
    centre: id, below: Math.min(reach, at), above: Math.min(reach, list.length - 1 - at),
    names, gains }, "spectr-preset-modulation")).catch((error) =>
      console.error("[Spectr] preset neighbourhood failed", error));
}
'''.replace('"INK"', '"' + INK + '"')


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 3:
        print('already applied  Bands and Preset targets, Hold for Length')
        return 0
    if raw.count(escaped(MARKER)):
        sys.exit('FAIL: the document is half patched by this script')
    edits = EDITS + [('the hooks and labels', HELPERS_ANCHOR, HELPERS + HELPERS_ANCHOR)]
    for label, old, new in edits:
        if raw.count(escaped(old)) != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, raw.count(escaped(old))))
    for label, old, new in edits:
        raw = raw.replace(escaped(old), escaped(new), 1)
        print('applied         ', label)
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
