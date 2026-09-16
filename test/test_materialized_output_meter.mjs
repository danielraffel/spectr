#!/usr/bin/env node
// THE OUTPUT LEVEL READOUT SAYS HOW HOT, HOLDS IT, AND IS CLEARED BY A PERSON.
//
// Before this, Spectr's Output trim (param 2, +-24 dB, applied post-render)
// and its Mix appeared NOWHERE in the shipping editor document or its help
// text -- both were host-only -- while Pulp's VisualizationBridge had been
// computing peak and a sample-level `clipped` verdict on the post-trim buffer
// the whole time, with `Spectr::read_meter()`'s only caller being a test. So a
// user could not tell they were handing the host a hot signal, and could not
// do anything about it from the plug-in.
//
// TWO THINGS THIS SUITE REFUSES TO DO:
//
//   1. Assert that a label exists. "A readout is present" passes on a meter
//      wired to the wrong buffer, a hold that falls, and a latch that clears
//      itself. Every check below reads a NUMBER or a latched state out of the
//      component after driving it with real frames.
//   2. Read a lamp. The readout shows dBFS, and the label is OVER rather than
//      CLIP because Spectr is float end to end and clips nothing internally --
//      what it reports is that the signal LEAVING the plug-in passed full
//      scale. That wording is pinned here so it cannot drift back.
//
//   STATIC   one leaf, rendered by Chrome, subscribing to the native frame
//            rather than polling; the trim written through the existing
//            `param_set` verb at Spectr's own kOutputTrim id; OVER not CLIP.
//   RUNTIME  the leaf's own script block is evaluated in a vm with a hook
//            runtime and driven with published frames: the hold must rise and
//            never fall, OVER must latch across quiet frames, a click must
//            clear both, silence must read "--" and not "0.0", the trim must
//            reach the host as a param_set, and an externally published trim
//            must move the control.
//
// Usage:
//   node test_materialized_output_meter.mjs <materialized-document.runtime.json>
//        [--plant-falling-hold | --plant-no-latch | --plant-clip-label
//         | --plant-untyped-readout]
//        [--expect-fail]
//
// --plant-falling-hold makes the hold track every frame, so the loudest moment
// of a session disappears while nobody is looking -- the exact thing a peak
// hold exists to prevent, and invisible to any check that only reads the
// current frame.
// --plant-no-latch keeps the hold but lets OVER clear itself on the next quiet
// frame, so a transient overload is gone before anyone sees it.
// --plant-clip-label renames the reading CLIP, a claim about Spectr that is
// false.
// --plant-untyped-readout strips the trim readout's own face and size so it
// inherits the document body default again -- how it shipped: 47% taller ink
// than every other readout in the header, and a 37pt glyph run inside its own
// 34pt box at the trim extremes.
// --expect-fail inverts the verdict, so a control is green only when this
// suite REJECTS that document. The inversion lives here rather than in
// WILL_FAIL because WILL_FAIL accepts any non-zero exit -- a usage error or an
// unreadable file satisfied it and proved nothing.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantFallingHold = args.includes("--plant-falling-hold");
const plantNoLatch = args.includes("--plant-no-latch");
const plantClipLabel = args.includes("--plant-clip-label");
const plantUntypedReadout = args.includes("--plant-untyped-readout");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_output_meter.mjs <runtime.json> "
    + "[--plant-falling-hold|--plant-no-latch|--plant-clip-label"
    + "|--plant-untyped-readout] "
    + "[--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const RISING_HOLD = "      if (peak !== null && (hold.peakDb === null "
  + "|| peak > hold.peakDb))\n        hold.peakDb = peak;\n";
const LATCH = "      if (payload.over === true) hold.over = true;\n";
const LABEL = '(over ? "OVER " : "PEAK ") + peakText';
const READOUT_STYLE = 'style: { width: 34, textAlign: "right", '
  + 'whiteSpace: "nowrap", flexShrink: 0, fontFamily: "var(--mono)", '
  + 'fontSize: 10, color: "rgba(255,255,255,0.72)" }';

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

if (plantFallingHold) {
  plant("a hold that follows every frame", RISING_HOLD,
    "      if (peak !== null) hold.peakDb = peak;\n");
}
if (plantNoLatch) {
  plant("an OVER that clears itself", LATCH,
    "      hold.over = payload.over === true;\n");
}
if (plantClipLabel) {
  plant("a readout that claims Spectr clipped", LABEL,
    '(over ? "CLIP " : "PEAK ") + peakText');
}
if (plantUntypedReadout) {
  plant("a trim readout with no type of its own", READOUT_STYLE,
    'style: { width: 34, textAlign: "right", whiteSpace: "nowrap", '
    + "flexShrink: 0 }");
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

// ------------------------------------------------------- positive controls
const meterBody = balancedBody(html, "function SpectrOutputMeter(");
const chromeBody = balancedBody(html, "function Chrome({");
const controls = {
  "script blocks": scriptBlocks.length,
  "output meter": meterBody ? 1 : 0,
  "chrome": chromeBody ? 1 : 0,
  "zoom readout": html.split("function SpectrZoomReadout(").length - 1,
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

// S1. One leaf, rendered by Chrome. A component nothing renders is a component
// no user can see, and nothing else in this suite would notice.
if (html.split("function SpectrOutputMeter() {").length - 1 !== 1) {
  fail("the document does not declare exactly one SpectrOutputMeter");
}
if (!chromeBody.includes("React.createElement(SpectrOutputMeter, null)")) {
  fail("Chrome() does not render SpectrOutputMeter, so the readout exists in "
    + "the document and nowhere on screen");
}
// S1b. It must be the LAST child of Chrome's fragment, and absolutely
// positioned. This document's text/layout/paint bindings address their nodes
// by positional DOM path, so a child inserted anywhere but the end renumbers
// every later sibling and silently re-points them: appended into the header's
// flex row, "BARS" lost its captured 48pt text basis and remeasured at 20pt,
// shrinking its hit box. Only `native buttons are tappable across their whole
// painted bounds` can see that, so pin the shape that avoids it here.
if (!chromeBody.includes(
    "React.createElement(SpectrOutputMeter, null));")) {
  fail("SpectrOutputMeter is not the last child of Chrome's fragment; "
    + "inserting it earlier renumbers the captured binding paths of every "
    + "later sibling and silently breaks their text measurement");
}
// The bar it sits over declares zIndex 5. Without a higher one this cluster
// paints under it and every press inside the button resolves to the bar: a
// control that is fully visible and completely dead.
if (!/zIndex: 6/.test(meterBody)) {
  fail("the cluster does not outrank the top bar's own zIndex, so it is "
    + "painted but not pressable");
}
if (!/position: "absolute"/.test(meterBody) || !/left: 282/.test(meterBody)) {
  fail("the cluster is not absolutely placed in the header's measured gap "
    + "(x=281.7..669.5); in the flex row it either renumbers bindings or runs "
    + "off the 1320pt canvas");
}

// S2. Driven by the native publication, never polled. A poll would repaint the
// whole imported document on a timer for a value that barely moves.
if (!meterBody.includes('window.pulp.on("output_meter"')) {
  fail("the readout does not subscribe to the native output_meter frame");
}
if (/setInterval/.test(meterBody)) {
  fail("the readout polls on an interval; the level is published, so a poll "
    + "only adds commits");
}

// S3. The trim reaches the host through the flat param verb that already
// exists, at Spectr's own kOutputTrim id. An id typed by hand somewhere else
// would write a different parameter and look like it worked.
if (!meterBody.includes('window.pulp.postMessage("param_set",')) {
  fail("the trim control does not write through param_set");
}
if (!/id:\s*2\s*,\s*value: next/.test(meterBody)) {
  fail("the trim control does not write Spectr's kOutputTrim (id 2)");
}
if (!/min:\s*-24/.test(meterBody) || !/max:\s*24/.test(meterBody)) {
  fail("the trim control does not span the parameter's own +-24 dB range");
}

// S4. OVER, not CLIP. Spectr clips nothing internally.
if (!meterBody.includes('(over ? "OVER " : "PEAK ") + peakText')) {
  fail("the readout does not print OVER/PEAK with its number");
}
// The LABEL, not the prose: the component's own comment explains why it is
// not CLIP, so a bare /CLIP/ would reject the correct document.
if (/"CLIP /.test(meterBody)) {
  fail("the readout says CLIP. Spectr is float end to end and clips nothing "
    + "internally; the honest claim is that the signal handed to the host "
    + "passed full scale");
}
if (!meterBody.includes('"data-spectr-output-over"')) {
  fail("the over state is not exposed as an attribute, so nothing outside "
    + "the component can read it");
}

// S5. THE TRIM READOUT CARRIES ITS OWN TYPE, and it is the PEAK button's.
//
// This is the half of the cluster nobody looked at. The readout declared
// width/textAlign/whiteSpace/flexShrink and no font at all, so it inherited
// the document body default while its sibling 14pt away declared
// var(--mono)/10/rgba(255,255,255,0.72). Rastered through the shipping native
// editor (`Spectr-native-shot --backend=skia`, authored 1320x860 box) the
// glyph ink measured 11.0pt against 7.5pt for PEAK, LIVE, BARS, BOTH,
// `32 bands` and `1.00x zoom` alike -- 47% taller than every other readout in
// the row, taller than the SPECTR wordmark, and the brightest non-brand
// element in a header where it is the one control with no on-screen label.
//
// It also overflowed: at the extremes the inherited face painted `+12.0` as a
// 37pt run inside a 34pt box. `tools/appearance_invariants.py` is the detector
// for exactly that, and it cannot see this one -- it adjudicates a checked-in
// Settings dump and never sees the header.
//
// Asserted against the sibling's declarations rather than against literals, so
// a deliberate retype of the cluster moves both halves or fails here.
const peakFontFamily = /fontFamily: "([^"]+)"/.exec(meterBody);
const peakFontSize = /fontSize: (\d+)/.exec(meterBody);
const readoutStyle = /"data-spectr-output-trim-readout": true,[\s\S]{0,400}?style: \{([^}]*)\}/
  .exec(meterBody);
if (!peakFontFamily || !peakFontSize) {
  fail("the PEAK button declares no fontFamily/fontSize, so there is no "
    + "sibling treatment for the trim readout to match");
} else if (!readoutStyle) {
  fail("no style object found on the trim readout");
} else {
  const style = readoutStyle[1];
  const family = /fontFamily: "([^"]+)"/.exec(style);
  const size = /fontSize: (\d+)/.exec(style);
  if (!family || family[1] !== peakFontFamily[1]) {
    fail("the trim readout does not declare the PEAK button's font family "
      + `(${JSON.stringify(peakFontFamily[1])}); unstated it inherits the `
      + "document body face and renders in a different typeface from the "
      + "number beside it");
  }
  if (!size || size[1] !== peakFontSize[1]) {
    fail("the trim readout does not declare the PEAK button's font size "
      + `(${peakFontSize[1]}); unstated it inherits the document body size `
      + "and rendered 47% taller than every other readout in the header");
  }
  if (!/color: "/.test(style)) {
    fail("the trim readout declares no colour, so it inherits the body "
      + "default and outshines every other readout in the row");
  }
  // The box must hold the widest value the control can print. min/max are
  // +-24 and the format is one optional sign, two digits, a point and a
  // decimal -- five glyphs. A mono advance is ~0.6em, which is what made the
  // inherited face overflow this same 34pt box at 37pt.
  const width = /width: (\d+)/.exec(style);
  const declared = size ? Number(size[1]) : Number(peakFontSize[1]);
  const widest = 5 * declared * 0.6;
  if (!width) {
    fail("the trim readout declares no width, so its box cannot be checked "
      + "against the widest value it can print");
  } else if (Number(width[1]) < widest) {
    fail(`the trim readout's ${width[1]}pt box cannot hold the widest value `
      + `it can print ("-24.0", ~${widest.toFixed(1)}pt at ${declared}pt `
      + "mono); the run paints outside its own box");
  }
}

