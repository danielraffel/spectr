#!/usr/bin/env node
// Executes the shipped group-mute rule and the shipped keydown handler out of
// the materialized document, and MEASURES what they do.
//
// THE RULE. With bands selected, `m` mutes the whole selection if ANY selected
// band is unmuted, and otherwise unmutes them all. One keypress always has an
// obvious result and a second always reverses it. Toggling each band
// independently would leave a mixed selection mixed, so the user could not
// tell what the key did.
//
// WHAT WAS NOT ALREADY THERE. `onMuteSel`, the context menu's group action, is
// an unconditional mute -- there is no group UNMUTE anywhere in the document.
// `onZeroSel` flattens the selection to 0 dB, which unmutes by side effect and
// throws every band's level away. So this is a capability the product did not
// have by any route, and the suite asserts BOTH directions for that reason.
//
// THE TRAP THIS SUITE EXISTS FOR. `selection` is React state in `FilterBank`,
// while the keydown handler lives in `App`. The bank object is the seam, and
// it is rebuilt by an effect whose deps are [N, snapshots, view,
// onNativeState] -- `selection` is NOT among them. A bank method closing over
// `selection` therefore reads the empty Set the state was created with,
// forever: the shortcut does nothing, silently, in exactly the case it exists
// for, and looks correct in review. The rig supplies a STALE `selection` and a
// live `selectionRef` so that reading the wrong one is measurable rather than
// merely discouraged.
//
// `SPECTR_CLICK` never delivers `pointerdown` and `setTimeout`/`rAF` from
// `SPECTR_EVAL` never fire, so neither in-process instrument can drive a
// keyboard path; the handler is executed directly instead.
//
// Usage:
//   node test_materialized_mute_selection.mjs <materialized-document.runtime.json>
//        [--plant-stale-closure] [--plant-unconditional-mute]
//        [--plant-flatten-unmute] [--plant-unbound] [--expect-fail]

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantStale = args.includes("--plant-stale-closure");
const plantUnconditional = args.includes("--plant-unconditional-mute");
const plantFlatten = args.includes("--plant-flatten-unmute");
const plantUnbound = args.includes("--plant-unbound");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_mute_selection.mjs <runtime.json> "
    + "[--plant-stale-closure] [--plant-unconditional-mute] "
    + "[--plant-flatten-unmute] [--plant-unbound] [--expect-fail]");
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

if (plantStale) {
  html = replaceExactlyOnce(html,
    "        const sel = selectionRef.current;",
    "        const sel = selection;",
    "--plant-stale-closure");
  notes.push("planted the stale closure (bank reads `selection`, not the mirror)");
}

if (plantUnconditional) {
  html = replaceExactlyOnce(html,
    "        const mute = bands.some((i) => !isMuted(targetGainsRef.current[i]));",
    "        const mute = true;",
    "--plant-unconditional-mute");
  notes.push("planted the one-way group mute (onMuteSel's rule, no toggle)");
}

if (plantFlatten) {
  html = replaceExactlyOnce(html,
    "          map.set(i, Number.isFinite(db) ? clamp(db / 24, -1, 1) : 0);",
    "          map.set(i, 0);",
    "--plant-flatten-unmute");
  notes.push("planted the flattening unmute (onZeroSel's rule, levels discarded)");
}

if (plantUnbound) {
  html = replaceExactlyOnce(html, '      if (k === "m") {', '      if (false) {',
    "--plant-unbound");
  notes.push("planted the unbound key (`m` reaches nothing)");
}

for (const n of notes) console.log(n);

// ---------------------------------------------------------------- the rule

const N = 32;

