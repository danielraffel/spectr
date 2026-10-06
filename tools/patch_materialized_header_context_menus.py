#!/usr/bin/env python3
"""Right-click a header control for its own small menu.

LIVE / FROZEN, MIX, INTENSITY, OUTPUT, LENGTH and BANDS each open a context
menu on right-click (a two-finger click on a trackpad), in the band menu's
style:

  * the control's name, then **Reset to <its default>** (100 %, 0.0 dB,
    1 bar, Live, 32 bands);
  * **MODULATION**, scoped to that control's target: LFO 1 and LFO 2, each a
    switch for this target, and while one is on its **Depth** slider right
    under it (progressive disclosure, as in the band menu); under LIVE /
    FROZEN, **Hold for Length** too;
  * **Ask before overriding modulation**.

A target switched on for an LFO that is itself off says so ("On, LFO off")
rather than switching the LFO on behind the user's back.

The menu is one overlay (outside press, Escape and a press on its control
close it; the wheel stays in it) mounted beside the override dialog, which
therefore stacks above it. Opening it hides the header tooltip.

Idempotent like the other patch_materialized_* scripts. Run after
tools/patch_materialized_modulation_structural_targets.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrHeaderContextMenus"


def escaped(value):
    return json.dumps(value, ensure_ascii=False)[1:-1]


def opener(control):
    return ('onContextMenu: (e) => { if (e && typeof e.preventDefault === "function") e.preventDefault(); '
            'spectrOpenControlMenu("' + control + '", e); },\n')


EDITS = [
    ('LIVE / FROZEN opens its menu on right-click',
     '    "data-spectr-freeze-toggle": true,\n',
     '    "data-spectr-freeze-toggle": true,\n    ' + opener('freeze')),
    ('LENGTH opens its menu on right-click',
     '      "data-spectr-length-trigger": true,\n',
     '      "data-spectr-length-trigger": true,\n      ' + opener('length')),
    ('BANDS opens its menu on right-click',
     '"data-spectr-dropdown": "bands", ',
     '"data-spectr-dropdown": "bands", ' + opener('bands').rstrip('\n') + ' '),
    ('the band count can be set from the menu',
     '  const bandsMenu = openMenu === "bands";\n',
     '  const bandsMenu = openMenu === "bands";\n'
     '  // __spectrHeaderContextMenus: BANDS > Reset to 32 bands.\n'
     '  globalThis.spectrSetBandCount = (n) => setSettings((s) => ({ ...s, bandCount: n }));\n'),
    ('each knob opens its menu on right-click, and can be reset from it',
     '      onWheel: onKnobWheel,\n',
     '      onWheel: onKnobWheel,\n'
     '      onContextMenu: (e) => { if (e && typeof e.preventDefault === "function") e.preventDefault();\n'
     '        (globalThis.__spectrKnobReset || (globalThis.__spectrKnobReset = {}))[name] = () => {\n'
     '          gesture(true); write(defaultValue, true); gesture(false); };\n'
     '        spectrOpenControlMenu(name === "output-trim" ? "output" : name, e); },\n'),
    ('the menu is mounted under the override dialog',
     'React.createElement(SpectrModulationOverrideDialog, { settings, setSettings })',
     'React.createElement(SpectrControlMenu, { settings }), '
     'React.createElement(SpectrModulationOverrideDialog, { settings, setSettings })'),
]

COMPONENT_ANCHOR = 'function SpectrModulationOverrideDialog({ settings, setSettings }) {\n'
COMPONENT = r'''// ── Header context menus (__spectrHeaderContextMenus) ──────────────────
const SPECTR_CONTROL_MENUS = {
  freeze: { title: "LIVE / FROZEN", target: 6, reset: "Reset to Live" },
  mix: { title: "MIX", target: 9, reset: "Reset to 100%", knob: "mix" },
  intensity: { title: "INTENSITY", target: 8, reset: "Reset to 100%", knob: "intensity" },
  output: { title: "OUTPUT", target: 10, reset: "Reset to 0.0 dB", knob: "output-trim" },
  length: { title: "LENGTH", target: 7, reset: "Reset to 1 bar" },
  bands: { title: "BANDS", target: 11, reset: "Reset to 32 bands" }
};
function spectrControlMenuStore() {
  return globalThis.__spectrControlMenu
    || (globalThis.__spectrControlMenu = { open: null, listeners: [] });
}
function spectrOpenControlMenu(control, event) {
  if (!SPECTR_CONTROL_MENUS[control]) return;
  if (typeof globalThis.spectrHeaderTipHide === "function") globalThis.spectrHeaderTipHide();
  const node = event && event.currentTarget;
  const box = node && typeof node.getBoundingClientRect === "function" ? node.getBoundingClientRect() : null;
  const store = spectrControlMenuStore();
  store.open = { control,
    x: box ? box.left : Number(event && event.clientX) || 0,
    y: box ? box.bottom + 6 : Number(event && event.clientY) || 0 };
  store.listeners.slice().forEach((fn) => { try { fn(); } catch (error) {} });
}
window.spectrOpenControlMenu = spectrOpenControlMenu;
function spectrCloseControlMenu() {
  const store = spectrControlMenuStore();
  if (!store.open) return;
  store.open = null;
  store.listeners.slice().forEach((fn) => { try { fn(); } catch (error) {} });
}
function spectrResetControl(control) {
  if (control === "freeze") { spectrSetFrozen(false); return; }
  if (control === "bands") {
    if (typeof globalThis.spectrSetBandCount === "function") globalThis.spectrSetBandCount(32);
    return;
  }
  if (control === "length") {
    const store = spectrFreezeStore();
    const presets = Array.isArray(store.lengthPresets) ? store.lengthPresets : [];
    const bar = presets.find((p) => p.bars === 1 && p.label === "1 bar") || presets[16];
    if (bar) spectrCommitFreezeLength(bar.bars, bar.fraction);
    return;
  }
  const knob = SPECTR_CONTROL_MENUS[control] && SPECTR_CONTROL_MENUS[control].knob;
  const reset = knob && globalThis.__spectrKnobReset && globalThis.__spectrKnobReset[knob];
  if (typeof reset === "function") reset();
}
function SpectrControlMenu({ settings }) {
  const store = spectrControlMenuStore();
  const [, setRevision] = React.useState(0);
  React.useEffect(() => {
    const sync = () => setRevision((n) => n + 1);
    store.listeners.push(sync);
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  const open = store.open;
  const { value: modulation, ready, publish } = window.useSpectrModulationState(!!open);
  if (!open) return null;
  const spec = SPECTR_CONTROL_MENUS[open.control];
  if (!spec) return null;
  const W = 250;
  const left = Math.max(8, Math.min(open.x, 1320 - W - 8));
  const top = Math.max(52, Math.min(open.y, 860 - 64 - 320));
  const row = (props, ...children) => React.createElement("button", Object.assign({
    type: "button",
    style: { display: "flex", alignItems: "center", gap: 10, width: "100%", padding: "6px 12px",
             background: "transparent", border: "none", color: "rgba(255,255,255,0.88)",
             cursor: "pointer", fontFamily: "var(--mono)", fontSize: 10.5, letterSpacing: 0.3,
             textAlign: "left" },
    onMouseEnter: (e) => { e.currentTarget.style.background = "rgba(120,180,255,0.14)"; },
    onMouseLeave: (e) => { e.currentTarget.style.background = "transparent"; }
  }, props), ...children);
  const heading = (label, rule) => React.createElement("div", {
    key: "h-" + label, "data-spectr-menu-section": "true",
    style: { fontSize: 8.5, fontWeight: 600, letterSpacing: 2, color: "rgba(178,200,224,0.85)",
             background: "rgba(120,160,200,0.08)", padding: "8px 12px 4px", textTransform: "uppercase",
             borderTop: rule ? "1px solid rgba(178,200,224,0.18)" : "none" }
  }, label);
  const sw = (on) => React.createElement("span", {
    "data-spectr-menu-switch": on ? "on" : "off",
    style: { width: 28, height: 16, borderRadius: 8, flexShrink: 0, position: "relative",
             background: on ? "hsl(200,80%,50%)" : "rgba(255,255,255,0.18)" }
  }, React.createElement("span", { style: { position: "absolute", top: 2, left: on ? 14 : 2, width: 12,
    height: 12, borderRadius: 6, background: "#fff" } }));
  const items = [heading(spec.title, false),
    row({ key: "reset", "data-spectr-control-action": "reset",
          onClick: () => { spectrResetControl(open.control); spectrCloseControlMenu(); } },
        React.createElement("span", { style: { flex: 1 } }, spec.reset)),
    heading("MODULATION", true)];
  [1, 2].forEach((lfo) => {
    const t = spec.target;
    const on = modulation["routeOn" + lfo + "_" + t] === true;
    const lfoOn = lfo === 1 ? modulation.enabled === true : modulation.lfo2Enabled === true;
    const lane = globalThis.spectrRouteLane(lfo, t);
    const stored = modulation["routeAmt" + lfo + "_" + t];
    const depth = Number.isFinite(stored) ? stored : 0.5;
    items.push(row({ key: "lfo" + lfo, "data-spectr-control-action": "lfo" + lfo,
                     role: "menuitemcheckbox", "aria-checked": on, disabled: !ready,
                     onClick: () => publish("routeOn" + lfo + "_" + t, lane, !on) },
      React.createElement("span", { style: { flex: 1 } }, "LFO " + lfo),
      React.createElement("span", { style: { opacity: 0.45, fontSize: 9.5 } },
        on ? (lfoOn ? "On" : "On, LFO off") : "Off"),
      sw(on)));
    if (on) items.push(React.createElement("div", {
      key: "depth" + lfo, "data-spectr-control-action": "depth" + lfo,
      style: { display: "flex", alignItems: "center", gap: 10, padding: "2px 12px 8px 26px",
               fontFamily: "var(--mono)", fontSize: 10.5, color: "rgba(255,255,255,0.7)" }
    }, React.createElement("span", { style: { width: 40, flexShrink: 0 } }, "Depth"),
      React.createElement(SpectrSettingsSlider, { target: open.control + "-" + lfo,
        gestureId: lane + 10, value: depth, min: 0, max: 1, step: 0.01,
        fmt: (v) => Math.round(v * 100) + "%",
        onChange: (next) => publish("routeAmt" + lfo + "_" + t, lane + 10, Math.round(next * 100) / 100) })));
  });
  if (open.control === "freeze") {
    const hold = modulation.holdForLength === true;
    items.push(row({ key: "hold", "data-spectr-control-action": "hold-for-length",
                     role: "menuitemcheckbox", "aria-checked": hold, disabled: !ready,
                     onClick: () => publish("holdForLength", 4140, !hold) },
      React.createElement("span", { style: { flex: 1 } }, "Hold for Length"), sw(hold)));
  }
  const ask = settings ? settings.askBeforeOverride !== false : true;
  items.push(heading("OPTIONS", true));
  items.push(row({ key: "ask", "data-spectr-control-action": "ask-before-override",
                   role: "menuitemcheckbox", "aria-checked": ask,
                   onClick: () => { if (typeof window.spectrSetAskBeforeOverride === "function") window.spectrSetAskBeforeOverride(!ask); } },
    React.createElement("span", { style: { flex: 1 } }, "Ask before overriding modulation"), sw(ask)));
  return React.createElement("div", {
    "data-spectr-control-menu": open.control, "data-spectr-overlay": "true",
    "data-pulp-popup-default": "off", role: "menu", "aria-label": spec.title + " actions",
    overlay: true, onDismiss: spectrCloseControlMenu,
    onWheel: (e) => { if (e && e.preventDefault) e.preventDefault(); if (e && e.stopPropagation) e.stopPropagation(); },
    style: { position: "fixed", left, top, width: W, display: "flex", flexDirection: "column",
             background: "rgba(12,16,22,0.97)", border: "1px solid rgba(255,255,255,0.12)",
             borderRadius: 5, padding: "6px 0", boxShadow: "0 14px 40px rgba(0,0,0,0.6)",
             backdropFilter: "blur(12px)", zIndex: 2147483001, pointerEvents: "auto" }
  }, ...items);
}
'''


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 2:
        print('already applied  header controls have context menus')
        return 0
    if raw.count(escaped(MARKER)):
        sys.exit('FAIL: the document is half patched by this script')
    edits = EDITS + [('the menu', COMPONENT_ANCHOR, COMPONENT + COMPONENT_ANCHOR)]
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
