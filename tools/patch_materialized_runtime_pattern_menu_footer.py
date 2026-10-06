#!/usr/bin/env python3
"""The preset menu's SAVE CURRENT / MANAGE footer sits below its last row.

THE DEFECT

    With the preset menu open, the bottom 8pt of AIR LIFT (4k+) -- the last
    factory row -- lay under the footer: its top 4pt pressed nothing, the next
    4pt pressed SAVE CURRENT. The all-controls tap-target sweep found it.

    The cause is not Pulp's layout. `native-ui/materialized/runtime.js` pins
    the open preset menu's geometry when it enters the captured "pattern"
    state: the popup at a fixed 334pt and the footer absolutely at 264pt, the
    numbers of the browser capture. The menu's rows have since become 30pt
    (menuItem's minHeight) on a 1pt gap, so eight factory rows under the 24pt
    FACTORY heading end 13pt below where the footer was pinned.

WHAT IT CHANGES

    The footer's top and the popup's height now follow the factory rows:
    H = 24 (FACTORY) + 31 per row (30pt row, 1pt gap). The footer goes 5pt
    below that (the 1pt gap plus its own 4pt margin, which the absolute
    offset does not include), the popup is H + 78pt tall (its 1pt borders and
    4pt padding, the footer's 63pt and margin), and it grows upward from the
    same bottom edge, 2pt above the trigger. For the eight factory rows that
    is the footer at 277 (was 264) and the popup 350pt (was 334), the height
    the same column has in a browser.

    NOT FIXED HERE: user presets. Their USER heading and rows come after
    AIR LIFT, inside the same pinned popup, and the footer covers them --
    measured before this patch with one user preset, the footer started 60pt
    above that preset's row's bottom. Making room for them means a taller
    popup past its 380pt maxHeight and, past a dozen presets, a scrolling
    list; the preset manager reaches them meanwhile.

Why a script and not a hand edit: runtime.js is a checked-in artifact. The
patch point is asserted to occur exactly once and a second run reports
"already applied".

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

MARKER = "const patternMenuContent ="

OLD = """      const patternReceipt = {
        popup: setBox(popup, 0, -336, 220, 334),
        footer: setBox(footer, 5, 264, 210, 63),
        save: setBox(save, 0, 3, 210, 28),
        manage: setBox(manage, 0, 33, 210, 28)
      };
"""

NEW = """      // The footer follows the factory rows rather than the capture's
      // offsets: a 24pt FACTORY heading and 31pt per row (menuItem's 30pt
      // minHeight on the 1pt gap). Pinned at the capture's 264 it covered the
      // last factory row's bottom 8pt once the rows were 30pt. The popup
      // grows upward from the same bottom edge.
      const patternFactoryRows = Array.isArray(globalThis.Spectr?.FACTORY_PATTERNS)
        ? globalThis.Spectr.FACTORY_PATTERNS.length
        : globalThis.document?.querySelectorAll?.(
            '[data-spectr-menu-root="pattern"] [data-spectr-pattern-menu-id^="factory:"]')?.length || 8;
      const patternMenuContent = 24 + 31 * patternFactoryRows;
      const patternPopupHeight = patternMenuContent + 78;
      const patternReceipt = {
        popup: setBox(popup, 0, -(patternPopupHeight + 2), 220, patternPopupHeight),
        footer: setBox(footer, 5, patternMenuContent + 5, 210, 63),
        save: setBox(save, 0, 3, 210, 28),
        manage: setBox(manage, 0, 33, 210, 28)
      };
"""


def main():
    raw = open(PATH, encoding="utf-8").read()
    if raw.count(MARKER) >= 1:
        if raw.count(NEW) != 1:
            sys.exit("FAIL: the pattern menu footer patch is present but not whole")
        print("already applied  pattern menu footer follows its rows")
        return 0
    count = raw.count(OLD)
    if count != 1:
        sys.exit("FAIL: pattern menu geometry patch point occurs %d times, expected 1" % count)
    raw = raw.replace(OLD, NEW, 1)
    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied          pattern menu footer follows its rows")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
