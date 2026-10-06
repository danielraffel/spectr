#!/usr/bin/env python3
"""Make the Settings scroll upgrade linear in the registry, not quadratic.

WHY THIS EXISTS

    The responsive layout's first full pass upgrades the Settings body to a
    native ScrollView (ensureSpectrNativeScrollView). That re-marks, re-appends
    and re-hydrates the whole Settings subtree in three walks, and each walk
    asked `childrenFor(element)` for every element -- a filter over the WHOLE
    registry per element. Measured on the editor open (in-process, traced
    build): 866 childrenFor calls, 61 ms of a 72 ms upgrade, inside a 96 ms
    first responsive pass.

WHAT IT CHANGES

    One pass over the registry indexes each node under the parent each of its
    two parent edges names (parentElement, _parentElement), in registry
    order. childrenFor reads that index and re-checks the live edges, so a
    child whose edge the walk itself rewrote is judged exactly as the
    per-element filter judged it. Order and de-duplication are unchanged:
    `_children` first, then registry children in registry order, first
    occurrence kept.

    Gate: test/test_editor_open.cpp "[editor-open]" asserts the registry
    nodes childrenFor visits stay linear in the registry
    (__spectrScrollUpgradeStats__: before this, every lookup visited all of
    it -- ~866 x 418).

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 the patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

MARKER = "registryChildrenByParent"

OLD = '''    const childrenFor = (element) => {
      const direct = (Array.isArray(element?._children) ? element._children : [])
        .concat(values.filter((candidate) => candidate && candidate !== element
          && (candidate.parentElement === element
              || candidate._parentElement === element)));
      return direct.filter((child, index, all) => child && all.indexOf(child) === index);
    };
'''
NEW = '''    // One registry pass indexes every node under the parent each of its two
    // parent edges names, so the three subtree walks below cost O(subtree)
    // rather than a registry scan per element (866 scans, 61 ms, on the
    // editor open). Lookups re-check the live edges, so a child whose edge a
    // walk rewrote is judged exactly as a fresh scan would judge it.
    // See tools/patch_materialized_runtime_linear_scroll_upgrade.py.
    const registryChildrenByParent = /* @__PURE__ */ new Map();
    const indexRegistryChild = (owner, candidate) => {
      if (!owner || owner === candidate) return;
      let list = registryChildrenByParent.get(owner);
      if (!list) registryChildrenByParent.set(owner, list = []);
      if (list[list.length - 1] !== candidate) list.push(candidate);
    };
    for (const candidate of values) {
      if (!candidate) continue;
      indexRegistryChild(candidate.parentElement, candidate);
      indexRegistryChild(candidate._parentElement, candidate);
    }
    const upgradeStats = g5.__spectrScrollUpgradeStats__
      || (g5.__spectrScrollUpgradeStats__ = { upgrades: 0, registry: 0, lookups: 0,
        visited: 0 });
    upgradeStats.upgrades += 1;
    upgradeStats.registry += values.length;
    const childrenFor = (element) => {
      upgradeStats.lookups += 1;
      const direct = Array.isArray(element?._children) ? element._children : [];
      const indexed = registryChildrenByParent.get(element) || [];
      upgradeStats.visited += indexed.length;
      const seen = /* @__PURE__ */ new Set();
      const children = [];
      const keep = (child) => {
        if (!child || seen.has(child)) return;
        seen.add(child);
        children.push(child);
      };
      for (const child of direct) keep(child);
      for (const candidate of indexed) {
        if (candidate !== element && (candidate.parentElement === element
            || candidate._parentElement === element)) keep(candidate);
      }
      return children;
    };
'''


def main():
    raw = open(PATH, encoding="utf-8").read()
    if MARKER in raw:
        print("already applied  linear scroll upgrade")
        return 0
    count = raw.count(OLD)
    if count != 1:
        sys.exit("FAIL: childrenFor occurs %d times, expected 1" % count)
    raw = raw.replace(OLD, NEW, 1)
    with open(PATH, "w", encoding="utf-8") as stream:
        stream.write(raw)
    print("applied  linear scroll upgrade")
    return 0


if __name__ == "__main__":
    sys.exit(main())