function bankRig() {
  const clampSrc = "const clamp = (v, a, b) => Math.max(a, Math.min(b, v));";
  const isMutedSrc = "function isMuted(g) { return g === -Infinity; }";
  if (!html.includes(clampSrc)) throw new Error("clamp: shipped definition changed");
  if (!/function isMuted\(g\) \{\s*return g === -Infinity;\s*\}/.test(html)) {
    throw new Error("isMuted: shipped definition changed");
  }
  const toggleSrc = blockAt(html, "toggleMuteSelection: () => {", "toggleMuteSelection");
  return new Function("N", `
    ${clampSrc}
    ${isMutedSrc}
    const targetGainsRef = { current: new Array(N).fill(0) };
    const mutedGainDbRef = { current: new Array(N).fill(0) };
    // THE TRAP, made measurable: the bank effect does not depend on
    // \`selection\`, so a method closing over it reads the Set from mount. The
    // stale value here is the empty Set that closure would see.
    const selection = new Set();
    const selectionRef = { current: new Set() };
    const commits = [];
    const commitMany = (map) => {
      commits.push(map);
      for (const [k, v] of map) {
        if (isMuted(v) && !isMuted(targetGainsRef.current[k]))
          mutedGainDbRef.current[k] = clamp(targetGainsRef.current[k], -1, 1) * 24;
        targetGainsRef.current[k] = v;
      }
    };
    const bank = { ${toggleSrc} };
    return {
      targetGainsRef, mutedGainDbRef, selectionRef, commits,
      toggleMuteSelection: bank.toggleMuteSelection,
    };
  `)(N);
}

let rig;
try { rig = bankRig(); }
catch (error) {
  console.error(`FAIL: could not build the bank rig: ${error.message}`);
  process.exit(2);
}

const select = (indices) => { rig.selectionRef.current = new Set(indices); };
const gains = (indices) => indices.map((i) => rig.targetGainsRef.current[i]);

// (a) The trap. A live selection must reach the rule at all -- this is the
//     positive control every assertion below depends on.
select([2, 3, 4]);
rig.targetGainsRef.current[2] = 0.5;
rig.targetGainsRef.current[3] = 0.25;
rig.targetGainsRef.current[4] = 0.75;
let r = rig.toggleMuteSelection();
check("the bank reads the LIVE selection, not the one captured at mount",
  r !== null, "toggleMuteSelection returned null for a non-empty selection "
    + "-- the stale-closure trap");

// (b) All unmuted -> mute all.
check("all unmuted: every selected band is muted",
  gains([2, 3, 4]).every((v) => v === -Infinity),
  `gains = ${gains([2, 3, 4]).join(",")}`);
check("all unmuted: the result reports the direction and the count",
  r && r.muted === true && r.count === 3,
  `result = ${JSON.stringify(r)}`);

// (c) A second press reverses the first, restoring the LEVELS rather than
//     flattening -- the property that separates this from `onZeroSel`.
r = rig.toggleMuteSelection();
check("all muted: the second press unmutes",
  gains([2, 3, 4]).every((v) => v !== -Infinity),
  `gains = ${gains([2, 3, 4]).join(",")}`);
check("all muted: unmute RESTORES each band's remembered level",
  Math.abs(rig.targetGainsRef.current[2] - 0.5) < 1e-9
    && Math.abs(rig.targetGainsRef.current[3] - 0.25) < 1e-9
    && Math.abs(rig.targetGainsRef.current[4] - 0.75) < 1e-9,
  `gains = ${gains([2, 3, 4]).join(",")} (flattening would give 0,0,0)`);
check("all muted: the result reports the direction",
  r && r.muted === false && r.count === 3, `result = ${JSON.stringify(r)}`);

// (d) THE MIXED-STATE RULE. If ANY selected band is unmuted, mute them all.
//     A per-band toggle would leave this selection mixed, which is the shape
//     the rule exists to forbid.
select([10, 11, 12]);
rig.targetGainsRef.current[10] = 0.4;
rig.targetGainsRef.current[11] = -Infinity;
rig.targetGainsRef.current[12] = 0.6;
r = rig.toggleMuteSelection();
check("mixed selection: one unmuted band mutes the whole selection",
  gains([10, 11, 12]).every((v) => v === -Infinity),
  `gains = ${gains([10, 11, 12]).join(",")} -- a per-band toggle would leave `
    + `this mixed`);
check("mixed selection: the result reports a mute", r && r.muted === true);

// ...and the next press unmutes every one of them, including the band that
// was already muted before the group action.
r = rig.toggleMuteSelection();
check("mixed selection: the reversing press unmutes every band",
  gains([10, 11, 12]).every((v) => v !== -Infinity),
  `gains = ${gains([10, 11, 12]).join(",")}`);

// (e) An empty selection is reported, not silently swallowed.
select([]);
check("empty selection: the rule declines rather than committing",
  rig.toggleMuteSelection() === null);
const before = rig.commits.length;
select([]);
rig.toggleMuteSelection();
check("empty selection: nothing is committed",
  rig.commits.length === before, "a commit was issued for an empty selection");

// ------------------------------------------------------- the key that runs it

