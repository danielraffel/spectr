#!/usr/bin/env python3
"""Resolve the captured state with one registry pass instead of one per state.

WHY THIS EXISTS

    Captured-state resolution runs on every React commit that touches a host
    node, and once when the editor opens. It asked
    __pulpFindMaterializedElement__ once per state-atlas entry (11 here), and
    every state that is not open is a miss: a scan that builds the registry
    array and match-tests every node (~400) before returning null. Measured
    in-process on the editor open (traced build): ~4,000 match tests, 17-25 ms
    per resolution -- the largest per-commit cost left after the scoped
    re-apply.

WHAT IT CHANGES

    __pulpFindMaterializedElements__(queries) answers a list of
    (selector, ancestor) lookups with exactly what __pulpFindMaterializedElement__
    would return for each, in one pass:

      * the browser-document fast path and the per-epoch miss cache are
        consulted per query first, as before;
      * the remaining queries share ONE registry pass. A node is
        match-tested against a query only if it carries every attribute the
        query's target selector names (read once per node per attribute) --
        a selector that requires [a] cannot match a node without a; and
      * each query keeps the first node in registry order that passes the
        same full test (target, direct parent, ancestor), and a miss is
        retained for the epoch exactly as a single lookup retains it.

    Nothing mutates between the lookups of one resolution, so answering them
    together is the same as answering them one by one.
    resolveCapturedStateFromAtlas asks for every state at once.

    Gate: test/test_editor_open.cpp "[editor-open]" counts full match tests
    per resolution (__spectrStateResolutionStats__).

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

MARKER = "g5.__pulpFindMaterializedElements__ ="

BATCH_ANCHOR = "  g5.__pulpActivateMaterializedElement__ = function(selector, eventName, eventData) {\n"
BATCH_INSERT = '''  // Many lookups against one unchanged registry, answered in one pass: each
  // result is exactly what __pulpFindMaterializedElement__ returns for that
  // query. See tools/patch_materialized_runtime_batched_state_resolution.py.
  const materializedSelectorAttributeNames = /* @__PURE__ */ new Map();
  const selectorAttributeNames = (selector) => {
    let names = materializedSelectorAttributeNames.get(selector);
    if (!names) {
      // Pseudo-classes and selector lists can name an attribute a match does
      // not require (`:not([x])`), so only a plain compound is prefiltered.
      names = /[:,]/.test(selector) ? []
        : Array.from(new Set(materializedParseSelector(selector).attributes
          .map((attribute) => attribute.name)));
      materializedSelectorAttributeNames.set(selector, names);
    }
    return names;
  };
  g5.__pulpFindMaterializedElements__ = function(queries) {
    const results = new Array(queries.length).fill(null);
    const stats = g5.__spectrStateResolutionStats__
      || (g5.__spectrStateResolutionStats__ = { passes: 0, match_tests: 0,
        attribute_reads: 0 });
    const pending = [];
    queries.forEach((query, index) => {
      if (!query || typeof query.selector !== "string" || query.selector.length === 0) return;
      const selector = query.selector;
      const ancestor = query.ancestor;
      recordQueriedAttributes(selector);
      recordQueriedAttributes(ancestor);
      if (g5.document && typeof g5.document.querySelector === "function") {
        const browserNode = g5.document.querySelector(selector);
        if (browserNode && (!ancestor || materializedClosest(browserNode, ancestor))) {
          results[index] = browserNode;
          return;
        }
      }
      const missEpoch = g5.__pulpMaterializedTreeEpoch__;
      const missCacheable = typeof missEpoch === "number"
        && (ancestor === void 0 || ancestor === null || typeof ancestor === "string");
      let missKey = "";
      if (missCacheable) {
        if (missEpoch !== materializedFindMissEpoch) {
          materializedFindMisses.clear();
          materializedFindMissEpoch = missEpoch;
        }
        missKey = selector + "\\u0000" + (ancestor || "");
        if (materializedFindMisses.has(missKey)) return;
      }
      let targetSelector = selector.trim();
      let effectiveAncestor = ancestor || "";
      let directParentSelector = "";
      const directParts = targetSelector.split(/\\s*>\\s*/).filter(Boolean);
      if (directParts.length > 1) {
        targetSelector = directParts.pop();
        directParentSelector = directParts.pop();
        if (!effectiveAncestor && directParts.length > 0) {
          effectiveAncestor = directParts.join(" > ");
        }
      }
      if (!effectiveAncestor) {
        const split = materializedLastDescendantSplit(targetSelector);
        if (split > 0) {
          effectiveAncestor = targetSelector.slice(0, split).trim();
          targetSelector = targetSelector.slice(split + 1).trim();
        }
      }
      pending.push({ index, targetSelector, directParentSelector, effectiveAncestor,
        required: selectorAttributeNames(targetSelector), missCacheable, missKey });
    });
    if (pending.length === 0) return results;
    stats.passes += 1;
    let open = pending.length;
    const presence = /* @__PURE__ */ new Map();
    for (const node of materializedDomRegistryValues()) {
      if (open === 0) break;
      presence.clear();
      const has = (name) => {
        let present = presence.get(name);
        if (present === void 0) {
          stats.attribute_reads += 1;
          present = typeof node.getAttribute !== "function"
            || node.getAttribute(name) !== null
            || (typeof node.hasAttribute === "function" && node.hasAttribute(name));
          presence.set(name, present);
        }
        return present;
      };
      const parent = node && (node.parentElement || node._parentElement || null);
      for (const query of pending) {
        if (query.found) continue;
        if (!query.required.every(has)) continue;
        stats.match_tests += 1;
        if (materializedMatches(node, query.targetSelector)
            && (!query.directParentSelector
              || materializedMatches(parent, query.directParentSelector))
            && (!query.effectiveAncestor || materializedClosest(
              query.directParentSelector ? parent : node, query.effectiveAncestor))) {
          query.found = true;
          results[query.index] = node;
          --open;
        }
      }
    }
    for (const query of pending) {
      if (!query.found && query.missCacheable) materializedFindMisses.add(query.missKey);
    }
    return results;
  };
'''

RESOLVE_OLD = '''  function resolveCapturedStateFromAtlas() {
    let levelFallback = "";
    for (let index = capturedStates.length - 1; index >= 0; --index) {
      const state = capturedStates[index];
      if (state.match) {
        const match = g5.__pulpFindMaterializedElement__(
          state.match.selector,
          state.match.ancestor
        );
'''
RESOLVE_NEW = '''  function resolveCapturedStateFromAtlas() {
    let levelFallback = "";
    // Every state's lookup in one registry pass (the loop below used to scan
    // the registry once per state).
    const matches = g5.__pulpFindMaterializedElements__(capturedStates.map(
      (state) => state.match ? { selector: state.match.selector,
        ancestor: state.match.ancestor } : null));
    for (let index = capturedStates.length - 1; index >= 0; --index) {
      const state = capturedStates[index];
      if (state.match) {
        const match = matches[index];
'''


def replace_once(raw, old, new, what):
    count = raw.count(old)
    if count != 1:
        sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (what, count))
    return raw.replace(old, new, 1)


def main():
    raw = open(PATH, encoding="utf-8").read()
    if MARKER in raw:
        print("already applied  batched state resolution")
        return 0
    raw = replace_once(raw, BATCH_ANCHOR, BATCH_INSERT + BATCH_ANCHOR, "activate")
    raw = replace_once(raw, RESOLVE_OLD, RESOLVE_NEW, "resolve")
    with open(PATH, "w", encoding="utf-8") as stream:
        stream.write(raw)
    print("applied  batched state resolution")
    return 0


if __name__ == "__main__":
    sys.exit(main())
