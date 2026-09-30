#!/usr/bin/env python3
"""A data-* attribute nothing reads is not a geometric change.

The host config decides per commit whether a prop change can move a captured
binding. Any key outside the paint-only whitelist counts as geometric, so a
commit that changes only a `data-*` attribute -- a hover state marker such as
`data-spectr-close-state` -- marked its subtree dirty (a metadata pass) and
bumped the mutation epoch, which invalidates the registry-miss cache and makes
the next captured-state resolution rescan the registry. Measured headless on
the Settings close button: ~1.8 ms of metadata pass and ~2.9 ms of state
resolution per hover, for an attribute nothing in the metadata path reads.

A data attribute can only matter to the metadata path through a selector or an
attribute read. So a `data-*` key now counts as non-geometric unless

  * the runtime itself names it -- every `data-*` literal in this runtime.js,
    collected when the patch is applied (selectors, getAttribute reads), or
  * a selector asked the registry finder about it: the finder records every
    attribute name a selector or ancestor mentions, before consulting its miss
    cache, so the captured-state match selectors are covered from the first
    commit that resolves them.

Before the finder has recorded anything, every `data-*` key stays geometric.

Why a script and not a hand edit: runtime.js is a checked-in built artifact
and many patch scripts anchor on its text, so edits are exact, replayable
substitutions that report "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

GATE_OLD = "      if (!PAINT_ONLY_KEYS.has(key) && !isEventHandler(key)) return false;\n"
GATE_NEW = ("      if (!PAINT_ONLY_KEYS.has(key) && !isEventHandler(key)\n"
            "          && !isUnreadDataAttribute(key)) return false;\n")
HELPER_ANCHOR = "  function isPaintOnlyUpdate(oldProps, newProps) {\n"
FINDER_OLD = ('  g5.__pulpFindMaterializedElement__ = function(selector, ancestor) {\n'
              '    if (typeof selector !== "string" || selector.length === 0) return null;\n')
FINDER_NEW = ('  const materializedQueriedAttributes = g5.__pulpMaterializedSelectorAttributes__\n'
              '    || (g5.__pulpMaterializedSelectorAttributes__ = /* @__PURE__ */ new Set());\n'
              '  const recordQueriedAttributes = (text) => {\n'
              '    if (typeof text !== "string") return;\n'
              '    for (const match of text.matchAll(/\\[\\s*([A-Za-z0-9_:-]+)/g))\n'
              '      materializedQueriedAttributes.add(match[1]);\n'
              '  };\n'
              '  g5.__pulpFindMaterializedElement__ = function(selector, ancestor) {\n'
              '    if (typeof selector !== "string" || selector.length === 0) return null;\n'
              '    // Recorded before the miss cache is consulted: an attribute a\n'
              '    // selector names is one whose change must bump the mutation epoch.\n'
              '    recordQueriedAttributes(selector);\n'
              '    recordQueriedAttributes(ancestor);\n')
MARKER = "function isUnreadDataAttribute(key) {"


def helper(names):
    listed = ", ".join(json.dumps(name) for name in names)
    return (
        "  // data-* attributes the runtime itself reads, fixed when it was built,\n"
        "  // and every attribute a selector has asked the registry finder about.\n"
        "  // A change to any other data-* attribute cannot move a captured binding\n"
        "  // or change a captured-state match, so it is not a geometric change.\n"
        "  var RUNTIME_READ_DATA_ATTRIBUTES = /* @__PURE__ */ new Set([%s]);\n"
        "  function isUnreadDataAttribute(key) {\n"
        "    if (typeof key !== \"string\" || !key.startsWith(\"data-\") || key.length <= 5)\n"
        "      return false;\n"
        "    if (RUNTIME_READ_DATA_ATTRIBUTES.has(key)) return false;\n"
        "    const queried = g4.__pulpMaterializedSelectorAttributes__;\n"
        "    return !!queried && !queried.has(key);\n"
        "  }\n" % listed)


def main():
    source = open(PATH, encoding="utf-8").read()
    if source.count(MARKER) == 1:
        print("already applied  unread data attributes are not geometric")
        return 0
    for label, token in (("the paint-only gate", GATE_OLD),
                         ("isPaintOnlyUpdate", HELPER_ANCHOR),
                         ("the registry finder", FINDER_OLD)):
        if source.count(token) != 1:
            sys.exit("FAIL: %s occurs %d times, expected 1"
                     % (label, source.count(token)))
    names = sorted(set(re.findall(r"data-[a-z0-9][a-z0-9-]*", source)))
    if not names:
        sys.exit("FAIL: the runtime names no data attributes; refusing an empty list")
    source = source.replace(GATE_OLD, GATE_NEW, 1)
    source = source.replace(HELPER_ANCHOR, helper(names) + HELPER_ANCHOR, 1)
    source = source.replace(FINDER_OLD, FINDER_NEW, 1)
    open(PATH, "w", encoding="utf-8").write(source)
    print("applied          unread data attributes are not geometric "
          "(%d runtime-read names)" % len(names))
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
