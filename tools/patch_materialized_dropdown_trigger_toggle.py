#!/usr/bin/env python3
"""A press on an open dropdown's own trigger closes it.

THE DEFECT

  Clicking the trigger of the dropdown that is already open closed it and
  opened it again in the same press. Pulp's overlay policy dismisses the open
  menu on any outside press and, when the press lands on an overlay TRIGGER,
  lets it through to that trigger, so switching from one dropdown to another
  costs one press rather than two. The trigger it lets the press through to
  can be the open menu's OWN trigger. Its handler was a toggle on the LATEST
  state (`setOpen((v) => !v)`), and the dismissal had already queued
  "closed", so the toggle reopened the menu.

THE FIX (interim -- REPLACE with the Pulp primitive)

  The behaviour belongs in the Pulp SDK's overlay policy (a press that
  dismissed a trigger's own overlay does not also activate that trigger), and
  is being added there. Spectr builds against SDK 0.884, which does not have
  it, so until it does every Spectr dropdown goes through one helper:

  * each dropdown's `onDismiss` reports it (`spectrDropdownDismissed(name)`);
  * its trigger's click asks `spectrDropdownWasOpen(name, open)` -- open as
    rendered, or dismissed by this very press -- and closes the dropdown if
    so, opens it if not. It never toggles the latest state;
  * its trigger reports where the pointer is (`spectrDropdownTriggerEnter`
    / `spectrDropdownTriggerLeave`).

  "This very press": a dismissal of the same dropdown while the pointer is
  on its trigger (onPointerEnter / onPointerLeave), under 400 ms ago (a
  click's down-to-up). A press on the trigger is the only press that both
  dismisses the dropdown and reaches that trigger; a dismissal with the
  pointer elsewhere (a press elsewhere, a menu row that opened another
  popover) records nothing, nor does Escape's (any key forgets). Measured,
  and why nothing simpler works: the dismissal re-renders at once, so even
  the trigger's rendered state reads "closed" by the click; and the host
  runs promise jobs and 0 ms timers after every call into script, i.e.
  between the dismissal and the click. The trigger registers no
  pointerdown: one that does changes what Pulp's popup owner does on every
  press (it clicked a stale menu's trigger).

  A press on a DIFFERENT trigger still switches menus in one press.

  Covered here: the header's band count and the rail's overflow, EDIT MODE,
  ANALYZER, preset and help menus. The LENGTH menu, its Custom editor and
  the editor's Fraction list use the same helpers from
  patch_materialized_freeze_length_menu_v2.py, which rebuilds them. Settings
  has no dropdowns (ThemeDropdown / MetaphorDropdown are not mounted). Each
  trigger carries `data-spectr-dropdown="<name>"` so the replacement can
  find them all.

Why a script and not a hand edit: the shipping document is one minified line
and the materialized generator cannot rebuild it. Each patch point is asserted
to occur exactly once, a half-patched document is refused, and a second run
reports "already applied".

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "function spectrDropdownWasOpen(name, open)"

HELPER_ANCHOR = ('const { useState: useStateChrome, useEffect: useEffectChrome, '
                 'useRef: useRefChrome } = React;\n')
HELPER = r'''// ── A press on an open dropdown's own trigger closes it ─────────────────
// INTERIM -- DELETE ON THE SDK BUMP. Pulp's overlay dismissal
// (route_press_to_active_overlay) is gaining this rule itself: a press on
// the open overlay's own trigger dismisses it AND consumes the press, so
// the trigger never sees it. Spectr's SDK (0.884) predates that. Once the
// SDK has it, delete these helpers and go back to plain toggles on every
// `data-spectr-dropdown` trigger: with the native rule in place these never
// close anything a press did not already close (the handler only ever
// closes, never reopens-then-closes), but a record left by a consumed press
// would swallow a second click on the same trigger within 400 ms.
//
// Pulp dismisses the open dropdown on an outside press and lets a press on
// a TRIGGER through, so switching menus is one press. When that trigger is
// the open menu's own, its toggle reopened the menu the press had just
// closed: the dismissal commits at once, so by the time the click arrives
// (on mouse-up) the trigger itself renders "closed". So every dropdown
// records its dismissal (spectrDropdownDismissed) when the pointer is on its
// own trigger -- a press there is the only press that both dismisses the
// dropdown and reaches that trigger -- and the trigger's click asks
// spectrDropdownWasOpen: open as rendered, or dismissed by this press.
// A dismissal with the pointer anywhere else (a press elsewhere, a row that
// opened another popover) or right after a key (Escape) records nothing; a
// record is consumed by the next click on that trigger and lapses after
// 400 ms, a click's down-to-up. A promise job or a 0 ms timer would not do:
// the host runs both after every call into script, between the dismissal
// and the click. No pointerdown is involved: a trigger that registers one
// changes what Pulp's popup owner does on every press.
const SPECTR_DROPDOWN_PRESS_MS = 400;
function spectrDropdownToggle() {
  const g = globalThis;
  if (!g.__spectrDropdownToggle) {
    const s = { dismissed: "", at: 0, keyAt: -1e12, hovered: "", generation: 0 };
    g.__spectrDropdownToggle = s;
    if (typeof document !== "undefined" && document && document.addEventListener)
      document.addEventListener("keydown", () => { s.dismissed = ""; s.keyAt = Date.now(); }, true);
  }
  return g.__spectrDropdownToggle;
}
function spectrDropdownDismissed(name) {
  const s = spectrDropdownToggle();
  const pressed = s.hovered === name && Date.now() - s.keyAt >= 50;
  s.dismissed = pressed ? String(name || "") : "";
  s.at = Date.now();
  const generation = ++s.generation;
  if (pressed)
    setTimeout(() => { if (s.generation === generation) s.dismissed = ""; },
               SPECTR_DROPDOWN_PRESS_MS);
}
// Where the pointer is: on this trigger, or not. Leaving the trigger also
// drops its record: a press that leaves its target does not click it.
function spectrDropdownTriggerEnter(name) { spectrDropdownToggle().hovered = name; }
function spectrDropdownTriggerLeave(name) {
  const s = spectrDropdownToggle();
  if (s.hovered === name) s.hovered = "";
  if (s.dismissed === name) s.dismissed = "";
}
function spectrDropdownWasOpen(name, open) {
  const s = spectrDropdownToggle();
  const pressed = s.dismissed === name && Date.now() - s.at < SPECTR_DROPDOWN_PRESS_MS;
  s.dismissed = "";
  return !!open || pressed;
}
spectrDropdownToggle();
'''

EDITS = [
    # band count
    ('"data-spectr-menu-root": "bands", style: { position: "relative", marginRight: 4 } }, '
     '/* @__PURE__ */ React.createElement("button", { "data-spectr-menu-trigger": true, ',
     '"data-spectr-menu-root": "bands", style: { position: "relative", marginRight: 4 } }, '
     '/* @__PURE__ */ React.createElement("button", { "data-spectr-menu-trigger": true, '
     '"data-spectr-dropdown": "bands", '
     'onPointerEnter: () => spectrDropdownTriggerEnter("bands"), onPointerLeave: () => spectrDropdownTriggerLeave("bands"), ',
     "bands trigger"),
    ('onClick: () => setBandsMenu((v) => !v)',
     'onClick: () => setOpenMenu(spectrDropdownWasOpen("bands", bandsMenu) ? null : "bands")',
     "bands toggle"),
    # the rail's dropdowns
    ('React.createElement(RailBtn, { popupKind: "menu", onClick: () => setOverflowMenu((v) => !v), '
     'active: overflowMenu }',
     'React.createElement(RailBtn, { popupKind: "menu", dropdown: "overflow", '
     'onClick: (e, wasOpen) => setOpenMenu(wasOpen ? null : "overflow"), active: overflowMenu }',
     "overflow trigger"),
    ('React.createElement(RailBtn, { popupKind: "listbox", onClick: () => toggleMenu("edit"), '
     'active: editMenu }',
     'React.createElement(RailBtn, { popupKind: "listbox", dropdown: "edit", '
     'onClick: (e, wasOpen) => setOpenMenu(wasOpen ? null : "edit"), active: editMenu }',
     "edit trigger"),
    ('React.createElement(RailBtn, { popupKind: "listbox", onClick: () => toggleMenu("analyzer"), '
     'active: analyzerMenu }',
     'React.createElement(RailBtn, { popupKind: "listbox", dropdown: "analyzer", '
     'onClick: (e, wasOpen) => setOpenMenu(wasOpen ? null : "analyzer"), active: analyzerMenu }',
     "analyzer trigger"),
    ('React.createElement(RailBtn, { popupKind: "menu", onClick: () => setPatternMenu((v) => !v), '
     'active: patternMenu }',
     'React.createElement(RailBtn, { popupKind: "menu", dropdown: "pattern", '
     'onClick: (e, wasOpen) => setOpenMenu(wasOpen ? null : "pattern"), active: patternMenu }',
     "preset trigger"),
    ('function RailBtn({ children, onClick, active, popupKind, railAction }) {\n'
     '  const [flash, setFlash] = useStateChrome(false);\n'
     '  const handle = (e) => {\n'
     '    setFlash(true);\n'
     '    setTimeout(() => setFlash(false), 180);\n'
     '    onClick && onClick(e);\n',
     'function RailBtn({ children, onClick, active, popupKind, railAction, dropdown }) {\n'
     '  const [flash, setFlash] = useStateChrome(false);\n'
     '  const handle = (e) => {\n'
     '    setFlash(true);\n'
     '    setTimeout(() => setFlash(false), 180);\n'
     '    // A dropdown trigger passes whether its dropdown was open when the\n'
     '    // press started (spectrDropdownWasOpen).\n'
     '    onClick && onClick(e, dropdown ? spectrDropdownWasOpen(dropdown, active) : void 0);\n',
     "rail button"),
    ('      "data-spectr-menu-trigger": popupKind ? true : void 0,\n',
     '      "data-spectr-menu-trigger": popupKind ? true : void 0,\n'
     '      "data-spectr-dropdown": dropdown || void 0,\n'
     '      onPointerEnter: dropdown ? () => spectrDropdownTriggerEnter(dropdown) : void 0,\n'
     '      onPointerLeave: dropdown ? () => spectrDropdownTriggerLeave(dropdown) : void 0,\n',
     "rail button mark"),
    # help
    ('"data-spectr-menu-root": "help", style: { position: "relative" } }, '
     '/* @__PURE__ */ React.createElement(\n    "button",\n    {\n'
     '      "data-spectr-menu-trigger": true,\n',
     '"data-spectr-menu-root": "help", style: { position: "relative" } }, '
     '/* @__PURE__ */ React.createElement(\n    "button",\n    {\n'
     '      "data-spectr-menu-trigger": true,\n'
     '      "data-spectr-dropdown": "help",\n'
     '      onPointerEnter: () => spectrDropdownTriggerEnter("help"),\n'
     '      onPointerLeave: () => spectrDropdownTriggerLeave("help"),\n',
     "help trigger"),
    ('      onClick: () => setHelpOpen((v) => !v),\n',
     '      onClick: () => setHelpOpen(!spectrDropdownWasOpen("help", helpOpen)),\n',
     "help toggle"),
    # every dropdown reports its dismissal
    ('bandsMenu && /* @__PURE__ */ React.createElement("div", { "data-spectr-menu-options": true, '
     '"data-spectr-overlay": "true", overlay: true, onDismiss: () => setOpenMenu(null),',
     'bandsMenu && /* @__PURE__ */ React.createElement("div", { "data-spectr-menu-options": true, '
     '"data-spectr-overlay": "true", overlay: true, onDismiss: () => { '
     'spectrDropdownDismissed("bands"); setOpenMenu(null); },',
     "bands dismissal"),
    ('overflowMenu && /* @__PURE__ */ React.createElement("div", { "data-spectr-menu-options": true, '
     '"data-spectr-overlay": "true", overlay: true, onDismiss: () => setOpenMenu(null),',
     'overflowMenu && /* @__PURE__ */ React.createElement("div", { "data-spectr-menu-options": true, '
     '"data-spectr-overlay": "true", overlay: true, onDismiss: () => { '
     'spectrDropdownDismissed("overflow"); setOpenMenu(null); },',
     "overflow dismissal"),
    ('patternMenu && /* @__PURE__ */ React.createElement("div", { "data-spectr-menu-options": true, '
     '"data-spectr-overlay": "true", overlay: true, onDismiss: () => setOpenMenu(null),',
     'patternMenu && /* @__PURE__ */ React.createElement("div", { "data-spectr-menu-options": true, '
     '"data-spectr-overlay": "true", overlay: true, onDismiss: () => { '
     'spectrDropdownDismissed("pattern"); setOpenMenu(null); },',
     "preset dismissal"),
    ('"data-spectr-help-panel": true, "data-spectr-overlay": "true", overlay: true, '
     'onDismiss: onClose, role: "dialog", "aria-label": "Keyboard shortcuts"',
     '"data-spectr-help-panel": true, "data-spectr-overlay": "true", overlay: true, '
     'onDismiss: () => { spectrDropdownDismissed("help"); onClose(); }, role: "dialog", '
     '"aria-label": "Keyboard shortcuts"',
     "help dismissal"),
]

# The EDIT MODE and ANALYZER popovers declare the same overlay text; each is
# patched inside its own function.
POPOVER_DISMISS_OLD = 'onDismiss: onClose, role: "listbox"'
POPOVERS = [("function EditModePopover({ value, onChange, onClose }) {", "edit"),
            ("function AnalyzerPopover({ value, onChange, onClose }) {", "analyzer")]


def popover_dismiss(name):
    return ('onDismiss: () => { spectrDropdownDismissed("%s"); onClose(); }, role: "listbox"'
            % name)


def once(html, text, label):
    count = html.count(text)
    if count != 1:
        sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))


def main():
    raw = open(PATH, encoding="utf-8").read()
    document = json.loads(raw)
    html = document["html"]

    if MARKER in html:
        for _old, new, label in EDITS:
            if html.count(new) != 1:
                sys.exit("FAIL: the trigger toggle is present but its %s is not; "
                         "the document is half patched" % label)
        for _start, name in POPOVERS:
            if html.count(popover_dismiss(name)) != 1:
                sys.exit("FAIL: the trigger toggle is present but the %s popover's "
                         "dismissal is not; the document is half patched" % name)
        print("already applied  dropdown trigger toggle")
        return 0

    once(html, HELPER_ANCHOR, "chrome script head")
    for old, _new, label in EDITS:
        once(html, old, label)
    for start, name in POPOVERS:
        once(html, start, name + " popover")

    html = html.replace(HELPER_ANCHOR, HELPER_ANCHOR + HELPER, 1)
    for old, new, _label in EDITS:
        html = html.replace(old, new, 1)
    for start, name in POPOVERS:
        at = html.index(start)
        end = html.index("\n}\n", at)
        body = html[at:end]
        if body.count(POPOVER_DISMISS_OLD) != 1:
            sys.exit("FAIL %s popover: its overlay declaration occurs %d times, expected 1"
                     % (name, body.count(POPOVER_DISMISS_OLD)))
        html = html[:at] + body.replace(POPOVER_DISMISS_OLD, popover_dismiss(name), 1) + html[end:]

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
    print("applied          dropdown trigger toggle")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
