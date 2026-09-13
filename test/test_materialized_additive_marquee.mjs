#!/usr/bin/env node
// The marquee's RESULT, not the fact that a handler ran.
//
// A gesture test that asserts "the handler fired" is satisfied by a handler
// that fires and computes the wrong set. What a person asked for here is a
// DISCONTIGUOUS selection -- two separate frequency ranges held at once with
// the bands between them untouched -- so that is what this asserts: the set
// itself, band by band, including the gap.
//
// Two things made this cheap, and both are worth writing down:
//
//   * `selection` was ALREADY a `Set`. A discontiguous selection has always
//     been representable; the old move handler simply threw the previous set
//     away and rebuilt from empty on every pointer sample. This was an input
//     change, not a data-model change, which is the difference between an
//     afternoon and a rewrite.
//   * the modifier the handler reads is `e.metaKey || e.ctrlKey`, so Command
//     and Control both already work and the panel's Command notation is
//     correct as it stands.
//
// The gesture is a TOGGLE against the selection frozen at press, not an
// add-only union, so the shipped label "Add/remove selection" describes what
// it does in both directions. Both directions are asserted below; a label
// that over-promises is the defect this file exists downstream of.
//
// Usage:
//   node test_materialized_additive_marquee.mjs <runtime.json>
//        [--plant-replaces | --plant-add-only | --plant-live-toggle]
//        [--expect-fail]
//
// --plant-replaces      restores the pre-change move handler (the second
//                       marquee wipes the first).
// --plant-add-only      makes the additive drag a union, so it can add but
//                       never remove -- the defect a mislabelled row hides.
// --plant-live-toggle   toggles against the LIVE selection instead of the
//                       frozen base, so a band flips again on every sample it
//                       stays inside and the selection strobes mid-drag.
//
// --expect-fail inverts the verdict: green only when this suite REJECTS the
// planted document. The inversion lives here rather than in WILL_FAIL because
// WILL_FAIL accepts any non-zero exit -- a usage error satisfies it and
// proves nothing.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantReplaces = args.includes("--plant-replaces");
const plantAddOnly = args.includes("--plant-add-only");
const plantLiveToggle = args.includes("--plant-live-toggle");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_additive_marquee.mjs "
    + "<runtime.json> [--plant-...] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const plant = (label, from, to, expected = 1) => {
  const found = html.split(from).length - 1;
  if (found !== expected) {
    console.error(`FAIL: plant "${label}" found ${found} sites, expected `
      + `${expected} -- the control cannot prove anything`);
    process.exit(2);
  }
  html = html.split(from).join(to);
  console.log(`planted   ${label}`);
};

if (plantReplaces) {
  plant("the move handler rebuilds from empty again",
    "      const base = p.baseSelection;\n"
    + "      const sel = base ? new Set(base) : /* @__PURE__ */ new Set();",
    "      const base = null;\n"
    + "      const sel = /* @__PURE__ */ new Set();");
}

if (plantAddOnly) {
  plant("the additive drag can add but never remove",
    "        if (base && sel.has(i)) sel.delete(i);\n        else sel.add(i);",
    "        sel.add(i);");
}

if (plantLiveToggle) {
  plant("the base is read live instead of frozen at press",
    "        baseSelection: shift ? new Set(selection) : null",
    "        baseSelection: shift ? selection : null");
  plant("and the move handler mutates it in place",
    "      const sel = base ? new Set(base) : /* @__PURE__ */ new Set();",
    "      const sel = base || /* @__PURE__ */ new Set();");
}

const failures = [];
const fail = (msg) => failures.push(msg);

// ------------------------------------------------------- positive controls
// A suite that cannot find its subject reports every document as clean.

const scriptBlocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const bankBlock = scriptBlocks.find((b) => b.includes("function FilterBank("));

const controls = {
  "script blocks": scriptBlocks.length,
  "filter bank": html.split("function FilterBank(").length - 1,
  "selection is a Set": html.split(
    "const [selection, setSelection] = useState(() => /* @__PURE__ */ new Set())")
    .length - 1,
  "marquee press": html.split('mode: "marquee",').length - 1,
  "marquee move": html.split('if (p.mode === "marquee") {').length - 1,
  "modifier reads meta OR ctrl": html.split(
    "meta = e.metaKey || e.ctrlKey").length - 1,
  "render hook publishes it": html.split(
    "selection: Array.from(selection).sort((a, b) => a - b)").length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(30), count);
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length) {
  console.error("FAIL: the payload has no " + blind.join(", ")
    + " -- this suite is reading the wrong document or the editor was "
    + "restructured, so it cannot render a verdict");
  process.exit(2);
}

