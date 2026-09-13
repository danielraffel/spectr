#!/usr/bin/env node
// Guards the contract that lets the morph slider move the viewport without
// making the editor expensive.
//
// A snapshot has always captured the window it was taken under. Morph ignored
// it, so the bands blended continuously while the window they are drawn in
// snapped at t=0.5. The editor half of the fix has three properties that each
// read green if you check the wrong thing:
//
//   LOG SPACE  `lmin`/`lmax` are log10 Hz, so interpolating THEM is the whole
//              point. A test that only pins t=0 and t=1 passes unchanged
//              against a snap, because a snap already gives the right answer
//              at both ends. The midpoint is the only assertion that
//              discriminates, and it must also reject a linear-Hz lerp, which
//              is the plausible wrong implementation.
//
//   NO COMMIT  The viewport publication carries a `live` flag because
//              WHILE LIVE coalescing a moving value to one commit per frame measured
//              WORSE than the 150ms poll it replaced -- every commit
//              re-applies the captured import metadata across the whole
//              document. Morph is dragged, and is LFO/automation targetable,
//              so it is exactly the value that moves every sample. The morph
//              path must therefore publish with `live = deferReact` and never
//              force a settle mid-gesture.
//
//   PLAYBACK   The switch governs whether morph APPLIES the viewport, never
//   SWITCH     whether capture records it. Turning it off must park the user
//              on the window they are looking at rather than snapping them to
//              an endpoint, and turning it back on must resume from the live
//              morph value.
//
// Usage:
//   node test_materialized_morph_viewport.mjs <materialized-document.runtime.json>
//        [--plant-snap | --plant-linear | --plant-live-commit
//         | --plant-ignore-switch] [--expect-fail]
//
// Each plant restores one specific wrong implementation, and each fails a
// different assertion, so a single plant that trips everything cannot hide
// which check is load bearing. --expect-fail inverts the verdict: a control is
// green only when this suite REJECTS that document, and red when the plant
// silently failed to apply or anything else went wrong.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantSnap = args.includes("--plant-snap");
const plantLinear = args.includes("--plant-linear");
const plantLiveCommit = args.includes("--plant-live-commit");
const plantIgnoreSwitch = args.includes("--plant-ignore-switch");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_morph_viewport.mjs <runtime.json> "
    + "[--plant-...] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

// Every plant must be OBSERVED to apply. A plant whose needle has drifted
// silently produces the current document, and the control then passes while
// proving nothing at all.
const plant = (label, from, to) => {
  if (html.split(from).length - 1 !== 1) {
    console.error(`FAIL: plant "${label}" found `
      + `${html.split(from).length - 1} sites, expected exactly 1 -- the `
      + "control cannot prove anything");
    process.exit(2);
  }
  html = html.replace(from, to);
  console.log("planted   %s", label);
};

const LERP_PAIR = "      const lmin = lerp(va.lmin, vb.lmin, amount);\n"
  + "      const lmax = lerp(va.lmax, vb.lmax, amount);\n";

if (plantSnap) {
  plant("a viewport that snaps at the midpoint instead of interpolating",
    LERP_PAIR,
    "      const dom = amount >= 0.5 ? vb : va;\n"
    + "      const lmin = dom.lmin;\n      const lmax = dom.lmax;\n");
}
if (plantLinear) {
  plant("a viewport interpolated linearly in Hz rather than in log space",
    LERP_PAIR,
    "      const lmin = Math.log10(lerp(Math.pow(10, va.lmin), "
    + "Math.pow(10, vb.lmin), amount));\n"
    + "      const lmax = Math.log10(lerp(Math.pow(10, va.lmax), "
    + "Math.pow(10, vb.lmax), amount));\n");
}
if (plantLiveCommit) {
  plant("a morph that settles the viewport on every pointer sample",
    "      notifyViewportListeners(deferReact === true);\n",
    "      notifyViewportListeners(false);\n");
}
if (plantIgnoreSwitch) {
  plant("a morph that ignores the playback switch",
    "      if ((globalThis.__spectrMorphViewport\n"
    + "           || (globalThis.__spectrMorphViewport = { enabled: true }))\n"
    + "          .enabled === false) return;\n",
    "      if (false) return;\n");
}

