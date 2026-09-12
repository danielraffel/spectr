#!/usr/bin/env node
// Guards two contracts at the seam where native publications write the
// editor's JS refs directly. Both were reported from the shipped AU, and
// neither is visible from the standalone or from a screenshot.
//
// (1) A COMMAND THAT REPORTS SUCCESS MUST REACH THE FIELD.
//     CLEAR published its status pill and left every band standing. The band
//     field reaches C++ through one effect whose echo suppressor
//     (`nativeProjectionRef`) is a ONE-SHOT: a projection arms it, the render
//     that projection causes consumes it. `applyHostAutomationState` armed it
//     and fired no shot -- it writes refs only and calls no state setter, so
//     the publication effect never re-runs for it and the flag stays latched
//     until some unrelated later edit consumes it instead. CLEAR was that
//     edit: it mutates the refs and calls setGains WITHOUT raising
//     `nativeEditPendingRef`, the "this is a local edit" signal `commitGain`
//     and `commitMany` use to override the suppressor. So the banner fired
//     and nothing crossed the bridge. It differed by host because
//     `processing_state_live` is published only when host_automation_revision
//     moves, and that only moves when something OUTSIDE the plugin writes a
//     parameter -- a DAW does, the standalone does not.
//
// (2) A MUTED BAND MUST PAINT ON ITS SENTINEL, NOT NEAR IT.
//     A muted band's painted value is `-Infinity`, and the draw loop holds it
//     there by collapsing any finite value it finds on a muted band down to
//     -1.02 and snapping to the sentinel -- about eight frames of ramp. Both
//     payload parsers already encode a muted band as -Infinity, but all three
//     projections into `renderGainsRef` overrode that with `? 0 :`, so every
//     projection knocked the band off its sentinel and restarted the collapse.
//     Measured on the pre-fix shipping document: a muted band paints ONE
//     constant value across 20 idle frames, and -0.36 / -0.59 / -0.74
//     repeating once projections arrive. No LFO is required -- host-automation
//     projections alone do it in a DAW -- but an LFO makes them continuous,
//     which is why it was first noticed with one running.
//
// The assertions are on the FIELD and on the PAINTED VALUE, never on a status
// message. The banner said exactly the right thing while the product did the
// wrong thing, which is the class of false green this suite exists to close.
//
// Usage:
//   node test_materialized_clear_and_mute_overlay.mjs <runtime.json>
//        [--plant-latched-suppressor | --plant-flatten-muted
//         | --plant-undeclared-clear] [--expect-fail]
//
// Each plant restores exactly one half of the pre-fix document, so a failing
// control names which check is load bearing. --expect-fail inverts the
// verdict: the row is green only when this suite REJECTS the planted
// document, and red when the plant fails to apply or anything else goes
// wrong. The inversion lives here rather than in WILL_FAIL because WILL_FAIL
// accepts any non-zero exit -- a usage error satisfied it and proved nothing.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantSuppressor = args.includes("--plant-latched-suppressor");
const plantFlatten = args.includes("--plant-flatten-muted");
const plantUndeclared = args.includes("--plant-undeclared-clear");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_clear_and_mute_overlay.mjs "
    + "<runtime.json> [--plant-...] [--expect-fail]");
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

if (plantSuppressor) {
  plant("the host-automation projection arms the one-shot suppressor again",
    "        // No nativeProjectionRef arm here, deliberately. That flag is a\n"
    + "        // ONE-SHOT suppressor for the render a projection causes, and this\n"
    + "        // path calls no state setter -- it writes refs only, so the\n"
    + "        // publication effect never re-runs for it. Arming it leaves the\n"
    + "        // flag latched until some unrelated later edit consumes it, and\n"
    + "        // that edit is the one that silently never reaches native.\n",
    "        nativeProjectionRef.current = true;\n");
  // The pre-fix document had no edit declaration on the local mutators
  // either. Both halves have to go for the runtime defect to reappear:
  // either one alone is sufficient to keep CLEAR working, which is exactly
  // why the fix carries both.
  plant("the local mutators stop declaring their edit",
    "        nativeEditPendingRef.current = true;\n", "", 4);
}

if (plantFlatten) {
  plant("projections flatten a muted band to 0 again",
    "state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)",
    "state.muted[index] ? 0 : clamp(value, -1.02, 1.02)", 3);
  plant("the modulation release flattens a muted band to 0 again",
    "isMuted(value) ? -Infinity : clamp(value, -1.02, 1.02)",
    "isMuted(value) ? 0 : clamp(value, -1.02, 1.02)");
}

