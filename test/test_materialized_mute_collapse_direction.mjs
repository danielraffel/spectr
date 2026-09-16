#!/usr/bin/env node
// A BAND GOING SILENT COLLAPSES TO THE ZERO LINE. IT DOES NOT PLUNGE.
//
// Reported from the installed build: "when muting i am not sure we should draw
// that box under the 0 horizontal line -- its an animation flash and is just
// too much". The screenshots show a tall outlined box sweeping from the zero
// line to the bottom of the plot and vanishing.
//
// Measured out of the painter's own command stream on the pre-fix document
// (band 5, 24 bands at 1000x600, zeroY 295.5, plot floor 480):
//
//     frame   rg[5]       selection stroke        body fill
//     0      -0.191       40.0t y293.5..333.5     35.8t y295.5..331.3
//     5      -0.925      176.0t y293.5..469.5    171.2t y295.5..466.7
//     10     -1.009      191.0t y293.5..484.5    186.7t y295.5..482.2
//     11     -Infinity     5.0t y293.5..298.5    (gone)
//
// Eleven frames, ~185 ms, 4.5 px PAST the floor of the plot, then gone.
//
// The cause is not the painter. `renderGainsRef` was ramped toward -1.02 so
// the next line could notice it crossed -1.01 and snap to the `-Infinity`
// sentinel -- a TRIPWIRE for the state machine, not a gain the band travels
// to. The painter cannot tell those apart, so it drew the trip. The settled
// state proves the intent: once the sentinel lands, `effectiveGains`
// normalises the non-finite value to ZERO and a muted band's body has no
// height at all. The ramp now travels to 0 and trips on arrival.
//
// WHAT THIS ASSERTS, AND WHY IT IS NOT "NEVER BELOW THE ZERO LINE".
// A band at negative gain draws below the zero line legitimately -- that is
// what negative gain looks like -- so a depth rule would fail on honest
// output and would have to be tuned until it stopped meaning anything. The
// invariant is about the DIRECTION OF TRAVEL:
//
//   A  the band's drawn extent is monotonically NON-INCREASING across every
//      frame of the transition. Pre-fix it grew 35.8 -> 186.7, a factor of
//      5.2. This is the assertion the defect fails.
//   B  nothing the band draws reaches the plot floor. A rect taller than the
//      plot it lives in is not information at any moment.
//   C  the transition TERMINATES on the sentinel, so "it never grows" cannot
//      be satisfied by a band that simply never animates and never mutes.
//   D  the last finite frame has collapsed to the line, so "non-increasing"
//      cannot be satisfied by stopping half way.
//   E  the settled state and the mute-chip selection routing are unchanged.
//
// EVERY FRAME, NOT THE ENDPOINTS. The defect is a flash: it is absent at the
// start and absent at the end, and an endpoints-only check passes while it is
// fully present. `--plant-midframe-dip` is that exact shape and exists to
// prove this suite samples the whole transition.
//
// DETECTION FLOOR. The instrument records the painter's drawing commands, so
// its geometric floor is 0 px -- no sampling, no anti-aliased boundary row.
// Its TEMPORAL floor is one frame: it reads every frame the rAF loop runs, so
// it cannot miss a flash lasting one frame or longer, and a flash shorter than
// one frame cannot be rendered at all. What it cannot see is compositing -- an
// alpha-0 mark records like a visible one -- which does not matter here
// because the defect is geometry, but is why nothing below reasons from
// colour.
//
// Usage:
//   node test_materialized_mute_collapse_direction.mjs <runtime.json>
//        [--plant-plunge | --plant-grow | --plant-frozen | --plant-midframe-dip]
//        [--expect-fail] [--report]

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const PLANTS = ["plunge", "grow", "frozen", "midframe-dip"];
const planted = PLANTS.filter((p) => args.includes(`--plant-${p}`));
const expectFail = args.includes("--expect-fail");
const report = args.includes("--report");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_mute_collapse_direction.mjs "
    + "<runtime.json> [--plant-...] [--expect-fail] [--report]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

