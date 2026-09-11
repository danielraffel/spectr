#!/usr/bin/env node
// Executes the shipped modulation display code out of the materialized
// document and MEASURES what it paints, rather than asserting that the source
// says what the source says.
//
// Two contracts, both user-visible, both previously broken:
//
// 1. EXCURSION. The audio owner publishes the post-LFO band field once per
//    processed block; the editor's rAF loop paints it. The loop also smooths
//    every band toward the CANONICAL (pre-LFO) target. While the overlay owns
//    the paint refs those two pull in opposite directions on every single
//    frame, so the painted waveform is a shrunken copy of the one the audio is
//    actually playing -- a rate-independent ~31% of the excursion is lost, and
//    an LFO the user can hear looks weaker than it is. This suite drives the
//    real `draw` and `applyModulationFrame` against a known producer and
//    requires the painted peak-to-peak to match the published peak-to-peak.
//
// 2. LIVE SETTINGS. Every LFO scalar is an automatable host parameter. The
//    settings panel used to read them exactly once, at mount, so a DAW
//    automating LFO 2 Depth animated the overlay while the panel stayed pinned
//    at whatever the session opened with. This suite mounts the real
//    SpectrModulationSettings component against a mock bridge, delivers a
//    native live-state frame, and requires the panel to follow it -- while a
//    value the user is mid-edit still wins over an in-flight frame.
//
// Usage:
//   node test_materialized_modulation.mjs <materialized-document.runtime.json>
//        [--plant-smoothing] [--plant-oneshot] [--expect-fail]
//
// --plant-smoothing restores the unguarded `rg[i] = smooth(rg[i], target, ...)`
// (the exact pre-fix line). --plant-oneshot deletes the live/hydrate
// subscription effect (the exact pre-fix shape). --expect-fail inverts the
// verdict, so a control row is green only when the planted defect is REJECTED,
// and a missing file, usage error, or thrown extractor still fails it.

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantSmoothing = args.includes("--plant-smoothing");
const plantOneshot = args.includes("--plant-oneshot");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_modulation.mjs <runtime.json> "
    + "[--plant-smoothing] [--plant-oneshot] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const failures = [];
const notes = [];

// ---------------------------------------------------------------- extraction
// A planted defect that rewrites nothing proves nothing, so every plant and
// every extraction asserts its own hit count before the measurement runs.

function replaceExactlyOnce(source, needle, replacement, label) {
  const n = source.split(needle).length - 1;
  if (n !== 1) throw new Error(`${label}: anchor occurs ${n} times, expected 1`);
  return source.replace(needle, replacement);
}

// Returns the source text of a brace-delimited block starting at `anchor`.
function blockAt(source, anchor, label) {
  const at = source.indexOf(anchor);
  if (at < 0) throw new Error(`${label}: anchor not found`);
  if (source.indexOf(anchor, at + 1) >= 0) throw new Error(`${label}: anchor is not unique`);
  const open = source.indexOf("{", at);
  if (open < 0) throw new Error(`${label}: no block after anchor`);
  let depth = 0;
  let inLine = false;
  let inString = null;
  for (let i = open; i < source.length; i++) {
    const c = source[i];
    const prev = source[i - 1];
    if (inLine) { if (c === "\n") inLine = false; continue; }
    if (inString) {
      if (c === "\\") { i++; continue; }
      if (c === inString) inString = null;
      continue;
    }
    if (c === "/" && source[i + 1] === "/") { inLine = true; i++; continue; }
    if (c === '"' || c === "'" || c === "`") { inString = c; continue; }
    if (c === "{") depth++;
    else if (c === "}") {
      depth--;
      if (depth === 0) return source.slice(at, i + 1);
    }
  }
  throw new Error(`${label}: unbalanced block`);
}

const PRE_FIX_SMOOTH = "rg[i] = smooth(rg[i], target, dt * k);";
const GUARDED_SMOOTH = `if (!modulationActiveRef.current) ${PRE_FIX_SMOOTH}`;

