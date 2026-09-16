#!/usr/bin/env python3
"""Give the band context menu the keyboard and the dismissal every other
overlay in this document already has.

Both defects below are long-standing code that only became OBSERVABLE when the
SDK pin moved to v0.854.1: before that no view in the shipping tree carried a
context-menu handler, so a right-click never reached the band menu and nothing
it does could be seen. Neither is a regression.

DEFECT 1 -- AN OPEN MENU KILLED EVERY SHORTCUT. The app-level keydown guard
`overlayBlocksShortcut()` returns true for any node carrying
`data-spectr-overlay="true"` other than Settings, and the band menu carries it.
So while the menu was open S/L/B/F/G, 1-5, `m`, `a` and `6` all died -- `m`
included, which is implemented and correct and simply never ran. `cmd+,`
survived only because it is a native chord consumed before DOM dispatch, which
is why the menu looked like it broke "the letters" specifically.

The guard is not weakened, it is made accurate. Its purpose is to stop a
surface that is CONSUMING typing -- a text field, a modal -- from having its
keystrokes reinterpreted as global commands. A band context menu consumes no
typing: it has no text input and no keyboard navigation of its own. Exempting
it is the same judgement already recorded for Settings, applied to the other
overlay that does not type. Every other overlay stays blocked.

WHAT A SHORTCUT MEANS WHILE THE MENU IS OPEN. It keeps its global meaning; the
menu does not rescope it to the band that was right-clicked. The menu's own
EDIT MODE rows are themselves global and they PRINT these exact letters as
their hints, so a letter that did something other than the row it labels would
be a lie rendered on screen. `m` is the same story from the other side: the
menu carries separate "Mute / Unmute" (the band) and "Mute / Unmute selection"
(the selection) rows, and `m` is the selection one everywhere else, including
in the SHORTCUTS panel. Rescoping it to the right-clicked band would make one
key mean two things depending on invisible state.

The menu closes when a shortcut fires, which is what clicking the row it
labels already does (`Item`'s onClick calls the handler and then onClose). It
closes BEFORE the handler runs, so the menu is never re-rendered with a
changed edit mode while open -- that is deliberate, and it keeps this change
clear of the separate, unfixed layout defect in which the menu's box does not
grow to its content.

DEFECT 2 -- THE MENU OPTED OUT OF THE FRAMEWORK'S DISMISSAL. Unlike the
help-guide scrim, the Settings dialog, the pattern manager and every dropdown
in this same document, the menu declared neither `overlay: true` nor
`onDismiss`, and hand-rolled both dismissal paths instead: a document
`pointerdown` listener armed inside `setTimeout(..., 0)`, and its own `window`
keydown for Escape. The outside-press path was measured not to work -- the
listener did not arm on the first press at all, and on a later press it fired,
computed `would_close=YES`, and the menu still stayed. This adopts the pattern
the siblings use rather than adding a third one.

WHY THIS IS ALSO THE ESCAPE FIX INSIDE A DAW, WHICH LOOKED LIKE A SEPARATE
BUG. Escape worked in the offscreen harness and failed in Logic, which reads
like two defects and is one. `onDismiss` is the load-bearing half, and the
mechanism is measured rather than reasoned: with the menu open, the framework
answers Escape with `OverlayEscapeResult::overlay` BOTH before and after this
change -- it was dismissing the overlay slot correctly the whole time. What it
had no way to do was tell React, so `ctxMenu` stayed set and the menu stayed
on screen. The harness missed it because a script-bridge key dispatch reaches
the menu's own `window` keydown listener, which closed it; a PLUGIN host never
fans a key out to script at all and consumes Escape in the shared policy
before any JS could see it. So offline the hand-rolled listener was doing the
work, and in a DAW nothing was.

`overlay: true` is deliberately declared alongside `onDismiss` even though
`role="menu"` already makes the runtime claim the slot, which means the claim
itself was never the missing piece. It is kept because every sibling overlay
in this document declares both, and because depending on the role auto-claim
is an inference about runtime internals rather than a statement of intent.

NOT IN SCOPE: the menu's box is 380 wide by a hardcoded 376 tall whether it
holds 14 rows or 17, so the 5 rows that do not fit restack from the top of the
box and paint over the first 5. That is the doubled text, it is below this
document in the materialized-runtime layout of a runtime-created subtree, and
four candidate fixes were tested and disproven. It needs a minimal Pulp repro
and is deliberately untouched here.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous. Idempotent on the marker.
"""
import json
import os
import sys

PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrBandMenuKeysAndDismissal"

# ---------------------------------------------------------------- defect 1
# The guard's exemption list. `continue` rather than an early `return false`:
# a later node in the same sweep may be a real typing surface and must still
# block. Reads the attribute the menu already carried, so nothing new has to
# be rendered for the guard to recognise it.
_GUARD_OLD = ('        if (node.getAttribute("data-spectr-settings-panel") !== null) continue;\n'
              '        return true;\n')
_GUARD_NEW = ('        if (node.getAttribute("data-spectr-settings-panel") !== null) continue;\n'
              '        // The band context menu consumes no typing -- no text field, no\n'
              '        // keyboard navigation of its own -- so it is not the kind of\n'
              '        // surface this guard exists to protect. It blocked every bare\n'
              '        // letter while it was open, `m` included. Exempting it is the\n'
              '        // judgement already recorded for Settings above; every other\n'
              '        // overlay still blocks.\n'
              '        if (node.getAttribute("data-spectr-band-context-menu") !== null) continue;\n'
              '        return true;\n')

# The dismiss channel. The App owns the keydown and FilterBank owns the menu
# state, so the menu publishes its own close while it is mounted.
#
# Declared INSIDE onKey rather than beside it, which is load-bearing rather
# than stylistic: test_materialized_mute_selection.mjs lifts this handler's
# block out of this document by brace matching and evaluates it standalone,
# so a helper one scope up is simply not there and every key in that rig dies
# on a ReferenceError. Keeping the handler self-contained keeps it testable
# in isolation. That rig supplies a stub `window`, so the typeof check is
# what makes the call a no-op there rather than a second failure.
_HELPER_OLD = '      const k = typeof e.key === "string" ? e.key.toLowerCase() : e.key;\n'
_HELPER_NEW = ('      const k = typeof e.key === "string" ? e.key.toLowerCase() : e.key;\n'
               '      // Close the band menu when a shortcut takes effect, matching what\n'
               '      // clicking the row that prints the same letter already does.\n'
               '      // Called BEFORE the handler runs, so the menu is never\n'
               '      // re-rendered with a changed edit mode while it is open.\n'
               '      const closeBandMenu = () => {\n'
               '        const dismiss = window.spectrDismissBandMenu;\n'
               '        if (typeof dismiss === "function") dismiss();\n'
               '      };\n')

_MODE_OLD = '      if (modeKeys[k]) {\n        e.preventDefault();\n'
_MODE_NEW = '      if (modeKeys[k]) {\n        e.preventDefault();\n        closeBandMenu();\n'

_MUTE_OLD = '      if (k === "m") {\n        e.preventDefault();\n'
_MUTE_NEW = '      if (k === "m") {\n        e.preventDefault();\n        closeBandMenu();\n'

_ANALYZER_OLD = '      if (k === "a" || k === "6") {\n        e.preventDefault();\n'
_ANALYZER_NEW = ('      if (k === "a" || k === "6") {\n        e.preventDefault();\n'
                 '        closeBandMenu();\n')

# ---------------------------------------------------------------- defect 2
# Both hand-rolled listeners go. The pointerdown one was measured not to
# dismiss; the Escape one worked offline but cannot receive the key inside a
# DAW at all, which is the half the overlay claim fixes. What replaces them is
# the publication of `onClose` for the shortcut path above -- dismissal itself
# is now the framework's, through `overlay` + `onDismiss` on the root below.
_EFFECT_OLD = ('  const ref = React.useRef(null);\n'
               '  React.useEffect(() => {\n'
               '    const onDown = (e) => {\n'
               '      if (ref.current && !ref.current.contains(e.target)) onClose();\n'
               '    };\n'
               '    const onKey = (e) => {\n'
               '      if (e.key === "Escape") onClose();\n'
               '    };\n'
               '    const t = setTimeout(() => document.addEventListener("pointerdown", onDown), 0);\n'
               '    window.addEventListener("keydown", onKey);\n'
               '    return () => {\n'
               '      clearTimeout(t);\n'
               '      document.removeEventListener("pointerdown", onDown);\n'
               '      window.removeEventListener("keydown", onKey);\n'
               '    };\n'
               '  }, [onClose]);\n')
