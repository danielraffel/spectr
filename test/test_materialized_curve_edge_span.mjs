#!/usr/bin/env node
// Every audible RUN of bands gets its own response curve, spanning its own edges.
//
// Two rules, one mechanism:
//
//   * A muted band carries no response, so the curve BREAKS there rather than
//     plunging across it. Each contiguous audible run is its own subpath.
//   * Each run is held FLAT from its own first band's left edge to its own last
//     band's right edge, so it covers exactly the bands it represents.
//
// The second rule subsumes the plot-extreme case: a run beginning at band 0 or
// ending at band N-1 reaches the outer edge of the plot, so neither end is left
// half drawn. The polyline is plotted through band CENTRES, so without it the
// line begins and ends halfway across the first and last band.
//
// The `fft` stair-step already draws exactly this and the user confirmed it
// reads correctly, so it is the REFERENCE here, not a subject: the response
// line was the outlier. In the default `both` visualization the two are painted
// over each other, so a response line bridging a mute the stair-step leaves
// empty is what reads as a line "sticking out past the edge" before dropping
// to 0. That is asserted directly, as a drift guard between the two painters.
//
// There is no layout node for a canvas stroke, so `painted_vs_measured_width`,
// `box_intersection` and `ink_extents` are structurally blind to this the same
// way they are blind to the axis headings. This suite therefore EXECUTES the
// shipping document's own bank block, hands the component a recording 2D
// context, and reads the coordinates the real painter emitted -- including
// whether each coordinate began a new subpath, which is the only way a BREAK
// is observable at all.
//
// Every edge is asserted against the document's OWN band geometry -- its
// `getGeom`, `bandCenterX` and `bandLeftX` source, evaluated here -- never
// against a constant typed into this file. Band count (32/64) and window size
// therefore cannot break the alignment, and the user changes both constantly.
//
// Five painters were examined:
//
//   * `drawMaskResponse`  -- the RESPONSE line. Both rules asserted.
//   * `drawBands`'s iir/hybrid bezier STROKE -- both rules asserted.
//   * `drawBands`'s iir FILL under it -- run break asserted.
//   * `drawBands`'s fft stair-step -- the reference. Asserted to reach the
//     edges and to break at the same bands, as a control that "reaches the
//     edge" and "breaks at a mute" are properties this suite can observe.
//   * `drawSpectrum` (PEAK/AVG) is sampled across the full inner width rather
//     than per band, so bands are not its domain and a muted band does not
//     describe a gap in it. Out of scope by construction.
//
// A SINGLE muted band breaks the curve, not only a run of two or more. The
// stair-step already breaks on one, and in `both` mode a response line that
// bridged a one-band notch the stair-step left empty would show the exact
// inconsistency this exists to remove.
//
// Zoom: band geometry is view-independent by construction -- `getGeom` closes
// over N alone and the bands always tile `inner.w`, while `view.lmin/lmax` only
// remap FREQUENCY onto those fixed columns. That is asserted statically (S1)
// rather than assumed, because if it ever stopped holding, the first/last
// VISIBLE band would stop being band 0 / band N-1 and every span below would be
// measuring the wrong columns.
//
// Usage:
//   node test_materialized_curve_edge_span.mjs <runtime.json>
//        [--plant-response-centres | --plant-overlay-centres
//         | --plant-geometry-follows-view | --plant-response-spans-mutes
//         | --plant-fill-spans-mutes] [--expect-fail]
//
// Each plant restores one pre-fix form, so a failing control names which check
// is load bearing. --expect-fail inverts the verdict: green only when this
// suite REJECTS the planted document. The inversion lives here rather than in a
// generic "any non-zero exit" so a usage error cannot satisfy the control.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantResponse = args.includes("--plant-response-centres");
const plantOverlay = args.includes("--plant-overlay-centres");
const plantGeometry = args.includes("--plant-geometry-follows-view");
const plantResponseMutes = args.includes("--plant-response-spans-mutes");
const plantFillMutes = args.includes("--plant-fill-spans-mutes");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_curve_edge_span.mjs <runtime.json> "
    + "[--plant-...] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const failures = [];
const fail = (message) => failures.push(message);

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

// NOTE ON THE `rendered` LINE IN EVERY PLANT BELOW. It routes through
// `macroAdjustedGain`, matching the shipping painter. That call is NOT part of
// any defect these plants reproduce -- it is carried on both sides of each
// substitution so a plant changes ONLY the geometry under test. Dropping it
// from the `from` side makes the plant match nothing, and the detector then
// exits 2 ("found 0 sites") rather than reporting a pass it cannot prove;
// dropping it from the `to` side would silently revert the macro overlay while
// claiming to test band centres.

