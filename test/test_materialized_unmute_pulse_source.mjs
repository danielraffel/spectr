#!/usr/bin/env node
// Executes the shipped commit functions out of the materialized document and
// MEASURES which sources fire the unmute flourish, rather than asserting that
// the source says what the source says.
//
// THE CONTRACT. `unmutePulseRef` is set to 1 on a muted -> unmuted transition
// and decayed in the rAF loop; while it is above zero the band's spectral edge
// is stroked brighter and thicker. That flourish is a response to a GESTURE.
// A REPLAY -- a morph sweep between two captured snapshots, or a slot recall --
// carries dozens of bands back across the mute threshold at once and, on a
// loop, repeatedly. Each of those is a real transition by the commit
// functions' own rules, and nothing used to tell them the change arrived from
// a replay rather than from a hand.
//
// The half that must SURVIVE is the band dropping to zero: `renderGainsRef` is
// still reset on a replayed unmute, so the band still falls and rises. Only
// the flair is withheld. A fix that withheld both would be fixing the wrong
// half, so this suite asserts the rise explicitly in the same case.
//
// WHY THIS CANNOT BE A CAPTURE. The defect is a REPEATING ANIMATION, ~285ms
// per crossing. A static screenshot taken at any instant is a valid picture of
// both the healthy and the broken build, so the assertion is on the ref being
// WRITTEN, per source. `SPECTR_CLICK` never delivers `pointerdown` and
// `setTimeout`/`rAF` scheduled from `SPECTR_EVAL` never fire, so neither
// in-process instrument can drive this either.
//
// WHY THE REPLAY SOURCES ARE DRIVEN THROUGH THE BROWSER BRANCH. In a native
// host `nativeOwnsSnapshots` is true and both `setMorph` and `recallSnap`
// return early, so their commit tails are unreachable there -- which is why
// the flourish was already gesture-only on the native surface, by accident,
// with nothing stating or testing the rule. This suite forces the branch that
// DOES commit, which is both the browser fallback that ships broken today and
// the shape any future change would reintroduce.
//
// Usage:
//   node test_materialized_unmute_pulse_source.mjs <materialized-document.runtime.json>
//        [--plant-ungate-gain] [--plant-ungate-many]
//        [--plant-unmarked-morph] [--plant-unmarked-recall] [--expect-fail]
//
// Each --plant restores one exact pre-fix spelling. --expect-fail inverts the
// verdict, so a control row is green only when the planted defect is REJECTED,
// and a missing file, usage error, or thrown extractor still fails it.

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantUngateGain = args.includes("--plant-ungate-gain");
const plantUngateMany = args.includes("--plant-ungate-many");
const plantUnmarkedMorph = args.includes("--plant-unmarked-morph");
const plantUnmarkedRecall = args.includes("--plant-unmarked-recall");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_unmute_pulse_source.mjs <runtime.json> "
    + "[--plant-ungate-gain] [--plant-ungate-many] [--plant-unmarked-morph] "
    + "[--plant-unmarked-recall] [--expect-fail]");
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

// ------------------------------------------------------------------- plants

if (plantUngateGain) {
  html = replaceExactlyOnce(html,
    "if (!replay) unmutePulseRef.current[idx] = 1;",
    "unmutePulseRef.current[idx] = 1;",
    "--plant-ungate-gain");
  notes.push("planted the ungated single-band flourish (pre-fix commitGain)");
}

if (plantUngateMany) {
  html = replaceExactlyOnce(html,
    "if (!replay) unmutePulseRef.current[k] = 1;",
    "unmutePulseRef.current[k] = 1;",
    "--plant-ungate-many");
  notes.push("planted the ungated batch flourish (pre-fix commitMany)");
}

if (plantUnmarkedMorph) {
  html = replaceExactlyOnce(html,
    "        morphViewport(s.A, s.B, v, deferReact);\n        commitMany(map, deferReact, true);",
    "        morphViewport(s.A, s.B, v, deferReact);\n        commitMany(map, deferReact);",
    "--plant-unmarked-morph");
  notes.push("planted the unmarked morph replay (pre-fix setMorph tail)");
}

if (plantUnmarkedRecall) {
  html = replaceExactlyOnce(html,
    "          map.set(i, snap.values[i]);\n        commitMany(map, false, true);",
    "          map.set(i, snap.values[i]);\n        commitMany(map);",
    "--plant-unmarked-recall");
  notes.push("planted the unmarked snapshot recall (pre-fix recallSnap tail)");
}

for (const n of notes) console.log(n);

// ------------------------------------------------- 1. the commit functions

const N = 24;

