#!/usr/bin/env node
// The response curve must cover the WHOLE first and last band, not half of each.
//
// The polyline is plotted through band CENTRES. Between bands that is right --
// each segment spans centre-to-centre and the bands tile continuously -- but at
// the two extremes the line begins and ends halfway across the first and last
// band, so each end of the plot carries a visibly half-drawn band.
//
// There is no layout node for a canvas stroke, so `painted_vs_measured_width`,
// `box_intersection` and `ink_extents` are structurally blind to this the same
// way they are blind to the axis headings. This suite therefore EXECUTES the
// shipping document's own bank block, hands the component a recording 2D
// context, and reads the coordinates the real painter emitted.
//
// Both edges are asserted against the document's OWN band geometry -- its
// `getGeom`, `bandCenterX` and `bandLeftX` source, evaluated here -- never
// against a constant typed into this file. Band count (32/64) and window size
// therefore cannot break the alignment, and the user changes both constantly.
//
// Three painters were examined. Two share the defect and are asserted here:
//
//   * `drawMaskResponse`  -- the RESPONSE line. Asserted at N=32 and N=64.
//   * `drawBands`'s iir/hybrid bezier overlay -- a curve through `p.cx`.
//     Asserted at N=32 and N=64.
//   * `drawBands`'s fft overlay already steps xL -> xR per band, and
//     `drawSpectrum` (PEAK/AVG) is already sampled across the full inner
//     width. Both are asserted to still reach the edges, as a control that
//     "reaches the edge" is a property this suite can actually observe.
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
//         | --plant-geometry-follows-view] [--expect-fail]
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

if (plantResponse) {
  plant("the response line goes back to band centres",
    "      if (i === 0) {\n"
    + "        ctx.moveTo(bandLeftX(0, g), y);\n"
    + "        ctx.lineTo(x, y);\n"
    + "      } else ctx.lineTo(x, y);\n"
    + "      if (i === N - 1) ctx.lineTo(bandLeftX(i, g) + g.bandW, y);\n",
    "      if (i === 0) ctx.moveTo(x, y);\n      else ctx.lineTo(x, y);\n");
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
    console.log("PASS: the response line and the iir/hybrid dsp curve each span "
      + "from the first band's left edge to the last band's right edge.");
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
  const push = (x, y) => current.ops.push({ x, y });
  const ctx = {
    strokeStyle: "", fillStyle: "", lineWidth: 1, lineJoin: "", lineCap: "",
    globalCompositeOperation: "", font: "", textAlign: "", textBaseline: "",
    globalAlpha: 1, shadowBlur: 0, shadowColor: "", filter: "",
    save() {}, restore() {}, clip() {}, closePath() {},
    beginPath() { start(); },
    moveTo(x, y) { push(x, y); },
    lineTo(x, y) { push(x, y); },
    bezierCurveTo(_a, _b, _c, _d, x, y) { push(x, y); },
    quadraticCurveTo(_a, _b, x, y) { push(x, y); },
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

function paint({ N, width, height, visualizationMode, dspMode, analyzerMode }) {
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
  if (bank && typeof bank.setGains === "function") {
    // A non-flat field: a flat line's endpoints are indistinguishable from a
    // degenerate one, so a flat stimulus could not tell a held endpoint from a
    // dropped one.
    bank.setGains(new Array(N).fill(0).map((_, i) => Math.sin(i * 0.7) * 0.8));
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
