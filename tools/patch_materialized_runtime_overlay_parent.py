#!/usr/bin/env python3
"""Teach the vendored materialized runtime the `overlayParent` prop.

WHY THIS EXISTS

    `native-ui/materialized/runtime.js` is a CHECKED-IN copy of Pulp's
    `@pulp/react` prop-applier, and it is what turns the band menu's JSX into
    bridge calls.  Its overlay arms claim with two arguments only:

        call("claimOverlay", id, true);

    `View::claim_overlay()` nests a claim only when the claiming view DESCENDS
    from the open overlay.  The band menu's `Macros` and `Modulation` panels are
    `position: fixed` and returned as SIBLINGS of the menu in a fragment, so the
    parent-chain walk cannot see the relationship: the submenu's claim reads as
    a rival menu, the band menu is popped, its `onDismiss` fires, and the menu
    unmounts together with the submenu it just opened.  Every row behind either
    submenu is unreachable.

    Measured on the shipping standalone before this patch, with a console line
    in the root's `onDismiss`:

        item-click Macros keepOpen=true
        toggleMacros
        root-onDismiss macros=true mod=false

    The pinned SDK (v0.873.0) already has the whole capability on the native
    side: `claimOverlay(id, consume, parentId)` resolves the parent widget and
    `claim_overlay(stacks_on)` stops its sweep there.  Its own `@pulp/react`
    reads an `overlayParent` prop from the whole prop bag and passes it as the
    third argument.  The vendored copy predates that, so declaring the parent
    in the document -- as `data-overlay-parent`, which only the web-compat
    element path reads, or as `overlayParent` -- reached nothing.  That is why
    both earlier document-only attempts left the failure count unchanged.

WHAT IT CHANGES

    The same three edits the SDK made, in this file's dialect:

      * `applyEventProp` takes the prop bag, and every claiming arm (`overlay`,
        `role`, `aria-modal`) emits through one helper that appends the declared
        parent when there is one.  The third argument is omitted rather than
        passed empty, so an undeclared claim emits the exact call it always
        has.
      * A claim that names a parent claims with consume=false, so a press
        outside the whole nest walks past the submenu to the menu underneath
        and closes both, while a press on the menu's own rows closes only the
        submenu. (The SDK's own applier still passes true; the dismissal walk
        stops at the first consuming entry, which strands the parent open.)
      * `overlayParent` is a handled key that emits nothing of its own -- it
        qualifies the claim the other arms make.
      * `applyChangedProps` re-emits the claim when ONLY the declared parent
        moved, so a re-pointed submenu does not stay nested on the menu it
        left.

    The Settings arm's deferred claim is left alone: Settings is not a lifted
    submenu and declares no parent.

WHY AN ARM AND NOT A RE-COPY

    See tools/patch_materialized_runtime_hit_slop.py: a re-copy is a 1.6MB diff
    of unrelated behaviour on the file that is the shipping editor.  `call()`
    forwards extra arguments and the native binding ignores a third argument
    it does not know, so this also degrades to today's behaviour against an
    older SDK rather than throwing.

    tools/patch_materialized_band_submenu_overlay_parent.py writes the
    declaration into the band menu; the two scripts are one change in two
    artifacts and both must be applied.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

MARKER = "const emitOverlayClaim = () => {"

EDITS = [
    (
        "signature + claim helper",
        '''  function applyEventProp(id, key, value) {
    switch (key) {
''',
        '''  function applyEventProp(id, key, value, props) {
    // The overlay this view DECLARES it stacks on, read from the whole prop
    // bag so JSX key order cannot decide whether it reaches the claim. A
    // lifted submenu is a sibling of its menu, so without the name its claim
    // reads as a rival and dismisses the menu underneath.
    // A submenu that names its parent does NOT consume its outside press: the
    // dismissal walk stops at the first consuming entry, so a consuming
    // submenu would spend a press outside the whole nest on closing itself and
    // leave its menu open. Deferring lets the walk reach the parent, which
    // consumes a press outside it and routes one that lands on its own rows.
    const emitOverlayClaim = () => {
      const parent = props && props.overlayParent;
      if (typeof parent === "string" && parent !== "")
        call("claimOverlay", id, false, parent);
      else
        call("claimOverlay", id, true);
    };
    switch (key) {
''',
    ),
    (
        "overlay arm",
        '''          call("claimOverlay", id, true);
          return true;
        }
        call("releaseOverlay", id);
        return true;
      // ARIA modal/popup auto-overlay.''',
        '''          emitOverlayClaim();
          return true;
        }
        call("releaseOverlay", id);
        return true;
      // ARIA modal/popup auto-overlay.''',
    ),
    (
        "role arm",
        '''            call("releaseOverlay", id);
            return true;
          }
          call("claimOverlay", id, true);
          return true;
        }
        return true;
      }
      case "aria-modal": {
        const truthy = value === true || value === "true" || value === "";
        if (truthy) {
          call("claimOverlay", id, true);
          return true;
        }
        return true;
      }
''',
        '''            call("releaseOverlay", id);
            return true;
          }
          emitOverlayClaim();
          return true;
        }
        return true;
      }
      case "aria-modal": {
        const truthy = value === true || value === "true" || value === "";
        if (truthy) {
          emitOverlayClaim();
          return true;
        }
        return true;
      }
      // Qualifies the claim the arms above make and is read from the prop bag
      // by all of them, so it emits nothing of its own. The update path, where
      // the declaration moves without a claiming key moving with it, is in
      // applyChangedProps.
      case "overlayParent":
        return true;
''',
    ),
    (
        "applyOne passes the prop bag",
        '''    if (applyEventProp(id, key, value)) return;
''',
        '''    if (applyEventProp(id, key, value, props)) return;
''',
    ),
    (
        "re-claim when only the declared parent moved",
        '''    if (svgPathStrokeChanged) {
      applySvgPathStrokeState(id, newProps, true);
      mutated = true;
    }
    // Visual props hoisted out of `style`/`className` are DERIVED, not authored.''',
        '''    if (svgPathStrokeChanged) {
      applySvgPathStrokeState(id, newProps, true);
      mutated = true;
    }
    // An overlay's declared parent and the props that claim it are one
    // compound state. When only the declaration moved, no claiming arm runs,
    // so re-emit here; when a claiming key moved too, its own arm already
    // re-claimed with the current declaration.
    if (oldProps.overlayParent !== newProps.overlayParent &&
        oldProps.overlay === newProps.overlay &&
        oldProps.role === newProps.role &&
        oldProps["aria-modal"] === newProps["aria-modal"]) {
      const role = typeof newProps.role === "string" ? newProps.role.toLowerCase() : "";
      const modal = newProps["aria-modal"];
      if (newProps.overlay || role === "dialog" || role === "alertdialog" ||
          role === "menu" || role === "listbox" ||
          modal === true || modal === "true" || modal === "") {
        const parent = newProps.overlayParent;
        if (typeof parent === "string" && parent !== "")
          call("claimOverlay", id, false, parent);
        else
          call("claimOverlay", id, true);
        mutated = true;
      }
    }
    // Visual props hoisted out of `style`/`className` are DERIVED, not authored.''',
    ),
]


def main():
    raw = open(PATH, encoding="utf-8").read()
    if raw.count(MARKER) >= 1:
        print("already applied  overlayParent claim arms")
        print("no change needed")
        return 0
    for name, old, new in EDITS:
        count = raw.count(old)
        if count != 1:
            sys.exit("FAIL: patch point %r occurs %d times, expected 1" % (name, count))
        raw = raw.replace(old, new, 1)
        print("applied          " + name)
    if raw.count(MARKER) != 1:
        sys.exit("FAIL: the marker is not present exactly once after patching")
    if raw.count('call("claimOverlay", id, true);') != 3:
        # Settings' deferred claim, plus the undeclared branch of the helper and
        # of the update path; every other claiming arm routes through the helper.
        sys.exit("FAIL: unexpected number of bare two-argument claims remain")
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
