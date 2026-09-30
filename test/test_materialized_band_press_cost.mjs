#!/usr/bin/env node
// PRESSING AND RELEASING A BAND DOES NOT RE-RENDER THE EDITOR.
//
// Measured with Perfetto on the shipping standalone, with and without audio:
// the press and the release of a band-drawing gesture cost 88-124 ms each, all
// of it React. This document is a captured import, so every commit re-applies
// the whole document (~44 ms). The press published the band reading through
// the bank's `onStatus` -- the App's state, then the banner's own commit -- and
// the release did that again after re-rendering the bank to refresh React's
// `gains`, a mirror nothing on screen reads.
//
// Two halves, one per component:
//
//   BANK    FilterBank evaluated in a vm with a hook shim that counts every
//           state change React would schedule (a setter resolving to the value
//           already held is a bail-out and is not counted). A stroke, a click
//           and a mute-brush pass are driven through the real handlers; each
//           press and release must schedule no render and publish no status
//           through `onStatus`, while the pill still gets the reading, undo is
//           still bracketed once, and native still receives the edit. Then the
//           consistency the release render used to paper over: a later
//           ordinary (non-deferred) edit must not bring a stale mirror back
//           over what was drawn.
//   BANNER  StatusBanner evaluated the same way against fake shell and text
//           nodes: `spectrStatusBannerShow` must put a message up and take it
//           down with no render, React's own messages must still work, a
//           rendered message must replace a direct one on screen, and status
//           info off must show nothing.
//
// Usage:
//   node test_materialized_band_press_cost.mjs <runtime.json> [--plant-...] [--expect-fail]
//
// --plant-release-render     re-renders the bank on release to refresh the
//                            mirror, as it shipped.
// --plant-status-via-react   publishes press/release readings through
//                            `onStatus` again.
// --plant-stale-mirror       builds commitMany's mirror on `prev` again, so a
//                            later ordinary edit restores the stale copy over
//                            a drawn stroke.
// --plant-banner-renders     makes the direct show a React state change.
// --expect-fail inverts the verdict: green only when this suite REJECTS the
// planted document. Not WILL_FAIL, which accepts any non-zero exit including
// the ones that mean the control never ran.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const documentPath = args.find((a) => !a.startsWith("--"));
const expectFail = args.includes("--expect-fail");
const has = (n) => args.includes(n);