// --------------------------------------------------------------- plants

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

const SEED = "            if (!Number.isFinite(rg[i])) rg[i] = 0;\n";
const RAMP = "            rg[i] = smooth(rg[i], 0, dt * 26);\n";
const TRIP = "            if (!Number.isFinite(rg[i]) || Math.abs(rg[i]) < 0.004) "
  + "rg[i] = -Infinity;\n";

if (planted.includes("plunge")) {
  // The shipped defect, exactly: travel to the tripwire and draw the trip.
  plant("the ramp travels to -1.02 again, so the band plunges past the floor",
    SEED + RAMP + TRIP,
    "            if (!Number.isFinite(rg[i])) rg[i] = -1.02;\n"
    + "            rg[i] = smooth(rg[i], -1.02, dt * 26);\n"
    + "            if (!Number.isFinite(rg[i]) || rg[i] < -1.01) rg[i] = -Infinity;\n");
}

if (planted.includes("grow")) {
  // Grows UPWARD instead. It never goes below the zero line at all, so a
  // depth-only rule would pass it: this is what earns assertion A its
  // "non-increasing" shape rather than a threshold.
  plant("the ramp travels to +1.02, so the band grows instead of collapsing",
    SEED + RAMP + TRIP,
    "            if (!Number.isFinite(rg[i])) rg[i] = 1.02;\n"
    + "            rg[i] = smooth(rg[i], 1.02, dt * 26);\n"
    + "            if (!Number.isFinite(rg[i]) || rg[i] > 1.01) rg[i] = -Infinity;\n");
}

if (planted.includes("frozen")) {
  // Never moves and never trips. Nothing ever grows and nothing ever goes
  // deep, so A and B are trivially satisfied -- C is the only thing standing
  // between this suite and certifying a band that never mutes.
  plant("the ramp is frozen, so the band never reaches the sentinel",
    RAMP, "            rg[i] = smooth(rg[i], rg[i], dt * 26);\n");
}

if (planted.includes("midframe-dip")) {
  // ONE deep frame in the middle of the transition. Absent at the first
  // frame, absent at the last. An endpoints-only suite reports this clean.
  plant("the band dips to the floor for a single mid-transition frame",
    RAMP,
    "            rg[i] = (rg[i] < 0.15 && rg[i] > 0.05) ? -1.02 "
    + ": smooth(rg[i], 0, dt * 26);\n");
}

const failures = [];
const fail = (msg) => failures.push(msg);
const check = (label, ok, detail) => {
  console.log("%s  %s", ok ? "ok   " : "FAIL ", label);
  if (!ok) fail(detail ? `${label} -- ${detail}` : label);
};

// ------------------------------------------------------- positive controls

const scriptBlocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const bankBlock = scriptBlocks.find((b) => b.includes("function FilterBank("));

const controls = {
  "filter bank": html.split("function FilterBank(").length - 1,
  "band painter": html.split("function drawBands(ctx, g) {").length - 1,
  "rAF draw loop": html.split("const draw = (now) => {").length - 1,
  // The ramp, matched WITHOUT its target value: every plant above rewrites
  // that value, and a control carrying it would read zero on a planted
  // document and abort before the check the plant exists to exercise.
  "mute ramp": html.split("rg[i] = smooth(rg[i], ").length - 1,
  "sentinel normalisation": html.split(
    "const effectiveGains = rg.map((value, index) => Number.isFinite(value) "
    + "? clamp(macroAdjustedGain(value, index), -1.02, 1.02) : 0);").length - 1,
  // The needle above carries `macroAdjustedGain` because the shipping painter
  // routes every displayed gain through the macro overlay. That call is
  // irrelevant to mute collapse, but the needle must match the painter
  // EXACTLY or this control reads 0 and the suite refuses to render a verdict
  // -- which is what it did, correctly, the first time the macro lane changed
  // this line.
  "mute chip box": html.split("function muteChipRect(i, g) {").length - 1,
  "paint state readback": html.split(
    "gains: Array.from(renderGainsRef.current)").length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(24), count);
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length || !bankBlock) {
  console.error("FAIL: the payload has no " + (blind.join(", ") || "bank block")
    + " -- this suite is reading the wrong document or the painter was "
    + "restructured, so it cannot render a verdict");
  process.exit(2);
}