function commitRig() {
  const clampSrc = "const clamp = (v, a, b) => Math.max(a, Math.min(b, v));";
  const isMutedSrc = "function isMuted(g) { return g === -Infinity; }";
  // The shipped definitions, asserted rather than assumed: a rig built on a
  // local copy of a helper the document no longer agrees with would measure
  // this file instead of the product.
  if (!html.includes(clampSrc)) throw new Error("clamp: shipped definition changed");
  if (!/function isMuted\(g\) \{\s*return g === -Infinity;\s*\}/.test(html)) {
    throw new Error("isMuted: shipped definition changed");
  }
  const commitGainSrc = blockAt(html, "const commitGain = (idx, value", "commitGain");
  const commitManySrc = blockAt(html, "const commitMany = (map, deferReact", "commitMany");

  return new Function("N", `
    ${clampSrc}
    ${isMutedSrc}
    const targetGainsRef = { current: new Array(N).fill(0) };
    const renderGainsRef = { current: new Array(N).fill(0) };
    const mutedGainDbRef = { current: new Array(N).fill(0) };
    const unmutePulseRef = { current: new Float32Array(N) };
    const nativeEditPendingRef = { current: false };
    let publications = 0;
    const queueNativeProcessingStatePublication = () => { publications += 1; };
    const setGains = () => {};
    ${commitGainSrc};
    ${commitManySrc};
    return {
      targetGainsRef, renderGainsRef, mutedGainDbRef, unmutePulseRef,
      nativeEditPendingRef, commitGain, commitMany,
      publications: () => publications,
    };
  `)(N);
}

// A band starts muted, with a remembered pre-mute level, and is then unmuted.
function primeMuted(rig, indices) {
  for (const i of indices) {
    rig.targetGainsRef.current[i] = -Infinity;
    rig.renderGainsRef.current[i] = -Infinity;
    rig.mutedGainDbRef.current[i] = 12;
    rig.unmutePulseRef.current[i] = 0;
  }
}

function check(label, condition, detail) {
  if (condition) return;
  failures.push(`${label}${detail ? " -- " + detail : ""}`);
}

let rig;
try {
  rig = commitRig();
} catch (error) {
  console.error(`FAIL: could not build the commit rig: ${error.message}`);
  process.exit(2);
}

// (a) A GESTURE still pulses. This is the positive control for the whole
//     suite: if an unmute stops pulsing entirely, every "no pulse" assertion
//     below passes for the wrong reason.
primeMuted(rig, [3]);
rig.commitGain(3, 0.5);
check("gesture: a single-band unmute fires the flourish",
  rig.unmutePulseRef.current[3] === 1,
  `unmutePulseRef[3] = ${rig.unmutePulseRef.current[3]}, expected 1`);

primeMuted(rig, [5, 6, 7]);
rig.commitMany(new Map([[5, 0.2], [6, 0.3], [7, 0.4]]));
check("gesture: a batch unmute fires the flourish on every band",
  [5, 6, 7].every((i) => rig.unmutePulseRef.current[i] === 1),
  `pulses = ${[5, 6, 7].map((i) => rig.unmutePulseRef.current[i]).join(",")}`);

// (b) A REPLAY does not.
primeMuted(rig, [9]);
rig.commitGain(9, 0.5, false, true);
check("replay: a single-band unmute withholds the flourish",
  rig.unmutePulseRef.current[9] === 0,
  `unmutePulseRef[9] = ${rig.unmutePulseRef.current[9]}, expected 0`);

primeMuted(rig, [11, 12, 13]);
rig.commitMany(new Map([[11, 0.2], [12, 0.3], [13, 0.4]]), false, true);
check("replay: a batch unmute withholds the flourish on every band",
  [11, 12, 13].every((i) => rig.unmutePulseRef.current[i] === 0),
  `pulses = ${[11, 12, 13].map((i) => rig.unmutePulseRef.current[i]).join(",")}`);

// (c) ...but the band still falls and rises. The report asks for the zero,
//     only not the flair, so withholding both would be the wrong fix.
check("replay: the band still drops to zero so it rises rather than jumping",
  [11, 12, 13].every((i) => rig.renderGainsRef.current[i] === 0),
  `renderGains = ${[11, 12, 13].map((i) => rig.renderGainsRef.current[i]).join(",")}`);

// (d) A replay is still a real edit: mute memory, the authored value and the
//     native publication all behave exactly as they did.
check("replay: the authored value still lands",
  rig.targetGainsRef.current[12] === 0.3,
  `targetGains[12] = ${rig.targetGainsRef.current[12]}`);
check("replay: the edit is still declared to the native publication lane",
  rig.nativeEditPendingRef.current === true);

