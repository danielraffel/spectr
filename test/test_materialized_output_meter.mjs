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
//            runtime, a real frame queue and a controllable clock, then
//            driven with published frames: the number must rise instantly,
//            stay readable for the hold window, then FALL to the live level;
//            OVER must survive that same elapsed time; a click must clear the
//            latch without blanking a meter that has signal in it; silence
//            must read "--" and not "0.0"; the trim must reach the host as a
//            param_set; and an externally published trim must move the
//            control.
//
// THIS SUITE DRIVES THE LEAF WITH `latchOver: true`. The overload half is a
// mode now: by default the chip goes out on its own window, and `overLatch`
// keeps the hold-until-clicked behaviour every runtime check below was
// written against. Pinning the latch here keeps those checks measuring the
// thing they describe; the DEFAULT, the mode split and the settings wiring
// are the subject of `test_materialized_over_auto_clear.mjs`, which is the
// suite that would notice if auto-clear stopped clearing.
//
// THE SPLIT THIS SUITE EXISTS TO HOLD. The number and the overload used to be
// one welded value, held forever. A level that only ever rises cannot show
// that the signal is alive -- a held reading and a frozen plug-in look
// identical, which is what a user asked about. An overload that decayed would
// hide the event it exists to catch. So they decay differently, and both
// halves get their own plant: a number that does not fall must be rejected,
// and an OVER that does fall must be rejected.
//
// THE CLOCK IS LOAD BEARING. The native side publishes only a CHANGED
// reading, so a steady tone publishes nothing at all. Every fall below is
// therefore driven by advancing TIME with no further publication -- the
// harshest case, and the one a frame-driven decay would freeze in.
//
// Usage:
//   node test_materialized_output_meter.mjs <materialized-document.runtime.json>
//        [--plant-no-decay | --plant-two-clock-reads
//         | --plant-decaying-latch | --plant-no-hold-window
//         | --plant-no-latch | --plant-clip-label | --plant-untyped-readout]
//        [--expect-fail]
//
// --plant-no-decay welds the two halves back together: the number holds
// forever, which is the state that shipped. Nothing that reads a single frame
// can see it -- only elapsed time with no publication under it can.
// --plant-two-clock-reads restores the version measured on the shipping
// standalone, where the pump read Date.now() separately for its commit and for
// its keep-falling decision. The fall then stops up to a frame above the live
// level and STAYS there -- -11.1 against a -11.5 signal. The runtime section
// is structurally blind to it, because its clock does not advance between two
// calls inside one frame; only the static shape can see it.
// --plant-decaying-latch welds them together the other way: OVER falls with
// the number, so an overload is gone once the reading has come back down and
// a person who looked away never learns it happened.
// --plant-no-hold-window removes the hold WINDOW, so the number follows every
// frame instantly. The fall is still there, so --plant-no-decay cannot see
// this one: a transient is simply unreadable because it was never held.
// --plant-no-latch keeps the number's ballistic but lets OVER clear itself on
// the next quiet frame, so a transient overload is gone before anyone sees
// it. A different mechanism from --plant-decaying-latch, and a different
// assertion: that one clears on TIME, this one on a FRAME.
// --plant-clip-label renames the reading CLIP, a claim about Spectr that is
// false.
// --plant-untyped-readout strips the trim readout's own face and size so it
// inherits the document body default again -- how it shipped: 47% taller ink
// than every other readout in the header, and a 37pt glyph run inside its own
// 34pt box at the trim extremes.
// --plant-unlabelled-trim removes the visible OUTPUT label, restoring the
// state that shipped: a slider next to a meter carrying only aria-label and
// title, neither of which a DAW ever draws.
// --plant-short-track restores the 58pt track, which under the runtime's
// fixed ~20pt thumb resolved 0.83 dB per point -- a 0.5 dB step roughly every
// device pixel at 2x, which is not adjustment.
// --expect-fail inverts the verdict, so a control is green only when this
// suite REJECTS that document. The inversion lives here rather than in
// WILL_FAIL because WILL_FAIL accepts any non-zero exit -- a usage error or an
// unreadable file satisfied it and proved nothing.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantNoDecay = args.includes("--plant-no-decay");
const plantTwoClockReads = args.includes("--plant-two-clock-reads");
const plantDecayingLatch = args.includes("--plant-decaying-latch");
const plantNoHoldWindow = args.includes("--plant-no-hold-window");
const plantNoLatch = args.includes("--plant-no-latch");
const plantClipLabel = args.includes("--plant-clip-label");
const plantUntypedReadout = args.includes("--plant-untyped-readout");
const plantUnlabelledTrim = args.includes("--plant-unlabelled-trim");
const plantShortTrack = args.includes("--plant-short-track");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_output_meter.mjs <runtime.json> "
    + "[--plant-no-decay|--plant-two-clock-reads|--plant-decaying-latch"
    + "|--plant-no-hold-window"
    + "|--plant-no-latch|--plant-clip-label|--plant-untyped-readout"
    + "|--plant-unlabelled-trim|--plant-short-track] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

