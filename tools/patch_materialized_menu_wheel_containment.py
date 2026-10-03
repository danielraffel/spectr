#!/usr/bin/env python3
"""While a menu or popover is open, the wheel never reaches what is behind it.

THE DEFECT

  Scrolling the band menu's Modulation submenu zoomed the viewport behind it.
  The submenu's head (Back, the LFO switches, Shape, Rate) has no scroller, so
  a wheel there bubbled out of the menu to the plot's zoom handler; so did a
  wheel past either end of the target list once the list stopped consuming it,
  a horizontal trackpad delta, and any wheel over the band menu itself. A wheel
  anywhere OUTSIDE the open menu reached the plot or a header knob directly,
  because the SDK routes a passive input outside a non-modal popover through
  the ordinary tree hit test.

THE RULE (the macOS menu behaviour)

  While a menu, submenu, dropdown or popover is open, a wheel over it scrolls
  only it and stops at its ends without chaining; a wheel anywhere else is
  swallowed (the menu stays open). The plot's zoom and the header knobs' wheel
  are the wheel consumers that can sit under or beside an open menu, so both
  refuse while one is open.

  `globalThis.spectrMenuHoldsWheel()` answers "is a menu or popover open": any
  mounted `data-spectr-overlay` node or lifted submenu panel with a box, except
  the Settings dialog while it is closed (it stays mounted).

INTERIM -- DELETE ON THE SDK BUMP that carries Pulp's overlay wheel
containment (route_passive_pointer swallows a passive input outside an open
overlay; deliver_mouse_wheel never bubbles a wheel out of the overlay it
resolved into). With that SDK this script's guards are redundant.

Idempotent like the other patch_materialized_* scripts: it substitutes exact
text, asserts each patch point occurs exactly once, refuses a half-patched
document and reports "already applied" on a second run. Run after
tools/patch_materialized_modal_blocks_plot_wheel.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "spectrMenuHoldsWheel"

EDITS = [
    ('the editor can tell whether a menu or popover is open',
     '  globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};\n',
     '  globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};\n'
     '  // Is a menu, submenu, dropdown or popover open? While one is, the wheel\n'
     '  // belongs to it: the plot and the knobs refuse it. INTERIM -- delete on\n'
     '  // the SDK bump carrying Pulp\'s overlay wheel containment; see\n'
     '  // tools/patch_materialized_menu_wheel_containment.py.\n'
     '  globalThis.spectrMenuHoldsWheel = () => {\n'
     '    if (typeof document === "undefined" || !document.querySelectorAll) return false;\n'
     '    const settings = document.querySelector\n'
     '      ? document.querySelector("[data-spectr-settings-panel]") : null;\n'
     '    const settingsClosed = !!settings && typeof settings.getAttribute === "function"\n'
     '      && settings.getAttribute("data-spectr-settings-live") !== "true";\n'
     '    const nodes = [...document.querySelectorAll(\'[data-spectr-overlay="true"]\'),\n'
     '                   ...document.querySelectorAll("[data-spectr-submenu-lifted]")];\n'
     '    return nodes.some((node) => {\n'
     '      if (!node) return false;\n'
     '      if (settingsClosed && (node === settings\n'
     '          || (typeof node.contains === "function" && node.contains(settings))))\n'
     '        return false;\n'
     '      const box = typeof node.getBoundingClientRect === "function"\n'
     '        ? node.getBoundingClientRect() : null;\n'
     '      return !!box && box.width > 0 && box.height > 0;\n'
     '    });\n'
     '  };\n'),
    ('the plot ignores a scroll while a menu is open',
     '    if (spectrModalDialogBlocksPlot()) return;\n',
     '    if (spectrModalDialogBlocksPlot()) return;\n'
     '    if (globalThis.spectrMenuHoldsWheel && globalThis.spectrMenuHoldsWheel()) return;\n'),
    ('a header knob ignores a scroll while a menu is open',
     '  const onKnobWheel = (e) => {\n'
     '    const dy = e && typeof e.deltaY === "number" ? e.deltaY : 0;\n'
     '    if (!dy) return;\n'
     '    if (typeof e.preventDefault === "function") e.preventDefault();\n'
     '    if (typeof e.stopPropagation === "function") e.stopPropagation();\n',
     '  const onKnobWheel = (e) => {\n'
     '    const dy = e && typeof e.deltaY === "number" ? e.deltaY : 0;\n'
     '    if (!dy) return;\n'
     '    if (typeof e.preventDefault === "function") e.preventDefault();\n'
     '    if (typeof e.stopPropagation === "function") e.stopPropagation();\n'
     '    if (globalThis.spectrMenuHoldsWheel && globalThis.spectrMenuHoldsWheel()) return;\n'),
    ('the Modulation submenu never lets a wheel chain out of it',
     '  const modulationWheel = (event) => {\n'
     '    const delta = event && typeof event.deltaY === "number" ? event.deltaY : 0;\n'
     '    if (!delta || !modulationGeomRef.current.scrolls) return;\n'
     '    if (typeof event.preventDefault === "function") event.preventDefault();\n'
     '    if (typeof event.stopPropagation === "function") event.stopPropagation();\n',
     '  const modulationWheel = (event) => {\n'
     '    // Contained: a wheel over the list never chains out of the menu, even\n'
     '    // at either end, sideways, or when the list does not scroll.\n'
     '    if (event && typeof event.preventDefault === "function") event.preventDefault();\n'
     '    if (event && typeof event.stopPropagation === "function") event.stopPropagation();\n'
     '    const delta = event && typeof event.deltaY === "number" ? event.deltaY : 0;\n'
     '    if (!delta || !modulationGeomRef.current.scrolls) return;\n'),
]


def escaped(value):
    # ensure_ascii=False: the artifact stores non-ASCII literally (see
    # tools/git/merge_materialized_runtime.py), so must every edit.
    return json.dumps(value, ensure_ascii=False)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    applied = [raw.count(escaped(new)) == 1 for _, _, new in EDITS]
    if all(applied):
        print('already applied  an open menu keeps the wheel to itself')
        return 0
    if any(applied):
        sys.exit('FAIL: the document is half patched by this script')
    for label, old, new in EDITS:
        if raw.count(escaped(old)) != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, raw.count(escaped(old))))
    for label, old, new in EDITS:
        raw = raw.replace(escaped(old), escaped(new), 1)
        print('applied         ', label)
    if raw.count(escaped(MARKER)) < 3:
        sys.exit('FAIL: the marker did not land')
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
