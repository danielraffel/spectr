#!/usr/bin/env python3
"""Pan the view through the live viewport lane, and settle it once on release.

An Alt / middle-button pan called `setView` on every pointer move. `setView`
writes React's copy of the view (`setReactView`), so every sample of a pan
scheduled a commit, and in this captured import a commit re-applies the
whole captured document. It also published every sample to the viewport
subscribers as a settled change, so the zoom readout committed on every move
as well.

The minimap drag, the minimap resize and the wheel already do this properly:
they write the live view with `commitLiveViewport` (the view ref, the native
processing-state publication, and a `live` notification the readout ignores)
and settle React's copy once when the gesture ends. A pan now does the same:
`commitLiveViewport` per move, and one `setView` on release -- and only when
the pan actually moved the view, so a press that goes nowhere commits nothing.

The painters already read the view ref, and the draw loop runs every frame
while a pointer mode is active, so the canvas follows the pan exactly as
before.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once before
writing, refuses a half-patched document, and reports "already applied" on a
second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

EDITS = [
    ('a pan move writes the live viewport, not React state',
     '        lmax = fullMax;\n'
     '      }\n'
     '      setView({ lmin, lmax });\n'
     '      return;\n'
     '    }\n'
     '    if (p.mode === "marquee") {',
     '        lmax = fullMax;\n'
     '      }\n'
     '      // Live, like the minimap drag and the wheel: no React commit per\n'
     '      // sample. The release settles it once.\n'
     '      commitLiveViewport({ lmin, lmax });\n'
     '      return;\n'
     '    }\n'
     '    if (p.mode === "marquee") {'),

    ('a pan settles the view once on release',
     '      postNative("macro_drag_end", {});\n'
     '    }\n'
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize")) {\n',
     '      postNative("macro_drag_end", {});\n'
     '    }\n'
     '    // One settling commit, and only if the pan moved the view.\n'
     '    if (p && p.mode === "pan" && p.viewStart\n'
     '        && (viewRef.current.lmin !== p.viewStart.lmin\n'
     '          || viewRef.current.lmax !== p.viewStart.lmax))\n'
     '      setView({ ...viewRef.current });\n'
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize")) {\n'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for label, old, new in EDITS:
        if old in new:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)

    raw = open(PATH, encoding='utf-8').read()
    applied = 0
    already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(old_e) == 0 and raw.count(new_e) == 1:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        applied += 1
        print('applied         ', label)

    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')
    if not applied:
        return 0

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
