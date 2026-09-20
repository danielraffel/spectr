#!/usr/bin/env node
// Executes the BOTTOM-BAR latency chip out of the materialized document and
// measures what it shows and what it sends.
//
// WHAT THIS COVERS THAT test_materialized_latency_mode.mjs DOES NOT. That
// suite drives `SpectrLatencySettings` -- the group inside the Settings modal.
// The chip in the bottom rail is a DIFFERENT component (`SpectrLatencyRail`)
// with its own reader, its own formatter and its own press path, and until
// this file existed nothing executed any of them. The two surfaces share only
// `globalThis.__spectrLatency` and `spectrToggleLatencyMode`, so every property
// below can break in the rail while the Settings suite stays green.
//
// THE FIGURE MUST BE DERIVED, AND FROM THE RIGHT PLACE. The payload carries a
// top-level `ms` AND a per-option `ms`, and they disagree the moment the mode
// is switched optimistically: the top-level figure still describes the mode the
// processor was in, while the label beside it already names the new one. A chip
// reading the top-level figure therefore shows the OLD latency next to the NEW
// mode -- right at rest, wrong exactly when the user pressed the thing. So the
// payload here gives the two a deliberately impossible relationship (a
// top-level `ms` matching neither option) and the assertions require the
// SELECTED OPTION's figure. A hardcoded millisecond figure is right at 48 kHz
// and wrong at 96 kHz, so the option figures are unusual too.
//
// THE TWO SURFACES MUST AGREE. Mode changes arrive from three places -- this
// chip, the `T` key, and the Settings chips -- and all three write one store.
// A writer that updates the store without waking its listeners leaves the other
// surface displaying the mode it used to be in. The rail is the surface that
// loses, because it re-renders ONLY when woken. That is asserted in both
// directions rather than assumed.
//
// Usage:
//   node test_materialized_latency_rail.mjs <runtime.json>
//        [--plant-top-level-ms] [--plant-typed-ms] [--plant-post-index]
//        [--plant-no-subscribe] [--plant-toggle-no-notify]
//        [--plant-settings-no-notify] [--plant-render-unhydrated]
//        [--expect-fail]
//
// Each plant restores one specific wrong implementation and fails a different
// assertion. --expect-fail inverts the verdict: a control row is green only
// when this suite REJECTS that document.

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const has = (f) => args.includes(f);
const expectFail = has("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_latency_rail.mjs <runtime.json> "
    + "[--plant-...] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const failures = [];
const check = (name, ok, detail) => {
  if (ok) console.log("  PASS %s", name);
  else {
    console.log("  FAIL %s  %s", name, detail === undefined ? "" : detail);
    failures.push(name);
  }
};

// Every plant must be OBSERVED to apply. A plant whose needle has drifted
// silently produces the current document, and the control then passes while
// proving nothing at all.
function plant(label, from, to) {
  const hits = html.split(from).length - 1;
  if (hits !== 1) {
    console.error(`FAIL: plant "${label}" found ${hits} sites, expected 1 `
      + "-- the control cannot prove anything");
    process.exit(2);
  }
  html = html.replace(from, to);
  console.log("planted   %s", label);
}

// Some shapes are shared verbatim by BOTH latency components -- the hydration
// subscription and the unhydrated guard are the same eight lines in each. A
// whole-document plant of one of those hits two sites and proves nothing, so
// these are planted INSIDE the rail's own function body and the count is
// checked there.
function plantIn(blockAnchor, label, from, to) {
  const block = blockAt(html, blockAnchor, `${label} (scope)`);
  const hits = block.split(from).length - 1;
  if (hits !== 1) {
    console.error(`FAIL: plant "${label}" found ${hits} sites inside `
      + `${blockAnchor} -- the control cannot prove anything`);
    process.exit(2);
  }
  html = html.replace(block, block.replace(from, to));
  console.log("planted   %s", label);
}

function blockAt(source, anchor, label) {
  const at = source.indexOf(anchor);
  if (at < 0) throw new Error(`${label}: anchor not found`);
  if (source.indexOf(anchor, at + 1) >= 0) throw new Error(`${label}: anchor is not unique`);
  const open = source.indexOf("{", at);
  let depth = 0, inLine = false, inString = null;
  for (let i = open; i < source.length; i++) {
    const c = source[i];
    if (inLine) { if (c === "\n") inLine = false; continue; }
    if (inString) {
      if (c === "\\") { i++; continue; }
      if (c === inString) inString = null;
      continue;
    }
    if (c === "/" && source[i + 1] === "/") { inLine = true; i++; continue; }
    if (c === '"' || c === "'" || c === "`") { inString = c; continue; }
    if (c === "{") depth++;
    else if (c === "}") { depth--; if (depth === 0) return source.slice(at, i + 1); }
  }
  throw new Error(`${label}: unbalanced block`);
}

// ------------------------------------------------------------------- plants

if (has("--plant-top-level-ms")) {
  // The subtle one: the chip reads the payload's top-level figure instead of
  // the selected option's. Correct at rest, wrong the instant the mode moves.
  plant("a chip reading the payload's top-level ms, not the selected option's",
    "  const millis = (typeof current.ms === 'number')\n"
    + "    ? (current.ms < 10 ? current.ms.toFixed(1) : String(Math.round(current.ms))) + \" ms\"",
    "  const millis = (typeof hydrated.ms === 'number')\n"
    + "    ? (hydrated.ms < 10 ? hydrated.ms.toFixed(1) : String(Math.round(hydrated.ms))) + \" ms\"");
}

if (has("--plant-typed-ms")) {
  // A figure typed into the chip: right at one sample rate, wrong at every
  // other, and it stops tracking the mode entirely.
  plant("a chip with the millisecond figure typed into it",
    "    millis ? (label + \" \\u00B7 \" + millis) : label));",
    "    label + \" \\u00B7 171 ms\"));");
}

if (has("--plant-post-index")) {
  // An index on the wire makes the option ORDER part of the contract, so
  // reordering the chips silently changes what they do.
  plant("a toggle that sends the option's index instead of its token",
    "Promise.resolve(window.pulp.postMessage(\"render_mode_set\", "
    + "{ mode: next.mode }, \"spectr-render-mode\"))",
    "Promise.resolve(window.pulp.postMessage(\"render_mode_set\", "
    + "{ mode: 1 }, \"spectr-render-mode\"))");
}

if (has("--plant-no-subscribe")) {
  // The invisible-forever defect: the editor mounts before the processor
  // answers, so a chip that never subscribes reads an empty global once and
  // stays blank for the whole session.
  plantIn("function SpectrLatencyRail() {",
    "a chip that never subscribes to hydration",
    "    const listeners = store.listeners || (store.listeners = []);\n"
    + "    const onHydrate = function () {\n"
    + "      setRevision(function (n) { return n + 1; });\n"
    + "    };\n"
    + "    listeners.push(onHydrate);",
    "    const listeners = store.listeners || (store.listeners = []);\n"
    + "    const onHydrate = function () {\n"
    + "      setRevision(function (n) { return n + 1; });\n"
    + "    };");
}

if (has("--plant-toggle-no-notify")) {
  // Pressing the chip moves the store but wakes nobody, so opening Settings
  // afterwards shows the mode it used to be in.
  plant("a toggle that writes the store without waking the listeners",
    "  store.state = Object.assign({}, state, { mode: next.mode });\n"
    + "  // Wake BOTH surfaces. The Settings chips subscribe to this same list,\n"
    + "  // so toggling from the rail moves the panel too -- otherwise opening\n"
    + "  // Settings after a T press would show the mode it used to be in.\n"
    + "  (store.listeners || []).forEach(function (fn) {",
    "  store.state = Object.assign({}, state, { mode: next.mode });\n"
    + "  ([]).forEach(function (fn) {");
}

if (has("--plant-settings-no-notify")) {
  // The mirror image, and the direction the rail loses: the Settings chips
  // write the store and wake nobody, so the rail chip keeps showing the mode
  // and the latency it used to be in. Removing the wake restores exactly that.
  plant("Settings chips that write the store without waking the listeners",
    "    // Wake BOTH surfaces, the same way spectrToggleLatencyMode does. The\n"
    + "    // bottom-bar chip re-renders ONLY when woken -- it keeps no state of its\n"
    + "    // own -- so without this it goes on showing the mode, and the millisecond\n"
    + "    // figure, it used to be in while the panel beside it already reads the\n"
    + "    // new one. `pending` above covers this component's own chips and reaches\n"
    + "    // nothing else; the listener list is the only channel the two surfaces\n"
    + "    // share. Each listener is isolated: one bad subscriber must not stop the\n"
    + "    // rest, and must not throw out of a click handler.\n"
    + "    (store.listeners || []).forEach(function (fn) {\n"
    + "      try { fn(); } catch (error) {\n"
    + "        console.error(\"[Spectr] latency listener failed\", error);\n"
    + "      }\n"
    + "    });\n",
    "");
}

if (has("--plant-render-unhydrated")) {
  // An empty chip before the payload arrives, rather than no chip.
  plantIn("function SpectrLatencyRail() {",
    "a chip that renders before the payload arrives",
    "  if (!hydrated || !Array.isArray(hydrated.options)\n"
    + "      || hydrated.options.length === 0) return null;",
    "  if (false) return null;");
}

// --------------------------------------------------------------- the rig

const toggleSrc = blockAt(html, "function spectrToggleLatencyMode() {", "spectrToggleLatencyMode");
const railSrc = blockAt(html, "function SpectrLatencyRail() {", "SpectrLatencyRail");
const publishSrc = blockAt(html, "const publish = function (next) {", "Settings publish");

// Deliberately unusual figures. A typed millisecond value cannot reproduce
// them, and the top-level `ms` matches NEITHER option, so a chip reading the
// wrong field is visible rather than merely suspected.
const PAYLOAD = {
  control_label: "Latency",
  mode: "linear_phase",
  samples: 8224,
  ms: 999,                                   // matches neither option, on purpose
  options: [
    { mode: "linear_phase", label: "Mixing",
      description: "Deepest cuts.", samples: 8224, ms: 171.4285714285 },
    { mode: "zero_latency", label: "Tracking",
      description: "Plays in time with you.", samples: 64, ms: 1.3333333333 },
  ],
};

function makeRig() {
  const posted = [];
  const logged = [];
  let created = [];
  // Hook state persists across re-renders, the way React's does; `cursor`
  // resets per render so the same call order reads the same slots.
  const states = [];
  let cursor = 0;
  let renderRequests = 0;
  let effectsRun = false;
  const cleanups = [];

  const React = {
    createElement: (type, props, ...children) => {
      const node = { type, props: props || {}, children: children.flat() };
      created.push(node);
      return node;
    },
    useState: (initial) => {
      const i = cursor++;
      if (states.length <= i)
        states[i] = typeof initial === "function" ? initial() : initial;
      return [states[i], (next) => {
        states[i] = typeof next === "function" ? next(states[i]) : next;
        renderRequests++;
      }];
    },
    // Effects run once, on the first render, like a [] dependency list. A stub
    // that swallowed them would leave the subscription untested -- which is
    // exactly the hole that shipped a control nobody could see.
    useEffect: (fn) => {
      if (effectsRun) return;
      const cleanup = fn();
      if (typeof cleanup === "function") cleanups.push(cleanup);
    },
  };

  const sandboxWindow = {
    pulp: {
      postMessage: (kind, body, tag) => {
        posted.push({ kind, body, tag });
        // The processor's answer. The rail logs the confirmed figures.
        return Promise.resolve({ payload: { latency: { mode: body && body.mode, samples: 64 } } });
      },
    },
  };

  const globals = {};
  const fn = new Function("React", "window", "globalThis", "console", "__ctl", `
    ${toggleSrc}
    ${railSrc}
    // The Settings writer, executed rather than restated, so the cross-surface
    // assertions measure the shipped code on both sides.
    const __settingsPublish = (store, active, setPending) => {
      ${publishSrc}
      return publish;
    };
    return {
      spectrToggleLatencyMode,
      SpectrLatencyRail,
      settingsPublish: __settingsPublish,
    };
  `)(React, sandboxWindow, globals,
     { error: (...a) => logged.push(a.join(" ")), log: (...a) => logged.push(a.join(" ")) },
     null);

  const render = () => {
    cursor = 0;
    created = [];
    // A component that THROWS is a component that failed, not a reason to kill
    // the run. Letting it escape would end the process before the assertions
    // ran, and an --expect-fail row that dies has proved nothing -- it cannot
    // say WHICH rule it broke, and it would read the same way if the rig
    // itself were broken. So the throw becomes a value the checks can judge.
    let node;
    try {
      node = fn.SpectrLatencyRail();
    } catch (error) {
      node = { threw: String(error && error.message) };
    }
    effectsRun = true;
    return { node, created };
  };

  return {
    globals, posted, logged, render, fn,
    renderRequests: () => renderRequests,
    hydrate: (payload) => {
      const store = globals.__spectrLatency || (globals.__spectrLatency = {});
      store.state = payload;
      (store.listeners || []).forEach((f) => f());
    },
  };
}

let rig;
try { rig = makeRig(); }
catch (error) {
  console.error(`FAIL: could not build the rail rig: ${error.message}`);
  process.exit(2);
}

const chipOf = (out) => out.created.filter((n) =>
  n.props && n.props["data-spectr-latency-chip"])[0];
const textOf = (out) => {
  const spans = out.created.filter((n) => n.type === "span");
  const last = spans[spans.length - 1];
  return last ? last.children.filter((c) => typeof c === "string").join("") : "";
};

// ---- 1. Before hydration: no chip at all, rather than an empty one --------
let out = rig.render();
check("an unhydrated rail renders no chip",
  out.node === null,
  out.node === null ? "null"
    : (out.node && out.node.threw ? `it threw: ${out.node.threw}` : "an element"));
check("mounting registers a hydration listener",
  Array.isArray(rig.globals.__spectrLatency && rig.globals.__spectrLatency.listeners)
    && rig.globals.__spectrLatency.listeners.length === 1,
  JSON.stringify((rig.globals.__spectrLatency || {}).listeners || null));

// ---- 2. A payload arriving AFTER mount wakes the chip ---------------------
const before = rig.renderRequests();
rig.hydrate(PAYLOAD);
check("a payload arriving after mount wakes the chip",
  rig.renderRequests() > before,
  `render requests ${before} -> ${rig.renderRequests()}`);

// ---- 3. The chip, and what it reads --------------------------------------
out = rig.render();
const chip = chipOf(out);
check("a latency chip renders once hydrated", !!chip);
check("the chip carries the live mode for the native probe to read",
  chip && chip.props["data-spectr-latency-mode"] === "linear_phase",
  chip && String(chip.props["data-spectr-latency-mode"]));

// The figure is the SELECTED OPTION's (171.43 -> "171 ms"), never the
// top-level 999 and never the other option's 1.3.
check("the chip shows the live mode's label, uppercased from the payload",
  /^MIXING\b/.test(textOf(out)), JSON.stringify(textOf(out)));
check("the chip shows the SELECTED OPTION's derived figure",
  /· 171 ms$/.test(textOf(out)), JSON.stringify(textOf(out)));
check("the chip does NOT show the payload's top-level figure",
  !/999/.test(textOf(out)), JSON.stringify(textOf(out)));

// ---- 4. Pressing it: one message, carrying the TOKEN ----------------------
const postsBefore = rig.posted.length;
if (chip && typeof chip.props.onClick === "function") chip.props.onClick();
check("pressing the chip posts exactly one message",
  rig.posted.length === postsBefore + 1,
  `posted ${rig.posted.length - postsBefore}`);
const msg = rig.posted[rig.posted.length - 1];
check("it posts render_mode_set", msg && msg.kind === "render_mode_set",
  msg && String(msg.kind));
check("it posts the next mode's TOKEN, not its index",
  msg && msg.body && msg.body.mode === "zero_latency",
  msg && JSON.stringify(msg.body));

// ---- 5. After the press the chip reads the OTHER option's figure ----------
out = rig.render();
check("the chip follows the switch to the other mode's label",
  /^TRACKING\b/.test(textOf(out)), JSON.stringify(textOf(out)));
// 1.333 -> "1.3 ms": under ten the figure keeps a decimal, which is the rule
// the Settings hint uses, so one surface cannot read 1.3 while the other reads 1.
check("a sub-10ms figure keeps one decimal rather than rounding to an integer",
  /· 1\.3 ms$/.test(textOf(out)), JSON.stringify(textOf(out)));

// ---- 6. Both surfaces stay coherent, in BOTH directions ------------------
// (a) The rail woke the Settings panel. Any listener other than the rail's own
//     stands in for the panel's subscription.
let settingsWakes = 0;
rig.globals.__spectrLatency.listeners.push(() => { settingsWakes += 1; });
if (chip && typeof chip.props.onClick === "function") chip.props.onClick();
check("pressing the rail chip wakes the Settings panel's subscription",
  settingsWakes > 0, `wakes = ${settingsWakes}`);

// (b) The other direction, which is the one the rail loses. The rail re-renders
//     ONLY when woken, so a Settings write that skips the listeners leaves the
//     chip showing the mode it used to be in.
const railWakesBefore = rig.renderRequests();
const store = rig.globals.__spectrLatency;
const publish = rig.fn.settingsPublish(store, store.state.mode, () => {});
const target = store.state.mode === "linear_phase" ? "zero_latency" : "linear_phase";
publish(target);
check("changing the mode from Settings wakes the rail chip",
  rig.renderRequests() > railWakesBefore,
  `render requests ${railWakesBefore} -> ${rig.renderRequests()} `
    + "-- the rail re-renders only when woken, so without this the bottom bar "
    + "keeps showing the mode and the latency it used to be in");
out = rig.render();
check("after a Settings change the chip shows the NEW mode's derived figure",
  textOf(out).includes(target === "zero_latency" ? "TRACKING" : "MIXING"),
  JSON.stringify(textOf(out)));

// ------------------------------------------------------------------ verdict

if (expectFail) {
  if (failures.length) {
    console.log("EXPECTED FAIL: the suite rejected this document (%d assertion%s)",
      failures.length, failures.length === 1 ? "" : "s");
    process.exit(0);
  }
  console.error("FAIL: the suite ACCEPTED a document it should have rejected "
    + "-- the assertions are not load bearing");
  process.exit(1);
}

if (failures.length) {
  console.error("\n%d check%s failed", failures.length, failures.length === 1 ? "" : "s");
  process.exit(1);
}
console.log("\nall checks passed");
process.exit(0);