// (e) A replayed MUTE still records mute memory, so a later unmute restores
//     the level rather than flattening the band to 0.
primeMuted(rig, [15]);
rig.targetGainsRef.current[15] = 0.75;
rig.commitMany(new Map([[15, -Infinity]]), false, true);
check("replay: a mute still records the level it is hiding",
  rig.mutedGainDbRef.current[15] === 0.75 * 24,
  `mutedGainDb[15] = ${rig.mutedGainDbRef.current[15]}, expected 18`);

// ------------------------------------------- 2. the replay sources mark thmselves

// The two entry points are driven through the branch that COMMITS, with
// `commitMany` replaced by a spy, so the assertion is on the argument the
// shipped code actually passes rather than on the text of the call.
function sourceSpy(anchor, label, invoke) {
  const src = blockAt(html, anchor, label);
  const calls = [];
  const rigFn = new Function("record", "N", `
    const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
    const lerp = (a, b, t) => a + (b - a) * t;
    function isMuted(g) { return g === -Infinity; }
    // The browser branch: this is the lane that reaches commitMany at all.
    const nativeOwnsSnapshots = false;
    const setMorph = () => {};
    const onStatus = () => {};
    const morphViewport = () => {};
    const optimisticNativeMorph = () => { throw new Error("native branch taken"); };
    const issueNativeCommand = () => { throw new Error("native branch taken"); };
    const commitMany = (map, deferReact, replay) => record({
      size: map.size, deferReact, replay,
    });
    const snapshotsRef = { current: {
      A: { gainDb: new Array(N).fill(-24), muted: new Array(N).fill(true),
           values: new Array(N).fill(-Infinity) },
      B: { gainDb: new Array(N).fill(12), muted: new Array(N).fill(false),
           values: new Array(N).fill(0.5) },
    } };
    const bank = { ${src} };
    return bank;
  `)((c) => calls.push(c), N);
  invoke(rigFn);
  return calls;
}

let morphCalls = [];
try {
  // Both a live drag sample (deferReact true) and the release replay
  // (deferReact false). A hand on the slider is still a sweep between two
  // captured states, so neither may pulse -- this is the deliberate decision
  // the issue asked to be made, asserted rather than left to prose.
  morphCalls = sourceSpy("setMorph: (v, deferReact = false) => {", "setMorph",
    (bank) => { bank.setMorph(0.62, true); bank.setMorph(0.62, false); });
} catch (error) {
  failures.push(`setMorph rig: ${error.message}`);
}
check("morph: the sweep reaches the commit lane at all",
  morphCalls.length === 2,
  `commitMany called ${morphCalls.length} times, expected 2`);
check("morph: a live drag sample declares itself a replay",
  morphCalls[0] && morphCalls[0].replay === true,
  `replay = ${morphCalls[0] && morphCalls[0].replay}`);
check("morph: the release replay declares itself a replay",
  morphCalls[1] && morphCalls[1].replay === true,
  `replay = ${morphCalls[1] && morphCalls[1].replay}`);
check("morph: the sweep still carries every band",
  morphCalls[0] && morphCalls[0].size === N,
  `map size = ${morphCalls[0] && morphCalls[0].size}, expected ${N}`);
check("morph: the drag still defers its React commit",
  morphCalls[0] && morphCalls[0].deferReact === true
    && morphCalls[1] && morphCalls[1].deferReact === false,
  "the replay flag must not disturb the existing deferReact lane");

let recallCalls = [];
try {
  recallCalls = sourceSpy("recallSnap: (slot) => {", "recallSnap",
    (bank) => { bank.recallSnap("B"); });
} catch (error) {
  failures.push(`recallSnap rig: ${error.message}`);
}
check("recall: the slot recall reaches the commit lane at all",
  recallCalls.length === 1,
  `commitMany called ${recallCalls.length} times, expected 1`);
check("recall: a slot recall declares itself a replay",
  recallCalls[0] && recallCalls[0].replay === true,
  `replay = ${recallCalls[0] && recallCalls[0].replay}`);

// --------------------------------------------------------------- verdict

const failed = failures.length > 0;
for (const f of failures) console.error(`  FAIL  ${f}`);

if (expectFail) {
  if (failed) {
    console.log(`OK (control): the planted defect was rejected `
      + `(${failures.length} finding(s))`);
    process.exit(0);
  }
  console.error("FAIL (control): the planted defect was NOT rejected -- the "
    + "suite cannot see this regression, so its clean run proves nothing");
  process.exit(1);
}

if (failed) {
  console.error(`FAIL: ${failures.length} finding(s)`);
  process.exit(1);
}
console.log("OK: the flourish answers a gesture and not a replay, and a "
  + "replayed band still drops to zero");
process.exit(0);
