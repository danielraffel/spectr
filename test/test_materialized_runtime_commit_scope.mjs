#!/usr/bin/env node
// A REACT COMMIT RE-APPLIES CAPTURED METADATA ONLY WHERE IT CHANGED SOMETHING.
//
// The vendored runtime re-applies Chromium-captured bindings after a React
// commit. Each layout binding forces two whole-tree layouts and each text
// binding re-shapes, so the cost of a commit is set by how much of the captured
// document it re-applies. This suite executes the shipping runtime's own source
// -- the host config's dirty marks and resetAfterCommit, the applier's scope
// test, the metadata hook wrapper and the registry finder -- in a vm against a
// minimal fake bridge, and asserts what each kind of commit costs:
//
//   HANDLER   a commit that only re-creates an `onX` closure marks nothing and
//             re-applies nothing.
//   PAINT     a commit that only changes a whitelisted paint key (a cursor, a
//             background) marks nothing and does not advance the epoch.
//   GEOMETRY  a commit that changes a geometric prop re-applies exactly that
//             node's subtree.
//   TEXT      a text commit re-applies exactly the element that owns the text.
//   STRUCTURE a container-level append, and a root resize, force the full pass.
//   EPOCH     the published mutation epoch is monotonic and moves only on a mark.
//   STATE     the captured-state refresh shares the gate, except when an
//             embedder's resolver is installed.
//   SCOPE     a binding is in scope exactly when its node is a scoped id or a
//             descendant of one.
//   POST      Spectr's responsive and optical passes follow a full pass and a
//             scoped pass that re-applied bindings, and nothing else.
//   MISSES    a registry miss is retained until the epoch moves, is never
//             retained without an epoch, and never under an element ancestor.
//
// Usage:
//   node test_materialized_runtime_commit_scope.mjs <runtime.js>
//        [--plant-no-paint-gate | --plant-unscoped-hook] [--expect-fail]
//
// --plant-no-paint-gate removes the paint-only exemption from commitUpdate.
// --plant-unscoped-hook hands the metadata hook `null` instead of the scope.
// --expect-fail inverts the verdict: green only when the suite REJECTS the
// document. It is not WILL_FAIL, which a usage error would also satisfy.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const runtimePath = args.find((a) => !a.startsWith("--"));
const expectFail = args.includes("--expect-fail");
if (!runtimePath) {
  console.error("usage: test_materialized_runtime_commit_scope.mjs <runtime.js> "
    + "[--plant-no-paint-gate|--plant-unscoped-hook] [--expect-fail]");
  process.exit(2);
}
let source = readFileSync(runtimePath, "utf8");

const plant = (label, from, to) => {
  const hits = source.split(from).length - 1;
  if (hits !== 1) {
    console.error(`FAIL: plant "${label}" found ${hits} sites, expected exactly 1 `
      + "-- the control cannot prove anything");
    process.exit(2);
  }
  source = source.replace(from, to);
  console.log("planted   %s", label);
};
if (args.includes("--plant-no-paint-gate")) {
  plant("commitUpdate without the paint-only gate",
    "      if (!isFixedTextOnlyUpdate(type, oldN, newN)\n"
    + "          && !isPaintOnlyUpdate(oldN, newN)) {\n",
    "      if (!isFixedTextOnlyUpdate(type, oldN, newN)) {\n");
}
if (args.includes("--plant-unscoped-hook")) {
  plant("an unscoped metadata hook call",
    'if (typeof metadataHook === "function") metadataHook(scope);',
    'if (typeof metadataHook === "function") metadataHook(null);');
}

const failures = [];
const fail = (msg) => failures.push(msg);

// Text between two markers, or null when either is absent. Absent pieces are
// reported as failures rather than crashing, so the unpatched runtime yields a
// verdict instead of a stack trace.
function lift(label, startMarker, endMarker, { includeEnd = false } = {}) {
  const start = source.indexOf(startMarker);
  if (start < 0) { fail(`${label}: "${startMarker.trim().slice(0, 60)}" is absent`); return null; }
  const end = source.indexOf(endMarker, start + startMarker.length);
  if (end < 0) { fail(`${label}: its end marker is absent`); return null; }
  return source.slice(start, includeEnd ? end + endMarker.length : end);
}