if (plantResponse) {
  // The ORIGINAL painter: band centres only, and straight through a mute.
  plant("the response line goes back to band centres",
    "    let inRun = false;\n"
    + "    for (let i = 0; i < N; ++i) {\n"
    + "      if (isMuted(tg[i])) {\n"
    + "        inRun = false;\n"
    + "        continue;\n"
    + "      }\n"
    + "      const rendered = Number.isFinite(rg[i]) ? clamp(macroAdjustedGain(rg[i], i), -1, 1) : 0;\n"
    + "      const y = g.zeroY - rendered * g.halfH;\n"
    + "      const x = bandCenterX(i, g);\n"
    + "      if (!inRun) {\n"
    + "        ctx.moveTo(bandLeftX(i, g), y);\n"
    + "        inRun = true;\n"
    + "      }\n"
    + "      ctx.lineTo(x, y);\n"
    + "      if (i === N - 1 || isMuted(tg[i + 1]))\n"
    + "        ctx.lineTo(bandLeftX(i, g) + g.bandW, y);\n"
    + "    }\n",
    "    for (let i = 0; i < N; ++i) {\n"
    + "      const rendered = Number.isFinite(rg[i]) ? clamp(macroAdjustedGain(rg[i], i), -1, 1) : 0;\n"
    + "      const y = isMuted(tg[i]) ? g.zeroY : g.zeroY - rendered * g.halfH;\n"
    + "      const x = bandCenterX(i, g);\n"
    + "      if (i === 0) ctx.moveTo(x, y);\n"
    + "      else ctx.lineTo(x, y);\n"
    + "    }\n");
}
if (plantResponseMutes) {
  // The painter as it stood after the plot-extreme fix: it reached both outer
  // edges, but still plunged across every interior mute. This is the form the
  // user reported, so a control that cannot reject it proves nothing.
  plant("the response line spans muted bands again",
    "    let inRun = false;\n"
    + "    for (let i = 0; i < N; ++i) {\n"
    + "      if (isMuted(tg[i])) {\n"
    + "        inRun = false;\n"
    + "        continue;\n"
    + "      }\n"
    + "      const rendered = Number.isFinite(rg[i]) ? clamp(macroAdjustedGain(rg[i], i), -1, 1) : 0;\n"
    + "      const y = g.zeroY - rendered * g.halfH;\n"
    + "      const x = bandCenterX(i, g);\n"
    + "      if (!inRun) {\n"
    + "        ctx.moveTo(bandLeftX(i, g), y);\n"
    + "        inRun = true;\n"
    + "      }\n"
    + "      ctx.lineTo(x, y);\n"
    + "      if (i === N - 1 || isMuted(tg[i + 1]))\n"
    + "        ctx.lineTo(bandLeftX(i, g) + g.bandW, y);\n"
    + "    }\n",
    "    for (let i = 0; i < N; ++i) {\n"
    + "      const rendered = Number.isFinite(rg[i]) ? clamp(macroAdjustedGain(rg[i], i), -1, 1) : 0;\n"
    + "      const y = isMuted(tg[i]) ? g.zeroY : g.zeroY - rendered * g.halfH;\n"
    + "      const x = bandCenterX(i, g);\n"
    + "      if (i === 0) {\n"
    + "        ctx.moveTo(bandLeftX(0, g), y);\n"
    + "        ctx.lineTo(x, y);\n"
    + "      } else ctx.lineTo(x, y);\n"
    + "      if (i === N - 1) ctx.lineTo(bandLeftX(i, g) + g.bandW, y);\n"
    + "    }\n");
}
if (plantFillMutes) {
  plant("the iir fill slides under muted bands again",
    "        let filling = false;\n"
    + "        let lastFilled = null;\n"
    + "        const closeFill = () => {\n"
    + "          if (filling && lastFilled) {\n"
    + "            ctx.lineTo(lastFilled.xE, lastFilled.y);\n"
    + "            ctx.lineTo(lastFilled.xE, zeroY);\n"
    + "            ctx.closePath();\n"
    + "          }\n"
    + "          filling = false;\n"
    + "          lastFilled = null;\n"
    + "        };\n"
    + "        for (let i = 0; i < N; i++) {\n"
    + "          const p = pts[i];\n"
    + "          if (!p) {\n"
    + "            closeFill();\n"
    + "            continue;\n"
    + "          }\n"
    + "          if (!filling) {\n"
    + "            ctx.moveTo(p.xL, zeroY);\n"
    + "            ctx.lineTo(p.xL, p.y);\n"
    + "            filling = true;\n"
    + "          }\n"
    + "          ctx.lineTo(p.cx, p.y);\n"
    + "          lastFilled = p;\n"
    + "        }\n"
    + "        closeFill();\n"
    + "        ctx.fill();\n",
    "        let filling = false;\n"
    + "        for (let i = 0; i < N; i++) {\n"
    + "          const p = pts[i];\n"
    + "          if (!p) continue;\n"
    + "          if (!filling) {\n"
    + "            ctx.moveTo(p.xL, zeroY);\n"
    + "            ctx.lineTo(p.xL, p.y);\n"
    + "            filling = true;\n"
    + "          }\n"
    + "          ctx.lineTo(p.cx, p.y);\n"
    + "        }\n"
    + "        if (filling) {\n"
    + "          for (let i = N - 1; i >= 0; i--) {\n"
    + "            const p = pts[i];\n"
    + "            if (!p) continue;\n"
    + "            ctx.lineTo(p.xE, p.y);\n"
    + "            ctx.lineTo(p.xE, zeroY);\n"
    + "            break;\n"
    + "          }\n"
    + "          ctx.closePath();\n"
    + "          ctx.fill();\n"
    + "        }\n");
}
if (plantOverlay) {
  plant("the dsp curve goes back to band centres",
    "          ctx.moveTo(p.xL, p.y);\n          ctx.lineTo(p.cx, p.y);\n",
    "          ctx.moveTo(p.cx, p.y);\n");
  plant("the dsp curve stops closing its run",
    "      }\n      closeRun();\n      ctx.stroke();\n      if (dspM === \"hybrid\") {\n",
    "      }\n      ctx.stroke();\n      if (dspM === \"hybrid\") {\n");
}
if (plantGeometry) {
  plant("band geometry starts following the zoom window",
    "    const bandGap = 2;\n    const bandW = (inner.w - bandGap * (N - 1)) / N;",
    "    const bandGap = 2;\n    const bandW = (inner.w - bandGap * (N - 1)) / N "
    + "* (view.lmax - view.lmin) / (Math.log10(2e4) - Math.log10(20));");
}

