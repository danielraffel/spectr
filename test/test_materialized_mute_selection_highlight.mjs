#!/usr/bin/env node
// A SELECTED MUTED BAND MARKS ITS BUTTON, NOT THE GLYPH INSIDE IT.
//
// Reported from the installed build: select a run of bands, press `m`, and the
// selection mark reads as "a small bright box around an icon sitting inside a
// larger unhighlighted button". The screenshot shows four adjacent bands, two
// muted and two not, and the muted ones carry a bright sliver straight across
// the speaker glyph in the middle of an otherwise unmarked chip.
//
// It is not a styling choice anywhere. `drawBands` outlines the band BODY, and
// a muted band has no body: its painted value is the `-Infinity` sentinel,
// which the painter's own normalisation
//
//     const effectiveGains = rg.map((v) => Number.isFinite(v) ? clamp(v, -1.02, 1.02) : 0)
//
// turns into ZERO -- not into -1.02. So `topY` and `botY` are both `zeroY`, the
// rect degenerates to its two 2.5px outsets and nothing between them, and that
// 5px sliver lands on the zero line, which is exactly where the mute chip is
// centred and exactly where its speaker glyph is drawn.
//
// THE FOUR THINGS THIS MEASURES, all against the real painter:
//
//   A  a selected muted band's mark CONTAINS its mute chip. Not "is taller
//      than 5px" -- containment, so a mark that grew but sat somewhere else
//      still fails.
//   B  an UNSELECTED muted band carries no mark at all. This is the user's
//      own second ask, and it is the failure mode any "style all the mute
//      buttons" fix would have shipped.
//   C  a selected UNMUTED band still gets its body rect, at the body's
//      geometry. The branch must not have eaten the ordinary case.
//   D  the mark is painted in a VISIBLE colour, asserted from the document's
//      own source rather than assumed.
//
// D is not padding. The instrument below records DRAWING COMMANDS, so its
// geometric detection floor is 0px -- it cannot miss a coordinate change of
// any size, unlike a raster diff. What it cannot see is compositing: a stroke
// issued in `rgba(255,255,255,0)`, or under `globalAlpha = 0`, is recorded
// identically to a visible one. So the colour is asserted explicitly and
// `--plant-transparent` proves that assertion is load bearing.
//
// Usage:
//   node test_materialized_mute_selection_highlight.mjs <runtime.json>
//        [--plant-body-rect | --plant-sliver | --plant-unconditional-ring
//         | --plant-transparent] [--expect-fail] [--report]
//
// Each plant installs a DIFFERENT wrong painter, so a failing control names
// which check is load bearing. --expect-fail inverts the verdict: the row is
// green only when this suite REJECTS the planted document, and red when the
// plant fails to apply or anything else goes wrong. The inversion lives here
// rather than in WILL_FAIL because WILL_FAIL accepts any non-zero exit -- a
// usage error satisfied it and proved nothing.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantBodyRect = args.includes("--plant-body-rect");
const plantSliver = args.includes("--plant-sliver");
const plantUnconditional = args.includes("--plant-unconditional-ring");
const plantTransparent = args.includes("--plant-transparent");
const expectFail = args.includes("--expect-fail");
const report = args.includes("--report");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_mute_selection_highlight.mjs "
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

const SEL_BRANCH_OPEN =
  '        if (G.targetMuted && muteStyle === "cutout") {\n'
  + '          const sel = muteChipRect(i, g);\n'
  + '          ctx.fillStyle = "rgba(255,255,255,0.12)";\n'
  + '          roundRect(ctx, sel.x, sel.y, sel.w, sel.h, 3);\n'
  + '          ctx.fill();\n'
  + '          roundRect(ctx, sel.x - 2.5, sel.y - 2.5, sel.w + 5, sel.h + 5, 5);\n'
  + '          ctx.stroke();\n'
  + '        } else {\n';