// PAINT ONLY. Asserted, not assumed: if a publication ever read the ramp,
// changing its direction would change what the DSP is told, and this whole
// suite would be reasoning about the wrong blast radius.
const publications = html.split("window.pulp.postMessage(\"processing_state_set\"").length - 1;
const fromTarget = html.split("const current = targetGainsRef.current;").length - 1;
console.log("control   %s %d publication(s), %d reading targetGainsRef",
  "paint-only".padEnd(24), publications, fromTarget);
if (publications === 0 || fromTarget < publications) {
  fail(`${publications} processing_state_set publication(s) but only ${fromTarget} `
    + "read targetGainsRef -- the paint ramp may reach the DSP, so its "
    + "direction is not a paint-only change");
}

// --------------------------------------------------------- the instrument

function recordingContext() {
  const groups = [];
  let pts = [];
  const push = (x, y) => { if (Number.isFinite(x) && Number.isFinite(y)) pts.push([x, y]); };
  const box = (l) => {
    const xs = l.map((p) => p[0]), ys = l.map((p) => p[1]);
    return { x: Math.min(...xs), y: Math.min(...ys),
             w: Math.max(...xs) - Math.min(...xs),
             h: Math.max(...ys) - Math.min(...ys) };
  };
  const close = (kind, style) => {
    if (pts.length) groups.push({ kind, style, ...box(pts) });
    pts = [];
  };
  const ctx = {
    strokeStyle: "", fillStyle: "", lineWidth: 1, lineJoin: "", lineCap: "",
    globalCompositeOperation: "", font: "", textAlign: "", textBaseline: "",
    globalAlpha: 1, shadowBlur: 0, shadowColor: "", filter: "",
    save() {}, restore() {}, clip() { pts = []; }, closePath() {},
    beginPath() { pts = []; },
    moveTo: (x, y) => push(x, y), lineTo: (x, y) => push(x, y),
    arcTo: (a, b, c, d) => { push(a, b); push(c, d); },
    bezierCurveTo: (_a, _b, _c, _d, x, y) => push(x, y),
    quadraticCurveTo: (_a, _b, x, y) => push(x, y),
    arc: (x, y, r) => { push(x - r, y - r); push(x + r, y + r); },
    ellipse() {}, rect: (x, y, w, h) => { push(x, y); push(x + w, y + h); },
    stroke() { close("stroke", ctx.strokeStyle); },
    fill() { close("fill", ctx.fillStyle); },
    strokeRect(x, y, w, h) { groups.push({ kind: "strokeRect", style: ctx.strokeStyle, x, y, w, h }); },
    fillRect(x, y, w, h) { groups.push({ kind: "fillRect", style: ctx.fillStyle, x, y, w, h }); },
    clearRect() {}, fillText() {}, strokeText() {},
    measureText: (t) => ({ width: String(t).length * 6,
                           actualBoundingBoxAscent: 6, actualBoundingBoxDescent: 2 }),
    setLineDash() {}, getLineDash: () => [], setTransform() {}, transform() {},
    translate() {}, scale() {}, rotate() {}, drawImage() {},
    createLinearGradient: () => ({ addColorStop() {} }),
    createRadialGradient: () => ({ addColorStop() {} }),
    createPattern: () => null, isPointInPath: () => false,
  };
  return { ctx, groups, reset() { groups.length = 0; pts = []; } };
}

