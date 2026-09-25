#!/usr/bin/env python3
"""Declare the band menu as the overlay its lifted submenus stack on.

The `Macros` and `Modulation` panels are `position: fixed` and returned as
SIBLINGS of the band menu in a fragment, so they do not descend from the menu
they belong to. Both carry `role="menu"`, which claims an overlay, and
`View::claim_overlay()` treats a claim that does not descend from the open
overlay as a rival: the band menu was popped, its `onDismiss` (`onClose`)
fired, and the whole menu unmounted the instant either submenu opened. Every
row behind both submenus was unreachable -- the `menu-absent` readings in
Spectr-standalone-band-menu-rows and the modulation-panel failures in
Spectr-standalone-band-menu-64.

`overlayParent` names the overlay a claim stacks on, by native widget id. The
band menu's own node is `ref.current`, mounted in the commit before either
submenu can open, and its `__pulpId` is the widget id the bridge speaks. When
the name cannot be read the prop is omitted, which is exactly the undeclared
claim -- never a guess.

The vendored runtime only forwards the declaration after
tools/patch_materialized_runtime_overlay_parent.py; the two scripts are one
change in two artifacts and both must be applied.

Raw-text surgery on the escaped document, never a JSON load/dump round trip.
Idempotent. Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
N = "\\n"

HELPER_ANCHOR = "  const macrosRef = React.useRef(null);" + N
HELPER = ("  // The lifted submenus stack on the band menu rather than replacing it;"
          + N + "  // see tools/patch_materialized_band_submenu_overlay_parent.py." + N
          + "  const submenuOverlayParent = () => ref.current && ref.current.__pulpId" + N
          + "    ? String(ref.current.__pulpId) : undefined;" + N)
EDITS = [
    ("submenu parent helper", HELPER_ANCHOR, HELPER + HELPER_ANCHOR),
    ("macros panel", "ref: macrosRef," + N,
     "ref: macrosRef," + N + "        overlayParent: submenuOverlayParent()," + N),
    ("modulation panel", "ref: modulationRef," + N,
     "ref: modulationRef," + N + "        overlayParent: submenuOverlayParent()," + N),
]
MARKER = "const submenuOverlayParent = () =>"


def main():
    raw = PATH.read_text(encoding="utf-8")
    if MARKER in raw:
        print("band submenus already declare their overlay parent")
        return 0
    for name, old, new in EDITS:
        count = raw.count(old)
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(old, new, 1)
    if raw.count("overlayParent: submenuOverlayParent()") != 2:
        sys.exit("FAIL: expected exactly two submenu declarations after patching")
    PATH.write_text(raw, encoding="utf-8")
    print("band submenus now stack on the band menu")
    return 0


if __name__ == "__main__":
    sys.exit(main())