const failures = [];
const fail = (msg) => failures.push(msg);
const check = (label, ok, detail) => {
  if (ok) console.log("ok        %s", label);
  else fail(`${label}${detail ? " -- " + detail : ""}`);
};

// ---------------------------------------------------------------- utilities

// Return the brace-balanced body that follows `header`, so a containment test
// is about the enclosing FUNCTION BODY and not about proximity in the file. A
// block-scoped declaration in the wrong body parses clean -- syntax is not
// scope -- so the static half has to be structural.
function balancedBodyFrom(source, header, balanceParams) {
  const at = source.indexOf(header);
  if (at < 0) return null;
  let from = at + header.length - 1;
  if (balanceParams) {
    // A destructured parameter object means the first brace after the name
    // opens the PARAMETER LIST, not the body. Balance the parens first, or
    // every declaration reports as out of scope -- a loud wrong answer, but
    // still wrong.
    const paren = source.indexOf("(", at);
    if (paren < 0) return null;
    let depth = 0;
    from = -1;
    for (let i = paren; i < source.length; i++) {
      if (source[i] === "(") depth++;
      else if (source[i] === ")") {
        depth--;
        if (depth === 0) { from = i; break; }
      }
    }
    if (from < 0) return null;
  }
  const open = source.indexOf("{", from);
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

// ------------------------------------------------------- positive controls
// A suite that cannot find its subject reports every document as clean, so
// prove the payload is the one we think it is before rendering any verdict.
const HELPER_HEADER = "const morphViewport = (a, b, amount, deferReact) => {";
const controls = {
  "playback-switch holder": html.split("globalThis.__spectrMorphViewport").length - 1,
  "viewport morph helper": html.split(HELPER_HEADER).length - 1,
  "native morph call": html.split("morphViewport(a, b, amount, deferReact);").length - 1,
  "fallback morph call": html.split("morphViewport(s.A, s.B, v, deferReact);").length - 1,
  "snapshot window parse": html.split("const snapView =").length - 1,
  "settings switch": html.split("publishMorphViewport").length - 1,
  "filter bank": html.split("function FilterBank(").length - 1,
};
for (const [label, count] of Object.entries(controls)) {
  console.log("control   %s %s", label.padEnd(24), count);
}
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length) {
  console.error("FAIL: the payload has no " + blind.join(", ")
    + " -- this suite is reading the wrong document or the editor was "
    + "restructured, so it cannot render a verdict");
  process.exit(2);
}

// ------------------------------------------------------------ static checks

const bankBody = balancedBodyFrom(html, "function FilterBank(", true);
if (!bankBody) {
  console.error("FAIL: could not balance FilterBank's body");
  process.exit(2);
}
check("the viewport morph lives inside FilterBank, where viewRef does",
  bankBody.includes(HELPER_HEADER));
check("both morph paths call it",
  bankBody.includes("morphViewport(a, b, amount, deferReact);")
  && bankBody.includes("morphViewport(s.A, s.B, v, deferReact);"));
check("capture records the window unconditionally",
  bankBody.includes("view: { lmin: viewRef.current.lmin, lmax: viewRef.current.lmax },"),
  "the switch must gate playback, never capture");

// The settings panel must update the switch on PRESENCE, not truthiness: it is
// not a host parameter and rides the hydration payload only, so a live
// automation frame omits it and `undefined !== false` would flip it back on.
check("the settings panel reads the switch on presence",
  html.includes("if (typeof modulation.morph_applies_viewport === 'boolean') {"));
