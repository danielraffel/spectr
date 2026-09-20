#!/usr/bin/env node
// THE OVER CHIP GOES OUT BY ITSELF; LATCHING IT IS A SETTING YOU TURN ON.
//
// Before this, the overload half of the output readout latched
// unconditionally: one transient and the chip stayed red until somebody
// clicked it -- past the fix, past the render, past the next twelve edits. A
// lamp that is red long after the condition ended stops being read as a
// condition and starts being read as decoration, which is how an indicator
// dies. The default is now a statement about NOW, and the old behaviour is
// kept whole behind `overLatch` for the person who leaves a mix rendering and
// wants to know afterwards whether it ever went over.
//
// WHAT THIS SUITE REFUSES TO DO. It never asserts that a setting EXISTS. A
// declared-and-unread setting, a row wired to the wrong key, and a default
// that says one thing while the code does another all pass "the toggle is
// present". Every check below either reads a rendered attribute out of the
// component after driving it with real frames and a controllable clock, or
// pins the exact wiring that carries the setting to it.
//
// THE FOUR THINGS IT EXISTS TO PROVE, each with its own plant:
//   (a) auto-clear actually clears -- no click, no publication, just time
//   (b) latch mode actually does NOT clear, over the same elapsed time
//   (c) the click still clears, in BOTH modes
//   (d) the shipping default is auto-clear, in the declared value AND in the
//       wiring that reads it (an absent key must resolve to auto-clear)
//
// AND THE TWO THAT MAKE (a) HONEST RATHER THAN MERELY TRUE:
//   * the chip stays red long enough to be SEEN. Edge-following would clear
//     on the next ~33ms tick, which is technically "only red when over" and
//     is invisible. --plant-blink is the control.
//   * a SUSTAINED overload does not blink. The window is measured from the
//     last over frame, not the first. --plant-no-refresh is the control.
//
// THE CLOCK IS LOAD BEARING, for the same reason as the sibling suite:
// `src/ui/native_editor.cpp` publishes only a CHANGED reading, so a steady
// level delivers no frames at all. Every clear below is driven by advancing
// TIME with nothing published under it -- the harsh case, and the one an
// implementation that waited for a falling-edge publication would hang in.
//
// Usage:
//   node test_materialized_over_auto_clear.mjs <materialized-document.runtime.json>
//        [--plant-always-latch | --plant-never-latch | --plant-default-latch
//         | --plant-wiring-default-latch | --plant-no-click-clear
//         | --plant-blink | --plant-stale-overpeak | --plant-no-refresh
//         | --plant-misrouted-setting | --plant-dead-switch
//         | --plant-inert-switch]
//        [--expect-fail]
//
// --plant-always-latch stops `expireOver` ever firing, which is the shipped
// behaviour: (a) must be rejected.
// --plant-never-latch makes the expiry ignore the mode, so the opt-in latch
// expires too: (b) must be rejected.
// --plant-default-latch flips the declared default to true: (d) must be
// rejected.
// --plant-wiring-default-latch keeps the declared default but reads it as
// `!== false`, so an absent key latches -- the same defect one layer down,
// and invisible to --plant-default-latch.
// --plant-no-click-clear drops the manual clear from `resetHold`: (c) must be
// rejected. Auto-clear must not have removed the way a person ends it early.
// --plant-blink zeroes the window, so the chip is red for one frame.
// --plant-stale-overpeak lets the lamp expire while the worst overshoot stays
// behind, so the tooltip and `data-spectr-output-over-peak` keep asserting an
// overload the chip says is not happening.
// --plant-no-refresh stamps only the FIRST over of a run, so a sustained
// overload goes dark while it is still going on.
// --plant-misrouted-setting wires the row to another key, so the toggle moves
// and the meter never hears about it.
// --plant-dead-switch strips the switch's own marker, so nothing driving the
// shipping app can find the control or read which mode it is in.
// --plant-inert-switch leaves the switch looking right and calling nothing.
// --expect-fail inverts the verdict, so a control is green only when this
// suite REJECTS that document.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const flag = (name) => args.includes(name);
const expectFail = flag("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_over_auto_clear.mjs <runtime.json> "
    + "[--plant-always-latch|--plant-never-latch|--plant-default-latch"
    + "|--plant-wiring-default-latch|--plant-no-click-clear|--plant-blink"
    + "|--plant-stale-overpeak|--plant-no-refresh|--plant-misrouted-setting"
    + "|--plant-dead-switch|--plant-inert-switch] "
    + "[--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

function plant(label, from, to) {
  const hits = html.split(from).length - 1;
  if (hits !== 1) {
    console.error(`FAIL: plant "${label}" found ${hits} sites, expected `
      + "exactly 1 -- the control cannot prove anything");
    process.exit(2);
  }
  html = html.replace(from, to);
  console.log("planted   %s", label);
}

const EXPIRY_WINDOW_TEST =
  "    if (nowMs - hold.overAt <= OVER_HOLD_MS) return;\n";
const OVER_EXPIRING =
  "  const overExpiring = (hold) => hold.over && !latchRef.current;\n";
const DECLARED_DEFAULT = '  "overLatch": false,\n';
const WIRING = "React.createElement(SpectrOutputMeter, "
  + "{ latchOver: settings.overLatch === true })";
const MANUAL_CLEAR =
  "    hold.over = false;\n    // Cleared by hand in BOTH modes.";
const WINDOW_CONST = "  const OVER_HOLD_MS = PEAK_HOLD_MS;\n";
const EXPIRY_TAIL =
  "    hold.overAt = 0;\n    hold.overPeakDb = null;\n  };\n";
const REFRESH = "        hold.overAt = now;\n";
const ROW_WIRING = "onChange: (v) => persist({ overLatch: v })";
const SWITCH_MARKER =
  '      "data-spectr-over-latch-toggle": overLatch ? "true" : void 0,\n';
const SWITCH_CLICK = "      onClick: () => onChange(!value),\n";

if (flag("--plant-always-latch")) {
  plant("an overload that never expires", EXPIRY_WINDOW_TEST,
    "    if (true) return;\n");
}
if (flag("--plant-never-latch")) {
  plant("an expiry that ignores the latch setting", OVER_EXPIRING,
    "  const overExpiring = (hold) => hold.over;\n");
}
if (flag("--plant-default-latch")) {
  plant("a declared default that latches", DECLARED_DEFAULT,
    '  "overLatch": true,\n');
}
if (flag("--plant-wiring-default-latch")) {
  plant("wiring that latches when the key is absent", WIRING,
    "React.createElement(SpectrOutputMeter, "
      + "{ latchOver: settings.overLatch !== false })");
}
if (flag("--plant-no-click-clear")) {
  plant("a click that no longer clears", MANUAL_CLEAR,
    "    // Cleared by hand in BOTH modes.");
}
if (flag("--plant-blink")) {
  plant("an overload nobody can see", WINDOW_CONST,
    "  const OVER_HOLD_MS = 0;\n");
}
if (flag("--plant-stale-overpeak")) {
  plant("a worst overshoot that outlives its lamp", EXPIRY_TAIL,
    "    hold.overAt = 0;\n  };\n");
}
if (flag("--plant-no-refresh")) {
  plant("a window the overload cannot refresh", REFRESH,
    "        if (hold.overAt === 0) hold.overAt = now;\n");
}
if (flag("--plant-dead-switch")) {
  plant("a switch nothing can find or read", SWITCH_MARKER, "");
}
if (flag("--plant-inert-switch")) {
  plant("a switch that writes nothing", SWITCH_CLICK,
    "      onClick: () => {},\n");
}
if (flag("--plant-misrouted-setting")) {
  plant("a settings row wired to another key", ROW_WIRING,
    "onChange: (v) => persist({ statusInfo: v })");
}

const failures = [];
const fail = (msg) => failures.push(msg);

function balancedBody(source, header) {
  const at = source.indexOf(header);
  if (at < 0) return null;
  const paren = source.indexOf("(", at);
  if (paren < 0) return null;
  let parenDepth = 0;
  let close = -1;
  for (let i = paren; i < source.length; i++) {
    if (source[i] === "(") parenDepth++;
    else if (source[i] === ")") {
      parenDepth--;
      if (parenDepth === 0) { close = i; break; }
    }
  }
  if (close < 0) return null;
  const open = source.indexOf("{", close);
  if (open < 0) return null;
  let depth = 0;
  for (let i = open; i < source.length; i++) {
    const ch = source[i];
    if (ch === "{") depth++;
    else if (ch === "}") {
      depth--;
      if (depth === 0) return source.slice(open, i + 1);
    }
  }
  return null;
}

const scriptBlocks = [...html.matchAll(
  /<script(?: type="text\/javascript")?>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const meterBody = balancedBody(html, "function SpectrOutputMeter(");
const chromeBody = balancedBody(html, "function Chrome({");
const defaultsBlock = (/\/\*EDITMODE-BEGIN\*\/([\s\S]*?)\/\*EDITMODE-END\*\//
  .exec(html) || [])[1];

// ------------------------------------------------------- positive controls
//
// Every check below reads one of these. A suite that cannot find them is
// reading the wrong document, and must say so rather than return a verdict.
const controls = {
  "script blocks": scriptBlocks.length,
  "output meter": meterBody ? 1 : 0,
  "chrome": chromeBody ? 1 : 0,
  "tweak defaults": defaultsBlock ? 1 : 0,
  "feedback group": html.split('title: "FEEDBACK"').length - 1,
  "settings toggle": html.split("function SpectrSettingsToggle(").length - 1,
};
for (const [label, count] of Object.entries(controls)) {
  console.log("control   %s %s", label.padEnd(16), count);
}
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length) {
  console.error("FAIL: the payload has no " + blind.join(", ")
    + " -- this suite is reading the wrong document or the editor was "
    + "restructured, so it cannot render a verdict");
  process.exit(2);
}

// ------------------------------------------------------------ static checks

// S1. THE SHIPPING DEFAULT IS AUTO-CLEAR, as a declared value.
if (!/"overLatch":\s*false/.test(defaultsBlock)) {
  fail('the tweak-defaults block does not declare "overLatch": false, so the '
    + "editor opens in whichever mode the absent key happens to resolve to");
}
if (/"overLatch":\s*true/.test(defaultsBlock)) {
  fail("the declared default latches the overload, which is the behaviour "
    + "being retired, not the one being kept behind a setting");
}

// S2. ...AND IN THE WIRING THAT READS IT. `=== true`, never `!== false`: a
// settings blob stored before this setting existed has no `overLatch` key at
// all, and must open un-latched rather than inheriting the old behaviour.
// S1 cannot see this -- the declared default would still say false.
if (!chromeBody.includes(WIRING)) {
  fail("Chrome does not hand the meter `latchOver: settings.overLatch === "
    + "true`; either the setting never reaches the readout, or an absent key "
    + "resolves to the latch that is being retired");
}

// S3. THE ROW IS REAL AND WIRED TO THE SAME KEY. A row bound to another key
// moves a toggle that changes nothing the meter reads.
const rowMatch =
  /React\.createElement\(SpectrSettingsField, \{ label: "OVER latch",[\s\S]{0,400}?\)\)/
    .exec(html);
if (!rowMatch) {
  fail("there is no settings row for the overload latch, so the behaviour "
    + "being kept has no way to be turned on");
} else {
  const row = rowMatch[0];
  if (!row.includes("value: settings.overLatch === true")) {
    fail("the latch row does not read `settings.overLatch === true`, so the "
      + "switch does not show the mode actually in force");
  }
  if (!row.includes(ROW_WIRING)) {
    fail("the latch row does not persist `overLatch`; the toggle moves and "
      + "nothing the meter reads changes");
  }
}

// S3b. THE ROW IS APPENDED LAST INSIDE ITS GROUP. Not cosmetic: this
// document's text/layout/paint bindings address nodes by POSITIONAL DOM path,
// so a row inserted anywhere but the end renumbers every later sibling and
// silently re-points them at the wrong node.
if (!/label: "OVER latch"[\s\S]{0,400}?\}\)\)\), React\.createElement\(SpectrModulationSettings/
    .test(html)) {
  fail("the latch row is not the last child of the FEEDBACK group; inserted "
    + "before a sibling it renumbers the positional binding paths");
}