// The fall itself: strip it and the number holds forever, which is the state
// that shipped.
const FALL = "    const fallen = hold.holdDb\n"
  + "      - PEAK_FALL_DB_PER_SEC * (since - PEAK_HOLD_MS) / 1000;\n";
// The hold WINDOW, separately. Planted by zeroing the constant rather than by
// deleting the early return: deleting it leaves `since - PEAK_HOLD_MS`
// negative for the first two seconds, so the readout would climb ABOVE the
// peak it just measured -- a different and cruder defect, which would let this
// control pass on an assertion that is not the one it claims to make.
const HOLD_WINDOW = "  const PEAK_HOLD_MS = 2000;\n";
const LATCH = "      if (payload.over === true) {\n        hold.over = true;\n";
// The latch read back out at render time. Tying it to the reading's own level
// is how an overload would decay WITH the number.
const LATCH_RENDER = "  const over = reading.over === true;\n";
const LABEL = '(over ? "OVER " : "PEAK ") + peakText';
const READOUT_STYLE = 'style: { width: 34, textAlign: "right", '
  + 'whiteSpace: "nowrap", flexShrink: 0, fontFamily: "var(--mono)", '
  + 'fontSize: 10, color: "rgba(255,255,255,0.72)" }';
const TRACK_STYLE = 'style: { width: 156, flexShrink: 0, '
  + 'accentColor: "hsl(200,80%,60%)" }';
const TRIM_LABEL_ELEMENT = '    /* @__PURE__ */ React.createElement("span", {\n'
  + '      "data-spectr-output-trim-label": true,\n'
  + '      // Declares its own face, size, tracking and colour. A control that\n'
  + '      // declares no type inherits the document body default, which is\n'
  + '      // how the readout beside it shipped 47% taller than every other\n'
  + '      // readout in this header.\n'
  + '      style: { fontFamily: "var(--mono)", fontSize: 10, '
  + 'letterSpacing: 0.8, color: "rgba(255,255,255,0.72)", lineHeight: 1, '
  + 'whiteSpace: "nowrap", flexShrink: 0 }\n'
  + '    }, "OUTPUT"),\n';

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

const ONE_CLOCK_READ = "    const now = Date.now();\n"
  + "    commitReading(now);\n"
  + "    if (falling(holdRef.current, now)\n"
  + "        || overExpiring(holdRef.current)) schedulePump();\n";