// Every read and write of the holder must self-heal. Several suites extract one
// function out of this document and run it in a bare vm with no head prelude,
// where an unguarded `globalThis.__spectrMorphViewport.enabled` is a TypeError
// thrown from inside an unrelated code path -- which is how the first version
// of this patch broke mount hydration in two other suites without naming
// itself anywhere in the failure.
check("every holder access self-heals rather than assuming the prelude ran",
  !/globalThis\.__spectrMorphViewport\.enabled/.test(html),
  "an unguarded holder read survives");
check("hydration reads the switch on presence",
  html.includes("&& typeof payload.modulation.morph_applies_viewport === 'boolean')"));

// ------------------------------------------------------------ runtime checks
// Static text cannot tell a log-space lerp from a linear one, and cannot see a
// commit at all. Run the real extracted helper.

const helperSource = HELPER_HEADER.replace("const morphViewport = ", "")
  .replace(/\{$/, "")
  + balancedBodyFrom(bankBody, HELPER_HEADER);

const publishes = [];
const viewRef = { current: { lmin: Math.log10(20), lmax: Math.log10(200) } };
const sandbox = {
  console,
  Math,
  Number,
  __spectrMorphViewport: { enabled: true },
  lerp: (a, b, t) => a + (b - a) * t,
  viewRef,
  notifyViewportListeners: (live) => publishes.push({
    live, lmin: viewRef.current.lmin, lmax: viewRef.current.lmax }),
};
// A vm context supplies its own `globalThis`, and the sandbox properties are
// installed on it -- so `globalThis.__spectrMorphViewport` inside the helper
// resolves to the holder above, which is exactly the indirection under test.
const context = vm.createContext(sandbox);
const morphViewport = vm.runInContext(`(${helperSource})`, context);
if (typeof morphViewport !== "function") {
  console.error("FAIL: the extracted helper is not callable -- extraction is "
    + "reading the wrong span");
  process.exit(2);
}

const A = { view: { lmin: Math.log10(20), lmax: Math.log10(200) } };
const B = { view: { lmin: Math.log10(2000), lmax: Math.log10(20000) } };
const hz = (l) => Math.pow(10, l);
const near = (a, b, tol) => Math.abs(a - b) <= (tol === undefined ? 1e-6 : tol);
const reset = () => {
  viewRef.current = { lmin: A.view.lmin, lmax: A.view.lmax };
  publishes.length = 0;
  sandbox.__spectrMorphViewport.enabled = true;
};

// 1. Ends, then the midpoint. Only the midpoint discriminates.
reset();
morphViewport(A, B, 0, false);
check("t=0 lands on A's window", near(hz(viewRef.current.lmin), 20, 1e-6));
morphViewport(A, B, 1, false);
check("t=1 lands on B's window", near(hz(viewRef.current.lmin), 2000, 1e-6));

morphViewport(A, B, 0.5, false);
const midMin = hz(viewRef.current.lmin);
const midMax = hz(viewRef.current.lmax);
// 20 -> 2000 Hz is two decades, so half way along the log axis is 200 Hz.
check("the midpoint is the geometric mean (200 Hz), not a snap",
  near(midMin, 200, 0.05), `got ${midMin.toFixed(3)} Hz`);
check("the midpoint upper bound is the geometric mean (2000 Hz)",
  near(midMax, 2000, 0.5), `got ${midMax.toFixed(3)} Hz`);
// The discriminating negative: a linear lerp of Hz answers 1010 Hz here.
check("the midpoint is not the arithmetic mean (1010 Hz)",
  !near(midMin, 1010, 1), `got ${midMin.toFixed(3)} Hz`);
// And not either endpoint, which is what snapping gives.
check("the midpoint is neither endpoint",
  !near(midMin, 20, 0.5) && !near(midMin, 2000, 0.5));

// 2. Equal steps in t must move the window by a constant RATIO. This rejects
//    any interpolation that is right at the ends and midpoint but wrong
//    between them.
reset();
let previous = 20;
let firstRatio = null;
let ratioConstant = true;
for (let step = 1; step <= 10; step++) {
  morphViewport(A, B, step / 10, false);
  const current = hz(viewRef.current.lmin);
  const ratio = current / previous;
  if (firstRatio === null) firstRatio = ratio;
  else if (!near(ratio, firstRatio, firstRatio * 0.002)) ratioConstant = false;
  previous = current;
}
check("equal steps in t move the window by a constant ratio",
  ratioConstant && firstRatio > 1, `first ratio ${firstRatio}`);

// 3. The switch off leaves the viewport untouched across a whole sweep, and
//    the sweep publishes nothing at all -- a no-op that still notified would
//    wake the readout for a value that never changed.
reset();
sandbox.__spectrMorphViewport.enabled = false;
viewRef.current = { lmin: Math.log10(440), lmax: Math.log10(4400) };
for (let step = 0; step <= 20; step++) morphViewport(A, B, step / 20, false);
check("the switch off leaves the viewport exactly where the user parked it",
  near(hz(viewRef.current.lmin), 440, 1e-6)
  && near(hz(viewRef.current.lmax), 4400, 1e-6));
check("the switch off publishes nothing", publishes.length === 0,
  `${publishes.length} publications`);

// 4. Flipping mid-sweep must park, not strand: disabling keeps the window the
//    user is looking at, and re-enabling resumes from the live morph value.
reset();
morphViewport(A, B, 0.25, false);
const atQuarter = hz(viewRef.current.lmin);
check("a quarter of the way is between the endpoints",
  atQuarter > 20 && atQuarter < 2000, `${atQuarter}`);
sandbox.__spectrMorphViewport.enabled = false;
morphViewport(A, B, 0.75, false);
check("disabling mid-sweep parks the viewport rather than snapping it",
  near(hz(viewRef.current.lmin), atQuarter, 1e-6));
sandbox.__spectrMorphViewport.enabled = true;
morphViewport(A, B, 0.75, false);
const atThreeQuarters = hz(viewRef.current.lmin);
check("re-enabling resumes from the live morph value",
  atThreeQuarters > atQuarter && atThreeQuarters < 2000,
  `${atThreeQuarters}`);

// 5. The commit contract. Morph is dragged AND is an LFO/automation
//    destination, so it is exactly the value that moves every sample. Across a
//    whole live gesture every publication must be marked live, so the readout
//    leaf ignores all of them; the settle at the end is the one that repaints.
reset();
for (let step = 0; step <= 40; step++) morphViewport(A, B, step / 40, true);
const liveGesture = publishes.length;
check("a live gesture publishes on every sample so the canvas keeps moving",
  liveGesture === 41, `${liveGesture} publications`);
check("and every one of them is marked live, so React commits nothing",
  publishes.every((p) => p.live === true),
  `${publishes.filter((p) => !p.live).length} settled publications mid-gesture`);
publishes.length = 0;
morphViewport(A, B, 1, false);
check("the settled sample publishes a settle, so the readout repaints",
  publishes.length === 1 && publishes[0].live === false);

// 6. A slot with no recorded window must leave the viewport alone rather than
//    inventing one -- this is the pre-projection processor case.
reset();
const before = viewRef.current.lmin;
morphViewport({ view: null }, B, 0.5, false);
morphViewport(A, { view: null }, 0.5, false);
check("a slot with no recorded window leaves the viewport alone",
  viewRef.current.lmin === before && publishes.length === 0);

// ------------------------------------------------------------------ verdict

const passed = failures.length === 0;
if (!passed) {
  console.error("");
  for (const f of failures) console.error("FAIL: " + f);
}
if (expectFail) {
  if (passed) {
    console.error("\nCONTROL FAILED: the suite ACCEPTED a document that was "
      + "deliberately broken -- it cannot detect this defect");
    process.exit(1);
  }
  console.log("\ncontrol ok: the suite rejected the planted document "
    + `(${failures.length} failed assertion(s))`);
  process.exit(0);
}
if (!passed) process.exit(1);
console.log("\nPASS: morph moves the viewport in log space, honours the "
  + "playback switch, and commits nothing during a live gesture");
process.exit(0);