_EFFECT_NEW = ('  const ref = React.useRef(null);\n'
               '  // Outside-press and Escape dismissal are the framework\'s, claimed by\n'
               '  // `overlay` + `onDismiss` on the root below -- the same declaration the\n'
               '  // help-guide scrim, the Settings dialog and every dropdown in this\n'
               '  // document already make. The two hand-rolled listeners that used to\n'
               '  // live here are gone: the document pointerdown one did not dismiss,\n'
               '  // and the window keydown one could never receive Escape inside a DAW.\n'
               '  //\n'
               '  // What remains is the menu publishing its own close, because the\n'
               '  // keydown handler that needs it lives in the App and the menu state\n'
               '  // lives in FilterBank. The identity check on teardown keeps a menu\n'
               '  // that is unmounting from clearing a newer menu\'s hook.\n'
               '  const __spectrBandMenuKeysAndDismissal = true;\n'
               '  React.useEffect(() => {\n'
               '    window.spectrDismissBandMenu = onClose;\n'
               '    return () => {\n'
               '      if (window.spectrDismissBandMenu === onClose)\n'
               '        window.spectrDismissBandMenu = null;\n'
               '    };\n'
               '  }, [onClose]);\n')

# The claim itself. Placed after `aria-label`, matching the Settings dialog's
# ordering of the same five attributes. `onDismiss` is what the framework
# calls back so React state actually clears; without it the view would be
# dismissed natively while `ctxMenu` stayed set and the menu re-rendered.
_ROOT_OLD = ('      "data-spectr-band-context-menu": "true",\n'
             '      role: "menu",\n'
             '      "aria-label": "Band actions",\n')
_ROOT_NEW = ('      "data-spectr-band-context-menu": "true",\n'
             '      role: "menu",\n'
             '      "aria-label": "Band actions",\n'
             '      overlay: true,\n'
             '      onDismiss: onClose,\n')

PATCHES = [
    ("exempt the band menu from the shortcut guard", _GUARD_OLD, _GUARD_NEW),
    ("declare the band-menu dismiss helper", _HELPER_OLD, _HELPER_NEW),
    ("close the menu on an edit-mode key", _MODE_OLD, _MODE_NEW),
    ("close the menu on the group-mute key", _MUTE_OLD, _MUTE_NEW),
    ("close the menu on the analyzer key", _ANALYZER_OLD, _ANALYZER_NEW),
    ("replace the hand-rolled dismissal with the dismiss hook",
     _EFFECT_OLD, _EFFECT_NEW),
    ("claim the framework overlay on the menu root", _ROOT_OLD, _ROOT_NEW),
]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]

    if MARKER in html:
        print("patch_materialized_band_menu_keys_and_dismissal: already applied")
        return 0

    for name, old, _new in PATCHES:
        count = html.count(old)
        if count != 1:
            print("patch_materialized_band_menu_keys_and_dismissal: %s: patch "
                  "point occurs %d times" % (name, count), file=sys.stderr)
            return 1

    for _name, old, new in PATCHES:
        html = html.replace(old, new, 1)

    for name, _old, new in PATCHES:
        if html.count(new) != 1:
            print("patch_materialized_band_menu_keys_and_dismissal: %s did not "
                  "apply" % name, file=sys.stderr)
            return 1
    if MARKER not in html:
        print("patch_materialized_band_menu_keys_and_dismissal: marker missing",
              file=sys.stderr)
        return 1

    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("patch_materialized_band_menu_keys_and_dismissal: applied %d patches"
          % len(PATCHES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