// ------------------------------------------------------- positive controls
// A suite that cannot find its subject reports every document as clean.

const scriptBlocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const bankBlock = scriptBlocks.find((b) => b.includes("function FilterBank("));

const controls = {
  "script blocks": scriptBlocks.length,
  "filter bank": html.split("function FilterBank(").length - 1,
  "response painter": html.split("function drawMaskResponse(ctx, g)").length - 1,
  "band painter": html.split("function drawBands(ctx, g)").length - 1,
  "analyzer painter": html.split("function drawSpectrum(ctx, g)").length - 1,
  "band geometry": html.split("const bandLeftX = (i, g) =>").length - 1,
  "geometry source": html.split("const getGeom = useCallback(() => {").length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(20), count);
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length) {
  console.error("FAIL: the payload has no " + blind.join(", ")
    + " -- this suite is reading the wrong document or the editor was "
    + "restructured, so it cannot render a verdict");
  process.exit(2);
}

// ----------------------------------------------------------------- verdict

function verdict() {
  const passed = failures.length === 0;
  for (const f of failures) console.log("FAIL:", f);
  if (passed) {
    console.log("PASS: the response line, the iir/hybrid dsp curve and the iir "
      + "fill each break at every muted band and span each audible run from "
      + "its own first band's left edge to its own last band's right edge, "
      + "reaching the plot edges when the run does.");
  }
  if (expectFail) {
    if (passed) {
      console.error("CONTROL FAIL: the planted pre-fix document was accepted, so "
        + "this suite cannot detect the defect it claims to guard");
      process.exit(1);
    }
    console.log("CONTROL PASS: the planted pre-fix document was rejected.");
    process.exit(0);
  }
  process.exit(passed ? 0 : 1);
}

// ------------------------------------------- the document's own geometry
// Every expected edge below comes from these three expressions, lifted
// verbatim out of the document. Re-typing `pad.l = 56` or a band width here
// would make this suite agree with itself instead of with the painter.

function balancedBody(source, header) {
  const at = source.indexOf(header);
  if (at < 0) return null;
  const open = source.indexOf("{", at + header.length - 1);
  if (open < 0) return null;
  let depth = 0;
  for (let i = open; i < source.length; i++) {
    if (source[i] === "{") depth++;
    else if (source[i] === "}" && --depth === 0) return source.slice(open, i + 1);
  }
  return null;
}

const geomBody = balancedBody(html, "const getGeom = useCallback(() => {");
const lineOf = (prefix) => {
  const at = html.indexOf(prefix);
  if (at < 0) return null;
  return html.slice(at, html.indexOf("\n", at));
};
const bandLeftSrc = lineOf("const bandLeftX = (i, g) =>");
const bandCenterSrc = lineOf("const bandCenterX = (i, g) =>");
if (!geomBody || !bandLeftSrc || !bandCenterSrc) {
  console.error("FAIL: could not lift getGeom / bandLeftX / bandCenterX out of "
    + "the document, so every expected edge below would be a constant this "
    + "file invented");
  process.exit(2);
}

// S1. Band columns must not depend on the zoom window. If they ever do, the
// first/last VISIBLE band stops being band 0 / N-1 and the spans below are
// measuring the wrong columns -- so this is asserted, not assumed.
if (/\bview\s*\./.test(geomBody)) {
  fail("getGeom now reads `view`, so band columns follow the zoom window: the "
    + "first and last VISIBLE band are no longer band 0 and band N-1 and every "
    + "edge span below is measuring the wrong columns");
  // Terminal: every span below is derived from this same expression, so it
  // cannot be evaluated -- and a crash is not a verdict.
  verdict();
}

const geometryFor = (N, width, height) => {
  const wrapRef = { current: { clientWidth: width, clientHeight: height } };
  const ctx = vm.createContext({ Math, Number, Infinity, NaN });
  ctx.wrapRef = wrapRef;
  ctx.N = N;
  const g = vm.runInContext("(() => " + geomBody + ")()", ctx);
  const helpers = vm.runInContext(
    "(() => { " + bandLeftSrc + " " + bandCenterSrc
    + " return { bandLeftX, bandCenterX }; })()", ctx);
  return {
    g,
    left: (i) => helpers.bandLeftX(i, g),
    right: (i) => helpers.bandLeftX(i, g) + g.bandW,
    centre: (i) => helpers.bandCenterX(i, g),
  };
};

// --------------------------------------------------- recording 2D context

const EPS = 1e-9;
const near = (a, b) => Math.abs(a - b) <= EPS;

