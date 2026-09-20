#!/usr/bin/env node
// THE EDITOR STOPS PAINTING, AND STOPS REPUBLISHING, WHEN NOTHING CHANGED.
//
// Three costs this suite holds, all of them paid while the user is doing
// nothing in particular, and all three invisible to every other suite here
// because every other suite measures a RESULT and these are about frames on
// which the result did not move.
//
//   1. THE DRAW LOOP PARKS. `draw` re-armed unconditionally and `renderAll()`
//      repainted the whole canvas on every one of those frames -- 826 canvas
//      bridge calls and 2.82ms of JS per frame on a genuinely idle editor, at
//      60Hz, for as long as it is open. In a DAW that is a continuous CPU
//      floor next to the audio thread.
//   2. NOTHING GOES STALE. A parked loop that fails to wake is a worse bug
//      than a busy one, so most of this suite is the WAKE side: every path
//      that can change the canvas without moving React state gets its own
//      row and its own plant.
//   3. THE STATUS BANNER IS NOT A REACT EVENT. `updateLiveHoverStatus` ran on
//      every frame and published each changed reading back to the parent
//      through `onStatus`, throttled to 700ms. `onStatus` is the PARENT'S
//      state and this document is a captured import, so each one re-applies
//      the whole captured document: 42 of them on a session arm with the
//      pointer resting on a band, against zero on an idle arm. It cannot be
//      exempted as paint-only either -- the pill's width is a function of the
//      string it paints, so the commit is geometric by construction.
//
// WHAT IS COUNTED. The 2D contexts handed to the painters are proxies that
// count every method call and every property write -- which is what a canvas
// bridge call IS on this runtime. "Zero calls" therefore means the painters
// did not run, not that this harness declined to look.
//
// WHY THE REACT SHIM DOES NOT RENDER BY ITSELF. Several checks below drive a
// path that mutates paint refs and ALSO marks React state dirty. Rendering on
// dirty would wake the loop through the render effect and every one of those
// rows would pass for the wrong reason. So the driver renders only when a
// check explicitly asks it to, and each wake row names the single mechanism
// it is allowed to be measuring.
//
// Usage:
//   node test_materialized_idle_draw_loop.mjs <materialized-document.runtime.json>
//        [--plant-...] [--expect-fail]
//
// --plant-unconditional-raf     re-arms the loop every frame, as it shipped.
// --plant-per-frame-alloc       restores drawSpectrum's per-frame allocation.
// --plant-status-republish      restores the 700ms onStatus publication.
// --plant-no-render-wake        removes the every-render wake.
// --plant-no-input-wake         removes the window capture listeners.
// --plant-no-commit-wake        removes commitMany's wake (the deferReact
//                               path commits painted gains with no render).
// --plant-no-modulation-wake    removes applyModulationFrame's wake.
// --plant-no-automation-wake    removes applyHostAutomationState's wake.
// --plant-no-resize-wake        removes the resize handler's wake.
// --plant-no-analyzer-wake      makes an incoming analyzer frame stop
//                               re-arming a parked loop (an editor that
//                               parks in silence then receives audio).
// --plant-no-analyzer-guard     lets the loop park against an analyzer whose
//                               frames it cannot identify.
// --plant-no-settle-check       lets the loop park while the peak hold, the
//                               running average and the per-band energy glow
//                               are still decaying.
// --plant-no-analyzer-frame-check  lets it park while new analyzer frames
//                               are still arriving.
// --plant-no-smoothing-check    lets it park while a gain is still smoothing.
// --expect-fail inverts the verdict, so a control is green only when this
// suite REJECTS that document. The inversion lives here rather than in
// WILL_FAIL because WILL_FAIL accepts any non-zero exit -- a usage error or
// an unreadable file satisfied it and proved nothing.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const documentPath = args.find((a) => !a.startsWith("--"));
const expectFail = args.includes("--expect-fail");
const has = (n) => args.includes(n);

