#!/usr/bin/env node
// Executes the shipped snapshot-clearing code out of the materialized document
// and MEASURES what it does.
//
// THE CAPABILITY WAS ABSENT. Measured on the revision before this one, with
// controls on the same instruments: `clear_snapshot` read 0 C++ files and 0
// occurrences in the shipping document, against `capture_snapshot` at 6 files
// and 1 occurrence. A filled slot could only be OVERWRITTEN, never emptied.
//
// WHICH IS WHY RESET ALL LIED. It advertises "gains . view . snapshots" and
// cleared two of the three: the editor dropped its own mirror and told the
// processor nothing, because there was nothing to tell it. The bank kept both
// slots `populated`, and the next full projection re-lit the dots from them --
// `acceptNativeState` sets `snapshotStatus` from `state.snapshots` -- so the
// reset visibly undid itself. That is the assertion this suite leads with,
// because it is a defect independent of any gesture.
//
// THE GESTURE is right-click on a FILLED RECALL button: not a capture button,
// where it would be ambiguous with the capture it sits beside, and not an
// empty one, which has nothing to do. `SPECTR_CLICK` cannot deliver a
// right-click at all and never delivers `pointerdown`, so the component's
// props are read directly instead of driven through the host.
//
// Usage:
//   node test_materialized_clear_snapshot.mjs <materialized-document.runtime.json>
//        [--plant-no-native-clear] [--plant-reset-keeps-bank]
//        [--plant-anything-clearable] [--plant-status-sticks] [--expect-fail]

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantNoNative = args.includes("--plant-no-native-clear");
const plantResetKeeps = args.includes("--plant-reset-keeps-bank");
const plantAnything = args.includes("--plant-anything-clearable");
const plantStatusSticks = args.includes("--plant-status-sticks");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_clear_snapshot.mjs <runtime.json> [--plant-...] [--expect-fail]");
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

