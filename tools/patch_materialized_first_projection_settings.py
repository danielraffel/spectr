#!/usr/bin/env python3
"""The first host projection re-renders nothing hydration already applied.

The editor's live-mode cache (nativeLiveModesRef) starts empty, so the first
live projection after hydration reaches the mode setters with values the
editor already holds. The primitive setters (analyzer, edit, visualization
mode) bail out on an equal value, but the motion mode is written into the
settings object with setSettings(current => ({ ...current, motionMode })),
which is a new object every time: a whole-app re-render and a full metadata
pass on the first automation change of every session -- measured headless at
~38-67 ms against ~3 ms for the changes that follow it.

The settings setter now keeps the current object when the motion mode already
matches, so the first projection costs what every later one does. This is the
same rule as the rest of the live projection: never call a React setter with a
value equal to the one it holds.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts the patch point occurs exactly once, and
reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 the patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

EDITS = [
    ('the first live projection re-renders nothing hydration already applied',
     '    if (modes.motionMode !== state.motionMode)\n'
     '      setSettings((current) => ({ ...current, motionMode: state.motionMode }));\n',
     '    // The mode cache starts empty, so the first projection after hydration\n'
     '    // reaches these setters with values the editor already holds. The\n'
     '    // primitive setters bail out on an equal value; settings is an object,\n'
     '    // so it keeps the current one when the mode already matches.\n'
     '    if (modes.motionMode !== state.motionMode)\n'
     '      setSettings((current) => current.motionMode === state.motionMode\n'
     '        ? current : { ...current, motionMode: state.motionMode });\n'),
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