if (plantSmoothing) {
  html = replaceExactlyOnce(html, GUARDED_SMOOTH, PRE_FIX_SMOOTH, "--plant-smoothing");
  notes.push("planted the unguarded canonical smoothing (pre-fix rAF loop)");
}

if (plantOneshot) {
  const effect = blockAt(html,
    "  React.useEffect(() => {\n    if (!spectrModulationBridge || typeof spectrModulationBridge.on !== \"function\")",
    "--plant-oneshot");
  // The whole statement, deps tail included -- deleting only the block leaves
  // a dangling argument list, and the suite would then be rejecting a syntax
  // error instead of the stale panel this control exists to exercise.
  const statement = effect + ", [spectrModulationBridge, readNativeModulation]);";
  html = replaceExactlyOnce(html, statement, "", "--plant-oneshot");
  notes.push("planted the one-shot settings hydration (pre-fix panel)");
}

for (const n of notes) console.log(n);

// ------------------------------------------------------ 1. excursion measure

function measureExcursion() {
  const drawSrc = blockAt(html, "const draw = (now) => {", "draw loop");
  const amfSrc = blockAt(html, "applyModulationFrame: (state) => {", "applyModulationFrame");
  const clampSrc = "const clamp = (v, a, b) => Math.max(a, Math.min(b, v));";
  const smoothSrc = "const smooth = (a, b, t) => a + (b - a) * (1 - Math.exp(-t));";
  const isMutedSrc = "function isMuted(g) { return g === -Infinity; }";
  for (const [needle, label] of [[clampSrc, "clamp"], [smoothSrc, "smooth"]]) {
    if (!html.includes(needle)) throw new Error(`${label}: shipped definition changed`);
  }
  if (!/function isMuted\(g\) \{\s*return g === -Infinity;\s*\}/.test(html)) {
    throw new Error("isMuted: shipped definition changed");
  }

  const N = 24;
  // The real loop, in a sandbox that supplies exactly the refs it closes over.
  const factory = new Function("N", `
    ${clampSrc}
    ${smoothSrc}
    ${isMutedSrc}
    return function makeRig(motionMode) {
      const targetGainsRef = { current: new Array(N).fill(0) };
      const renderGainsRef = { current: new Array(N).fill(0) };
      const modulationActiveRef = { current: false };
      const timeRef = { current: 0 };
      const unmutePulseRef = { current: new Float32Array(N) };
      const edgeGlowRef = { current: { left: 0, right: 0, top: 0, bottom: 0 } };
      const rafRef = { current: 0 };
      const renderAllRef = { current: () => {} };
      const renderAll = () => {};
      const updateLiveHoverStatus = () => {};
      const requestAnimationFrame = () => 0;
      let last = 0;
      ${drawSrc};
      const bank = { ${amfSrc} };
      return {
        targetGainsRef, renderGainsRef, modulationActiveRef,
        setLast: (t) => { last = t; },
        draw,
        applyModulationFrame: bank.applyModulationFrame,
      };
    };
  `)(N);

  // A frame schedule with real jitter: a perfect 16.667 ms cadence hides every
  // hold/catch-up artefact this measurement exists to find.
  const frameSchedule = (count, seed) => {
    let s = seed >>> 0;
    const out = [];
    let t = 0;
    for (let i = 0; i < count; i++) {
      s = (s * 1664525 + 1013904223) >>> 0;
      const jitter = ((s >>> 8) % 1000) / 1000; // 0..1
      // 60 Hz nominal, occasionally a dropped frame (>= 25 ms gap).
      const gap = jitter > 0.88 ? 0.0333 + jitter * 0.02 : 0.0140 + jitter * 0.0055;
      t += gap;
      out.push({ t, gap });
    }
    return out;
  };

  const band = 7;
  const results = [];
  let gapsOver25ms = 0;
  for (const producerHz of [60, 46.9, 23.4]) {
    for (const lfoHz of [0.5, 2, 8]) {
      const rig = factory("live");
      const canonical = 0.25;
      rig.targetGainsRef.current.fill(canonical);
      rig.renderGainsRef.current.fill(canonical);
      const frames = frameSchedule(420, 0x5eed + Math.round(producerHz * 10) + lfoHz * 7);
      rig.setLast(0);
      const published = [];
      const painted = [];
      let nextPublish = 0;
      const depth = 0.6;
      for (const { t, gap } of frames) {
        if (gap >= 0.025) gapsOver25ms++;
        while (nextPublish <= t) {
          const g = canonical + depth * Math.sin(2 * Math.PI * lfoHz * nextPublish);
          rig.applyModulationFrame({
            active: true,
            n: N,
            gains: new Array(N).fill(g),
            muted: new Array(N).fill(false),
          });
          published.push(g);
          nextPublish += 1 / producerHz;
        }
        rig.draw(t * 1000);
        // Settling transient: the first 25% of the run is warm-up.
        painted.push(rig.renderGainsRef.current[band]);
      }
      const tail = (a) => a.slice(Math.floor(a.length * 0.35));
      const span = (a) => Math.max(...a) - Math.min(...a);
      const pubSpan = span(tail(published));
      const paintSpan = span(tail(painted));
      const ratio = pubSpan > 0 ? paintSpan / pubSpan : 0;
      results.push({ producerHz, lfoHz, ratio });
    }
  }

  const ratios = results.map((r) => r.ratio).sort((a, b) => a - b);
  const worst = ratios[0];
  // p95 of the LOSS, i.e. the 5th percentile of retained excursion: the value
  // 95% of sweep cells beat. A median is useless here -- the shipped defect
  // was uniform across every cell, so half the cells being fine proves nothing.
  const p95 = ratios[Math.max(0, Math.floor(ratios.length * 0.05))];
  console.log(`excursion: ${results.length} cells (producer x LFO rate), `
    + `worst ${(worst * 100).toFixed(1)}%, p95 ${(p95 * 100).toFixed(1)}%, `
    + `${gapsOver25ms} inter-frame gaps >= 25 ms`);
  for (const r of results) {
    console.log(`  producer ${String(r.producerHz).padStart(5)} Hz  LFO ${String(r.lfoHz).padStart(4)} Hz  `
      + `painted/published ${(r.ratio * 100).toFixed(1)}%`);
  }

  const FLOOR = 0.97;
  if (!(worst >= FLOOR)) {
    failures.push(`painted excursion is only ${(worst * 100).toFixed(1)}% of the `
      + `published excursion (floor ${(FLOOR * 100).toFixed(0)}%); the overlay `
      + `under-reports the modulation the audio is playing`);
  }

  // Control 1 -- UNMODULATED. Same rig, overlay never engaged. The canonical
  // smoothing must still run, or "the excursion now matches" would be
  // satisfied by a loop that simply stopped animating anything.
  const ctl = factory("live");
  ctl.targetGainsRef.current.fill(0);
  ctl.renderGainsRef.current.fill(0);
  ctl.setLast(0);
  ctl.targetGainsRef.current.fill(0.8);
  let ctlT = 0;
  const ctlSeries = [];
  for (let i = 0; i < 90; i++) {
    ctlT += 1 / 60;
    ctl.draw(ctlT * 1000);
    ctlSeries.push(ctl.renderGainsRef.current[band]);
  }
  const settled = ctlSeries[ctlSeries.length - 1];
  const moved = ctlSeries.some((v, i) => i > 0 && v !== ctlSeries[i - 1]);
  console.log(`control (unmodulated): painted ${ctlSeries[0].toFixed(4)} -> `
    + `${settled.toFixed(4)} toward target 0.8000, monotone-advancing=${moved}`);
  if (!moved || Math.abs(settled - 0.8) > 0.01) {
    failures.push("control: the unmodulated path no longer smooths toward the "
      + "canonical target -- the excursion result above is meaningless");
  }

  // Control 2 -- RELEASE. On the falling edge the overlay hands the refs back,
  // and canonical smoothing must resume. Without this, "skip smoothing while
  // active" could silently become "never smooth again".
  const rel = factory("live");
  rel.targetGainsRef.current.fill(0.5);
  rel.renderGainsRef.current.fill(0.5);
  rel.setLast(0);
  rel.applyModulationFrame({ active: true, n: N, gains: new Array(N).fill(-0.9), muted: new Array(N).fill(false) });
  let relT = 0;
  relT += 1 / 60; rel.draw(relT * 1000);
  const heldUnderOverlay = rel.renderGainsRef.current[band];
  rel.applyModulationFrame({ active: false, n: N });
  for (let i = 0; i < 60; i++) { relT += 1 / 60; rel.draw(relT * 1000); }
  const afterRelease = rel.renderGainsRef.current[band];
  console.log(`control (release): under overlay ${heldUnderOverlay.toFixed(4)}, `
    + `after release ${afterRelease.toFixed(4)} toward canonical 0.5000`);
  if (Math.abs(heldUnderOverlay - (-0.9)) > 1e-9) {
    failures.push(`control: the overlay frame was not painted verbatim `
      + `(${heldUnderOverlay.toFixed(4)} != -0.9000)`);
  }
  if (Math.abs(afterRelease - 0.5) > 0.01) {
    failures.push("control: canonical smoothing did not resume after the "
      + "overlay released the paint refs");
  }
}