// ----------------------------------------------------------- runtime check
// Static text cannot tell a hold that rises from one that follows. Execute the
// leaf's own script block and drive it with frames.

const leafBlock = scriptBlocks.find((b) => b.includes("function SpectrOutputMeter("));
if (!leafBlock) {
  fail("no script block declares SpectrOutputMeter");
} else {
  const commits = [];
  const posted = [];
  const hooks = { slots: [], index: 0, effects: [] };
  let rerender = () => {};
  const React = {
    createElement: (type, props, ...children) => ({ type, props, children }),
    useState(initial) {
      const i = hooks.index++;
      if (hooks.slots.length <= i) {
        hooks.slots[i] = typeof initial === "function" ? initial() : initial;
      }
      const set = (next) => {
        const resolved = typeof next === "function" ? next(hooks.slots[i]) : next;
        if (Object.is(resolved, hooks.slots[i])) return;
        hooks.slots[i] = resolved;
        commits.push(resolved);
        rerender();
      };
      return [hooks.slots[i], set];
    },
    useEffect(fn) { hooks.effects.push({ fn }); },
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
  const pulp = {
    on(type, callback) {
      listeners.set(type, callback);
      return () => listeners.delete(type);
    },
    postMessage(type, payload) { posted.push({ type, payload }); return { ok: true }; },
  };
  const sandbox = {
    React, Math, Array, Object, Set, Map, String, Number, Boolean, JSON,
    isFinite, parseFloat,
    Date: { now: () => now },
    console: { log() {}, warn() {}, error(...a) { commits.push("error:" + a.join(" ")); } },
    requestAnimationFrame(fn) { fn(); return 1; },
    cancelAnimationFrame() {},
  };
  sandbox.globalThis = sandbox;
  sandbox.window = sandbox;
  sandbox.window.pulp = pulp;
  sandbox.pulp = pulp;
  sandbox.document = { querySelector: () => null, getElementById: () => null,
    addEventListener() {}, removeEventListener() {} };

  try {
    const context = vm.createContext(sandbox);
    vm.runInContext(leafBlock, context, { filename: "spectr-output-meter.js" });
    const Meter = context.SpectrOutputMeter;
    if (typeof Meter !== "function") {
      fail("SpectrOutputMeter is not reachable after evaluating its own "
        + "script block -- it was declared in another scope");
    } else {
      let element = null;
      const render = () => {
        hooks.index = 0;
        hooks.effects = [];
        element = Meter();
        return element;
      };
      rerender = () => { render(); };
      render();
      const cleanups = hooks.effects.map(({ fn }) => fn());

      // Walk the rendered tree for the two readouts. Reading the rendered
      // element rather than the hook slots is deliberate: what a person sees
      // is the only thing that matters here.
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
      const peakText = () => flatten(peakNode());
      const overFlag = () => peakNode() && peakNode().props["data-spectr-output-over"];
      const trimNode = () => find(element, (p) => p["data-spectr-output-trim"]);
      const trimText = () =>
        flatten(find(element, (p) => p["data-spectr-output-trim-readout"]));

      const publish = (payload) => {
        const cb = listeners.get("output_meter");
        if (cb) cb({ payload });
      };

      if (!listeners.has("output_meter")) {
        fail("the leaf never subscribed to output_meter, so nothing can ever "
          + "reach it and every reading below would be vacuous");
      }

      // R1. SILENCE READS "--", NOT "0.0". A meter that prints 0 dBFS for
      // silence is indistinguishable from one printing full scale.
      publish({ peak_db: null, over: false, trim_db: 0 });
      const silent = peakText();
      if (!silent.includes("--")) {
        fail(`silence reads ${JSON.stringify(silent)}, expected "--"`);
      }
      if (/0\.0/.test(silent)) {
        fail(`silence reads ${JSON.stringify(silent)}, which is a level`);
      }

      // R2. THE HOLD RISES.
      publish({ peak_db: -20.0, over: false, trim_db: 0 });
      if (!peakText().includes("-20.0")) {
        fail(`a -20 dBFS frame reads ${JSON.stringify(peakText())}`);
      }

      // R3. THE HOLD DOES NOT FALL. Sweep genuinely quieter frames, then
      // require the printed value to be unchanged -- and control the STIMULUS,
      // because "unchanged" is trivially true if the frames were all the same.
      commits.length = 0;
      const quiet = new Set();
      for (let i = 0; i < 40; i++) {
        const db = -30 - i * 0.7;
        quiet.add(db.toFixed(1));
        publish({ peak_db: db, over: false, trim_db: 0 });
      }
      if (quiet.size < 20) {
        fail(`the quiet sweep produced ${quiet.size} distinct levels, so "the `
          + 'hold did not move" proves nothing');
      }
      const heldAfterQuiet = peakText();
      if (!peakText().includes("-20.0")) {
        fail(`after ${quiet.size} quieter frames the hold reads `
          + `${JSON.stringify(peakText())}, expected it to still read -20.0 -- `
          + "the loudest moment must survive until someone clears it");
      }
      const quietCommits = commits.filter((c) => typeof c === "object").length;
      if (quietCommits !== 0) {
        fail(`${quietCommits} React commit(s) across ${quiet.size} quiet `
          + "frames; a hold that is not moving must cost nothing");
      }

      // R4. OVER LATCHES, AND SHOWS HOW FAR OVER.
      publish({ peak_db: 1.4, over: true, trim_db: 0 });
      if (overFlag() !== "true") {
        fail(`an over frame left data-spectr-output-over = `
          + `${JSON.stringify(overFlag())}`);
      }
      if (!peakText().startsWith("OVER") || !peakText().includes("+1.4")) {
        fail(`an over frame reads ${JSON.stringify(peakText())}, expected `
          + '"OVER +1.4" -- the number is the actionable part, not the state');
      }
      for (let i = 0; i < 40; i++) publish({ peak_db: -50, over: false, trim_db: 0 });
      const heldAfterOver = peakText();
      const overAfterQuiet = overFlag();
      if (overFlag() !== "true" || !peakText().includes("+1.4")) {
        fail(`40 quiet frames cleared the overload: reads `
          + `${JSON.stringify(peakText())} with over=${overFlag()}. A person `
          + "who looked away has no way to learn it happened.");
      }

      // R5. A PERSON CLEARS IT. That is the only event meaning "I saw this".
      peakNode().props.onClick();
      const afterClick = peakText();
      if (overFlag() !== "false" || !peakText().includes("--")) {
        fail(`after a click the readout is ${JSON.stringify(peakText())} with `
          + `over=${overFlag()}, expected a cleared "--"`);
      }
      // ...and it re-arms, or clearing would be a one-way off switch.
      publish({ peak_db: -6.0, over: false, trim_db: 0 });
      if (!peakText().includes("-6.0")) {
        fail(`after clearing, a new frame reads ${JSON.stringify(peakText())} `
          + "-- the readout does not re-arm");
      }

      // R6. THE TRIM REACHES THE HOST, at the right id, with the right value.
      posted.length = 0;
      trimNode().props.onChange({ target: { value: "6" } });
      const write = posted.find((p) => p.type === "param_set");
      if (!write) {
        fail("moving the trim wrote no param_set; the control is inert");
      } else if (write.payload.id !== 2 || write.payload.value !== 6) {
        fail(`the trim wrote ${JSON.stringify(write.payload)}, expected `
          + "{ id: 2, value: 6 }");
      }
      if (!trimText().includes("+6.0")) {
        fail(`after moving the trim the readout is ${JSON.stringify(trimText())}`);
      }
      // The range is the parameter's own, so a value past it is refused.
      posted.length = 0;
      trimNode().props.onChange({ target: { value: "99" } });
      const clamped = posted.find((p) => p.type === "param_set");
      if (!clamped || clamped.payload.value !== 24) {
        fail(`a +99 dB write was not clamped to the parameter's +24 dB `
          + `ceiling: ${JSON.stringify(clamped && clamped.payload)}`);
      }

      // R7. AN EXTERNALLY PUBLISHED TRIM MOVES THE CONTROL, so host automation
      // is visible -- but only once the local write has settled, or the round
      // trip would fight a drag in progress.
      publish({ peak_db: -6.0, over: false, trim_db: -12.0 });
      if (!trimText().includes("24.0")) {
        fail("a published trim moved the control while a local write was "
          + `still in flight: ${JSON.stringify(trimText())}`);
      }
      now += 1000;
      publish({ peak_db: -6.0, over: false, trim_db: -12.0 });
      if (!trimText().includes("-12.0")) {
        fail(`a trim published by the host did not move the control: `
          + `${JSON.stringify(trimText())}`);
      }

      for (const c of cleanups) if (typeof c === "function") c();
      if (listeners.has("output_meter")) {
        fail("unmount left the output_meter subscription attached");
      }
      console.log("runtime   silence=%s afterQuietSweep=%s(%d distinct) "
        + "afterOverThenQuiet=%s/%s afterClick=%s trim=%s",
        JSON.stringify(silent), JSON.stringify(heldAfterQuiet), quiet.size,
        JSON.stringify(heldAfterOver), String(overAfterQuiet),
        JSON.stringify(afterClick), JSON.stringify(trimText()));
    }
  } catch (error) {
    fail(`evaluating the readout's script block threw `
      + `${error.constructor.name}: ${error.message}`);
  }
}

// ----------------------------------------------------------------- verdict

const passed = failures.length === 0;
for (const f of failures) console.log("FAIL:", f);
if (passed) {
  console.log("PASS: the output readout holds its peak, latches OVER until a "
    + "person clears it, and its trim reaches kOutputTrim.");
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
