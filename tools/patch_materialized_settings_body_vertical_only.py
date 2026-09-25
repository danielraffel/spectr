#!/usr/bin/env python3
"""Settings scrolls vertically only.

Since Pulp v0.873.0 the Settings body is a native ScrollView (vertical, and
its wheel handler zeroes horizontal deltas) wrapping the authored body
element, which keeps its own `overflowY: "auto"`. The runtime maps every
overflow axis to one `setOverflow`, so that inner element became a plain
overflow container on BOTH axes: its content is 110px wider than its box, so
the sideways component of a two-finger trackpad scroll moved the whole panel
left and clipped every label. Measured on the standalone: one sideways wheel
put the body at scroll_offset_x 110 = its max. Worse, because it claimed the
wheel it also swallowed vertical wheels before the ScrollView could scroll.

`overflowX: "hidden"` after `overflowY` makes the inner element a pure clip:
it no longer scrolls or claims the wheel, and the native ScrollView above it
-- which the authored `overflowY` still requests -- owns vertical scrolling.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
OLD = '"data-spectr-settings-body": true, style: { flex: 1, minHeight: 0, overflowY: "auto", marginRight: -20, paddingRight: 38 }'
NEW = '"data-spectr-settings-body": true, style: { flex: 1, minHeight: 0, overflowY: "auto", overflowX: "hidden", marginRight: -20, paddingRight: 38 }'


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(NEW) in raw:
        print("settings body already scrolls vertically only")
        return 0
    count = raw.count(encode(OLD))
    if count != 1:
        sys.exit("FAIL: settings body anchor occurs %d times, expected 1" % count)
    raw = raw.replace(encode(OLD), encode(NEW), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("settings body now scrolls vertically only")
    return 0


if __name__ == "__main__":
    sys.exit(main())