if (!documentPath) {
  console.error("usage: test_materialized_idle_draw_loop.mjs <runtime.json> "
    + "[--plant-...] [--expect-fail]");
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

if (has("--plant-unconditional-raf")) {
  plant("a loop that re-arms every frame",
    "      rafRef.current = idleFramesRef.current < IDLE_TAIL_FRAMES\n"
    + "        ? requestAnimationFrame(draw)\n"
    + "        : 0;\n",
    "      rafRef.current = requestAnimationFrame(draw);\n");
}
if (has("--plant-per-frame-alloc")) {
  plant("a fresh Float32Array every frame",
    "    const arr = spectrumArrRef.current;\n",
    "    const arr = new Float32Array(steps + 1);\n");
}
if (has("--plant-status-republish")) {
  plant("the throttle clock",
    '  const liveStatusLabelRef = useRef("");\n',
    '  const liveStatusLabelRef = useRef("");\n'
    + "  const statusRefreshAtRef = useRef(0);\n");
  plant("the 700ms React republication",
    "    // NO React publication here, deliberately.",
    "    const now = performance.now();\n"
    + "    if (onStatus && now - statusRefreshAtRef.current >= 700) {\n"
    + "      statusRefreshAtRef.current = now;\n"
    + "      onStatus(label);\n"
    + "    }\n"
    + "    // NO React publication here, deliberately.");
}
if (has("--plant-no-render-wake")) {
  plant("a render that does not re-arm the loop",
    "  useEffect(() => {\n    wakeDraw();\n  });\n",
    "  useEffect(() => {\n  });\n");
}
if (has("--plant-no-input-wake")) {
  plant("input that does not re-arm the loop",
    "    for (const name of events) window.addEventListener(name, wake, true);\n",
    "");
}
if (has("--plant-no-commit-wake")) {
  plant("a deferred commit that does not re-arm the loop",
    "    // `deferReact` exists precisely to skip the setState, so this commit\n"
    + "    // can change every painted gain with no render behind it.\n"
    + "    wakeDraw();\n",
    "");
}
if (has("--plant-no-modulation-wake")) {
  plant("a modulation overlay that does not re-arm the loop",
    "        // The one publication path that calls no state setter at all, by\n"
    + "        // design -- so it is the one path with no render behind it, and\n"
    + "        // it has to arm the frame that draws what it just wrote.\n"
    + "        wakeDraw();\n",
    "");
}
if (has("--plant-no-automation-wake")) {
  plant("host automation that does not re-arm the loop",
    "        // Writes the paint refs and the viewport with no state setter,\n"
    + "        // deliberately (see the comment above about the one-shot flag).\n"
    + "        wakeDraw();\n",
    "");
}
if (has("--plant-no-resize-wake")) {
  plant("a resize that does not re-arm the loop",
    "      if (renderAllRef.current) renderAllRef.current();\n      wakeDraw();\n",
    "      if (renderAllRef.current) renderAllRef.current();\n");
}
if (has("--plant-no-analyzer-wake")) {
  plant("an analyzer frame that does not re-arm a parked loop",
    '    const off = window.pulp.on("analyzer_frame", () => wakeDraw());\n',
    "    const off = window.pulp.on(\"analyzer_frame\", () => {});\n");
}
if (has("--plant-no-analyzer-guard")) {
  plant("a loop that parks against an analyzer it cannot read",
    "      if (frame === void 0 || !analyzer || analyzer.native !== true) {\n",
    "      if (false) {\n");
}
if (has("--plant-no-settle-check")) {
  // ONE mechanism, planted as one: "is analyzer-derived paint still moving?"
  // The spectrum's curves and the per-band energy glow decay together after
  // the same last frame, so a plant that removed only one would leave the
  // other holding the loop open and the control would prove nothing.
  plant("a loop blind to the spectrum still decaying",
    "      if (!spectrumSettledRef.current) busy = true;\n", "");
  plant("a loop blind to the band glow still decaying",
    "      const bandEnergy = window.__spectrBandEnergy;\n"
    + "      if (bandEnergy) {\n"
    + "        let seen = bandEnergyRef.current;\n"
    + "        if (!seen || seen.length !== bandEnergy.length)\n"
    + "          seen = bandEnergyRef.current = new Float32Array(bandEnergy.length);\n"
    + "        for (let i = 0; i < bandEnergy.length; i++) {\n"
    + "          if (Math.abs(bandEnergy[i] - seen[i]) > 5e-4) busy = true;\n"
    + "          seen[i] = bandEnergy[i];\n"
    + "        }\n"
    + "      }\n", "");
}
if (has("--plant-no-analyzer-frame-check")) {
  plant("a loop blind to new analyzer frames",
    "      } else if (frame !== analyzerFrameRef.current) {\n",
    "      } else if (false) {\n");
}
if (has("--plant-no-smoothing-check")) {
  plant("a loop blind to a gain still smoothing",
    "            if (Math.abs(rg[i] - target) > 2e-4) busy = true;\n", "");
}

// ------------------------------------------------------------ script block
const blocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const bankBlock = blocks.find((b) => b.includes("function FilterBank("));

// STRUCTURAL CONTROLS. Facts about the document that no plant above touches.
// A suite that cannot find its subject reports every document as idle.
const controls = {
  "script blocks": blocks.length,
  "filter bank": html.split("function FilterBank(").length - 1,
  "draw loop": html.split("    const draw = (now) => {").length - 1,
  "spectrum painter": html.split("  function drawSpectrum(ctx, g) {").length - 1,
  "band painter": html.split("  function drawBands(ctx, g) {").length - 1,
  "status banner": html.split("  const updateLiveHoverStatus = () => {").length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(20), count);
const blind = Object.entries(controls)
  .filter(([k, v]) => (k === "script blocks" ? v === 0 : v !== 1)).map(([k]) => k);
if (blind.length || !bankBlock) {
  console.error("FAIL: " + (blind.join(", ") || "the bank's script block")
    + " is not present exactly once -- this suite is reading the wrong "
    + "document, so it cannot render a verdict");
  process.exit(2);
}

// ------------------------------------------------------------------- shim
const N = 32;
const WIDTH = 1000;
const HEIGHT = 600;
const FRAME_MS = 1000 / 60;

let clock = 1000;
let rafSeq = 0;
const rafPending = new Map();
let canvasCalls = 0;
let f32Allocs = 0;
let statusCalls = 0;

const hooks = [];
let cursor = 0;
const effects = [];
const cleanups = [];

// Every method call and every property write on a 2D context is one canvas
// bridge call on this runtime, so both are counted. Property READS are not.
const gradient = () => ({ addColorStop() { canvasCalls++; } });
const context2d = () => {
  const own = {
    canvas: { width: WIDTH, height: HEIGHT },
    measureText: (s) => { canvasCalls++; return { width: String(s || "").length * 6 }; },
    createLinearGradient: () => { canvasCalls++; return gradient(); },
    createRadialGradient: () => { canvasCalls++; return gradient(); },
    createPattern: () => { canvasCalls++; return null; },
  };
  return new Proxy(own, {
    get(o, p) { return p in o ? o[p] : () => { canvasCalls++; }; },
    set(o, p, v) { canvasCalls++; o[p] = v; return true; },
  });
};
const canvasElement = () => {
  const ctx = context2d();
  return { width: WIDTH, height: HEIGHT, style: {}, getContext: () => ctx };
};
const wrapElement = {
  clientWidth: WIDTH, clientHeight: HEIGHT,
  getBoundingClientRect: () => ({ left: 0, top: 0, width: WIDTH, height: HEIGHT }),
  setPointerCapture() {}, releasePointerCapture() {}, focus() {},
  addEventListener() {}, removeEventListener() {}, dataset: {}, style: {},
};

const React = {
  createElement: (type, props, ...children) => ({ type, props, children }),
  memo: (f) => f, Fragment: "Fragment",
  useState(init) {
    const i = cursor++;
    if (!hooks[i]) hooks[i] = { value: typeof init === "function" ? init() : init };
    return [hooks[i].value, (next) => {
      const resolved = typeof next === "function" ? next(hooks[i].value) : next;
      if (Object.is(resolved, hooks[i].value)) return;   // React bails out
      hooks[i].value = resolved;
    }];
  },
  useRef(init) {
    const i = cursor++;
    if (!hooks[i]) hooks[i] = { current: init };
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

// The analyzer the editor actually ships against: frame-driven, never
// time-driven. `acceptAnalyzerFrame` installs a FRESH object per accepted
// frame, which is the identity the draw loop compares.
let analyzerFrame = { epoch: 1, sequence_number: 0 };
let analyzerLevel = 0;
const analyzer = {
  native: true,
  sample: () => analyzerLevel,
  project: (a, zeroY, halfH) => zeroY - a * halfH * 0.95,
  scale: () => ({ floor: -120, ceiling: 24 }),
  normalizeDb: () => 0,
  debugSnapshot: () => analyzerFrame,
};
// Models `emit('analyzer_frame', ...)`: acceptAnalyzerFrame installs a fresh
// frame object, THEN the message reaches every subscriber.
const nativeListeners = new Map();
const publishAnalyzerFrame = (level) => {
  analyzerLevel = level;
  analyzerFrame = { epoch: 1, sequence_number: analyzerFrame.sequence_number + 1 };
  for (const fn of [...(nativeListeners.get("analyzer_frame") || [])])
    fn({ type: "analyzer_frame", payload: analyzerFrame, id: "" });
};

const windowListeners = new Map();
const banner = { style: {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 200, height: 24 }) };
const bannerText = { textContent: "" };

const sandbox = {
  React, Math, Array, Object, Set, Map, String, Number, Boolean, JSON, Date,
  Error, Promise, Symbol, Uint8Array, isNaN, isFinite, parseFloat, parseInt,
  Infinity, NaN,
  // Counting constructor: the per-frame allocation check reads this.
  Float32Array: new Proxy(Float32Array, {
    construct(T, a) { f32Allocs++; return new T(...a); },
  }),
  console: { log() {}, warn() {}, error() {} },
  requestAnimationFrame(fn) { const id = ++rafSeq; rafPending.set(id, fn); return id; },
  cancelAnimationFrame(id) { rafPending.delete(id); },
  setTimeout() { return 0; }, clearTimeout() {},
  setInterval() { return 0; }, clearInterval() {},
  performance: { now: () => clock },
  addEventListener(name, fn) {
    if (!windowListeners.has(name)) windowListeners.set(name, new Set());
    windowListeners.get(name).add(fn);
  },
  removeEventListener(name, fn) {
    if (windowListeners.has(name)) windowListeners.get(name).delete(fn);
  },
  spectrPlaceStatusBanner() {},
  spectrStatusBannerWidth: (s) => 40 + String(s || "").length * 6,
  document: {
    getElementById: () => null,
    querySelector: (sel) => (sel === "[data-spectr-status-banner]" ? banner
      : sel === "[data-spectr-status-text]" ? bannerText : null),
    createElement: () => ({ getContext: () => null, style: {} }),
    addEventListener() {}, removeEventListener() {}, body: { style: {} },
  },
};
sandbox.globalThis = sandbox;
sandbox.window = sandbox;
sandbox.__spectrTestHooks = {};
sandbox.SpectrFreq = { fmt: (hz) => String(Math.round(hz)) };
sandbox.SpectrAnalyzer = analyzer;
sandbox.Spectr = { FACTORY_PATTERNS: [], resolveGains: () => [] };
sandbox.pulp = {
  postMessage() { return Promise.resolve({ ok: true, payload: { ok: true, revision: 1 } }); },
  on(type, callback) {
    if (typeof callback !== "function") return () => {};
    if (!nativeListeners.has(type)) nativeListeners.set(type, new Set());
    nativeListeners.get(type).add(callback);
    return () => nativeListeners.get(type).delete(callback);
  },
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

// ----------------------------------------------------------------- driver
const settings = {
  bandCount: N, metaphor: "bar", bloom: 0.5, spectrumIntensity: 0.8,
  muteStyle: "badge", motionMode: "live", showMinimap: true, showRulers: true,
  theme: "dark", unmuteOnDraw: true,
};
const sharedState = { current: null };
const props = {
  settings, sharedState, dspMode: "fft", editMode: "sculpt",
  analyzerMode: "both", visualizationMode: "both", nativeHydrated: true,
  onStateChange() {}, onStatus() { statusCalls++; },
  onEditModeChange() {}, onNativeState() {},
};

let surface = null;
const attach = (node) => {
  if (!node || typeof node !== "object") return;
  const p = node.props;
  if (p) {
    if (p["data-spectr-filter-surface"]) surface = node;
    if (p.ref && typeof p.ref === "object" && "current" in p.ref) {
      p.ref.current = node.type === "canvas" ? canvasElement()
        : p["data-spectr-filter-surface"] ? wrapElement
        : { style: {}, dataset: {}, addEventListener() {}, removeEventListener() {} };
    }
  }
  for (const child of node.children || []) {
    if (Array.isArray(child)) child.forEach(attach);
    else attach(child);
  }
};
// Renders ONLY when a check asks. Several rows below drive a path that also
// marks state dirty; rendering on dirty would wake the loop through the
// render effect and those rows would pass for the wrong reason.
const render = () => {
  cursor = 0;
  const element = sandbox.FilterBank(props);
  while (effects.length) {
    const e = effects.shift();
    if (typeof cleanups[e.i] === "function") cleanups[e.i]();
    cleanups[e.i] = e.fn();
  }
  attach(element);
  return element;
};

const step = () => {
  clock += FRAME_MS;
  const due = [...rafPending.values()];
  rafPending.clear();
  for (const fn of due) fn(clock);
};
const armed = () => rafPending.size > 0;
// Frames until the loop parks, or null if it never does.
const settle = (cap = 600) => {
  let frames = 0;
  while (armed() && frames < cap) { step(); frames++; }
  return armed() ? null : frames;
};
const dispatchWindow = (name, event) => {
  for (const fn of [...(windowListeners.get(name) || [])]) fn(event || {});
};
const pointerEvent = (x, y, mods) => ({
  clientX: x, clientY: y, pointerId: 1, button: 0,
  shiftKey: !!(mods && mods.shift), altKey: !!(mods && mods.alt),
  metaKey: !!(mods && mods.meta), ctrlKey: false,
  preventDefault() {}, stopPropagation() {},
});

const PAD = { l: 56, r: 56, t: 70, b: 120 };
const inner = { x: PAD.l, y: PAD.t, w: WIDTH - PAD.l - PAD.r, h: HEIGHT - PAD.t - PAD.b };
const bandGap = 2;
const bandW = (inner.w - bandGap * (N - 1)) / N;
const centre = (i) => inner.x + i * (bandW + bandGap) + bandW / 2;
const PLOT_Y = inner.y + inner.h * 0.4;

render();
render();   // second pass: the refs are attached, so getGeom() resolves
render();

// STRUCTURAL CONTROLS, part two: the harness itself.
if (!surface) {
  console.error("FAIL: no [data-spectr-filter-surface] in the rendered tree");
  process.exit(2);
}
if (!sharedState.current) {
  console.error("FAIL: the bank never published its handle, so the "
    + "publication paths below cannot be driven");
  process.exit(2);
}
const failures = [];
const fail = (m) => { failures.push(m); console.error("FAIL: " + m); };

canvasCalls = 0;
step();
const paintCost = canvasCalls;
console.log("control   %s %s", "paint per frame".padEnd(20), paintCost);
if (paintCost === 0) {
  console.error("FAIL: a driven frame painted nothing -- this harness cannot "
    + "tell a parked loop from a dead one, so every zero below is meaningless");
  process.exit(2);
}

// ------------------------------------------- 0. two branches, asserted static
// THE IN-LOOP IDENTITY CHECK IS ASSERTED STATICALLY, AND THIS SUITE SAYS SO
// RATHER THAN PRETENDING OTHERWISE. With the `analyzer_frame` subscription in
// place every accepted frame already calls wakeDraw(), which zeroes the idle
// tail -- so no stimulus this harness can build distinguishes a loop that also
// compares frame identity from one that does not. The check is belt and braces
// for a host where `window.pulp.on` is unavailable (the screenshot realm
// installs no dispatcher), and the honest measurement of "it is present" is a
// static one. Its control (--plant-no-analyzer-frame-check) fails HERE.
if (!bankBlock.includes("} else if (frame !== analyzerFrameRef.current) {")) {
  fail("the draw loop does not compare the analyzer's frame identity. With no "
    + "`analyzer_frame` subscription -- a host that installs no dispatcher -- "
    + "this is the only thing that can tell the loop a live spectrum moved");
}

// ------------------------------------------------------------- 1. it parks
// The whole point. An idle editor -- nothing hovered, nothing playing,
// nothing animating -- must stop asking for frames.
const IDLE_CAP = 200;
const settledIn = settle(IDLE_CAP);
console.log("idle      parked after %s frame(s)",
  settledIn === null ? `>${IDLE_CAP}` : settledIn);
if (settledIn === null) {
  fail(`the draw loop was still asking for frames after ${IDLE_CAP} idle `
    + `frames at ${paintCost} canvas calls each. On a plug-in editor that is `
    + "a permanent CPU floor next to the audio thread, paid for a canvas that "
    + "is painting the same pixels every frame");
} else {
  canvasCalls = 0;
  for (let i = 0; i < 120; i++) step();
  console.log("idle      %d further frames cost %d canvas calls", 120, canvasCalls);
  if (canvasCalls !== 0) {
    fail(`120 frames after parking still cost ${canvasCalls} canvas calls`);
  }
}

// ------------------------------------------- 2. it wakes for every source
// A parked loop that fails to wake is a worse bug than a busy one, so each
// source gets its own row naming the one mechanism it is measuring.
const wakeCheck = (label, trigger, why) => {
  if (settle(IDLE_CAP) === null) {
    fail(`${label}: the loop never parked, so this row cannot tell a wake `
      + "from a loop that simply never stopped");
    return;
  }
  trigger();
  const rearmed = armed();
  canvasCalls = 0;
  step();
  const painted = canvasCalls;
  console.log("wake      %s re-armed=%s painted=%d",
    label.padEnd(26), rearmed, painted);
  if (!rearmed || painted === 0) fail(`${label}: ${why}`);
};

wakeCheck("a React render", () => { settings.bloom = settings.bloom === 0.5 ? 0.9 : 0.5; render(); },
  "a state change did not re-arm the parked loop. renderAll is a useCallback "
  + "over sixteen pieces of state and a render is the only way any of them "
  + "can move, so the canvas would hold the last frame before the change");

wakeCheck("pointer input", () => dispatchWindow("pointermove", pointerEvent(centre(8), PLOT_Y)),
  "a pointer event did not re-arm the parked loop. The interaction handlers "
  + "mutate pointerRef, marqueeRef, hoverRef, edgeGlowRef and the paint gains "
  + "directly and schedule no render, so nothing else would notice");

wakeCheck("a key press", () => dispatchWindow("keydown", { key: "a" }),
  "a key press did not re-arm the parked loop; the global key handler edits "
  + "selection and gains through the same ref-only paths");

wakeCheck("a window resize", () => dispatchWindow("resize", {}),
  "a resize did not re-arm the parked loop. The resize handler reallocates "
  + "the backing store, which clears it -- so a loop that does not wake "
  + "leaves a blank canvas until something else happens");

wakeCheck("a deferred gain commit", () => {
  // Straight to the surface handler, NOT through the window listeners, and
  // with no render afterwards: the only wake available here is commitMany's.
  surface.props.onPointerDown(pointerEvent(centre(6), PLOT_Y));
  surface.props.onPointerMove(pointerEvent(centre(6), PLOT_Y - 40));
  surface.props.onPointerUp(pointerEvent(centre(6), PLOT_Y - 40));
}, "a commit that changed the painted gains did not re-arm the parked loop. "
  + "commitMany's deferReact path exists precisely to skip the setState, so "
  + "there is no render behind it to wake anything else");

wakeCheck("a modulation frame", () => {
  sharedState.current.applyModulationFrame({
    active: true, n: N,
    gains: Array.from({ length: N }, (_, i) => Math.sin(i) * 0.4),
    muted: Array(N).fill(false),
  });
}, "the modulation overlay did not re-arm the parked loop. It writes the "
  + "paint refs and calls no state setter at all, by design, so it is the one "
  + "publication path with no render behind it");

// Release the overlay before the next row: while it is active the loop is
// CORRECTLY never idle, which would leave every row below unable to start
// from a parked loop.
sharedState.current.applyModulationFrame({ active: false, n: N, gains: [], muted: [] });

wakeCheck("host automation", () => {
  sharedState.current.applyHostAutomationState({
    revision: 9, n: N,
    gainDb: Array.from({ length: N }, (_, i) => (i % 5) - 2),
    gains: Array.from({ length: N }, (_, i) => ((i % 5) - 2) / 24),
    muted: Array(N).fill(false),
    minHz: 20, maxHz: 20000, canUndo: true, canRedo: false, macros: null,
  });
}, "host automation did not re-arm the parked loop. It writes the paint refs "
  + "and the viewport directly and deliberately calls no state setter");

// --------------------------------------- 3. it stays awake while it should
// Parking one frame too early is the regression this change risks, so each
// thing that is still moving gets a row that reads FRAMES, not a boolean.
const stayAwake = (label, arm, expected, why) => {
  if (settle(600) === null) {
    fail(`${label}: the loop never parked beforehand, so this row cannot tell `
      + "a loop that stayed awake from one that never stopped");
    return;
  }
  arm();
  const frames = settle(600);
  console.log("awake     %s painted %s frame(s), needs > %d",
    label.padEnd(26), frames === null ? ">600" : frames, expected);
  if (frames !== null && frames <= expected) fail(`${label}: ${why} `
    + `(it parked after ${frames} frames)`);
};

// New analyzer frames keep arriving: the spectrum, the per-band energy glow,
// the mask and the minimap all read them, so the loop must not park at all.
{
  if (settle(600) === null) {
    fail("the analyzer row could not start from a parked loop");
  } else {
    let stillArmed = true;
    for (let i = 0; i < 90; i++) {
      publishAnalyzerFrame(0.3 + 0.5 * Math.abs(Math.sin(i / 7)));
      if (!armed()) { stillArmed = false; break; }
      step();
    }
    console.log("awake     %s %s", "new analyzer frames".padEnd(26),
      stillArmed ? "never parked over 90 frames" : "PARKED");
    if (!stillArmed) {
      fail("the loop parked while new analyzer frames were still arriving. "
        + "acceptAnalyzerFrame installs a fresh object per accepted frame and "
        + "four painters read it, so a parked loop freezes a live spectrum");
    }
  }
}

// The analyzer goes quiet AFTER activity. The peak hold decays at 0.88 and
// the per-band energy glow at 0.9 per frame, so both keep moving for about a
// second with no further frames -- far longer than the settled-frame tail.
stayAwake("an analyzer decay tail", () => {
  for (let i = 0; i < 6; i++) { publishAnalyzerFrame(1); step(); }
  analyzerLevel = 0;      // quiet: deliberately NO further frame published,
                          // so only the decay itself can hold the loop open
}, 20, "the loop parked while the peak hold and the per-band energy glow were "
  + "still decaying, freezing them part-way down");

// A gain still smoothing toward its target. `smooth` closes ~31% of the gap
// per frame at the live motion rate, so a full-scale move needs >20 frames.
stayAwake("a gain still smoothing", () => {
  sharedState.current.setGains(Array.from({ length: N }, (_, i) => (i === 12 ? 1 : 0)));
  render();                     // the render is the wake; the DURATION is the check
}, 20, "the loop parked while a band was still travelling to its new level, "
  + "so the move would finish somewhere off-screen and snap on the next event");

// ------------------------------------ 4. it refuses to park when it cannot see
// A synthetic analyzer samples TIME and animates on its own; this loop has no
// way to know that, so an analyzer it does not recognise must keep it awake.
{
  settle(600);
  const real = sandbox.SpectrAnalyzer;
  sandbox.SpectrAnalyzer = { sample: () => 0, project: (a, z) => z,
    scale: () => ({ floor: -120, ceiling: 24 }), normalizeDb: () => 0 };
  render();
  const frames = settle(120);
  sandbox.SpectrAnalyzer = real;
  console.log("awake     %s %s", "an unrecognised analyzer".padEnd(26),
    frames === null ? "never parked (correct)" : `PARKED after ${frames}`);
  if (frames !== null) {
    fail("the loop parked against an analyzer with no frame identity and no "
      + "`native` claim. A synthetic analyzer animates from the clock, and "
      + "nothing in this loop can see that, so the honest answer is to keep "
      + "painting");
  }
  render();
  settle(600);
}

// --------------------------------- 5. no allocation in the per-frame path
// drawSpectrum built a `new Float32Array(steps + 1)` every frame -- 321 floats
// at 60Hz -- while peaksRef and avgSpectrumRef on the next two lines were
// already hoisted.
{
  publishAnalyzerFrame(0.5);
  for (let i = 0; i < 10; i++) { publishAnalyzerFrame(0.5); step(); }  // warm up
  f32Allocs = 0;
  const FRAMES = 60;
  for (let i = 0; i < FRAMES; i++) {
    publishAnalyzerFrame(0.4 + 0.2 * Math.sin(i / 5));   // keep it awake
    step();
  }
  console.log("alloc     %d painted frame(s) allocated %d Float32Array(s)",
    FRAMES, f32Allocs);
  if (f32Allocs !== 0) {
    fail(`${FRAMES} painted frames allocated ${f32Allocs} Float32Arrays. The `
      + "spectrum's sample buffer is fixed-size and lives as long as the "
      + "editor; allocating it per frame is pure GC churn on a runtime whose "
      + "collector shares a thread with the UI");
  }
  analyzerLevel = 0;
  settle(600);
}

// ------------------------- 6. the status banner is not a whole-document event
// The direct-DOM fast path owns the text AND the pill geometry. Publishing the
// same reading back through `onStatus` re-applies the whole captured document.
{
  render();
  settle(600);
  // Put the pointer on a band and give it a reading that keeps moving.
  dispatchWindow("pointermove", pointerEvent(centre(12), PLOT_Y));
  surface.props.onPointerMove(pointerEvent(centre(12), PLOT_Y));
  render();
  // A target the hovered band is NOT already sitting on, so the reading
  // under the pointer genuinely sweeps while it travels there.
  sharedState.current.setGains(Array.from({ length: N }, (_, i) => (i === 12 ? -0.8 : 0)));
  render();
  statusCalls = 0;
  bannerText.textContent = "";
  clock += 5000;                       // past any throttle window
  let labelWrites = 0;
  let previous = bannerText.textContent;
  for (let i = 0; i < 90; i++) {
    step();
    clock += 400;                      // three throttle windows across the run
    if (bannerText.textContent !== previous) {
      labelWrites++;
      previous = bannerText.textContent;
    }
  }
  console.log("status    %d frame(s): %d direct label write(s), %d React "
    + "publication(s)", 90, labelWrites, statusCalls);
  // STIMULUS CONTROL. Zero publications is what a correct document and a
  // pointer that never landed on a band both produce. The direct writes prove
  // the reading was live and moving, so the zero below means something.
  if (labelWrites === 0) {
    console.error("FAIL: the banner's own text never changed across 90 frames, "
      + "so this row never exercised a live reading and its publication count "
      + "is not evidence about anything");
    process.exit(2);
  }
  if (statusCalls !== 0) {
    fail(`a live hover reading published ${statusCalls} status update(s) back `
      + "through React across 90 frames. `onStatus` is the parent's state and "
      + "this document is a captured import, so each one re-applies the whole "
      + "captured document -- and the pill's width is a function of the string "
      + "it paints, so no paint-only exemption can ever cover it");
  }
}

// ---------------------------------------------------------------- verdict
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
console.log("PASS: the editor parks when nothing is moving, wakes for every "
  + "path that can change the canvas, allocates nothing per frame, and does "
  + "not republish a live hover reading through React.");
