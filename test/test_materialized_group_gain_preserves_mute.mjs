#!/usr/bin/env node
// Executes the shipped selection-drag gesture out of the materialized document
// and MEASURES whether it moves levels without touching mute.
//
// THE RULE. Dragging the gain of a SELECTION is an offset: one shared dB delta
// over every member. Mute is a separate axis and the gesture says nothing about
// it, so a muted member stays muted and an unmuted member stays unmuted --
// independently, per band -- while every member's LEVEL moves by the same
// delta. A muted member's level is the one it will return to, so the offset has
// to land underneath the mute rather than being discarded.
//
// WHY A SELECTION OF UNMUTED BANDS CANNOT PROVE THIS. The defect is a muted
// member being dragged back to life, so every assertion here drives a MIXED
// selection -- muted and unmuted bands under one drag -- and requires each band
// to keep its own answer. A suite that selected only unmuted bands would be
// green under every wrong implementation below, because all three of them agree
// with the right one on an unmuted band. That is the shape four green-but-empty
// tests took before this one.
//
// THE TWO WRONG ANSWERS, AND WHY BOTH ARE TESTED. The gesture used to commit
// through `commitDrawnGains`, which answers a different question -- what should
// DRAWING over a muted band do -- and `unmuteOnDraw` is the user's answer to
// it. Routed through that path an offset gets two wrong answers and no right
// one:
//
//   setting ON   every muted member silently unmutes (the reported bug)
//   setting OFF  the mute is held but the offset is THROWN AWAY, so those
//                bands freeze while the rest of the selection moves
//
// So asserting only "still muted" would accept the second answer. The stash
// assertions below require the offset to have landed underneath the mute, which
// is what makes unmuting afterwards return the band to where the group moved it.
//
// THE PRESS-TIME BASE. `commitGroupOffset` writes a muted member's new level
// into `mutedGainDbRef`, the same store `editBaseGain` reads. The move handler
// recomputes from the press snapshot on every pointer sample, so a base
// resolved LIVE would add the delta to a base that already carries the previous
// sample's delta and a muted band would run away up the plot while its unmuted
// neighbours track the pointer. A single-sample drag cannot see this, so the
// drags here deliver several samples and require the result to depend only on
// where the pointer ENDED.
//
// Usage:
//   node test_materialized_group_gain_preserves_mute.mjs <runtime.json>
//        [--plant-draw-commit] [--plant-draw-commit-held] [--plant-live-base]
//        [--plant-raw-base] [--plant-drop-stash] [--expect-fail]
//
// Each plant restores one specific wrong implementation and fails a different
// assertion. --expect-fail inverts the verdict: a control row is green only
// when this suite REJECTS that document.

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantDrawCommit = args.includes("--plant-draw-commit");
const plantDrawCommitHeld = args.includes("--plant-draw-commit-held");
const plantLiveBase = args.includes("--plant-live-base");
const plantRawBase = args.includes("--plant-raw-base");
const plantDropStash = args.includes("--plant-drop-stash");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_group_gain_preserves_mute.mjs "
    + "<runtime.json> [--plant-...] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const failures = [];
