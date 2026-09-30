#!/usr/bin/env python3
"""Scope the per-commit captured-metadata re-apply in the vendored runtime.

WHY THIS EXISTS

    `native-ui/materialized/runtime.js` is a CHECKED-IN copy of Pulp's React
    host config and materialized-import runtime, and it is what the shipping
    editor runs. The copy predates four @pulp/react changes that decide how much
    work a React commit costs, so none of them reaches Spectr:

      * A PAINT-ONLY GATE. `commitUpdate` marks the whole materialized tree
        dirty for any prop change except a fixed-size text swap. A commit that
        only rewrites a tint or a cursor -- what a hover or a drag emits by the
        hundred -- therefore re-applies Chromium-captured metadata across the
        entire captured document.
      * HANDLER EXEMPTION. React recreates every inline `onX` closure on each
        render, so a handler-only diff -- the most common commit during a drag
        -- disqualified the commit on a payload that is a function identity.
      * SUBTREE SCOPE. Even a genuinely geometric commit changes one subtree,
        yet the dirty state is one boolean and `__pulpApplyMaterializedImport
        Metadata__` takes no argument, so every re-apply walks every layout,
        paint and text binding -- two forced whole-tree layouts per layout
        binding and a re-shape per text binding.
      * A GATED STATE HOOK AND RETAINED REGISTRY MISSES. `resetAfterCommit`
        calls `__pulpRefreshMaterializedState__` on every commit, and each call
        resolves one selector per captured state, where every miss reads and
        match-tests the whole DOM registry.

    Upstream, these are @pulp/react's `isPaintOnlyUpdate` / PAINT_ONLY_KEYS,
    the `onX` exemption, `markMaterializedTreeDirty(scopeId)` with a full-pass
    escape hatch, the scoped `applyMaterializedImportMetadata(metadata,
    scopeIds)`, the published `__pulpMaterializedTreeEpoch__` and the epoch-keyed
    miss set in `__pulpFindMaterializedElement__`. This script writes the same
    behaviour into this artifact's own dialect (`g4` in the host config, `g5` in
    the runtime entry).

SEMANTICS KEPT EXACTLY

    * Absent or empty scope means "apply everything". An unscoped mark -- a
      container-level append, reorder or clear, a removal from the container --
      forces the full pass, and so does a root resize or a metadata or state
      hook that arrived or was swapped. A scoped mark never narrows a pending
      unscoped one.
    * An unrecognised prop key means "geometric": only keys on the curated
      whitelist, and `onX` handlers, are exempt.
    * The shipped `__pulpMaterializedMetadataDiagnostics__` keeps describing the
      last FULL pass; a scoped pass publishes beside it, so validators that read
      applied-versus-expected never see a scoped pass as a mass miss.

    Two adaptations to this artifact, both in the safe direction:

    * `commitTextUpdate` is reachable here (upstream marks it unscoped because
      it never runs). A text instance has no captured node of its own, so it is
      scoped to the element that owns it -- its parent -- and falls back to the
      full pass when it has none.
    * Retained misses are keyed by selector and a STRING ancestor only. Elements
      of the registry pass themselves as the ancestor of `querySelector`, and an
      element has no stable string form, so those lookups are not retained
      rather than risk two elements sharing one cached miss.

SPECTR'S OWN POST-APPLY WORK

    `__pulpApplyMaterializedImportMetadata__` also runs Spectr's responsive
    layout, both optical-centring passes and the canvas behaviour sync after
    every re-apply. On a scoped pass these run only when the pass actually
    re-applied a captured binding: those passes exist to correct captured
    geometry and line boxes, so a scoped pass that re-wrote one needs them, and
    one that re-wrote nothing (the common case during a drag over the plot)
    does not. A full pass always runs them.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous, or the file is half patched.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized", "runtime.js")

PAINT_ONLY_BLOCK = """  // Prop keys whose value can change without moving a single box. Curated by
  // hand: a key qualifies only when its native setter repaints without
  // invalidating layout AND no captured-import binding writes the same
  // channel. `opacity` and `color` fail the second test -- the metadata pass
  // drives them itself -- and the border shorthands carry a width, so neither
  // is here. An omission costs the full re-apply; a wrong entry costs a stale
  // layout, so when in doubt a key stays out.
  var PAINT_ONLY_KEYS = /* @__PURE__ */ new Set([
    "background", "backgroundColor", "backgroundGradient", "backgroundImage",
    "backgroundAttachment", "backgroundClip", "backgroundOrigin", "backgroundRepeat",
    "borderColor", "borderTopColor", "borderRightColor",
    "borderBottomColor", "borderLeftColor", "borderCurve",
    "outlineColor", "outlineStyle",
    "boxShadow", "backdropFilter", "filter", "clipPath",
    "mask", "maskImage", "maskSize", "mixBlendMode", "isolation",
    "backfaceVisibility",
    "shadowColor", "shadowOffset", "shadowOpacity", "shadowRadius",
    "cursor", "userSelect", "pointerEvents"
  ]);
  // True when every key a commit changed is provably non-geometric: on the
  // whitelist above, or an `onX` handler, whose payload is a function
  // identity React recreates on every render. An unrecognised key means
  // "assume geometric". A commit that changed nothing is not evidence a
  // repaint is safe and takes the ordinary path.
  function isPaintOnlyUpdate(oldProps, newProps) {
    let changed = 0;
    const keys = /* @__PURE__ */ new Set([...Object.keys(oldProps), ...Object.keys(newProps)]);
    for (const key of keys) {
      if (Object.is(oldProps[key], newProps[key])) continue;
      if (!PAINT_ONLY_KEYS.has(key) && !isEventHandler(key)) return false;
      changed += 1;
    }
    return changed > 0;
  }
