#!/usr/bin/env python3
"""Declare `aria-haspopup` on the Settings button, like every other Spectr trigger.

WHY THIS EXISTS

    Pulp's overlay-dismissal policy delivers a press that lands on a control
    marked `View::overlay_trigger()` to that control instead of spending it on
    closing the open overlay, so switching between two dismissable surfaces
    costs one press.  The mark comes from `aria-haspopup`
    (`tools/patch_materialized_overlay_trigger.py` wires the runtime arm, and
    the same arm is landing upstream in `@pulp/react`).

    Every Spectr control that opens an overlay already declares it -- the
    band-count button, the help button, the settings Select, and every rail
    button built by `RailBtn` -- with exactly one exception: the gear that
    opens the Settings dialog.  Measured in the live tree, the two buttons are
    immediate neighbours in the bottom rail and behave differently:

        __behavior_pr_8q  root(1234,819.5 -> 1260,845.5)  trigger=0   Settings
        __behavior_pr_8r  root(1274,819.5 -> 1300,845.5)  trigger=1   Help

    Both open a dismissable overlay; Help costs one press to switch to and
    Settings costs two.  The Settings dialog is `role="dialog"
    aria-modal="true"`, so `aria-haspopup="dialog"` is the accurate ARIA token
    and the one the Help button already uses for the same kind of surface.

    This is a markup consistency fix, not a behaviour invention: it says out
    loud what the button has always done.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 the patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# Several `before` spellings so the edit converges from any prior state.
VARIANTS = [
    ('"data-spectr-settings-open": true,\n'
     '      onClick: () => setSettingsOpen(true),\n'
     '      title: "Settings",'),
    ('"data-spectr-settings-open": true,\n'
     '      onClick: () => setSettingsOpen(true),'),
]
AFTER = ('"data-spectr-settings-open": true,\n'
         '      "aria-haspopup": "dialog",\n'
         '      "aria-expanded": settingsOpen,\n'
         '      onClick: () => setSettingsOpen(true),\n'
         '      title: "Settings",')
MARKER = ('"data-spectr-settings-open": true,\n'
          '      "aria-haspopup": "dialog",')


def escaped(value):
    """The document is one JSON line; patch its escaped text, not a re-dump.

    A `json.load` / `json.dump` round trip would rewrite every byte of an
    795 KB artifact for a three-line edit, so the diff would say nothing about
    what changed. Matching the escaped form leaves the rest of the file
    byte-identical.
    """
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()
    if raw.count(escaped(MARKER)) >= 1:
        print("already applied  Settings aria-haspopup")
        print("no change needed")
        return 0
    for variant in VARIANTS:
        if raw.count(escaped(variant)) == 1:
            raw = raw.replace(escaped(variant), escaped(AFTER), 1)
            break
    else:
        sys.exit("FAIL: no Settings-button variant matched exactly once")
    if raw.count(escaped(MARKER)) != 1:
        sys.exit("FAIL: the marker is not present exactly once after patching")
    if raw.count(escaped('"data-spectr-settings-open": true')) != 1:
        sys.exit("FAIL: the Settings button was duplicated")
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")
    if document["html"].count('"aria-haspopup": "dialog"') != 2:
        sys.exit("FAIL: expected exactly two dialog triggers (help + settings), "
                 "found %d" % document["html"].count('"aria-haspopup": "dialog"'))
    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied          Settings aria-haspopup")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