const BODY_RECT_INDENTED =
  '          ctx.strokeRect(\n'
  + '            Math.round(G.cx - G.innerW / 2) - 2.5,\n'
  + '            Math.round(Math.min(G.topY, G.botY)) - 2.5,\n'
  + '            Math.round(G.innerW) + 5,\n'
  + '            Math.round(Math.abs(G.botY - G.topY)) + 5\n'
  + '          );\n'
  + '        }\n';
const BODY_RECT_FLAT =
  '        ctx.strokeRect(\n'
  + '          Math.round(G.cx - G.innerW / 2) - 2.5,\n'
  + '          Math.round(Math.min(G.topY, G.botY)) - 2.5,\n'
  + '          Math.round(G.innerW) + 5,\n'
  + '          Math.round(Math.abs(G.botY - G.topY)) + 5\n'
  + '        );\n';

if (plantBodyRect) {
  // The shipped defect, exactly: one unconditional body rect, which on a muted
  // band is the 5px sliver over the speaker glyph.
  plant("the selection goes back to outlining the (absent) band body",
    SEL_BRANCH_OPEN + BODY_RECT_INDENTED, BODY_RECT_FLAT);
}

if (plantSliver) {
  // The branch is present and the mark is still glyph sized. This separates
  // "the code has an if" from "the mark covers the button" -- a suite that
  // only looked for the branch would pass this.
  plant("the ring keeps the branch but stays the size of the glyph",
    "          roundRect(ctx, sel.x - 2.5, sel.y - 2.5, sel.w + 5, sel.h + 5, 5);",
    "          roundRect(ctx, sel.x - 2.5, g.zeroY - 2.5, sel.w + 5, 5, 5);");
}

if (plantUnconditional) {
  // The plausible over-fix: style every mute button, so an unselected muted
  // neighbour is indistinguishable from a selected one.
  plant("every muted band gets the ring, selected or not",
    "      if (G.isSel) {\n"
    + '        ctx.strokeStyle = "rgba(255,255,255,0.85)";',
    "      if (G.isSel || (G.targetMuted && muteStyle === \"cutout\")) {\n"
    + '        ctx.strokeStyle = "rgba(255,255,255,0.85)";');
}

if (plantTransparent) {
  // Geometry identical, ink absent. The check that rejects this is the only
  // one standing between this suite and its own blind spot.
  plant("the mark is painted in a fully transparent white",
    '      if (G.isSel) {\n        ctx.strokeStyle = "rgba(255,255,255,0.85)";',
    '      if (G.isSel) {\n        ctx.strokeStyle = "rgba(255,255,255,0)";');
  plant("the tint is fully transparent too",
    '          ctx.fillStyle = "rgba(255,255,255,0.12)";',
    '          ctx.fillStyle = "rgba(255,255,255,0)";');
}

const failures = [];
const fail = (msg) => failures.push(msg);
const check = (label, ok, detail) => {
  console.log("%s  %s", ok ? "ok   " : "FAIL ", label);
  if (!ok) fail(detail ? `${label} -- ${detail}` : label);
};

// ------------------------------------------------------- positive controls
// A suite that cannot find its subject reports every document as clean.

const scriptBlocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const bankBlock = scriptBlocks.find((b) => b.includes("function FilterBank("));

const controls = {
  "filter bank": html.split("function FilterBank(").length - 1,
  "band painter": html.split("function drawBands(ctx, g) {").length - 1,
  "cutout chip loop": html.split('if (muteStyle === "cutout") {').length - 1,
  "chip body paint": html.split('ctx.fillStyle = "rgba(18,22,28,0.92)";').length - 1,
  // A PREFIX, deliberately. `--plant-unconditional-ring` widens this very
  // condition, and a control that matched the whole `if (G.isSel) {` would
  // read zero on the planted document and abort with NO VERDICT before the
  // check the plant exists to exercise ever ran.
  "selection branch": html.split("if (G.isSel").length - 1,
  "mute sentinel": html.split("rg[i] = smooth(rg[i], -1.02, dt * 26)").length - 1,
  "marquee press": html.split('mode: "marquee",').length - 1,
  "selection readback": html.split(
    "selection: Array.from(selection).sort((a, b) => a - b)").length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(22), count);
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length || !bankBlock) {
  console.error("FAIL: the payload has no " + (blind.join(", ") || "bank block")
    + " -- this suite is reading the wrong document or the painter was "
    + "restructured, so it cannot render a verdict");
  process.exit(2);
}