"""

EDITS = [
    (
        "text-only updates ignore re-created handlers; paint-only gate",
        """    for (const key of nonTextKeys) {
      if (oldProps[key] !== newProps[key]) return false;
    }
    return true;
  }
  var PulpHostConfig = {""",
        """    for (const key of nonTextKeys) {
      // A re-created inline handler is not a geometry change.
      if (isEventHandler(key)) continue;
      if (oldProps[key] !== newProps[key]) return false;
    }
    return true;
  }
""" + PAINT_ONLY_BLOCK + """  var PulpHostConfig = {""",
    ),
    (
        "appendInitialChild marks the parent and the child",
        """    appendInitialChild(parentInstance, child) {
      markMaterializedTreeDirty();
""",
        """    appendInitialChild(parentInstance, child) {
      markMaterializedTreeDirty(parentInstance.id);
      markMaterializedTreeDirty(child.id);
""",
    ),
    (
        "appendChild marks the parent and the child",
        """    appendChild(parentInstance, child) {
      markMaterializedTreeDirty();
""",
        """    appendChild(parentInstance, child) {
      markMaterializedTreeDirty(parentInstance.id);
      markMaterializedTreeDirty(child.id);
""",
    ),
    (
        "insertBefore marks the reordered parent and the moved child",
        """    insertBefore(parentInstance, child, beforeChild) {
      markMaterializedTreeDirty();
""",
        """    insertBefore(parentInstance, child, beforeChild) {
      // A reorder renumbers the parent's whole child list, so the parent is the
      // scope; the child too, because a cross-parent insert moves it out of a
      // subtree the parent no longer contains.
      markMaterializedTreeDirty(parentInstance.id);
      markMaterializedTreeDirty(child.id);
""",
    ),
    (
        "removeChild marks the parent whose children renumber",
        """    removeChild(parentInstance, child) {
      markMaterializedTreeDirty();
""",
        """    removeChild(parentInstance, child) {
      // The child is leaving; its former siblings are what renumber.
      markMaterializedTreeDirty(parentInstance.id);
""",
    ),
    (
        "commitUpdate skips paint-only commits and scopes the rest",
        """      if (!isFixedTextOnlyUpdate(type, oldN, newN)) markMaterializedTreeDirty();
""",
        """      if (!isFixedTextOnlyUpdate(type, oldN, newN)
          && !isPaintOnlyUpdate(oldN, newN)) {
        markMaterializedTreeDirty(instance.id);
      }
""",
    ),
    (
        "commitTextUpdate scopes to the element that owns the text",
        """    commitTextUpdate(textInstance, _oldText, newText) {
      markMaterializedTreeDirty();
""",
        """    commitTextUpdate(textInstance, _oldText, newText) {
      // A text instance has no captured node of its own; the element that
      // owns it is the leaf whose bindings the new text can affect.
      markMaterializedTreeDirty(textInstance && textInstance.parentId);
""",
    ),
    (
        "resetTextContent scopes to its own node",
        """    resetTextContent(instance) {
      markMaterializedTreeDirty();
""",
        """    resetTextContent(instance) {
      markMaterializedTreeDirty(instance.id);
""",
    ),
    (
        "resetAfterCommit passes the scope and gates the state hook",
        """      const shouldReapply =
        materializedTreeDirty || rootSignature !== materializedRootSignature;
      materializedRootSignature = rootSignature;
      materializedTreeDirty = false;
      if (shouldReapply) {
        const metadataHook = g4.__pulpApplyMaterializedImportMetadata__;
        if (typeof metadataHook === "function") metadataHook();
      }
      // The state hook is cheap when the state is unchanged and re-applies the
      // metadata itself when it is not, so it stays unconditional.
      const stateHook = g4.__pulpRefreshMaterializedState__;
      if (typeof stateHook === "function") stateHook();
""",
        """      const metadataHook = g4.__pulpApplyMaterializedImportMetadata__;
      const stateHook = g4.__pulpRefreshMaterializedState__;
      // Reasons OTHER than the per-node marks -- a root resize, or a hook that
      // arrived or was swapped -- invalidate evidence the marks say nothing
      // about, so they force the full pass. Only an ARRIVING state hook counts;
      // one being torn down leaves nothing to refresh.
      const unscopedReason = materializedTreeDirtyAll
        || rootSignature !== materializedRootSignature
        || metadataHook !== materializedHookApplied
        || (typeof stateHook === "function"
          && stateHook !== materializedStateHookApplied);
      const shouldReapply = unscopedReason || materializedDirtyIds.size > 0;
      // `null` means "no scope, re-apply everything".
      const scope = unscopedReason ? null : Array.from(materializedDirtyIds);
      materializedRootSignature = rootSignature;
      materializedTreeDirtyAll = false;
      materializedDirtyIds.clear();
      if (shouldReapply) {
        materializedHookApplied = metadataHook;
        if (typeof metadataHook === "function") metadataHook(scope);
      }
      // Captured-state matching resolves selectors over the registry, and a
      // commit that mutated no host node cannot have changed which selector
      // answers, so it shares the gate. A state resolver an embedder installs
      // is not a function of the registry, so it keeps the unconditional
      // refresh.
      const stateResolverInstalled =
        typeof g4.__pulpMaterializedStateResolver__ === "function";
      if (typeof stateHook === "function"
          && (shouldReapply || stateResolverInstalled)) {
        materializedStateHookApplied = stateHook;
        stateHook();
      }
""",
    ),
    (
        "dirty state records scoped marks and publishes a mutation epoch",
        """  let materializedTreeDirty = true;
  let materializedRootSignature = "";
  function markMaterializedTreeDirty() {
    materializedTreeDirty = true;
  }
""",
        """  //
  // One level deeper: a commit that mutated ONE node left every other captured
  // node's geometry as it was, so each mark records the subtree root whose
  // descendants-or-self may have moved, and the re-apply is restricted to that
  // scope. `materializedTreeDirtyAll` is the escape hatch for a mutation whose
  // blast radius is not one subtree; it is kept separate from the id set so a
  // later scoped mark can never narrow an earlier unscoped one.
  let materializedTreeDirtyAll = true;
  const materializedDirtyIds = /* @__PURE__ */ new Set();
  let materializedRootSignature = "";
  // Hook identities last applied, so a hook that arrives or is swapped forces
  // the full pass rather than waiting for the next host mutation.
  let materializedHookApplied;
  let materializedStateHookApplied;
  // Monotonic mutation counter, published on the first mark. The runtime keys
  // retained registry misses by it; before the first mark it is absent and
  // the runtime declines to retain anything.
  let materializedTreeEpoch = 0;
  // `scopeId` names the subtree root whose descendants-or-self may have moved;
  // omitting it means "blast radius unknown" and forces the full re-apply.
  function markMaterializedTreeDirty(scopeId) {
    if (typeof scopeId === "string" && scopeId.length > 0) {
      materializedDirtyIds.add(scopeId);
    } else {
      materializedTreeDirtyAll = true;
    }
    materializedTreeEpoch += 1;
    g4.__pulpMaterializedTreeEpoch__ = materializedTreeEpoch;
  }
""",
    ),
    (
        "applier accepts a scope",
        """  function applyMaterializedImportMetadata(metadata) {
    const values = materializedDomRegistryValues();
""",
        """  // A commit's dirty scope is a list of native ids whose subtrees may have
  // moved. A binding is in scope when its node is one of them or a descendant,
  // which is a walk UP the parent chain: pure JS, where the work it avoids is
  // bridge traffic and forced layouts.
  function materializedScopeSet(scopeIds) {
    if (!Array.isArray(scopeIds) || scopeIds.length === 0) return null;
    const set = /* @__PURE__ */ new Set();
    for (const id of scopeIds) {
      if (id === null || id === void 0) continue;
      const text = String(id);
      if (text) set.add(text);
    }
    return set.size > 0 ? set : null;
  }
  function materializedNodeInScope(node, scopeSet) {
    let current = node;
    // Bounded, so a detached node that cycles cannot hang a commit.
    for (let depth = 0; current && depth < 4096; ++depth) {
      const id = current.__pulpId || current.id;
      if (id && scopeSet.has(String(id))) return true;
      current = current.parentElement || current._parentElement || null;
    }
    return false;
  }
  // `scopeIds` is optional; absent or empty means "apply everything". Every
  // caller other than the per-commit hook passes nothing.
  function applyMaterializedImportMetadata(metadata, scopeIds) {
    const scopeSet = materializedScopeSet(scopeIds);
    const values = materializedDomRegistryValues();
""",
    ),
    (
        "diagnostics count layout bindings skipped by scope",
        """      layout_node_miss: 0,
      layout_dynamic_nodes""",
        """      layout_node_miss: 0,
      layout_out_of_scope: 0,
      layout_dynamic_nodes""",
    ),
    (
        "diagnostics count text bindings skipped by scope",
        """      text_node_miss: 0,
      text_content_mismatch: 0,""",
        """      text_node_miss: 0,
      text_out_of_scope: 0,
      text_content_mismatch: 0,""",
    ),
    (
        "diagnostics count paint bindings skipped by scope",
        """      paint_node_miss: 0,
      paint_unsupported: 0,""",
        """      paint_node_miss: 0,
      paint_out_of_scope: 0,
      paint_unsupported: 0,""",
    ),
    (
        "layout bindings outside the scope are skipped",
        """        if (!id) {
          ++diagnostics.layout_node_miss;
          continue;
        }
        const parent = node.parentElement || node._parentElement;""",
        """        if (!id) {
          ++diagnostics.layout_node_miss;
          continue;
        }
        if (scopeSet && !materializedNodeInScope(node, scopeSet)) {
          ++diagnostics.layout_out_of_scope;
          continue;
        }
        const parent = node.parentElement || node._parentElement;""",
    ),
    (
        "paint bindings outside the scope are skipped",
        """      if (!id) {
        ++diagnostics.paint_node_miss;
        continue;
      }
      diagnostics.paint_nodes.push({""",
        """      if (!id) {
        ++diagnostics.paint_node_miss;
        continue;
      }
      if (scopeSet && !materializedNodeInScope(node, scopeSet)) {
        ++diagnostics.paint_out_of_scope;
        continue;
      }
      diagnostics.paint_nodes.push({""",
    ),
    (
        "text bindings outside the scope are skipped",
        """        else ++diagnostics.text_node_miss;
        continue;
      }
      const anonymousTargets""",
        """        else ++diagnostics.text_node_miss;
        continue;
      }
      if (scopeSet && !materializedNodeInScope(node, scopeSet)) {
        ++diagnostics.text_out_of_scope;
        continue;
      }
      const anonymousTargets""",
    ),
    (
        "a scoped pass publishes its diagnostics beside the full pass's",
        """    g5.__pulpMaterializedMetadataDiagnostics__ = diagnostics;
    return applied;
  }
  function applySpectrToolbarOpticalCentering() {""",
        """    // A scoped pass skips most of the document by construction, so its
    // counts are not comparable to a full pass's; the shipped key keeps the
    // last FULL application and a scoped pass publishes beside it.
    if (scopeSet) {
      diagnostics.scoped = true;
      diagnostics.scope_size = scopeSet.size;
      g5.__pulpMaterializedScopedApplyDiagnostics__ = diagnostics;
    } else {
      g5.__pulpMaterializedMetadataDiagnostics__ = diagnostics;
    }
    return applied;
  }
  function applySpectrToolbarOpticalCentering() {""",
    ),
    (
        "Spectr's post-apply passes follow only a pass that re-applied bindings",
        """  g5.__pulpApplyMaterializedImportMetadata__ = function() {
    const applied = applyMaterializedImportMetadata(activeMaterializedMetadata);
""",
        """  g5.__pulpApplyMaterializedImportMetadata__ = function(scopeIds) {
    const applied = applyMaterializedImportMetadata(
      activeMaterializedMetadata, scopeIds);
    // The responsive layout, optical centring and canvas behaviour passes
    // correct captured geometry, line boxes and canvas bindings. A full pass
    // always needs them; a scoped pass needs them only when it re-applied a
    // captured binding, and one that re-applied nothing leaves them as they
    // were.
    g5.__spectrScopedPostApplySkipped__ =
      (g5.__spectrScopedPostApplySkipped__ || 0);
    if (materializedScopeSet(scopeIds) && applied === 0) {
      ++g5.__spectrScopedPostApplySkipped__;
      return applied;
    }
""",
    ),
    (
        "registry misses are retained until the next host mutation",
        """      if (browserNode && (!ancestor || materializedClosest(browserNode, ancestor))) {
        return browserNode;
      }
    }
    let targetSelector = selector.trim();""",
        """      if (browserNode && (!ancestor || materializedClosest(browserNode, ancestor))) {
        return browserNode;
      }
    }
    // A miss reads and match-tests every registry node, and captured-state
    // resolution asks one selector per state on every commit. A miss cannot
    // become a hit without a host mutation, and every host mutation bumps the
    // published epoch, so misses are retained per epoch. Only a string
    // ancestor is part of the key: an element ancestor has no stable key, so
    // those lookups are not retained. With no epoch published nothing is.
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
      if (materializedFindMisses.has(missKey)) return null;
    }
    let targetSelector = selector.trim();""",
    ),
    (
        "a registry miss is recorded for its epoch",
        """      ))) return node;
    }
    return null;
  };
  g5.__pulpActivateMaterializedElement__ = function(selector, eventName, eventData) {""",
        """      ))) return node;
    }
    if (missCacheable) materializedFindMisses.add(missKey);
    return null;
  };
  g5.__pulpActivateMaterializedElement__ = function(selector, eventName, eventData) {""",
    ),
    (
        "the retained-miss set is declared beside the finder",
        """    return split;
  }
  g5.__pulpFindMaterializedElement__ = function(selector, ancestor) {""",
        """    return split;
  }
  let materializedFindMissEpoch = null;
  const materializedFindMisses = /* @__PURE__ */ new Set();
  g5.__pulpFindMaterializedElement__ = function(selector, ancestor) {""",
    ),
]

# Present only when every edit landed; absent tokens the patch removes.
REQUIRED_AFTER = {
    "function isPaintOnlyUpdate(oldProps, newProps) {": 1,
    "function markMaterializedTreeDirty(scopeId) {": 1,
    "g4.__pulpMaterializedTreeEpoch__ = materializedTreeEpoch;": 1,
    "if (typeof metadataHook === \"function\") metadataHook(scope);": 1,
    "function applyMaterializedImportMetadata(metadata, scopeIds) {": 1,
    "g5.__pulpMaterializedScopedApplyDiagnostics__ = diagnostics;": 1,
    "if (missCacheable) materializedFindMisses.add(missKey);": 1,
    "if (scopeSet && !materializedNodeInScope(node, scopeSet)) {": 3,
}
FORBIDDEN_AFTER = (
    "let materializedTreeDirty = true;",
    "materializedTreeDirty = false;",
    "if (!isFixedTextOnlyUpdate(type, oldN, newN)) markMaterializedTreeDirty();",
    "if (typeof stateHook === \"function\") stateHook();",
)


def main() -> int:
    for label, old, new in EDITS:
        if old in new:
            print(f"error: patch point survives its own replacement: {label}",
                  file=sys.stderr)
            return 1
    try:
        with open(PATH, "r", encoding="utf-8") as handle:
            source = handle.read()
    except OSError as error:
        print(f"error: cannot read {PATH}: {error}", file=sys.stderr)
        return 1

    applied = 0
    already = 0
    for label, old, new in EDITS:
        if source.count(old) == 0 and source.count(new) == 1:
            already += 1
            continue
        count = source.count(old)
        if count != 1:
            print(f"error: patch point {count}x (want 1): {label}", file=sys.stderr)
            return 1
        source = source.replace(old, new, 1)
        applied += 1
        print(f"applied  {label}")

    if applied == 0:
        print(f"already applied: all {already} edits present")
        return 0
    if already:
        print(f"error: partially applied ({already} present, {applied} missing) -- "
              "refusing a half-written artifact", file=sys.stderr)
        return 1

    for token, want in REQUIRED_AFTER.items():
        got = source.count(token)
        if got != want:
            print(f"error: {token!r} appears {got}x after patching (want {want})",
                  file=sys.stderr)
            return 1
    for token in FORBIDDEN_AFTER:
        if token in source:
            print(f"error: {token!r} survives patching", file=sys.stderr)
            return 1

    with open(PATH, "w", encoding="utf-8") as handle:
        handle.write(source)
    print(f"applied {applied} edits to {os.path.relpath(PATH, REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
