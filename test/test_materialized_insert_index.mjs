#!/usr/bin/env node
// Proves a child's AUTHORED index is its NATIVE index -- including for a
// subtree that mounts late into a parent that kept its other children.
//
// The user's report: "after messing with presets/snapshot open the dropdown
// gets messed up again ... do you know the root cause of the layout being
// changed in them". The screenshots show EDIT MODE and ANALYZER with each
// row's caption and its description separated and overlapping: SCULPT/"Free-",
// LEVEL/"Flat", PEAK/its description.
//
// WHY THAT HAPPENS, and why it is a MOUNT bug rather than a layout bug:
//
//   Every native createX APPENDS -- the widget factory has no other mode.
//   host-config's attach() computes the right insert index and keeps `childIds`
//   and the DOM shim in that order, but the vendored runtime then called
//   materialize() WITHOUT the index, so the index was dropped on the floor.
//   A row that mounts late lands LAST rather than in place. Re-opening a
//   dropdown remounts its rows into an overlay that kept its other children,
//   so the rows come back after the siblings that stayed and every caption
//   separates from the body it labels.
//
//   It is state-dependent, which is why the menu is right until something
//   remounts it, and why "it was fine before this" is the correct observation
//   rather than a red herring.
//
// Pulp #8272 (8042c3815) fixed BOTH halves of the seam. SDK v0.850.0 carries
// the native half: `insertChild` is a registered bridge function over
// View::move_child_to_index. The renderer half never reached this repo, because
// native-ui/materialized/runtime.js is a CHECKED-IN artifact that an SDK repin
// does not touch. tools/patch_materialized_runtime_insert_index.py transplants
// it; this suite is what proves the transplant is present and load-bearing.
//
// The artifact already contained an `insertChild` call BEFORE that transplant,
// in insertBefore()'s same-parent REORDER branch, which predates #8272 and was
// dead code until the SDK defined the function. So a grep for the identifier
// says nothing about whether the MOUNT path is wired, and this suite therefore
// asserts on emitted bridge traffic and resulting order -- never on the
// presence of a token.
//
// WHAT IS EXECUTED: the artifact's OWN attach / materialize / materializeUnder,
// extracted by brace matching and run against a recording bridge whose
// createWidget APPENDS exactly like the native factory and whose insertChild
// repositions exactly like View::move_child_to_index. The assertion is that the
// recorded native order equals the renderer's own `childIds` for every parent.
// Nothing here restates the implementation: the expected order is the authored
// order, which the test states independently.
//
// Usage:
//   node test_materialized_insert_index.mjs <runtime.js>
//        [--plant-drop-index] [--plant-append-only] [--plant-queue-order]
//        [--expect-fail]
//
// --plant-drop-index   restores the exact pre-fix shape: materialize() ignores
//                      its index argument. This is the defect that shipped.
// --plant-append-only  keeps the threading but removes the insertChild emit --
//                      the plausible half-fix, and the reason the assertion is
//                      on native order rather than on call arity.
// --plant-queue-order  restores queue-order draining of a deferred parent's
//                      pendingChildren, which scrambles a subtree that is
//                      reordered before its parent reaches the bridge.
// --expect-fail        inverts the verdict, so a control row is green only when
//                      the planted defect is REJECTED, and a missing file or a
//                      thrown extractor still fails it.
//
// Exit 0 pass, 1 fail, 2 usage/extraction error. Never 77: this needs no
// device, no GPU and no build, so a skip here would be a bug.

import fs from 'node:fs';

const args = process.argv.slice(2);
const plantDropIndex = args.includes('--plant-drop-index');
const plantAppendOnly = args.includes('--plant-append-only');
const plantQueueOrder = args.includes('--plant-queue-order');
const expectFail = args.includes('--expect-fail');
const runtimePath = args.find((a) => !a.startsWith('--'));

if (!runtimePath) {
  console.error('usage: test_materialized_insert_index.mjs <runtime.js> '
    + '[--plant-drop-index] [--plant-append-only] [--plant-queue-order] '
    + '[--expect-fail]');
  process.exit(2);
}

let source;
try {
  source = fs.readFileSync(runtimePath, 'utf8');
} catch (error) {
  console.error('FAIL: cannot read ' + runtimePath + ': ' + error.message);
  process.exit(2);
}

