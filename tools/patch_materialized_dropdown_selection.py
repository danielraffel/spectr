"""Make an open dropdown answer its own shortcut letters, and stop it showing
two selections at once.

Two defects, one control.

  1. With the EDIT MODE menu OPEN, none of the letters the rows advertise
     (S / L / B / F / G) did anything.  The global shortcut handler is guarded
     by `overlayBlocksShortcut()`, and every toolbar popover is mounted with
     `data-spectr-overlay="true"` -- so the guard is correctly true while a
     menu is open and the handler returns before reading the key.  Widening
     that guard is the wrong fix: it exists so a modal cannot be typed
     through, and the EDIT MODE menu is exactly the surface that should answer
     an EDIT MODE letter.  So the popover takes ownership of its own keys
     while it is open, and commits through `onChange` -- the identical path a
     mouse click takes, which publishes the mode, fires the status line, and
     closes the menu.  A letter now selects AND dismisses.

  2. Opening the menu lit TWO rows: the app's own selected row (LEVEL, say)
     plus a second highlight on the first row (SCULPT).  The second one is
     Pulp's popup owner painting its keyboard cursor.  It seeds that cursor
     from the list's ARIA selection, and these rows -- unlike the Settings
     `Select`, which does carry `aria-selected` and therefore lands the cursor
     on the row it should -- advertised no selection at all, so the cursor
     fell back to index 0.  Marking the rows is the honest fix and it is
     required for assistive technology regardless: a `role="listbox"` whose
     children are bare buttons has no selection to report.

     This alone collapses the two indicators onto ONE row under the currently
     pinned SDK.  Removing the pre-seeded cursor entirely -- so nothing is
     highlighted until the user hovers or presses an arrow -- is a change in
     `core/view/js/web-compat-document.js` and arrives with an SDK repin.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# The popover's own key owner.  It deliberately re-states the guard set the
# global handler uses rather than importing it: the global handler lives in a
# different component and a different closure, and the two surfaces are
# mutually exclusive by construction (this listener only exists while the menu
# is mounted, and the global handler is blocked for exactly that span), so
# there is never a window where both run.
EDIT_KEYS = '''function spectrEditModeShortcut(key) {
  const map = {
    s: "sculpt", l: "level", b: "boost", f: "flare", g: "glide",
    "1": "sculpt", "2": "level", "3": "boost", "4": "flare", "5": "glide"
  };
  return map[typeof key === "string" ? key.toLowerCase() : key] || null;
}
'''

POPOVER_HEAD = 'function EditModePopover({ value, onChange, onClose }) {\n'

# `onChange` is the whole commit: the Chrome call site passes
# `(v) => { setEditMode(v); setOpenMenu(null); }`, and that `setEditMode` is
# the publishing wrapper the app hands Chrome, so one call publishes the mode,
# fires the status line, and dismisses the menu -- exactly what a click does.
POPOVER_KEYS = '''  React.useEffect(() => {
    const onKey = (e) => {
      if (e.metaKey || e.ctrlKey || e.altKey || e.repeat || e.isComposing) return;
      const t = e.target;
      if (t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA"
                || t.tagName === "SELECT" || t.isContentEditable)) return;
      const mode = spectrEditModeShortcut(e.key);
      if (!mode) return;
      e.preventDefault();
      onChange(mode);
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onChange]);
'''

EDITS = [
    ('the popover owns the letters its rows advertise',
     POPOVER_HEAD,
     EDIT_KEYS + POPOVER_HEAD + POPOVER_KEYS),

    # `role="option"` is required for `aria-selected` to mean anything, and the
    # container is already `role="listbox"`, so the rows were the missing half
    # of a shape the document had otherwise declared.
    ('EDIT MODE rows report which one is selected',
     '        key: m.k,\n'
     '        "data-spectr-edit-mode": m.k,\n'
     '        onClick: () => onChange(m.k),\n',
     '        key: m.k,\n'
     '        role: "option",\n'
     '        "aria-selected": active ? "true" : "false",\n'
     '        "data-spectr-edit-mode": m.k,\n'
     '        onClick: () => onChange(m.k),\n'),

    ('ANALYZER rows report which one is selected',
     '        key: a.k,\n'
     '        "data-spectr-analyzer-mode": a.k,\n'
     '        onClick: () => onChange(a.k),\n',
     '        key: a.k,\n'
     '        role: "option",\n'
     '        "aria-selected": active ? "true" : "false",\n'
     '        "data-spectr-analyzer-mode": a.k,\n'
     '        onClick: () => onChange(a.k),\n'),
]

# A row inside a listbox that still reports no selection is the whole defect.
FORBIDDEN_AFTER = (
    '        key: m.k,\n        "data-spectr-edit-mode": m.k,\n',
    '        key: a.k,\n        "data-spectr-analyzer-mode": a.k,\n',
)
REQUIRED_AFTER = (
    'function spectrEditModeShortcut(key) {',
    'const mode = spectrEditModeShortcut(e.key);',
    '        role: "option",\n        "aria-selected": active ? "true" : "false",\n'
    '        "data-spectr-edit-mode": m.k,',
    '        role: "option",\n        "aria-selected": active ? "true" : "false",\n'
    '        "data-spectr-analyzer-mode": a.k,',
)


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for label, _old, new in EDITS:
        if not new:
            sys.exit('FAIL %s: empty replacement has no applied marker' % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) >= 1:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e)
        changed = True
        applied += 1
        print('applied         ', label)

    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change needed')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