// Lifted from the document rather than restated, so a restyle cannot leave
// this suite tracking a mark that no longer exists while reporting the one it
// remembers.
const SELECTION_STROKE = (() => {
  const at = html.indexOf("      if (G.isSel");
  if (at < 0) return null;
  const m = html.slice(at, at + 4000).match(/ctx\.strokeStyle\s*=\s*"([^"]+)"/);
  return m ? m[1] : null;
})();
if (!SELECTION_STROKE) {
  console.error("FAIL: the selection branch declares no strokeStyle, so this "
    + "suite cannot tell the selection outline from any other stroke");
  process.exit(2);
}
console.log("control   %s %s", "selection stroke".padEnd(24), SELECTION_STROKE);

const N = 24;
const WIDTH = 1000;
const HEIGHT = 600;
const PAD = { l: 56, r: 56, t: 70, b: 120 };
const inner = { x: PAD.l, y: PAD.t, w: WIDTH - PAD.l - PAD.r, h: HEIGHT - PAD.t - PAD.b };
const zeroY = inner.y + inner.h * 0.55;
const PLOT_FLOOR = inner.y + inner.h;
const bandGap = 2;
const bandW = (inner.w - bandGap * (N - 1)) / N;
const centre = (i) => inner.x + i * (bandW + bandGap) + bandW / 2;
const CHIP_H = Math.min(26, Math.max(18, inner.h * 0.12));
// The mute chip is the band's BUTTON and legitimately straddles the zero line;
// its ring adds the 2.5 selection outset. Derived, never tuned.
const CHIP_ALLOWANCE = CHIP_H / 2 + 2.5;
const PLOT_Y = inner.y + inner.h * 0.4;

