#!/usr/bin/env node
// THE BAND READOUT NEVER PRINTS AN INDEX FROM ONE LAYOUT AGAINST ANOTHER
// LAYOUT'S DIVISOR.
//
// The defect, from a user screenshot on a bank hovered as one of 64:
//
//     155.5kHz   24.0 dB   BAND 42/32
//
// Index 41 was measured under a 64-band layout; `N` -- the divisor in the
// label AND the divisor in bandCenterFreq -- had already become 32. Both
// halves are that one mismatch:
//
//     N=32, i=41  ->  155473 Hz   the printed frequency, above the view's top
//     N=64, i=41  ->    1763 Hz   the band actually under the pointer
//
// `N` is `settings.bandCount`, and band count is host parameter 3003, so
// automation / a preset recall / a live-state resync rewrites it with NO
// pointer event; `hover` keeps the index it last measured, and the status
// effect is declared `[hoverBand, N, onStatus]` so the rewrite deliberately
// re-runs it with the stale index.
//
// ASSERT THE COMPUTED VALUES, NEVER THE PRESENCE OF A LABEL. A readout suite
// that checks "a string containing BAND appears" passes on the defect: the
// defect produced a perfectly well-formed label. So every check below reads a
// number: which index survives, what frequency it maps to, and whether that
// frequency is inside the view at all.
//
//   STATIC   the document declares one range-and-layout gate (hoverBandOf),
//            every consumer of a hover index goes through it, both hover
//            writers stamp the layout they measured under, the ref copy and
//            the React copy are the SAME stamped object, and both frequency
//            helpers clamp.
//   RUNTIME  the shipping source text of hoverBandOf / bandCenterFreq /
//            liveHoverLabel is executed in a vm against the exact screenshot
//            stimulus (N=32 with a hover measured at n=64, band=41) and has to
//            produce no reading; the same hover under its own N=64 has to
//            produce 1763 Hz; and no index in [-100, 400] under any N in the
//            shipping set may map outside [20, 20000] Hz.
//
// Usage:
//   node test_materialized_band_readout.mjs <materialized-document.runtime.json>
//        [--plant-no-guard | --plant-no-clamp | --plant-unstamped-state]
//        [--expect-fail]
//
// --plant-no-guard restores the pre-fix shape exactly: hoverBand takes the raw
// index and liveHoverLabel formats it. That is the document that printed
// BAND 42/32, and this suite must reject it.
// --plant-no-clamp keeps the guard but removes bandCenterFreq's clamp, so the
// belt-and-braces half is proved to be load bearing on its own -- otherwise it
// could be deleted tomorrow with every test still green.
// --plant-unstamped-state writes the stamp to the ref and hands the bare
// object to setHover, which is the regression that silently deletes the
// readout entirely rather than making it wrong. It was a real mistake made
// while writing this change.
// --expect-fail inverts the verdict, so a control is green only when this
// suite REJECTS that document. The inversion lives here rather than in
// WILL_FAIL because WILL_FAIL accepts any non-zero exit -- a usage error or an
// unreadable file satisfied it and proved nothing.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantNoGuard = args.includes("--plant-no-guard");
const plantNoClamp = args.includes("--plant-no-clamp");
const plantUnstamped = args.includes("--plant-unstamped-state");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_band_readout.mjs <runtime.json> "
    + "[--plant-no-guard|--plant-no-clamp|--plant-unstamped-state] "
    + "[--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const GUARD_DECL = "  const hoverBandOf = (h) => h && !h.mini && h.n === N"
  + " && h.band >= 0 && h.band < N ? h.band : -1;\n";
const HOVER_BAND = "  const hoverBand = hoverBandOf(hover);\n";
const LABEL_HEAD = "  const liveHoverLabel = (current) => {\n"
  + "    const band = hoverBandOf(current);\n"
  + "    if (band < 0) return \"\";\n";
const CENTRE_CLAMP =
  "    const a = view.lmin + (clamp(i, 0, N - 1) + 0.5) / N * (view.lmax - view.lmin);\n";
const STAMP_PAIR = "    const stamped = next ? { ...next, n: N } : next;\n"
  + "    hoverRef.current = stamped;\n"
  + "    if (!pointerRef.current || !pointerRef.current.mode) setHover(stamped);\n";