function replaceExactlyOnce(source, needle, replacement, label) {
  const n = source.split(needle).length - 1;
  if (n !== 1) throw new Error(`${label}: anchor occurs ${n} times, expected 1`);
  return source.replace(needle, replacement);
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

if (plantNoNative) {
  html = replaceExactlyOnce(html,
    '        if (nativeOwnsSnapshots) {\n          issueNativeCommand(\n            "clear_snapshot",\n            { slot },\n            "SNAPSHOT " + slot + " CLEARED"\n          );\n          return;\n        }\n',
    "",
    "--plant-no-native-clear");
  notes.push("planted the mirror-only clear (the editor forgets, the processor does not)");
}

if (plantResetKeeps) {
  html = replaceExactlyOnce(html,
    '          issueNativeCommand("clear_snapshot", { slot: "A" }, null, true);\n          issueNativeCommand("clear_snapshot", { slot: "B" }, null, true);\n',
    "",
    "--plant-reset-keeps-bank");
  notes.push("planted the pre-fix RESET ALL (clears the mirror, keeps the bank)");
}

if (plantAnything) {
  html = replaceExactlyOnce(html,
    "onContextMenu: onClear && !isCapture && filled ? (event) => {",
    "onContextMenu: onClear ? (event) => {",
    "--plant-anything-clearable");
  notes.push("planted the unscoped gesture (a capture button and an empty slot accept it)");
}

if (plantStatusSticks) {
  html = replaceExactlyOnce(html,
    '    if (/SNAPSHOT ([AB]) CLEARED/.test(msg)) {',
    '    if (false) {',
    "--plant-status-sticks");
  notes.push("planted the sticky dot (a cleared slot keeps its lit indicator)");
}

for (const n of notes) console.log(n);

// -------------------------------------------------- 1. the bank's clear lane

function bankRig(fn, invoke) {
  const src = blockAt(html, fn, fn);
  const commands = [];
  const statuses = [];
  const state = { snapshots: { A: {}, B: {} } };
  const bank = new Function("record", "status", "state", "N", `
    const nativeOwnsSnapshots = true;
    const issueNativeCommand = (type, payload, successStatus, skipState) =>
      record({ type, payload, successStatus, skipState });
    const onStatus = status;
    const snapshotsRef = { current: state.snapshots };
    const setSnapshots = (next) => { state.snapshots = next; };
    const setGains = () => {};
    const setSelection = () => {};
    const setView = () => {};
    const setMorph = () => {};
    const targetGainsRef = { current: new Array(N).fill(0) };
    const renderGainsRef = { current: new Array(N).fill(0) };
    const nativeEditPendingRef = { current: false };
    const bank = { ${src} };
    return bank;
  `)((c) => commands.push(c), (s) => statuses.push(s), state, 8);
  invoke(bank);
  return { commands, statuses, state };
}

let clearRun;
try {
  clearRun = bankRig("clearSnap: (slot) => {", (b) => b.clearSnap("A"));
} catch (error) {
  failures.push(`clearSnap rig: ${error.message}`);
  clearRun = { commands: [], statuses: [], state: { snapshots: {} } };
}

check("clearing a slot tells the PROCESSOR, not only the editor's mirror",
  clearRun.commands.some((c) => c.type === "clear_snapshot"),
  `commands = ${JSON.stringify(clearRun.commands)}`);
check("the clear names the slot it is emptying",
  clearRun.commands.some((c) => c.type === "clear_snapshot" && c.payload && c.payload.slot === "A"),
  `commands = ${JSON.stringify(clearRun.commands)}`);
check("the editor's own mirror drops the slot",
  clearRun.state.snapshots && clearRun.state.snapshots.A === null,
  `mirror = ${JSON.stringify(clearRun.state.snapshots)}`);
check("the other slot is untouched",
  clearRun.state.snapshots && clearRun.state.snapshots.B !== null);
check("the clear announces itself, as every destructive action here does",
  clearRun.commands.some((c) => /SNAPSHOT A CLEARED/.test(c.successStatus || "")),
  `statuses = ${JSON.stringify(clearRun.commands.map((c) => c.successStatus))}`);

// ------------------------------------------- 2. RESET ALL means what it says

let resetRun;
try {
  resetRun = bankRig("resetAll: () => {", (b) => b.resetAll());
} catch (error) {
  failures.push(`resetAll rig: ${error.message}`);
  resetRun = { commands: [], statuses: [], state: {} };
}
const resetClears = resetRun.commands.filter((c) => c.type === "clear_snapshot");
check("RESET ALL clears the PROCESSOR's bank, not just the mirror",
  resetClears.length === 2,
  `issued ${resetClears.length} clear_snapshot command(s) -- without both, the `
    + `next full projection re-lights the dots from \`populated\``);
check("RESET ALL clears BOTH slots",
  resetClears.some((c) => c.payload.slot === "A")
    && resetClears.some((c) => c.payload.slot === "B"),
  `slots = ${JSON.stringify(resetClears.map((c) => c.payload && c.payload.slot))}`);
check("RESET ALL's clears skip the state fan-out the gains publication carries",
  resetClears.every((c) => c.skipState === true));

// ------------------------------------------------------- 3. the gesture scope

// `blockAt` balances from the first `{` after its anchor, which for a
// function with a DESTRUCTURED parameter list is the parameter brace, not the
// body. This takes the full signature -- which ends with the body brace -- and
// balances from there.
function fnAt(source, signature, label) {
  const at = source.indexOf(signature);
  if (at < 0) throw new Error(`${label}: signature not found`);
  if (source.indexOf(signature, at + 1) >= 0) throw new Error(`${label}: signature is not unique`);
  let depth = 0, inLine = false, inString = null;
  for (let i = at + signature.length - 1; i < source.length; i++) {
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
  throw new Error(`${label}: unbalanced function body`);
}

function snapBtnProps(props) {
  const src = fnAt(html,
    "function SnapBtn({ id, action, slot, filled, onClick, onClear, capture, label }) {",
    "SnapBtn");
  return new Function("props", `
    const useStateChrome = (initial) => [initial, () => {}];
    const React = { createElement: (tag, p) => ({ tag, props: p }) };
    ${src};
    return SnapBtn(props);
  `)(props);
}

const cases = [
  ["a FILLED RECALL button accepts the clear gesture",
   { action: "recall", slot: "A", filled: true, onClear: () => {}, label: "A" }, true],
  ["an EMPTY recall button does not -- there is nothing to clear",
   { action: "recall", slot: "A", filled: false, onClear: () => {}, label: "A" }, false],
  ["a CAPTURE button does not -- the gesture would be ambiguous with capture",
   { action: "capture", slot: "A", filled: true, capture: true, onClear: () => {}, label: "A" }, false],
  ["a button given no clear handler does not",
   { action: "recall", slot: "A", filled: true, label: "A" }, false],
];
for (const [label, props, want] of cases) {
  let el;
  try { el = snapBtnProps(props); }
  catch (error) { failures.push(`SnapBtn rig: ${error.message}`); continue; }
  const has = typeof (el && el.props && el.props.onContextMenu) === "function";
  check(label, has === want, `onContextMenu is ${has ? "wired" : "absent"}`);
}

// The gesture must swallow the press, or the host's own context menu opens
// over the editor at the same time.
try {
  let fired = 0;
  let prevented = false;
  const el = snapBtnProps({ action: "recall", slot: "B", filled: true,
                            onClear: () => { fired += 1; }, label: "B" });
  el.props.onContextMenu({ preventDefault: () => { prevented = true; } });
  check("the clear gesture runs its handler", fired === 1);
  check("the clear gesture claims the press", prevented);
} catch (error) { failures.push(`SnapBtn gesture: ${error.message}`); }

// Filled/empty is readable by a probe rather than only by colour.
try {
  const on = snapBtnProps({ action: "recall", slot: "A", filled: true, label: "A" });
  const off = snapBtnProps({ action: "recall", slot: "A", filled: false, label: "A" });
  check("a slot's state is legible to a probe, not only to an eye",
    on.props["data-spectr-snapshot-filled"] === "true"
      && off.props["data-spectr-snapshot-filled"] === "false");
  // The affordance the issue asked to be judged: filled/empty is NOT
  // opacity-only. Four painted channels plus the interactive state.
  check("filled/empty differs in opacity",
    on.props.style.opacity !== off.props.style.opacity);
  check("filled/empty differs in background",
    on.props.style.background !== off.props.style.background);
  check("filled/empty differs in border",
    on.props.style.border !== off.props.style.border);
  check("filled/empty differs in text colour",
    on.props.style.color !== off.props.style.color);
  check("an empty recall button is disabled, not merely dim",
    off.props.disabled === true && on.props.disabled === false);
} catch (error) { failures.push(`SnapBtn affordance: ${error.message}`); }

// ------------------------------------------ 4. a cleared slot goes dark

check("a CLEARED status drives the indicator down",
  html.includes("if (/SNAPSHOT ([AB]) CLEARED/.test(msg)) {")
    && html.includes("setSnapshotStatus((s) => ({ ...s, [slot]: false }));"),
  "without the falling edge a cleared slot keeps its lit dot, its enabled "
    + "recall button and an enabled morph slider over a slot that is gone");
check("...and the rising edge still works",
  html.includes("setSnapshotStatus((s) => ({ ...s, [slot]: true }));"));
// The morph precondition reads the same two flags, so clearing re-disables
// the control and brings its caption back with no extra wiring.
check("the morph control still gates on the same two flags",
  html.includes('hasBoth ? "enabled" : "disabled"')
    && html.includes('hasA ? "SET B TO MORPH" : (hasB ? "SET A TO MORPH" : "SET A + B TO MORPH")'));

// --------------------------------------------------------------- verdict

for (const f of failures) console.error(`  FAIL  ${f}`);
const failed = failures.length > 0;
if (expectFail) {
  if (failed) {
    console.log(`OK (control): the planted defect was rejected (${failures.length} finding(s))`);
    process.exit(0);
  }
  console.error("FAIL (control): the planted defect was NOT rejected -- the "
    + "suite cannot see this regression, so its clean run proves nothing");
  process.exit(1);
}
if (failed) { console.error(`FAIL: ${failures.length} finding(s)`); process.exit(1); }
console.log("OK: a slot can be emptied, RESET ALL empties both, and the "
  + "gesture is scoped to a filled recall button");
process.exit(0);
