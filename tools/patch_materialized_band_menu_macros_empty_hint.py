#!/usr/bin/env python3
"""The Macros submenu says what to do when it has nothing to offer.

With no bands selected and no macro assigned, the Macros submenu rendered
only `‹ Back` and its `MACROS` header -- an empty panel that read as broken.
It now carries one disabled row saying how to get its actions.

Requires patch_materialized_band_menu_placement_and_intent.py (rows are plain
`Item(...)` calls). Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
OLD = '''        React.createElement(Divider, { label: "MACROS" }),
'''
NEW = '''        React.createElement(Divider, { label: "MACROS" }),
        !hasSel && !assigned.length && Item({
          action: "macros-empty", label: "Select bands to assign a macro",
          disabled: true, onClick: () => {}
        }),
'''
MARKER = 'action: "macros-empty"'


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("macros empty hint already applied")
        return 0
    count = raw.count(encode(OLD))
    if count != 1:
        sys.exit("FAIL: macros divider anchor occurs %d times, expected 1" % count)
    raw = raw.replace(encode(OLD), encode(NEW), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("macros submenu now explains itself when empty")
    return 0


if __name__ == "__main__":
    sys.exit(main())