function recordingContext() {
  const groups = [];
  let current = null;
  const start = () => { current = { ops: [], stroke: null, width: null, fill: null }; };
  start();
  // The op KIND is what makes a BREAK observable: moveTo and lineTo both
  // land a coordinate, and a suite that recorded only coordinates could not
  // tell a curve that skips a muted band from one that paints across it.
  const push = (x, y, op) => current.ops.push({ x, y, op });
  const ctx = {
    strokeStyle: "", fillStyle: "", lineWidth: 1, lineJoin: "", lineCap: "",
    globalCompositeOperation: "", font: "", textAlign: "", textBaseline: "",
    globalAlpha: 1, shadowBlur: 0, shadowColor: "", filter: "",
    save() {}, restore() {}, clip() {}, closePath() {},
    beginPath() { start(); },
    moveTo(x, y) { push(x, y, "move"); },
    lineTo(x, y) { push(x, y, "line"); },
    bezierCurveTo(_a, _b, _c, _d, x, y) { push(x, y, "bezier"); },
    quadraticCurveTo(_a, _b, x, y) { push(x, y, "bezier"); },
    arc() {}, rect() {}, roundRect() {}, ellipse() {},
    stroke() {
      if (current.ops.length) {
        groups.push({ ...current, kind: "stroke", style: ctx.strokeStyle,
                      width: ctx.lineWidth });
      }
      start();
    },
    fill() {
      if (current.ops.length) {
        groups.push({ ...current, kind: "fill", style: ctx.fillStyle,
                      width: ctx.lineWidth });
      }
      start();
    },
    fillRect() {}, strokeRect() {}, clearRect() {}, fillText() {}, strokeText() {},
    measureText: (t) => ({ width: String(t).length * 6, actualBoundingBoxAscent: 6,
                           actualBoundingBoxDescent: 2 }),
    setLineDash() {}, getLineDash: () => [], setTransform() {}, transform() {},
    translate() {}, scale() {}, rotate() {}, drawImage() {},
    createLinearGradient: () => ({ addColorStop() {} }),
    createRadialGradient: () => ({ addColorStop() {} }),
    createPattern: () => null,
    isPointInPath: () => false,
  };
  return { ctx, groups };
}

// -------------------------------------------------------- the sandbox

const hooksFor = () => {
  const state = { hooks: [], cursor: 0, effects: [], cleanups: [] };
  const React = {
    createElement: (type, props, ...children) => ({ type, props, children }),
    memo: (fn) => fn,
    Fragment: "Fragment",
    useState(initial) {
      const i = state.cursor++;
      if (!state.hooks[i]) state.hooks[i] = { value: typeof initial === "function" ? initial() : initial };
      const set = (next) => {
        const resolved = typeof next === "function" ? next(state.hooks[i].value) : next;
        state.hooks[i].value = resolved;
      };
      return [state.hooks[i].value, set];
    },
    useRef(initial) {
      const i = state.cursor++;
      if (!state.hooks[i]) state.hooks[i] = { current: initial };
      return state.hooks[i];
    },
    useMemo(fn, deps) {
      const i = state.cursor++;
      const prev = state.hooks[i];
      const changed = !prev || !prev.deps || !deps || deps.length !== prev.deps.length
        || deps.some((d, j) => d !== prev.deps[j]);
      if (changed) state.hooks[i] = { deps, value: fn() };
      return state.hooks[i].value;
    },
    useCallback(fn, deps) {
      const i = state.cursor++;
      const prev = state.hooks[i];
      const changed = !prev || !prev.deps || !deps || deps.length !== prev.deps.length
        || deps.some((d, j) => d !== prev.deps[j]);
      if (changed) state.hooks[i] = { deps, fn };
      return state.hooks[i].fn;
    },
    useEffect(fn, deps) {
      const i = state.cursor++;
      const prev = state.hooks[i];
      const changed = !prev || !prev.deps || !deps || deps.length !== prev.deps.length
        || deps.some((d, j) => d !== prev.deps[j]);
      state.hooks[i] = { deps };
      if (changed) state.effects.push({ i, fn });
    },
  };
  return { state, React };
};

// Walk the element tree and hand every ref a DOM stub, so `getGeom` has a wrap
// to measure and `renderAll` has canvases to paint into. Without this the
// painter returns at `if (!g) return` and the whole suite passes vacuously --
// which is exactly the false green the controls below exist to catch.
function attachRefs(node, wrap, canvases, seen = new Set()) {
  if (!node || typeof node !== "object" || seen.has(node)) return;
  seen.add(node);
  if (Array.isArray(node)) { for (const c of node) attachRefs(c, wrap, canvases, seen); return; }
  const props = node.props || {};
  if (props.ref && typeof props.ref === "object") {
    if (node.type === "canvas") props.ref.current = canvases.shift() || null;
    else if (props.ref.current === null) props.ref.current = wrap;
  }
  for (const child of node.children || []) attachRefs(child, wrap, canvases, seen);
  if (props.children) attachRefs(props.children, wrap, canvases, seen);
}