if (plantTwoClockReads) {
  plant("a pump that reads the clock twice", ONE_CLOCK_READ,
    "    commitReading(Date.now());\n"
    + "    if (falling(holdRef.current, Date.now())\n"
    + "        || overExpiring(holdRef.current)) schedulePump();\n");
}
if (plantNoDecay) {
  plant("a number that never falls", FALL,
    "    const fallen = hold.holdDb;\n");
}
if (plantDecayingLatch) {
  plant("an overload that decays with the number", LATCH_RENDER,
    "  const over = reading.over === true && reading.peakDb !== null\n"
    + "    && reading.peakDb >= -12;\n");
}
if (plantNoHoldWindow) {
  plant("a number with no hold window", HOLD_WINDOW,
    "  const PEAK_HOLD_MS = 0;\n");
}
if (plantNoLatch) {
  plant("an OVER that clears itself", LATCH,
    "      if (true) {\n        hold.over = payload.over === true;\n");
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
if (plantUnlabelledTrim) {
  plant("a trim with no on-screen label", TRIM_LABEL_ELEMENT, "");
}
if (plantShortTrack) {
  plant("the 58pt track that shipped", TRACK_STYLE,
    'style: { width: 58, flexShrink: 0, accentColor: "hsl(200,80%,60%)" }');
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
if (html.split("function SpectrOutputMeter({ latchOver = false } = {}) {")
    .length - 1 !== 1) {
  fail("the document does not declare exactly one SpectrOutputMeter");
}
if (!chromeBody.includes("React.createElement(SpectrOutputMeter, "
    + "{ latchOver: settings.overLatch === true })")) {
  fail("Chrome() does not render SpectrOutputMeter, so the readout exists in "
    + "the document and nowhere on screen");
}
// S1b. The meter stays after the captured Chrome children, immediately before
// the appended latency rail. This document's bindings address their nodes
// by positional DOM path, so a child inserted anywhere but the end renumbers
// every later sibling and silently re-points them: appended into the header's
// flex row, "BARS" lost its captured 48pt text basis and remeasured at 20pt,
// shrinking its hit box. Only `native buttons are tappable across their whole
// painted bounds` can see that, so pin the shape that avoids it here.
if (!chromeBody.includes(
    "React.createElement(SpectrOutputMeter, "
    + "{ latchOver: settings.overLatch === true }), /* @__PURE__ */ "
    + "React.createElement(SpectrLatencyRail, null));")) {
  fail("SpectrOutputMeter and SpectrLatencyRail are not appended in order "
    + "after Chrome's captured children; moving them earlier renumbers "
    + "the later binding paths and breaks their text measurement");
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
// S4a2. THE PRINTED LEVEL IS READABLE FROM OUTSIDE. The runtime's element
// shim reads textContent as empty, so a probe driving the shipping app can
// only see this number through an attribute -- and without one, "the number
// did not move" cannot be told from "the number could not be read", which is
// the failure mode this whole change is about.
if (!meterBody.includes('"data-spectr-output-peak-db"')) {
  fail("the printed level is not exposed as an attribute, so nothing driving "
    + "the real app can read the value that now has to move");
}

// S4b. THE TWO HALVES ARE SEPARATE VALUES. `over` must be carried by the
// latch alone; deriving it from the printed level is how it would decay with
// the number, and the runtime section below is the only other thing that
// could notice.
if (!/const over = reading\.over === true;/.test(meterBody)) {
  fail("the rendered OVER state is not read straight off the latch; anything "
    + "else makes it a function of the level, which decays");
}
// S4c. THE FALL HAS ITS OWN CLOCK. The native side publishes only a CHANGED
// reading, so a decay driven by incoming frames freezes on a steady tone --
// the exact defect being removed. rAF, and self-terminating: `setInterval` is
// already rejected by S2.
if (!/requestAnimationFrame\(pump\)/.test(meterBody)) {
  fail("the fall is not driven by a frame clock, so it can only advance when "
    + "the native side publishes -- and it publishes nothing while a reading "
    + "is unchanged");
}
if (!/if \(falling\(holdRef\.current, now\)\s*\|\| overExpiring\(holdRef\.current\)\) schedulePump\(\);/
    .test(meterBody)) {
  fail("the fall pump does not reschedule itself conditionally, so it is "
    + "either a one-shot or a permanent per-frame animation in the header");
}
// S4d. ONE CLOCK READ PER FRAME, SHARED BY THE COMMIT AND THE STOP DECISION.
// Two reads leave the fall a frame short: the last commit lands just above the
// live level, the check a moment later sees the crossing and stops, and the
// readout sits 0.1-0.4 dB high on a signal that is still there. This is a
// STATIC check on purpose -- it is the one defect in this component the
// runtime section below is structurally blind to, because its clock does not
// advance between two calls inside one frame.
if (!/const pump = \(\) => \{\s*pumpRef\.current = 0;\s*const now = Date\.now\(\);\s*commitReading\(now\);/
    .test(meterBody)) {
  fail("the fall pump does not take a single timestamp and use it for both "
    + "the commit and the keep-falling decision; two clock reads stop the "
    + "fall up to a frame above the live level, and it stays there");
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

// S6. THE TRIM HAS AN ON-SCREEN LABEL, AND IT LABELS THE TRACK.
//
// Every other control in this header is labelled -- LIVE, PRECISION, BARS,
// RESPONSE, BOTH, the band chip, the zoom readout. The trim shipped carrying
// only `aria-label` and `title`. Neither is drawn: in a DAW a user met an
// unlabelled slider beside a meter and had to guess what it moved.
//
// The word is asserted to be the host parameter's own name. kOutputTrim is
// exposed to the host as "Output", so somebody automating it in their DAW
// reads the same word in both places; a label that said TRIM or GAIN would
// be a second name for one parameter.
const labelStyle =
  /"data-spectr-output-trim-label": true,[\s\S]{0,600}?style: \{([^}]*)\}/
    .exec(meterBody);
const labelText =
  /"data-spectr-output-trim-label": true,[\s\S]{0,900}?\}, "([^"]*)"\)/
    .exec(meterBody);
if (!labelStyle || !labelText) {
  fail("the trim carries no on-screen label; aria-label and title are not "
    + "drawn, so in a DAW this is an unlabelled slider beside a meter");
} else {
  if (labelText[1] !== "OUTPUT") {
    fail(`the trim's visible label reads ${JSON.stringify(labelText[1])}; it `
      + 'must read "OUTPUT", the name the host already shows for kOutputTrim, '
      + "or one parameter has two names");
  }
  // Same defect class as S5, on the element added to fix a different one: a
  // control that declares no type inherits the document body default.
  const style = labelStyle[1];
  const family = /fontFamily: "([^"]+)"/.exec(style);
  const size = /fontSize: (\d+)/.exec(style);
  if (!peakFontFamily || !peakFontSize) {
    // already reported by S5
  } else {
    if (!family || family[1] !== peakFontFamily[1]) {
      fail("the trim label does not declare the PEAK button's font family "
        + `(${JSON.stringify(peakFontFamily[1])}); unstated it inherits the `
        + "document body face and reads as a different kind of thing from "
        + "every other word in the header");
    }
    if (!size || size[1] !== peakFontSize[1]) {
      fail("the trim label does not declare the PEAK button's font size "
        + `(${peakFontSize[1]}); unstated it inherits the document body size`);
    }
  }
  if (!/color: "/.test(style)) {
    fail("the trim label declares no colour, so it inherits the body default "
      + "and outshines the readouts it sits among");
  }
  // No declared width, so the label can never be a box too small for its own
  // word -- the failure mode S5 guards for the readout, which DOES need a
  // fixed box because its value changes width.
  if (/width: /.test(style)) {
    fail("the trim label declares a fixed width; it has no reason to, and a "
      + "box narrower than the word clips it");
  }
  if (!/whiteSpace: "nowrap"/.test(style)) {
    fail("the trim label does not declare whiteSpace nowrap, so it can wrap "
      + "inside a 26pt-tall header row");
  }
  // Order. A label after the track labels the readout, not the control.
  const labelAt = meterBody.indexOf('"data-spectr-output-trim-label"');
  const trackAt = meterBody.indexOf('"data-spectr-output-trim":');
  const readoutAt = meterBody.indexOf('"data-spectr-output-trim-readout"');
  if (!(labelAt < trackAt && trackAt < readoutAt)) {
    fail("the cluster is not laid out label -> track -> readout "
      + `(${labelAt}/${trackAt}/${readoutAt}); a label after the track reads `
      + "as a caption on the number instead of a name for the control");
  }
}

