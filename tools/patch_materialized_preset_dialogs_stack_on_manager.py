#!/usr/bin/env python3
"""The Save and Delete preset dialogs stack on the preset manager instead of closing it.

`PatternSaveDialog` is rendered by the App as a SIBLING of `PatternManager`,
not inside it, but it is opened from the manager's own "Save current" action.
Both claim an overlay (`role="dialog"` + `aria-modal` on the scrim, `overlay`
on the panel). Since Pulp v0.873.0 a claim that does not descend from the open
overlay is treated as a different menu: the dialog's claim swept the manager
panel off the stack, its `onDismiss` (`onClose`) fired, and the manager closed
behind the dialog. The save still reached the processor, but the list it
should have appeared in was gone -- Spectr-preset-operations read zero user
rows and reported "could not save a preset". v0.857.1 did not sweep rivals,
which is why this surfaced only with the SDK pin.

The Delete confirmation has the same shape one level in: it is rendered inside
the manager's scrim but BESIDE the manager panel, and the panel is the top
claim when it opens, so the panel was swept, the manager closed, and DELETE
removed the preset with no confirmation ever shown (Spectr-preset-operations:
"DELETE did not ask for confirmation").

The fix names the manager panel -- the top claim when either dialog opens -- as
the overlay the dialog's scrim stacks on, by native widget id. When the manager
is not open the lookup finds nothing and the prop is omitted, which is exactly
the undeclared claim. Needs the vendored runtime to forward `overlayParent`
(tools/patch_materialized_runtime_overlay_parent.py).

Raw-text surgery on the escaped document, never a JSON load/dump round trip.
Idempotent. Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "data-spectr-dialog-stacks-on-manager"
PARENT = """      // Opened from the preset manager but not inside its panel, so it names
      // the panel as the overlay it stacks on; see
      // tools/patch_materialized_preset_dialogs_stack_on_manager.py.
      "data-spectr-dialog-stacks-on-manager": true,
      overlayParent: (() => {
        const panel = typeof document !== "undefined" && document.querySelector
          ? document.querySelector("[data-spectr-pattern-manager-panel]") : null;
        return panel && panel.__pulpId ? String(panel.__pulpId) : undefined;
      })(),
"""
EDITS = [
    ("save dialog", '''      "data-spectr-save-dialog": true,
'''),
    ("delete dialog", '''      "data-spectr-delete-dialog": true,
'''),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("preset dialogs already stack on the preset manager")
        return 0
    for name, anchor in EDITS:
        count = raw.count(encode(anchor))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(anchor), encode(anchor + PARENT), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("save and delete dialogs now stack on the preset manager")
    return 0


if __name__ == "__main__":
    sys.exit(main())