function paint({ N, width, height, visualizationMode, dspMode, analyzerMode,
                 mutes = [] }) {
  const { state, React } = hooksFor();
  const main = recordingContext();
  const overlay = recordingContext();
  let rafQueue = [];
  let clock = 1000;
  const sandbox = {
    React, Math, Array, Object, Set, Map, String, Number, Boolean, JSON, Date,
    Error, Promise, Float32Array, Uint8Array, isNaN, isFinite, parseFloat,
    parseInt, Infinity, NaN,
    console: { log() {}, warn() {}, error() {} },
    requestAnimationFrame(fn) { rafQueue.push(fn); return rafQueue.length; },
    cancelAnimationFrame(id) { rafQueue[id - 1] = null; },
    setTimeout() { return 0; }, clearTimeout() {},
    setInterval() { return 0; }, clearInterval() {},
    performance: { now: () => clock },
    devicePixelRatio: 1,
    addEventListener() {}, removeEventListener() {},
    document: {
      getElementById: () => null, querySelector: () => null,
      querySelectorAll: () => [],
      createElement: () => ({ getContext: () => null, style: {} }),
      addEventListener() {}, removeEventListener() {},
    },
  };
  sandbox.globalThis = sandbox;
  sandbox.window = sandbox;
  sandbox.__spectrTestHooks = {};
  sandbox.window.pulp = {
    postMessage() { return Promise.resolve({ ok: true, payload: { ok: true, revision: 1 } }); },
    on() { return () => {}; },
  };
  // The analyzer samples through this; a flat, non-zero field keeps it finite.
  sandbox.window.SpectrAnalyzer = {
    sample: () => -40,
    project: (v, zeroY, halfH) => zeroY - (v + 100) / 100 * halfH,
  };
  sandbox.window.SpectrFreq = { fmt: (v) => String(Math.round(v)) };

  try {
    vm.runInContext(bankBlock, vm.createContext(sandbox), { filename: "bank-block.js" });
  } catch (error) {
    fail(`evaluating the bank's script block threw ${error.constructor.name}: ${error.message}`);
    return null;
  }
  if (typeof sandbox.FilterBank !== "function") {
    fail("FilterBank is not reachable after evaluating its own script block");
    return null;
  }

  const sharedState = { current: null };
  const props = {
    settings: {
      bandCount: N, metaphor: "bar", bloom: 0, spectrumIntensity: 1,
      muteStyle: "badge", motionMode: "live", showMinimap: false,
      showRulers: false, theme: "dark", unmuteOnDraw: false,
    },
    sharedState, dspMode, editMode: "sculpt", analyzerMode,
    visualizationMode, nativeHydrated: true,
    onStateChange() {}, onStatus() {}, onEditModeChange() {}, onNativeState() {},
  };

  const domStub = (extra) => ({
    style: {}, dataset: {}, classList: { add() {}, remove() {}, toggle() {},
      contains: () => false },
    getBoundingClientRect: () => ({ left: 0, top: 0, right: width,
      bottom: height, width, height }),
    setPointerCapture() {}, releasePointerCapture() {},
    setAttribute() {}, removeAttribute() {}, getAttribute: () => null,
    addEventListener() {}, removeEventListener() {},
    appendChild() {}, removeChild() {}, contains: () => false,
    focus() {}, blur() {},
    querySelector: () => null, querySelectorAll: () => [],
    ...extra,
  });
  const wrap = domStub({ clientWidth: width, clientHeight: height });
  const canvasStub = (rec) => domStub({ width, height,
    getContext: () => rec.ctx });

  const effectErrors = [];
  const render = () => {
    state.cursor = 0;
    const element = sandbox.FilterBank(props);
    attachRefs(element, wrap, [canvasStub(main), canvasStub(overlay)]);
    while (state.effects.length) {
      const e = state.effects.shift();
      try {
        if (typeof state.cleanups[e.i] === "function") state.cleanups[e.i]();
        state.cleanups[e.i] = e.fn();
      } catch (error) {
        // A DOM affordance this stub does not model. Recorded, never
        // swallowed: if it were the effect that drives the paint, the groups
        // come back empty and assertSpan refuses to render a verdict.
        effectErrors.push(`${error.constructor.name}: ${error.message}`);
      }
    }
    return element;
  };
  render();
  // Refs are null on the first pass (React attaches them after the tree is
  // built), so the effects that ran above saw no canvas. A second pass runs
  // with the stubs in place.
  render();
  for (let i = 0; i < 3; i++) {
    clock += 16.667;
    const due = rafQueue;
    rafQueue = [];
    for (const fn of due) if (fn) { try { fn(clock); } catch { /* drained */ } }
  }
  const bank = sharedState.current;
  if (mutes.length && !(bank && typeof bank.setGains === "function")) {
    // Silently painting an unmuted field would make every break assertion
    // below vacuous, and "no segment crosses a muted band" is trivially true
    // when no band is muted.
    fail(`the bank exposes no setGains, so the ${mutes.length} requested mute(s) `
      + "were never applied and nothing about a mute boundary was measured");
    return null;
  }
  if (bank && typeof bank.setGains === "function") {
    // A non-flat field: a flat line's endpoints are indistinguishable from a
    // degenerate one, so a flat stimulus could not tell a held endpoint from a
    // dropped one. `-Infinity` is the document's own mute sentinel (`isMuted`).
    const muted = new Set(mutes);
    bank.setGains(new Array(N).fill(0).map((_, i) =>
      muted.has(i) ? -Infinity : Math.sin(i * 0.7) * 0.8));
    render();
    for (let i = 0; i < 12; i++) {
      clock += 16.667;
      const due = rafQueue;
      rafQueue = [];
      for (const fn of due) if (fn) { try { fn(clock); } catch { /* drained */ } }
    }
  }
  return { main, overlay, effectErrors };
}