// S7. THE TRACK RESOLVES FINELY ENOUGH TO AIM WITH, MEASURED AGAINST THE
// PATTERN THIS REPO ALREADY USES.
//
// The runtime maps a press to a value across the FULL declared track width
// under a fixed ~20pt thumb, so resolution is (max - min) / width dB per
// point and nothing else. It shipped at 58pt:
//
//     48 dB / 58pt = 0.828 dB per point
//
// -- confirmed through the shipping standalone, where a 25pt drag from the
// track centre moved the trim 20.5 dB (0.82 measured, the difference being
// the 0.5 dB step). At 2x that is a 0.5 dB step roughly every device pixel:
// there is no fine adjustment at all.
//
// The bar is Spectr's own Settings slider, whose track is DERIVED from that
// component's declarations rather than pinned to a literal here, so a
// deliberate change to the Settings row moves this gate with it.
const sliderRow = /const SSlider = [\s\S]{0,400}?gap: (\d+), width: (\d+) \}/
  .exec(html);
const sliderReadout =
  /const SSlider = [\s\S]{0,1200}?className: "tnum", style: \{ width: (\d+)/
    .exec(html);
const trackStyle =
  /"data-spectr-output-trim": true,[\s\S]{0,600}?style: \{([^}]*)\}/
    .exec(meterBody);
