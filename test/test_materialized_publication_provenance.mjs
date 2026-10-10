#!/usr/bin/env node
// A FULL-STATE PUBLICATION SAYS WHAT IT WAS DRAWN FROM, AND CARRIES THE LIVE
// VIEWPORT.
//
// The editor publishes its whole picture (`processing_state_set`: every band,
// the viewport, the band count) on every edit, a frame after the render it
// read, while host automation reaches the same state on the parameter-sync
// worker thread and is handed to the editor once per frame. Native merges a
// publication against the state it was DRAWN from -- a value equal to that
// state is carried over, not edited -- so a stale publication cannot revert a
// host write. That needs the editor to say which state that was.
//
// FilterBank is evaluated in a vm with the same hook shim as
// test_materialized_band_press_cost.mjs, and driven through its real pointer
// handlers and bank handle:
//
//   DRAWN      Every publication carries `drawn_revision`: the revision of the
//              last state the bank APPLIED. A projection it applied advances
//              it; an older one it refused does not.
//   LIVE VIEW  The window a publication carries is the live one: after a
//              `setView` that has not rendered yet (the render-time `view`
//              object is then the previous window), and during continuous
//              host viewport automation that never renders at all.
//   AUTOMATION A band edit during host gain/mute automation carries the
//              automated values of every other band.
//
// Usage:
//   node test_materialized_publication_provenance.mjs <runtime.json>
//        [--plant-no-drawn | --plant-render-view] [--expect-fail]
//
// --plant-no-drawn     publications carry no drawn revision, as shipped.
// --plant-render-view  publications read the render-time `view`, as shipped.
// --expect-fail inverts the verdict: green only when this suite REJECTS the
// planted document.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const documentPath = args.find((a) => !a.startsWith("--"));
const expectFail = args.includes("--expect-fail");
const has = (n) => args.includes(n);