const notes = [];
const check = (label, ok, detail) => {
  if (!ok) failures.push(`${label}${detail ? " -- " + detail : ""}`);
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
  notes.push(`planted ${label}`);
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

// The press-time expression, read off the shipping line rather than retyped,
// so a plant that rewrites it is what gets executed.
function pressExpr(source) {
  const at = source.indexOf("groupStart: selection");
  if (at < 0) throw new Error("groupStart: press-site anchor not found");
  if (source.indexOf("groupStart: selection", at + 1) >= 0)
    throw new Error("groupStart: press-site anchor is not unique");
  const eol = source.indexOf("\n", at);
  const line = source.slice(at + "groupStart:".length, eol).trim();
  return line.replace(/,$/, "");
}

// ------------------------------------------------------------------- plants

if (plantDrawCommit) {
  // THE REPORTED DEFECT: the offset commits through the draw policy, which with
  // `unmuteOnDraw` at its default ON reads a finite gain as "no longer muted".
  plant("the offset committed as a draw (unmuteOnDraw ON, the reported bug)",
    "        commitGroupOffset(map);",
    "        commitDrawnGains(map);");
}

if (plantDrawCommitHeld) {
  // The SAME wrong route with the setting OFF: mute survives, the offset does
  // not. Only the stash assertions can see this one, which is why it is a
  // separate control from --plant-draw-commit.
  plant("the offset committed as a draw (unmuteOnDraw OFF, offset discarded)",
    "        commitGroupOffset(map);",
    "        commitDrawnGains(map);");
  plant("unmuteOnDraw forced OFF for that route",
    "    if (unmuteOnDrawRef.current) {",
    "    if (false) {");
}

if (plantLiveBase) {
  // The accumulation trap: a base re-resolved on every pointer sample reads a
  // store the previous sample already wrote, so a muted member runs away.
  plant("a base re-resolved live on every pointer sample",
    "        for (const [i, base] of p.groupStart.entries())\n"
    + "          map.set(i, clamp(base + delta, -1, 1));",
    "        for (const [i, base] of p.groupStart.entries())\n"
    + "          map.set(i, clamp(editBaseGain(targetGainsRef.current[i], i) + delta, -1, 1));");
}

if (plantRawBase) {
  // A press snapshot that does not resolve the level stashed under a mute:
  // the muted member's base is the -Infinity sentinel, so the offset lands on
  // the clamp floor and the level the band would return to is destroyed.
  plant("a press snapshot that does not resolve the stashed level",
    "[i, editBaseGain(targetGainsRef.current[i], i)]",
    "[i, targetGainsRef.current[i]]");
}

if (plantDropStash) {
  // Mute held, offset dropped: the member is skipped without its stashed level
  // being moved. Indistinguishable from correct until you unmute.
  plant("a commit that holds mute but never moves the level underneath it",
    "        mutedGainDbRef.current[index] = clamp(value, -1, 1) * 24;\n",
    "");
}

for (const n of notes) console.log(n);

// ---------------------------------------------------------------- the rig

const N = 32;
// Pointer geometry. `halfH` is the half-height the drag normalises against, so
// a delta of d gain units is a rise of d * halfH pixels.
const HALF_H = 200;

function dragRig() {
  const clampSrc = "const clamp = (v, a, b) => Math.max(a, Math.min(b, v));";
  if (!html.includes(clampSrc)) throw new Error("clamp: shipped definition changed");
  if (!/function isMuted\(g\) \{\s*return g === -Infinity;\s*\}/.test(html))
    throw new Error("isMuted: shipped definition changed");

  const commitManySrc = blockAt(html,
    "const commitMany = (map, deferReact = false, replay = false) => {", "commitMany");
  const commitGroupOffsetSrc = blockAt(html, "const commitGroupOffset = (map) => {",
    "commitGroupOffset");
  const commitDrawnGainsSrc = blockAt(html, "const commitDrawnGains = (map) => {",
    "commitDrawnGains");
  const restoreMutedGainSrc = blockAt(html, "const restoreMutedGain = (index) => {",
    "restoreMutedGain");
  const editBaseGainSrc = blockAt(html, "const editBaseGain = (value, index) => {",
    "editBaseGain");
  const groupBranchSrc = blockAt(html, "if (p.groupStart) {", "group drag branch");
  const groupStartSrc = pressExpr(html);

  return new Function("N", "HALF_H", `
    ${clampSrc}
    function isMuted(g) { return g === -Infinity; }

    const targetGainsRef = { current: new Array(N).fill(0) };
    const mutedGainDbRef = { current: new Array(N).fill(0) };
    const renderGainsRef = { current: new Array(N).fill(0) };
    const unmutePulseRef = { current: new Array(N).fill(0) };
    const nativeEditPendingRef = { current: false };
    const unmuteOnDrawRef = { current: true };   // the shipped default

    let publications = 0;
    const wakeDraw = () => {};
    const setGains = () => {};
    const queueNativeProcessingStatePublication = () => { publications++; };

    ${commitManySrc}
    ${commitDrawnGainsSrc}
    ${commitGroupOffsetSrc}
    ${restoreMutedGainSrc}
    ${editBaseGainSrc}

    // Mute a band the way the product does: commit the sentinel, which stashes
    // the level underneath it.
    const mute = (i) => commitMany(new Map([[i, -Infinity]]), true);

    // One press, then a pointer sample per entry in \`steps\` (each a gain-unit
    // delta). Executes the SHIPPED press expression and the SHIPPED move
    // branch, so a plant in either is what runs.
    const drag = (selectionIndices, band, steps) => {
      const selection = new Set(selectionIndices);
      const p = {
        groupStart: ${groupStartSrc},
      };
      const g = { halfH: HALF_H };
      for (const d of steps) {
        const dy = -d * HALF_H;
        // The shipped branch ends in the move handler's own \`return\`, so each
        // sample runs inside its own call frame -- otherwise the first sample
        // would return out of the whole drag and the suite would measure a
        // one-sample drag while believing it measured four.
        (() => { ${groupBranchSrc} })();
      }
      return p;
    };

    return {
      targetGainsRef, mutedGainDbRef, unmutePulseRef, unmuteOnDrawRef,
      mute, drag, commitMany,
      publicationCount: () => publications,
    };
  `)(N, HALF_H);
}

let rig;
try { rig = dragRig(); }
catch (error) {
  console.error(`FAIL: could not build the drag rig: ${error.message}`);
  process.exit(2);
}

const gain = (i) => rig.targetGainsRef.current[i];
const stash = (i) => rig.mutedGainDbRef.current[i];
const muted = (i) => rig.targetGainsRef.current[i] === -Infinity;
const near = (a, b, tol = 1e-9) => Math.abs(a - b) <= tol;

// ------------------------------------------------------------ the scenario
//
// A MIXED selection, which is the only shape that can fail the way the bug
// fails: bands 6 and 7 muted, bands 8 and 9 left alone, all four dragged
// together. Distinct starting levels so a band landing on another band's
// answer is visible rather than coincidental.

const SEL = [6, 7, 8, 9];
rig.targetGainsRef.current[6] = 0.25;
rig.targetGainsRef.current[7] = -0.50;
rig.targetGainsRef.current[8] = 0.75;
rig.targetGainsRef.current[9] = -0.125;

rig.mute(6);
rig.mute(7);

// The premise the whole suite rests on. If the setup did not actually produce a
// mixed selection, every assertion below is vacuous.
check("setup: the selection really is mixed before the drag",
  muted(6) && muted(7) && !muted(8) && !muted(9),
  `muted = [${SEL.filter(muted).join(",")}]`);
check("setup: muting stashed the level each muted band will return to",
  near(stash(6), 0.25 * 24) && near(stash(7), -0.50 * 24),
  `stash = ${stash(6)}, ${stash(7)}`);

// One drag of +0.20 gain units, delivered as four pointer samples the way a
// real drag arrives. The samples RISE to the final position; the handler
// recomputes from the press snapshot each time, so only the last one decides.
const DELTA = 0.20;
rig.drag(SEL, 8, [0.05, 0.10, 0.15, DELTA]);

// (a) Positive control. The gesture has to have DONE something, or every
//     "unchanged" assertion below passes for the wrong reason.
check("the drag moved the unmuted members at all",
  !near(gain(8), 0.75) && !near(gain(9), -0.125),
  `gains 8,9 = ${gain(8)}, ${gain(9)}`);

// (b) THE RULE, per band, on a mixed selection.
check("a muted member is still muted after the group drag",
  muted(6) && muted(7),
  `band6 muted=${muted(6)} (gain ${gain(6)}), band7 muted=${muted(7)} (gain ${gain(7)})`);
check("an unmuted member is still unmuted after the group drag",
  !muted(8) && !muted(9),
  `band8 muted=${muted(8)}, band9 muted=${muted(9)}`);

// (c) The offset reached the unmuted members.
check("an unmuted member moved by exactly the shared delta",
  near(gain(8), 0.75 + DELTA) && near(gain(9), -0.125 + DELTA),
  `band8 = ${gain(8)} (want ${0.75 + DELTA}), band9 = ${gain(9)} (want ${-0.125 + DELTA})`);

// (d) The offset reached the muted members too, UNDERNEATH the mute. This is
//     the assertion that separates "mute preserved" from "mute preserved and
//     the gesture silently did nothing".
check("a muted member's stashed level moved by the same shared delta",
  near(stash(6), (0.25 + DELTA) * 24) && near(stash(7), (-0.50 + DELTA) * 24),
  `band6 stash = ${stash(6)} (want ${(0.25 + DELTA) * 24}), `
    + `band7 stash = ${stash(7)} (want ${(-0.50 + DELTA) * 24})`);

// (e) The user-visible consequence of (d): unmuting returns the band to where
//     the GROUP moved it, not to where it was when it was muted.
rig.commitMany(new Map([[6, rig.mutedGainDbRef.current[6] / 24]]), true);
check("unmuting a dragged member returns it to the level the group moved it to",
  !muted(6) && near(gain(6), 0.25 + DELTA),
  `band6 = ${gain(6)} (want ${0.25 + DELTA})`);

// (f) No accumulation. A second drag delivered as many samples must land on the
//     press base plus the FINAL delta, not the base plus the sum of samples.
//     A muted member is the one at risk, because the commit writes the store
//     its base is read from.
const before7 = stash(7);
rig.drag([7, 8], 8, [0.10, 0.20, 0.30]);
check("a multi-sample drag lands on the final delta, not the sum of samples",
  near(stash(7), before7 + 0.30 * 24),
  `band7 stash = ${stash(7)} (want ${before7 + 0.30 * 24}, `
    + `runaway would be ${before7 + 0.60 * 24})`);

// (g) The native side is told. A drag whose members are ALL muted moves no
//     targetGain at all, so the publication is the only thing carrying the new
//     stashed levels down to the processor.
const pubsBefore = rig.publicationCount();
rig.drag([6, 7], 7, [0.05]);
check("an all-muted group drag still publishes to native",
  rig.publicationCount() > pubsBefore,
  `publications ${pubsBefore} -> ${rig.publicationCount()}`);

// ------------------------------------------------------------------ verdict

const failed = failures.length > 0;
for (const f of failures) console.error("FAIL: %s", f);

if (expectFail) {
  if (failed) {
    console.log("EXPECTED FAIL: the suite rejected this document (%d assertion%s)",
      failures.length, failures.length === 1 ? "" : "s");
    process.exit(0);
  }
  console.error("FAIL: the suite ACCEPTED a document it should have rejected "
    + "-- the assertions are not load bearing");
  process.exit(1);
}

if (failed) process.exit(1);
console.log("OK: a selection drag moves every member's level and leaves every "
  + "member's mute exactly as it was");
process.exit(0);
