#!/usr/bin/env python3
"""A header control's context menu names, and can open, its own target.

The header menus (tools/patch_materialized_header_context_menus.py) offered
"LFO 1" and "LFO 2" switches under MODULATION. Each one already toggled THIS
control's target for that LFO, but nothing on screen said so: the rows read
as the whole LFO, and the target list -- the thing the switch actually moves
-- was nowhere in the menu. This makes the target the subject of the row:

  * under MODULATION, one row per LFO reads "LFO 1 -> Freeze": a dot for
    whether that LFO is running (violet, Spectr's modulation colour, or dim),
    the route's own state ("On", "Off", or "LFO off" when the route is on
    but its LFO is not), and the route's switch. While the route is on its
    Depth row sits under it (the same progressive disclosure as the band
    menu), and a route whose LFO is off offers "Turn on LFO n" rather than
    switching the LFO on behind the user's back;
  * "All targets..." opens the band menu's full Modulation submenu in place
    of the header menu, on the LFO that drives this control, with the target
    list scrolled to this control's row, that row marked with the preset
    list's selection accent (left rule + tint + bright label) and the
    keyboard cursor on it. "< LIVE / FROZEN" at its top returns to the
    header menu; Escape and Left close it like any submenu;
  * MORPH (the A-B slider) and PRESET gain the same menu, so every control
    that maps to an LFO target has one. PRESET has no reset row. Snapshot A
    and B keep their existing right-click (clear a filled slot).

The full submenu is the band menu's own panel, not a copy: the band menu
mounts in a "focus" mode whose root panel is parked off-screen and whose
Modulation submenu opens at the header menu's position as soon as the band
menu has mounted (a submenu whose overlay claim lands before its parent's is
swept off the stack by it). Closing that submenu, by any route, closes the
whole menu.

The header menu also measures itself now, so it fits under its control and
opens ABOVE a control near the bottom edge (MORPH, PRESET).

Idempotent like the other patch_materialized_* scripts. Run after
tools/patch_materialized_header_context_menus.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrControlMenuTargets"


def escaped(value):
    return json.dumps(value, ensure_ascii=False)[1:-1]


def control_opener(control):
    return ('onContextMenu: (e) => { if (e && typeof e.preventDefault === "function") e.preventDefault(); '
            'spectrOpenControlMenu("' + control + '", e); }')


EDITS = [
    # ── The header menu ────────────────────────────────────────────────────
    ('MORPH and PRESET have menus too',
     '  bands: { title: "BANDS", target: 11, reset: "Reset to 32 bands" }\n};\n',
     '  bands: { title: "BANDS", target: 11, reset: "Reset to 32 bands" },\n'
     '  // __spectrControlMenuTargets: the A-B slider and the preset button.\n'
     '  morph: { title: "MORPH", target: 3, reset: "Reset to A" },\n'
     '  preset: { title: "PRESET", target: 12 }\n};\n'),
    ('MORPH resets to A',
     '  if (control === "freeze") { spectrSetFrozen(false); return; }\n',
     '  if (control === "freeze") { spectrSetFrozen(false); return; }\n'
     '  if (control === "morph") {\n'
     '    if (typeof globalThis.__spectrMorphReset === "function") globalThis.__spectrMorphReset();\n'
     '    return;\n'
     '  }\n'),
    ('the opener remembers the room above its control',
     '    y: box ? box.bottom + 6 : Number(event && event.clientY) || 0 };\n',
     '    y: box ? box.bottom + 6 : Number(event && event.clientY) || 0,\n'
     '    above: box ? box.top - 6 : Number(event && event.clientY) || 0 };\n'),
    ('All targets opens the full Modulation submenu on this target',
     'function SpectrControlMenu({ settings }) {\n',
     '// "All targets...": the band menu\'s Modulation submenu, opened in place of\n'
     '// this menu on the LFO that drives the control (LFO 2 only when it alone\n'
     '// does), scrolled to and marking the control\'s own target.\n'
     'function spectrOpenControlTargets(open, spec, entry, modulation, node) {\n'
     '  const opener = globalThis.spectrOpenModulationFocus;\n'
     '  if (typeof opener !== "function") return false;\n'
     '  const box = node && typeof node.getBoundingClientRect === "function" ? node.getBoundingClientRect() : null;\n'
     '  const target = entry ? entry[0] : null;\n'
     '  const lfo = target !== null && modulation && modulation["routeOn1_" + target] !== true\n'
     '    && modulation["routeOn2_" + target] === true ? 2 : 1;\n'
     '  const reopen = { control: open.control, x: open.x, y: open.y, above: open.above };\n'
     '  spectrCloseControlMenu();\n'
     '  opener({ target, key: entry ? entry[1] : "", lfo, title: spec.title,\n'
     '    left: box ? box.left : open.x, top: box ? box.top : open.y,\n'
     '    back: () => {\n'
     '      const store = spectrControlMenuStore();\n'
     '      store.open = reopen;\n'
     '      store.listeners.slice().forEach((fn) => { try { fn(); } catch (error) {} });\n'
     '    } });\n'
     '  return true;\n'
     '}\n'
     'function SpectrControlMenu({ settings }) {\n'),
    ('the menu measures itself',
     '  const { value: modulation, ready, publish } = window.useSpectrModulationState(!!open);\n'
     '  if (!open) return null;\n',
     '  const { value: modulation, ready, publish } = window.useSpectrModulationState(!!open);\n'
     '  const menuRef = React.useRef(null);\n'
     '  const [menuH, setMenuH] = React.useState(null);\n'
     '  React.useLayoutEffect(() => {\n'
     '    const node = menuRef.current;\n'
     '    const h = node ? node.offsetHeight || node.clientHeight || 0 : 0;\n'
     '    if (Number.isFinite(h) && h > 0 && h !== menuH) setMenuH(h);\n'
     '  });\n'
     '  if (!open) return null;\n'),
    ('the menu opens below its control, or above it near the bottom',
     '  const top = Math.max(52, Math.min(open.y, 860 - 64 - 320));\n',
     '  const H = menuH === null ? 360 : menuH;\n'
     '  const below = open.y + H <= 860 - 8;\n'
     '  const wanted = below || !Number.isFinite(open.above) ? open.y : open.above - H;\n'
     '  const top = Math.max(52, Math.min(wanted, 860 - 8 - H));\n'),
    ('a control without a default has no reset row',
     '  const items = [heading(spec.title, false),\n'
     '    row({ key: "reset", "data-spectr-control-action": "reset",\n'
     '          onClick: () => { spectrResetControl(open.control); spectrCloseControlMenu(); } },\n'
     '        React.createElement("span", { style: { flex: 1 } }, spec.reset)),\n'
     '    heading("MODULATION", true)];\n',
     '  const items = [heading(spec.title, false)];\n'
     '  if (spec.reset) items.push(\n'
     '    row({ key: "reset", "data-spectr-control-action": "reset",\n'
     '          onClick: () => { spectrResetControl(open.control); spectrCloseControlMenu(); } },\n'
     '        React.createElement("span", { style: { flex: 1 } }, spec.reset)));\n'
     '  items.push(heading("MODULATION", true));\n'
     '  // __spectrControlMenuTargets: the control\'s own entry in the one\n'
     '  // target list the band menu and Settings show.\n'
     '  const routeEntry = Number.isFinite(spec.target)\n'
     '    ? spectrModulationRouteList().find(([target]) => target === spec.target) || null : null;\n'
     '  const targetKey = routeEntry ? routeEntry[1] : "";\n'
     '  const targetLabel = routeEntry ? routeEntry[2] : "";\n'),
    ('each LFO row is this control\'s target for that LFO',
     '  [1, 2].forEach((lfo) => {\n'
     '    const t = spec.target;\n'
     '    const on = modulation["routeOn" + lfo + "_" + t] === true;\n'
     '    const lfoOn = lfo === 1 ? modulation.enabled === true : modulation.lfo2Enabled === true;\n'
     '    const lane = globalThis.spectrRouteLane(lfo, t);\n'
     '    const stored = modulation["routeAmt" + lfo + "_" + t];\n'
     '    const depth = Number.isFinite(stored) ? stored : 0.5;\n'
     '    items.push(row({ key: "lfo" + lfo, "data-spectr-control-action": "lfo" + lfo,\n'
     '                     role: "menuitemcheckbox", "aria-checked": on, disabled: !ready,\n'
     '                     onClick: () => publish("routeOn" + lfo + "_" + t, lane, !on) },\n'
     '      React.createElement("span", { style: { flex: 1 } }, "LFO " + lfo),\n'
     '      React.createElement("span", { style: { opacity: 0.45, fontSize: 9.5 } },\n'
     '        on ? (lfoOn ? "On" : "On, LFO off") : "Off"),\n'
     '      sw(on)));\n',
     '  if (routeEntry) [1, 2].forEach((lfo) => {\n'
     '    const t = spec.target;\n'
     '    const on = modulation["routeOn" + lfo + "_" + t] === true;\n'
     '    const lfoOn = lfo === 1 ? modulation.enabled === true : modulation.lfo2Enabled === true;\n'
     '    const lane = globalThis.spectrRouteLane(lfo, t);\n'
     '    const stored = modulation["routeAmt" + lfo + "_" + t];\n'
     '    const depth = Number.isFinite(stored) ? stored : 0.5;\n'
     '    items.push(row({ key: "lfo" + lfo, "data-spectr-control-action": "lfo" + lfo,\n'
     '                     "data-spectr-control-target": targetKey,\n'
     '                     "data-spectr-control-route": on ? "on" : "off",\n'
     '                     "aria-label": "LFO " + lfo + " to " + targetLabel,\n'
     '                     role: "menuitemcheckbox", "aria-checked": on, disabled: !ready,\n'
     '                     onClick: () => publish("routeOn" + lfo + "_" + t, lane, !on) },\n'
     '      React.createElement("span", { "data-spectr-control-lfo-state": lfoOn ? "on" : "off",\n'
     '        style: { width: 6, height: 6, borderRadius: 3, flexShrink: 0,\n'
     '                 background: lfoOn ? "rgb(205,180,255)" : "rgba(255,255,255,0.22)" } }),\n'
     '      React.createElement("span", { style: { flex: 1, color: on ? "hsl(200,90%,82%)" : "rgba(255,255,255,0.88)" } },\n'
     '        "LFO " + lfo + " \\u2192 " + targetLabel),\n'
     '      React.createElement("span", { style: { opacity: on && !lfoOn ? 0.85 : 0.45, fontSize: 9.5,\n'
     '        color: on && !lfoOn ? "rgb(255,205,150)" : "rgba(255,255,255,1)" } },\n'
     '        on ? (lfoOn ? "On" : "LFO off") : "Off"),\n'
     '      sw(on)));\n'
     '    if (on && !lfoOn) items.push(row({ key: "power" + lfo,\n'
     '                     "data-spectr-control-action": "lfo" + lfo + "-power", disabled: !ready,\n'
     '                     onClick: () => publish(lfo === 1 ? "enabled" : "lfo2Enabled", lfo === 1 ? 4000 : 4010, true) },\n'
     '      React.createElement("span", { style: { flex: 1, paddingLeft: 16, color: "rgba(255,255,255,0.7)" } },\n'
     '        "Turn on LFO " + lfo)));\n'),
    ('All targets... closes the MODULATION section',
     '      React.createElement("span", { style: { flex: 1 } }, "Hold for Length"), sw(hold)));\n'
     '  }\n',
     '      React.createElement("span", { style: { flex: 1 } }, "Hold for Length"), sw(hold)));\n'
     '  }\n'
     '  items.push(row({ key: "all-targets", "data-spectr-control-action": "all-targets",\n'
     '                   "aria-haspopup": "menu", disabled: !ready,\n'
     '                   onClick: () => spectrOpenControlTargets(open, spec, routeEntry, modulation, menuRef.current) },\n'
     '    React.createElement("span", { style: { flex: 1 } }, "All targets\\u2026"),\n'
     '    React.createElement("span", { style: { opacity: 0.45, fontSize: 9.5 } }, "\\u203A")));\n'),
    ('the menu carries its target and its measured box',
     '    "data-spectr-control-menu": open.control, "data-spectr-overlay": "true",\n',
     '    ref: menuRef, "data-spectr-control-menu": open.control, "data-spectr-overlay": "true",\n'
     '    "data-spectr-control-menu-target": targetKey,\n'),

    # ── MORPH and PRESET open it ──────────────────────────────────────────
    ('MORPH opens its menu on right-click',
     '      "data-spectr-morph-state": hasBoth ? "enabled" : "disabled",\n',
     '      "data-spectr-morph-state": hasBoth ? "enabled" : "disabled",\n'
     '      ' + control_opener('morph') + ',\n'),
    ('MORPH can be reset from it',
     '  const ratio = Math.max(0, Math.min(1, v));\n  const grown = hovered && hasBoth;\n',
     '  const ratio = Math.max(0, Math.min(1, v));\n  const grown = hovered && hasBoth;\n'
     '  // __spectrControlMenuTargets: MORPH > Reset to A.\n'
     '  globalThis.__spectrMorphReset = () => {\n'
     '    publishedRef.current = 0;\n'
     '    setV(0);\n'
     '    if (bankRef.current) bankRef.current.setMorph(0, false);\n'
     '  };\n'),
    ('a rail button forwards its right-click',
     'function RailBtn({ children, onClick, active, popupKind, railAction, dropdown }) {',
     'function RailBtn({ children, onClick, active, popupKind, railAction, dropdown, onContextMenu }) {'),
    ('a rail button forwards its right-click (prop)',
     '      "aria-expanded": popupKind ? active : void 0,\n      onClick: handle,',
     '      "aria-expanded": popupKind ? active : void 0,\n      onContextMenu,\n      onClick: handle,'),
    ('PRESET opens its menu on right-click',
     'React.createElement(RailBtn, { popupKind: "menu", dropdown: "pattern", onClick:',
     'React.createElement(RailBtn, { popupKind: "menu", dropdown: "pattern", '
     + control_opener('preset') + ', onClick:'),

    # ── The band menu's Modulation submenu, opened on one target ─────────
    ('the filter surface can open the band menu on a target',
     '  const [ctxMenu, setCtxMenu] = useState(null);\n',
     '  const [ctxMenu, setCtxMenu] = useState(null);\n'
     '  // __spectrControlMenuTargets: a header menu\'s "All targets...".\n'
     '  globalThis.spectrOpenModulationFocus = (focus) => setCtxMenu(\n'
     '    { x: focus.left, y: focus.top, band: -1, focus });\n'),
    ('the focus reaches the menu',
     '      StableContextMenu,\n      {\n        x: ctxMenu.x,',
     '      StableContextMenu,\n      {\n'
     '        key: ctxMenu.focus ? "modulation-focus" : "band",\n'
     '        modulationFocus: ctxMenu.focus || null,\n'
     '        x: ctxMenu.x,'),
    ('a new focus re-renders the menu',
     'previous.macros === next.macros);',
     'previous.macros === next.macros && previous.modulationFocus === next.modulationFocus);'),
    ('the menu takes a focus',
     'onMuteSel, onFitView, canUndo, canRedo, macros, onUndo, onRedo, onMacroMembers }) {\n',
     'onMuteSel, onFitView, canUndo, canRedo, macros, onUndo, onRedo, onMacroMembers, modulationFocus }) {\n'),
    ('on the LFO that drives the target',
     '  const [modulationSource, setModulationSource] = React.useState(1);\n',
     '  const [modulationSource, setModulationSource] = React.useState(\n'
     '    modulationFocus && modulationFocus.lfo === 2 ? 2 : 1);\n'),
    ('the keyboard cursor keeps the focused row marked',
     '        if (menuRow) row.style.background = on ? cursorFill : "transparent";\n',
     '        const focusRow = row.getAttribute\n'
     '          && row.getAttribute("data-spectr-modulation-focus") === "true";\n'
     '        if (menuRow) row.style.background = focusRow\n'
     '          ? (on ? "rgba(120,180,255,0.32)" : spectrFocusFill)\n'
     '          : on ? cursorFill : "transparent";\n'),
    ('a row can carry the focus accent',
     '  const Item = ({ key, action, label, hint, onClick, onKeyDown, onHover, disabled, danger, sub, keepOpen, checked, expanded, hasPopup, toggle }) =>',
     '  // __spectrControlMenuTargets: the preset list\'s selection accent.\n'
     '  const spectrFocusFill = "rgba(120,180,255,0.2)";\n'
     '  const Item = ({ key, action, label, hint, onClick, onKeyDown, onHover, disabled, danger, sub, keepOpen, checked, expanded, hasPopup, toggle, accent }) =>'),
    ('the accent is addressable',
     '      "data-spectr-band-action": action,\n      onKeyDown,\n',
     '      "data-spectr-band-action": action,\n'
     '      "data-spectr-modulation-focus": accent ? "true" : void 0,\n'
     '      onKeyDown,\n'),
    ('the accent paints',
     '        padding: "6px 12px",\n        background: "transparent",\n        border: "none",\n'
     '        color: disabled ? "rgba(255,255,255,0.25)" : danger ? "rgba(255,180,190,0.9)" : "rgba(255,255,255,0.88)",',
     '        padding: accent ? "6px 12px 6px 10px" : "6px 12px",\n'
     '        background: accent ? spectrFocusFill : "transparent",\n        border: "none",\n'
     '        borderLeft: accent ? "2px solid hsl(200,85%,65%)" : void 0,\n'
     '        color: disabled ? "rgba(255,255,255,0.25)" : accent ? "hsl(200,90%,82%)" : danger ? "rgba(255,180,190,0.9)" : "rgba(255,255,255,0.88)",'),
    ('the pointer leaving keeps the accent',
     '        e.currentTarget.style.background = "transparent";\n'
     '        // The pointer left the row, so the cursor is no longer on it; the',
     '        e.currentTarget.style.background = accent ? spectrFocusFill : "transparent";\n'
     '        // The pointer left the row, so the cursor is no longer on it; the'),
    ('the focused target row carries the accent',
     '        sub: on ? "On" : "Off", toggle: on, checked: on,\n'
     '        disabled: !modulationReady, keepOpen: true,\n',
     '        sub: on ? "On" : "Off", toggle: on, checked: on,\n'
     '        accent: !!modulationFocus && modulationFocus.target === target,\n'
     '        disabled: !modulationReady, keepOpen: true,\n'),
    ('a focused submenu sits where the header menu was',
     '    560, entryOffsets.modulation));\n',
     '    560, entryOffsets.modulation));\n'
     '  const modulationFocusTop = modulationFocus\n'
     '    ? Math.max(52, Math.min(modulationFocus.top, menuBottom\n'
     '        - Math.min(modulationH === null ? 560 : modulationH, modulationPanelMaxFor()) - 8))\n'
     '    : null;\n'),
    ('a focused submenu sits where the header menu was (style)',
     '          left: submenuOnLeft ? Math.max(8, left - W - 6) : Math.min(vw - W - 8, left + W + 6),\n'
     '          top: modulationTop, width: W,',
     '          left: modulationFocus ? Math.max(8, modulationFocus.left)\n'
     '            : submenuOnLeft ? Math.max(8, left - W - 6) : Math.min(vw - W - 8, left + W + 6),\n'
     '          top: modulationFocus ? modulationFocusTop : modulationTop, width: W,'),
    ('Back returns to the header menu',
     '        Item({ action: "modulation-back", label: "\u2039 Back", keepOpen: true, onClick: () => closeSubmenuLevel("modulation") }),',
     '        Item({ action: "modulation-back", label: modulationFocus ? "\u2039 " + modulationFocus.title : "\u2039 Back", keepOpen: true,\n'
     '          onClick: () => {\n'
     '            if (!modulationFocus) { closeSubmenuLevel("modulation"); return; }\n'
     '            onClose();\n'
     '            if (typeof modulationFocus.back === "function") modulationFocus.back();\n'
     '          } }),'),
    ('the focused row is scrolled to, and closing the submenu closes the menu',
     '  // Each opening starts at the top.\n',
     '  // __spectrControlMenuTargets: once the list is measured, the focused\n'
     '  // target sits a third of the way down the viewport with the keyboard\n'
     '  // cursor on it. A focused menu IS its submenu: closing that (Escape,\n'
     '  // Left, an outside press) closes the menu.\n'
     '  const modulationFocusDoneRef = React.useRef(false);\n'
     '  React.useLayoutEffect(() => {\n'
     '    if (!modulationFocus || modulationFocusDoneRef.current || !modulationOpen) return;\n'
     '    if (modulationMeasure.rows === null || modulationMeasure.head === null) return;\n'
     '    const rows = modulationRowsRef.current;\n'
     '    const node = document.querySelector(\'[data-spectr-modulation-focus="true"]\');\n'
     '    if (!rows || !node || typeof node.getBoundingClientRect !== "function"\n'
     '        || typeof rows.getBoundingClientRect !== "function") return;\n'
     '    const r = node.getBoundingClientRect(), box = rows.getBoundingClientRect();\n'
     '    if (!r || !box || !(r.height > 0)) return;\n'
     '    modulationFocusDoneRef.current = true;\n'
     '    const viewportH = modulationGeomRef.current.viewportH;\n'
     '    modulationScrollTo((r.top - box.top) - Math.max(0, (viewportH - r.height) / 3));\n'
     '    const at = levelRows("modulation").indexOf(node);\n'
     '    if (at >= 0) cursorRef.current = { level: "modulation", index: at };\n'
     '    setModulationRevealTick((n) => n + 1);\n'
     '  });\n'
     '  // Opened after the band menu has mounted, as a hover would open it: a\n'
     '  // submenu that claims its overlay before its parent menu does is\n'
     '  // swept off the stack by the parent\'s claim.\n'
     '  const modulationFocusOpenedRef = React.useRef(false);\n'
     '  React.useEffect(() => {\n'
     '    if (modulationFocus) openModulation(true);\n'
     '  }, []);\n'
     '  React.useEffect(() => {\n'
     '    if (!modulationFocus) return;\n'
     '    if (modulationOpen) modulationFocusOpenedRef.current = true;\n'
     '    else if (modulationFocusOpenedRef.current) onClose();\n'
     '  }, [modulationOpen]);\n'
     '  // Each opening starts at the top.\n'),
    ('a focused menu parks its root off-screen',
     '      "data-spectr-band-context-menu": "true",\n',
     '      "data-spectr-band-context-menu": "true",\n'
     '      "data-spectr-modulation-focus-target": modulationFocus ? String(modulationFocus.key || "") : void 0,\n'),
    ('a focused menu parks its root off-screen (style)',
     '        position: "fixed",\n        left,\n        top,\n        width: W,\n        background: "rgba(12,16,22,0.97)",',
     '        position: "fixed",\n        left: modulationFocus ? -4000 : left,\n        top,\n        width: W,\n'
     '        background: "rgba(12,16,22,0.97)",'),
]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 2:
        print('already applied  header menus show and open their own targets')
        return 0
    if raw.count(escaped(MARKER)):
        sys.exit('FAIL: the document is half patched by this script')
    for label, old, new in EDITS:
        count = raw.count(escaped(old))
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1' % (label, count))
    for label, old, new in EDITS:
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