const trackWidth = trackStyle ? /width: (\d+)/.exec(trackStyle[1]) : null;
const trimMin = /min:\s*(-?\d+)/.exec(meterBody);
const trimMax = /max:\s*(-?\d+)/.exec(meterBody);
const trimStep = /step:\s*([\d.]+)/.exec(meterBody);
if (!sliderRow || !sliderReadout) {
  fail("the Settings SSlider's own geometry is unreadable, so there is no "
    + "in-repo pattern to measure the header trim against -- this check "
    + "would otherwise pass vacuously");
} else if (!trackWidth || !trimMin || !trimMax || !trimStep) {
  fail("the trim declares no width/min/max/step, so its resolution cannot be "
    + "computed at all");
} else {
  const settingsTrack =
    Number(sliderRow[2]) - Number(sliderRow[1]) - Number(sliderReadout[1]);
  const width = Number(trackWidth[1]);
  const span = Number(trimMax[1]) - Number(trimMin[1]);
  const step = Number(trimStep[1]);
  const dbPerPoint = span / width;
  const pointsPerStep = step / dbPerPoint;
  console.log("control   %s %s", "settings track".padEnd(16), settingsTrack);
  console.log("measured  trim track %dpt -> %s dB/pt, %s pt per %s dB step",
    width, dbPerPoint.toFixed(3), pointsPerStep.toFixed(2), step);
  if (width < settingsTrack) {
    fail(`the trim's ${width}pt track is shorter than the ${settingsTrack}pt `
      + "track Spectr's own Settings sliders use under the same fixed thumb "
      + `(${dbPerPoint.toFixed(2)} dB per point against `
      + `${(span / settingsTrack).toFixed(2)}); it shipped at 58pt and was `
      + "unusable for fine adjustment");
  }
  // Independent floor, in case the Settings row is ever shortened: one
  // pointer point of travel must never skip a step, or consecutive device
  // pixels land on non-adjacent values.
  if (pointsPerStep < 1) {
    fail(`one ${step} dB step is ${pointsPerStep.toFixed(2)}pt of travel, so `
      + "a single-point pointer move skips values");
  }
}

// S8. AND IT STILL FITS THE GAP IT WAS PLACED IN.
//
// The cluster is absolutely positioned at x=282 in the header's empty flex
// spacer, measured at x=281.7..669.5 with the LIVE button's painted left edge
// at x=687.5. Lengthening the track is only a fix if the result stays inside
// that gap -- a track long enough to reach under LIVE trades one defect for a
// worse one. Computed from the cluster's own declarations so it tracks any
// later edit to them.
const clusterLeft =
  /"data-spectr-output-cluster": true,[\s\S]{0,400}?left: (\d+)/
    .exec(meterBody);
