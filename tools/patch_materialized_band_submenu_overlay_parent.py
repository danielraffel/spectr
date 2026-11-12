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

The same sibling boundary applies to pointer bubbling: a submenu press cannot
bubble through the root menu because the panels are separate native views.
Each lifted panel therefore owns pointerdown, pointerup, and click propagation
stops, matching the root menu's gesture isolation.

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
PARENT_EDITS = [
    ("submenu parent helper", HELPER_ANCHOR, HELPER + HELPER_ANCHOR),
    ("macros panel", "ref: macrosRef," + N,
     "ref: macrosRef," + N + "        overlayParent: submenuOverlayParent()," + N),
    ("modulation panel", "ref: modulationRef," + N,
     "ref: modulationRef," + N + "        overlayParent: submenuOverlayParent()," + N),
]
MARKER = "const submenuOverlayParent = () =>"

POINTER_ISOLATION = (
    "        onPointerDown: (event) => event.stopPropagation()," + N
    + "        onPointerUp: (event) => event.stopPropagation()," + N
    + "        onClick: (event) => event.stopPropagation()," + N
)
POINTER_MARKER = "        onPointerDown: (event) => event.stopPropagation(),"
POINTER_EDITS = [
    ("macros pointer isolation",
     "        overlayParent: submenuOverlayParent()," + N
     + "        onDismiss: () => closeSubmenuLevel(\\\"macros\\\")," + N,
     "        overlayParent: submenuOverlayParent()," + N
     + POINTER_ISOLATION
     + "        onDismiss: () => closeSubmenuLevel(\\\"macros\\\")," + N),
    ("modulation pointer isolation",
     "        overlayParent: submenuOverlayParent()," + N
     + "        onDismiss: () => closeSubmenuLevel(\\\"modulation\\\")," + N,
     "        overlayParent: submenuOverlayParent()," + N
     + POINTER_ISOLATION
     + "        onDismiss: () => closeSubmenuLevel(\\\"modulation\\\")," + N),
]


def main():
    raw = PATH.read_text(encoding="utf-8")
    changed = False
    if MARKER not in raw:
        for name, old, new in PARENT_EDITS:
            count = raw.count(old)
            if count != 1:
                sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
            raw = raw.replace(old, new, 1)
        changed = True
        print("band submenus now stack on the band menu")
    elif raw.count("overlayParent: submenuOverlayParent()") != 2:
        sys.exit("FAIL: expected exactly two submenu declarations")

    if raw.count(POINTER_MARKER) == 0:
        for name, old, new in POINTER_EDITS:
            count = raw.count(old)
            if count != 1:
                sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
            raw = raw.replace(old, new, 1)
        changed = True
        print("lifted submenu pointer gestures now stop at their own panels")
    elif raw.count(POINTER_MARKER) != 2:
        sys.exit("FAIL: expected exactly two lifted submenu pointer isolation handlers")

    if raw.count("overlayParent: submenuOverlayParent()") != 2:
        sys.exit("FAIL: expected exactly two submenu declarations after patching")
    if raw.count(POINTER_MARKER) != 2:
        sys.exit("FAIL: expected exactly two lifted submenu pointer isolation handlers")
    for name, _old, new in POINTER_EDITS:
        if raw.count(new) != 1:
            sys.exit("FAIL: %s declaration is missing or duplicated" % name)
    if changed:
        PATH.write_text(raw, encoding="utf-8")
    else:
        print("band submenus already declare parent and pointer isolation")
    return 0


if __name__ == "__main__":
    sys.exit(main())