// ------------------------------------------------------------ host config
{
  const handler = lift("isEventHandler", "  function isEventHandler(key) {",
    "  function virtualListPropRank(key) {");
  const region = lift("host config", "  function asText(children) {",
    "  function attach(parent, child, index) {");
  if (handler && region) {
    const g = {
      setTimeout, clearTimeout, queueMicrotask, Promise, Object, Array, Set, Map,
      String, Number, Math, JSON, console,
      import_constants: { DefaultEventPriority: 32 },
      NoEventPriority: 0, currentUpdatePriority: 0,
      normalizeHostProps: (_t, p) => ({ ...(p || {}) }),
      applyChangedProps() {}, syncDomSemanticProps() {},
      attach() {}, detach() {}, attachToRoot() {}, call2() {},
      requestLayoutFlush() {}, applyButtonCaptionLayout2() {},
      unregisterDomSubtree() {}, shallowDiff: () => ({}),
      autoId: () => "auto", domRegistry: () => new Map(),
      rootSize: { width: 1320, height: 860 },
    };
    g.g4 = g;
    g.getRootSize = () => g.rootSize;
    const hookCalls = [];
    let stateCalls = 0;
    g.__pulpApplyMaterializedImportMetadata__ = (scope) => { hookCalls.push(scope); };
    g.__pulpRefreshMaterializedState__ = () => { ++stateCalls; };
    let host = null;
    try {
      host = vm.runInContext(
        "(function () {\n" + handler + region
        + "\nreturn { config: PulpHostConfig };\n})()",
        vm.createContext(g), { filename: "host-config.js" });
    } catch (error) {
      fail(`the host config does not evaluate: ${error.message}`);
    }
    if (host) {
      const cfg = host.config;
      const commit = () => {
        hookCalls.length = 0;
        stateCalls = 0;
        cfg.resetAfterCommit({});
        return { hooks: hookCalls.slice(), state: stateCalls };
      };
      const epoch = () => g.__pulpMaterializedTreeEpoch__;
      const instance = (id, props = {}) => ({ id, type: "div", props, childIds: [] });
      commit();                                   // the mount's full pass
      const idle = commit();
      if (idle.hooks.length !== 0)
        fail(`a commit that mutated nothing re-applied metadata ${idle.hooks.length} time(s)`);
      if (idle.state !== 0)
        fail("a commit that mutated nothing still refreshed the captured state");

      // HANDLER
      const box = instance("box");
      const e0 = epoch();
      cfg.commitUpdate(box, null, "div",
        { width: 10, onPointerMove: () => 1 }, { width: 10, onPointerMove: () => 2 });
      const handlerOnly = commit();
      console.log("measured  handler-only commit: %d re-apply(s)", handlerOnly.hooks.length);
      if (handlerOnly.hooks.length !== 0)
        fail("a handler-only commit re-applied captured metadata");
      if (epoch() !== e0) fail("a handler-only commit advanced the mutation epoch");

      // PAINT
      cfg.commitUpdate(box, null, "div",
        { width: 10, cursor: "crosshair", background: "red" },
        { width: 10, cursor: "grab", background: "blue" });
      const paintOnly = commit();
      console.log("measured  paint-only commit: %d re-apply(s)", paintOnly.hooks.length);
      if (paintOnly.hooks.length !== 0)
        fail("a paint-only commit (cursor, background) re-applied captured metadata");
      if (epoch() !== e0) fail("a paint-only commit advanced the mutation epoch");

      // An unknown key is geometric, even beside a paint key.
      cfg.commitUpdate(box, null, "div",
        { width: 10, cursor: "grab" }, { width: 10, cursor: "grab", gap: 4 });
      const unknown = commit();
      if (unknown.hooks.length !== 1)
        fail("a commit with an unrecognised prop key was treated as paint-only");

      // GEOMETRY
      const e1 = epoch();
      cfg.commitUpdate(box, null, "div", { width: 10 }, { width: 12 });
      const geometric = commit();
      console.log("measured  geometric commit: scope %j", geometric.hooks);
      if (geometric.hooks.length !== 1 || JSON.stringify(geometric.hooks[0]) !== '["box"]')
        fail(`a geometric commit re-applied scope ${JSON.stringify(geometric.hooks)}, `
          + 'expected exactly ["box"]');
      if (!(typeof epoch() === "number" && epoch() > e1))
        fail(`a geometric commit did not advance the epoch (${e1} -> ${epoch()})`);
      if (geometric.state !== 1)
        fail("a mutating commit did not refresh the captured state");

      // TEXT
      cfg.commitTextUpdate({ id: "t7", parentId: "leaf7" }, "old", "new");
      const text = commit();
      console.log("measured  text commit: scope %j", text.hooks);
      if (text.hooks.length !== 1 || JSON.stringify(text.hooks[0]) !== '["leaf7"]')
        fail(`a text commit re-applied scope ${JSON.stringify(text.hooks)}, `
          + 'expected exactly its owning leaf ["leaf7"]');

      // STRUCTURE
      cfg.commitUpdate(box, null, "div", { width: 12 }, { width: 14 });
      cfg.appendChildToContainer({}, instance("rootchild"));
      const structural = commit();
      if (structural.hooks.length !== 1 || structural.hooks[0] != null)
        fail(`a container append re-applied scope ${JSON.stringify(structural.hooks)}; `
          + "it must force the full pass, and a scoped mark must not narrow it");
      g.rootSize = { width: 990, height: 645 };
      const resized = commit();
      if (resized.hooks.length !== 1 || resized.hooks[0] != null)
        fail("a root resize did not force the full pass");
      const parent = instance("list");
      cfg.appendChild(parent, instance("row"));
      const appended = commit();
      const scope = appended.hooks[0];
      if (!Array.isArray(scope) || !scope.includes("list") || !scope.includes("row"))
        fail(`an append re-applied scope ${JSON.stringify(scope)}; it must cover `
          + "the parent whose children renumber and the new child");

      // EPOCH
      const e2 = epoch();
      cfg.commitUpdate(box, null, "div", { width: 14 }, { width: 16 });
      cfg.commitUpdate(box, null, "div", { width: 16 }, { width: 18 });
      if (!(epoch() === e2 + 2)) fail(`two marks moved the epoch ${e2} -> ${epoch()}`);
      commit();

      // STATE resolver keeps the unconditional refresh.
      g.__pulpMaterializedStateResolver__ = () => null;
      const resolverIdle = commit();
      if (resolverIdle.state !== 1)
        fail("with a state resolver installed the refresh must stay unconditional");
      delete g.__pulpMaterializedStateResolver__;
    }
  }
}

