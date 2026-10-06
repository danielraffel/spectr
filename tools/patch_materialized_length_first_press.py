#!/usr/bin/env python3
"""Every LENGTH row, Fraction row and Custom editor control takes the first press.

THE DEFECT

  Choosing a LENGTH sometimes took a second press. Every Pulp host fires the
  click on mouse-up only when the tree's hit test at the release point finds
  the very view the press went to (deliver_mouse_up's same-view rule). Two
  things broke that, measured through the plug-in host's press route
  (test "every LENGTH row, Fraction row and Custom editor button takes the
  first press", 112 of 165 presses missed before this patch):

  * Below ~534pt the hit test reaches the LENGTH menu only through the
    header wrappers' downward reach (hitSlop "0 0 860 0"). That reach is the
    trigger's 88pt width, and the menu is 146pt wide, so on 15/16 bar, 1, 2,
    4 and 8 bars and Custom length... the right part of the row took the
    press (routed through the open overlay) and the release resolved to the
    editor root: no click. The reach now also covers the menu, the Custom
    editor and its Fraction list sideways ("0 160 860 60": the editor hangs
    38pt left of the trigger and ends 110pt right of it).

  * A row's check mark and caption, the Fraction trigger's text and chevron
    and the Bars steppers' chevrons are views of their own. A press on the
    caption that slipped 1-3pt off it before the release (or the reverse)
    resolved to two different views, and the click was dropped. They are
    decoration, so they no longer take the pointer (pointerEvents: "none"),
    and the press and the release both resolve to the row or button.

  The same-view rule itself is Pulp's (a W3C click goes to the nearest
  common ancestor of the press and release targets); until it changes there,
  decoration inside a pressable control must not be hit-testable.

Why a script and not a hand edit: the shipping document is one minified line
and the materialized generator cannot rebuild it. Each patch point is asserted
to occur exactly once, a half-patched document is refused, and a second run
reports "already applied". Needs patch_materialized_freeze_length_menu_v2.py
applied first.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "// Decoration inside a LENGTH control never takes the pointer"
PREREQUISITE = "function SpectrLengthFractionList("

EDITS = [
    ('const SPECTR_LENGTH_REACH = "0 0 860 0";\n',
     '// Down the whole editor, and sideways over the menu (146pt), the Custom\n'
     '// editor (from 38pt left of the trigger to 110pt right of it) and its\n'
     '// Fraction list: a release there must find the row it was pressed on.\n'
     'const SPECTR_LENGTH_REACH = "0 160 860 60";\n',
     "LENGTH reach"),
    ('function SpectrLengthCheck({ on }) {\n'
     '  // A drawn check: the bound mono face covers Basic Latin only.\n'
     '  return /* @__PURE__ */ React.createElement("svg", {\n'
     '    width: 10, height: 10, viewBox: "0 0 10 10", "aria-hidden": true,\n'
     '    style: { flex: "none", opacity: on ? 1 : 0 }\n',
     MARKER + ': a click fires\n'
     '// only when the release finds the view the press went to, so a check, a\n'
     '// caption or a chevron that took the pointer turned a 1-3pt slip off it\n'
     '// into a dropped click.\n'
     'const SPECTR_LENGTH_DECORATION = { pointerEvents: "none" };\n'
     'function SpectrLengthCheck({ on }) {\n'
     '  // A drawn check: the bound mono face covers Basic Latin only.\n'
     '  return /* @__PURE__ */ React.createElement("svg", {\n'
     '    width: 10, height: 10, viewBox: "0 0 10 10", "aria-hidden": true,\n'
     '    style: { flex: "none", opacity: on ? 1 : 0, pointerEvents: "none" }\n',
     "check mark"),
    ('  }, /* @__PURE__ */ React.createElement(SpectrLengthCheck, { on: selected }),\n'
     '     /* @__PURE__ */ React.createElement("span", null, label));\n',
     '  }, /* @__PURE__ */ React.createElement(SpectrLengthCheck, { on: selected }),\n'
     '     /* @__PURE__ */ React.createElement("span", { style: SPECTR_LENGTH_DECORATION }, label));\n',
     "LENGTH row caption"),
    ('  }, /* @__PURE__ */ React.createElement(SpectrLengthCheck, { on: f === value }),\n'
     '     /* @__PURE__ */ React.createElement("span", null, spectrFractionText(f)))))),\n',
     '  }, /* @__PURE__ */ React.createElement(SpectrLengthCheck, { on: f === value }),\n'
     '     /* @__PURE__ */ React.createElement("span", { style: SPECTR_LENGTH_DECORATION }, spectrFractionText(f)))))),\n',
     "Fraction row caption"),
    ('        }, /* @__PURE__ */ React.createElement("span", null, spectrFractionText(fraction)),\n'
     '           /* @__PURE__ */ React.createElement("span", { style: { fontSize: 9, opacity: 0.7 } }, "▾")))),\n',
     '        }, /* @__PURE__ */ React.createElement("span", { style: SPECTR_LENGTH_DECORATION }, spectrFractionText(fraction)),\n'
     '           /* @__PURE__ */ React.createElement("span", { style: { fontSize: 9, opacity: 0.7, pointerEvents: "none" } }, "▾")))),\n',
     "Fraction trigger text"),
    ('  }, /* @__PURE__ */ React.createElement("svg", { width: 9, height: 6, viewBox: "0 0 9 6", "aria-hidden": true },\n',
     '  }, /* @__PURE__ */ React.createElement("svg", { width: 9, height: 6, viewBox: "0 0 9 6", "aria-hidden": true,\n'
     '      style: SPECTR_LENGTH_DECORATION },\n',
     "Bars stepper chevron"),
]


def once(html, needle, label):
    count = html.count(needle)
    if count != 1:
        sys.exit("FAIL: %s patch point occurs %d times, expected 1" % (label, count))


def main():
    raw = open(PATH, encoding="utf-8").read()
    document = json.loads(raw)
    html = document["html"]

    if MARKER in html:
        for _, new, label in EDITS:
            if html.count(new) != 1:
                sys.exit("FAIL: the LENGTH first-press patch is present but its %s is not; "
                         "the document is half patched" % label)
        print("already applied  LENGTH first press")
        return 0

    if PREREQUISITE not in html:
        sys.exit("FAIL: %r is missing; apply patch_materialized_freeze_length_menu_v2.py first"
                 % PREREQUISITE)
    for old, _, label in EDITS:
        once(html, old, label)
    for old, new, _ in EDITS:
        html = html.replace(old, new, 1)

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
    print("applied          LENGTH first press")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