// -------------------------------------------------------------- the shim

const N = 32;
let rafQueue = [];
let clock = 1000;
const hooks = [];
let cursor = 0;
const effects = [];
const cleanups = [];
const log = [];

const React = {
  createElement: (type, props, ...children) => ({ type, props, children }),
  memo: (fn) => fn,
  Fragment: "Fragment",
  useState(initial) {
    const i = cursor++;
    if (!hooks[i]) hooks[i] = { value: typeof initial === "function" ? initial() : initial };
    const set = (next) => {
      const resolved = typeof next === "function" ? next(hooks[i].value) : next;
      if (Object.is(resolved, hooks[i].value)) return;
      hooks[i].value = resolved;
    };
    return [hooks[i].value, set];
  },
  useRef(initial) {
    const i = cursor++;
    if (!hooks[i]) hooks[i] = { current: initial };
    return hooks[i];
  },
  useMemo(fn, deps) {
    const i = cursor++;
    const prev = hooks[i];
    const changed = !prev || !prev.deps || !deps || deps.length !== prev.deps.length
      || deps.some((d, j) => d !== prev.deps[j]);
    if (changed) hooks[i] = { deps, value: fn() };
    return hooks[i].value;
  },
  useCallback(fn, deps) {
    const i = cursor++;
    const prev = hooks[i];
    const changed = !prev || !prev.deps || !deps || deps.length !== prev.deps.length
      || deps.some((d, j) => d !== prev.deps[j]);
    if (changed) hooks[i] = { deps, fn };
    return hooks[i].fn;
  },
  useEffect(fn, deps) {
    const i = cursor++;
    const prev = hooks[i];
    const changed = !prev || !prev.deps || !deps || deps.length !== prev.deps.length
      || deps.some((d, j) => d !== prev.deps[j]);
    hooks[i] = { deps };
    if (changed) effects.push({ i, fn });
  },
};

const WIDTH = 1000;
const HEIGHT = 600;
const style = {};
// The handlers read layout off this element and take pointer capture on it.
// A null ref throws out of onPointerDown before any branch runs, which reads
// exactly like "the modifier is not handled".
const wrapElement = {
  clientWidth: WIDTH,
  clientHeight: HEIGHT,
  getBoundingClientRect: () => ({ left: 0, top: 0, width: WIDTH, height: HEIGHT }),
  setPointerCapture() {},
  releasePointerCapture() {},
  dataset: {},
  style,
};

const sandbox = {
  React, Math, Array, Object, Set, Map, String, Number, Boolean, JSON, Date,
  Error, Promise, Float32Array, Uint8Array, isNaN, isFinite, parseFloat, parseInt,
  Infinity, NaN,
  console: { log() {}, warn() {}, error(...a) { log.push("console.error:" + a.join(" ")); } },
  requestAnimationFrame(fn) { rafQueue.push(fn); return rafQueue.length; },
  cancelAnimationFrame(id) { rafQueue[id - 1] = null; },
  setTimeout() { return 0; }, clearTimeout() {},
  setInterval() { return 0; }, clearInterval() {},
  performance: { now: () => clock },
  addEventListener() {}, removeEventListener() {},
  document: {
    getElementById: () => null, querySelector: () => null,
    createElement: () => ({ getContext: () => null, style: {} }),
    addEventListener() {}, removeEventListener() {},
  },
};
sandbox.globalThis = sandbox;
sandbox.window = sandbox;
sandbox.__spectrTestHooks = {};
// The hover readout formats a frequency on every pointer sample. Its absence
// throws out of onPointerMove before the marquee branch, which is a dead
// instrument rather than a failing assertion.
sandbox.window.SpectrFreq = {
  fmt: (hz) => String(Math.round(hz)),
};
sandbox.window.Spectr = { FACTORY_PATTERNS: [], resolveGains: () => [] };
sandbox.window.pulp = {
  postMessage() { return Promise.resolve({ ok: true, payload: { ok: true, revision: 1 } }); },
  on() { return () => {}; },
};