// ------------------------------------------------------------- applier scope
{
  const helpers = lift("scope helpers", "  function materializedScopeSet(scopeIds) {",
    "  // `scopeIds` is optional");
  if (helpers) {
    const scope = vm.runInContext("(function () {\n" + helpers
      + "\nreturn { set: materializedScopeSet, inScope: materializedNodeInScope };\n})()",
      vm.createContext({ Set, Array, String }));
    const node = (id, parent) => ({ __pulpId: id, parentElement: parent || null });
    const root = node("root");
    const header = node("header", root);
    const leaf = node("leaf", header);
    const other = node("other", root);
    const set = scope.set(["header"]);
    if (!scope.inScope(leaf, set)) fail("a descendant of a scoped id is out of scope");
    if (!scope.inScope(header, set)) fail("a scoped node is out of its own scope");
    if (scope.inScope(other, set)) fail("a sibling of the scoped subtree is in scope");
    if (scope.inScope(root, set)) fail("an ancestor of the scoped subtree is in scope");
    if (scope.set([]) !== null || scope.set(null) !== null)
      fail("an empty or absent scope must mean 'apply everything' (null)");
  }
}

// ---------------------------------------------------- Spectr post-apply passes
{
  const wrapper = lift("metadata hook wrapper",
    "  g5.__pulpApplyMaterializedImportMetadata__ = function(",
    "\n  };\n", { includeEnd: true });
  const helpers = lift("scope helpers (post)", "  function materializedScopeSet(scopeIds) {",
    "  function materializedNodeInScope(node, scopeSet) {");
  if (wrapper && helpers) {
    const counts = { responsive: 0, toolbar: 0, header: 0, canvas: 0 };
    const ctx = {
      Set, Array, String, counts, appliedNext: 0,
      activeMaterializedMetadata: {},
    };
    ctx.g5 = ctx;
    ctx.__spectrResponsiveLayoutReceipt__ = { width: 1320, height: 860 };
    ctx.applyMaterializedImportMetadata = () => ctx.appliedNext;
    ctx.applySpectrResponsiveLayout = () => { ++counts.responsive; };
    ctx.applySpectrToolbarOpticalCentering = () => { ++counts.toolbar; };
    ctx.applySpectrHeaderOpticalCentering = () => { ++counts.header; };
    ctx.syncMaterializedCanvasBehaviorsAfterCommit = () => { ++counts.canvas; };
    vm.runInContext(helpers + wrapper, vm.createContext(ctx));
    const run = (scope, applied) => {
      for (const k of Object.keys(counts)) counts[k] = 0;
      ctx.appliedNext = applied;
      ctx.__pulpApplyMaterializedImportMetadata__(scope);
      return { ...counts };
    };
    const full = run(undefined, 5);
    if (!full.responsive || !full.toolbar || !full.header || !full.canvas)
      fail(`a full pass skipped Spectr's post-apply passes: ${JSON.stringify(full)}`);
    const emptyScoped = run(["plot"], 0);
    console.log("measured  scoped pass re-applying nothing: %j", emptyScoped);
    if (emptyScoped.responsive || emptyScoped.toolbar || emptyScoped.header || emptyScoped.canvas)
      fail("a scoped pass that re-applied no binding still ran Spectr's post-apply "
        + `passes: ${JSON.stringify(emptyScoped)}`);
    const busyScoped = run(["header"], 2);
    if (!busyScoped.responsive || !busyScoped.toolbar || !busyScoped.header)
      fail("a scoped pass that re-applied captured bindings skipped the passes that "
        + `correct them: ${JSON.stringify(busyScoped)}`);
  }
}

