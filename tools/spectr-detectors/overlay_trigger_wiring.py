#!/usr/bin/env python3
"""Assert the shipping artifacts wire every overlay opener to `setOverlayTrigger`.

Pulp's overlay-dismissal policy claims a popover with consume=true, so a press
outside an open menu closes it WITHOUT also operating whatever sits under the
press. `View::overlay_trigger()` is the counterpart mark: a press that lands on
a marked control is DELIVERED to it instead of being spent on the dismissal, so
switching from one dropdown to another costs one press.

Both halves are needed, and Spectr shipped with only the first. Measured on the
live shipping tree with Pulp's own policy verb:

    views=418  overlay_trigger-marked=0
    bands menu open -> active_overlay consumes_outside_click=1
    press on the EDIT trigger -> routing=dismissed consume_press=1

`consume_press=1` is the host's instruction to stop before the ordinary hit
test, so the trigger never saw the press and the user paid a second one.

This reads the CHECKED-IN artifacts rather than a capture, because the wiring is
a property of what ships, and because the runtime half has no layout node a
capture could look at. Three claims:

  ARM        `native-ui/materialized/runtime.js` carries an `aria-haspopup`
             arm that calls `setOverlayTrigger`. Without it the attribute is
             inert: it reads back fine in the DOM and never leaves JavaScript.

  TRIGGERS   every `data-spectr-menu-trigger` control in the document also
             declares `aria-haspopup`, which is what the arm keys on.

  SETTINGS   the `data-spectr-settings-open` gear declares it too. It opens a
             `role="dialog" aria-modal="true"` panel and sits immediately
             beside the Help button, which already declared it -- two adjacent
             buttons, same kind of surface, and only one of them cost one press.

The companion live-tree gates are the `switching native dropdowns costs one
press` / `dismissing a native dropdown over ordinary content still consumes`
cases in `test/test_native_state_parity.cpp`; they adjudicate the BEHAVIOUR
through `route_press_to_active_overlay`. This adjudicates the WIRING, which is
the part a renamed component or a re-pinned runtime can silently drop.

Each `--plant` reverts one claim to the shape that shipped before the fix. A
detector that still passes under a plant is measuring nothing, so a plant run
MUST fail.
"""
import argparse
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOC = os.path.join(REPO, "native-ui", "materialized",
                   "materialized-document.runtime.json")
RUNTIME = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

ARM = re.compile(r'case\s+"aria-haspopup"\s*:(?P<body>.*?)\n      \}', re.S)
CALL = 'call("setOverlayTrigger"'

# Each trigger declaration and the ~400 characters of props that follow it, so
# the `aria-haspopup` sibling is inside the window whichever order the props
# were authored in.
TRIGGER = re.compile(r'"data-spectr-menu-trigger":\s*(?:true|popupKind[^,]*),'
                     r'(?P<props>.{0,400})', re.S)
SETTINGS = re.compile(r'"data-spectr-settings-open":\s*true,(?P<props>.{0,400})',
                      re.S)

# Two menu-trigger sites are authored inline (the band-count button and the
# help button) and one is the shared `RailBtn`, which produces the other four
# at runtime. Three declarations is therefore the whole population; a different
# count means the component was restructured and this detector is aimed at the
# wrong surface.
EXPECTED_TRIGGER_DECLARATIONS = 3

PLANTS = {
    # The exact state that shipped: the attribute is declared, the runtime
    # ignores it. This is the plant that matters -- it restores the two-press
    # behaviour without breaking anything a syntax check would notice.
    "inert-arm": ("runtime",
                  ('call("setOverlayTrigger", id, _isTrigger);',
                   'return true;')),
    # A trigger that stops declaring what it opens. The band-count button is
    # authored on one line and the help button across several, so this names
    # the one-line spelling exactly rather than a shape that could drift.
    "silent-trigger": ("doc",
                       ('"data-spectr-menu-trigger": true, '
                        '"aria-haspopup": "listbox",',
                        '"data-spectr-menu-trigger": true,')),
    # The shared RailBtn, which produces four of the six triggers at runtime --
    # a separate control because the two inline buttons could stay correct
    # while the component that makes the majority of them regressed.
    "silent-railbtn": ("doc",
                       ('"aria-haspopup": popupKind || void 0,\n', '')),
    # The Settings gear reverted to the odd-one-out it was.
    "silent-settings": ("doc",
                        ('"data-spectr-settings-open": true,\n'
                         '      "aria-haspopup": "dialog",',
                         '"data-spectr-settings-open": true,')),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--doc", default=DOC)
    ap.add_argument("--runtime", default=RUNTIME)
    ap.add_argument("--plant", choices=sorted(PLANTS),
                    help="negative control: revert one claim to its pre-fix shape")
    args = ap.parse_args()

    document = json.load(open(args.doc, encoding="utf-8"))
    html = document.get("html")
    if not isinstance(html, str):
        sys.exit(f"{args.doc}: no html payload -- nothing to adjudicate")
    runtime = open(args.runtime, encoding="utf-8").read()

    if args.plant:
        which, (old, new) = PLANTS[args.plant]
        target = runtime if which == "runtime" else html
        if old not in target:
            sys.exit(f"plant {args.plant}: {old!r} is absent, so the control "
                     f"changed nothing and its verdict is meaningless")
        if which == "runtime":
            runtime = target.replace(old, new, 1)
        else:
            html = target.replace(old, new, 1)

    bad = []

    arm = ARM.search(runtime)
    if arm is None:
        bad.append("ARM: runtime.js has no `case \"aria-haspopup\"` arm, so "
                   "the attribute never reaches the bridge")
    elif CALL not in arm.group("body"):
        bad.append("ARM: the `aria-haspopup` arm does not call "
                   "setOverlayTrigger, so it is inert")
    else:
        print("control: runtime.js `aria-haspopup` arm calls setOverlayTrigger")

    triggers = TRIGGER.findall(html)
    # Positive control. Zero matches means the markup was restructured, not
    # that every trigger is wired -- reporting that as a pass is exactly how
    # this gate would go hollow.
    print(f"control: {len(triggers)} menu-trigger declaration(s) in the "
          f"shipping document")
    if len(triggers) != EXPECTED_TRIGGER_DECLARATIONS:
        sys.exit(f"FAIL: expected {EXPECTED_TRIGGER_DECLARATIONS} menu-trigger "
                 f"declarations, found {len(triggers)} -- the detector is "
                 f"measuring the wrong surface")
    for index, props in enumerate(triggers):
        if "aria-haspopup" in props:
            print(f"  menu trigger {index}: declares aria-haspopup")
        else:
            bad.append(f"menu trigger {index} declares no aria-haspopup, so it "
                       f"is never marked and switching to it costs two presses")

    gear = SETTINGS.findall(html)
    print(f"control: {len(gear)} settings-open declaration(s)")
    if len(gear) != 1:
        sys.exit(f"FAIL: expected 1 settings-open declaration, found "
                 f"{len(gear)} -- the detector is measuring the wrong surface")
    if "aria-haspopup" in gear[0]:
        print("  settings gear: declares aria-haspopup")
    else:
        bad.append("the settings gear declares no aria-haspopup, so opening "
                   "settings from an open menu costs two presses while its "
                   "immediate neighbour Help costs one")

    if bad:
        for line in bad:
            print("FAIL:", line, file=sys.stderr)
        return 1
    print("OK: every overlay opener is declared and the runtime arm wires it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