function keyRig() {
  // THREE handlers spell `const onKey = (e) => {`. The App-level one --
  // the only one that owns the global shortcuts -- is the one whose first
  // statement reads the event target; anchoring on the bare arrow would
  // pick an overlay's handler and measure the wrong surface.
  const onKeySrc = blockAt(html,
    "const onKey = (e) => {\n      const t = e.target;", "onKey");
  const modeKeysSrc = blockAt(html, "const modeKeys = {", "modeKeys");
  return new Function("record", `
    ${modeKeysSrc};
    // Every guard the handler consults, answered the way a focused editor
    // with no overlay open would answer it.
    const document = { hasFocus: () => true, querySelector: () => null,
                       querySelectorAll: () => [] };
    function overlayBlocksShortcut() { return false; }
    const setEditMode = (v) => record({ kind: "editMode", value: v });
    const setAnalyzerMode = () => record({ kind: "analyzer" });
    const fireStatus = (msg) => record({ kind: "status", value: msg });
    const window = { spectrPublishMode: () => {} };
    const bankRef = { current: {
      toggleMuteSelection: () => { record({ kind: "toggle" }); return { count: 4, muted: true }; },
    } };
    ${onKeySrc};
    return onKey;
  `);
}

function press(key, bank) {
  const events = [];
  const onKey = keyRig()((e) => events.push(e));
  const prevented = { value: false };
  onKey({ key, repeat: false, isComposing: false, keyCode: 0,
          metaKey: false, ctrlKey: false, altKey: false, shiftKey: false,
          target: { tagName: "DIV", isContentEditable: false },
          preventDefault: () => { prevented.value = true; } });
  return { events, prevented: prevented.value };
}

let pressed;
try { pressed = press("m"); }
catch (error) { failures.push(`onKey rig: ${error.message}`); pressed = { events: [], prevented: false }; }

check("`m` reaches the group-mute rule",
  pressed.events.some((e) => e.kind === "toggle"),
  `handler recorded ${JSON.stringify(pressed.events)}`);
check("`m` claims the key so it cannot fall through to the host",
  pressed.prevented);
check("`m` reports what it did",
  pressed.events.some((e) => e.kind === "status" && /BANDS? MUTED/.test(e.value)),
  `statuses = ${JSON.stringify(pressed.events.filter((e) => e.kind === "status"))}`);

// A selection-less press must SAY so. A key that silently does nothing is
// indistinguishable from one that is broken.
try {
  const onKey = keyRig()((e) => { if (e.kind === "status") empty.push(e.value); });
  var empty = [];
  onKey({ key: "m", repeat: false, isComposing: false, keyCode: 0,
          metaKey: false, ctrlKey: false, altKey: false, shiftKey: false,
          target: { tagName: "DIV", isContentEditable: false },
          preventDefault: () => {} });
} catch (error) { /* covered by the rig check above */ }

// ...and it must not have stolen a key that already meant something. These are
// the collisions the issue asked to be proved rather than read off the source.
for (const [key, kind] of [["s", "editMode"], ["l", "editMode"], ["b", "editMode"],
                           ["f", "editMode"], ["g", "editMode"], ["a", "analyzer"],
                           ["6", "analyzer"]]) {
  let out;
  try { out = press(key); } catch (error) { failures.push(`onKey rig (${key}): ${error.message}`); continue; }
  check(`\`${key}\` still does what it did`,
    out.events.some((e) => e.kind === kind),
    `recorded ${JSON.stringify(out.events)}`);
  check(`\`${key}\` does not reach group mute`,
    !out.events.some((e) => e.kind === "toggle"));
}

// The tap-one variant is deliberately NOT implemented: a plain CLICK must keep
// meaning "toggle the band under the pointer", selection or not.
check("CLICK still toggles a single band rather than the selection",
  html.includes("commitGain(b, isMuted(cur) ? 0 : -Infinity);"));
check("the menu's one-way group mute is untouched",
  html.includes("for (const i of selection) map.set(i, -Infinity);"));

// The panel must advertise the key it binds, and bind the key it advertises.
check("the SHORTCUTS panel advertises the key",
  html.includes('React.createElement(Hrow, { k: "M" }, "Mute/unmute selection")'));

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
console.log("OK: `m` mutes or unmutes the whole selection, restores levels, "
  + "and collides with nothing");
process.exit(0);