// ------------------------------------------------------------ registry misses
{
  const finder = lift("registry finder", "  let materializedFindMissEpoch = null;",
    "  g5.__pulpActivateMaterializedElement__");
  if (finder) {
    let scans = 0;
    const registry = [{ tagName: "div", __pulpId: "a" }];
    const ctx = {
      Set, String, Array,
      materializedDomRegistryValues: () => { ++scans; return registry; },
      materializedMatches: (node, sel) => node && node.tagName === sel,
      materializedClosest: () => true,
      materializedLastDescendantSplit: () => -1,
    };
    ctx.g5 = ctx;
    vm.runInContext(finder, vm.createContext(ctx));
    const find = (sel, anc) => ctx.__pulpFindMaterializedElement__(sel, anc);
    find("span"); find("span");
    if (scans !== 2) fail(`with no epoch published a miss was retained (${scans} scans for 2 lookups)`);
    ctx.__pulpMaterializedTreeEpoch__ = 1;
    scans = 0;
    find("span"); find("span"); find("span");
    console.log("measured  3 repeated misses at one epoch: %d registry scan(s)", scans);
    if (scans !== 1) fail(`three identical misses at one epoch scanned the registry ${scans} times`);
    ctx.__pulpMaterializedTreeEpoch__ = 2;
    scans = 0;
    find("span");
    if (scans !== 1) fail("a retained miss survived an epoch change");
    registry.push({ tagName: "span", __pulpId: "s" });
    ctx.__pulpMaterializedTreeEpoch__ = 3;
    if (!find("span")) fail("a node added at a new epoch was not found");
    if (!find("span")) fail("a hit was not served on repeat");
    scans = 0;
    const element = { tagName: "section" };
    find("p", element); find("p", element);
    if (scans !== 2) fail("a miss under an element ancestor was retained");
  }
}