// Every plant must be observed to apply. A plant whose needle has drifted
// silently produces the CURRENT document, and the control then passes while
// proving nothing at all.
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

if (plantNoGuard) {
  plant("the pre-fix hoverBand", GUARD_DECL + HOVER_BAND,
    "  const hoverBand = hover && !hover.mini ? hover.band : -1;\n");
  plant("the pre-fix liveHoverLabel", LABEL_HEAD,
    "  const liveHoverLabel = (current) => {\n"
    + "    if (!current || current.mini) return \"\";\n"
    + "    const band = current.band;\n");
  // The guard is gone, so the clamp is all that is left; the pre-fix document
  // had neither, and it is the pre-fix document this control must reproduce.
  plant("the pre-fix bandCenterFreq", CENTRE_CLAMP,
    "    const a = view.lmin + (i + 0.5) / N * (view.lmax - view.lmin);\n");
}

if (plantNoClamp) {
  plant("bandCenterFreq without its clamp", CENTRE_CLAMP,
    "    const a = view.lmin + (i + 0.5) / N * (view.lmax - view.lmin);\n");
}

if (plantUnstamped) {
  plant("a React copy that never carries the stamp", STAMP_PAIR,
    "    const stamped = next ? { ...next, n: N } : next;\n"
    + "    hoverRef.current = stamped;\n"
    + "    if (!pointerRef.current || !pointerRef.current.mode) setHover(next);\n");
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

// ------------------------------------------------------- positive controls
// A suite that cannot find its subject reports every document as clean.
const bankBody = balancedBody(html, "function FilterBank(");
const controls = {
  "filter bank body": bankBody ? 1 : 0,
  "hover state": bankBody ? bankBody.split("const [hover, setHover]").length - 1 : 0,
  "band divisor": bankBody ? bankBody.split("const N = bandCount;").length - 1 : 0,
  "label sites": html.split("   BAND ").length - 1,
  "centre frequency": bankBody
    ? bankBody.split("const bandCenterFreq = (i) => {").length - 1 : 0,
};
for (const [label, count] of Object.entries(controls)) {
  console.log("control   %s %s", label.padEnd(20), count);
}
const blind = Object.entries(controls).filter(([, v]) => v === 0).map(([k]) => k);
if (blind.length) {
  console.error("FAIL: the payload has no " + blind.join(", ")
    + " -- this suite is reading the wrong document or the editor was "
    + "restructured, so it cannot render a verdict");
  process.exit(2);
}

// ------------------------------------------------------------ static checks

// S1. Exactly one gate, declared in FilterBank's own body. A const declared in
// a sibling body still parses -- syntax is not scope -- so ask the body.
if (!bankBody.includes("const hoverBandOf = ")) {
  fail("FilterBank() declares no hoverBandOf: nothing reconciles a hover "
    + "index with the layout it was measured under");
}
if (bankBody.split("const hoverBandOf = ").length - 1 > 1) {
  fail("FilterBank() declares hoverBandOf more than once");
}
// The gate must test the LAYOUT, not merely the range. An in-range stale index
// is still the wrong band: band 11 of 64 is not band 11 of 32.
if (bankBody.includes("const hoverBandOf = ")
    && !/const hoverBandOf = \(h\) =>[^\n]*h\.n === N/.test(bankBody)) {
  fail("hoverBandOf does not compare the reading's own layout to N, so an "
    + "in-range index measured under a different band count still reports");
}

// S2. Every consumer of a hover index goes through the gate. Reading
// `hover.band` or `current.band` anywhere else reintroduces the defect.
for (const needle of ["hover.band", "current.band"]) {
  const n = bankBody.split(needle).length - 1;
  if (n !== 0) {
    fail(`${n} site(s) still read ${needle} directly instead of through `
      + "hoverBandOf, so a stale index can reach a readout again");
  }
}
if (!bankBody.includes(HOVER_BAND.trim())) {
  fail("hoverBand is not derived through hoverBandOf");
}
if (!bankBody.includes("const band = hoverBandOf(current);")) {
  fail("liveHoverLabel does not derive its index through hoverBandOf");
}
if (!bankBody.includes("if (hoverBandOf(current) < 0) return;")) {
  fail("the per-frame status write does not gate on hoverBandOf, so the "
    + "direct DOM fast path can still paint a stale reading");
}

// S3. Both hover writers stamp, and the ref copy and the React copy are the
// SAME stamped object. Stamping only the ref leaves the state copy without an
// `n`, which the gate then rejects forever: the readout vanishes outright.
const stamped = bankBody.split("hoverRef.current = { band, x, y, n: N };").length - 1;
if (stamped !== 2) {
  fail(`${stamped} of the 2 direct hover writers stamp their layout; an `
    + "unstamped reading is refused forever and the readout disappears");
}
if (bankBody.split("hoverRef.current = { band, x, y };").length - 1 !== 0) {
  fail("a direct hover writer still records no layout");
}
if (!bankBody.includes(STAMP_PAIR)) {
  fail("updatePointerHover does not hand the SAME stamped reading to both the "
    + "ref and React state; the unstamped copy is refused forever");
}

// S4. Both frequency helpers clamp their index into the bank.
if (!bankBody.includes(CENTRE_CLAMP.trim())) {
  fail("bandCenterFreq does not clamp its index, so an out-of-range index "
    + "extrapolates a frequency past the top of the view");
}
if (!bankBody.includes("const j = clamp(i, 0, N - 1);")) {
  fail("bandFreqRange does not clamp its index");
}

// S5. The label still reads the same single N it always did. This change is
// about which INDEX reaches the divisor, not about adding a second divisor.
if (html.split("BAND ${hoverBand + 1}/${N}").length - 1 !== 1
    || html.split('"   BAND " + (band + 1) + "/" + N').length - 1 !== 1) {
  fail("the two band-readout label sites are not both present and singular");
}

// ----------------------------------------------------------- runtime check
// Static text cannot tell you 155473 from 1763. Execute the SHIPPING SOURCE of
// the three pieces -- lifted out of the document by exact needle, so a plant
// changes what runs -- and read the numbers back.

function lift(name, header) {
  const at = bankBody.indexOf(header);
  if (at < 0) { fail(`could not lift ${name} out of FilterBank()`); return null; }
  // Arrow bodies here are single-statement or brace-balanced; take to the
  // terminating `};` at the declaration's own indentation.
  const end = bankBody.indexOf("\n  const ", at + 1);
  return bankBody.slice(at, end < 0 ? bankBody.length : end);
}

const liftedGate = lift("hoverBandOf", "  const hoverBandOf = ")
  ?? lift("hoverBand", "  const hoverBand = ");
const liftedCentre = lift("bandCenterFreq", "  const bandCenterFreq = ");
const liftedLabel = lift("liveHoverLabel", "  const liveHoverLabel = ");

if (liftedGate && liftedCentre && liftedLabel) {
  // The pre-fix document has no hoverBandOf at all, so supply one that mimics
  // the shape it had: no gate. The lifted `hoverBand` line is what the plant
  // leaves behind and it is not a function, so build the harness around
  // whichever of the two the document actually declares.
  const hasGate = liftedGate.includes("const hoverBandOf");
  const gateSrc = hasGate ? liftedGate
    : "const hoverBandOf = (h) => h && !h.mini ? h.band : -1;\n";

  const readings = [];
  const sandbox = {
    Math, Number, String, JSON, Array, Object, console: { log() {}, error() {} },
  };
  sandbox.globalThis = sandbox;
  const harness = `
    var N, view, clamp, isMuted, renderGainsRef, targetGainsRef, window;
    function build(n, viewIn) {
      N = n; view = viewIn;
      clamp = (v, a, b) => Math.max(a, Math.min(b, v));
      isMuted = (v) => !(v > -1.0e9) || v === -Infinity;
      renderGainsRef = { current: new Array(128).fill(1.0) };
      targetGainsRef = { current: new Array(128).fill(1.0) };
      window = { SpectrFreq: { fmt: (f) => String(Math.round(f)) } };
      ${gateSrc}
      ${liftedCentre}
      ${liftedLabel}
      return {
        gate: (h) => hoverBandOf(h),
        centre: (i) => bandCenterFreq(i),
        label: (h) => liveHoverLabel(h),
      };
    }
  `;
  try {
    const context = vm.createContext(sandbox);
    vm.runInContext(harness, context, { filename: "spectr-band-readout.js" });
    const lmin = Math.log10(20), lmax = Math.log10(20000);
    const view = { lmin, lmax };

    // R1. THE SCREENSHOT, EXACTLY. A reading measured under 64 bands, read
    // back after N has become 32. This is the only stimulus that matters.
    const at32 = context.build(32, view);
    const stale = { band: 41, x: 100, y: 100, n: 64 };
    const staleLabel = at32.label(stale);
    readings.push(`staleLabel=${JSON.stringify(staleLabel)}`);
    if (staleLabel !== "") {
      fail("a hover measured under 64 bands still produces a reading after "
        + `the bank became 32: ${JSON.stringify(staleLabel)} -- this is the `
        + "BAND 42/32 defect");
    }
    if (at32.gate(stale) !== -1) {
      fail(`the gate accepted a 64-band reading under N=32 (returned `
        + `${at32.gate(stale)})`);
    }

    // R2. THE SAME READING UNDER ITS OWN LAYOUT still reports, and reports the
    // band that was actually under the pointer. Without this, "produces no
    // reading" would be satisfied by a readout that never works at all -- and
    // --plant-unstamped-state is exactly that regression.
    const at64 = context.build(64, view);
    const liveLabel = at64.label(stale);
    const liveHz = at64.centre(41);
    readings.push(`liveHz=${liveHz.toFixed(1)}`);
    readings.push(`liveLabel=${JSON.stringify(liveLabel)}`);
    if (liveLabel === "") {
      fail("a hover measured under 64 bands produces no reading under N=64 "
        + "either -- the readout is dead, not fixed");
    }
    if (!liveLabel.includes("BAND 42/64")) {
      fail(`the 64-band reading is ${JSON.stringify(liveLabel)}, expected it `
        + "to name BAND 42/64");
    }
    if (!(Math.abs(liveHz - 1763.4) < 1.0)) {
      fail(`band 42 of 64 across 20Hz-20kHz is ${liveHz.toFixed(1)} Hz, `
        + "expected 1763.4 Hz");
    }

    // R3. THE WRONG-DIVISOR ARITHMETIC ITSELF. Index 41 under N=32 is what
    // printed 155.5kHz. No index, under any shipping band count, may map
    // outside the view -- which is the property a clamp buys and a guard
    // alone does not.
    const top = Math.pow(10, lmax), bottom = Math.pow(10, lmin);
    let worst = 0, worstAt = "";
    for (const n of [32, 40, 48, 56, 64]) {
      const bank = context.build(n, view);
      for (let i = -100; i <= 400; i++) {
        const f = bank.centre(i);
        if (!Number.isFinite(f)) {
          fail(`bandCenterFreq(${i}) at N=${n} is not finite`);
          continue;
        }
        const over = Math.max(f - top, bottom - f);
        if (over > worst) { worst = over; worstAt = `N=${n} i=${i} -> ${f.toFixed(1)}Hz`; }
      }
    }
    readings.push(`worstExcursion=${worst.toFixed(1)}Hz (${worstAt || "none"})`);
    if (worst > 0.5) {
      fail(`bandCenterFreq leaves the 20Hz-20kHz view by ${worst.toFixed(1)} `
        + `Hz: ${worstAt}. The screenshot's 155473 Hz is this excursion.`);
    }
    // Control on the STIMULUS: prove the sweep above would have caught the
    // reported defect, by computing it the pre-fix way right here. If this
    // does not reproduce 155473 the sweep is measuring the wrong thing.
    const prefix = Math.pow(10, lmin + (41 + 0.5) / 32 * (lmax - lmin));
    readings.push(`prefixArithmetic=${prefix.toFixed(0)}Hz`);
    if (Math.abs(prefix - 155473) > 5) {
      fail(`the reported defect does not reproduce: N=32 i=41 computes `
        + `${prefix.toFixed(0)} Hz, expected 155473 Hz. This suite is `
        + "measuring the wrong formula.");
    }

    console.log("runtime   %s", readings.join("  "));
  } catch (error) {
    fail(`executing the lifted readout source threw `
      + `${error.constructor.name}: ${error.message}`);
  }
}

// ----------------------------------------------------------------- verdict

const passed = failures.length === 0;
for (const f of failures) console.log("FAIL:", f);
if (passed) {
  console.log("PASS: a hover reading is refused unless it belongs to the "
    + "current band layout, and no band frequency leaves the view.");
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
