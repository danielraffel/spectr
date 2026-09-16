// A marquee changes MEMBERSHIP, not values, so its cost must scale with the
// bands it crosses -- never with how finely the pointer stream was sampled.
//
// This is the cost half of test_materialized_additive_marquee.mjs, which
// asserts the same gesture's RESULT. Both are needed: a handler can produce
// exactly the right selection and still re-run a 2,226-line component on every
// pointer sample, which is what made the marquee feel sluggish while a
// band-gain drag over the identical path felt fine. Measured on the shipping
// standalone, 180 delivered samples, identical path, audio off:
//
//     marquee   64.0 ms per sample (p50), frame-gap p95 74.4 ms, 189/462 frames late
//     drag       0.66 ms per sample (p50), frame-gap p95 18.6 ms,  11/462 frames late
//
// The drag is cheap because commitDrawnGains defers React entirely. This suite
// holds the marquee to the same standard.
//
// WHAT IS COUNTED. React bails out of a render when a state setter resolves to
// the value already held (Object.is). The shim below models exactly that: a
// setter that resolves to the current value records nothing, and the driver
// re-renders only when a state value actually moved. So "renders" here is the
// number of renders REACT WOULD SCHEDULE, not the number this harness chose to
// perform.
//
// usage:
//   node test_materialized_marquee_render_cost.mjs <runtime.json>
//        [--plant-selection-churn | --plant-marquee-state] [--expect-fail]
//
// --plant-selection-churn  hands setSelection a fresh Set every sample again.
// --plant-marquee-state    puts the rubber-band rectangle back in React state.
// --expect-fail            inverts the verdict: green only when this suite
//                          REJECTS the planted document. Not WILL_FAIL, which
//                          accepts any non-zero exit including the ones that
//                          mean the control never ran.
import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const documentPath = args.find((a) => !a.startsWith("--"));
const plantSelectionChurn = args.includes("--plant-selection-churn");
const plantMarqueeState = args.includes("--plant-marquee-state");
const expectFail = args.includes("--expect-fail");

if (!documentPath) {
  console.error("usage: test_materialized_marquee_render_cost.mjs "
    + "<runtime.json> [--plant-...] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const plant = (label, from, to) => {
  const found = html.split(from).length - 1;
  if (found !== 1) {
    console.error(`FAIL: plant "${label}" found ${found} sites, expected 1 `
      + "-- the control cannot prove anything");
    process.exit(2);
  }
  html = html.split(from).join(to);
  console.log(`planted   ${label}`);
};

if (plantSelectionChurn) {
  plant("a fresh Set on every sample",
    "setSelection((prev) => sameBandSet(prev, sel) ? prev : sel);",
    "setSelection(sel);");
}

if (plantMarqueeState) {
  // Restores the shipped-before form exactly: the rubber-band rectangle back
  // in React state, so every sample schedules a render even when the
  // selection has not moved.
  plant("the rectangle is React state",
    "const marqueeRef = useRef(null);",
    "const [marquee, setMarquee] = useState(null);");
  plant("drawMarquee reads that state",
    "    const marquee = marqueeRef.current;\n    if (!marquee) return;",
    "    if (!marquee) return;");
  plant("press writes state",
    "marqueeRef.current = { x1: x, y1: y, x2: x, y2: y };",
    "setMarquee({ x1: x, y1: y, x2: x, y2: y });");
  plant("move writes state",
    "marqueeRef.current = { x1: p.startX, y1: p.startY, x2: x, y2: y };",
    "setMarquee({ x1: p.startX, y1: p.startY, x2: x, y2: y });");
  plant("release clears state (no gesture)",
    "if (!p || !p.mode) {\n      marqueeRef.current = null;",
    "if (!p || !p.mode) {\n      setMarquee(null);");
  plant("release clears state (marquee)",
    'if (p.mode === "marquee") {\n      marqueeRef.current = null;',
    'if (p.mode === "marquee") {\n      setMarquee(null);');
}

// ---------------------------------------------------------- script block
const blocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const bankBlock = blocks.find((b) => b.includes("function FilterBank("));
// A suite that cannot find its subject reports every document as cheap.
const controls = {
  "script blocks": blocks.length,
  "filter bank": html.split("function FilterBank(").length - 1,
  "marquee press": html.split('mode: "marquee",').length - 1,
  "marquee move": html.split('if (p.mode === "marquee") {').length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(24), count);
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length) {
  console.error("FAIL: the payload has no " + blind.join(", ")
    + " -- this suite is reading the wrong document, so it cannot render a "
    + "verdict");
  process.exit(2);
}
if (!bankBlock) {
  console.error("FAIL: no script block defines FilterBank -- this suite is "
    + "reading the wrong document");
  process.exit(2);
}