// ---------------------------------------------------- one face pass per pass
// The applier set the Settings labels' face in a block that had been
// appended 57 times: 171 registry scans and 171 bridge setters on every
// pass, scoped or not. One copy does the same work.
{
  const block = 'for (const labelText of ["APPEARANCE", "Theme", "Bloom"]) {';
  const copies = source.split(block).length - 1;
  console.log("measured  Settings label face block: %d cop%s", copies,
    copies === 1 ? "y" : "ies");
  if (copies !== 1) {
    fail(`the Settings label face block runs ${copies} times per metadata `
      + "pass; each copy is a full registry scan per label for no change");
  }
}

// ------------------------------------------- one path walk per binding per pass
// The applier resolves every captured binding's node by walking its path from
// the registry roots, and asked up to five times per binding per pass (once
// per filter, once to apply), re-filtering each node's children on every step
// -- a querySelector per Settings-overlay candidate. With a pass-scoped
// children memo, each node's children are filtered once per pass however many
// bindings walk through it.
{
  const walkSrc = lift("path walk", "  function materializedElementChildren(node, registrySet) {",
    "  function ensureSpectrNativeScrollView(node, values) {");
  if (walkSrc) {
    let filters = 0;
    const node = (tag, children = []) => {
      const n = { tagName: tag, _children: children,
        getAttribute(name) { filters += name === "aria-label" ? 1 : 0; return null; } };
      for (const c of children) c.parentElement = n;
      return n;
    };
    const leaves = Array.from({ length: 12 }, () => node("span"));
    const row = node("div", leaves);
    const root = node("div", [node("div", [row])]);
    const values = [root, root._children[0], row, ...leaves];
    const sandbox = { Set, Map, Array, String };
    vm.runInNewContext(walkSrc + `
      globalThis.walk = (bindings, values, memoised) => {
        const index = materializedPathIndex(values);
        if (memoised) index.childrenMemo = new Map();
        for (const b of bindings) materializedNodeAtPath(b, values, true, index);
      };`, sandbox);
    const bindings = leaves.map((_, i) => ({ path: [
      { tag: "div", index: 0 }, { tag: "div", index: 0 },
      { tag: "div", index: 0 }, { tag: "span", index: i }] }));
    filters = 0;
    sandbox.walk(bindings, values, true);
    const memoised = filters;
    filters = 0;
    sandbox.walk(bindings, values, false);
    const unmemoised = filters;
    console.log("measured  12 bindings through one row: %d child filters with the "
      + "pass memo, %d without", memoised, unmemoised);
    // Every node on the shared path is filtered once: root's, the wrapper's,
    // the row's children -- 1 + 1 + 12 checks -- however many bindings walk it.
    if (memoised > 14) {
      fail(`12 bindings sharing one path re-filtered children ${memoised} times; `
        + "a pass-scoped memo filters each node's children once");
    }
    if (!(unmemoised > memoised)) {
      fail("the walk without a memo did no more work than with one, so this row "
        + "cannot tell the two apart");
    }
  }
  if (!source.includes("pathIndex.childrenMemo = ")
      || !source.includes("const nodeAtPath = (binding) => {")) {
    fail("the metadata applier does not share path walks across a pass");
  }
}

// ------------------------------------------------------------------ verdict
if (expectFail) {
  if (failures.length === 0) {
    console.error("FAIL: the planted runtime PASSED -- the control does not "
      + "discriminate and proves nothing");
    process.exit(1);
  }
  console.log("PASS (inverted): the planted runtime was rejected.");
  for (const f of failures) console.log("  rejected: " + f);
  process.exit(0);
}
if (failures.length > 0) {
  for (const f of failures) console.error("FAIL: " + f);
  console.error(`\n${failures.length} failure(s).`);
  process.exit(1);
}
console.log("PASS: commits re-apply captured metadata only where they changed "
  + "something, and repeated registry misses cost one scan per epoch.");
