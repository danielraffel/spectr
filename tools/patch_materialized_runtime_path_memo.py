#!/usr/bin/env python3
"""Resolve each captured binding's path once per metadata pass.

`applyMaterializedImportMetadata` finds the live node for every captured
layout, text and paint binding by walking its path from the registry roots.
It did that walk up to five times per binding per pass -- once in each filter
that asks (Settings descendant, manager detail, status overlay, the band-count
text merge) and again in the loop that applies it -- and every step of every
walk re-filtered the node's children, which for the Settings overlay runs a
`querySelector` on each candidate. A scoped pass paid all of it to decide that
nearly every binding was out of scope: ~14 ms of a ~17 ms hook on a hover
commit, measured headless on the shipping editor.

One pass works against one registry snapshot (`values` and `pathIndex` were
already taken once per pass), so the walks can share their work:

  * `pathIndex.childrenMemo` keeps each node's filtered children for the pass,
    and `materializedNodeAtPath` reads it when present (only on the
    hidden-Settings-filtered walk the applier uses; other callers pass no memo
    and behave exactly as before);
  * `nodeAtPath(binding)` keeps each binding's resolved node for the pass.

Measured: the hook's filter phase 14 ms -> 1.5 ms, the whole hook ~17 ms ->
~3 ms per hover commit.

Why a script and not a hand edit: runtime.js is a checked-in built artifact
and many patch scripts anchor on its text, so edits are exact, replayable
substitutions that report "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

APPLIER_HEAD = "  function applyMaterializedImportMetadata(metadata, scopeIds) {"
WALK_CALL = "materializedNodeAtPath(binding, values, true, pathIndex)"
WALK_SITES = 7

MEMO_DECL_OLD = "    const pathIndex = materializedPathIndex(values);\n"
MEMO_DECL_NEW = (
    "    const pathIndex = materializedPathIndex(values);\n"
    "    // One pass resolves every binding's path against one registry snapshot,\n"
    "    // so each node's filtered children and each binding's node are computed\n"
    "    // once per pass rather than once per filter that asks.\n"
    "    pathIndex.childrenMemo = /* @__PURE__ */ new Map();\n"
    "    const pathMemo = /* @__PURE__ */ new Map();\n"
    "    const nodeAtPath = (binding) => {\n"
    "      if (pathMemo.has(binding)) return pathMemo.get(binding);\n"
    "      const node = materializedNodeAtPath(binding, values, true, pathIndex);\n"
    "      pathMemo.set(binding, node);\n"
    "      return node;\n"
    "    };\n")

WALK_OLD = (
    "    for (const step of binding.path) {\n"
    "      node = siblings[step.index] || null;\n"
    "      if (!node || materializedNodeTag(node) !== step.tag) return null;\n"
    "      siblings = materializedElementChildren(node, registrySet);\n"
    "      if (filterHiddenSettings) {")
WALK_NEW = (
    "    const memo = filterHiddenSettings && index.childrenMemo ? index.childrenMemo : null;\n"
    "    for (const step of binding.path) {\n"
    "      node = siblings[step.index] || null;\n"
    "      if (!node || materializedNodeTag(node) !== step.tag) return null;\n"
    "      if (memo && memo.has(node)) {\n"
    "        siblings = memo.get(node);\n"
    "        continue;\n"
    "      }\n"
    "      siblings = materializedElementChildren(node, registrySet);\n"
    "      if (filterHiddenSettings) {")
WALK_TAIL_OLD = (
    '          return panel?.getAttribute?.("data-spectr-settings-live") === "true";\n'
    "        });\n"
    "      }\n"
    "    }\n")
WALK_TAIL_NEW = (
    '          return panel?.getAttribute?.("data-spectr-settings-live") === "true";\n'
    "        });\n"
    "      }\n"
    "      if (memo) memo.set(node, siblings);\n"
    "    }\n")


def applier_bounds(source):
    start = source.index(APPLIER_HEAD)
    end = source.index("\n  }\n", source.index("return applied;", start))
    return start, end


def main():
    source = open(PATH, encoding="utf-8").read()
    if source.count(APPLIER_HEAD) != 1:
        sys.exit("FAIL: the metadata applier is not where this script expects it")

    if source.count(WALK_NEW) == 1 and source.count("pathIndex.childrenMemo = ") == 1:
        print("already applied  binding paths resolve once per metadata pass")
        return 0

    if source.count(WALK_OLD) != 1:
        sys.exit("FAIL: the path walk occurs %d times, expected 1"
                 % source.count(WALK_OLD))
    walk = source.index(WALK_OLD)
    walk_end = source.index("    return node;\n  }\n", walk)
    segment = source[walk:walk_end]
    if segment.count(WALK_TAIL_OLD) != 1:
        sys.exit("FAIL: the path walk's children filter is not where expected")
    segment = segment.replace(WALK_OLD, WALK_NEW, 1).replace(
        WALK_TAIL_OLD, WALK_TAIL_NEW, 1)
    source = source[:walk] + segment + source[walk_end:]

    start, end = applier_bounds(source)
    body = source[start:end]
    if body.count(WALK_CALL) != WALK_SITES:
        sys.exit("FAIL: the applier walks paths at %d sites, expected %d"
                 % (body.count(WALK_CALL), WALK_SITES))
    if body.count(MEMO_DECL_OLD) != 1:
        sys.exit("FAIL: the applier's path index is not declared once")
    body = body.replace(WALK_CALL, "nodeAtPath(binding)")
    body = body.replace(MEMO_DECL_OLD, MEMO_DECL_NEW, 1)
    source = source[:start] + body + source[end:]

    if source.count("nodeAtPath(binding)") < WALK_SITES:
        sys.exit("FAIL: the applier's walks were not all routed through the memo")
    open(PATH, "w", encoding="utf-8").write(source)
    print("applied          binding paths resolve once per metadata pass")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
