#!/usr/bin/env python3
"""Set the Settings labels' face once per metadata pass, not 57 times.

`applyMaterializedImportMetadata` in the vendored runtime.js carries the same
block -- look up the "APPEARANCE", "Theme" and "Bloom" labels and set each to
the bound monospace face -- 57 times in a row, the residue of a generator that
appended it once per run. Every copy does identical, idempotent work, and every
copy runs on every metadata pass: 171 full registry scans (`values.find` over
every node) and 171 bridge setters per pass, whatever the pass was for.

One copy does exactly the same thing. This script asserts the run is a pure
repetition of one block before collapsing it, so it cannot swallow a block that
differs.

Why a script and not a hand edit: runtime.js is a checked-in built artifact and
~90 patch scripts anchor on its text, so edits are exact, replayable
substitutions that report "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 the run is missing or not a pure
repetition.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

BLOCK = ('    if (monoBinding && typeof g5.setFontFamily === "function") {\n'
         '      for (const labelText of ["APPEARANCE", "Theme", "Bloom"]) {\n'
         '        const node = values.find((candidate) =>\n'
         '          String(candidate && candidate.textContent || "") === labelText);\n'
         '        const nodeId = node && (node.__pulpTextTargetId || node.__pulpId || node.id);\n'
         '        if (!nodeId) continue;\n'
         '        g5.setFontFamily(String(nodeId), materializedRuntimeFontStack(monoBinding));\n'
         '      }\n'
         '    }\n')


def main():
    source = open(PATH, encoding="utf-8").read()
    count = source.count(BLOCK)
    if count == 1:
        print("already applied  the Settings labels' face is set once per pass")
        return 0
    if count < 2:
        sys.exit("FAIL: the Settings label face block occurs %d times" % count)
    start = source.index(BLOCK)
    end = source.rindex(BLOCK) + len(BLOCK)
    if source[start:end] != BLOCK * count:
        sys.exit("FAIL: the %d copies are not one contiguous run; refusing to "
                 "collapse blocks that may differ" % count)
    source = source[:start] + BLOCK + source[end:]
    if source.count(BLOCK) != 1:
        sys.exit("FAIL: the collapse did not leave exactly one block")
    open(PATH, "w", encoding="utf-8").write(source)
    print("applied          the Settings labels' face is set once per pass "
          "(%d copies -> 1)" % count)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