// The painter's own stroke colour identifies its group. Lifted from the
// document, so a restyle cannot silently make this suite measure grid lines.
const styleIn = (header, which = "strokeStyle") => {
  const body = balancedBody(html, header);
  if (!body) return null;
  const m = body.match(new RegExp(which + ' = "([^"]+)"'));
  return m ? m[1] : null;
};
const responseStyle = styleIn("function drawMaskResponse(ctx, g) {");
if (!responseStyle) {
  console.error("FAIL: drawMaskResponse no longer declares a literal "
    + "strokeStyle, so this suite cannot identify its polyline");
  process.exit(2);
}

const groupsStyled = (rec, style) =>
  rec.groups.filter((gr) => gr.style === style && gr.ops.length > 1);

function assertSpan(label, group, geo, N) {
  if (!group) {
    fail(`${label}: the painter emitted no polyline at all, so nothing was `
      + "measured -- this suite must not report a verdict on it");
    return;
  }
  const xs = group.ops.map((o) => o.x);
  const first = xs[0];
  const last = xs[xs.length - 1];
  const wantL = geo.left(0);
  const wantR = geo.right(N - 1);
  // Stimulus control: a degenerate 2-point path would satisfy the edges
  // trivially while painting nothing in between.
  if (group.ops.length < N) {
    fail(`${label}: only ${group.ops.length} points for ${N} bands, so the `
      + "polyline is not the band curve and its endpoints prove nothing");
    return;
  }
  const ys = new Set(group.ops.map((o) => o.y.toFixed(4)));
  if (ys.size < 3) {
    fail(`${label}: the curve painted ${ys.size} distinct y value(s); a flat `
      + "line cannot distinguish a held endpoint from a dropped one");
  }
  if (!near(first, wantL)) {
    fail(`${label}: first plotted x=${first.toFixed(4)}, band 0 left edge is `
      + `${wantL.toFixed(4)} (band 0 centre is ${geo.centre(0).toFixed(4)}) -- `
      + "the first band is half covered");
  }
  if (!near(last, wantR)) {
    fail(`${label}: last plotted x=${last.toFixed(4)}, band ${N - 1} right edge `
      + `is ${wantR.toFixed(4)} (its centre is ${geo.centre(N - 1).toFixed(4)}) `
      + "-- the last band is half covered");
  }
  console.log(`span      %s N=%s  first=%s (want %s)  last=%s (want %s)  pts=%s`,
    label.padEnd(22), String(N).padEnd(3), first.toFixed(3), wantL.toFixed(3),
    last.toFixed(3), wantR.toFixed(3), group.ops.length);
}

// ------------------------------------------------ runs and subpaths

// The audible runs a mute set implies, derived here rather than restated, so a
// fixture change cannot leave an expectation behind.
function runsOf(N, mutes) {
  const muted = new Set(mutes);
  const runs = [];
  let start = -1;
  for (let i = 0; i < N; i++) {
    if (muted.has(i)) {
      if (start >= 0) { runs.push([start, i - 1]); start = -1; }
    } else if (start < 0) start = i;
  }
  if (start >= 0) runs.push([start, N - 1]);
  return runs;
}

// A `moveTo` opens a new subpath; a `lineTo`/`bezierCurveTo` continues the one
// in progress. Splitting on that is what turns "the painter emitted these
// coordinates" into "the painter drew these disconnected pieces".
function subpathsOf(group) {
  const subs = [];
  for (const op of group.ops) {
    if (op.op === "move" || subs.length === 0) subs.push([]);
    subs[subs.length - 1].push(op);
  }
  return subs;
}

// The user's complaint, stated as a measurement: no painted segment may pass
// over a band that is muted. Reported per crossing so a failure names the band.
function crossingsOverMutes(subs, geo, mutes) {
  const found = [];
  for (const sp of subs) {
    for (let k = 1; k < sp.length; k++) {
      const a = Math.min(sp[k - 1].x, sp[k].x);
      const b = Math.max(sp[k - 1].x, sp[k].x);
      for (const m of mutes) {
        if (b > geo.left(m) + EPS && a < geo.right(m) - EPS)
          found.push({ band: m, a, b });
      }
    }
  }
  return found;
}