// -------------------------------------------------- 2. live settings readback

function measureLiveSettings() {
  const componentSrc = blockAt(html, "function SpectrModulationSettings() {",
    "SpectrModulationSettings");

  // A minimal ordered-hook runtime. Enough to mount one function component,
  // run its effects, and re-render on setState -- which is all this contract
  // needs, and keeps the test reading the SHIPPED component rather than a
  // paraphrase of it.
  const makeRuntime = () => {
    const hooks = [];
    let cursor = 0;
    let rendered = null;
    const effects = [];
    const cleanups = [];
    let scheduled = false;
    const React = {
      useState(initial) {
        const i = cursor++;
        if (!(i in hooks)) hooks[i] = { value: typeof initial === "function" ? initial() : initial };
        const slot = hooks[i];
        return [slot.value, (next) => {
          slot.value = typeof next === "function" ? next(slot.value) : next;
          if (!scheduled) { scheduled = true; queueMicrotask(() => { scheduled = false; render(); }); }
        }];
      },
      useRef(initial) {
        const i = cursor++;
        if (!(i in hooks)) hooks[i] = { current: initial };
        return hooks[i];
      },
      // Real useCallback memoises on deps. A shim that returns a fresh
      // function every render makes every dependent effect re-run, which here
      // means re-issuing the mount hydration -- a late response then clobbers
      // the automation value and the suite measures the shim, not the panel.
      useCallback(fn, deps) {
        const i = cursor++;
        const prev = hooks[i];
        const changed = !prev || !prev.deps || !deps
          || deps.length !== prev.deps.length || deps.some((d, j) => d !== prev.deps[j]);
        if (changed) hooks[i] = { deps, fn };
        return hooks[i].fn;
      },
      useEffect(fn, deps) {
        const i = cursor++;
        const prev = hooks[i];
        const changed = !prev || !prev.deps || !deps
          || deps.length !== prev.deps.length || deps.some((d, j) => d !== prev.deps[j]);
        hooks[i] = { deps };
        if (changed) effects.push({ i, fn });
      },
      createElement: (type, props, ...children) => ({ type, props, children }),
    };
    const stub = (name) => name;
    const run = new Function("React", "window", "SpectrSettingsGroup",
      "SpectrSettingsField", "SpectrSettingsToggle", "SpectrSettingsChips",
      "SpectrSettingsSlider", `${componentSrc}; return SpectrModulationSettings;`);
    let component = null;
    function render() {
      cursor = 0;
      rendered = component();
      while (effects.length) {
        const e = effects.shift();
        if (typeof cleanups[e.i] === "function") cleanups[e.i]();
        cleanups[e.i] = e.fn();
      }
    }
    const api = {
      React,
      get rendered() { return rendered; },
      mount(win) {
        component = run(React, win, stub("group"), stub("field"),
          stub("toggle"), stub("chips"), stub("slider"));
        render();
      },
      // The panel's state lives in hook slot 0 (its single useState).
      state: () => hooks[0].value,
      flush: async () => { for (let i = 0; i < 8; i++) await Promise.resolve(); render(); },
    };
    return api;
  };

  const makeBridge = () => {
    const listeners = new Map();
    const sent = [];
    return {
      sent,
      emit(channel, payload) {
        for (const fn of listeners.get(channel) || []) fn({ payload });
      },
      listenerCount: (channel) => (listeners.get(channel) || []).length,
      pulp: {
        postMessage(kind, body) {
          sent.push({ kind, body });
          if (kind === "processing_state_get") {
            return Promise.resolve({ payload: { modulation: {
              enabled: true, shape: 0, beats_per_cycle: 4, depth: 0.25, target: 0,
              lfo2_enabled: true, lfo2_shape: 1, lfo2_beats_per_cycle: 2,
              lfo2_depth: 0.1, target_mask: 15 } } });
          }
          return Promise.resolve({});
        },
        on(channel, fn) {
          if (!listeners.has(channel)) listeners.set(channel, []);
          listeners.get(channel).push(fn);
          return () => {
            const a = listeners.get(channel);
            a.splice(a.indexOf(fn), 1);
          };
        },
      },
    };
  };

  return (async () => {
    const bridge = makeBridge();
    const win = { pulp: bridge.pulp };
    const rt = makeRuntime();
    rt.mount(win);
    await rt.flush();

    const hydrated = rt.state();
    console.log(`settings: hydrated lfo2Depth=${hydrated.lfo2Depth} `
      + `depth=${hydrated.depth} enabled=${hydrated.enabled}`);
    if (hydrated.lfo2Depth !== 0.1) {
      failures.push(`settings: mount hydration did not land (lfo2Depth=${hydrated.lfo2Depth}, expected 0.1)`);
      return;
    }

    // The user's case: a DAW automates LFO 2 Depth while the editor is open.
    // Native republishes on the live channel; the panel must follow.
    bridge.emit("processing_state_live", { modulation: {
      enabled: true, shape: 0, beats_per_cycle: 4, depth: 0.25, target: 0,
      lfo2_enabled: true, lfo2_shape: 1, lfo2_beats_per_cycle: 2,
      lfo2_depth: 0.85, target_mask: 15 } });
    await rt.flush();
    const afterLive = rt.state();
    console.log(`settings: after host automation lfo2Depth=${afterLive.lfo2Depth}`);
    if (afterLive.lfo2Depth !== 0.85) {
      failures.push(`settings: host automation on LFO 2 Depth did not reach the `
        + `panel (lfo2Depth=${afterLive.lfo2Depth}, expected 0.85) -- it still `
        + `shows the value the session opened with`);
    }

    // Automation on the LFO target lane resets the mask; the chips must follow
    // rather than keep drawing the user's old destination set.
    bridge.emit("processing_state_hydrate", { modulation: {
      enabled: true, shape: 0, beats_per_cycle: 4, depth: 0.25, target: 3,
      lfo2_enabled: true, lfo2_shape: 1, lfo2_beats_per_cycle: 2,
      lfo2_depth: 0.85, target_mask: 8 } });
    await rt.flush();
    const afterMask = rt.state();
    console.log(`settings: after target automation targetMask=${afterMask.targetMask} `
      + `target=${afterMask.target} selection=${afterMask.targetSelection}`);
    if (afterMask.targetMask !== 8 || afterMask.target !== 3) {
      failures.push(`settings: target automation did not reach the panel `
        + `(targetMask=${afterMask.targetMask}, target=${afterMask.target})`);
    }

    // Control: the panel following native must NOT mean a frame already in
    // flight yanks a control out from under the user. A local write wins until
    // native echoes it back.
    const before = rt.state().depth;
    const publishDepth = 0.42;
    // Drive the real publish path through the rendered tree's onChange.
    const slider = findProp(rt, "Depth");
    if (!slider) {
      failures.push("control: could not reach the Depth control's onChange");
      return;
    }
    slider(publishDepth);
    await rt.flush();
    bridge.emit("processing_state_live", { modulation: {
      enabled: true, shape: 0, beats_per_cycle: 4, depth: before, target: 3,
      lfo2_enabled: true, lfo2_shape: 1, lfo2_beats_per_cycle: 2,
      lfo2_depth: 0.85, target_mask: 8 } });
    await rt.flush();
    const contested = rt.state().depth;
    console.log(`control (in-flight frame): local write ${publishDepth}, stale `
      + `native ${before}, panel shows ${contested}`);
    if (contested !== publishDepth) {
      failures.push(`control: a stale native frame overwrote the user's own `
        + `edit (depth=${contested}, expected ${publishDepth})`);
    }
    // ...and once native echoes the write back, the panel is following native
    // again rather than latched on the local value forever.
    bridge.emit("processing_state_live", { modulation: {
      enabled: true, shape: 0, beats_per_cycle: 4, depth: publishDepth, target: 3,
      lfo2_enabled: true, lfo2_shape: 1, lfo2_beats_per_cycle: 2,
      lfo2_depth: 0.85, target_mask: 8 } });
    await rt.flush();
    bridge.emit("processing_state_live", { modulation: {
      enabled: true, shape: 0, beats_per_cycle: 4, depth: 0.11, target: 3,
      lfo2_enabled: true, lfo2_shape: 1, lfo2_beats_per_cycle: 2,
      lfo2_depth: 0.85, target_mask: 8 } });
    await rt.flush();
    const released = rt.state().depth;
    console.log(`control (echo release): panel follows native again -> ${released}`);
    if (released !== 0.11) {
      failures.push(`control: the panel stayed latched on the local edit after `
        + `native echoed it (depth=${released}, expected 0.11)`);
    }
  })();
}