// One transition, captured frame by frame.
function runTransition({ muteStyle, select, muteIndices, frames = 26 }) {
  const hooks = []; let cursor = 0;
  const effects = []; const cleanups = [];
  const React = {
    createElement: (type, props, ...children) => ({ type, props, children }),
    memo: (fn) => fn, Fragment: "Fragment",
    useState(initial) {
      const i = cursor++;
      if (!hooks[i]) hooks[i] = { value: typeof initial === "function" ? initial() : initial };
      return [hooks[i].value, (n) => { hooks[i].value = typeof n === "function" ? n(hooks[i].value) : n; }];
    },
    useRef(initial) { const i = cursor++; if (!hooks[i]) hooks[i] = { current: initial }; return hooks[i]; },
    useMemo(fn, deps) {
      const i = cursor++; const p = hooks[i];
      const ch = !p || !p.deps || !deps || deps.length !== p.deps.length || deps.some((d, j) => d !== p.deps[j]);
      if (ch) hooks[i] = { deps, value: fn() };
      return hooks[i].value;
    },
    useCallback(fn, deps) {
      const i = cursor++; const p = hooks[i];
      const ch = !p || !p.deps || !deps || deps.length !== p.deps.length || deps.some((d, j) => d !== p.deps[j]);
      if (ch) hooks[i] = { deps, fn };
      return hooks[i].fn;
    },
    useEffect(fn, deps) {
      const i = cursor++; const p = hooks[i];
      const ch = !p || !p.deps || !deps || deps.length !== p.deps.length || deps.some((d, j) => d !== p.deps[j]);
      hooks[i] = { deps };
      if (ch) effects.push({ i, fn });
    },
  };
  const main = recordingContext();
  const overlay = recordingContext();
  let rafQueue = []; let clock = 1000;
  const domStub = (extra) => ({
    style: {}, dataset: {},
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    getBoundingClientRect: () => ({ left: 0, top: 0, right: WIDTH, bottom: HEIGHT, width: WIDTH, height: HEIGHT }),
    setPointerCapture() {}, releasePointerCapture() {},
    setAttribute() {}, removeAttribute() {}, getAttribute: () => null,
    addEventListener() {}, removeEventListener() {},
    appendChild() {}, removeChild() {}, contains: () => false,
    focus() {}, blur() {}, querySelector: () => null, querySelectorAll: () => [],
    ...extra,
  });
  const wrapElement = domStub({ clientWidth: WIDTH, clientHeight: HEIGHT });
  const canvasStub = (rec) => domStub({ width: WIDTH, height: HEIGHT, getContext: () => rec.ctx });
  const sandbox = {
    React, Math, Array, Object, Set, Map, String, Number, Boolean, JSON, Date,
    Error, Promise, Float32Array, Uint8Array, isNaN, isFinite, parseFloat,
    parseInt, Infinity, NaN,
    console: { log() {}, warn() {}, error() {} },
    requestAnimationFrame(fn) { rafQueue.push(fn); return rafQueue.length; },
    cancelAnimationFrame(id) { rafQueue[id - 1] = null; },
    setTimeout() { return 0; }, clearTimeout() {},
    setInterval() { return 0; }, clearInterval() {},
    performance: { now: () => clock }, devicePixelRatio: 1,
    addEventListener() {}, removeEventListener() {},
    document: {
      getElementById: () => null, querySelector: () => null, querySelectorAll: () => [],
      createElement: () => ({ getContext: () => null, style: {} }),
      addEventListener() {}, removeEventListener() {},
    },
  };
  sandbox.globalThis = sandbox; sandbox.window = sandbox;
  sandbox.__spectrTestHooks = {};
  sandbox.window.SpectrAnalyzer = { sample: () => -40, project: (v, z, h) => z - (v + 100) / 100 * h };
  sandbox.window.SpectrFreq = { fmt: (hz) => String(Math.round(hz)) };
  sandbox.window.Spectr = { FACTORY_PATTERNS: [], resolveGains: () => [] };
  sandbox.window.pulp = {
    postMessage() { return Promise.resolve({ ok: true, payload: { ok: true, revision: 1 } }); },
    on() { return () => {}; },
  };
  const sharedState = { current: null };
  const props = {
    settings: {
      bandCount: N, metaphor: "columns", bloom: 0, spectrumIntensity: 0,
      muteStyle, motionMode: "live", showMinimap: false, showRulers: false,
      theme: "dark", unmuteOnDraw: false,
    },
    sharedState, dspMode: "fft", editMode: "sculpt", analyzerMode: "off",
    visualizationMode: "bars", nativeHydrated: true,
    onStateChange() {}, onStatus() {}, onEditModeChange() {}, onNativeState() {},
  };
  function attachRefs(node, canvases, seen = new Set()) {
    if (!node || typeof node !== "object" || seen.has(node)) return;
    seen.add(node);
    if (Array.isArray(node)) { for (const c of node) attachRefs(c, canvases, seen); return; }
    const p = node.props || {};
    if (p.ref && typeof p.ref === "object") {
      if (node.type === "canvas") p.ref.current = canvases.shift() || null;
      else if (p.ref.current === null) p.ref.current = wrapElement;
    }
    for (const child of node.children || []) attachRefs(child, canvases, seen);
    if (p.children) attachRefs(p.children, canvases, seen);
  }
  let surface = null;
  const findSurface = (node) => {
    if (!node || typeof node !== "object") return null;
    if (Array.isArray(node)) { for (const c of node) { const h = findSurface(c); if (h) return h; } return null; }
    if (node.props && node.props["data-spectr-filter-surface"]) return node;
    for (const child of node.children || []) { const h = findSurface(child); if (h) return h; }
    return findSurface((node.props || {}).children);
  };
  const render = () => {
    cursor = 0;
    const element = sandbox.FilterBank(props);
    attachRefs(element, [canvasStub(main), canvasStub(overlay)]);
    surface = findSurface(element) || surface;
    if (surface && surface.props.ref) surface.props.ref.current = wrapElement;
    while (effects.length) {
      const e = effects.shift();
      try {
        if (typeof cleanups[e.i] === "function") cleanups[e.i]();
        cleanups[e.i] = e.fn();
      } catch { /* a DOM affordance this stub does not model */ }
    }
    return element;
  };
  const frame = (n = 1) => {
    for (let i = 0; i < n; i++) {
      clock += 16.667;
      const due = rafQueue; rafQueue = [];
      for (const fn of due) if (fn) { try { fn(clock); } catch { /* drained */ } }
    }
  };
  try { vm.runInContext(bankBlock, vm.createContext(sandbox), { filename: "bank.js" }); }
  catch (error) { return { error: `${error.constructor.name}: ${error.message}` }; }
  if (typeof sandbox.FilterBank !== "function") return { error: "FilterBank unreachable" };
  render(); render(); frame(2);
  if (!sandbox.__spectrTestHooks.renderState) return { error: "no renderState hook" };
  if (!surface) return { error: "no [data-spectr-filter-surface] in the tree" };
  const bank = sharedState.current;
  if (!bank || typeof bank.setGains !== "function") return { error: "bank exposes no setGains" };

  // Alternating signs: a band at negative gain draws BELOW the zero line
  // honestly, so a fixture of only positive bands could not tell a depth rule
  // from a direction rule.
  bank.setGains(new Array(N).fill(0).map((_, i) =>
    (i % 2 ? -1 : 1) * (0.45 + 0.2 * Math.sin(i))));
  render(); frame(24);

  let selection = [];
  if (select) {
    const ev = (x, y) => ({ clientX: x, clientY: y, pointerId: 1, button: 0,
      shiftKey: false, altKey: false, metaKey: true, ctrlKey: false,
      preventDefault() {}, stopPropagation() {} });
    const x0 = centre(select[0]) - bandW / 2 - 1;
    const x1 = centre(select[1]) + bandW / 2 + 1;
    surface.props.onPointerDown(ev(x0, PLOT_Y)); render();
    for (let s = 1; s <= 6; s++) { surface.props.onPointerMove(ev(x0 + (x1 - x0) * (s / 6), PLOT_Y)); render(); }
    surface.props.onPointerUp(ev(x1, PLOT_Y)); render(); frame();
    selection = sandbox.__spectrTestHooks.renderState().selection;
  }

  // The transition begins here.
  const tg = sandbox.__spectrTestHooks.renderState().targetGains.slice();
  for (const i of muteIndices) tg[i] = -Infinity;
  bank.setGains(tg);
  render();

  // WHAT COUNTS AS "WHAT THE BAND DRAWS", and why it is not every mark in the
  // column. The `cutout` style deliberately paints a full-height wash and two
  // dashed guides for a muted band, reaching the plot floor by design, and the
  // chip and its glyph straddle the zero line. NONE of those read the ramp:
  // they key off `targetGainsRef` and are identical on every frame of the
  // transition, so they are chrome that says "this band is muted", not the
  // moving mark that was reported. The marks that DO represent the value are
  // the body -- the only gradient-filled thing in the column -- and the
  // selection outline, which is the `outlined box` of the report and is the
  // mark that fell through to the body rect under `collapse`.
  const tracked = (gp) => typeof gp.style === "object" || gp.style === SELECTION_STROKE;
  const timeline = [];
  for (let f = 0; f < frames; f++) {
    main.reset();
    frame(1);
    const state = sandbox.__spectrTestHooks.renderState();
    const per = {};
    for (const i of muteIndices) {
      const mine = main.groups.filter((gp) =>
        Math.abs(gp.x + gp.w / 2 - centre(i)) < bandW * 0.9 && tracked(gp) && gp.h > 0.5);
      let bottom = -Infinity, top = Infinity;
      for (const m of mine) {
        bottom = Math.max(bottom, m.y + m.h);
        top = Math.min(top, m.y);
      }
      per[i] = {
        rg: state.gains[i],
        bottom: mine.length ? bottom : null,
        extent: mine.length ? bottom - top : null,
        marks: mine.length,
      };
    }
    timeline.push({ f, per,
      gridMarks: main.groups.filter((gp) => gp.w > bandW * 2).length });
  }
  return { timeline, selection };
}