// The colours are lifted from the document rather than restated here, so a
// restyle cannot leave this suite measuring a mark that no longer exists while
// reporting the one it remembers.
const styleAfter = (anchor, prop) => {
  const at = html.indexOf(anchor);
  if (at < 0) return null;
  const m = html.slice(at, at + 4000).match(
    new RegExp(`ctx\\.${prop}\\s*=\\s*"([^"]+)"`));
  return m ? m[1] : null;
};
const SELECTION_STROKE = styleAfter("      if (G.isSel", "strokeStyle");
const CHIP_FILL = "rgba(18,22,28,0.92)";
if (!SELECTION_STROKE) {
  console.error("FAIL: the selection branch declares no strokeStyle, so this "
    + "suite cannot tell its mark from any other stroke on the canvas");
  process.exit(2);
}
console.log("control   %s %s", "selection stroke".padEnd(22), SELECTION_STROKE);

// A colour with no ink. `rgba(r,g,b,0)`, `transparent`, and anything the
// painter could issue that paints nothing.
const invisible = (style) => {
  if (typeof style !== "string") return true;
  if (style === "transparent") return true;
  const m = style.match(/rgba?\(([^)]*)\)/);
  if (!m) return false;
  const parts = m[1].split(",").map((p) => parseFloat(p.trim()));
  return parts.length >= 4 && !(parts[3] > 0);
};

// --------------------------------------------------------- the instrument
//
// A recording 2D context. Every path op lands a coordinate, and `stroke()` /
// `fill()` close the group and record its BOUNDING BOX together with the
// colour and width in force. `strokeRect` and `fillRect` are groups of their
// own. This is the drawing command stream, so its geometric floor is 0px --
// there is no sampling and no anti-aliased boundary row to argue about, which
// is why the assertions below are containment rather than a pixel tolerance.