if (plantUndeclared) {
  plant("clearGains stops declaring its edit",
    "      clearGains: () => {\n"
    + "        // Declare the local edit. Without this the publication effect\n"
    + "        // cannot tell a user command from a native echo, and a CLEAR that\n"
    + "        // lands after a projection is dropped on the floor while the\n"
    + "        // status pill still reports success.\n"
    + "        nativeEditPendingRef.current = true;\n",
    "      clearGains: () => {\n");
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
  "publication effect": html.split(
    'window.pulp.postMessage("processing_state_set"').length - 1,
  "echo suppressor": html.split(
    "if (nativeProjectionRef.current && !nativeEditPendingRef.current)").length - 1,
  "host-automation projection": html.split("applyHostAutomationState: (state)").length - 1,
  "modulation overlay": html.split("applyModulationFrame: (state)").length - 1,
  "muted paint sentinel": html.split("rg[i] = smooth(rg[i], -1.02, dt * 26)").length - 1,
};
for (const [label, count] of Object.entries(controls))
  console.log("control   %s %s", label.padEnd(28), count);
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length) {
  console.error("FAIL: the payload has no " + blind.join(", ")
    + " -- this suite is reading the wrong document or the editor was "
    + "restructured, so it cannot render a verdict");
  process.exit(2);
}

// ------------------------------------------------------------ static check
// S1. Every local mutator that writes targetGainsRef and calls setGains must
// declare the edit. This is defence in depth for the runtime check below:
// with the suppressor no longer armed by a path that cannot consume it,
// either half alone keeps CLEAR working, so the second half can only be
// asserted statically. A mutator that silently loses its declaration is one
// unrelated future arm away from being swallowed again.

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

for (const header of ["      clearGains: () => {", "      resetAll: () => {",
                      "      reset: () => {", "      setGains: (arr) => {"]) {
  const name = header.trim().split(":")[0];
  const body = balancedBody(html, header);
  if (!body) { fail(`could not extract the body of ${name}`); continue; }
  if (!body.includes("targetGainsRef.current =")) {
    fail(`${name} no longer writes targetGainsRef -- this check has drifted `
      + "off its subject and cannot render a verdict");
    continue;
  }
  if (!body.includes("nativeEditPendingRef.current = true")) {
    fail(`${name} mutates the band field without declaring a local edit, so `
      + "the publication effect cannot tell it from a native echo and a "
      + "latched suppressor will swallow it");
  }
}

// ------------------------------------------------------------ runtime half

const N = 32;
const log = [];
let rafQueue = [];
let clock = 1000;
const hooks = [];
let cursor = 0;
const effects = [];
const cleanups = [];
const sent = [];

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
      log.push("commit");
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

