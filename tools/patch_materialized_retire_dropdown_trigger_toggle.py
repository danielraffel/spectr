#!/usr/bin/env python3
"""Retire the interim dropdown trigger-toggle helpers; Pulp owns the rule.

WHY

  tools/patch_materialized_dropdown_trigger_toggle.py (and the LENGTH /
  Fraction rebuild in patch_materialized_freeze_length_menu_v2.py) worked
  around Pulp's overlay dismissal letting a press on the OPEN dropdown's own
  trigger through to that trigger, whose toggle then reopened the menu the
  press had just closed. Every dropdown recorded its dismissal when the
  pointer was on its trigger, and the trigger's click asked
  `spectrDropdownWasOpen` instead of reading its rendered state.

  Pulp SDK 0.895.1 does this natively (Pulp's overlay anchor rule): a press
  on the trigger that opened an overlay dismisses it and is spent on the
  close, so the trigger's click never runs. A press on a DIFFERENT trigger
  still switches menus in one press.

  With the native rule in place the helpers are not merely dead: a press the
  SDK consumed still left the dismissal record behind, so a second click on
  the same trigger within 400 ms read "was open" and closed an already-closed
  dropdown -- the user's re-open was swallowed.

WHAT IT CHANGES

  * removes the helper block from the chrome script;
  * each trigger toggles on its rendered state again
    (`setFractionOpen(!fractionOpen)`, `bandsMenu ? null : "bands"`, ...);
  * drops the pointer enter/leave reporting and the dismissal reporting.

  The `data-spectr-dropdown` marks stay: they name each dropdown's trigger
  for tests and tooling and change no behaviour.

Why a script and not a hand edit: the shipping document is one minified line.
Each patch point is asserted to occur exactly once, a half-retired document is
refused, and a second run reports "already applied".

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

HELPER_START = "// ── A press on an open dropdown's own trigger closes it ─────────────────\n"
HELPER_END = "\nspectrDropdownToggle();\n"

EDITS = [
    # Fraction (LENGTH Custom editor)
    ('          onPointerEnter: () => spectrDropdownTriggerEnter("fraction"),\n'
     '          onPointerLeave: () => spectrDropdownTriggerLeave("fraction"),\n', '',
     "fraction pointer"),
    ('setFractionOpen(!spectrDropdownWasOpen("fraction", fractionOpen));',
     'setFractionOpen(!fractionOpen);', "fraction toggle"),
    ('onDismiss: () => { spectrDropdownDismissed("fraction"); setFractionOpen(false); },',
     'onDismiss: () => setFractionOpen(false),', "fraction dismissal"),
    # LENGTH
    ('      onPointerEnter: () => spectrDropdownTriggerEnter("length"),\n'
     '      onPointerLeave: () => spectrDropdownTriggerLeave("length"),\n', '',
     "length pointer"),
    ('      // Closes whatever it opened -- the menu or the Custom editor -- when\n'
     '      // either was open as this press began (spectrDropdownWasOpen).\n'
     '      onClick: () => {\n'
     '        if (spectrDropdownWasOpen("length", menuOpen || editorOpen)) {\n',
     '      // Closes whatever it opened -- the menu or the Custom editor. A press\n'
     '      // on this trigger while the overlay it opened is up is spent on\n'
     '      // Pulp\'s dismissal and never reaches this handler.\n'
     '      onClick: () => {\n'
     '        if (menuOpen || editorOpen) {\n',
     "length toggle"),
    ('onDismiss: () => { spectrDropdownDismissed("length"); onClose(false); },',
     'onDismiss: () => onClose(false),', "length editor dismissal"),
    ('onDismiss: () => { spectrDropdownDismissed("length"); setMenuOpen(false); },',
     'onDismiss: () => setMenuOpen(false),', "length menu dismissal"),
    # bands
    ('onPointerEnter: () => spectrDropdownTriggerEnter("bands"), '
     'onPointerLeave: () => spectrDropdownTriggerLeave("bands"), ', '',
     "bands pointer"),
    ('setOpenMenu(spectrDropdownWasOpen("bands", bandsMenu) ? null : "bands")',
     'setOpenMenu(bandsMenu ? null : "bands")', "bands toggle"),
    ('onDismiss: () => { spectrDropdownDismissed("bands"); setOpenMenu(null); },',
     'onDismiss: () => setOpenMenu(null),', "bands dismissal"),
    ('onDismiss: () => { spectrDropdownDismissed("overflow"); setOpenMenu(null); },',
     'onDismiss: () => setOpenMenu(null),', "overflow dismissal"),
    ('onDismiss: () => { spectrDropdownDismissed("pattern"); setOpenMenu(null); },',
     'onDismiss: () => setOpenMenu(null),', "preset dismissal"),
    # rail buttons
    ('    // A dropdown trigger passes whether its dropdown was open when the\n'
     '    // press started (spectrDropdownWasOpen).\n'
     '    onClick && onClick(e, dropdown ? spectrDropdownWasOpen(dropdown, active) : void 0);\n',
     '    // A dropdown trigger passes whether its dropdown is open.\n'
     '    onClick && onClick(e, dropdown ? !!active : void 0);\n',
     "rail button"),
    ('      onPointerEnter: dropdown ? () => spectrDropdownTriggerEnter(dropdown) : void 0,\n'
     '      onPointerLeave: dropdown ? () => spectrDropdownTriggerLeave(dropdown) : void 0,\n', '',
     "rail button pointer"),
    ('onDismiss: () => { spectrDropdownDismissed("edit"); onClose(); }, role: "listbox"',
     'onDismiss: onClose, role: "listbox"', "edit popover dismissal"),
    ('onDismiss: () => { spectrDropdownDismissed("analyzer"); onClose(); }, role: "listbox"',
     'onDismiss: onClose, role: "listbox"', "analyzer popover dismissal"),
    # help
    ('      onPointerEnter: () => spectrDropdownTriggerEnter("help"),\n'
     '      onPointerLeave: () => spectrDropdownTriggerLeave("help"),\n', '',
     "help pointer"),
    ('setHelpOpen(!spectrDropdownWasOpen("help", helpOpen))',
     'setHelpOpen(!helpOpen)', "help toggle"),
    ('onDismiss: () => { spectrDropdownDismissed("help"); onClose(); }, role: "dialog"',
     'onDismiss: onClose, role: "dialog"', "help dismissal"),
]

NAMES = ("spectrDropdownWasOpen", "spectrDropdownDismissed",
         "spectrDropdownTriggerEnter", "spectrDropdownTriggerLeave",
         "spectrDropdownToggle", "SPECTR_DROPDOWN_PRESS_MS")


def main():
    raw = open(PATH, encoding="utf-8").read()
    document = json.loads(raw)
    html = document["html"]

    if not any(name in html for name in NAMES):
        print("already applied  retire dropdown trigger toggle")
        return 0

    start = html.find(HELPER_START)
    if start < 0 or html.count(HELPER_START) != 1:
        sys.exit("FAIL helper block: start marker occurs %d times, expected 1"
                 % html.count(HELPER_START))
    end = html.find(HELPER_END, start)
    if end < 0:
        sys.exit("FAIL helper block: no end marker after its start")
    html = html[:start] + html[end + len(HELPER_END):]

    for old, new, label in EDITS:
        count = html.count(old)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))
        html = html.replace(old, new, 1)

    left = [name for name in NAMES if re.search(r"\b%s\b" % name, html)]
    if left:
        sys.exit("FAIL: the document still names %s after retirement" % ", ".join(left))

    document["html"] = html
    # Keep every byte outside the html payload as it was, and encode the html
    # the way tools/git/merge_materialized_runtime.py does (ensure_ascii=False).
    head, tail = raw.split('"html":"', 1)
    rest = tail.split('","mime_type":', 1)
    if len(rest) != 2:
        sys.exit("FAIL: the html payload is not followed by mime_type")
    out = (head + '"html":' + json.dumps(html, ensure_ascii=False)
           + ',"mime_type":' + rest[1])
    if json.loads(out) != document:
        sys.exit("FAIL: re-encoding the document changed more than its html")
    open(PATH, "w", encoding="utf-8").write(out)
    print("applied          retire dropdown trigger toggle")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