// Walks the rendered tree for the labelled field's onChange.
function findProp(rt, label) {
  let found = null;
  const walk = (node) => {
    if (!node || typeof node !== "object" || found) return;
    if (Array.isArray(node)) { node.forEach(walk); return; }
    const props = node.props || {};
    if (props.label === label) {
      const kid = (node.children || []).flat().find((c) => c && c.props && c.props.onChange);
      if (kid) { found = kid.props.onChange; return; }
    }
    walk(node.children);
  };
  walk(rt.rendered ? rt.rendered : null);
  return found;
}

// ------------------------------------------------------------------- verdict

const run = async () => {
  try {
    measureExcursion();
  } catch (error) {
    failures.push(`excursion measurement threw: ${error.message}`);
  }
  try {
    await measureLiveSettings();
  } catch (error) {
    failures.push(`live-settings measurement threw: ${error.message}`);
  }

  const passed = failures.length === 0;
  if (passed) {
    console.log("OK: the overlay paints the published excursion, and the "
      + "settings panel follows host automation");
  } else {
    for (const f of failures) console.error(`FAIL: ${f}`);
  }

  if (expectFail) {
    if (passed) {
      console.error("FAIL: the planted defect was NOT rejected");
      process.exit(1);
    }
    console.log("OK: the planted defect was rejected");
    process.exit(0);
  }
  process.exit(passed ? 0 : 1);
};

run();