// ---------------------------------------------------------------- extraction
// Brace-match a top-level `function NAME(` out of the bundle. Anchored on the
// declaration text so a call site cannot be mistaken for the definition.
function extractFunction(name) {
  const anchor = '  function ' + name + '(';
  const start = source.indexOf(anchor);
  if (start < 0) throw new Error('no declaration for ' + name);
  if (source.indexOf(anchor, start + 1) >= 0) {
    throw new Error('ambiguous declaration for ' + name);
  }
  let i = source.indexOf('{', start);
  if (i < 0) throw new Error('no body for ' + name);
  let depth = 0;
  for (; i < source.length; ++i) {
    const ch = source[i];
    if (ch === '{') ++depth;
    else if (ch === '}') {
      --depth;
      if (depth === 0) return source.slice(start, i + 1);
    }
  }
  throw new Error('unbalanced body for ' + name);
}

let attachSrc;
let materializeSrc;
let materializeUnderSrc;
try {
  attachSrc = extractFunction('attach');
  materializeSrc = extractFunction('materialize');
  materializeUnderSrc = extractFunction('materializeUnder');
} catch (error) {
  console.error('FAIL: extraction: ' + error.message);
  process.exit(2);
}

// ------------------------------------------------------------------- plants
function replaceExactlyOnce(text, old, next, label) {
  const count = text.split(old).length - 1;
  if (count !== 1) {
    console.error('FAIL: ' + label + ' matched ' + count + 'x (want 1)');
    process.exit(2);
  }
  return text.replace(old, next);
}

if (plantDropIndex) {
  // The exact pre-fix shape: the index reaches materialize and is discarded.
  materializeSrc = replaceExactlyOnce(
    materializeSrc,
    'materializeUnder(parent.id, child, appendsLast ? void 0 : index);',
    'materializeUnder(parent.id, child);',
    'plant-drop-index');
}
if (plantAppendOnly) {
  // Threading kept, emit removed: createWidget appends and nothing moves it.
  materializeUnderSrc = replaceExactlyOnce(
    materializeUnderSrc,
    'call2("insertChild", parentId, child.id, index);',
    'void index;',
    'plant-append-only');
}
if (plantQueueOrder) {
  materializeUnderSrc = replaceExactlyOnce(
    materializeUnderSrc,
    'drained.sort((a, b) => authoredOrder(a) - authoredOrder(b));',
    '',
    'plant-queue-order');
}

// ------------------------------------------------------------------- harness
// A recording bridge with the native factory's ONE capability -- append -- plus
// insertChild, which repositions an existing child the way
// View::move_child_to_index does.
function buildHarness() {
  const native = new Map();          // parentId -> [childId...]
  const calls = [];

  const ensure = (id) => {
    if (!native.has(id)) native.set(id, []);
    return native.get(id);
  };
  const detachNative = (id) => {
    for (const list of native.values()) {
      const at = list.indexOf(id);
      if (at >= 0) list.splice(at, 1);
    }
  };

  const createWidget = (type, id, parentId, _props) => {
    calls.push(['createWidget', id, parentId]);
    detachNative(id);
    ensure(parentId).push(id);       // APPEND. The factory has no other mode.
    ensure(id);
  };

  const call2 = (name, ...rest) => {
    calls.push([name, ...rest]);
    if (name === 'insertChild') {
      const [parentId, childId, index] = rest;
      const list = ensure(parentId);
      const at = list.indexOf(childId);
      if (at < 0) return;            // fails closed, like the native op
      list.splice(at, 1);
      list.splice(Math.max(0, Math.min(index, list.length)), 0, childId);
    } else if (name === 'removeWidget') {
      detachNative(rest[0]);
    } else if (name === 'moveWidget') {
      const [childId, parentId, index] = rest;
      detachNative(childId);
      const list = ensure(parentId);
      list.splice(Math.max(0, Math.min(index, list.length)), 0, childId);
    }
  };

  const g4 = { insertChild: () => {}, removeWidget: () => {} };

  const scope = {
    createWidget,
    call2,
    g4,
    applyAllProps: () => {},
    bindSourceLocation: () => {},
    isSvgPrimitive: () => false,
    parseSvgViewBox: () => undefined,
    svgViewportFor: () => undefined,
  };

  const body = `
    ${attachSrc}
    ${materializeSrc}
    ${materializeUnderSrc}
    return { attach, materialize, materializeUnder };
  `;
  const names = Object.keys(scope);
  const factory = new Function(...names, body);
  const api = factory(...names.map((n) => scope[n]));
  return { ...api, native, calls };
}

let nextId = 0;
const node = (label) => ({
  id: label + '#' + (++nextId),
  label,
  type: 'div',
  props: {},
  childIds: [],
  pendingChildren: [],
  onBridge: false,
  parentId: undefined,
  _dom: undefined,
});

