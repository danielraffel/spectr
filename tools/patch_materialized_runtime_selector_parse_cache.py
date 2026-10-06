#!/usr/bin/env python3
"""Transplant Pulp's memoized web-compat selector parse.

WHY THIS EXISTS

    Every React commit that touches a host node also resolves the captured
    state: one selector per state-atlas entry, each a registry scan that
    calls Element.prototype.matches(selector) on every node. Pulp's
    web-compat matches() re-tokenised the selector on every call, so a scan
    of ~420 nodes parsed the same selector ~420 times. Measured on the AU
    editor-open probe (traced build): ~16 ms of captured-state resolution
    per commit, most of it re-parsing.

    Pulp memoizes the parse by source text (core/view/js/web-compat-document-
    selectors.js; `_parseSelector.__pulpMemoized`). This SDK predates that.

WHAT IT CHANGES

    Before the runtime's first selector query, wrap the realm's global
    `_parseSelector` in the same bounded memo, unless the SDK's own parse is
    already memoized. matches()/closest()/querySelector() look the parser up
    by its global name, so they all go through the wrapper. The parse is a
    pure function of the string and no caller mutates the record it returns,
    so the results are identical.

DELETE WHEN

    Spectr builds against a Pulp SDK whose `_parseSelector.__pulpMemoized` is
    true (the wrapper is already inert there).

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 the patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

MARKER = "memoizeWebCompatSelectorParse"
ANCHOR = "  g5.__pulpFindMaterializedElement__ = function(selector, ancestor) {\n"
INSERT = (
    "  // Transplanted Pulp fix: memoized web-compat selector parse\n"
    "  // (tools/patch_materialized_runtime_selector_parse_cache.py). Inert on an\n"
    "  // SDK whose own parse is memoized.\n"
    "  (function memoizeWebCompatSelectorParse(host) {\n"
    "    const parse = host._parseSelector;\n"
    "    if (typeof parse !== \"function\" || parse.__pulpMemoized) return;\n"
    "    const cache = /* @__PURE__ */ new Map();\n"
    "    const memoized = function(str) {\n"
    "      if (!str) return parse(str);\n"
    "      const key = String(str);\n"
    "      const hit = cache.get(key);\n"
    "      if (hit !== void 0) return hit;\n"
    "      const parsed = parse(key);\n"
    "      if (cache.size >= 512) cache.clear();\n"
    "      cache.set(key, parsed);\n"
    "      return parsed;\n"
    "    };\n"
    "    memoized.__pulpMemoized = true;\n"
    "    host._parseSelector = memoized;\n"
    "  })(globalThis);\n"
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    if MARKER in raw:
        print("already applied  selector parse cache")
        return 0
    count = raw.count(ANCHOR)
    if count != 1:
        sys.exit("FAIL: find-element definition occurs %d times, expected 1" % count)
    raw = raw.replace(ANCHOR, INSERT + ANCHOR, 1)
    with open(PATH, "w", encoding="utf-8") as stream:
        stream.write(raw)
    print("applied  selector parse cache")
    return 0


if __name__ == "__main__":
    sys.exit(main())