// ------------------------------------------------------------ the readings

const MUTED = [5, 6];
const SELECT = [4, 7];

for (const muteStyle of ["cutout", "collapse"]) {
  console.log("\n--- muteStyle: %s ---", muteStyle);
  const run = runTransition({ muteStyle, select: SELECT, muteIndices: MUTED });
  if (run.error) {
    console.error(`FAIL: the ${muteStyle} run could not be driven: ${run.error}`);
    process.exit(2);
  }
  const { timeline } = run;

  // The grid is in every frame and is the proof the recorder stayed live: a
  // timeline of empty frames would satisfy every rule below.
  const liveFrames = timeline.filter((t) => t.gridMarks > 0).length;
  check(`${muteStyle}: the recorder stayed live for all ${timeline.length} frames`,
    liveFrames === timeline.length,
    `only ${liveFrames} frames recorded any grid`);

  if (run.selection.length === 0) {
    console.error("FAIL: the marquee selected nothing, so the selection mark is "
      + "absent for a reason this suite did not intend");
    process.exit(2);
  }

  for (const band of MUTED) {
    const series = timeline.map((t) => ({ f: t.f, ...t.per[band] }));
    const finite = series.filter((s) => Number.isFinite(s.rg));
    const settled = series.filter((s) => !Number.isFinite(s.rg));

    if (report) {
      for (const s of series.slice(0, 14)) {
        console.log(`  b${band} f${String(s.f).padStart(2)} rg=${String(s.rg).padEnd(13).slice(0, 13)}`
          + ` bottom=${s.bottom === null ? "-" : s.bottom.toFixed(1).padStart(7)}`
          + ` extent=${s.extent === null ? "-" : s.extent.toFixed(1).padStart(6)}`
          + ` depth=${s.bottom === null ? "-" : (s.bottom - zeroY).toFixed(1).padStart(6)}`);
      }
    }

    // D (run first: everything else is vacuous without it). The band must be
    // visibly drawn when the transition starts.
    const first = finite[0];
    check(`${muteStyle} b${band}: the transition starts from a visibly drawn band`,
      !!first && first.extent !== null && first.extent > 10,
      first ? `first transition frame extent is ${first.extent}` : "no finite frame at all");

    // C. It terminates on the sentinel.
    check(`${muteStyle} b${band}: the collapse reaches the -Infinity sentinel`,
      settled.length > 0 && finite.length < timeline.length,
      `${finite.length} of ${timeline.length} frames still finite -- the band `
      + "never finished muting, so it is not muted, only quiet");

    // A. THE DEFECT. Never larger than it has already been -- measured against
    // the RUNNING MAXIMUM of every earlier frame, not against the frame
    // immediately before.
    //
    // Comparing neighbours looks equivalent and is not, which a plant caught:
    // at exactly -1.02 the painter's own `effectiveGains[i] <= -1.01` guard
    // skips the band, so the deepest frame of a plunge records NO marks at all.
    // A neighbour chain then compares (drawn, null) and (null, drawn), skips
    // both, and never compares across the gap -- so a 35 -> 138 excursion with
    // one blank frame in the middle read CLEAN. A running maximum cannot be
    // stepped over that way.
    // GROWTH_SLACK is the painter's own quantisation, not a fudge factor: the
    // rects go out as Math.round(origin) and Math.round(size), rounded
    // INDEPENDENTLY, so a drawn edge can sit up to 1 px from its ideal and a
    // shrinking band's bottom legitimately jitters by a pixel. 1.5 is that plus
    // a half-pixel. It is 20x below the smallest excursion any plant below
    // produces (32 px), so it costs nothing in sensitivity to the defect.
    const GROWTH_SLACK = 1.5;
    let grew = null, peak = null;
    for (const s of finite) {
      if (s.extent === null) continue;
      if (peak && s.extent > peak.extent + GROWTH_SLACK) {
        grew = { peak, s, axis: "extent" }; break;
      }
      if (peak && s.bottom - zeroY > peak.bottom - zeroY + GROWTH_SLACK) {
        grew = { peak, s, axis: "depth" }; break;
      }
      if (!peak || s.extent > peak.extent) peak = s;
    }
    check(`${muteStyle} b${band}: the band never grows on its way to silent`,
      grew === null,
      grew ? `${grew.axis} grew from ${(grew.axis === "extent"
        ? grew.peak.extent : grew.peak.bottom - zeroY).toFixed(1)} at frame `
        + `${grew.peak.f} to ${(grew.axis === "extent"
        ? grew.s.extent : grew.s.bottom - zeroY).toFixed(1)} at frame ${grew.s.f}; `
        + "a band going silent that gets BIGGER is the reported flash" : undefined);

    // A2. It must not blink out and come back either. A frame on which a
    // transitioning band draws NOTHING is the painter's deep-value guard
    // firing, which is the plunge arriving at the bottom of the range -- and it
    // is a visible flicker in its own right.
    const drawn = finite.map((s) => s.extent !== null);
    const firstDrawn = drawn.indexOf(true);
    const lastDrawn = drawn.lastIndexOf(true);
    const blanks = firstDrawn < 0 ? [] :
      finite.slice(firstDrawn, lastDrawn + 1).filter((s) => s.extent === null);
    check(`${muteStyle} b${band}: it never blinks out mid-transition`,
      blanks.length === 0,
      blanks.length ? `frame(s) ${blanks.map((s) => s.f).join(",")} draw nothing for `
        + `a band that is drawn before and after -- rg was ${blanks.map(
          (s) => s.rg).join(",")}, deep enough to trip the painter's own `
        + "skip guard" : undefined);

    // B. Nothing reaches the floor of the plot it lives in.
    const deepest = finite.reduce((m, s) =>
      s.bottom !== null && (m === null || s.bottom > m.bottom) ? s : m, null);
    check(`${muteStyle} b${band}: nothing it draws reaches the plot floor`,
      deepest === null || deepest.bottom < PLOT_FLOOR,
      deepest ? `frame ${deepest.f} reaches y=${deepest.bottom.toFixed(1)} against a `
        + `plot floor of ${PLOT_FLOOR.toFixed(1)}` : undefined);

    // D2. It collapsed TO the line, not to somewhere else.
    const last = finite[finite.length - 1];
    check(`${muteStyle} b${band}: the last frame before the sentinel sits on the line`,
      !!last && last.bottom !== null && last.bottom - zeroY <= CHIP_ALLOWANCE + 0.5,
      last && last.bottom !== null
        ? `last finite frame is ${(last.bottom - zeroY).toFixed(1)}px below the zero `
          + `line, past the ${CHIP_ALLOWANCE.toFixed(1)}px the mute chip and its ring occupy`
        : "no finite frame to read");
  }
}