const settings = {
  bandCount: N, metaphor: "bar", bloom: 0, spectrumIntensity: 0,
  muteStyle: "badge", motionMode: "live", showMinimap: true, showRulers: true,
  theme: "dark", unmuteOnDraw: false,
};
const sharedState = { current: null };
const props = {
  settings, sharedState, dspMode: "fft", editMode: "sculpt",
  analyzerMode: "off", visualizationMode: "bars", nativeHydrated: true,
  onStateChange() {}, onStatus() {}, onEditModeChange() {}, onNativeState() {},
};

let surface = null;
const findSurface = (node) => {
  if (!node || typeof node !== "object") return null;
  if (node.props && node.props["data-spectr-filter-surface"]) return node;
  for (const child of node.children || []) {
    const hit = findSurface(child);
    if (hit) return hit;
  }
  return null;
};
const render = () => {
  cursor = 0;
  const element = sandbox.FilterBank(props);
  while (effects.length) {
    const e = effects.shift();
    if (typeof cleanups[e.i] === "function") cleanups[e.i]();
    cleanups[e.i] = e.fn();
  }
  surface = findSurface(element);
  if (surface && surface.props.ref) surface.props.ref.current = wrapElement;
  return element;
};
const frame = (n = 1) => {
  for (let i = 0; i < n; i++) {
    clock += 16.667;
    const due = rafQueue;
    rafQueue = [];
    for (const fn of due) if (fn) { try { fn(clock); } catch (e) { log.push("raf:" + e.message); } }
  }
};

try {
  vm.runInContext(bankBlock, vm.createContext(sandbox), { filename: "bank-block.js" });
} catch (error) {
  console.error(`FAIL: evaluating the bank's script block threw `
    + `${error.constructor.name}: ${error.message}`);
  process.exit(2);
}

if (typeof sandbox.FilterBank !== "function") {
  console.error("FAIL: FilterBank is not reachable after evaluating its own "
    + "script block");
  process.exit(2);
}

render();
render();          // second pass: the ref is attached, so getGeom() resolves
frame(2);

if (!sandbox.__spectrTestHooks.renderState) {
  console.error("FAIL: the bank did not install its renderState test hook");
  process.exit(2);
}

// ------------------------------------------------------------- geometry
// Band centres, derived the way the editor derives them, so the drag lands
// where a person's drag would.

const PAD = { l: 56, r: 56, t: 70, b: 120 };
const inner = { x: PAD.l, y: PAD.t, w: WIDTH - PAD.l - PAD.r, h: HEIGHT - PAD.t - PAD.b };
const bandGap = 2;
const bandW = (inner.w - bandGap * (N - 1)) / N;
const centre = (i) => inner.x + i * (bandW + bandGap) + bandW / 2;
const PLOT_Y = inner.y + inner.h * 0.4;

const selection = () => sandbox.__spectrTestHooks.renderState().selection;

const event = (x, y, mods) => ({
  clientX: x, clientY: y, pointerId: 1, button: 0,
  shiftKey: !!(mods && mods.shift), altKey: false,
  metaKey: !!(mods && mods.meta), ctrlKey: !!(mods && mods.ctrl),
  preventDefault() {}, stopPropagation() {},
});

// One marquee drag, sampled the way a pointer stream samples it.
const marquee = (fromBand, toBand, mods, samples = 6) => {
  const x0 = centre(fromBand) - bandW / 2 - 1;
  const x1 = centre(toBand) + bandW / 2 + 1;
  surface.props.onPointerDown(event(x0, PLOT_Y, mods));
  render();
  for (let s = 1; s <= samples; s++) {
    const x = x0 + (x1 - x0) * (s / samples);
    surface.props.onPointerMove(event(x, PLOT_Y, mods));
    render();
  }
  surface.props.onPointerUp(event(x1, PLOT_Y, mods));
  render();
  frame();
  return selection();
};

const range = (a, b) => {
  const out = [];
  for (let i = a; i <= b; i++) out.push(i);
  return out;
};
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

// ------------------------------------------------------- stimulus control
// Before asserting anything about an additive drag, prove a PLAIN marquee
// selects at all in this harness. Without it, "both regions are selected"
// and "the driver never reached the handler" are the same empty set.