function assertRuns(label, group, geo, N, mutes) {
  if (!group) {
    fail(`${label}: the painter emitted no path at all, so nothing was `
      + "measured -- this suite must not report a verdict on it");
    return null;
  }
  const runs = runsOf(N, mutes);
  const subs = subpathsOf(group);
  // Stimulus control. If the mutes never reached the painter there would be a
  // single run, and every assertion below would be about the wrong picture.
  if (runs.length < 2) {
    fail(`${label}: the fixture implies ${runs.length} run(s), so it cannot `
      + "demonstrate a break at all");
    return null;
  }
  if (subs.length !== runs.length) {
    fail(`${label}: ${subs.length} subpath(s) for ${runs.length} audible run(s) `
      + `(muted bands ${mutes.join(",")}) -- the curve does not break where the `
      + "bands do");
    return null;
  }
  for (let k = 0; k < runs.length; k++) {
    const [from, to] = runs[k];
    const xs = subs[k].map((o) => o.x);
    const lo = Math.min(...xs), hi = Math.max(...xs);
    if (!near(lo, geo.left(from)))
      fail(`${label}: run ${k} (bands ${from}..${to}) starts at `
        + `${lo.toFixed(4)}, band ${from}'s left edge is `
        + `${geo.left(from).toFixed(4)} (its centre is `
        + `${geo.centre(from).toFixed(4)})`);
    if (!near(hi, geo.right(to)))
      fail(`${label}: run ${k} (bands ${from}..${to}) ends at `
        + `${hi.toFixed(4)}, band ${to}'s right edge is `
        + `${geo.right(to).toFixed(4)} (its centre is `
        + `${geo.centre(to).toFixed(4)})`);
  }
  const crossings = crossingsOverMutes(subs, geo, mutes);
  for (const c of crossings)
    fail(`${label}: a segment ${c.a.toFixed(2)} -> ${c.b.toFixed(2)} is painted `
      + `over muted band ${c.band} (x ${geo.left(c.band).toFixed(2)} .. `
      + `${geo.right(c.band).toFixed(2)}) -- the curve trails past the edge of `
      + "the audible group instead of stopping there");
  console.log("runs      %s N=%s  subpaths=%s runs=%s  spans %s  crossings=%s",
    label.padEnd(26), String(N).padEnd(3), subs.length, runs.length,
    subs.map((sp) => {
      const xs = sp.map((o) => o.x);
      return `${Math.min(...xs).toFixed(1)}..${Math.max(...xs).toFixed(1)}`;
    }).join(" "), crossings.length);
  return subs;
}

// ------------------------------------------ mute boundaries (the defect)
//
// Three fixtures, each answering a question the others cannot:
//
//   interior   a run of two muted bands mid-spectrum AND a lone muted band --
//              the user's screenshots show a run of two, and a single-band
//              notch is the case a "only break on 2+" rule would get wrong.
//   edges      band 0 and band N-1 muted -- proves the run rule generalises
//              the plot-extreme hold rather than sitting beside a special case
//              that would now paint a phantom endpoint over a muted band.
//   single     exactly one muted band, so the decision to break on one is
//              asserted on its own rather than riding along with a run.
const MUTE_FIXTURES = [
  ["interior", (N) => [Math.round(N * 0.375), Math.round(N * 0.375) + 1,
                       Math.round(N * 0.625)]],
  ["edges", (N) => [0, N - 1, Math.round(N / 2)]],
  ["single", (N) => [Math.round(N * 0.4)]],
];

for (const [N, width, height] of [[32, 990, 645], [64, 990, 645]]) {
  const geo = geometryFor(N, width, height);
  for (const [fixture, pick] of MUTE_FIXTURES) {
    const mutes = [...new Set(pick(N))].sort((a, b) => a - b);

    // `both` is the shipping default (`useAppS("both")`) and the mode the user
    // reported: the stair-step and the response line are painted over each
    // other, so this is where a response line bridging a mute the stair-step
    // leaves empty is visible.
    const both = paint({ N, width, height, visualizationMode: "both",
                         dspMode: "fft", analyzerMode: "off", mutes });
    if (!both) continue;

    const resp = groupsStyled(both.main, responseStyle);
    const respSubs = assertRuns(`response/${fixture} ${width}x${height}`,
                                resp[resp.length - 1], geo, N, mutes);

    // Drift guard. The fft stair-step is the reference the user confirmed
    // reads correctly; the two are drawn over each other, so they must break
    // at the SAME bands or the picture contradicts itself. Its steps end one
    // inter-band gap past the band (xR joins adjacent steps), so the right
    // edge is asserted as a lower bound rather than an equality.
    const step = groupsStyled(both.main, "rgba(210,225,245,0.70)");
    const stepGroup = step[step.length - 1];
    if (!stepGroup) {
      fail(`fft reference/${fixture} ${width}x${height}: the stair-step painted `
        + "nothing, so the break this suite compares against is unproven");
    } else if (respSubs) {
      const stepSubs = subpathsOf(stepGroup);
      const runs = runsOf(N, mutes);
      if (stepSubs.length !== runs.length) {
        fail(`fft reference/${fixture} ${width}x${height}: ${stepSubs.length} `
          + `subpath(s) for ${runs.length} run(s) -- the reference painter no `
          + "longer breaks at mutes, so it cannot anchor this comparison");
      } else {
        for (let k = 0; k < runs.length; k++) {
          const rx = respSubs[k].map((o) => o.x);
          const sx = stepSubs[k].map((o) => o.x);
          if (!near(Math.min(...rx), Math.min(...sx)))
            fail(`drift/${fixture} ${width}x${height}: run ${k} starts at `
              + `${Math.min(...rx).toFixed(4)} on the response line but `
              + `${Math.min(...sx).toFixed(4)} on the fft stair-step`);
          if (Math.max(...sx) < Math.max(...rx) - EPS)
            fail(`drift/${fixture} ${width}x${height}: run ${k} ends at `
              + `${Math.max(...rx).toFixed(4)} on the response line, past the `
              + `stair-step's ${Math.max(...sx).toFixed(4)}`);
        }
        console.log(`drift     %s runs agree`,
          `${fixture} ${width}x${height} N=${N}`.padEnd(26));
      }
    }

    // The iir overlay: its bezier STROKE and the FILL underneath it. The fill
    // is a separate path that used to slide under bands its own stroke skips.
    const bars = paint({ N, width, height, visualizationMode: "bars",
                         dspMode: "iir", analyzerMode: "off", mutes });
    if (!bars) continue;
    const iir = groupsStyled(bars.main, "rgba(200,230,255,0.55)");
    assertRuns(`dsp-iir/${fixture} ${width}x${height}`, iir[iir.length - 1],
               geo, N, mutes);
    const fills = bars.main.groups.filter(
      (gr) => gr.kind === "fill" && gr.style === "rgba(180,210,255,0.05)");
    const fillGroup = fills[fills.length - 1];
    if (!fillGroup) {
      fail(`iir fill/${fixture} ${width}x${height}: no fill group with the iir `
        + "fill style -- the fill was restyled and this check is now blind");
    } else {
      const fillSubs = subpathsOf(fillGroup);
      const runs = runsOf(N, mutes);
      if (fillSubs.length !== runs.length)
        fail(`iir fill/${fixture} ${width}x${height}: ${fillSubs.length} `
          + `polygon(s) for ${runs.length} audible run(s) -- the fill does not `
          + "break where its own stroke does");
      const crossings = crossingsOverMutes(fillSubs, geo, mutes)
        // The vertical closing edges at a run's own boundary touch that
        // boundary; only a segment with real horizontal extent over a muted
        // band is ink the band should not carry.
        .filter((c) => c.b - c.a > EPS);
      for (const c of crossings)
        fail(`iir fill/${fixture} ${width}x${height}: filled across muted band `
          + `${c.band} (${c.a.toFixed(2)} -> ${c.b.toFixed(2)})`);
      console.log("fill      %s polygons=%s runs=%s crossings=%s",
        `${fixture} ${width}x${height} N=${N}`.padEnd(26), fillSubs.length,
        runs.length, crossings.length);
    }
  }
}