// S4. THE PROBE ATTRIBUTE STILL TRACKS THE LAMP. The acceptance suite and
// every native probe read `data-spectr-output-over`, and they must be reading
// the same flag the chip paints -- not a second notion of "shown over" that
// can disagree with it.
if (!/const over = reading\.over === true;/.test(meterBody)) {
  fail("the rendered OVER state is no longer read straight off the reading");
}
if (!meterBody.includes('"data-spectr-output-over": over ? "true" : "false"')) {
  fail("the over attribute is no longer derived from the rendered flag, so a "
    + "probe and a person can see different things");
}

// S5. THE CLEAR IS TIMED, NOT EDGE-FOLLOWED. `hold.over = payload.over` would
// be the obvious implementation and is wrong twice over: a one-tick overload
// would be invisible, and a dropped falling edge -- the publisher wraps its
// `load_script` in a catch that logs and swallows -- would leave the chip red
// forever, which is the defect being fixed wearing the fix's clothes.
if (/hold\.over = payload\.over/.test(meterBody)) {
  fail("the overload follows the published edge directly; a single-tick over "
    + "is then never seen, and a dropped falling edge is permanent red");
}
if (!/const OVER_HOLD_MS =/.test(meterBody)) {
  fail("the overload declares no hold window of its own");
}

