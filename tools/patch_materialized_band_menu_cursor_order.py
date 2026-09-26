#!/usr/bin/env python3
"""The keyboard cursor walks rows in on-screen order and skips disabled rows.

`levelRows()` took `document.querySelectorAll("button")` and kept those
inside the panel that were not `.disabled`. Measured on the standalone, both
halves were wrong in this runtime:

  * The query returns buttons in CREATION order, not tree order. The
    `Modulation ›` row is created after the rows around it, so the list read
    `... select-none | undo | redo | modulation-toggle | macros-toggle` and
    ArrowDown from `Select none` jumped to `Fit full range`, reaching
    `Modulation` only after `Redo`.
  * The shim never reflects `disabled` into the DOM (it goes straight to the
    native widget), so `.disabled` read false on every row and the cursor
    stopped on `Select none` and `Redo` while they were greyed out.

Rows are now collected by walking the panel's children in tree order, and a
disabled row says so with `aria-disabled="true"` -- the ARIA state for
exactly this, which assistive technology reads too -- and the cursor skips it.

Walking in tree order also makes the Modulation submenu's EDIT LFO 1 / EDIT
LFO 2 tabs cursor stops right after LFO 2, where they paint. Those tabs show
which one is selected with their own background, so the cursor brightens a
tab's border instead of repainting (and then clearing) that background.

Requires patch_materialized_band_menu_placement_and_intent.py.
Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const collectInTreeOrder = "
EDITS = [
    (
        "rows in tree order, disabled skipped",
        '''    const rows = [];
    const buttons = document.querySelectorAll("button");
    for (let i = 0; i < buttons.length; ++i)
      if (panel.contains(buttons[i]) && !buttons[i].disabled) rows.push(buttons[i]);
    return rows;
  };''',
        '''    // Buttons are identified by the query but ORDERED by a walk of the
    // panel's own children: the query answers in creation order, which is
    // not the order the rows paint in. `disabled` is not reflected into the
    // DOM here, so a disabled row is recognised by its `aria-disabled`.
    const buttons = new Set(document.querySelectorAll("button"));
    const rows = [];
    const collectInTreeOrder = (node) => {
      if (!node) return;
      if (buttons.has(node)) {
        const off = node.getAttribute && node.getAttribute("aria-disabled") === "true";
        if (!off) rows.push(node);
        return;
      }
      const kids = node.children || node.childNodes || [];
      for (let i = 0; i < kids.length; ++i) collectInTreeOrder(kids[i]);
    };
    collectInTreeOrder(panel);
    return rows;
  };''',
    ),
    (
        "rows state their disabled state",
        '''      "aria-haspopup": hasPopup,
      "data-spectr-band-action": action,''',
        '''      "aria-haspopup": hasPopup,
      "aria-disabled": disabled ? "true" : "false",
      "data-spectr-band-action": action,''',
    ),
    (
        "cursor never overwrites a control's own background",
        '''      levelRows(level).forEach((row, i) => {
        row.style.background = level === cursor.level && i === cursor.index
          ? cursorFill : "transparent";
      });''',
        '''      levelRows(level).forEach((row, i) => {
        const on = level === cursor.level && i === cursor.index;
        // A menu row's background IS the highlight. Any other stop -- the
        // EDIT LFO tabs -- paints its selected state with its own background,
        // so the cursor brightens the tab's border instead, and off-cursor
        // restores the border its `aria-pressed` state is authored with.
        const role = row.getAttribute ? String(row.getAttribute("role") || "") : "";
        const menuRow = role === "menuitem" || role === "menuitemcheckbox";
        if (menuRow) row.style.background = on ? cursorFill : "transparent";
        else {
          const pressed = row.getAttribute && row.getAttribute("aria-pressed") === "true";
          row.style.borderColor = on ? "rgba(170,215,255,0.95)"
            : pressed ? "rgba(180,210,255,0.4)" : "rgba(255,255,255,0.1)";
        }
      });''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("band menu cursor order already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("band menu cursor now walks on-screen order and skips disabled rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
