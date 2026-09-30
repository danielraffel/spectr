#!/usr/bin/env python3
"""The EDIT MODE rows lay out the same with or without their key badge.

THE DEFECT

  Each EDIT MODE row is an icon beside a text column. The text column held a
  header line (title, "· tagline", a spacer, and the S/L/B/F/G key badge) and
  the description below it. Neither the text column nor the header said how
  to lay out their children: the column was a bare inline <span> holding a
  flex header and a block description, and the native runtime decides how to
  flow such a span from what is inside it. The key badge -- a bordered flex
  box -- was the one descendant that made it stack the header above the
  description.

  Hiding the badge while plain-key shortcuts go to the DAW (the plug-in
  default) removed that box, and the runtime then laid the header and the
  description side by side: the title and tagline collapsed into a narrow
  middle column, wrapped and vertically centred against the description,
  which moved into a right-hand column.

THE FIX

  * The text column is an explicit flex column, so the description always
    sits below the header and spans the column, whatever the header holds.
  * The header is an explicit flex row, and the title and tagline never wrap.
  * The badge's box is always present. While the plain-key shortcuts go to
    the DAW it is an empty, invisible, aria-hidden box styled like the badge
    (and without the chip attribute), so the header line and the rows have
    the same geometry in a plug-in and in the standalone -- the visible badge
    is the only difference.

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

MARKER = "data-spectr-shortcut-placeholder"

EDITS = [
    ('the EDIT MODE row text column and header lay out explicitly',
     '/* @__PURE__ */ React.createElement("span", { style: { flex: 1 } }, '
     '/* @__PURE__ */ React.createElement("span", { style: { display: "flex", alignItems: "center", gap: 6, marginBottom: 3 } }, '
     '/* @__PURE__ */ React.createElement("span", { style: { letterSpacing: 1.5, fontWeight: 600 } }, m.label), '
     '/* @__PURE__ */ React.createElement("span", { style: { opacity: 0.5, fontSize: 9 } }, "\\xB7 ", m.tagline), '
     '/* @__PURE__ */ React.createElement("span", { style: { flex: 1 } }), '
     'keyboardPolicy.active && /* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": m.k, style: spectrShortcutChipStyle() }, m.hint)), ',
     # The text column: header above, description below and full width.
     '/* @__PURE__ */ React.createElement("span", { style: { flex: 1, minWidth: 0, display: "flex", flexDirection: "column", alignItems: "stretch" } }, '
     # The header line: one row, never wrapping, as tall as the badge.
     '/* @__PURE__ */ React.createElement("span", { style: { display: "flex", flexDirection: "row", alignItems: "center", gap: 6, marginBottom: 3, minHeight: 16 } }, '
     '/* @__PURE__ */ React.createElement("span", { style: { letterSpacing: 1.5, fontWeight: 600, whiteSpace: "nowrap", flexShrink: 0 } }, m.label), '
     '/* @__PURE__ */ React.createElement("span", { style: { opacity: 0.5, fontSize: 9, whiteSpace: "nowrap", flexShrink: 0 } }, "\\xB7 ", m.tagline), '
     '/* @__PURE__ */ React.createElement("span", { style: { flex: 1 } }), '
     # The badge's box is always there, so the header line does not depend
     # on whether the plain-key shortcuts are live. While they go to the DAW
     # it is an empty, invisible box of the badge's own size; only the live
     # badge carries the key and the chip attribute.
     'keyboardPolicy.active ? /* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": m.k, style: spectrShortcutChipStyle() }, m.hint) '
     ': /* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-placeholder": m.k, "aria-hidden": "true", style: { ...spectrShortcutChipStyle(), opacity: 0, borderColor: "transparent" } })), '),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 1:
        for label, old, new in EDITS:
            if raw.count(escaped(new)) != 1:
                sys.exit('FAIL %s: document is half patched' % label)
        print('already applied  EDIT MODE rows keep their layout without the badge')
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
