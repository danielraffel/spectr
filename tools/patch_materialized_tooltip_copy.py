#!/usr/bin/env python3
"""Header tooltip copy: AUTO and MIX say what the control does, nothing else.

  AUTO  "Auto Gain — keeps the volume steady as you boost or cut."
        ("turn it on when you want it" told the user nothing; why AUTO starts
        off, and its limits, are in the help guide.)
  MIX   "Mix: blend Spectr's sound with the original input."
        (The "Great with Freeze" pitch is the help guide's to make.)

The two scripts that first wrote these strings
(tools/patch_materialized_tooltip_panel.py, tools/patch_materialized_header_
tooltips.py) now write the new copy, so on a fresh chain this is a no-op.

Idempotent like the other patch_materialized_* scripts.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

EDITS = [
    ('AUTO says what it does',
     'globalThis.spectrHeaderTip("Auto Gain: AUTO keeps the level steady — turn it on when you want it.", "[data-spectr-auto-gain]")',
     'globalThis.spectrHeaderTip("Auto Gain — keeps the volume steady as you boost or cut.", "[data-spectr-auto-gain]")'),
    ('MIX says what it does',
     'tip: "Mix: blend Spectr\'s sound with the original. Great with Freeze.",',
     'tip: "Mix: blend Spectr\'s sound with the original input.",'),
]


def escaped(value):
    return json.dumps(value, ensure_ascii=False)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    changed = False
    for label, old, new in EDITS:
        o, n = raw.count(escaped(old)), raw.count(escaped(new))
        if o == 0 and n == 1:
            print('already applied ', label)
            continue
        if o != 1 or n != 0:
            sys.exit('FAIL %s: old copy %d times, new copy %d times' % (label, o, n))
        raw = raw.replace(escaped(old), escaped(new), 1)
        changed = True
        print('applied         ', label)
    if changed:
        json.loads(raw)
        open(PATH, 'w', encoding='utf-8').write(raw)
        print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
