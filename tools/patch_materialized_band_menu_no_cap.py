#!/usr/bin/env python3
"""Do not cap the band menu panel: the cap squashes its rows.

Pulp does not scroll an overflow container. `overflow: scroll` is treated
like `hidden` (pulp view.hpp:1655), CSS `auto` maps to `hidden` in
style_visual_api.cpp, and a wheel over the panel was measured moving
nothing -- so `overflowY: "auto"` does not scroll, it CLIPS.

With a `maxHeight` on a flex column, Yoga's answer to content that does not
fit is to SHRINK the rows: pitch drops from 29px to ~20px, labels stop
matching their buttons, and rows overlap. Measured with EDIT MODE preserved
and the modulation submenu present: every reopen reported
`Mute / Unmute:unreachable`, and the all-macros case added nine
row-on-row `:overlap` readings. `flexShrink: 0` instead clips the tail --
five rows unreachable rather than one. Both are worse than not capping.

`menuMaxHeight` is KEPT for positioning and for the submenu's own height;
only the panel's CSS cap is removed. Re-cap once core Pulp can genuinely
scroll an overflow container.

Idempotent. Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
OLD = '''        maxHeight: menuMaxHeight,
        overflowY: "auto",
        overscrollBehavior: "contain",
'''
NEW = '''        // NO maxHeight / overflowY here: Pulp cannot scroll an overflow
        // container, so a cap does not scroll the rows, it makes Yoga shrink
        // them until labels no longer match their buttons and rows overlap.
        // See tools/patch_materialized_band_menu_no_cap.py.
'''


def main():
    document = json.loads(PATH.read_text())
    html = document["html"]
    if NEW in html and OLD not in html:
        print("band menu cap already removed")
        return 0
    if html.count(OLD) != 1:
        sys.exit("FAIL: panel cap anchor occurs %d times, expected 1" % html.count(OLD))
    document["html"] = html.replace(OLD, NEW, 1)
    PATH.write_text(json.dumps(document, separators=(",", ":"), ensure_ascii=False) + "\n")
    print("band menu panel no longer caps its height")
    return 0


if __name__ == "__main__":
    sys.exit(main())
