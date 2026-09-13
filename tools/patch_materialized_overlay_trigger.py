#!/usr/bin/env python3
"""Teach the vendored materialized runtime that `aria-haspopup` marks a trigger.

WHY THIS EXISTS

    `native-ui/materialized/runtime.js` is a CHECKED-IN copy of Pulp's
    materialized prop applier, and it is the thing that turns a React prop into
    a bridge call for the shipping editor.  Its ARIA arm already teaches the
    framework which element IS an overlay:

        case "role":        dialog|alertdialog|menu|listbox -> claimOverlay(id, true)
        case "aria-modal":  truthy                          -> claimOverlay(id, true)

    Both claim with consume=true, which is what makes a press outside an open
    menu close it WITHOUT also operating whatever sits under the press.

    Nothing taught it the other half: which element OPENS an overlay.  So a
    press on a second dropdown's trigger while the first is open is spent
    entirely on the dismissal and never reaches the trigger -- switching
    dropdowns costs two presses.  Measured on the live shipping tree, with
    Pulp's own policy verb, before this patch:

        views=418  overlay_trigger-marked=0
        bands menu open -> active_overlay consumes_outside_click=1
        press on the EDIT trigger:
            hit chain (7 ancestors): overlay_trigger=0 on every one
            route_press_to_active_overlay -> routing=dismissed consume_press=1

    `consume_press=1` is the host's instruction to stop before the ordinary hit
    test (window_host_mac.mm), so the EDIT trigger never sees that press.

    The pinned SDK HAS the capability on both sides of the seam:
    `setOverlayTrigger` is a registered bridge function and
    `OverlayDismissalPolicy::trigger_press_passes_through` (default true) makes
    `route_press_to_active_overlay` return consume_press=false when the press
    lands on a marked control or any of its ancestors.  The only thing missing
    is the arm that connects the ARIA the app already declares to that call.

    Spectr's four trigger sites all already declare it: the band-count button
    (`aria-haspopup="listbox"`), the help button (`"dialog"`), the settings
    Select (`"listbox"`), and every rail button built by `RailBtn`
    (`aria-haspopup={popupKind}` -> overflow/edit/analyzer/pattern).  No markup
    change is needed, and none is made: the fix is that the runtime stops
    ignoring what the markup says.

WHY AN ARM AND NOT A RE-COPY

    Re-copying runtime.js from the SDK would be a 1.6MB diff carrying every
    unrelated change between the vendored revision and today, on a file that is
    the shipping editor's entire behaviour.  A single case arm, written in this
    file's own dialect, is reviewable.  `call()` no-ops when the named bridge
    function is absent, so the arm degrades safely against an older SDK rather
    than throwing.

    The same arm is being added upstream in Pulp
    (`packages/pulp-react/src/prop-applier-events.ts`), so a future re-pin
    carries it natively and this patch becomes a no-op re-application.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 the patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

# The anchor is the LAST arm of the ARIA overlay group, so the new arm lands
# inside the same switch and reads next to the claim it is the counterpart of.
OLD = '''      case "aria-modal": {
        const truthy = value === true || value === "true" || value === "";
        if (truthy) {
          call("claimOverlay", id, true);
          return true;
        }
        return true;
      }
'''

NEW = '''      case "aria-modal": {
        const truthy = value === true || value === "true" || value === "";
        if (truthy) {
          call("claimOverlay", id, true);
          return true;
        }
        return true;
      }
      // `aria-haspopup` is the counterpart of the two arms above: they say
      // "this element IS a dismissable overlay", this one says "this control
      // OPENS one".  The overlay-dismissal policy needs both.  Without the
      // mark, a press on a second dropdown's trigger while the first is open
      // is consumed by the dismissal and never reaches the trigger, so
      // switching menus costs two presses instead of one.
      //
      // Scoped to triggers deliberately -- ordinary content stays consumed,
      // or clicking away from a menu would also operate whatever sits under
      // the click.  Any ARIA token other than absent/"false" marks
      // (true|menu|listbox|tree|grid|dialog); "false" unmarks, so a control
      // that stops offering a popup stops being a trigger.
      case "aria-haspopup": {
        const _popup = typeof value === "string" ? value.toLowerCase() : value;
        const _isTrigger = _popup === true
          || (typeof _popup === "string" && _popup !== "" && _popup !== "false");
        call("setOverlayTrigger", id, _isTrigger);
        return true;
      }
'''

MARKER = 'call("setOverlayTrigger", id, _isTrigger);'


def main():
    raw = open(PATH, encoding="utf-8").read()
    if raw.count(MARKER) >= 1:
        print("already applied  aria-haspopup overlay-trigger arm")
        print("no change needed")
        return 0
    count = raw.count(OLD)
    if count != 1:
        sys.exit("FAIL: patch point occurs %d times, expected 1" % count)
    raw = raw.replace(OLD, NEW, 1)
    if raw.count(MARKER) != 1:
        sys.exit("FAIL: the marker is not present exactly once after patching")
    if raw.count('case "aria-modal": {') != 1:
        sys.exit("FAIL: the anchor arm was duplicated")
    if raw.count('case "aria-haspopup": {') != 1:
        sys.exit("FAIL: the new arm is not present exactly once")
    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied          aria-haspopup overlay-trigger arm")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