// E. The settled state and the #134 chip routing are untouched.
const settledChecks = {
  "muted bands still normalise to zero height":
    html.includes("const effectiveGains = rg.map((value, index) => Number.isFinite(value) "
      + "? clamp(macroAdjustedGain(value, index), -1.02, 1.02) : 0);"),
  "the mute chip still has one shared box": html.includes("function muteChipRect(i, g) {"),
  "a selected muted band still outlines its chip":
    html.includes('if (G.targetMuted && muteStyle === "cutout") {'),
  "the chip painter still reads the shared box":
    html.includes("const chipBox = muteChipRect(i, g);"),
};
console.log("");
for (const [label, ok] of Object.entries(settledChecks))
  check(`settled state: ${label}`, ok);

// ---------------------------------------------------------------- verdict

const ok = failures.length === 0;
if (!ok) { console.error(""); for (const f of failures) console.error("FAIL: " + f); }
if (expectFail) {
  if (ok) {
    console.error("\nFAIL: the planted document PASSED, so this control proves "
      + "nothing about the check it was meant to exercise");
    process.exit(1);
  }
  console.log("\nOK: the planted document was rejected, as the control requires");
  process.exit(0);
}
if (!ok) process.exit(1);
console.log("\nOK: a muting band shrinks into the zero line under both mute styles, "
  + "never grows, never reaches the plot floor, and still lands on the sentinel");
process.exit(0);