// ---------------------------------------------------------------- shim
const N = 32;
let rafQueue = [];
let clock = 1000;
const hooks = [];
let cursor = 0;
const effects = [];
const cleanups = [];
// Every state value that actually moved since the last render. React would
// schedule a render for each; a setter that resolves to the held value is a
// bailout and is deliberately NOT counted.
let stateMutations = 0;
let dirty = false;

const React = {
  createElement: (type, props, ...children) => ({ type, props, children }),
  memo: (fn) => fn,
  Fragment: "Fragment",
  useState(initial) {
    const i = cursor++;
    if (!hooks[i]) hooks[i] = { value: typeof initial === "function" ? initial() : initial };
    const set = (next) => {
      const resolved = typeof next === "function" ? next(hooks[i].value) : next;
      if (Object.is(resolved, hooks[i].value)) return;   // React bails out
      hooks[i].value = resolved;
      stateMutations++;
      dirty = true;
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
const wrapElement = {
  clientWidth: WIDTH,
  clientHeight: HEIGHT,
  getBoundingClientRect: () => ({ left: 0, top: 0, width: WIDTH, height: HEIGHT }),
  setPointerCapture() {}, releasePointerCapture() {},
  dataset: {}, style: {},
};

const sandbox = {
  React, Math, Array, Object, Set, Map, String, Number, Boolean, JSON, Date,
  Error, Promise, Float32Array, Uint8Array, isNaN, isFinite, parseFloat, parseInt,
  Infinity, NaN,
  console: { log() {}, warn() {}, error() {} },
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
sandbox.window.SpectrFreq = { fmt: (hz) => String(Math.round(hz)) };
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
const props = {
  settings, sharedState: { current: null }, dspMode: "fft", editMode: "sculpt",
  analyzerMode: "off", visualizationMode: "bars", nativeHydrated: true,
  onStateChange() {}, onStatus() {}, onEditModeChange() {}, onNativeState() {},
};

let surface = null;
let renders = 0;
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
  renders++;
  cursor = 0;
  dirty = false;
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
// React re-renders only when a commit actually changed state. Rendering
// unconditionally here would make every document look identical and the gate
// could never fail.
const renderIfDirty = () => { if (dirty) render(); };

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
// The rAF loop paints to a canvas this harness has no 2D context for. It is
// drained (not skipped) so the component reaches steady state, and its paint
// throw is swallowed: what is under test is how often React is asked to
// render, not what the canvas draws.
for (const fn of rafQueue.splice(0)) if (fn) { try { fn(clock); } catch {} }

// -------------------------------------------------------------- geometry
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
  metaKey: !!(mods && mods.meta), ctrlKey: false,
  preventDefault() {}, stopPropagation() {},
});

// One marquee drag. Returns what it selected and what it cost.
const marquee = (fromBand, toBand, samples) => {
  const x0 = centre(fromBand) - bandW / 2 - 1;
  const x1 = centre(toBand) + bandW / 2 + 1;
  stateMutations = 0;
  renders = 0;
  surface.props.onPointerDown(event(x0, PLOT_Y, { meta: true }));
  renderIfDirty();
  for (let s = 1; s <= samples; s++) {
    const x = x0 + (x1 - x0) * (s / samples);
    surface.props.onPointerMove(event(x, PLOT_Y, { meta: true }));
    renderIfDirty();
  }
  surface.props.onPointerUp(event(x1, PLOT_Y, { meta: true }));
  renderIfDirty();
  return { selected: selection(), renders, mutations: stateMutations };
};

const failures = [];
const fail = (m) => { failures.push(m); console.error("FAIL: " + m); };

// ------------------------------------------------------ stimulus control
// A cost of zero is what a dead driver and a perfect handler both produce.
// Prove the gesture selects before reading anything into its cost.
const LOW = 4, HIGH = 15;
const BANDS = HIGH - LOW + 1;
const coarse = marquee(LOW, HIGH, 6);
if (coarse.selected.length !== BANDS) {
  console.error(`FAIL: the Command marquee selected [${coarse.selected}], `
    + `expected the ${BANDS} bands ${LOW}..${HIGH} -- the driver never reached `
    + "the marquee branch, so its cost is not evidence about anything");
  process.exit(2);
}
console.log("stimulus  marquee over %d bands selected [%s]",
  BANDS, coarse.selected.join(","));
console.log("cost      %d samples -> %d renders, %d state mutations",
  6, coarse.renders, coarse.mutations);

// C1. THE ASK. Sampling the SAME drag ten times more finely must not cost ten
// times more. The pointer crossed the same 12 band boundaries either way.
const fine = marquee(LOW, HIGH, 120);
console.log("cost      %d samples -> %d renders, %d state mutations",
  120, fine.renders, fine.mutations);
if (!(fine.selected.length === BANDS)) {
  fail(`the fine-sampled marquee selected [${fine.selected}], expected `
    + `${BANDS} bands -- the two drags are not comparable`);
}
// The bound is the work the gesture genuinely implies: one render per band
// boundary crossed, plus a small constant for press and release. A handler
// that re-renders per sample lands at ~120 and fails by a wide margin, so the
// slack costs the gate nothing.
const BOUND = BANDS + 4;
if (fine.renders > BOUND) {
  fail(`a ${120}-sample marquee over ${BANDS} bands scheduled ${fine.renders} `
    + `renders, expected at most ${BOUND}. The gesture changes MEMBERSHIP, so `
    + "its cost must follow the bands it crosses, not the pointer's sample "
    + "rate -- this is the shape that made the marquee sluggish while a "
    + "band-gain drag over the same path was not.");
}

// C2. Doubling the sample rate again must not move the cost. Both of these
// resolve every boundary, so any difference between them IS sample-rate
// scaling. (The 6-sample drag above is deliberately NOT the comparison: with
// fewer samples than boundaries it cannot cross them all, so it under-counts
// for a reason that has nothing to do with the handler.)
const finer = marquee(LOW, HIGH, 240);
console.log("cost      %d samples -> %d renders, %d state mutations",
  240, finer.renders, finer.mutations);
if (finer.renders > fine.renders + 2) {
  fail(`the same drag cost ${fine.renders} renders at 120 samples and `
    + `${finer.renders} at 240 -- the cost is tracking the sample rate`);
}

// C3. The rubber-band rectangle alone must not schedule a render. A sample
// that moves the pointer INSIDE one band changes the rectangle and nothing
// else, and the rectangle is drawn by the permanent rAF loop either way.
//
// Asserted as INVARIANCE across two sample rates rather than against a fixed
// bound: the editor legitimately settles some other state (the crosshair
// cursor) on first entering the plot, and a magic constant would either hide
// per-sample churn or fail on that one-time settle.
const subBand = (samples) => {
  const xLo = centre(LOW) - bandW / 4;
  const xHi = centre(LOW) + bandW / 4;
  renders = 0;
  surface.props.onPointerDown(event(xLo, PLOT_Y, { meta: true }));
  renderIfDirty();
  // The first move legitimately changes membership (empty -> this band).
  surface.props.onPointerMove(event(xLo, PLOT_Y, { meta: true }));
  renderIfDirty();
  const before = renders;
  for (let s = 1; s <= samples; s++) {
    surface.props.onPointerMove(
      event(xLo + (xHi - xLo) * (s / samples), PLOT_Y, { meta: true }));
    renderIfDirty();
  }
  const cost = renders - before;
  surface.props.onPointerUp(event(xHi, PLOT_Y, { meta: true }));
  renderIfDirty();
  return cost;
};
const few = subBand(20);
const many = subBand(400);
console.log("cost      sub-band motion, membership constant: "
  + "%d samples -> %d renders, %d samples -> %d renders", 20, few, 400, many);
if (many !== few) {
  fail(`20 pointer samples inside a single band scheduled ${few} renders and `
    + `400 scheduled ${many}. Only the rubber-band rectangle moved, and it is `
    + "painted from the component's own requestAnimationFrame loop, which runs "
    + "every frame regardless -- so the cost must not follow the sample rate.");
}
if (many > 1) {
  fail(`sub-band motion costs ${many} renders per drag; at most one (the `
    + "editor settling its crosshair cursor on entering the plot) is expected");
}

// ------------------------------------------------------------- verdict
if (expectFail) {
  if (failures.length === 0) {
    console.error("FAIL: the planted document PASSED -- the control does not "
      + "discriminate and proves nothing");
    process.exit(1);
  }
  console.log("PASS (inverted): the planted document was rejected.");
  process.exit(0);
}
if (failures.length > 0) {
  console.error(`\n${failures.length} failure(s).`);
  process.exit(1);
}
console.log("PASS: a marquee's cost follows the bands it crosses, not the "
  + "pointer's sample rate.");
