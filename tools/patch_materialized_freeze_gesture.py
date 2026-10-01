#!/usr/bin/env python3
"""A press of the LIVE / FROZEN toggle (or a freeze key) records in automation.

THE DEFECT

  The toggle wrote Freeze through `param_set`, which sets the parameter's
  value and nothing else. The DSP followed and the host's parameter moved,
  but a host recording in Touch, Latch or Write keys on the edit GESTURE --
  begin, value, end -- and a bare value change gave it no touch to record,
  so a press left no automation behind. Every other editor-driven parameter
  edit in Spectr (band gains and mutes, macros) goes out inside a gesture.

THE FIX

  `spectrSetFrozen` -- the one write both the toggle and every freeze key go
  through -- posts `freeze_set`, which the processor applies as one complete
  gesture (Spectr::set_freeze_from_editor). The cached copy still flips first,
  so the face turns over on the press.

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

MARKER = "__spectrFreezeGesture"

EDITS = [
    ('the freeze write goes out as a host gesture',
     '// Cached copy first, so the face flips on the press, then the processor,\n'
     '// which owns the value and reports it to the host.\n'
     'function spectrSetFrozen(frozen) {\n'
     '  const store = spectrFreezeStore();\n'
     '  const next = frozen === true;\n'
     '  if (store.frozen !== next) {\n'
     '    store.frozen = next;\n'
     '    spectrFreezeNotify(store);\n'
     '  }\n'
     '  if (window.pulp && typeof window.pulp.postMessage === "function") {\n'
     '    try {\n'
     '      Promise.resolve(window.pulp.postMessage("param_set",\n'
     '        { id: 3, value: next ? 1 : 0 }, "spectr-freeze")).catch((error) =>\n',
     '// Cached copy first, so the face flips on the press, then the processor,\n'
     '// which owns the value and reports it to the host as one complete edit\n'
     '// gesture (' + MARKER + ") -- begin, value, end -- which is what a host\n"
     '// recording in Touch, Latch or Write keys on. A bare value write moves\n'
     '// the parameter but leaves such a host nothing to record.\n'
     'function spectrSetFrozen(frozen) {\n'
     '  const store = spectrFreezeStore();\n'
     '  const next = frozen === true;\n'
     '  if (store.frozen !== next) {\n'
     '    store.frozen = next;\n'
     '    spectrFreezeNotify(store);\n'
     '  }\n'
     '  if (window.pulp && typeof window.pulp.postMessage === "function") {\n'
     '    try {\n'
     '      Promise.resolve(window.pulp.postMessage("freeze_set",\n'
     '        { frozen: next }, "spectr-freeze")).catch((error) =>\n'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 1:
        for label, old, new in EDITS:
            if raw.count(escaped(new)) != 1:
                sys.exit('FAIL %s: document is half patched' % label)
        print('already applied  the freeze write goes out as a host gesture')
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
