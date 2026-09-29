#!/usr/bin/env python3
"""A scroll over the plot does nothing while a modal dialog is open.

THE DEFECT

  With the About guide open, a two-finger scroll over the lower part of the
  band plot zoomed the viewport underneath the dialog. The About scrim paints
  over the whole editor, but it is mounted inside the help control and reaches
  outside its ancestors' bounds, so the SDK's tree hit test cannot descend into
  it there and lands on the plot. Presses and hovers already resolve through
  the open overlay first; the wheel did not.

THE FIX

  The SDK fix routes wheel and trackpad magnify/rotate through the open
  overlays (pulp `route_passive_pointer`). Until Spectr builds against an SDK
  carrying it, the plot's own wheel handler refuses to act while a modal
  dialog is open: About, Settings, the preset manager, and the save and delete
  dialogs. Presses and drags need nothing here: the SDK already routes a press
  outside an open overlay to its dismissal and consumes it.

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

MARKER = "spectrModalDialogBlocksPlot"

EDITS = [
    ('the plot ignores a scroll while a modal dialog is open',
     '  const onWheel = (e) => {\n'
     '    e.preventDefault();\n'
     '    const g = getGeom();\n',
     '  // A modal dialog makes the plot behind it inert. Settings stays mounted\n'
     '  // while closed, so its live flag answers; every other dialog here is\n'
     '  // mounted only while open.\n'
     '  const spectrModalDialogBlocksPlot = () => {\n'
     '    const settings = document.querySelector("[data-spectr-settings-panel]");\n'
     '    if (settings && typeof settings.getAttribute === "function"\n'
     '        && settings.getAttribute("data-spectr-settings-live") === "true")\n'
     '      return true;\n'
     '    return ["[data-spectr-help-guide-scrim]", "[data-spectr-pattern-manager-panel]",\n'
     '            "[data-spectr-delete-dialog]", "[data-spectr-save-panel]"]\n'
     '      .some((selector) => !!document.querySelector(selector));\n'
     '  };\n'
     '  const onWheel = (e) => {\n'
     '    e.preventDefault();\n'
     '    if (spectrModalDialogBlocksPlot()) return;\n'
     '    const g = getGeom();\n'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 1:
        print('already applied  a modal dialog makes the plot ignore the wheel')
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