const sandbox = {
  React, Math, Array, Object, Set, Map, String, Number, Boolean, JSON, Date,
  Error, Promise, Float32Array, Uint8Array, isNaN, isFinite, parseFloat, parseInt,
  Infinity, NaN,
  console: { log() {}, warn() {}, error(...a) { log.push("console.error:" + a.join(" ")); } },
  // A real timestamp on every frame. Without one, dt is NaN, the muted branch
  // snaps straight to the sentinel and the oscillation this suite measures
  // would be invisible -- a silent false pass.
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
sandbox.window.pulp = {
  postMessage(type, payload) {
    sent.push({ type, payload });
    return Promise.resolve({ ok: true, payload: { ok: true, revision: 1 } });
  },
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

let bank = null;
const render = () => {
  cursor = 0;
  const element = sandbox.FilterBank(props);
  while (effects.length) {
    const e = effects.shift();
    if (typeof cleanups[e.i] === "function") cleanups[e.i]();
    cleanups[e.i] = e.fn();
  }
  bank = sharedState.current;
  return element;
};
// One frame: advance the clock, drain the rAF queue once. The draw loop
// re-queues itself, so this is a real frame and not a flush-to-quiescence.
const frame = (n = 1) => {
  for (let i = 0; i < n; i++) {
    clock += 16.667;
    const due = rafQueue;
    rafQueue = [];
    for (const fn of due) if (fn) { try { fn(clock); } catch (e) { log.push("raf:" + e.message); } }
  }
};
const painted = () => sandbox.__spectrTestHooks.renderState().gains;
const target = () => sandbox.__spectrTestHooks.renderState().targetGains;
const fields = () => sent.filter((s) => s.type === "processing_state_set");
const liveState = (gainDb, muted, revision) => ({
  n: N, gainDb: gainDb.slice(), muted: muted.slice(),
  gains: gainDb.map((db, i) => muted[i] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
  minHz: 20, maxHz: 20000, revision,
  motionMode: "live", analyzerMode: "off", editMode: "sculpt",
  visualizationMode: "bars",
});
const modFrame = (active, gainDb, muted) => ({
  n: N, active, muted: muted.slice(),
  gains: gainDb.map((db, i) => muted[i] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
});

try {
  vm.runInContext(bankBlock, vm.createContext(sandbox), { filename: "bank-block.js" });
} catch (error) {
  fail(`evaluating the bank's script block threw ${error.constructor.name}: `
    + error.message);
}

if (typeof sandbox.FilterBank !== "function") {
  fail("FilterBank is not reachable after evaluating its own script block");
} else {
  render();
  frame(2);

  if (!sandbox.__spectrTestHooks.renderState) {
    fail("the bank did not install its renderState test hook, so this suite "
      + "cannot read the painted field and must not render a verdict");
  } else {

  // ---- R1. CLEAR after a host-automation projection reaches the field.
  //
  // The order is the product's: the editor publishes an edit, native adopts
  // it and echoes a live projection back (a DAW moves host_automation_revision
  // on every parameter write), and only then does the user press CLEAR.
  sent.length = 0;
  bank.setGains(new Array(N).fill(0).map((_, i) => (i < 8 ? 0.5 : 0)));
  render();
  frame();
  const baseline = fields();
  // Stimulus control: without a NON-ZERO field already in native, "CLEAR
  // published zeros" is trivially true and proves nothing.
  if (baseline.length !== 1) {
    fail(`the baseline edit produced ${baseline.length} field publications, `
      + "expected 1 -- the stimulus never reached the bridge, so every "
      + "verdict below is vacuous");
  } else if (!baseline[0].payload.gain_db.some((db) => db !== 0)) {
    fail("the baseline field published all zeros, so a cleared field is "
      + "indistinguishable from it and this check proves nothing");
  }

  const adopted = bank.applyHostAutomationState(
    liveState(new Array(N).fill(0).map((_, i) => (i < 8 ? 12 : 0)),
              new Array(N).fill(false), 7));
  if (adopted !== true) {
    fail("the host-automation projection was refused, so the latch this "
      + "check exists to defeat was never armed");
  }

  sent.length = 0;
  bank.clearGains();
  render();
  frame();
  const cleared = fields();
  if (cleared.length === 0) {
    fail("CLEAR published NOTHING to native after a host-automation "
      + "projection: the status pill reports CLEARED GAINS and the band "
      + "field in C++ is untouched");
  } else {
    const gainDb = cleared[cleared.length - 1].payload.gain_db;
    const muted = cleared[cleared.length - 1].payload.muted;
    const live = gainDb.filter((db) => db !== 0).length;
    if (live !== 0)
      fail(`CLEAR published a field with ${live} non-zero band(s); every `
        + "band must reach native at 0 dB");
    if (muted.some(Boolean))
      fail("CLEAR published a field with muted bands still set");
    console.log(`runtime   R1 clear published ${cleared.length} field(s), `
      + `nonZeroBands=${live}`);
  }

  // ---- R2. The preset-apply path is the same imperative setter, so a preset
  // chosen after a projection has to reach the field too.
  bank.applyHostAutomationState(
    liveState(new Array(N).fill(0), new Array(N).fill(false), 9));
  sent.length = 0;
  bank.setGains(new Array(N).fill(0).map((_, i) => (i % 2 ? 0.75 : -0.25)));
  render();
  frame();
  const preset = fields();
  if (preset.length === 0) {
    fail("applying a preset after a host-automation projection published "
      + "NOTHING to native: the pattern name appears in the status line and "
      + "the field never changes");
  } else {
    const db = preset[preset.length - 1].payload.gain_db;
    if (!(Math.abs(db[1] - 18) < 1e-6 && Math.abs(db[0] + 6) < 1e-6))
      fail(`the preset published gain_db[0..1]=${db[0]},${db[1]}, expected `
        + "-6,18 -- the field that crossed is not the preset");
    console.log(`runtime   R2 preset published ${preset.length} field(s), `
      + `gain_db[0..1]=${db[0]},${db[1]}`);
  }

  // ---- R3. A muted band with a NON-ZERO authored gain, under host-automation
  // projections and NO LFO. This is the user's refined repro, and it is the
  // cheapest discriminator: if the painted value moves here, the LFO is
  // incidental.
  const authored = new Array(N).fill(0).map((_, i) => (i < 8 ? 12 : 0));
  const muted = new Array(N).fill(false);
  muted[2] = true;               // muted, authored at +12 dB
  const MUTED = 2, UNMUTED = 3;  // same authored gain, mute is the only difference
  bank.applyHostAutomationState(liveState(authored, muted, 11));
  render();
  frame(30);                     // let the field settle before measuring

  if (!Number.isFinite(target()[UNMUTED]) || target()[MUTED] !== -Infinity) {
    fail(`the projection did not land as expected (targetGains muted=`
      + `${target()[MUTED]} unmuted=${target()[UNMUTED]}), so the paint `
      + "measurements below are reading the wrong bands");
  }

  const key = (v) => (Number.isFinite(v) ? v.toFixed(2) : "-Inf");
  // Native publishes on its own tick, NOT in lockstep with the browser's
  // frames, and that is what makes the defect a sawtooth rather than a
  // constant: the collapse ramp gets part-way down and is reset. Publishing
  // once per frame would hold the ramp at its first step and report ONE
  // distinct value -- a stable-looking measurement of an unstable band.
  const PUBLISH_EVERY = 3;
  const sweep = (label, stimulus, frames) => {
    const seenMuted = new Set();
    const seenUnmuted = new Set();
    for (let f = 0; f < frames; f++) {
      if (f % PUBLISH_EVERY === 0) stimulus(f);
      frame();
      seenMuted.add(key(painted()[MUTED]));
      seenUnmuted.add(key(painted()[UNMUTED]));
    }
    console.log(`runtime   ${label} mutedValues=${seenMuted.size} `
      + `unmutedValues=${seenUnmuted.size} muted={${[...seenMuted].join(",")}}`);
    return { seenMuted, seenUnmuted };
  };

  // Idle control: nothing published, so nothing may move. If the muted band
  // is already unstable here, the measurement below cannot attribute anything
  // to the projections.
  const idle = sweep("R3 idle       ", () => {}, 20);
  if (idle.seenMuted.size !== 1)
    fail(`a muted band painted ${idle.seenMuted.size} distinct values across `
      + "20 idle frames with nothing published; it must sit still");

  let revision = 12;
  const projected = sweep("R3 projections", () => {
    bank.applyHostAutomationState(liveState(authored, muted, revision++));
  }, 24);
  if (projected.seenMuted.size !== 1)
    fail(`a muted band drawn at +12 dB painted ${projected.seenMuted.size} `
      + `distinct values (${[...projected.seenMuted].join(", ")}) across 24 `
      + "host-automation projections and NO LFO -- it flickers");
  // Positive control on the same stimulus: the UNMUTED band at the same
  // authored gain must also be stable, so a stable muted band is a property
  // of the fix rather than of a dead draw loop.
  if (projected.seenUnmuted.size !== 1)
    fail(`the unmuted control band painted ${projected.seenUnmuted.size} `
      + "distinct values under the same projections, so this stimulus "
      + "disturbs every band and the muted result is not about mute");

  // ---- R4. The same band with an LFO running. Modulation frames arrive
  // continuously, so this is the rate at which the defect was first seen.
  // The positive control is a MODULATED unmuted band, which MUST move --
  // otherwise a still muted band proves only that the overlay is dead.
  let phase = 0;
  const lfo = sweep("R4 modulation ", () => {
    phase += 0.5;
    const modulated = authored.slice();
    modulated[UNMUTED] = 12 * Math.sin(phase);
    bank.applyModulationFrame(modFrame(true, modulated, muted));
  }, 24);
  if (lfo.seenMuted.size !== 1)
    fail(`a muted band painted ${lfo.seenMuted.size} distinct values `
      + `(${[...lfo.seenMuted].join(", ")}) across 24 modulation frames -- it `
      + "flickers while the LFO runs");
  if (lfo.seenUnmuted.size < 5)
    fail(`the modulated unmuted control band painted only `
      + `${lfo.seenUnmuted.size} distinct values, so the overlay is not `
      + "actually driving the paint refs and a still muted band proves nothing");

  // The muted band's one painted value must be the sentinel itself, not some
  // other constant: the draw loop, drawBands and drawMaskResponse all key off
  // isMuted(), so a muted band parked at a finite constant is still wrong.
  if (painted()[MUTED] !== -Infinity)
    fail(`a muted band paints ${painted()[MUTED]}, expected the -Infinity `
      + "sentinel every collapse path targets");

  // Releasing the overlay must leave it on the sentinel too.
  bank.applyModulationFrame(modFrame(false, authored, muted));
  frame(3);
  if (painted()[MUTED] !== -Infinity)
    fail(`releasing the modulation overlay left a muted band painting `
      + `${painted()[MUTED]}, expected the -Infinity sentinel`);
  }
}

// ----------------------------------------------------------------- verdict

const passed = failures.length === 0;
for (const f of failures) console.log("FAIL:", f);
if (passed) {
  console.log("PASS: CLEAR reaches the band field after a native projection, "
    + "and a muted band paints on its sentinel.");
}

if (expectFail) {
  if (passed) {
    console.error("CONTROL FAIL: the planted pre-fix document was accepted, "
      + "so this suite cannot detect the defect it claims to guard");
    process.exit(1);
  }
  console.log("CONTROL PASS: the planted pre-fix document was rejected.");
  process.exit(0);
}
process.exit(passed ? 0 : 1);
