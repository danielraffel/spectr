#!/usr/bin/env python3
"""Freeze LENGTH, second pass: the menu, the Fraction list, the trigger label.

THE MENU

  One flat list: the sixteen fractions of a bar on their own (1/32 bar ...
  15/16 bar), a separator, 1, 2, 4 and 8 bars, a separator, then -- when a
  compound length such as 1 1/8 bars is in force -- that length, checked,
  right above "Custom length...", which edits it. A fraction row is bars 0 +
  exactly that fraction, a preset of the Freeze Length parameter like the
  whole bars (the processor lists its presets in this order). The collapsed
  control always shows the value, never "Custom...".

  Height: as tall as its rows, up to the room between the trigger and the
  bottom rail, measured when it opens; past that the rows scroll. At the
  authored 1320x860 every row fits (the 22 rows and two separators are
  720pt of the 743pt available); a compound length past the loop memory
  adds its row and note and scrolls a little. Pulp does not scroll an
  overflow container (`overflow: auto` only clips here, see
  patch_materialized_band_menu_no_cap.py), so this is the help guide's
  scroller: a clipping viewport with a numeric height, the rows moved by a
  negative margin, the wheel, the keys, and Spectr's 4pt scrollbar. It opens
  with the checked row in view and two rows of context around it.

  Keyboard: an arrow on the focused trigger opens the menu on the checked
  row; Up/Down walk every row and wrap; Home/End; PageUp/PageDown move a
  viewport less a row; the highlighted row is kept in view; Return or Space
  activates; Escape closes and returns the focus to the trigger. Spectr
  owns this keyboard: the trigger opts out of Pulp's popup owner
  (data-pulp-popup-default="off"), which cannot scroll its highlight into a
  clipped view. It keeps that owner's conventions -- no highlight until an
  arrow or a hover, the first arrow steps off the checked row, the same
  highlight fill, `data-pulp-popup-active` on the highlighted row -- and
  makes the navigation claim the owner would have made.

THE FRACTION LIST (the Custom editor)

  One column of the seventeen fractions, Spectr's 30pt menu rows, in a
  viewport eight rows tall (254pt) that scrolls: by the wheel or a trackpad,
  by Up/Down/Home/End (the highlighted row is scrolled into view in the same
  step), and it opens with the chosen fraction centred. A 4pt scrollbar --
  the help guide's track and thumb -- shows where the view is. Pulp does not
  scroll an overflow container (`overflow: auto` only clips here, see
  patch_materialized_band_menu_no_cap.py), so this is the help guide's
  scroller: a clipping viewport with a numeric height and the rows moved
  inside it by a negative margin. It owns its keyboard on the same terms as
  the menu, and Escape closes the list and leaves the editor open.

  The zero fraction shows as an em dash, "no fraction", in the list and on
  its trigger; the model keeps "0" (3 + 0 is three whole bars, previewed
  "= 3 bars").

  WHY ROWS COULD NOT BE PRESSED. The list used to be mounted inside the
  Fraction trigger's wrapper, i.e. BEFORE the editor's preview, CANCEL and
  APPLY in tree order. A press is routed to the list's row, but the click
  fires on mouse-up only if the tree's hit test at that point finds the same
  row -- and inside the editor's box the hit test found the later siblings
  under the list (measured: the "0" and "1/32" rows, over the preview and
  the buttons, took no press; every row below the editor did). The list is
  now the editor's LAST child, positioned under the trigger (left edge to
  left edge, the trigger's 108pt width, like a combo box), so it is the
  topmost thing at every point it covers, for painting and hit testing.

EVERY ROW TAKES A PRESS

  The menu hangs from the header's output cluster (26pt tall) down to
  ~740pt. A tree hit test forgives an overflowing child only 500pt past its
  box, so the rows below ~535pt were routed into the menu on the press but
  never clicked: Pulp fires the click on mouse-up only when the tree's hit
  test finds the pressed row again. The cluster, the LENGTH wrapper and the
  menu root now reach down the editor for hit testing (hitSlop) without
  taking a press themselves (pointerEvents box-none).

THE TRIGGER LABEL

  While the Custom editor is open the trigger reads "Custom", not
  "Custom…". Measured in the shipping standalone, the old label was already
  the bound JetBrains Mono face at the value size (every glyph on one 6.3pt
  pitch, like "1 bar"); what read as a different font was its ellipsis,
  three dots squeezed into one cell.

TRIGGER TOGGLE

  The LENGTH trigger closes its menu or its Custom editor, and the Fraction
  trigger its list, when they were open as the press began, through the
  interim helpers of patch_materialized_dropdown_trigger_toggle.py (to be
  replaced by the Pulp SDK's own rule).

Why a script and not a hand edit: the shipping document is one minified line
and the materialized generator cannot rebuild it. Each patch point is asserted
to occur exactly once, a half-patched document is refused, and a second run
reports "already applied". Needs patch_materialized_freeze_length.py and
patch_materialized_dropdown_trigger_toggle.py applied first.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "function SpectrLengthFractionList("
PREREQUISITES = ("function SpectrFreezeLength()", "function spectrDropdownWasOpen(name, open)")

# ── rows ────────────────────────────────────────────────────────────────────

ROW_STYLE_START = "function spectrLengthRowStyle(selected) {\n"
ROW_STYLE_END = "function SpectrLengthCheck({ on }) {\n"
ROW_STYLE_NEW = r'''const SPECTR_LENGTH_HIGHLIGHT = "rgba(120,180,255,0.18)";
// A row of the LENGTH menu or the Fraction list: Spectr's `menuItem`, the
// EDIT MODE / ANALYZER menus' tint and edge when checked, and Pulp's popup
// highlight fill when highlighted.
function spectrLengthRowStyle(selected, highlighted) {
  const base = typeof menuItem === "object" && menuItem ? menuItem : {
    background: "rgba(255,255,255,0.025)", border: "1px solid transparent",
    color: "rgba(255,255,255,0.85)", fontFamily: "var(--mono)", fontSize: 10.5,
    letterSpacing: 0.8, padding: "7px 10px", minHeight: 30, width: "100%",
    boxSizing: "border-box", display: "flex", alignItems: "center",
    justifyContent: "flex-start", gap: 6, textAlign: "left", cursor: "pointer",
    borderRadius: 3 };
  return Object.assign({}, base,
    { whiteSpace: "nowrap", flexShrink: 0, height: 30, minHeight: 30 },
    selected ? { background: SPECTR_LENGTH_ACCENT, border: "1px solid " + SPECTR_LENGTH_ACCENT_EDGE,
                 color: "#fff" } : {},
    highlighted ? { background: SPECTR_LENGTH_HIGHLIGHT, backgroundColor: SPECTR_LENGTH_HIGHLIGHT } : {});
}
// Spectr's scrollbar -- the help guide's 4pt track and thumb -- for a
// viewport `height` tall showing `offset` of `content`.
function SpectrLengthScrollbar({ height, content, offset, name }) {
  const max = Math.max(1, content - height);
  const thumb = Math.max(24, Math.round(height / content * height));
  return /* @__PURE__ */ React.createElement("div", {
    [name]: true,
    "aria-hidden": true,
    style: { position: "relative", width: 4, height, flexShrink: 0, borderRadius: 2,
             background: "rgba(255,255,255,0.06)" }
  }, /* @__PURE__ */ React.createElement("div", {
    [name + "-thumb"]: true,
    style: { position: "absolute", top: Math.round(Math.min(1, offset / max) * (height - thumb)),
             left: 0, width: 4, height: thumb, borderRadius: 2,
             background: "rgba(150,200,255,0.45)" }
  }));
}
'''

ROW_START = "function SpectrLengthRow({ label, selected, action, onPick, value }) {\n"
ROW_END = ("// The custom editor: Bars + Fraction, a preview from the processor, CANCEL\n"
           "// and APPLY. `initial` is the custom length to start from.\n"
           "function SpectrLengthEditor(")
ROW_NEW = r'''function SpectrLengthRow({ label, selected, highlighted, action, onPick, onHover, value }) {
  return /* @__PURE__ */ React.createElement("button", {
    role: "option",
    "aria-selected": selected ? "true" : "false",
    "data-spectr-length-option": value,
    "data-spectr-length-action": action || undefined,
    "data-pulp-popup-active": highlighted ? "true" : "false",
    onPointerEnter: onHover,
    onClick: onPick,
    style: spectrLengthRowStyle(selected, highlighted)
  }, /* @__PURE__ */ React.createElement(SpectrLengthCheck, { on: selected }),
     /* @__PURE__ */ React.createElement("span", null, label));
}
''' + r'''// The Custom editor's Fraction list: one column of Spectr's menu rows in a
// viewport eight rows tall. Pulp does not scroll an overflow container
// (`overflow: auto` only clips), so this is the help guide's scroller: the
// viewport clips at a numeric height, the rows move inside it by a negative
// margin, and the wheel, the arrow keys and the opening selection move them.
//
// The list owns its keyboard highlight (its trigger opts out of Pulp's popup
// owner) so that the highlight and the scroll move in the same step: the
// popup owner moves its highlight after the key's listeners have run, where
// nothing here could follow it without waiting for a frame. It keeps that
// owner's conventions: no highlight until an arrow or a hover asks for one,
// the first arrow steps off the chosen row, the ends wrap, Return or Space
// picks the highlighted row, the same highlight fill, and
// `data-pulp-popup-active` on the highlighted row.
const SPECTR_FRACTION_ROW = 30;
const SPECTR_FRACTION_GAP = 2;
const SPECTR_FRACTION_VISIBLE = 8;
const SPECTR_FRACTION_HIGHLIGHT = "rgba(120,180,255,0.18)";
// The zero fraction is "no fraction": the model keeps "0" (3 + 0 is three
// whole bars), the list and the trigger show an em dash.
function spectrFractionText(f) { return f === "0" ? "\u2014" : f; }
function spectrFractionListMetrics(count) {
  const pitch = SPECTR_FRACTION_ROW + SPECTR_FRACTION_GAP;
  const viewport = SPECTR_FRACTION_VISIBLE * pitch - SPECTR_FRACTION_GAP;
  const content = Math.max(0, count * pitch - SPECTR_FRACTION_GAP);
  return { pitch, viewport: Math.min(viewport, content), content,
           maxScroll: Math.max(0, content - viewport) };
}
function SpectrLengthFractionList({ fractions, value, onPick, onDismiss }) {
  const m = spectrFractionListMetrics(fractions.length);
  const clamp = (v) => Math.max(0, Math.min(m.maxScroll, Math.round(v)));
  const chosen = fractions.indexOf(value);
  // Opens with the chosen fraction centred in view.
  const [offset, setOffset] = React.useState(() =>
    clamp(Math.max(0, chosen) * m.pitch - (m.viewport - SPECTR_FRACTION_ROW) / 2));
  const [active, setActive] = React.useState(-1); // -1: no highlight shown
  const state = React.useRef({ offset, active });
  state.current.offset = offset;
  state.current.active = active;
  const scrollTo = (next) => {
    const v = clamp(next);
    if (v !== state.current.offset) { state.current.offset = v; setOffset(v); }
  };
  // Keep row `index` in view: the least movement that shows all of it.
  const reveal = (index) => {
    const top = index * m.pitch;
    const bottom = top + SPECTR_FRACTION_ROW;
    const o = state.current.offset;
    if (top < o) scrollTo(top);
    else if (bottom > o + m.viewport) scrollTo(bottom - m.viewport);
  };
  const highlight = (index, show) => {
    state.current.active = index;
    setActive(index);
    if (show) reveal(index);
  };
  const actions = React.useRef(null);
  actions.current = { highlight, onPick, fractions, chosen };
  React.useEffect(() => {
    const onKey = (event) => {
      const a = actions.current;
      if (!event || !a || event.metaKey || event.ctrlKey || event.altKey) return;
      const key = event.key;
      const count = a.fractions.length;
      const take = () => {
        if (typeof event.preventDefault === "function") event.preventDefault();
        if (typeof event.stopImmediatePropagation === "function") event.stopImmediatePropagation();
        if (typeof event.stopPropagation === "function") event.stopPropagation();
      };
      const at = state.current.active;
      if (key === "ArrowDown" || key === "ArrowUp") {
        take();
        const step = key === "ArrowDown" ? 1 : -1;
        const from = at >= 0 ? at : a.chosen;
        const next = from >= 0 ? (from + step + count) % count : (step > 0 ? 0 : count - 1);
        a.highlight(next, true);
      } else if (key === "Home" || key === "End") {
        take();
        a.highlight(key === "Home" ? 0 : count - 1, true);
      } else if (key === "Enter" || key === " ") {
        take();
        a.onPick(a.fractions[at >= 0 ? at : Math.max(0, a.chosen)]);
      } else if (key === "Escape") {
        // Escape closes the list and nothing else. After the script's key
        // listeners run, Pulp retires the topmost overlay claim -- this list
        // -- and that calls its onDismiss. So the list must not also close
        // itself here: the claim retired afterwards would be the editor's.
        take();
      } else if (key === "Tab") {
        take();
      }
    };
    document.addEventListener("keydown", onKey, true);
    // The keyboard is this list's while it is open, in a plug-in host too:
    // the claim Pulp's popup owner would have made for it.
    if (typeof claimDocumentNavigationFocus === "function") claimDocumentNavigationFocus();
    return () => {
      document.removeEventListener("keydown", onKey, true);
      if (typeof releaseDocumentNavigationFocus === "function") releaseDocumentNavigationFocus();
    };
  }, []);
  const onWheel = (event) => {
    const delta = event && typeof event.deltaY === "number" ? event.deltaY : 0;
    if (!delta) return;
    if (typeof event.preventDefault === "function") event.preventDefault();
    if (typeof event.stopPropagation === "function") event.stopPropagation();
    scrollTo(state.current.offset + delta);
  };
  const scrolls = m.maxScroll > 0;
  return /* @__PURE__ */ React.createElement("div", {
    "data-spectr-length-fraction-options": true,
    "data-spectr-length-fraction-offset": String(offset),
    "data-spectr-overlay": "true",
    "data-pulp-popup-default": "off",
    overlay: true,
    onDismiss,
    onWheel,
    role: "listbox",
    "aria-label": "Fraction of a bar",
    // Under the Fraction trigger, its width, like a combo box. Mounted as
    // the editor's last child, so these are editor coordinates: the trigger
    // starts after the editor's 12pt padding, the 84pt Bars field and the
    // 20pt "+", and ends below the 12pt padding, the 12pt caption row, the
    // 8pt gap and its own 28pt.
    style: Object.assign({}, SPECTR_LENGTH_PANEL, {
      position: "absolute", top: 12 + 12 + 8 + 28 + 3, left: 12 + 84 + 20, width: 108, zIndex: 31,
      backdropFilter: undefined, flexDirection: "row", gap: 4, alignItems: "flex-start"
    })
  }, /* @__PURE__ */ React.createElement("div", {
    "data-spectr-length-fraction-viewport": true,
    style: { flex: 1, minWidth: 0, height: m.viewport, flexShrink: 0, overflow: "hidden",
             position: "relative" }
  }, /* @__PURE__ */ React.createElement("div", {
    "data-spectr-length-fraction-rows": true,
    style: { marginTop: 0 - offset, flexShrink: 0, display: "flex", flexDirection: "column",
             gap: SPECTR_FRACTION_GAP }
  }, fractions.map((f, index) => /* @__PURE__ */ React.createElement("button", {
    key: f,
    role: "option",
    "aria-selected": f === value ? "true" : "false",
    "data-spectr-length-fraction-option": f,
    "data-pulp-popup-active": index === active ? "true" : "false",
    onPointerEnter: () => { if (state.current.active !== index) highlight(index, false); },
    onClick: () => onPick(f),
    style: Object.assign(spectrLengthRowStyle(f === value),
                         { height: SPECTR_FRACTION_ROW, minHeight: SPECTR_FRACTION_ROW,
                           flexShrink: 0 },
                         index === active ? { background: SPECTR_FRACTION_HIGHLIGHT,
                                              backgroundColor: SPECTR_FRACTION_HIGHLIGHT } : {})
  }, /* @__PURE__ */ React.createElement(SpectrLengthCheck, { on: f === value }),
     /* @__PURE__ */ React.createElement("span", null, spectrFractionText(f)))))),
  scrolls && /* @__PURE__ */ React.createElement(SpectrLengthScrollbar, {
    name: "data-spectr-length-fraction-scrollbar", height: m.viewport, content: m.content,
    offset }));
}
'''

# ── the editor: its dismissal, the Fraction trigger, the list mounted last ──

EDITOR_DISMISS_OLD = "    onDismiss: () => onClose(false),\n"
EDITOR_DISMISS_NEW = '    onDismiss: () => { spectrDropdownDismissed("length"); onClose(false); },\n'

FRACTION_OLD_START = ('        /* @__PURE__ */ React.createElement("button", {\n'
                      '          "data-spectr-length-fraction": fraction,\n')
FRACTION_OLD_END = '             /* @__PURE__ */ React.createElement("span", null, f)))))))),\n'
FRACTION_NEW = r'''        /* @__PURE__ */ React.createElement("button", {
          "data-spectr-length-fraction": fraction,
          "data-spectr-dropdown": "fraction",
          "data-pulp-popup-default": "off",
          onPointerEnter: () => spectrDropdownTriggerEnter("fraction"),
          onPointerLeave: () => spectrDropdownTriggerLeave("fraction"),
          "aria-haspopup": "listbox",
          "aria-expanded": fractionOpen,
          "aria-label": "Fraction of a bar, " + (fraction === "0" ? "none" : fraction),
          onClick: () => { setFocus(1); setFractionOpen(!spectrDropdownWasOpen("fraction", fractionOpen)); },
          style: { ...field, width: 108, border: ring(1), padding: "0 8px 0 10px", display: "flex",
                   alignItems: "center", justifyContent: "space-between", cursor: "pointer" }
        }, /* @__PURE__ */ React.createElement("span", null, spectrFractionText(fraction)),
           /* @__PURE__ */ React.createElement("span", { style: { fontSize: 9, opacity: 0.7 } }, "▾")))),
'''

EDITOR_END_OLD = '''      }, "APPLY")));
}
function SpectrFreezeLength() {
'''
EDITOR_END_NEW = '''      }, "APPLY")),
    // The Fraction list last, so it is topmost wherever it reaches: over the
    // preview and the buttons as well as below the editor.
    fractionOpen && /* @__PURE__ */ React.createElement(SpectrLengthFractionList, {
      fractions,
      value: fraction,
      onDismiss: () => { spectrDropdownDismissed("fraction"); setFractionOpen(false); },
      onPick: (f) => { setFraction(f); setFractionOpen(false); setFocus(1); }
    }));
}
function SpectrFreezeLength() {
'''

# ── the header's output cluster lets the LENGTH menu's rows be hit ─────────

CLUSTER_OLD = ('  return /* @__PURE__ */ React.createElement("span", {\n'
               '    "data-spectr-output-cluster": true,\n'
               '    style: {\n'
               '    position: "absolute",\n'
               '    left: 282,\n'
               '    top: 9,\n'
               '    height: 26,\n')
CLUSTER_NEW = CLUSTER_OLD + (
    '    // The LENGTH menu hangs from this cluster far below its 26pt, past the\n'
    '    // 500pt a tree hit test forgives an overflowing child: reach down the\n'
    '    // whole editor for hit testing (hitSlop), but never take a press\n'
    '    // itself (box-none), so a press over the plot still reaches the plot.\n'
    '    pointerEvents: "box-none",\n'
    '    hitSlop: "0 0 860 0",\n')

# ── the LENGTH control itself ───────────────────────────────────────────────

CONTROL_START = "function SpectrFreezeLength() {\n"
CONTROL_END = "\nfunction SpectrOutputMeter("
CONTROL_NEW = r'''// The LENGTH menu's geometry: Spectr's 30pt rows on a 2pt gap, a separator
// 1pt with 3pt above and below, the panel's 6pt padding and 1pt border.
const SPECTR_LENGTH_ROW = 30;
const SPECTR_LENGTH_GAP = 2;
const SPECTR_LENGTH_SEPARATOR = 7;
// How far below the header the LENGTH menu can reach, for hit testing: the
// whole editor.
const SPECTR_LENGTH_REACH = "0 0 860 0";
function SpectrFreezeLength() {
  const store = spectrFreezeStore();
  const [, setRevision] = React.useState(0);
  const [menuOpen, setMenuOpen] = React.useState(false);
  const [editorOpen, setEditorOpen] = React.useState(false);
  // The keyboard / hover highlight, an index into the rows; -1 shows none
  // (a menu opened by a click shows only its checked row).
  const [active, setActive] = React.useState(-1);
  // The list's scroll offset and the viewport it scrolls in, sized when the
  // menu opens to the room below the trigger.
  const [offset, setOffset] = React.useState(0);
  const [viewport, setViewport] = React.useState(0);
  const triggerRef = React.useRef(null);
  const rootRef = React.useRef(null);
  React.useEffect(() => {
    const sync = () => setRevision((n) => n + 1);
    store.listeners.push(sync);
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  const length = store.length;
  const presets = Array.isArray(store.lengthPresets) ? store.lengthPresets : [];
  const custom = !!length && length.preset >= presets.length;
  const capped = !!length && length.capped;
  // One flat list: every fraction of a bar, a separator, the whole bars, a
  // separator, then -- when a compound length is in force -- that length,
  // checked, right above "Custom length...", which edits it. The processor
  // lists its presets in this order (fractions then bars, ascending).
  const items = [];
  const rows = [];
  const addRow = (row) => { row.index = rows.length; rows.push(row); items.push(row); };
  presets.forEach((preset, index) => {
    if (preset.bars > 0 && index > 0 && presets[index - 1].bars === 0) items.push({ separator: "bars" });
    addRow({ kind: "preset", preset, presetIndex: index });
  });
  items.push({ separator: "custom" });
  if (custom) addRow({ kind: "custom" });
  addRow({ kind: "editor" });
  // Each row's top in the list, and the list's height.
  let y = 0;
  items.forEach((item) => {
    item.top = y;
    y += (item.separator ? SPECTR_LENGTH_SEPARATOR : SPECTR_LENGTH_ROW) + SPECTR_LENGTH_GAP;
  });
  const capNote = capped ? 46 : 0;
  const content = y - SPECTR_LENGTH_GAP + capNote;
  const checked = rows.findIndex((r) =>
    (r.kind === "preset" && !!length && r.presetIndex === length.preset) || r.kind === "custom");
  // "Custom", without an ellipsis: U+2026 is one cell of three squeezed
  // dots in the mono face, and read as a different font beside the other
  // values.
  const label = editorOpen ? "Custom" : length ? length.label : "";
  // A fixed box, so a long custom length never moves OUTPUT and PEAK: a
  // label past what 10.5pt fits in its 60pt ("1 1/8 bars", "128 15/16
  // bars") steps the size down to fit instead (a mono glyph is 0.6 em).
  const glyphs = Math.max(1, label.length);
  const fontSize = Math.min(10.5, Math.floor(600 / (0.6 * glyphs)) / 10);
  const pick = (preset) => {
    setMenuOpen(false);
    spectrCommitFreezeLength(preset.bars, preset.fraction);
  };
  const openEditor = () => { setMenuOpen(false); setEditorOpen(true); };
  // The room the menu has: from its top (29pt below the trigger's) down to
  // 8pt above the bottom rail, in the editor's own coordinates, measured
  // when it opens. Past that the list scrolls.
  const room = () => {
    const rail = document.querySelector('[data-spectr-bottom-rail]');
    const root = rootRef.current;
    const r = rail && rail.getBoundingClientRect ? rail.getBoundingClientRect() : null;
    const a = root && root.getBoundingClientRect ? root.getBoundingClientRect() : null;
    const bottom = r && r.height > 0 ? Number(r.top) : 804;
    const top = a && a.height > 0 ? Number(a.top) + 29 : 39;
    return Math.max(4 * (SPECTR_LENGTH_ROW + SPECTR_LENGTH_GAP), bottom - 8 - top - 14);
  };
  const clampFor = (vh) => (v) => Math.max(0, Math.min(Math.max(0, content - vh), Math.round(v)));
  // Keep row `index` in view with `context` rows of room on either side
  // where the list allows it: the least movement that does.
  const revealIn = (index, current, vh, context) => {
    const row = rows[index];
    if (!row) return current;
    const pad = context * (SPECTR_LENGTH_ROW + SPECTR_LENGTH_GAP);
    const clamp = clampFor(vh);
    if (row.top - pad < current) return clamp(row.top - pad);
    if (row.top + SPECTR_LENGTH_ROW + pad > current + vh)
      return clamp(row.top + SPECTR_LENGTH_ROW + pad - vh);
    return current;
  };
  // Open on the checked row, two rows of context around it; `reveal` (an
  // arrow key) shows the highlight there at once.
  const openMenu = (reveal, edge) => {
    const vh = Math.min(content, room());
    const seed = checked >= 0 ? checked : edge === "last" ? rows.length - 1 : 0;
    setViewport(vh);
    setOffset(revealIn(seed, 0, vh, 2));
    setActive(reveal ? seed : -1);
    setEditorOpen(false);
    setMenuOpen(true);
  };
  const activate = (row) => {
    if (!row) return;
    if (row.kind === "preset") pick(row.preset);
    else if (row.kind === "editor") openEditor();
    else setMenuOpen(false);
  };
  const highlight = (index) => {
    setActive(index);
    setOffset((o) => revealIn(index, o, viewport, 0));
  };
  // The menu's keyboard. Spectr owns it (the trigger opts out of Pulp's
  // popup owner, which cannot scroll a highlight into view) and keeps that
  // owner's conventions: an arrow on the focused trigger opens the menu on
  // the checked row; Up/Down move one row and wrap, the first stepping off
  // the checked row; Home/End; PageUp/PageDown a viewport less a row;
  // Return or Space activates the highlighted row; the same highlight fill
  // and `data-pulp-popup-active`; Escape closes it and returns the focus to
  // the trigger.
  const keys = React.useRef(null);
  keys.current = { menuOpen, editorOpen, rows, active, checked, viewport, openMenu, activate,
                   highlight };
  React.useEffect(() => {
    const onKey = (event) => {
      const k = keys.current;
      if (!event || !k || event.metaKey || event.ctrlKey || event.altKey) return;
      const key = event.key;
      const take = () => {
        if (typeof event.preventDefault === "function") event.preventDefault();
        if (typeof event.stopImmediatePropagation === "function") event.stopImmediatePropagation();
        if (typeof event.stopPropagation === "function") event.stopPropagation();
      };
      if (!k.menuOpen) {
        const trigger = triggerRef.current;
        const focused = typeof document !== "undefined" ? document.activeElement : null;
        if (k.editorOpen || !trigger || !focused
            || !(focused === trigger || (trigger.contains && trigger.contains(focused)))) return;
        if (key === "ArrowDown" || key === "ArrowUp") {
          take();
          k.openMenu(true, key === "ArrowUp" ? "last" : "first");
        }
        return;
      }
      const count = k.rows.length;
      const at = k.active;
      const page = Math.max(1, Math.floor(k.viewport / (SPECTR_LENGTH_ROW + SPECTR_LENGTH_GAP)) - 1);
      if (key === "ArrowDown" || key === "ArrowUp") {
        take();
        const step = key === "ArrowDown" ? 1 : -1;
        const from = at >= 0 ? at : k.checked;
        k.highlight(from >= 0 ? (from + step + count) % count : step > 0 ? 0 : count - 1);
      } else if (key === "PageDown" || key === "PageUp") {
        take();
        const from = at >= 0 ? at : Math.max(0, k.checked);
        k.highlight(Math.max(0, Math.min(count - 1, from + (key === "PageDown" ? page : -page))));
      } else if (key === "Home" || key === "End") {
        take();
        k.highlight(key === "Home" ? 0 : count - 1);
      } else if (key === "Enter" || key === " ") {
        take();
        k.activate(k.rows[at >= 0 ? at : k.checked >= 0 ? k.checked : 0]);
      } else if (key === "Escape") {
        // Closed here, as Pulp's popup owner closed its menus; the claim
        // Pulp retires after the key's listeners is then already gone (or is
        // this menu's, and closes nothing more).
        take();
        setMenuOpen(false);
        if (triggerRef.current && triggerRef.current.focus) triggerRef.current.focus();
      } else if (key === "Tab") {
        take();
      }
    };
    document.addEventListener("keydown", onKey, true);
    return () => document.removeEventListener("keydown", onKey, true);
  }, []);
  // The keyboard is the menu's while it is open, in a plug-in host too: the
  // claim Pulp's popup owner would have made for it.
  React.useEffect(() => {
    if (!menuOpen) return undefined;
    if (typeof claimDocumentNavigationFocus === "function") claimDocumentNavigationFocus();
    return () => {
      if (typeof releaseDocumentNavigationFocus === "function") releaseDocumentNavigationFocus();
    };
  }, [menuOpen]);
  const onWheel = (event) => {
    const delta = event && typeof event.deltaY === "number" ? event.deltaY : 0;
    if (!delta || viewport >= content) return;
    if (typeof event.preventDefault === "function") event.preventDefault();
    if (typeof event.stopPropagation === "function") event.stopPropagation();
    setOffset((o) => clampFor(viewport)(o + delta));
  };
  const scrolls = menuOpen && viewport > 0 && viewport < content;
  const element = (item) => {
    if (item.separator) {
      return /* @__PURE__ */ React.createElement("div", {
        key: "separator-" + item.separator, "aria-hidden": true,
        "data-spectr-length-separator": item.separator,
        style: { height: 1, margin: "3px 4px", background: "rgba(255,255,255,0.1)", flexShrink: 0 }
      });
    }
    const row = item;
    const value = row.kind === "preset" ? row.preset.label
      : row.kind === "editor" ? "custom-editor" : "custom";
    const text = row.kind === "preset" ? row.preset.label
      : row.kind === "editor" ? "Custom length…" : length.label;
    return /* @__PURE__ */ React.createElement(SpectrLengthRow, {
      key: value, label: text, value,
      selected: row.index === checked && row.kind !== "editor",
      highlighted: row.index === active,
      action: row.kind === "editor" ? "custom" : undefined,
      onHover: () => { if (keys.current && keys.current.active !== row.index) setActive(row.index); },
      onPick: () => activate(row)
    });
  };
  return /* @__PURE__ */ React.createElement("span", {
    "data-spectr-freeze-length": true,
    "data-spectr-freeze-length-label": length ? length.label : "",
    "data-spectr-freeze-length-preset": length ? String(length.preset) : "",
    // The menu reaches far below the header, past the 500pt a tree hit test
    // forgives an overflowing child, so every box between it and the
    // editor -- this wrapper, the menu root and the header's output cluster
    // -- reaches down that far for hit testing (hitSlop) without ever
    // taking a press itself (box-none). Without it a press on the lower
    // rows was routed into the menu but never clicked: the click needs the
    // tree's hit test to find the same row at mouse-up.
    style: { position: "relative", display: "inline-flex", alignItems: "center", flexShrink: 0,
             pointerEvents: "box-none", hitSlop: SPECTR_LENGTH_REACH }
  }, /* @__PURE__ */ React.createElement("div", { ref: rootRef, "data-spectr-menu-root": "length",
    style: { position: "relative", pointerEvents: "box-none", hitSlop: SPECTR_LENGTH_REACH } },
    /* @__PURE__ */ React.createElement("button", {
      ref: triggerRef,
      "data-spectr-menu-trigger": true,
      "data-spectr-length-trigger": true,
      "data-spectr-dropdown": "length",
      "data-pulp-popup-default": "off",
      onPointerEnter: () => spectrDropdownTriggerEnter("length"),
      onPointerLeave: () => spectrDropdownTriggerLeave("length"),
      "aria-haspopup": "listbox",
      "aria-expanded": menuOpen,
      "aria-label": "Freeze length, " + (length ? length.label : ""),
      title: "How much of the incoming sound a freeze takes in"
        + (capped ? ". Longer than Spectr can hold at this tempo: a freeze loops the last "
           + Math.round(length.capSeconds) + " s." : ""),
      // Closes whatever it opened -- the menu or the Custom editor -- when
      // either was open as this press began (spectrDropdownWasOpen).
      onClick: () => {
        if (spectrDropdownWasOpen("length", menuOpen || editorOpen)) {
          setEditorOpen(false);
          setMenuOpen(false);
          return;
        }
        openMenu(false);
      },
      style: {
        width: 88, minWidth: 88, flexShrink: 0, height: 24, boxSizing: "border-box",
        padding: "0 7px 0 8px", borderRadius: 3, display: "flex", alignItems: "center",
        justifyContent: "space-between", gap: 4, cursor: "pointer",
        // The header's band-count trigger, token for token.
        background: menuOpen || editorOpen ? "rgba(255,255,255,0.08)" : "transparent",
        border: "1px solid " + (menuOpen || editorOpen ? "rgba(255,255,255,0.18)" : "rgba(255,255,255,0.08)"),
        color: "rgba(255,255,255,0.85)", fontFamily: SPECTR_LENGTH_FACE, lineHeight: 1
      }
    }, /* @__PURE__ */ React.createElement("span", {
      "data-spectr-length-value": true,
      style: { fontSize, letterSpacing: 0, whiteSpace: "nowrap", flexShrink: 0,
               color: capped ? "hsl(35,90%,75%)" : "rgba(255,255,255,0.88)" }
    }, label),
       /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { fontSize: 9, opacity: 0.7, flexShrink: 0 } }, "▾")),
    menuOpen && /* @__PURE__ */ React.createElement("div", {
      "data-spectr-menu-options": true,
      "data-spectr-length-offset": String(offset),
      "data-spectr-length-viewport": String(viewport),
      "data-spectr-overlay": "true",
      "data-pulp-popup-default": "off",
      overlay: true,
      onDismiss: () => { spectrDropdownDismissed("length"); setMenuOpen(false); },
      onWheel,
      role: "listbox",
      "aria-label": "Freeze length",
      // As tall as its rows, up to the room below the trigger; past that
      // the rows scroll in a clipping viewport (Pulp does not scroll an
      // overflow container -- the help guide's scroller). Everything the
      // panel shows lies inside its own box, so every visible row takes a
      // press, and a row scrolled away lies outside it.
      style: Object.assign({}, SPECTR_LENGTH_PANEL, {
        position: "absolute", top: 29, left: 0, width: 160, zIndex: 20,
        flexDirection: "row", gap: 4, alignItems: "flex-start"
      })
    }, /* @__PURE__ */ React.createElement("div", {
      "data-spectr-length-viewport-box": true,
      style: { flex: 1, minWidth: 0, height: viewport || content, flexShrink: 0,
               overflow: "hidden", position: "relative" }
    }, /* @__PURE__ */ React.createElement("div", {
      "data-spectr-length-rows": true,
      style: { marginTop: 0 - offset, flexShrink: 0, display: "flex", flexDirection: "column",
               gap: SPECTR_LENGTH_GAP }
    }, items.map(element),
      capped && /* @__PURE__ */ React.createElement("div", {
        "data-spectr-length-cap-note": true,
        style: { height: capNote - SPECTR_LENGTH_GAP, flexShrink: 0, boxSizing: "border-box",
                 padding: "5px 8px 3px", fontFamily: "var(--sans)", fontSize: 9.5, lineHeight: 1.45,
                 color: "rgba(255,255,255,0.6)", whiteSpace: "normal" }
      }, "At " + Math.round(length.tempo) + " BPM this is longer than Spectr can hold. A freeze loops the last "
         + Math.round(length.capSeconds) + " s."))),
    scrolls && /* @__PURE__ */ React.createElement(SpectrLengthScrollbar, {
      name: "data-spectr-length-scrollbar", height: viewport, content, offset }))),
    editorOpen && /* @__PURE__ */ React.createElement(SpectrLengthEditor, {
      store,
      initial: custom ? { bars: length.bars, fraction: length.fraction }
        : length ? { bars: length.customBars, fraction: length.customFraction }
        : { bars: 1, fraction: "0" },
      onClose: () => setEditorOpen(false)
    }));
}
'''


def once(html, text, label):
    count = html.count(text)
    if count != 1:
        sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))


def between(html, start, end, label):
    once(html, start, label + " start")
    at = html.index(start)
    stop = html.find(end, at)
    if stop < 0:
        sys.exit("FAIL %s: no end after its start" % label)
    return at, stop


def main():
    raw = open(PATH, encoding="utf-8").read()
    document = json.loads(raw)
    html = document["html"]

    if MARKER in html:
        for marker, label in ((ROW_STYLE_NEW, "row style"), (ROW_NEW, "rows and list"),
                              (EDITOR_DISMISS_NEW, "editor dismissal"),
                              (FRACTION_NEW, "Fraction trigger"),
                              (EDITOR_END_NEW.rsplit("function SpectrFreezeLength", 1)[0], "list mount"),
                              (CONTROL_NEW, "LENGTH control"), (CLUSTER_NEW, "output cluster")):
            if html.count(marker) != 1:
                sys.exit("FAIL: the LENGTH menu v2 is present but its %s is not; "
                         "the document is half patched" % label)
        print("already applied  LENGTH menu v2, one-column Fraction list")
        return 0

    for prerequisite in PREREQUISITES:
        if prerequisite not in html:
            sys.exit("FAIL: %r is missing; apply its patch first" % prerequisite)
    once(html, EDITOR_DISMISS_OLD, "editor dismissal")
    once(html, CLUSTER_OLD, "output cluster")
    html = html.replace(CLUSTER_OLD, CLUSTER_NEW, 1)
    once(html, EDITOR_END_OLD, "editor end")

    at, stop = between(html, ROW_STYLE_START, ROW_STYLE_END, "row style")
    html = html[:at] + ROW_STYLE_NEW + html[stop:]
    at, stop = between(html, ROW_START, ROW_END, "row")
    html = html[:at] + ROW_NEW + html[stop:]
    html = html.replace(EDITOR_DISMISS_OLD, EDITOR_DISMISS_NEW, 1)
    at, stop = between(html, FRACTION_OLD_START, FRACTION_OLD_END, "Fraction trigger and list")
    html = html[:at] + FRACTION_NEW + html[stop + len(FRACTION_OLD_END):]
    html = html.replace(EDITOR_END_OLD, EDITOR_END_NEW, 1)
    at, stop = between(html, CONTROL_START, CONTROL_END, "LENGTH control")
    html = html[:at] + CONTROL_NEW + html[stop + 1:]

    document["html"] = html
    # Keep every byte outside the html payload as it was, and encode the html
    # the way tools/git/merge_materialized_runtime.py does (ensure_ascii=False).
    head, tail = raw.split('"html":"', 1)
    rest = tail.split('","mime_type":', 1)
    if len(rest) != 2:
        sys.exit("FAIL: the html payload is not followed by mime_type")
    out = (head + '"html":' + json.dumps(html, ensure_ascii=False)
           + ',"mime_type":' + rest[1])
    if json.loads(out) != document:
        sys.exit("FAIL: re-encoding the document changed more than its html")
    open(PATH, "w", encoding="utf-8").write(out)
    print("applied          LENGTH menu v2, one-column Fraction list")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
