#!/usr/bin/env python3
"""Band menu: real placement, submenus beside their entry, hover intent,
and rows that survive a re-render.

PLACEMENT. The menu sized itself from `document.getElementById("root")`,
which is null in the materialized runtime, so it fell back to
`window.innerWidth/innerHeight` -- a stale 800x600. Measured on the
standalone: with the editor surface 1320x860 and the menu at x=378,
`378 + 2*250 + 6 > 792` said there was no room on the right, so every
submenu opened to the LEFT of its `›`; and a menu opened at y=400 was pulled
up to y=163 by a bottom edge 260px too high. The menu's own parent is the
editor surface (it read 1320x860), so the menu measures that instead.

SUBMENU SIDE AND HEIGHT. A submenu opens to the right of its entry and only
flips left when it would not fit (the platform/Spotify convention), and its
first row lines up with the entry that opened it -- measured with
`getBoundingClientRect()`, which agrees with the native rects -- instead of a
constant guessed from the row count.

ROWS SURVIVE A RE-RENDER. `Item` was a component defined inside
`ContextMenu`, so every render created a new component TYPE and React
unmounted and remounted every row. Any state the row carried in its DOM --
the hover fill, the keyboard cursor -- vanished on each render: press Return
on `LFO 1` and the row went dark although the menu stayed open. `Item` is now
called as a plain function, so each row stays the same `<button>` across
renders, and the cursor is repainted after every render.

HOVER INTENT. Moving the pointer onto a different band-menu row closed
nothing, so a submenu hovered once stayed open for the life of the menu.
Now, with a submenu open, hovering another band-menu row closes it (or, on
the other submenu's entry, switches to it) after a short grace period, and
reaching the open submenu -- or returning to its own entry -- cancels that.
The grace period is what lets a diagonal path from an entry to its submenu
cross a neighbouring row without closing the submenu the user is heading
for. With no submenu open, hovering an entry opens it immediately.

Requires patch_materialized_band_menu_keyboard_nav.py.
Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const __spectrBandMenuPlacementAndIntent = true;"
HOVER_INTENT_MS = 250

EDITS = [
    (
        "surface size",
        '''  const root = document.getElementById("root");
  const vw = root ? root.clientWidth : window.innerWidth, vh = root ? root.clientHeight : window.innerHeight;
''',
        '''  // The editor surface is the menu's own parent. `getElementById("root")`
  // is null in this runtime and `window.innerWidth` is a stale 800x600, so
  // both were measuring a screen that does not exist; see
  // tools/patch_materialized_band_menu_placement_and_intent.py.
  const __spectrBandMenuPlacementAndIntent = true;
  const [surface, setSurface] = React.useState(null);
  const [entryOffsets, setEntryOffsets] = React.useState({ macros: null, modulation: null });
  // Until the surface is measured: the root element where a runtime has one,
  // then the window, then the authored design surface -- never NaN.
  const rootBox = typeof document !== "undefined" && document.getElementById
    ? document.getElementById("root") : null;
  const vw = surface ? surface.w
    : (rootBox && rootBox.clientWidth) || window.innerWidth || 1320;
  const vh = surface ? surface.h
    : (rootBox && rootBox.clientHeight) || window.innerHeight || 860;
''',
    ),
    (
        "measure surface and entry rows",
        '''  React.useLayoutEffect(() => {
    const node = ref.current;
    if (!node) return;
''',
        '''  React.useLayoutEffect(() => {
    const node = ref.current;
    if (!node) return;
    const parent = node.parentElement;
    const pw = parent ? parent.offsetWidth : 0, ph = parent ? parent.offsetHeight : 0;
    if (pw > 0 && ph > 0 && (!surface || surface.w !== pw || surface.h !== ph))
      setSurface({ w: pw, h: ph });
    if (typeof node.getBoundingClientRect === "function" && document.querySelector) {
      const menuTop = node.getBoundingClientRect().top;
      const offsetOf = (selector) => {
        const entry = document.querySelector(selector);
        if (!entry || typeof entry.getBoundingClientRect !== "function") return null;
        const dy = entry.getBoundingClientRect().top - menuTop;
        return Number.isFinite(dy) ? Math.round(dy) : null;
      };
      const next = { macros: offsetOf("[data-spectr-macros-anchor]"),
                     modulation: offsetOf("[data-spectr-modulation-anchor]") };
      if (next.macros !== entryOffsets.macros || next.modulation !== entryOffsets.modulation)
        setEntryOffsets(next);
    }
''',
    ),
    (
        "submenu beside its entry",
        '''  const submenuTopFor = (measured, estimate) => {
    const h = Math.min(measured === null ? estimate : measured, menuMaxHeight);
    return Math.max(8, Math.min(top + 108 + (hasSel ? 100 : 0),
                                menuBottom - h - 8));
  };
  const macrosTop = submenuTopFor(
    macrosH, 68 + (hasSel ? 116 : 0) + assigned.length * 29);
  const modulationTop = submenuTopFor(modulationH, 420);
''',
        '''  // The submenu's first row lines up with the entry that opened it (the
  // panel's own 6px top padding is subtracted), then the panel is kept on
  // screen. The row-count estimate only stands until the entry is measured.
  const submenuTopFor = (measured, estimate, entryOffset) => {
    const h = Math.min(measured === null ? estimate : measured, menuMaxHeight);
    const wanted = entryOffset === null
      ? top + 108 + (hasSel ? 100 : 0)
      : top + entryOffset - 6;
    return Math.max(8, Math.min(wanted, menuBottom - h - 8));
  };
  const macrosTop = submenuTopFor(
    macrosH, 68 + (hasSel ? 116 : 0) + assigned.length * 29, entryOffsets.macros);
  const modulationTop = submenuTopFor(modulationH, 420, entryOffsets.modulation);
''',
    ),
    (
        "rows keep their DOM node",
        '''  const Item = ({ action, label, hint, onClick, onKeyDown, onHover, disabled, danger, sub, keepOpen, checked, expanded, hasPopup }) => /* @__PURE__ */ React.createElement(
    "button",
    {
      role: checked === undefined ? "menuitem" : "menuitemcheckbox",''',
        '''  // Called as a plain function, never as a component: a component type
  // defined inside ContextMenu is a NEW type every render, and React remounts
  // every row -- losing the hover fill and the keyboard cursor each time.
  const Item = ({ key, action, label, hint, onClick, onKeyDown, onHover, disabled, danger, sub, keepOpen, checked, expanded, hasPopup }) => /* @__PURE__ */ React.createElement(
    "button",
    {
      key,
      role: checked === undefined ? "menuitem" : "menuitemcheckbox",''',
    ),
    (
        "hover moves the cursor and carries intent",
        '''      onMouseEnter: (e) => {
        if (!disabled) {
          syncHover(e.currentTarget);
          e.currentTarget.style.background = "rgba(120,180,255,0.14)";
          if (onHover) onHover();
        }
      },
      onMouseLeave: (e) => {
        e.currentTarget.style.background = "transparent";
      }''',
        '''      onMouseEnter: (e) => {
        const node = e.currentTarget;
        const level = (disabled ? null : syncHover(node)) || levelOf(node);
        if (!disabled) node.style.background = cursorFill;
        hoverIntent(level, action, disabled ? null : onHover);
      },
      onMouseLeave: (e) => {
        e.currentTarget.style.background = "transparent";
        // The pointer left the row, so the cursor is no longer on it; the
        // repaint after the next render must not light it again.
        const cursor = cursorRef.current;
        const rows = levelRows(cursor.level);
        if (rows[cursor.index] === e.currentTarget)
          cursorRef.current = { level: cursor.level, index: -1 };
      }''',
    ),
    (
        "sync hover reports its level",
        '''  const syncHover = (node) => {
    for (const level of ["root", "macros", "modulation"]) {
      const index = levelRows(level).indexOf(node);
      if (index >= 0) { moveCursor(level, index); return; }
    }
  };
''',
        '''  const syncHover = (node) => {
    for (const level of ["root", "macros", "modulation"]) {
      const index = levelRows(level).indexOf(node);
      if (index >= 0) { moveCursor(level, index); return level; }
    }
    return null;
  };
  // A disabled row is not a cursor stop, but hovering it is still a move
  // away from an open submenu.
  const levelOf = (node) => {
    for (const level of ["root", "macros", "modulation"]) {
      const panel = levelPanel(level);
      if (panel && typeof panel.contains === "function" && panel.contains(node))
        return level;
    }
    return null;
  };
  // Hover intent; see tools/patch_materialized_band_menu_placement_and_intent.py.
  const openLevelRef = React.useRef(null);
  openLevelRef.current = macrosOpen ? "macros" : modulationOpen ? "modulation" : null;
  const intentRef = React.useRef(null);
  const cancelIntent = () => {
    if (intentRef.current !== null) {
      clearTimeout(intentRef.current);
      intentRef.current = null;
    }
  };
  React.useEffect(() => cancelIntent, []);
  const hoverIntent = (level, action, onHover) => {
    // Inside a submenu: the user arrived, so nothing may close it now.
    if (level !== "root") { cancelIntent(); return; }
    const open = openLevelRef.current;
    const opens = action === "macros-toggle" ? "macros"
      : action === "modulation-toggle" ? "modulation" : null;
    cancelIntent();
    if (open === null) {
      if (onHover) onHover();
      return;
    }
    if (opens === open) return;
    intentRef.current = setTimeout(() => {
      intentRef.current = null;
      // Something else already changed the open submenu (a click, a key).
      if (openLevelRef.current !== open) return;
      if (opens && onHover) onHover();
      else closeSubmenuLevel(open);
    }, ''' + str(HOVER_INTENT_MS) + ''');
  };
''',
    ),
    (
        "cursor repaints after every render",
        '''  React.useEffect(() => { paintCursor(); }, [cursorTick, macrosOpen, modulationOpen]);
''',
        '''  // After EVERY render: a toggle row that re-renders with its new value
  // (LFO 1 Off -> On) keeps the cursor it had.
  React.useEffect(() => { paintCursor(); });
''',
    ),
    (
        "a key supersedes a pending hover",
        '''    const onEscapeKey = (event) => {
      if (!event || event.metaKey || event.ctrlKey || event.altKey) return;
''',
        '''    const onEscapeKey = (event) => {
      if (!event || event.metaKey || event.ctrlKey || event.altKey) return;
      cancelIntent();
''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("band menu placement and hover intent already applied")
        return 0
    if encode("const __spectrBandMenuKeyboardNav = true;") not in raw:
        sys.exit("FAIL: apply patch_materialized_band_menu_keyboard_nav.py first")
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    # Every Item call site inside ContextMenu becomes a plain call. Scoped to
    # the component so no other `Item` in the document is touched.
    html_start = raw.index(encode("function ContextMenu("))
    html_end = raw.index(encode("window.ContextMenu = ContextMenu;"), html_start)
    body = raw[html_start:html_end]
    calls = body.count("React.createElement(Item, ")
    if calls < 20:
        sys.exit("FAIL: expected the band menu's Item call sites, found %d" % calls)
    body = body.replace("React.createElement(Item, ", "Item(")
    raw = raw[:html_start] + body + raw[html_end:]
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("band menu placement and hover intent applied (%d rows now plain calls)" % calls)
    return 0


if __name__ == "__main__":
    sys.exit(main())