const LOW = [2, 6];
const HIGH = [20, 24];
const first = marquee(LOW[0], LOW[1], { meta: true });
if (!same(first, range(LOW[0], LOW[1]))) {
  console.error(`FAIL: the plain Command marquee selected [${first}], `
    + `expected [${range(LOW[0], LOW[1])}] -- the driver never reached the `
    + "handler, so every verdict below would be vacuous");
  process.exit(2);
}
console.log("stimulus  plain marquee selected [%s]", first.join(","));

// ------------------------------------------------------------- assertions

// A1. The plain marquee still REPLACES. Additive behaviour that leaks into
// the unmodified gesture is a regression, not a feature.
const replaced = marquee(HIGH[0], HIGH[1], { meta: true });
if (!same(replaced, range(HIGH[0], HIGH[1])))
  fail(`a second plain Command marquee left [${replaced}], expected only `
    + `[${range(HIGH[0], HIGH[1])}] -- the unmodified gesture must replace`);

// A2. THE ASK. Marquee one region, then marquee a separate region with the
// additive modifier: both regions held, and the bands BETWEEN them are not.
marquee(LOW[0], LOW[1], { meta: true });
const both = marquee(HIGH[0], HIGH[1], { meta: true, shift: true });
const wanted = range(LOW[0], LOW[1]).concat(range(HIGH[0], HIGH[1]));
if (!same(both, wanted)) {
  fail(`the additive marquee produced [${both}], expected [${wanted}]`);
} else {
  const gap = range(LOW[1] + 1, HIGH[0] - 1).filter((i) => both.includes(i));
  if (gap.length)
    fail(`bands ${gap} between the two regions are selected; a discontiguous `
      + "selection must leave the gap alone");
  console.log("runtime   A2 discontiguous selection [%s], gap %d..%d clear",
    both.join(","), LOW[1] + 1, HIGH[0] - 1);
}

// A3. The other half of the advertised label. Dragging the additive marquee
// back over bands it already holds REMOVES them, and touches nothing else.
const removed = marquee(LOW[0], LOW[1], { meta: true, shift: true });
if (!same(removed, range(HIGH[0], HIGH[1])))
  fail(`re-dragging the additive marquee over the held low region left `
    + `[${removed}], expected only [${range(HIGH[0], HIGH[1])}] -- the row `
    + 'says "Add/remove selection" and this is the remove half');
else
  console.log("runtime   A3 additive re-drag removed the low region, left [%s]",
    removed.join(","));

// A4. The drag is idempotent. Toggling against a live selection instead of
// the base frozen at press flips a band again on every sample it stays
// inside, so a long drag lands on parity rather than on a selection. Same
// gesture, many more samples: same answer.
marquee(LOW[0], LOW[1], { meta: true });
const coarse = marquee(HIGH[0], HIGH[1], { meta: true, shift: true }, 3);
marquee(LOW[0], LOW[1], { meta: true });
const fine = marquee(HIGH[0], HIGH[1], { meta: true, shift: true }, 41);
if (!same(coarse, fine))
  fail(`the same additive drag gave [${coarse}] at 3 samples and [${fine}] at `
    + "41 -- the toggle is reading the live selection, not the frozen base");
else
  console.log("runtime   A4 sample-count invariant across 3 and 41 samples");

// A5. Control-as-the-modifier reaches the same handler, which is what the
// person testing the build actually pressed.
marquee(LOW[0], LOW[1], { ctrl: true });
const viaCtrl = marquee(HIGH[0], HIGH[1], { ctrl: true, shift: true });
if (!same(viaCtrl, wanted))
  fail(`the Control+Shift marquee produced [${viaCtrl}], expected [${wanted}]`);
else console.log("runtime   A5 Control+Shift reaches the same handler");

// ------------------------------------------------------------- verdict

for (const entry of log) if (entry.startsWith("console.error:")) fail(entry);

const clean = failures.length === 0;
if (!clean) for (const f of failures) console.error("FAIL:", f);
if (expectFail) {
  if (clean) {
    console.error("FAIL: the planted document PASSED -- the control does not "
      + "discriminate and this suite proves nothing");
    process.exit(1);
  }
  console.log("PASS (inverted): the planted document was rejected.");
  process.exit(0);
}
if (!clean) process.exit(1);
console.log("PASS: the marquee builds and unbuilds a discontiguous selection.");