const clusterGap = /gap: (\d+),\n\s*flexShrink/.exec(meterBody);
const peakWidth = /width: (\d+),\n\s*minWidth/.exec(meterBody);
const readoutWidth = readoutStyle ? /width: (\d+)/.exec(readoutStyle[1]) : null;
const HEADER_GAP_END = 669.5;
if (!clusterLeft || !clusterGap || !peakWidth || !readoutWidth || !trackWidth
    || !labelText) {
  fail("the cluster's own geometry is unreadable, so whether it still fits "
    + "the header's measured gap cannot be answered");
} else {
  const glyph = Number(peakFontSize ? peakFontSize[1] : 10);
  // A mono advance is ~0.6em, plus the button's own 0.8pt of tracking. This
  // estimates "OUTPUT" at 40.8pt against 41.0pt measured from a Skia raster
  // of the shipping editor, so it is accurate to ~0.2pt -- immaterial against
  // the ~19pt of headroom the bound below leaves, but it IS an estimate and
  // not a measurement, which is why the bound is the gap's end and not the
  // LIVE button's edge 18pt further right.
  const labelWidth = labelText[1].length * (glyph * 0.6 + 0.8);
  const right = Number(clusterLeft[1])
    + Number(peakWidth[1]) + Number(clusterGap[1])
    + labelWidth + Number(clusterGap[1])
    + Number(trackWidth[1]) + Number(clusterGap[1])
    + Number(readoutWidth[1]);
  console.log("measured  cluster 282..%s against a gap ending %s",
    right.toFixed(1), HEADER_GAP_END);
  if (right > HEADER_GAP_END) {
    fail(`the cluster now ends at ~${right.toFixed(1)}, past the header's `
      + `measured empty gap (x=281.7..${HEADER_GAP_END}); it crowds or runs `
      + "under the LIVE/PRECISION group at x=687.5");
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
  // A REAL frame queue, not a synchronous stub. The stub that used to sit
  // here called its callback immediately, which turns any self-rescheduling
  // animation into unbounded recursion -- so the decay this suite exists to
  // measure could not have been evaluated at all, let alone measured over
  // time.
  let frameSeq = 1;
  const frames = new Map();
  const FRAME_MS = 1000 / 60;
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

  try {
    const context = vm.createContext(sandbox);
    vm.runInContext(leafBlock, context, { filename: "spectr-output-meter.js" });
    const Meter = context.SpectrOutputMeter;
    if (typeof Meter !== "function") {
      fail("SpectrOutputMeter is not reachable after evaluating its own "
        + "script block -- it was declared in another scope");
    } else {
      let element = null;
      // LATCHED, explicitly. Every runtime check below measures the hold-
      // until-clicked half, which is now what `overLatch` selects rather than
      // what the component always does. Rendering the default here would make
      // R3's tail assert that an auto-clearing chip does not clear.
      const render = () => {
        hooks.index = 0;
        hooks.effects = [];
        element = Meter({ latchOver: true });
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
      // Advance the clock and run whatever frames fall due, PUBLISHING
      // NOTHING. This is the harsh case and the realistic one: the native
      // side publishes only a CHANGED reading, so a steady level or a stopped
      // transport delivers no frames at all, and a decay driven by incoming
      // publications would freeze here.
      const advance = (ms) => {
        const until = now + ms;
        while (now < until) {
          now = Math.min(until, now + FRAME_MS);
          const due = [...frames.values()];
          frames.clear();
          for (const fn of due) fn(now);
        }
      };

      if (!listeners.has("output_meter")) {
        fail("the leaf never subscribed to output_meter, so nothing can ever "
          + "reach it and every reading below would be vacuous");
      }

      // R0. THE LABEL IS ACTUALLY RENDERED, not merely declared somewhere in
      // the source. S6 reads text; this reads the tree the component returns,
      // so a label parked in a branch nothing takes -- or a component that
      // returns nothing at all -- is caught here and nowhere else.
      const labelNode = find(element, (p) => p["data-spectr-output-trim-label"]);
      if (!labelNode) {
        fail("the rendered cluster contains no trim label node; it is "
          + "declared in the source and absent from what a person sees");
      } else if (flatten(labelNode) !== "OUTPUT") {
        fail(`the rendered trim label reads ${JSON.stringify(flatten(labelNode))}`);
      }
      // ...and it is a SIBLING of the track inside the cluster, not nested in
      // it, which would make it part of the slider's own hit region.
      const clusterNode = find(element, (p) => p["data-spectr-output-cluster"]);
      if (clusterNode) {
        const kinds = (clusterNode.children || [])
          .filter((c) => c && typeof c === "object" && c.props)
          .map((c) => c.props["data-spectr-output-peak"] ? "peak"
            : c.props["data-spectr-output-trim-label"] ? "label"
            : c.props["data-spectr-output-trim"] ? "track"
            : c.props["data-spectr-output-trim-readout"] ? "readout" : "?");
        if (kinds.join(",") !== "peak,label,track,readout") {
          fail(`the rendered cluster's children are ${kinds.join(",")}, `
            + "expected peak,label,track,readout");
        }
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

      // R2. THE NUMBER RISES INSTANTLY. A peak meter that ramps up misses
      // the transient it exists to catch.
      publish({ peak_db: -20.0, over: false, trim_db: 0 });
      if (!peakText().includes("-20.0")) {
        fail(`a -20 dBFS frame reads ${JSON.stringify(peakText())}`);
      }

      // THE BALLISTIC'S OWN NUMBERS, read out of the component rather than
      // pinned here, so a deliberate retune moves this suite with it -- but
      // bounded, because "conventional" is the whole justification and a
      // 50 ms hold or a 200 dB/s fall would satisfy every timing assertion
      // below while being unreadable on screen.
      const holdMs = Number((/const PEAK_HOLD_MS = (\d+)/.exec(meterBody)
        || [])[1]);
      const fallRate = Number(
        (/const PEAK_FALL_DB_PER_SEC = ([\d.]+)/.exec(meterBody) || [])[1]);
      if (!isFinite(holdMs) || !isFinite(fallRate)) {
        fail("the component declares no PEAK_HOLD_MS / PEAK_FALL_DB_PER_SEC, "
          + "so its ballistic cannot be measured and every timing check "
          + "below would be vacuous");
      } else {
        console.log("measured  hold %dms, fall %s dB/s", holdMs, fallRate);
        if (holdMs < 1000 || holdMs > 3000) {
          fail(`a ${holdMs}ms hold is outside the 1000-3000ms window a `
            + "number you READ needs: under a second it is gone before a "
            + "glance lands on it, over three it stops tracking the signal");
        }
        if (fallRate < 6 || fallRate > 30) {
          fail(`a ${fallRate} dB/s fall is outside 6-30: slower and a 40 dB `
            + "return takes most of a bar, faster and the digits blur");
        }
      }

      // R3. THE NUMBER IS HELD LONG ENOUGH TO READ, THEN FALLS TO THE LIVE
      //     LEVEL -- AND THE OVERLOAD SURVIVES THAT SAME ELAPSED TIME.
      //
      // This is the split, asserted in one place, because the two halves are
      // only meaningful against each other: a moving number says the readout
      // is live, and a word that stays through the same interval says
      // something happened. Driven by TIME with no publication under it.
      const LOUD = 1.4;      // over full scale
      const LIVE = -40.0;    // the programme level it must return to
      commits.length = 0;
      publish({ peak_db: LOUD, over: true, trim_db: 0 });
      if (!peakText().startsWith("OVER") || !peakText().includes("+1.4")) {
        fail(`the overload frame reads ${JSON.stringify(peakText())}, `
          + 'expected "OVER +1.4"');
      }
      // The attribute and the glyphs must be the SAME number. An attribute
      // that drifts from the printed text would make every probe below a
      // measurement of the attribute rather than of the readout.
      const levelAttr = () => peakNode().props["data-spectr-output-peak-db"];
      if (!peakText().includes(levelAttr())) {
        fail(`the chip prints ${JSON.stringify(peakText())} while its `
          + `attribute says ${JSON.stringify(levelAttr())}; a probe reading `
          + "the attribute would not be reading the readout");
      }
      const overPeakAttr = peakNode().props["data-spectr-output-over-peak"];
      if (overPeakAttr !== "1.4") {
        fail(`the worst overshoot is exposed as `
          + `${JSON.stringify(overPeakAttr)}, expected "1.4" -- it is no `
          + "longer what the chip prints, so losing it here loses it");
      }
      // The signal drops to programme level and then NOTHING is published
      // again, exactly as the native publisher behaves once a reading stops
      // changing.
      publish({ peak_db: LIVE, over: false, trim_db: 0 });
      // A FIXED interval, deliberately NOT derived from PEAK_HOLD_MS. Timing
      // the stimulus off the constant under test makes this check blind to
      // that constant shrinking: with a 0 ms hold, `advance(holdMs * 0.8)`
      // advances nothing and the transient is trivially still on screen. 800
      // ms is the human quantity the hold exists to cover -- hear it, look
      // over, read a five-glyph run.
      const GLANCE_MS = 800;
      advance(GLANCE_MS);
      const duringHold = peakText();
      if (!duringHold.includes("+1.4")) {
        fail(`${GLANCE_MS}ms after the transient the readout is `
          + `${JSON.stringify(duringHold)}; it must still show +1.4 or a peak `
          + "you were not looking at is unreadable");
      }
      // Partway down. A SNAP to the live level would satisfy the end state
      // below while showing nothing in between, so require a value strictly
      // inside the interval -- this is the assertion that makes it a fall.
      advance(holdMs + 200);
      const midFall = peakText();
      const midValue = Number((/(-?\d+\.\d)/.exec(midFall) || [])[1]);
      if (!(midValue < LOUD && midValue > LIVE)) {
        fail(`one second into the fall the readout is `
          + `${JSON.stringify(midFall)}; expected a value strictly between `
          + `${LIVE} and ${LOUD} -- it is snapping, not falling`);
      }
      // And it lands ON the live level rather than sailing past it.
      advance(((LOUD - LIVE) / fallRate) * 1000 + 500);
      const settled = peakText();
      if (!settled.includes(LIVE.toFixed(1))) {
        fail(`after the hold plus a full ${fallRate} dB/s return the readout `
          + `is ${JSON.stringify(settled)}, expected ${LIVE.toFixed(1)} -- `
          + "the number is not falling back to the signal, so a held reading "
          + "and a frozen plug-in still look identical");
      }
      if (levelAttr() !== LIVE.toFixed(1)) {
        fail(`after the fall the level attribute is `
          + `${JSON.stringify(levelAttr())}, expected ${LIVE.toFixed(1)} -- `
          + "it has drifted from the glyphs beside it");
      }
      // THE OTHER HALF. Same elapsed time, and it must NOT have moved.
      if (overFlag() !== "true") {
        fail(`the overload cleared itself while the number fell `
          + `(over=${overFlag()}). A person who looked away has no other way `
          + "to learn it happened.");
      }
      if (!settled.startsWith("OVER")) {
        fail(`the chip reads ${JSON.stringify(settled)} after the fall; the `
          + "word must still say OVER while the number tracks the signal");
      }
      if (peakNode().props["data-spectr-output-over-peak"] !== "1.4") {
        fail("the worst overshoot was lost while the number fell");
      }

      // R3b. THE FALL SELF-TERMINATES. A converged readout must schedule no
      // frame and cost no commit, or this is a permanent per-frame animation
      // in a header -- the cost the zoom readout's live guard exists to
      // avoid.
      const pendingAtRest = frames.size;
      commits.length = 0;
      advance(2000);
      const restCommits = commits.filter((c) => typeof c === "object").length;
      if (pendingAtRest !== 0) {
        fail(`${pendingAtRest} frame callback(s) still scheduled once the `
          + "readout converged; the fall does not self-terminate");
      }
      if (restCommits !== 0) {
        fail(`${restCommits} React commit(s) across 2s of a settled readout `
          + "with nothing published; a meter that is not moving must cost "
          + "nothing");
      }

      // R4. IT RE-ARMS AGAINST WHAT IS SHOWN, not against the session's
      // loudest. After the fall above, a -25 transient is well under the
      // +1.4 that was once latched -- and it is the only thing a person
      // would see, so it must be held.
      publish({ peak_db: -25.0, over: false, trim_db: 0 });
      if (!peakText().includes("-25.0")) {
        fail(`a -25 dBFS transient after the fall reads `
          + `${JSON.stringify(peakText())}; a hold that re-arms only above `
          + "the session maximum catches nothing after the first peak");
      }
      publish({ peak_db: LIVE, over: false, trim_db: 0 });
      advance(holdMs * 0.8);
      if (!peakText().includes("-25.0")) {
        fail(`the re-armed hold did not survive its own window: `
          + `${JSON.stringify(peakText())}`);
      }
      advance(holdMs + ((LOUD - LIVE) / fallRate) * 1000 + 500);

      // R5. A PERSON CLEARS THE LATCH -- AND CLEARING DOES NOT BLANK A METER
      //     THAT HAS SIGNAL IN IT.
      //
      // The number clears itself now, so what a click is FOR is the latch.
      // Blanking the level as well would be the same lie in the other
      // direction: with a steady signal the native publishes nothing, so a
      // blanked readout would sit at "--" while audio runs.
      peakNode().props.onClick();
      const afterClick = peakText();
      if (overFlag() !== "false") {
        fail(`after a click over=${overFlag()}, expected it cleared`);
      }
      if (afterClick.startsWith("OVER")) {
        fail(`after a click the chip still reads ${JSON.stringify(afterClick)}`);
      }
      if (!afterClick.includes(LIVE.toFixed(1))) {
        fail(`after a click the readout is ${JSON.stringify(afterClick)}; it `
          + `must fall back to the live ${LIVE.toFixed(1)}, not blank -- the `
          + "signal is still there and nothing will republish it");
      }
      if (peakNode().props["data-spectr-output-over-peak"] !== "") {
        fail("a click left the worst overshoot behind");
      }
      // ...and it re-arms, or clearing would be a one-way off switch.
      publish({ peak_db: -6.0, over: false, trim_db: 0 });
      if (!peakText().includes("-6.0")) {
        fail(`after clearing, a new frame reads ${JSON.stringify(peakText())} `
          + "-- the readout does not re-arm");
      }

      // R5b. DIGITAL SILENCE RETURNS TO "--". With nothing under it the fall
      // is unbounded, so it must end at the floor rather than counting down
      // forever -- and it must not stop at a number, which would read as a
      // level that is not there.
      publish({ peak_db: null, over: false, trim_db: 0 });
      advance(holdMs + 200000 / fallRate + 1000);
      const afterSilence = peakText();
      if (!afterSilence.includes("--")) {
        fail(`after the signal stopped the readout is `
          + `${JSON.stringify(afterSilence)}, expected "--"`);
      }
      if (frames.size !== 0) {
        fail("the fall into silence never stopped scheduling frames");
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
      console.log("runtime   silence=%s duringHold=%s midFall=%s "
        + "settled=%s over=%s afterClick=%s afterSilence=%s trim=%s",
        JSON.stringify(silent), JSON.stringify(duringHold),
        JSON.stringify(midFall), JSON.stringify(settled), String(overFlag()),
        JSON.stringify(afterClick), JSON.stringify(afterSilence),
        JSON.stringify(trimText()));
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
  console.log("PASS: the readout holds its peak long enough to read and then "
    + "falls to the live level, the overload holds until a person clears it "
    + "with `overLatch` on, and the trim reaches kOutputTrim.");
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
