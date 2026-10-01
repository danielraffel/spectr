#!/usr/bin/env python3
"""Escape clears the band selection again, and the SHORTCUTS panel says so.

THE DEFECT

  Escape used to leave a band selection. The keyboard-policy change removed
  it because no surface named the key, which left no key at all for "clear
  the selection" -- the only way out was a press outside it.

THE FIX

  * Escape clears a standing band selection, in a plug-in and in the
    standalone alike. It is not a Musical Typing key and it is consumed only
    when there is a selection to clear, so an idle Escape still reaches the
    host -- which is why it is not behind the plain-key policy.
  * Whatever is open takes Escape first: the guard above already returns
    for any overlay that owns the keyboard (menus, dialogs), and the band
    context menu -- which that guard lets through on purpose -- closes on
    its own Escape, so this returns while it is mounted.
  * The SHORTCUTS panel lists "ESC  Clear selection" in every context.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once, refuses a
half-patched document, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrEscapeClearsSelection"

EDITS = [
    ('Escape clears a standing selection in every context',
     '      // Every key below is a plain-key global shortcut. Inside a plug-in\n'
     '      // they are the DAW\'s -- Musical Typing plays notes on these rows --\n'
     '      // unless the user turned on "Keyboard shortcuts in DAW". Returning\n'
     '      // WITHOUT preventDefault is what hands the key back to the host.\n'
     '      // (Escape no longer clears a selection -- the key was never shown\n'
     '      // anywhere -- which retires __spectrSelectionDeselectGestures; a press\n'
     '      // outside the selection still clears it.)\n'
     '      if (typeof globalThis.spectrPlainKeysActive === "function"\n'
     '          && !globalThis.spectrPlainKeysActive()) return;\n',
     '      // Escape clears a standing band selection (' + MARKER + '),\n'
     '      // in a plug-in and in the standalone alike: it is no Musical Typing\n'
     '      // key, and it is consumed only when there is a selection to clear,\n'
     '      // so an idle Escape still reaches the host. Whatever is open takes\n'
     '      // it first -- the guard above returned for any overlay that owns\n'
     '      // the keyboard, and the band menu (which that guard lets through)\n'
     '      // closes on its own Escape.\n'
     '      if (e.key === "Escape") {\n'
     '        if (typeof window.spectrDismissBandMenu === "function") return;\n'
     '        const bank = bankRef.current;\n'
     '        const had = bank && typeof bank.selectionSize === "function"\n'
     '          ? bank.selectionSize() : 0;\n'
     '        if (!had) return;\n'
     '        e.preventDefault();\n'
     '        bank.selectNone();\n'
     '        fireStatus("SELECTION CLEARED");\n'
     '        return;\n'
     '      }\n'
     '      // Every key below is a plain-key global shortcut. Inside a plug-in\n'
     '      // they are the DAW\'s -- Musical Typing plays notes on these rows --\n'
     '      // unless the user turned on "Keyboard shortcuts in DAW". Returning\n'
     '      // WITHOUT preventDefault is what hands the key back to the host.\n'
     '      if (typeof globalThis.spectrPlainKeysActive === "function"\n'
     '          && !globalThis.spectrPlainKeysActive()) return;\n'),

    ('the SHORTCUTS panel names Escape in every context',
     '/* @__PURE__ */ React.createElement(Hrow, { k: "DRAG SEL" }, "Group move"), ',
     '/* @__PURE__ */ React.createElement(Hrow, { k: "DRAG SEL" }, "Group move"), '
     '/* @__PURE__ */ React.createElement(Hrow, { k: "ESC" }, "Clear selection"), '),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 1:
        for label, old, new in EDITS:
            if raw.count(escaped(new)) != 1:
                sys.exit('FAIL %s: document is half patched' % label)
        print('already applied  Escape clears the band selection')
        return 0
    for label, old, new in EDITS:
        if raw.count(escaped(old)) != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, raw.count(escaped(old))))
    for label, old, new in EDITS:
        raw = raw.replace(escaped(old), escaped(new), 1)
        print('applied         ', label)
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
