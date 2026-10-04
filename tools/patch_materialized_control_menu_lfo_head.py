#!/usr/bin/env python3
"""Header context menus carry the Modulation submenu's head, and nest each
route's own settings under it.

What a header menu (LIVE / FROZEN, LENGTH, MIX, INTENSITY, OUTPUT, BANDS,
MORPH, PRESET) shows after this:

  TITLE            Reset to <default>
  MODULATION       LFO 1 [switch]  LFO 2 [switch]  [EDIT LFO 1][EDIT LFO 2]
  LFO n PARAMETERS Shape (four glyphs + name), Rate (slider, beats)
  <TARGET> TARGET  LFO 1 -> <Target> [route switch]   ("LFO off" when the
                     | Depth                            route is on and its
                     | Hold for Length (Freeze only)    LFO is not)
                   LFO 2 -> <Target> [route switch]
                     | ...
                   All targets...                     (the full list)

"Ask before overriding modulation" is not in these menus: it is a Setting
(Settings > MODULATION, and the override dialog's "Don't ask again").

The MODULATION / PARAMETERS block is the band menu's Modulation submenu head,
not a copy: both menus build it with `spectrModulationHeadRows` out of the
same row kit, `spectrMenuKit` (Item, ShapeRow, SliderRow, Divider), which
this script lifts out of ContextMenu unchanged except that the attribute a
row's action is written to is a parameter (`data-spectr-band-action` in the
band menu, `data-spectr-control-action` in the header menus). The EDIT tab
opens on the LFO that drives the control (LFO 2 only when it alone does).

A route's children sit under it behind the same 1 px guide line Settings
draws, and are disclosed by the same rule: mounted always, shown while the
route is on (rows that mount late are appended, not placed). Hold for
Length is one setting, so under both LFOs' Freeze routes it is the same
switch.

The header menus get the band menu's keyboard model: Up/Down/Home/End move
a cursor over the enabled rows in paint order (the EDIT tabs included),
Left/Right step a slider row (Shape, Rate, Depth), Return presses the row.
Escape stays the overlay's.

Idempotent. Run after tools/patch_materialized_control_menu_targets.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrModulationMenuKit"


def escaped(value):
    return json.dumps(value, ensure_ascii=False)[1:-1]


KIT_START = "  // __spectrControlMenuTargets: the preset list's selection accent.\n"
KIT_END = "    const macrosPanel = macrosOpen && React.createElement(\"div\", {\n"
KIT_CALL = (
    "  // __spectrModulationMenuKit: the row kit, shared with the header menus.\n"
    "  const { Item, ShapeRow, SliderRow, Divider, spectrFocusFill } = spectrMenuKit({\n"
    "    onClose, cursorFill, cursorRef, levelRows, syncHover, levelOf, hoverIntent });\n")

HEAD_START = "        React.createElement(Divider, { label: \"MODULATION\" }),\n"
HEAD_END = "        React.createElement(Divider, { label: \"LFO \" + modulationSource + \" TARGETS\" })),\n"
HEAD_CALL = (
    "        ...spectrModulationHeadRows({ kit: { Item, ShapeRow, SliderRow, Divider },\n"
    "          modulation, ready: modulationReady, publish: publishModulation,\n"
    "          source: modulationSource, setSource: setModulationSource }),\n")

SHARED_ANCHOR = "window.ContextMenu = ContextMenu;\n"

MENU_START = "function SpectrControlMenu({ settings }) {\n"
MENU_END = "function SpectrModulationOverrideDialog({ settings, setSettings }) {\n"

NEW_MENU = r'''function SpectrControlMenu({ settings }) {
  // __spectrModulationMenuKit: the band menu's Modulation head and row kit,
  // then only this control's own target, its route settings nested under
  // each LFO's route.
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
  const menuRef = React.useRef(null);
  const [menuH, setMenuH] = React.useState(null);
  React.useLayoutEffect(() => {
    const node = menuRef.current;
    const h = node ? node.offsetHeight || node.clientHeight || 0 : 0;
    if (Number.isFinite(h) && h > 0 && h !== menuH) setMenuH(h);
  });
  // The keyboard cursor: one enabled row, in paint order, painted with the
  // hover fill (the band menu's model, on one level).
  const cursorFill = "rgba(120,180,255,0.14)";
  const cursorRef = React.useRef({ level: "root", index: -1 });
  const levelRows = () => {
    const panel = menuRef.current;
    if (!panel || typeof document === "undefined" || !document.querySelectorAll) return [];
    const stops = new Set([...document.querySelectorAll("button"),
                           ...document.querySelectorAll('[role="slider"]')]);
    const rows = [];
    const walk = (node) => {
      if (!node) return;
      if (stops.has(node)) {
        if (!(node.getAttribute && node.getAttribute("aria-disabled") === "true")) rows.push(node);
        return;
      }
      const kids = node.children || node.childNodes || [];
      for (let i = 0; i < kids.length; ++i) walk(kids[i]);
    };
    walk(panel);
    return rows;
  };
  const paintCursor = () => {
    const cursor = cursorRef.current;
    levelRows().forEach((row, i) => {
      const on = i === cursor.index;
      const role = row.getAttribute ? String(row.getAttribute("role") || "") : "";
      if (role === "menuitem" || role === "menuitemcheckbox" || role === "slider")
        row.style.background = on ? cursorFill : "transparent";
      else {
        const pressed = row.getAttribute && row.getAttribute("aria-pressed") === "true";
        row.style.borderColor = on ? "rgba(170,215,255,0.95)"
          : pressed ? "rgba(180,210,255,0.4)" : "rgba(255,255,255,0.1)";
      }
    });
  };
  const syncHover = (node) => {
    const index = levelRows().indexOf(node);
    if (index >= 0) { cursorRef.current = { level: "root", index }; paintCursor(); }
    return "root";
  };
  const kit = spectrMenuKit({ onClose: spectrCloseControlMenu, cursorFill, cursorRef,
    levelRows, syncHover, levelOf: () => "root", hoverIntent: () => {},
    actionAttr: "data-spectr-control-action" });
  const { Item, SliderRow, Divider } = kit;
  // The EDIT tab, per opening: the LFO that drives this control, else LFO 1.
  const [picked, setPicked] = React.useState(null);
  React.useEffect(() => { cursorRef.current = { level: "root", index: -1 }; }, [open]);
  React.useEffect(() => { paintCursor(); });
  React.useEffect(() => {
    if (!open) return undefined;
    const onKey = (event) => {
      if (!event || event.metaKey || event.ctrlKey || event.altKey) return;
      const key = event.key;
      const rows = levelRows();
      const n = rows.length;
      const index = cursorRef.current.index < n ? cursorRef.current.index : -1;
      const row = index >= 0 ? rows[index] : null;
      const consume = () => {
        if (typeof event.preventDefault === "function") event.preventDefault();
        if (typeof event.stopPropagation === "function") event.stopPropagation();
      };
      if (key === "ArrowDown" || key === "ArrowUp" || key === "Home" || key === "End") {
        consume();
        if (!n) return;
        const next = key === "Home" ? 0 : key === "End" ? n - 1
          : index < 0 ? (key === "ArrowDown" ? 0 : n - 1)
          : key === "ArrowDown" ? (index + 1) % n : (index - 1 + n) % n;
        cursorRef.current = { level: "root", index: next };
        paintCursor();
        return;
      }
      if ((key === "ArrowRight" || key === "ArrowLeft") && row
          && typeof row.__spectrSliderStep === "function") {
        consume();
        row.__spectrSliderStep(key === "ArrowRight" ? 1 : -1);
        return;
      }
      if (key === "Enter" && row && typeof row.click === "function") {
        consume();
        row.click();
      }
    };
    const doc = typeof document !== "undefined" ? document : null;
    if (doc && typeof doc.addEventListener === "function") doc.addEventListener("keydown", onKey, true);
    return () => {
      if (doc && typeof doc.removeEventListener === "function") doc.removeEventListener("keydown", onKey, true);
    };
  }, [open]);
  if (!open) return null;
  const spec = SPECTR_CONTROL_MENUS[open.control];
  if (!spec) return null;
  const W = 250;
  const left = Math.max(8, Math.min(open.x, 1320 - W - 8));
  const H = menuH === null ? 520 : menuH;
  const below = open.y + H <= 860 - 8;
  const wanted = below || !Number.isFinite(open.above) ? open.y : open.above - H;
  const top = Math.max(52, Math.min(wanted, 860 - 8 - H));
  // The control's own entry in the one target list the band menu and
  // Settings show.
  const routeEntry = Number.isFinite(spec.target)
    ? spectrModulationRouteList().find(([target]) => target === spec.target) || null : null;
  const targetKey = routeEntry ? routeEntry[1] : "";
  const targetLabel = routeEntry ? routeEntry[2] : "";
  const routeOn = (lfo) => !!routeEntry && modulation["routeOn" + lfo + "_" + spec.target] === true;
  const driving = routeOn(1) ? 1 : routeOn(2) ? 2 : 1;
  const source = picked && picked.open === open ? picked.source : driving;
  const items = [React.createElement(Divider, { key: "title", label: spec.title, rule: false })];
  if (spec.reset) items.push(Item({ key: "reset", action: "reset", label: spec.reset,
    onClick: () => spectrResetControl(open.control) }));
  items.push(...spectrModulationHeadRows({ kit, modulation, ready, publish, source,
    setSource: (next) => setPicked({ open, source: next }) }));
  if (routeEntry) {
    items.push(React.createElement(Divider, { key: "target", label: targetLabel + " target" }));
    [1, 2].forEach((lfo) => {
      const t = spec.target;
      const on = routeOn(lfo);
      const lfoOn = lfo === 1 ? modulation.enabled === true : modulation.lfo2Enabled === true;
      const lane = globalThis.spectrRouteLane(lfo, t);
      const stored = modulation["routeAmt" + lfo + "_" + t];
      const depth = Number.isFinite(stored) ? stored : 0.5;
      items.push(React.createElement("div", { key: "route" + lfo,
          "data-spectr-control-target": targetKey, "data-spectr-control-route": on ? "on" : "off",
          "data-spectr-control-route-lfo": lfo, "data-spectr-control-lfo-state": lfoOn ? "on" : "off",
          style: { display: "flex", flexDirection: "column" } },
        Item({ action: "lfo" + lfo, label: "LFO " + lfo + " → " + targetLabel,
          sub: on ? (lfoOn ? "On" : "LFO off") : "Off",
          subTone: on && !lfoOn ? "rgb(255,205,150)" : void 0,
          toggle: on, checked: on, disabled: !ready, keepOpen: true,
          onClick: () => publish("routeOn" + lfo + "_" + t, lane, !on) }),
        // The route's own settings, nested as Settings nests them: mounted
        // always, shown while the route is on.
        React.createElement("div", { "data-spectr-control-route-children": lfo,
            "data-spectr-disclosed": on ? "1" : "0",
            style: { display: on ? "flex" : "none", flexDirection: "column", marginLeft: 20,
                     borderLeft: "1px solid rgba(120,180,255,0.3)" } },
          SliderRow({ action: "depth" + lfo, label: "Depth", value: depth, min: 0, max: 1, step: 0.01,
            fmt: (v) => Math.round(v * 100) + "%", disabled: !ready || !on, gestureId: lane + 10,
            onChange: (next) => publish("routeAmt" + lfo + "_" + t, lane + 10, Math.round(next * 100) / 100) }),
          open.control === "freeze" && Item({
            action: lfo === 1 ? "hold-for-length" : "hold-for-length-2", label: "Hold for Length",
            sub: modulation.holdForLength === true ? "On" : "Off",
            toggle: modulation.holdForLength === true, checked: modulation.holdForLength === true,
            disabled: !ready || !on, keepOpen: true,
            onClick: () => publish("holdForLength", 4140, modulation.holdForLength !== true) }))));
    });
  }
  items.push(Item({ key: "all-targets", action: "all-targets", label: "All targets…", sub: "›",
    hasPopup: "menu", disabled: !ready, keepOpen: true,
    onClick: () => spectrOpenControlTargets(open, spec, routeEntry, modulation, menuRef.current) }));
  // No OPTIONS section: "Ask before overriding modulation" lives in
  // Settings > MODULATION and in the override dialog's "Don't ask again".
  return React.createElement("div", {
    ref: menuRef, "data-spectr-control-menu": open.control, "data-spectr-overlay": "true",
    "data-spectr-control-menu-target": targetKey,
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

HEAD_FN_PRE = (
    "// __spectrModulationMenuKit: the Modulation submenu's head -- the two LFO\n"
    "// switches, the EDIT LFO tabs and the edited LFO's Shape and Rate -- as\n"
    "// rows, for the band menu's submenu and every header context menu.\n"
    "function spectrModulationHeadRows({ kit, modulation, ready, publish, source, setSource }) {\n"
    "  const { Item, ShapeRow, SliderRow, Divider } = kit;\n"
    "  const modulationReady = ready, publishModulation = publish;\n"
    "  const modulationSource = source, setModulationSource = setSource;\n"
    "  const spectrModulationShapes = [\"Sin\", \"Tri\", \"Square\", \"Saw\"];\n"
    "  const spectrModulationRates = [0.25, 0.5, 1, 2, 4, 8, 16];\n"
    "  return [\n")


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 2:
        print('already applied  header menus carry the Modulation head')
        return 0
    if raw.count(escaped(MARKER)):
        sys.exit('FAIL: the document is half patched by this script')
    document = json.loads(raw)
    html = document['html']
    for label, anchor in [('kit start', KIT_START), ('kit end', KIT_END), ('head start', HEAD_START),
                          ('head end', HEAD_END), ('shared anchor', SHARED_ANCHOR),
                          ('menu start', MENU_START), ('menu end', MENU_END)]:
        if html.count(anchor) != 1:
            sys.exit('FAIL %s: occurs %d times, expected 1' % (label, html.count(anchor)))
        if raw.count(escaped(anchor)) != 1:
            sys.exit('FAIL %s: not addressable in the escaped document' % label)

    # 1. The row kit, out of ContextMenu.
    a = html.index(KIT_START)
    b = html.index(KIT_END, a)
    kit_body = html[a:b]
    if kit_body.count('"data-spectr-band-action": action,') != 3:
        sys.exit('FAIL: the kit does not carry its three action attributes')
    kit_body = kit_body.replace('"data-spectr-band-action": action,', '[actionAttr]: action,')
    kit_body = kit_body.replace(
        'checked, expanded, hasPopup, toggle, accent }) =>',
        'checked, expanded, hasPopup, toggle, accent, subTone }) =>', 1)
    sub_old = 'sub && /* @__PURE__ */ React.createElement("span", { style: { opacity: 0.45, fontSize: 9.5 } }, sub),'
    if kit_body.count(sub_old) != 1:
        sys.exit('FAIL: the Item sub span is not where expected')
    kit_body = kit_body.replace(sub_old,
        'sub && /* @__PURE__ */ React.createElement("span", { style: subTone '
        '? { opacity: 0.9, fontSize: 9.5, color: subTone } : { opacity: 0.45, fontSize: 9.5 } }, sub),')
    kit_fn = ("// __spectrModulationMenuKit: the band menu's rows, as a kit any menu can\n"
              "// build with. `actionAttr` is the attribute a row's action is written to.\n"
              "function spectrMenuKit({ onClose, cursorFill, cursorRef, levelRows, syncHover, levelOf,\n"
              "                         hoverIntent, actionAttr = \"data-spectr-band-action\" }) {\n"
              + kit_body
              + "  return { Item, ShapeRow, SliderRow, Divider, spectrFocusFill };\n}\n")

    # 2. The Modulation head, out of the submenu.
    c = html.index(HEAD_START)
    d = html.index(HEAD_END, c)
    head_body = html[c:d].rstrip()
    if not head_body.endswith(','):
        sys.exit('FAIL: the head does not end in a row separator')
    head_fn = HEAD_FN_PRE + head_body[:-1] + "\n  ];\n}\n"

    out = html[:a] + KIT_CALL + html[b:c] + HEAD_CALL + html[d:]
    out = out.replace(SHARED_ANCHOR, kit_fn + head_fn + SHARED_ANCHOR, 1)
    # 3. The header menu.
    m = out.index(MENU_START)
    n = out.index(MENU_END, m)
    if '__spectrControlMenuTargets' not in out[m - 1200:n]:
        sys.exit('FAIL: the header menu is not the one this script expects')
    out = out[:m] + NEW_MENU + out[n:]
    # 4. The "Turn on" row's replacement is the LFO switch; nothing else names it.
    document['html'] = out
    # Write as a replacement of the escaped payload so the rest of the file
    # keeps its bytes.
    old_payload = '"html":"' + escaped(html) + '"'
    if raw.count(old_payload) != 1:
        sys.exit('FAIL: the html payload is not addressable')
    raw = raw.replace(old_payload, '"html":"' + escaped(out) + '"', 1)
    json.loads(raw)
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('applied          the row kit and the Modulation head are shared')
    print('applied          header menus: Modulation head, nested route settings')
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