// ------------------------------------------------------------------ scenarios
const failures = [];
const check = (name, actual, expected) => {
  const a = JSON.stringify(actual);
  const e = JSON.stringify(expected);
  if (a === e) {
    console.log('  PASS ' + name + '  ' + a);
  } else {
    failures.push(name);
    console.log('  FAIL ' + name + '\n    expected ' + e + '\n    actual   ' + a);
  }
};
const labelsOf = (h, parent) =>
  (h.native.get(parent.id) || []).map(
    (id) => id.slice(0, id.lastIndexOf('#')));

// 1. The reported defect. A dropdown row list where two rows unmount and come
//    back while their siblings stay -- the shape of a re-opened menu after a
//    preset apply or a snapshot recall re-renders the overlay subtree.
{
  const h = buildHarness();
  const menu = node('MENU');
  menu.onBridge = true;
  h.native.set(menu.id, []);

  const rows = ['SCULPT', 'LEVEL', 'BOOST', 'FLARE', 'GLIDE'].map(node);
  rows.forEach((row, i) => h.attach(menu, row, i));
  check('initial mount is authored order', labelsOf(h, menu),
        ['SCULPT', 'LEVEL', 'BOOST', 'FLARE', 'GLIDE']);

  // LEVEL and FLARE unmount (a re-render drops them), then remount in place.
  for (const victim of [rows[1], rows[3]]) {
    const at = menu.childIds.indexOf(victim.id);
    menu.childIds.splice(at, 1);
    h.calls.push(['removeWidget', victim.id]);
    for (const list of h.native.values()) {
      const k = list.indexOf(victim.id);
      if (k >= 0) list.splice(k, 1);
    }
    victim.onBridge = false;
    victim.parentId = undefined;
  }
  h.attach(menu, rows[1], 1);
  h.attach(menu, rows[3], 3);

  check('remounted rows land in authored order', labelsOf(h, menu),
        ['SCULPT', 'LEVEL', 'BOOST', 'FLARE', 'GLIDE']);
  check('native order tracks childIds', labelsOf(h, menu),
        menu.childIds.map((id) => id.slice(0, id.lastIndexOf('#'))));
}

// 2. A caption/description pair per row -- the structure the screenshots show
//    separating. The body remounts; it must stay directly under its caption.
{
  const h = buildHarness();
  const row = node('ROW');
  row.onBridge = true;
  h.native.set(row.id, []);

  const caption = node('CAPTION');
  const body = node('BODY');
  const trailing = node('CHEVRON');
  h.attach(row, caption, 0);
  h.attach(row, body, 1);
  h.attach(row, trailing, 2);

  const at = row.childIds.indexOf(body.id);
  row.childIds.splice(at, 1);
  for (const list of h.native.values()) {
    const k = list.indexOf(body.id);
    if (k >= 0) list.splice(k, 1);
  }
  body.onBridge = false;
  body.parentId = undefined;
  h.attach(row, body, 1);

  check('a remounted description stays under its caption', labelsOf(h, row),
        ['CAPTION', 'BODY', 'CHEVRON']);
}

// 3. A subtree reordered BEFORE its parent reaches the bridge. Exercises the
//    deferred pendingChildren queue, whose drain order is a separate decision
//    from the indexed emit above.
{
  const h = buildHarness();
  const root = node('ROOT');
  root.onBridge = true;
  h.native.set(root.id, []);

  const panel = node('PANEL');           // deferred: not on the bridge yet
  const a = node('A');
  const b = node('B');
  const c = node('C');
  // Attaches arrive out of authored order, as a reorder before mount does.
  h.attach(panel, b, 0);
  h.attach(panel, c, 1);
  h.attach(panel, a, 0);

  h.attach(root, panel, 0);              // panel materializes, draining its queue
  check('a deferred subtree drains in authored order', labelsOf(h, panel),
        ['A', 'B', 'C']);
}

// ------------------------------------------------------------------- verdict
const failed = failures.length > 0;
const verdict = expectFail ? failed : !failed;
if (failed) {
  console.log('\n' + failures.length + ' check(s) failed: ' + failures.join(', '));
} else {
  console.log('\nall checks passed');
}
if (expectFail) {
  console.log(verdict
    ? 'CONTROL OK: the planted defect was rejected'
    : 'CONTROL BROKEN: the planted defect was NOT rejected');
}
process.exit(verdict ? 0 : 1);
