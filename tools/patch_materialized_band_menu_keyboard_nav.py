#!/usr/bin/env python3
"""Keyboard navigation for the band context menu and its two submenus.

WHAT THE MENU DOES AFTER THIS PATCH

  * Up / Down (and Home / End) move a visible highlight through the enabled
    rows of whichever level is open: the Macros or Modulation submenu when one
    is open, otherwise the band menu itself. The highlight is the same fill the
    pointer hover paints, and hovering a row moves the keyboard cursor there,
    so there is only ever one highlighted row.
  * Right on `Modulation ›` / `Macros ›` (or Return on either) opens that
    submenu with the cursor on its first row. Left goes back to the band menu,
    onto the entry that opened the submenu.
  * Return activates the highlighted row, exactly as a click would.
  * Escape inside a submenu closes only the submenu; Escape in the band menu
    closes it.
  * While the menu is open it owns the arrow keys. With no menu open the
    listener does not exist, so an arrow still reaches the DAW. Return with no
    highlighted row is not taken either.

WHY IT IS THE MENU'S AND NOT PULP'S POPUP DEFAULT

  Pulp's popup default (`__pulpPopupDefaultHandle__`) is a single-level roving
  cursor: it has no notion of Right opening a submenu or Left leaving one, and
  measured on the shipping standalone it never adopted this menu -- every
  arrow reached script and nothing was highlighted. Two keyboard owners on one
  menu step twice per press, so the menu and its panels opt out with
  `data-pulp-popup-default="off"`.

WHY ESCAPE NO LONGER CLOSES THE SUBMENU FROM SCRIPT

  Each submenu holds its own overlay claim stacked on the band menu (see
  tools/patch_materialized_band_submenu_overlay_parent.py), so the framework
  already retires exactly one layer per Escape: the standalone routes Escape to
  the overlay stack after the script fan-out, and the plugin editor routes it
  there INSTEAD of script. Closing the submenu from script as well made the
  standalone spend one press on two layers -- script unmounted the submenu,
  and the overlay route then popped the band menu that had become the top.
  Measured: one Escape inside Macros closed the whole menu. The submenus now
  close in their own `onDismiss`, which both hosts reach, and which also stops
  a popped submenu from staying painted with no claim behind it (the plugin
  path popped the claim but nothing told the panel).

Raw-text surgery on the escaped document: each edit is written as decoded
source and encoded with the document's own JSON string escaping, then matched
exactly once. Requires patch_materialized_band_submenu_overlay_parent.py.

Idempotent. Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const __spectrBandMenuKeyboardNav = true;"

CURSOR_BLOCK = '''  const modulationRef = React.useRef(null);
  // Keyboard cursor. One row of one level is current; `index` is into that
  // level's ENABLED rows in document order, so a disabled row is skipped
  // rather than landed on. Painted with the hover fill, and a hover moves it,
  // so the pointer and the keyboard never show two highlighted rows.
  const __spectrBandMenuKeyboardNav = true;
  const cursorRef = React.useRef({ level: "root", index: -1 });
  const [cursorTick, setCursorTick] = React.useState(0);
  const cursorFill = "rgba(120,180,255,0.14)";
  const levelPanel = (level) => level === "macros" ? macrosRef.current
    : level === "modulation" ? modulationRef.current : ref.current;
  const levelRows = (level) => {
    const panel = levelPanel(level);
    if (!panel || typeof panel.contains !== "function"
        || typeof document === "undefined" || !document.querySelectorAll) return [];
    const rows = [];
    const buttons = document.querySelectorAll("button");
    for (let i = 0; i < buttons.length; ++i)
      if (panel.contains(buttons[i]) && !buttons[i].disabled) rows.push(buttons[i]);
    return rows;
  };
  const paintCursor = () => {
    const cursor = cursorRef.current;
    for (const level of ["root", "macros", "modulation"]) {
      levelRows(level).forEach((row, i) => {
        row.style.background = level === cursor.level && i === cursor.index
          ? cursorFill : "transparent";
      });
    }
  };
  const moveCursor = (level, index) => {
    cursorRef.current = { level, index };
    paintCursor();
  };
  // A level that is about to mount has no rows yet, so the cursor is set now
  // and painted by the effect below once the panel has committed.
  const deferCursor = (level, index) => {
    cursorRef.current = { level, index };
    setCursorTick((tick) => tick + 1);
  };
  React.useEffect(() => { paintCursor(); }, [cursorTick, macrosOpen, modulationOpen]);
  const toggleAction = (level) => level === "macros" ? "macros-toggle" : "modulation-toggle";
  // Leaving a submenu puts the cursor back on the entry that opened it.
  const returnCursorFrom = (level) => {
    const rows = levelRows("root");
    const index = rows.findIndex((row) => row.getAttribute
      && row.getAttribute("data-spectr-band-action") === toggleAction(level));
    deferCursor("root", index);
  };
  // Closes ONE level. A panel's own dismissal also arrives when the OTHER
  // submenu opens and its claim sweeps this one off the stack; closing both
  // there would shut the submenu that is opening. The cursor only moves back
  // to the band menu if it was in the level that closed.
  const closeSubmenuLevel = (level) => {
    if (level === "macros") setMacrosOpen(false);
    else setModulationOpen(false);
    if (cursorRef.current.level === level) returnCursorFrom(level);
  };
  const enterSubmenu = (level) => {
    if (level === "macros") openMacros(true);
    else openModulation(true);
    deferCursor(level, 0);
  };
  const syncHover = (node) => {
    for (const level of ["root", "macros", "modulation"]) {
      const index = levelRows(level).indexOf(node);
      if (index >= 0) { moveCursor(level, index); return; }
    }
  };
'''

EDITS = [
    ("cursor state", "  const modulationRef = React.useRef(null);\n", CURSOR_BLOCK),
    (
        "keyboard handler",
        '''    const onEscapeKey = (event) => {
      if (!event || event.key !== "Escape") return;
      if (typeof event.preventDefault === "function") event.preventDefault();
      // One press retires one layer, so a user inside a submenu gets
      // back to the menu rather than losing both at once.
      if (macrosOpen || modulationOpen) {
        setMacrosOpen(false);
        setModulationOpen(false);
        return;
      }
      onClose();
    };''',
        '''    const onEscapeKey = (event) => {
      if (!event || event.metaKey || event.ctrlKey || event.altKey) return;
      const key = event.key;
      const consume = () => {
        if (typeof event.preventDefault === "function") event.preventDefault();
      };
      const submenu = macrosOpen ? "macros" : modulationOpen ? "modulation" : null;
      if (key === "Escape") {
        consume();
        // One press retires one layer. An open submenu is the top overlay
        // claim, and both hosts route Escape to that stack, which pops the
        // submenu and fires its onDismiss. Closing it here as well would let
        // the same press pop the band menu underneath too.
        if (!submenu) onClose();
        return;
      }
      // Keys go to the top level: the open submenu, else the band menu.
      const level = submenu || "root";
      const rows = levelRows(level);
      const cursor = cursorRef.current;
      const index = cursor.level === level && cursor.index < rows.length ? cursor.index : -1;
      const row = index >= 0 ? rows[index] : null;
      const opens = row && row.getAttribute
        ? row.getAttribute("data-spectr-band-action") : "";
      const opensLevel = level === "root" && opens === "macros-toggle" ? "macros"
        : level === "root" && opens === "modulation-toggle" ? "modulation" : null;
      if (key === "ArrowDown" || key === "ArrowUp" || key === "Home" || key === "End") {
        consume();
        const n = rows.length;
        if (!n) return;
        // The first press lands ON an edge rather than stepping past it.
        const next = key === "Home" ? 0 : key === "End" ? n - 1
          : index < 0 ? (key === "ArrowDown" ? 0 : n - 1)
          : key === "ArrowDown" ? (index + 1) % n : (index - 1 + n) % n;
        moveCursor(level, next);
        return;
      }
      if (key === "ArrowRight") {
        consume();
        if (opensLevel) enterSubmenu(opensLevel);
        return;
      }
      if (key === "ArrowLeft") {
        consume();
        if (submenu) closeSubmenuLevel(submenu);
        return;
      }
      if (key === "Enter" && row) {
        consume();
        if (opensLevel) enterSubmenu(opensLevel);
        else if (typeof row.click === "function") row.click();
      }
    };''',
    ),
    (
        "hover moves the cursor",
        '''      onMouseEnter: (e) => {
        if (!disabled) {
          e.currentTarget.style.background = "rgba(120,180,255,0.14)";
          if (onHover) onHover();
        }
      },''',
        '''      onMouseEnter: (e) => {
        if (!disabled) {
          syncHover(e.currentTarget);
          e.currentTarget.style.background = "rgba(120,180,255,0.14)";
          if (onHover) onHover();
        }
      },''',
    ),
    (
        "macros panel owns its dismissal",
        '''        ref: macrosRef,
        overlayParent: submenuOverlayParent(),
''',
        '''        ref: macrosRef,
        overlayParent: submenuOverlayParent(),
        onDismiss: () => closeSubmenuLevel("macros"),
        "data-pulp-popup-default": "off",
''',
    ),
    (
        "modulation panel owns its dismissal",
        '''        ref: modulationRef,
        overlayParent: submenuOverlayParent(),
''',
        '''        ref: modulationRef,
        overlayParent: submenuOverlayParent(),
        onDismiss: () => closeSubmenuLevel("modulation"),
        "data-pulp-popup-default": "off",
''',
    ),
    (
        "macros back row",
        'React.createElement(Item, { action: "macros-back", label: "\\u2039 Back", keepOpen: true, onClick: closeSubmenus }),',
        'React.createElement(Item, { action: "macros-back", label: "\\u2039 Back", keepOpen: true, onClick: () => closeSubmenuLevel("macros") }),',
    ),
    (
        "modulation back row",
        'React.createElement(Item, { action: "modulation-back", label: "‹ Back", keepOpen: true, onClick: closeSubmenus }),',
        'React.createElement(Item, { action: "modulation-back", label: "‹ Back", keepOpen: true, onClick: () => closeSubmenuLevel("modulation") }),',
    ),
    (
        "band menu opts out of the popup default",
        '''      "data-spectr-band-context-menu": "true",
''',
        '''      "data-spectr-band-context-menu": "true",
      "data-pulp-popup-default": "off",
''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("band menu keyboard navigation already applied")
        return 0
    if encode("const submenuOverlayParent = () =>") not in raw:
        sys.exit("FAIL: apply patch_materialized_band_submenu_overlay_parent.py first")
    for name, old, new in EDITS:
        old_raw, new_raw = encode(old), encode(new)
        count = raw.count(old_raw)
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(old_raw, new_raw, 1)
    json.loads(raw)  # the document must still parse; nothing is re-serialised
    PATH.write_text(raw, encoding="utf-8")
    print("band menu keyboard navigation applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