if (!documentPath) {
  console.error("usage: test_materialized_band_press_cost.mjs <runtime.json> "
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

if (has("--plant-release-render")) {
  plant("a release that re-renders the bank",
    "      nativeProjectionRef.current = false;\n"
    + "      nativeEditPendingRef.current = false;\n"
    + "      if (p.changed && p.editedBand >= 0)\n",
    "      setGains(targetGainsRef.current.slice());\n"
    + "      if (p.changed && p.editedBand >= 0)\n");
}
if (has("--plant-status-via-react")) {
  plant("press and release readings through onStatus",
    "    if (typeof window.spectrStatusBannerShow === \"function\"\n"
    + "        && window.spectrStatusBannerShow(message)) return;\n", "");
}
if (has("--plant-stale-mirror")) {
  plant("a mirror built on prev",
    "    if (!deferReact) setGains(nextTarget.slice());\n",
    "    if (!deferReact) setGains((prev) => {\n"
    + "      const nextState = prev.slice();\n"
    + "      for (const [k, v] of map) nextState[k] = v;\n"
    + "      return nextState;\n"
    + "    });\n");
}
if (has("--plant-banner-renders")) {
  plant("a direct show that renders",
    "    paintStatus(shell, textNode, display);\n    directRef.current = true;\n",
    "    setText(display);\n    setVisible(true);\n    directRef.current = true;\n");
}

const failures = [];
const fail = (msg) => failures.push(msg);

const blocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const bankBlock = blocks.find((b) => b.includes("function FilterBank("));
const bannerBlock = blocks.find((b) => b.includes("function StatusBanner("));
const controls = {
  "script blocks": blocks.length,
  "filter bank": html.split("function FilterBank(").length - 1,
  "status banner": html.split("function StatusBanner(").length - 1,
  "pointer down": html.split("  const onPointerDown = (e) => {").length - 1,
  "pointer up": html.split("  const onPointerUp = (e) => {").length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(16), count);
const blind = Object.entries(controls)
  .filter(([k, v]) => (k === "script blocks" ? v === 0 : v !== 1)).map(([k]) => k);
if (blind.length || !bankBlock || !bannerBlock) {
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
  if (!surface || typeof surface.props.onPointerDown !== "function") {
    console.error("FAIL: the bank rendered no pointer surface");
    process.exit(2);
  }
  const renderState = () => sandbox.__spectrTestHooks.renderState();
  // One interaction, measured: the renders it schedules, the statuses it
  // publishes through React, and what reached the pill directly.
  const measure = (label, fn) => {
    const commits = hooks.commits.length;
    const status = statusCalls;
    const pill = shown.length;
    fn();
    const r = {
      label, commits: hooks.commits.length - commits,
      statusCalls: statusCalls - status, shown: shown.slice(pill),
    };
    console.log("measured  %s: %d render(s), %d onStatus, pill %s", label.padEnd(18),
      r.commits, r.statusCalls, JSON.stringify(r.shown));
    return r;
  };
  const noRender = (r) => {
    if (r.commits !== 0) {
      fail(`${r.label} scheduled ${r.commits} React render(s); in this captured `
        + "import each re-applies the whole document (~44 ms)");
    }
    if (r.statusCalls !== 0) {
      fail(`${r.label} published ${r.statusCalls} status message(s) through `
        + "onStatus -- the App's state, a root commit plus the banner's own");
    }
  };

  // B1. A DRAWN STROKE: press, draw across three bands, release.
  posted.length = 0;
  const before = renderState().targetGains.slice();
  const down = measure("stroke press", () =>
    surface.props.onPointerDown(pointer(centre(12), PLOT_Y)));
  surface.props.onPointerMove(pointer(centre(13), PLOT_Y - 60));
  surface.props.onPointerMove(pointer(centre(14), PLOT_Y - 60));
  const up = measure("stroke release", () =>
    surface.props.onPointerUp(pointer(centre(14), PLOT_Y - 60)));
  noRender(down);
  noRender(up);
  // The press changes nothing, so it shows nothing: the pill is feedback for
  // an edit. The release ends a stroke that did change bands, so it reads out
  // the band the stroke last edited under the pointer.
  if (down.shown.length !== 0) {
    fail(`the press put ${JSON.stringify(down.shown)} in the pill; a press that `
      + "has changed no band must show nothing");
  }
  if (!up.shown.some((m) => /BAND 15\/32/.test(m))) {
    fail(`the release put ${JSON.stringify(up.shown)} in the pill; it must show `
      + "the reading of the band the stroke last edited (band 15)");
  }
  const drawn = renderState().targetGains;
  const drawnAfterStroke = drawn.slice();
  if (!(drawn[13] > 0.05) || drawn[13] === before[13]) {
    fail(`the stroke did not draw band 14 (target ${drawn[13]}); the rows above `
      + "measured a gesture that did nothing");
  }
  const count = (type) => posted.filter((p) => p.type === type).length;
  if (count("undo_gesture_start") !== 1 || count("undo_gesture_end") !== 1) {
    fail(`the stroke bracketed undo ${count("undo_gesture_start")}/`
      + `${count("undo_gesture_end")} times, expected one entry`);
  }
  const lastSet = posted.filter((p) => p.type === "processing_state_set").pop();
  if (!lastSet || Math.abs(lastSet.payload.gain_db[13] - drawn[13] * 24) > 1e-6) {
    fail("native never received the stroke's final gains");
  }

  // B1b. THE SAME STROKE AGAIN changes nothing -- bands 14 and 15 already sit
  // at the level it draws -- so press, moves and release show nothing.
  const replay = measure("no-op stroke", () => {
    surface.props.onPointerDown(pointer(centre(13), PLOT_Y - 60));
    surface.props.onPointerMove(pointer(centre(14), PLOT_Y - 60));
    surface.props.onPointerUp(pointer(centre(14), PLOT_Y - 60));
  });
  noRender(replay);
  if (renderState().targetGains.some((value, i) => value !== drawnAfterStroke[i])) {
    console.error("FAIL: the no-op stroke changed a band, so its empty pill is "
      + "not evidence about anything");
    process.exit(2);
  }
  if (replay.shown.length !== 0) {
    fail(`a stroke that changed nothing put ${JSON.stringify(replay.shown)} in `
      + "the pill; only a gesture that changes a band may show a reading");
  }

  // B2. A CLICK toggles one band's mute.
  posted.length = 0;
  const clickDown = measure("click press", () =>
    surface.props.onPointerDown(pointer(centre(20), PLOT_Y)));
  const clickUp = measure("click release", () =>
    surface.props.onPointerUp(pointer(centre(20), PLOT_Y)));
  noRender(clickDown);
  noRender(clickUp);
  if (clickUp.shown[clickUp.shown.length - 1] !== "BAND 21 MUTED") {
    fail(`a click's pill reads ${JSON.stringify(clickUp.shown)}, expected it to `
      + 'end on "BAND 21 MUTED"');
  }
  const muteSet = posted.filter((p) => p.type === "processing_state_set").pop();
  if (!muteSet || muteSet.payload.muted[20] !== true) {
    fail("the click's mute never reached native");
  }
  if (Number.isFinite(renderState().targetGains[20])) {
    fail("the click did not mute band 21 on screen");
  }

  // B3. THE MUTE BRUSH.
  const brushDown = measure("brush press", () =>
    surface.props.onPointerDown(pointer(centre(5), PLOT_Y, { shift: true })));
  surface.props.onPointerMove(pointer(centre(7), PLOT_Y, { shift: true }));
  const brushUp = measure("brush release", () =>
    surface.props.onPointerUp(pointer(centre(7), PLOT_Y, { shift: true })));
  noRender(brushDown);
  noRender(brushUp);
  if (Number.isFinite(renderState().targetGains[6])) {
    fail("the mute brush did not mute band 7");
  }

  // B4. A STALE MIRROR CANNOT COME BACK. Nothing above refreshed React's
  // `gains`. An ordinary edit that DOES render (unmute-all goes through
  // commitMany without deferral) must carry what was drawn, not the mirror's
  // old copy -- which the [gains] effect would then write back over the ref.
  if (!sharedState.current || typeof sharedState.current.unmuteAll !== "function") {
    fail("the bank handle has no unmuteAll, so the stale-mirror check cannot run");
  } else {
    sharedState.current.unmuteAll();
    render();
    const after = renderState().targetGains;
    console.log("measured  after unmute-all: band 14 %s (drawn %s)",
      after[13], drawn[13]);
    if (Math.abs(after[13] - drawn[13]) > 1e-9) {
      fail(`unmute-all put band 14 back to ${after[13]} from the drawn `
        + `${drawn[13]}: a mirror built on React's stale copy overwrote the stroke`);
    }
    if (!Number.isFinite(after[20]) || !Number.isFinite(after[6])) {
      fail("unmute-all did not unmute the clicked and brushed bands");
    }
  }

  // B5. A PAN (Alt + drag) moves the view on every sample without a render.
  // Minimap drags and the wheel already publish the live viewport and settle
  // once; a pan re-rendered the whole captured document on every move. It
  // gets one settling commit on release, and the view must land where the
  // drag put it.
  if (!sharedState.current || typeof sharedState.current.zoomTo !== "function") {
    fail("the bank handle has no zoomTo, so the pan row cannot run");
  } else {
    sharedState.current.zoomTo(200, 2000);
    settle();
    const start = { ...renderState().view };
    const span = start.lmax - start.lmin;
    const panDown = measure("pan press", () =>
      surface.props.onPointerDown(pointer(centre(16), PLOT_Y, { alt: true })));
    const moves = measure("pan moves", () => {
      for (let i = 1; i <= 12; i++)
        surface.props.onPointerMove(pointer(centre(16) - i * 10, PLOT_Y, { alt: true }));
    });
    const live = { ...renderState().view };
    const panUp = measure("pan release", () =>
      surface.props.onPointerUp(pointer(centre(16) - 120, PLOT_Y, { alt: true })));
    render();
    const settled = renderState();
    const expected = start.lmin + (120 / inner.w) * span;
    console.log("measured  pan: view %s..%s live, %s..%s settled (expected lmin %s)",
      live.lmin.toFixed(6), live.lmax.toFixed(6),
      settled.view.lmin.toFixed(6), settled.view.lmax.toFixed(6), expected.toFixed(6));
    if (moves.commits !== 0) {
      fail(`12 pan moves scheduled ${moves.commits} React render(s); a pan must `
        + "publish the live viewport and settle once, like a minimap drag");
    }
    if (panDown.commits !== 0) fail(`the pan press scheduled ${panDown.commits} render(s)`);
    if (panUp.commits > 1) {
      fail(`the pan release scheduled ${panUp.commits} render(s); one settle is enough`);
    }
    if (Math.abs(live.lmin - expected) > 1e-9 || Math.abs(live.lmax - live.lmin - span) > 1e-9) {
      fail(`the live view is ${live.lmin}..${live.lmax}; the drag put it at `
        + `${expected}..${expected + span}`);
    }
    if (Math.abs(settled.view.lmin - expected) > 1e-9
        || Math.abs(settled.reactView.lmin - expected) > 1e-9) {
      fail(`after release the view is ${settled.view.lmin} (React copy `
        + `${settled.reactView.lmin}); both must settle at ${expected}`);
    }
  }
}

// ============================================================== BANNER
{
  const hooks = makeHooks();
  const attributes = new Map();
  const shell = {
    style: {},
    setAttribute(name, value) { attributes.set(name, String(value)); },
    removeAttribute(name) { attributes.delete(name); },
    getAttribute(name) { return attributes.has(name) ? attributes.get(name) : null; },
    getBoundingClientRect: () => ({ left: 0, top: 104, width: 200, height: 26 }),
  };
  const textNode = { textContent: "" };
  const sandbox = {
    ...baseGlobals, ...timing, React: hooks.React,
    document: {
      querySelector: (sel) => sel === "[data-spectr-status-shell]" ? shell
        : sel === "[data-spectr-status-text]" ? textNode : null,
      getElementById: () => null, addEventListener() {}, removeEventListener() {},
    },
  };
  sandbox.globalThis = sandbox;
  sandbox.window = sandbox;
  try {
    vm.runInContext(bannerBlock, vm.createContext(sandbox), { filename: "chrome-block.js" });
  } catch (error) {
    console.error(`FAIL: evaluating the banner's script block threw ${error.message}`);
    process.exit(2);
  }
  let element = null;
  // React's own writes to the two nodes, reconciled against what was last
  // RENDERED, never against the live node -- which is how a direct write and
  // a render can disagree on screen.
  let renderedText;
  let renderedFlag;
  const render = (props) => {
    hooks.cursor = 0;
    element = sandbox.StatusBanner(props);
    const flag = element.props["data-spectr-status-banner"];
    if (flag !== renderedFlag) {
      renderedFlag = flag;
      if (flag === undefined) attributes.delete("data-spectr-status-banner");
      else attributes.set("data-spectr-status-banner", String(flag));
    }
    const text = element.children[0].children.join("");
    if (text !== renderedText) { renderedText = text; textNode.textContent = text; }
    hooks.runEffects();
  };
  const show = (message) => sandbox.spectrStatusBannerShow(message);
  const up = () => attributes.get("data-spectr-status-banner") === "true";
  let props = { message: "", disabled: false };
  render(props);
  if (typeof sandbox.spectrStatusBannerShow !== "function") {
    fail("StatusBanner installs no spectrStatusBannerShow, so the bank has no "
      + "way to show a reading without a render");
  } else {
    // S1. UP AND DOWN WITH NO RENDER.
    hooks.commits.length = 0;
    const handled = sandbox.spectrStatusBannerShow("BAND 3 MUTED");
    const upCommits = hooks.commits.length;
    if (handled !== true) fail("the direct show refused a mounted banner");
    if (!up() || textNode.textContent !== "BAND 3 MUTED" || shell.style.opacity !== 1) {
      fail(`a direct show left the pill at up=${up()} text=`
        + `${JSON.stringify(textNode.textContent)} opacity=${shell.style.opacity}`);
    }
    advance(1500);
    if (!up()) fail("a MUTED reading was taken down before its 2.8 s hold");
    advance(2000);
    const downCommits = hooks.commits.length - upCommits;
    if (up() || textNode.textContent !== "" || shell.style.opacity !== 0) {
      fail(`the direct message never went down: up=${up()} text=`
        + `${JSON.stringify(textNode.textContent)} opacity=${shell.style.opacity}`);
    }
    console.log("measured  direct show: %d render(s) up, %d down", upCommits, downCommits);
    if (upCommits !== 0 || downCommits !== 0) {
      fail(`showing and hiding a direct message scheduled ${upCommits}+`
        + `${downCommits} render(s); each re-applies the whole document`);
    }
    // S2. REACT'S OWN MESSAGES STILL SHOW AND HIDE.
    props = { message: "EDIT → SCULPT|1", disabled: false };
    render(props);
    render(props);
    if (!up() || textNode.textContent !== "EDIT → SCULPT") {
      fail(`a rendered message shows ${JSON.stringify(textNode.textContent)}`);
    }
    // S3. A RENDERED MESSAGE REPLACES A DIRECT ONE ON SCREEN, even when
    // React's own copy already holds that message and its setters bail out.
    show("BAND 9/32");
    props = { message: "EDIT → SCULPT|2", disabled: false };
    render(props);
    render(props);
    if (textNode.textContent !== "EDIT → SCULPT") {
      fail(`a rendered message over a direct one left the pill reading `
        + `${JSON.stringify(textNode.textContent)}`);
    }
    advance(3000);
    render(props);
    if (up()) fail("the rendered message never went down");
    // S4. STATUS INFO OFF SHOWS NOTHING, and says it was handled.
    props = { message: "", disabled: true };
    render(props);
    if (show("BAND 1/32") !== true || up()) {
      fail("with status info off a direct show still put the pill up");
    }
  }
}

const passed = failures.length === 0;
for (const f of failures) console.log("FAIL:", f);
if (passed) {
  console.log("PASS: pressing and releasing a band schedules no render; the "
    + "pill shows each reading directly and takes it down directly.");
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
