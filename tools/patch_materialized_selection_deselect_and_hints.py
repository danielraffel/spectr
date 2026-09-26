#!/usr/bin/env python3
"""Leaving a band selection is one gesture, and the menu shows its shortcuts.

With bands selected (Select all, or a Cmd-drag marquee) the only way out was
the band menu's `Select none`. Two ordinary gestures now do it:

  * Escape clears the selection when nothing else is open. The band menu is
    exempt from the global shortcut guard, so this bails while the menu is
    mounted -- its own Escape closes it first. The key is consumed only when
    there was a selection to clear, so an idle Escape still reaches the DAW.
  * A press on a band OUTSIDE the selection only clears the selection. It
    used to clear it and then run the ordinary band press, and a press that
    does not drag toggles that band's mute -- so "click away to deselect"
    silently muted a band. A press on a SELECTED band still drags the whole
    group, as before.

Found while proving the Escape half: `SettingsModal` is always mounted and
its Escape listener ignored `open`, so it consumed every Escape in the editor
-- measured as a plugin-path Escape with nothing open reporting
`script-consumed`, so a DAW never received the key. It now listens only
while Settings is open.

The band menu's `Select all` and `Select none` rows now carry the shortcuts
the global handler already implements (Cmd/Ctrl+A and Cmd/Ctrl+Shift+A), in
the same chip the Undo and Redo rows use.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "__spectrSelectionDeselectGestures"

EDITS = [
    (
        "escape clears a selection",
        '''      const k = typeof e.key === "string" ? e.key.toLowerCase() : e.key;
      // Close the band menu when a shortcut takes effect, matching what''',
        '''      const k = typeof e.key === "string" ? e.key.toLowerCase() : e.key;
      // Escape leaves a band selection. The band menu passes the guard above
      // on purpose, so while it is mounted its own Escape closes it and this
      // stays out of the way. Consumed only when there was something to
      // clear, so an idle Escape still reaches the host.
      if (e.key === "Escape") {
        const __spectrSelectionDeselectGestures = true;
        if (typeof window.spectrDismissBandMenu === "function") return;
        const bank = bankRef.current;
        const had = bank && typeof bank.selectionSize === "function"
          ? bank.selectionSize() : 0;
        if (!had) return;
        e.preventDefault();
        bank.selectNone();
        fireStatus("SELECTION CLEARED");
        return;
      }
      // Close the band menu when a shortcut takes effect, matching what''',
    ),
    (
        "a press outside the selection only deselects",
        '''    if (!selection.has(band)) {
      setSelection(/* @__PURE__ */ new Set());
    }''',
        '''    // With a selection standing, a press on a band outside it is "stop
    // selecting", not an edit: running the ordinary press here toggled that
    // band's mute on release. The press does nothing else, and the pointer
    // mode below is one no handler acts on.
    if (selection.size > 0 && !selection.has(band)) {
      setSelection(/* @__PURE__ */ new Set());
      pointerRef.current = { mode: "deselect" };
      if (onStatus) onStatus("SELECTION CLEARED");
      return;
    }''',
    ),
    (
        "select all shows its shortcut",
        '''Item({ action: "select-all", label: "Select all", onClick: onSelectAll }),''',
        '''Item({ action: "select-all", label: "Select all", hint: "Cmd+A", onClick: onSelectAll }),''',
    ),
    (
        "select none shows its shortcut",
        '''Item({ action: "select-none", label: "Select none", onClick: onSelectNone, disabled: !hasSel }),''',
        '''Item({ action: "select-none", label: "Select none", hint: "Cmd+Shift+A", onClick: onSelectNone, disabled: !hasSel }),''',
    ),
    (
        "settings takes Escape only while open",
        '''  React.useLayoutEffect(() => {
    const onKey = (event) => {
      if (event.key === "Escape" && !document.querySelector('[data-pulp-popup-active]')) {
        event.preventDefault();
        event.stopPropagation();
        onClose();
      }
    };
    document.addEventListener("keydown", onKey, true);
    return () => document.removeEventListener("keydown", onKey, true);
  }, [onClose]);''',
        '''  React.useLayoutEffect(() => {
    // Settings stays mounted while closed (`open` toggles it), so an
    // unconditional listener consumed EVERY Escape in the editor: a DAW never
    // received one, and the key could not clear a band selection.
    if (!open) return undefined;
    const onKey = (event) => {
      if (event.key === "Escape" && !document.querySelector('[data-pulp-popup-active]')) {
        event.preventDefault();
        event.stopPropagation();
        onClose();
      }
    };
    document.addEventListener("keydown", onKey, true);
    return () => document.removeEventListener("keydown", onKey, true);
  }, [onClose, open]);''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("selection deselect gestures already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("selection deselect gestures and menu shortcut hints applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