// ----------------------------------------------------------- runtime checks

const leafBlock = scriptBlocks.find((b) =>
  b.includes("function SpectrOutputMeter("));
if (!leafBlock) {
  fail("no script block declares SpectrOutputMeter");
} else {
  // A rig per instance: fresh hook slots, its own frame queue and clock, so
  // the two modes are compared over the same stimulus without either one
  // inheriting the other's ballistic.
  const makeRig = (initialProps) => {
    const commits = [];
    const hooks = { slots: [], index: 0, effects: [], effectIndex: 0 };
    let props = initialProps;
    let element = null;
    let rerender = () => {};
    const React = {
      createElement: (type, p, ...children) => ({ type, props: p, children }),
      useState(initial) {
        const i = hooks.index++;
        if (hooks.slots.length <= i) {
          hooks.slots[i] = typeof initial === "function" ? initial() : initial;
        }
        const set = (next) => {
          const resolved =
            typeof next === "function" ? next(hooks.slots[i]) : next;
          if (Object.is(resolved, hooks.slots[i])) return;
          hooks.slots[i] = resolved;
          commits.push(resolved);
          rerender();
        };
        return [hooks.slots[i], set];
      },
      // DEPS-AWARE, unlike the sibling suite's shim. The mode-change effect is
      // the whole mechanism behind "turn the latch off and a stuck chip is
      // released", so an effect runner that fires once at mount could not
      // observe it -- and this suite would pass without ever exercising it.
      useEffect(fn, deps) {
        const i = hooks.effectIndex++;
        hooks.effects.push({ fn, deps, index: i });
      },
      useRef(initial) {
        const i = hooks.index++;
        if (hooks.slots.length <= i) hooks.slots[i] = { current: initial };
        return hooks.slots[i];
      },
      useMemo(fn) { return fn(); },
      useCallback(fn) { return fn; },
    };
    const listeners = new Map();
    let now = 1000000;
    let frameSeq = 1;
    const frames = new Map();
    const FRAME_MS = 1000 / 60;
    const pulp = {
      on(type, callback) {
        listeners.set(type, callback);
        return () => listeners.delete(type);
      },
      postMessage() { return { ok: true }; },
    };
    const sandbox = {
      React, Math, Array, Object, Set, Map, String, Number, Boolean, JSON,
      isFinite, parseFloat,
      Date: { now: () => now },
      console: { log() {}, warn() {},
        error(...a) { commits.push("error:" + a.join(" ")); } },
      requestAnimationFrame(fn) {
        const id = frameSeq++;
        frames.set(id, fn);
        return id;
      },
      cancelAnimationFrame(id) { frames.delete(id); },
    };
    sandbox.globalThis = sandbox;
    sandbox.window = sandbox;
    sandbox.window.pulp = pulp;
    sandbox.pulp = pulp;
    sandbox.document = { querySelector: () => null, getElementById: () => null,
      addEventListener() {}, removeEventListener() {} };

    const context = vm.createContext(sandbox);
    // Evaluating the block is itself the parse proof for this component.
    // `node --check` exits 0 on files it cannot run; this does not.
    vm.runInContext(leafBlock, context, { filename: "spectr-output-meter.js" });
    const Meter = context.SpectrOutputMeter;
    if (typeof Meter !== "function") return null;

    const ran = new Map();
    const flushEffects = () => {
      for (const { fn, deps, index } of hooks.effects) {
        const previous = ran.get(index);
        const unchanged = previous && deps && previous.deps
          && previous.deps.length === deps.length
          && previous.deps.every((d, i) => Object.is(d, deps[i]));
        if (unchanged) continue;
        if (previous && typeof previous.cleanup === "function") {
          previous.cleanup();
        }
        ran.set(index, { deps, cleanup: fn() });
      }
    };
    const render = () => {
      hooks.index = 0;
      hooks.effectIndex = 0;
      hooks.effects = [];
      element = props === undefined ? Meter() : Meter(props);
      return element;
    };
    rerender = () => { render(); };
    render();
    flushEffects();

    const find = (node, pred) => {
      if (!node || typeof node !== "object") return null;
      if (node.props && pred(node.props)) return node;
      for (const child of node.children || []) {
        const hit = find(child, pred);
        if (hit) return hit;
      }
      return null;
    };
    const flatten = (node) => {
      if (node == null || node === false) return "";
      if (typeof node !== "object") return String(node);
      return (node.children || []).map(flatten).join("");
    };
    const peakNode = () => find(element, (p) => p["data-spectr-output-peak"]);
    return {
      context,
      commits,
      frames,
      subscribed: () => listeners.has("output_meter"),
      peakText: () => flatten(peakNode()),
      overFlag: () => peakNode() && peakNode().props["data-spectr-output-over"],
      overPeak: () => peakNode().props["data-spectr-output-over-peak"],
      click: () => peakNode().props.onClick(),
      setProps: (next) => { props = next; render(); flushEffects(); },
      publish: (payload) => {
        const cb = listeners.get("output_meter");
        if (cb) cb({ payload });
      },
      // Advance the clock and run whatever frames fall due, PUBLISHING
      // NOTHING. The native side publishes only a CHANGED reading, so this is
      // the realistic case as well as the harsh one.
      advance: (ms) => {
        const until = now + ms;
        while (now < until) {
          now = Math.min(until, now + FRAME_MS);
          const due = [...frames.values()];
          frames.clear();
          for (const fn of due) fn(now);
        }
      },
    };
  };

  const windowMs = Number(
    (/const OVER_HOLD_MS = PEAK_HOLD_MS/.test(meterBody)
      ? (/const PEAK_HOLD_MS = (\d+)/.exec(meterBody) || [])[1]
      : (/const OVER_HOLD_MS = (\d+)/.exec(meterBody) || [])[1]));
  if (!isFinite(windowMs)) {
    fail("the overload's hold window cannot be read out of the component, so "
      + "every timing check below would be vacuous");
  }
  console.log("measured  over window %sms", windowMs);

  const LOUD = 1.4;
  const LIVE = -40.0;
  // A HUMAN quantity, deliberately NOT derived from the window under test.
  // Timing the stimulus off the constant makes the check blind to that
  // constant collapsing: with a 0ms window, `advance(windowMs * 0.4)`
  // advances nothing and the chip is trivially still red.
  const GLANCE_MS = 800;
  // Long enough that any window inside the readable range has expired, and
  // short enough to stay a statement about this window rather than about
  // "eventually".
  const PAST_WINDOW_MS = 6000;

  try {
    // ---------------------------------------------------------------- (a)
    // R1. AUTO-CLEAR: the chip goes out on TIME, with no click and nothing
    // published under it.
    const auto = makeRig({ latchOver: false });
    if (!auto) {
      fail("SpectrOutputMeter is not reachable after evaluating its own "
        + "script block -- it was declared in another scope");
    } else {
      if (!auto.subscribed()) {
        fail("the leaf never subscribed to output_meter, so every reading "
          + "below would be vacuous");
      }
      auto.publish({ peak_db: LOUD, over: true, trim_db: 0 });
      if (auto.overFlag() !== "true") {
        fail(`an over frame did not light the chip (over=${auto.overFlag()})`);
      }
      if (!auto.peakText().startsWith("OVER")) {
        fail(`an over frame reads ${JSON.stringify(auto.peakText())}`);
      }
      if (auto.overPeak() !== "1.4") {
        fail(`the worst overshoot reads ${JSON.stringify(auto.overPeak())}, `
          + 'expected "1.4"');
      }
      // The signal comes back under, and then NOTHING is published again --
      // exactly how the native publisher behaves once a reading stops moving.
      auto.publish({ peak_db: LIVE, over: false, trim_db: 0 });

      // R1a. IT STAYS RED LONG ENOUGH TO BE SEEN. This is what separates a
      // hold window from edge-following, and it is the check --plant-blink
      // exists to fail: a chip that is correct for 16ms reports nothing.
      auto.advance(GLANCE_MS);
      if (auto.overFlag() !== "true") {
        fail(`${GLANCE_MS}ms after a transient overload the chip has already `
          + "gone out; an over you were not looking at is unreportable");
      }

      // R1b. ...AND THEN IT GOES OUT BY ITSELF.
      auto.advance(PAST_WINDOW_MS);
      if (auto.overFlag() !== "false") {
        fail(`${GLANCE_MS + PAST_WINDOW_MS}ms after the last over frame the `
          + `chip is still red (over=${auto.overFlag()}) with nothing `
          + "published and nobody clicking; this is the defect being fixed");
      }
      if (auto.peakText().startsWith("OVER")) {
        fail(`the chip still reads ${JSON.stringify(auto.peakText())} after `
          + "its window elapsed");
      }
      // R1c. THE WORST OVERSHOOT GOES WITH IT. A cleared lamp beside a
      // retained "worst +1.4 dBFS" asserts an overload the lamp denies, and
      // that number is what the acceptance suite reads.
      if (auto.overPeak() !== "") {
        fail(`the lamp cleared but the worst overshoot is still `
          + `${JSON.stringify(auto.overPeak())}; the tooltip and the probe `
          + "attribute then claim an overload the chip says is not happening");
      }
      // R1d. CLEARING THE LAMP DID NOT BLANK THE METER. The signal is still
      // there and nothing will republish it.
      if (!auto.peakText().includes(LIVE.toFixed(1))) {
        fail(`after the lamp cleared the readout is `
          + `${JSON.stringify(auto.peakText())}, expected the live `
          + `${LIVE.toFixed(1)} -- the clear took the level with it`);
      }
      // R1e. AND IT COSTS NOTHING ONCE SETTLED. A pump that keeps running is
      // a permanent per-frame animation in the header.
      auto.commits.length = 0;
      auto.advance(2000);
      if (auto.frames.size !== 0) {
        fail(`${auto.frames.size} frame callback(s) still scheduled after the `
          + "chip cleared; the auto-clear pump does not self-terminate");
      }
      if (auto.commits.filter((c) => typeof c === "object").length !== 0) {
        fail("a settled readout still commits React with nothing published");
      }

      // R1f. A SUSTAINED OVERLOAD DOES NOT BLINK. The window is measured from
      // the LAST over frame; a run of them must keep the chip red throughout,
      // not re-light it every window.
      for (let i = 0; i < 6; i++) {
        auto.publish({ peak_db: LOUD + i * 0.1, over: true, trim_db: 0 });
        auto.advance(windowMs * 0.6);
        if (auto.overFlag() !== "true") {
          fail(`a sustained overload went dark at frame ${i} while it was `
            + "still going on; the window is not refreshed by later overs");
          break;
        }
      }
      // ...and a fresh over after a clear measures its own worst overshoot
      // rather than inheriting the retired one.
      auto.publish({ peak_db: LIVE, over: false, trim_db: 0 });
      auto.advance(PAST_WINDOW_MS);
      auto.publish({ peak_db: 0.2, over: true, trim_db: 0 });
      if (auto.overPeak() !== "0.2") {
        fail(`a new overload after an auto-clear reports a worst overshoot of `
          + `${JSON.stringify(auto.overPeak())}, expected "0.2" -- it is `
          + "still carrying the retired report's number");
      }
    }

    // ---------------------------------------------------------------- (b)
    // R2. LATCH MODE: the same stimulus, the same elapsed time, and it must
    // NOT clear. Opt-in behaviour that quietly behaves like the default is
    // not a setting.
    const latched = makeRig({ latchOver: true });
    if (latched) {
      latched.publish({ peak_db: LOUD, over: true, trim_db: 0 });
      latched.publish({ peak_db: LIVE, over: false, trim_db: 0 });
      latched.advance(GLANCE_MS + PAST_WINDOW_MS);
      if (latched.overFlag() !== "true") {
        fail(`with the latch ON the chip cleared itself after `
          + `${GLANCE_MS + PAST_WINDOW_MS}ms (over=${latched.overFlag()}); a `
          + "person who looked away has no other way to learn it happened");
      }
      if (!latched.peakText().startsWith("OVER")) {
        fail(`with the latch ON the chip reads `
          + `${JSON.stringify(latched.peakText())} after the same elapsed `
          + "time");
      }
      if (latched.overPeak() !== "1.4") {
        fail("with the latch ON the worst overshoot was dropped; latched, it "
          + "means worst since you last cleared");
      }
      // ------------------------------------------------------------- (c/2)
      // R2b. THE CLICK STILL CLEARS WITH THE LATCH ON -- that is what it is
      // for.
      latched.click();
      if (latched.overFlag() !== "false") {
        fail(`a click did not clear the latched overload `
          + `(over=${latched.overFlag()})`);
      }
      if (latched.overPeak() !== "") {
        fail("a click left the worst overshoot behind in latch mode");
      }
      // ...and it clears the BALLISTIC, not merely the rendered state.
      // `resetHold` commits `over: false` directly, so a clear that forgot to
      // reset the hold itself looks identical until the next ordinary frame
      // re-commits from it -- and the chip then goes red with no overload
      // under it. Only a publication after the click can see that.
      latched.publish({ peak_db: LIVE, over: false, trim_db: 0 });
      if (latched.overFlag() !== "false") {
        fail(`an ordinary frame after a click re-lit the chip `
          + `(over=${latched.overFlag()}) with no overload; the click cleared `
          + "the rendered state but not the hold behind it");
      }
    }

    // ---------------------------------------------------------------- (c/1)
    // R3. THE CLICK STILL CLEARS IN AUTO MODE TOO, before the window would
    // have elapsed. Auto-clear shortens the wait; it must not remove the way
    // a person ends the report early.
    const clicked = makeRig({ latchOver: false });
    if (clicked) {
      clicked.publish({ peak_db: LOUD, over: true, trim_db: 0 });
      if (clicked.overFlag() !== "true") {
        fail("the click rig never lit, so the clear below proves nothing");
      }
      clicked.advance(windowMs * 0.2);
      clicked.click();
      if (clicked.overFlag() !== "false") {
        fail(`a click did not clear the overload in auto-clear mode `
          + `(over=${clicked.overFlag()})`);
      }
      if (clicked.overPeak() !== "") {
        fail("a click left the worst overshoot behind in auto-clear mode");
      }
      clicked.publish({ peak_db: LIVE, over: false, trim_db: 0 });
      if (clicked.overFlag() !== "false") {
        fail(`an ordinary frame after a click re-lit the chip in auto-clear `
          + `mode (over=${clicked.overFlag()})`);
      }
    }

    // ---------------------------------------------------------------- (d)
    // R4. THE DEFAULT IS AUTO-CLEAR AT RUNTIME, not only as a declared
    // value. Rendered with NO props at all -- which is what an absent
    // `overLatch` key resolves to -- the chip must still go out by itself.
    const bare = makeRig(undefined);
    if (bare) {
      bare.publish({ peak_db: LOUD, over: true, trim_db: 0 });
      if (bare.overFlag() !== "true") {
        fail("the default-mode rig never lit, so the clear below is vacuous");
      }
      bare.publish({ peak_db: LIVE, over: false, trim_db: 0 });
      bare.advance(GLANCE_MS + PAST_WINDOW_MS);
      if (bare.overFlag() !== "false") {
        fail("with no `latchOver` prop at all the chip latched; the default "
          + "must be the auto-clear the user asked for, not the old behaviour");
      }
    }

    // R5. TURNING THE LATCH OFF RELEASES A CHIP THAT IS ALREADY STUCK ON.
    // The native side owes this component no further frame while the signal
    // is steady, so the mode change has to drive its own commit -- otherwise
    // the setting appears not to work until the next transient.
    const flipped = makeRig({ latchOver: true });
    if (flipped) {
      flipped.publish({ peak_db: LOUD, over: true, trim_db: 0 });
      flipped.publish({ peak_db: LIVE, over: false, trim_db: 0 });
      flipped.advance(GLANCE_MS + PAST_WINDOW_MS);
      if (flipped.overFlag() !== "true") {
        fail("the flip rig did not latch, so the release below is vacuous");
      }
      flipped.setProps({ latchOver: false });
      flipped.advance(200);
      if (flipped.overFlag() !== "false") {
        fail(`turning the latch off left the chip red `
          + `(over=${flipped.overFlag()}) with nothing published and nobody `
          + "clicking; the setting does not take effect until a new overload");
      }
    }
    // R6. THE SWITCH IS LIVE. A row whose control renders no state and calls
    // nothing looks right in every screenshot and does nothing. Driven
    // directly rather than by mounting the whole settings panel, which would
    // prove less about this one control and take much longer to say it.
    const toggleRig = makeRig({ latchOver: false });
    if (toggleRig) {
      const Toggle = toggleRig.context.SpectrSettingsToggle;
      if (typeof Toggle !== "function") {
        fail("SpectrSettingsToggle is not reachable, so the row's control is "
          + "never driven and the static checks above pin text nobody runs");
      } else {
        const wrote = [];
        const off = Toggle({ value: false, overLatch: true,
          onChange: (v) => wrote.push(v) });
        if (off.props["data-spectr-over-latch-toggle"] !== "true") {
          fail("the latch switch does not mark itself the way its neighbours "
            + "do, so nothing driving the shipping app can find it");
        }
        if (off.props["data-spectr-over-latch-state"] !== "off") {
          fail("the latch switch does not report `off` for a false value, so "
            + "its state cannot be read from outside");
        }
        off.props.onClick();
        const on = Toggle({ value: true, overLatch: true, onChange() {} });
        if (on.props["data-spectr-over-latch-state"] !== "on") {
          fail("the latch switch does not report `on` for a true value");
        }
        if (wrote.length !== 1 || wrote[0] !== true) {
          fail(`pressing the latch switch wrote ${JSON.stringify(wrote)}, `
            + "expected [true] -- the control is inert");
        }
      }
    }
  } catch (error) {
    fail(`driving the readout threw ${error.constructor.name}: `
      + `${error.message}`);
  }
}

// ----------------------------------------------------------------- verdict

const passed = failures.length === 0;
for (const f of failures) console.log("FAIL:", f);
if (passed) {
  console.log("PASS: the overload chip is red while the signal is over and "
    + "goes out by itself afterwards, `overLatch` keeps it until a click, the "
    + "click clears in both modes, and the shipping default is auto-clear.");
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