if (!documentPath) {
  console.error("usage: test_materialized_publication_provenance.mjs <runtime.json> "
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

if (has("--plant-no-drawn")) {
  plant("publications without a drawn revision",
    "    drawn: nativeAppliedRevisionRef.current\n", "    drawn: undefined\n");
}
if (has("--plant-render-view")) {
  plant("publications read the render-time view",
    "    lmin: viewRef.current.lmin,\n    lmax: viewRef.current.lmax,\n",
    "    lmin: view.lmin,\n    lmax: view.lmax,\n");
}

const failures = [];
const fail = (msg) => failures.push(msg);

const blocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const bankBlock = blocks.find((b) => b.includes("function FilterBank("));
const controls = {
  "script blocks": blocks.length,
  "filter bank": html.split("function FilterBank(").length - 1,
  "pointer down": html.split("  const onPointerDown = (e) => {").length - 1,
  "pointer up": html.split("  const onPointerUp = (e) => {").length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(16), count);
const blind = Object.entries(controls)
  .filter(([k, v]) => (k === "script blocks" ? v === 0 : v !== 1)).map(([k]) => k);
if (blind.length || !bankBlock) {
  console.error("FAIL: " + (blind.join(", ") || "a script block")
    + " is not present exactly once -- this suite is reading the wrong "
    + "document, so it cannot render a verdict");
  process.exit(2);
}

// A hook runtime shared by both halves. Renders only when asked; a state
// change React would schedule is counted in `commits`.
const makeHooks = () => {
  const state = { slots: [], cursor: 0, effects: [], cleanups: [], commits: [] };
  const depsChanged = (prev, deps) => !prev || !prev.deps || !deps
    || deps.length !== prev.deps.length || deps.some((d, j) => d !== prev.deps[j]);
  state.React = {
    createElement: (type, props, ...children) => ({ type, props, children }),
    memo: (f) => f, Fragment: "Fragment",
    useState(init) {
      const i = state.cursor++;
      if (!state.slots[i]) state.slots[i] = { value: typeof init === "function" ? init() : init };
      return [state.slots[i].value, (next) => {
        const resolved = typeof next === "function" ? next(state.slots[i].value) : next;
        if (Object.is(resolved, state.slots[i].value)) return;   // React bails out
        state.slots[i].value = resolved;
        state.commits.push(i);
      }];
    },
    useRef(init) {
      const i = state.cursor++;
      if (!state.slots[i]) state.slots[i] = { current: init };
      return state.slots[i];
    },
    useMemo(fn, deps) {
      const i = state.cursor++;
      if (depsChanged(state.slots[i], deps)) state.slots[i] = { deps, value: fn() };
      return state.slots[i].value;
    },
    useCallback(fn, deps) {
      const i = state.cursor++;
      if (depsChanged(state.slots[i], deps)) state.slots[i] = { deps, fn };
      return state.slots[i].fn;
    },
    useEffect(fn, deps) {
      const i = state.cursor++;
      const prev = state.slots[i];
      state.slots[i] = { deps };
      if (depsChanged(prev, deps)) state.effects.push({ i, fn });
    },
  };
  state.React.useLayoutEffect = state.React.useEffect;
  state.runEffects = () => {
    while (state.effects.length) {
      const e = state.effects.shift();
      if (typeof state.cleanups[e.i] === "function") state.cleanups[e.i]();
      state.cleanups[e.i] = e.fn();
    }
  };
  return state;
};

// Timers and frames on one controllable clock.
let clock = 1000;
const timers = new Map();
let timerSeq = 0;
const frames = new Map();
let frameSeq = 0;
const timing = {
  setTimeout(fn, ms) { const id = ++timerSeq; timers.set(id, { fn, at: clock + (ms || 0) }); return id; },
  clearTimeout(id) { timers.delete(id); },
  setInterval() { return 0; }, clearInterval() {},
  requestAnimationFrame(fn) { const id = ++frameSeq; frames.set(id, fn); return id; },
  cancelAnimationFrame(id) { frames.delete(id); },
  performance: { now: () => clock },
};
const advance = (ms) => {
  const until = clock + ms;
  while (clock < until) {
    clock = Math.min(until, clock + 1000 / 60);
    const due = [...frames.values()];
    frames.clear();
    for (const fn of due) fn(clock);
    for (const [id, t] of [...timers.entries()]) {
      if (t.at <= clock) { timers.delete(id); t.fn(); }
    }
  }
};
const baseGlobals = {
  Math, Array, Object, Set, Map, String, Number, Boolean, JSON, Date, Error,
  Promise, Symbol, Uint8Array, Float32Array, isNaN, isFinite, parseFloat,
  parseInt, Infinity, NaN, console: { log() {}, warn() {}, error() {} },
};

// ================================================================ BANK
{
  const hooks = makeHooks();
  const posted = [];
  const shown = [];
  let statusCalls = 0;
  const N = 32;
  const WIDTH = 1000;
  const HEIGHT = 600;
  const context2d = () => new Proxy({
    canvas: { width: WIDTH, height: HEIGHT },
    measureText: (s) => ({ width: String(s || "").length * 6 }),
    createLinearGradient: () => ({ addColorStop() {} }),
    createRadialGradient: () => ({ addColorStop() {} }),
    createPattern: () => null,
  }, { get(o, p) { return p in o ? o[p] : () => {}; }, set(o, p, v) { o[p] = v; return true; } });
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
  const sandbox = {
    ...baseGlobals, ...timing, React: hooks.React,
    addEventListener() {}, removeEventListener() {},
    spectrPlaceStatusBanner() {},
    spectrStatusBannerWidth: (s) => 40 + String(s || "").length * 6,
    // The banner's direct show, recorded. Its own behaviour is the BANNER
    // half's subject.
    spectrStatusBannerShow: (message) => { shown.push(message); return true; },
    document: {
      getElementById: () => null, querySelector: () => null,
      createElement: () => ({ getContext: () => null, style: {} }),
      addEventListener() {}, removeEventListener() {}, body: { style: {} },
    },
  };
  sandbox.globalThis = sandbox;
  sandbox.window = sandbox;
  sandbox.__spectrTestHooks = {};
  sandbox.SpectrFreq = { fmt: (hz) => String(Math.round(hz)) };
  sandbox.SpectrAnalyzer = {
    native: true, sample: () => 0, project: (a, z, h) => z - a * h * 0.95,
    scale: () => ({ floor: -120, ceiling: 24 }), normalizeDb: () => 0,
    debugSnapshot: () => ({ epoch: 1, sequence_number: 0 }),
  };
  sandbox.Spectr = { FACTORY_PATTERNS: [], resolveGains: () => [] };
  sandbox.pulp = {
    postMessage(type, payload) {
      posted.push({ type, payload });
      return Promise.resolve({ ok: true, payload: { ok: true, revision: 1 } });
    },
    on() { return () => {}; },
  };
  try {
    vm.runInContext(bankBlock, vm.createContext(sandbox), { filename: "bank-block.js" });
  } catch (error) {
    console.error(`FAIL: evaluating the bank's script block threw ${error.message}`);
    process.exit(2);
  }
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
      if (p.ref && typeof p.ref === "object" && "current" in p.ref && !p.ref.current) {
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
  const render = () => {
    hooks.cursor = 0;
    const element = sandbox.FilterBank(props);
    hooks.runEffects();
    attach(element);
    return element;
  };
  const PAD = { l: 56, r: 56, t: 70, b: 120 };
  const inner = { x: PAD.l, y: PAD.t, w: WIDTH - PAD.l - PAD.r, h: HEIGHT - PAD.t - PAD.b };
  const bandW = (inner.w - 2 * (N - 1)) / N;
  const centre = (i) => inner.x + i * (bandW + 2) + bandW / 2;
  const PLOT_Y = inner.y + inner.h * 0.4;
  let stamp = 0;
  const pointer = (x, y, mods) => ({
    clientX: x, clientY: y, pointerId: 1, button: 0, timeStamp: ++stamp,
    shiftKey: !!(mods && mods.shift), altKey: !!(mods && mods.alt), metaKey: false,
    ctrlKey: false, preventDefault() {}, stopPropagation() {},
  });
  // Renders until nothing React would schedule is left: the harness's
  // equivalent of the editor settling after mount.
  const settle = () => {
    for (let i = 0; i < 8; i++) {
      const before = hooks.commits.length;
      render();
      advance(50);
      if (hooks.commits.length === before) break;
    }
  };
  settle();

  render();
  const bank = sharedState.current;
  if (!surface || typeof surface.props.onPointerDown !== "function"
      || !bank || typeof bank.applyHostAutomationState !== "function"
      || typeof bank.hydrateProcessingState !== "function") {
    console.error("FAIL: the bank rendered no pointer surface or bank handle");
    process.exit(2);
  }
  const stateOf = (revision, over = {}) => {
    const gainDb = new Array(N).fill(0);
    const muted = new Array(N).fill(false);
    for (const [i, db] of Object.entries(over.gains || {})) gainDb[i] = db;
    for (const i of over.muted || []) muted[i] = true;
    return {
      n: N, gainDb, muted,
      gains: gainDb.map((db, i) => muted[i] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
      minHz: over.minHz || 20, maxHz: over.maxHz || 20000, revision,
      motionMode: "live", analyzerMode: "both", editMode: "sculpt",
      visualizationMode: "both", canUndo: false, canRedo: false, macros: null,
      snapshots: { A: null, B: null },
    };
  };
  const lastSet = () => posted.filter((p) => p.type === "processing_state_set").pop();
  const click = (band) => {
    surface.props.onPointerDown(pointer(centre(band), PLOT_Y));
    surface.props.onPointerUp(pointer(centre(band), PLOT_Y));
  };
  const near = (a, b) => Math.abs(a - b) <= Math.abs(b) * 1e-9 + 1e-9;

  bank.hydrateProcessingState(stateOf(5));
  settle();
  render();

  // 1. A projection the bank applied advances the drawn revision; the direct
  //    publication a click makes carries it and the live automated window.
  const applied = bank.applyHostAutomationState(stateOf(9, {
    gains: { 2: -12 }, muted: [9], minHz: 100, maxHz: 1000 }));
  if (applied !== true) {
    console.error("FAIL: the bank refused a newer projection, so nothing below "
      + "is measured against a state it applied");
    process.exit(2);
  }
  posted.length = 0;
  click(20);
  let set = lastSet();
  console.log("measured  click after projection 9: drawn %s, window %s..%s",
    set && set.payload.drawn_revision, set && set.payload.min_hz,
    set && set.payload.max_hz);
  if (!set) fail("a click published nothing, so no provenance was measured");
  else {
    if (set.payload.drawn_revision !== 9)
      fail(`a click after applying projection 9 published drawn_revision `
        + `${set.payload.drawn_revision}; native cannot tell what it carried over`);
    if (!near(set.payload.min_hz, 100) || !near(set.payload.max_hz, 1000))
      fail(`a click during host viewport automation published the window `
        + `${set.payload.min_hz}..${set.payload.max_hz}, not the automated 100..1000`);
    if (set.payload.gain_db[2] !== -12 || set.payload.muted[9] !== true)
      fail("a click during host gain/mute automation did not carry the "
        + "automated values of the bands it did not touch");
    if (set.payload.muted[20] !== true)
      fail("the click's own edit is missing from its publication");
  }

  // 2. An OLDER projection the bank refuses does not move the drawn revision.
  if (bank.applyHostAutomationState(stateOf(7, { minHz: 300, maxHz: 3000 })) !== false)
    fail("the bank applied a projection older than one it already holds");
  posted.length = 0;
  click(21);
  set = lastSet();
  if (!set || set.payload.drawn_revision !== 9)
    fail(`after refusing projection 7 the bank published drawn_revision `
      + `${set && set.payload.drawn_revision}, expected the 9 it applied`);

  // 3. Continuous host viewport automation, never rendered: every window the
  //    host moves through is live in the next publication.
  for (let step = 0; step < 6; step++) {
    const lo = 150 + step * 40, hi = 1500 + step * 400;
    bank.applyHostAutomationState(stateOf(10 + step, { minHz: lo, maxHz: hi }));
    advance(16);
  }
  posted.length = 0;
  click(22);
  set = lastSet();
  if (!set || !near(set.payload.min_hz, 350) || !near(set.payload.max_hz, 3500)
      || set.payload.drawn_revision !== 15)
    fail(`a click mid-automation published ${set && set.payload.min_hz}..`
      + `${set && set.payload.max_hz} drawn ${set && set.payload.drawn_revision}; `
      + "expected the last automated 350..3500 at revision 15");

  // 4. A setView that has not rendered yet: the render-time `view` is the
  //    previous window, the live one is the new one.
  bank.zoomTo(400, 4000);
  posted.length = 0;
  click(23);
  set = lastSet();
  console.log("measured  click after unrendered setView: window %s..%s",
    set && set.payload.min_hz, set && set.payload.max_hz);
  if (!set || !near(set.payload.min_hz, 400) || !near(set.payload.max_hz, 4000))
    fail(`a click after zooming to 400..4000 (not yet rendered) published `
      + `${set && set.payload.min_hz}..${set && set.payload.max_hz}: the window `
      + "the editor already left");

  // 5. The render publication (the [gains, view, N] effect) carries the same.
  settle();
  render();
  posted.length = 0;
  bank.setGains(new Array(N).fill(0.25));
  render();
  advance(50);
  set = lastSet();
  if (!set) fail("a React edit published nothing through the render effect");
  else if (set.payload.drawn_revision !== 15)
    fail(`the render publication carried drawn_revision `
      + `${set.payload.drawn_revision}, expected 15`);

  // 6. Several direct pointer samples in one presentation interval collapse
  //    to the latest complete state. Release still flushes that final state
  //    synchronously, so callers do not need to wait for the next frame.
  posted.length = 0;
  if (typeof surface.props.onPointerMove !== "function")
    fail("the surface has no pointer-move handler for the coalescing probe");
  else {
    surface.props.onPointerDown(pointer(centre(24), PLOT_Y));
    for (let i = 1; i <= 5; i++)
      surface.props.onPointerMove(pointer(centre(24) + i * 2, PLOT_Y));
    const beforeFramePublishes = posted.filter((p) => p.type === "processing_state_set");
    if (beforeFramePublishes.length !== 0)
      fail(`rapid move samples published ${beforeFramePublishes.length} states before a frame`);
    advance(16);
    const framePublishes = posted.filter((p) => p.type === "processing_state_set");
    if (framePublishes.length !== 1)
      fail(`rapid move samples published ${framePublishes.length} states in one frame; expected 1`);
    surface.props.onPointerUp(pointer(centre(24) + 10, PLOT_Y));
    const releasePublishes = posted.filter((p) => p.type === "processing_state_set");
    if (releasePublishes.length !== 1)
      fail(`pointer release published ${releasePublishes.length} states after the frame flush; expected 1`);
  }
}

const passed = failures.length === 0;
for (const f of failures) console.log("FAIL:", f);
if (passed) {
  console.log("PASS: every publication names the state it was drawn from and "
    + "carries the live viewport and the automated values.");
}
if (expectFail) {
  if (passed) {
    console.error("CONTROL FAIL: the planted document was accepted, so this "
      + "suite cannot detect the defect it claims to guard");
    process.exit(1);
  }
  console.log("CONTROL PASS: the planted document was rejected.");
  process.exit(0);
}
process.exit(passed ? 0 : 1);