// A plot the painter never filled would make every span above vacuous, so the
// geometry is cross-checked against the plot box the document itself computes.
for (const [N, width, height] of [[32, 990, 645], [64, 990, 645], [32, 1440, 900]]) {
  const geo = geometryFor(N, width, height);
  if (!near(geo.left(0), geo.g.inner.x))
    fail(`geometry: band 0 left edge ${geo.left(0)} != inner.x ${geo.g.inner.x}`);
  if (!near(geo.right(N - 1), geo.g.inner.x + geo.g.inner.w))
    fail(`geometry: band ${N - 1} right edge ${geo.right(N - 1)} != inner right `
      + `${geo.g.inner.x + geo.g.inner.w}`);

  // --- the RESPONSE line, with the bars painter switched off so the only
  // curve in the main context is the one under test.
  const response = paint({ N, width, height, visualizationMode: "response",
                           dspMode: "iir", analyzerMode: "off" });
  if (response) {
    const found = groupsStyled(response.main, responseStyle);
    assertSpan(`response ${width}x${height}`, found[found.length - 1], geo, N);
  }

  // --- the iir DSP overlay inside drawBands.
  const overlayStyle = "rgba(200,230,255,0.55)";
  const bars = paint({ N, width, height, visualizationMode: "bars",
                       dspMode: "iir", analyzerMode: "off" });
  if (bars) {
    const found = groupsStyled(bars.main, overlayStyle);
    if (!found.length) {
      fail(`dsp overlay ${width}x${height}: no stroke group with the iir curve `
        + "style -- the overlay was restyled and this check is now blind");
    } else {
      assertSpan(`dsp-iir ${width}x${height}`, found[found.length - 1], geo, N);
    }
  }

  // --- controls: the two painters that already reach the edges. If either
  // regressed, "reaching the edge" would stop being observable here at all.
  const fft = paint({ N, width, height, visualizationMode: "bars",
                      dspMode: "fft", analyzerMode: "off" });
  if (fft) {
    const found = groupsStyled(fft.main, "rgba(210,225,245,0.70)");
    const step = found[found.length - 1];
    if (!step) {
      fail(`fft control ${width}x${height}: the stair-step overlay painted `
        + "nothing, so this suite's notion of an edge-reaching curve is unproven");
    } else {
      const xs = step.ops.map((o) => o.x);
      if (!near(xs[0], geo.left(0)))
        fail(`fft control ${width}x${height}: starts at ${xs[0]}, expected `
          + `${geo.left(0)} -- the control that proves an edge-reaching painter `
          + "is observable here has itself regressed");
      if (xs[xs.length - 1] < geo.right(N - 1) - EPS)
        fail(`fft control ${width}x${height}: ends at ${xs[xs.length - 1]}, `
          + `expected at least ${geo.right(N - 1)}`);
      console.log(`control   fft step %s  first=%s last=%s`,
        `${width}x${height} N=${N}`.padEnd(16), xs[0].toFixed(3),
        xs[xs.length - 1].toFixed(3));
    }
  }
}

verdict();