function recordingContext() {
  const groups = [];
  let pts = [];
  const push = (x, y) => { if (Number.isFinite(x) && Number.isFinite(y)) pts.push([x, y]); };
  const box = (list) => {
    const xs = list.map((p) => p[0]), ys = list.map((p) => p[1]);
    return { x: Math.min(...xs), y: Math.min(...ys),
             w: Math.max(...xs) - Math.min(...xs),
             h: Math.max(...ys) - Math.min(...ys) };
  };
  const close = (kind, style) => {
    if (pts.length) groups.push({ kind, style, width: ctx.lineWidth, ...box(pts) });
    pts = [];
  };
  const ctx = {
    strokeStyle: "", fillStyle: "", lineWidth: 1, lineJoin: "", lineCap: "",
    globalCompositeOperation: "", font: "", textAlign: "", textBaseline: "",
    globalAlpha: 1, shadowBlur: 0, shadowColor: "", filter: "",
    save() {}, restore() {}, clip() { pts = []; }, closePath() {},
    beginPath() { pts = []; },
    moveTo(x, y) { push(x, y); },
    lineTo(x, y) { push(x, y); },
    arcTo(x1, y1, x2, y2) { push(x1, y1); push(x2, y2); },
    bezierCurveTo(_a, _b, _c, _d, x, y) { push(x, y); },
    quadraticCurveTo(_a, _b, x, y) { push(x, y); },
    arc(x, y, r) { push(x - r, y - r); push(x + r, y + r); },
    ellipse() {}, rect(x, y, w, h) { push(x, y); push(x + w, y + h); },
    stroke() { close("stroke", ctx.strokeStyle); },
    fill() { close("fill", ctx.fillStyle); },
    strokeRect(x, y, w, h) {
      groups.push({ kind: "strokeRect", style: ctx.strokeStyle,
                    width: ctx.lineWidth, x, y, w, h });
    },
    fillRect(x, y, w, h) {
      groups.push({ kind: "fillRect", style: ctx.fillStyle,
                    width: ctx.lineWidth, x, y, w, h });
    },
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

// ------------------------------------------------------------ the sandbox

const N = 24;
const WIDTH = 1000;
const HEIGHT = 600;
const MUTED = [5, 6, 13, 14];     // two pairs, so a selected pair has an
const SELECT = [4, 5, 6, 7];      // unselected muted pair to be told apart from

let rafQueue = [];
let clock = 1000;
const hooks = [];
let cursor = 0;
const effects = [];
const cleanups = [];

const React = {
  createElement: (type, props, ...children) => ({ type, props, children }),
  memo: (fn) => fn,
  Fragment: "Fragment",
  useState(initial) {
    const i = cursor++;
    if (!hooks[i]) hooks[i] = { value: typeof initial === "function" ? initial() : initial };
    return [hooks[i].value, (next) => {
      hooks[i].value = typeof next === "function" ? next(hooks[i].value) : next;
    }];
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

const main = recordingContext();
const overlay = recordingContext();

const domStub = (extra) => ({
  style: {}, dataset: {},
  classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
  getBoundingClientRect: () => ({ left: 0, top: 0, right: WIDTH, bottom: HEIGHT,
                                  width: WIDTH, height: HEIGHT }),
  setPointerCapture() {}, releasePointerCapture() {},
  setAttribute() {}, removeAttribute() {}, getAttribute: () => null,
  addEventListener() {}, removeEventListener() {},
  appendChild() {}, removeChild() {}, contains: () => false,
  focus() {}, blur() {}, querySelector: () => null, querySelectorAll: () => [],
  ...extra,
});
const wrapElement = domStub({ clientWidth: WIDTH, clientHeight: HEIGHT });
const canvasStub = (rec) => domStub({ width: WIDTH, height: HEIGHT,
                                      getContext: () => rec.ctx });

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
    getElementById: () => null, querySelector: () => null, querySelectorAll: () => [],
    createElement: () => ({ getContext: () => null, style: {} }),
    addEventListener() {}, removeEventListener() {},
  },
};
sandbox.globalThis = sandbox;
sandbox.window = sandbox;
sandbox.__spectrTestHooks = {};
sandbox.window.SpectrAnalyzer = {
  sample: () => -40,
  project: (v, zeroY, halfH) => zeroY - (v + 100) / 100 * halfH,
};
sandbox.window.SpectrFreq = { fmt: (hz) => String(Math.round(hz)) };
sandbox.window.Spectr = { FACTORY_PATTERNS: [], resolveGains: () => [] };
sandbox.window.pulp = {
  postMessage() { return Promise.resolve({ ok: true, payload: { ok: true, revision: 1 } }); },
  on() { return () => {}; },
};

// `cutout` is the SHIPPED DEFAULT and the style that paints the chip at all.
// The sibling suites run `badge`, under which no chip exists and every
// assertion here would be vacuous.
const settings = {
  bandCount: N, metaphor: "bar", bloom: 0, spectrumIntensity: 0,
  muteStyle: "cutout", motionMode: "live", showMinimap: false,
  showRulers: false, theme: "dark", unmuteOnDraw: false,
};
const sharedState = { current: null };
const props = {
  settings, sharedState, dspMode: "fft", editMode: "sculpt",
  analyzerMode: "off", visualizationMode: "bars", nativeHydrated: true,
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
  if (Array.isArray(node)) {
    for (const c of node) { const hit = findSurface(c); if (hit) return hit; }
    return null;
  }
  if (node.props && node.props["data-spectr-filter-surface"]) return node;
  for (const child of node.children || []) {
    const hit = findSurface(child);
    if (hit) return hit;
  }
  return findSurface((node.props || {}).children);
};
const effectErrors = [];
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
    } catch (error) {
      effectErrors.push(`${error.constructor.name}: ${error.message}`);
    }
  }
  return element;
};
const frame = (n = 1) => {
  for (let i = 0; i < n; i++) {
    clock += 16.667;
    const due = rafQueue;
    rafQueue = [];
    for (const fn of due) if (fn) { try { fn(clock); } catch { /* drained */ } }
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
render();       // second pass: refs are attached, so getGeom() resolves
frame(2);

if (!sandbox.__spectrTestHooks.renderState) {
  console.error("FAIL: the bank did not install its renderState test hook");
  process.exit(2);
}
if (!surface) {
  console.error("FAIL: no [data-spectr-filter-surface] in the tree, so the "
    + "marquee has nothing to drive");
  process.exit(2);
}

// --------------------------------------------------------------- geometry
// Derived the way the editor derives it, so the drag lands where a person's
// drag would and the expected chip box is independent of the painter.

const PAD = { l: 56, r: 56, t: 70, b: 120 };
const inner = { x: PAD.l, y: PAD.t, w: WIDTH - PAD.l - PAD.r, h: HEIGHT - PAD.t - PAD.b };
const zeroY = inner.y + inner.h * 0.55;
const bandGap = 2;
const bandW = (inner.w - bandGap * (N - 1)) / N;
const centre = (i) => inner.x + i * (bandW + bandGap) + bandW / 2;
const PLOT_Y = inner.y + inner.h * 0.4;
const expectedChip = (i) => {
  const h = Math.min(26, Math.max(18, inner.h * 0.12));
  return { x: inner.x + i * (bandW + bandGap) + 0.5, y: zeroY - h / 2,
           w: bandW - 1, h };
};

const selectionOf = () => sandbox.__spectrTestHooks.renderState().selection;
const event = (x, y, mods) => ({
  clientX: x, clientY: y, pointerId: 1, button: 0,
  shiftKey: !!(mods && mods.shift), altKey: false,
  metaKey: !!(mods && mods.meta), ctrlKey: !!(mods && mods.ctrl),
  preventDefault() {}, stopPropagation() {},
});
const marquee = (fromBand, toBand, samples = 6) => {
  const x0 = centre(fromBand) - bandW / 2 - 1;
  const x1 = centre(toBand) + bandW / 2 + 1;
  surface.props.onPointerDown(event(x0, PLOT_Y, { meta: true }));
  render();
  for (let s = 1; s <= samples; s++) {
    surface.props.onPointerMove(event(x0 + (x1 - x0) * (s / samples), PLOT_Y, { meta: true }));
    render();
  }
  surface.props.onPointerUp(event(x1, PLOT_Y, { meta: true }));
  render();
  frame();
  return selectionOf();
};

// ---------------------------------------------------------- the stimulus

const bank = sharedState.current;
if (!bank || typeof bank.setGains !== "function") {
  console.error("FAIL: the bank exposes no setGains, so nothing was ever muted "
    + "and every assertion below would be vacuous");
  process.exit(2);
}
// `-Infinity` is the document's own mute sentinel (`isMuted`). The unmuted
// bands are given a real gain so their body rect is non-degenerate and check C
// can tell a body from a sliver.
const mutedSet = new Set(MUTED);
bank.setGains(new Array(N).fill(0).map((_, i) =>
  mutedSet.has(i) ? -Infinity : 0.45 + 0.2 * Math.sin(i)));
render();
// Long enough for the collapse ramp to reach the sentinel: a band still
// mid-ramp is finite, and this suite would then be measuring a body that is
// about to disappear rather than the muted steady state.
frame(40);

const painted = sandbox.__spectrTestHooks.renderState().gains;
const stillFinite = MUTED.filter((i) => Number.isFinite(painted[i]));
if (stillFinite.length) {
  console.error(`FAIL: band(s) ${stillFinite.join(",")} never reached the mute `
    + "sentinel, so the muted steady state was never painted");
  process.exit(2);
}
console.log("stimulus  muted [%s] reached the -Infinity sentinel", MUTED.join(","));

const selected = marquee(SELECT[0], SELECT[SELECT.length - 1]);
if (!SELECT.every((i) => selected.includes(i)) || selected.length === 0) {
  console.error(`FAIL: the marquee selected [${selected}], expected to cover `
    + `[${SELECT}] -- the stimulus never reached the handler, so "no mark" and `
    + '"no selection" are the same empty set');
  process.exit(2);
}
console.log("stimulus  marquee selected [%s]", selected.join(","));

// One clean frame, recorded. Everything before this is setup noise.
main.reset();
overlay.reset();
frame(1);
if (main.groups.length === 0) {
  console.error("FAIL: the painter issued no drawing commands on the measured "
    + `frame (effect errors: ${effectErrors.join("; ") || "none"})`);
  process.exit(2);
}
console.log("stimulus  %d drawing groups recorded on the measured frame",
  main.groups.length);

// ------------------------------------------------------------ the readings

const overlaps = (a, b) => a.x < b.x + b.w && b.x < a.x + a.w
  && a.y < b.y + b.h && b.y < a.y + a.h;
const contains = (outer, innerBox) =>
  outer.x <= innerBox.x + 1e-6 && outer.y <= innerBox.y + 1e-6
  && outer.x + outer.w >= innerBox.x + innerBox.w - 1e-6
  && outer.y + outer.h >= innerBox.y + innerBox.h - 1e-6;

const selMarks = main.groups.filter((g) => g.style === SELECTION_STROKE
  && (g.kind === "stroke" || g.kind === "strokeRect"));
const chipFills = main.groups.filter((g) => g.style === CHIP_FILL);
const markFor = (i) => selMarks.filter((m) => overlaps(m, expectedChip(i)));

console.log("reading   selection marks=%d, chip fills=%d",
  selMarks.length, chipFills.length);

// The chip painter is the control for every chip-relative reading below.
check(`the painter drew a chip for each of the ${MUTED.length} muted bands`,
  chipFills.length === MUTED.length,
  `found ${chipFills.length} chips in ${CHIP_FILL}`);
for (const i of MUTED) {
  const want = expectedChip(i);
  const got = chipFills.find((c) => overlaps(c, want));
  if (!got) { fail(`no mute chip painted over band ${i}`); continue; }
  if (Math.abs(got.h - want.h) > 0.01 || Math.abs(got.w - want.w) > 0.01)
    fail(`band ${i}'s chip is ${got.w.toFixed(3)}x${got.h.toFixed(3)}, `
      + `expected ${want.w.toFixed(3)}x${want.h.toFixed(3)} -- this suite's `
      + "idea of the chip box has drifted from the painter's");
}

if (report) {
  for (const i of [...SELECT, ...MUTED].sort((a, b) => a - b)) {
    const c = expectedChip(i);
    const m = markFor(i)[0];
    const fmt = (b) => `${b.w.toFixed(3)}x${b.h.toFixed(3)}@(${b.x.toFixed(3)},${b.y.toFixed(3)})`;
    console.log(`  band ${String(i).padStart(2)}  `
      + `muted=${String(mutedSet.has(i)).padEnd(5)} `
      + `selected=${String(selected.includes(i)).padEnd(5)} `
      + `chip=${fmt(c).padEnd(34)} mark=${m ? fmt(m) : "NONE"}`);
  }
}

// A. A selected muted band's mark contains its whole mute button.
const selectedMuted = SELECT.filter((i) => mutedSet.has(i));
check(`the stimulus produced ${selectedMuted.length} selected muted band(s)`,
  selectedMuted.length >= 2,
  "the fixture no longer exercises the reported case");
for (const i of selectedMuted) {
  const chip = expectedChip(i);
  const marks = markFor(i);
  if (marks.length !== 1) {
    fail(`band ${i} is selected and muted but carries ${marks.length} selection `
      + "mark(s) over its chip, expected exactly 1");
    continue;
  }
  const m = marks[0];
  check(`band ${i}: the selection mark contains the whole mute button`,
    contains(m, chip),
    `mark ${m.w.toFixed(3)}x${m.h.toFixed(3)}@(${m.x.toFixed(3)},${m.y.toFixed(3)}) `
    + `does not contain chip ${chip.w.toFixed(3)}x${chip.h.toFixed(3)}@`
    + `(${chip.x.toFixed(3)},${chip.y.toFixed(3)}) -- this is the reported `
    + "defect: a glyph-sized mark inside an unmarked button");
  check(`band ${i}: the mark is painted in a visible colour`,
    !invisible(m.style), `stroke style is ${m.style}`);
  // The tint that makes the button read lifted rather than merely outlined.
  const tint = main.groups.find((gp) => gp.kind === "fill"
    && gp.style !== CHIP_FILL && !invisible(gp.style)
    && Math.abs(gp.x - chip.x) < 0.01 && Math.abs(gp.h - chip.h) < 0.01);
  check(`band ${i}: the selected button is tinted, not only outlined`,
    !!tint, "no visible fill covering the chip box");
}

// B. An unselected muted neighbour carries no mark. The user's second ask.
const unselectedMuted = MUTED.filter((i) => !selected.includes(i));
check(`the stimulus produced ${unselectedMuted.length} unselected muted band(s)`,
  unselectedMuted.length >= 2,
  "without one, 'a neighbour does not look selected' is untested");
for (const i of unselectedMuted) {
  check(`band ${i}: muted but unselected, so it carries no selection mark`,
    markFor(i).length === 0,
    `${markFor(i).length} mark(s) over an unselected band's chip -- selecting `
    + "one band made its muted neighbour look selected too");
}

// C. The ordinary case is untouched: a selected UNMUTED band still gets the
//    body rect, at the body's geometry rather than the chip's.
const selectedUnmuted = SELECT.filter((i) => !mutedSet.has(i));
check(`the stimulus produced ${selectedUnmuted.length} selected unmuted band(s)`,
  selectedUnmuted.length >= 2, "the regression guard has nothing to guard");
for (const i of selectedUnmuted) {
  const marks = selMarks.filter((m) => Math.abs(m.x + m.w / 2 - centre(i)) < bandW / 2);
  if (marks.length !== 1) {
    fail(`band ${i} is selected and unmuted but carries ${marks.length} mark(s), `
      + "expected exactly 1 body rect");
    continue;
  }
  check(`band ${i}: an unmuted selection still outlines the band body`,
    marks[0].kind === "strokeRect" && marks[0].h > expectedChip(i).h + 5,
    `mark is ${marks[0].kind} ${marks[0].h.toFixed(3)} tall; a body rect over a `
    + "real gain must be taller than the chip");
}

// D. Every mark the suite counted is ink.
check("no selection mark is painted in a transparent colour",
  selMarks.every((m) => !invisible(m.style)),
  "a mark was recorded with zero alpha -- the geometry is right and nothing is "
  + "on screen, which is the one thing a command recorder cannot see by itself");

// ---------------------------------------------------------------- verdict

const ok = failures.length === 0;
if (!ok) {
  console.error("");
  for (const f of failures) console.error("FAIL: " + f);
}
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
console.log("\nOK: a selected muted band outlines and tints its mute button, an "
  + "unselected muted neighbour carries no mark, and a selected unmuted band "
  + "still outlines its body");
process.exit(0);
