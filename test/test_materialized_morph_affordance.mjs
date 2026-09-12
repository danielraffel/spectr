#!/usr/bin/env node
// Executes the shipped MorphSlider out of the materialized document and
// MEASURES what it renders and what it permits, rather than asserting that the
// source says what the source says.
//
// The morph slider interpolates the band field between snapshot A and snapshot
// B, so with either slot empty it has no endpoints and does nothing. The
// shipping UI has always ENFORCED that -- `disabled` forwards to
// `View::setEnabled(false)`, `onPointerDown` and `commitFromPointer` return
// early, and `bank.setMorph` re-checks its own mirror. What it never did was
// EXPLAIN it: the control had no visible name, and its disabled state was
// conveyed by opacity alone, so a user who had not captured both slots saw a
// dim unlabelled track that ignored them. Reported as "i can't figure out how
// to see the morph slider actually morph".
//
// Three contracts:
//
// 1. REASONED. While a slot is empty the control renders a caption naming the
//    slot that is missing -- "SET A + B", "SET A", or "SET B" -- readable
//    rather than sharing the dim of the control it explains, and absolutely
//    positioned so it costs the transport row no width.
// 2. STILL GATED. Explaining the precondition must not relax it. The disabled
//    control must still refuse a pointer commit, the enabled one must still
//    accept it, and a slot going empty after both were set must disable it
//    again -- the clear-after-set case.
//
// Contract 3 is also the scope control: it drives the extracted component
// through its real pointer path, so a render that parsed but did not actually
// wire its handlers cannot pass.
//
// Usage:
//   node test_materialized_morph_affordance.mjs <materialized-document.runtime.json>
//        [--plant-wrapper-dim] [--plant-nohint] [--expect-fail]
//
// --plant-nohint removes the reason caption (the exact pre-fix track).
// --plant-wrapper-dim restores the pre-fix wrapper dim, which renders the
// caption at 35% along with the control it explains. --expect-fail inverts the
// verdict, so a control row is
// green only when the planted defect is REJECTED, and a missing file, usage
// error, or thrown extractor still fails it.

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantWrapperDim = args.includes("--plant-wrapper-dim");
const plantNoHint = args.includes("--plant-nohint");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_morph_affordance.mjs <runtime.json> "
    + "[--plant-wrapper-dim] [--plant-nohint] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const failures = [];
const notes = [];

function replaceExactlyOnce(source, needle, replacement, label) {
  const n = source.split(needle).length - 1;
  if (n !== 1) throw new Error(`${label}: anchor occurs ${n} times, expected 1`);
  return source.replace(needle, replacement);
}

// ------------------------------------------------------------------- plants

const WRAPPER_SHIPPED =
  '{ style: { display: "flex", alignItems: "center", gap: 6, '
  + 'marginLeft: 6 } }';
const WRAPPER_PRE_FIX =
  '{ style: { display: "flex", alignItems: "center", gap: 6, '
  + 'marginLeft: 6, opacity: hasBoth ? 1 : 0.35 } }';

// The caption, exactly as the patch script emits it.
const CAPTION =
  'hasBoth ? null : /* @__PURE__ */ React.createElement("div", '
  + '{ "data-spectr-morph-hint": true, style: { position: "absolute", '
  + 'left: 0, top: 1, width: "100%", height: 14, lineHeight: "14px", '
  + 'textAlign: "center", pointerEvents: "none", fontSize: 9, '
  + 'letterSpacing: 0.3, whiteSpace: "nowrap", '
  + 'color: "rgba(255,255,255,0.72)" } }, '
  + 'hasA ? "SET B" : (hasB ? "SET A" : "SET A + B"))';

if (plantNoHint) {
  html = replaceExactlyOnce(html, ',\n    ' + CAPTION, "", "--plant-nohint");
  notes.push("planted the pre-fix track (no reason caption)");
}
if (plantWrapperDim) {
  // The pre-fix wrapper dim, which drags the caption down with everything
  // else -- a reason rendered at 35% is present but not legible.
  html = replaceExactlyOnce(html, WRAPPER_SHIPPED, WRAPPER_PRE_FIX,
    "--plant-wrapper-dim");
  notes.push("planted the pre-fix wrapper dim (caption dimmed with the control)");
}
for (const n of notes) console.log(n);

// --------------------------------------------------------------- extraction

function blockAt(source, anchor, label) {
  const at = source.indexOf(anchor);
  if (at < 0) throw new Error(`${label}: anchor not found`);
  if (source.indexOf(anchor, at + 1) >= 0) throw new Error(`${label}: anchor is not unique`);
  // From the END of the anchor: a destructuring parameter list opens a brace
  // too, and capturing that one yields a signature with no body.
  const open = source.indexOf("{", at + anchor.length);
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
    else if (c === "}") { depth--; if (depth === 0) return source.slice(at, i + 1); }
  }
  throw new Error(`${label}: unbalanced block`);
}

const morphSrc = blockAt(html,
  "function MorphSlider({ bankRef, hasA, hasB })", "MorphSlider");

// --------------------------------------------------------------------- rig
// A hook harness small enough to be obviously honest: `render` resets the
// cursor and calls the REAL component, so state and refs persist across
// renders exactly as they would in React.

function makeRig() {
  const states = [];
  const refs = [];
  let stateIndex = 0;
  let refIndex = 0;

  const createElement = (type, props, ...children) => ({
    type,
    props: props || {},
    children: children.flat(Infinity).filter((c) => c !== null && c !== undefined
      && c !== false && c !== true),
  });
  const useState = (init) => {
    const i = stateIndex++;
    if (states.length <= i) states.push(typeof init === "function" ? init() : init);
    return [states[i], (next) => {
      states[i] = typeof next === "function" ? next(states[i]) : next;
    }];
  };
  const useRef = (init) => {
    const i = refIndex++;
    if (refs.length <= i) refs.push({ current: init });
    return refs[i];
  };

  const factory = new Function("React", "useStateChrome", `
    ${morphSrc};
    return MorphSlider;
  `)({ createElement, useRef }, useState);

  return {
    render(props) {
      stateIndex = 0;
      refIndex = 0;
      return factory(props);
    },
  };
}

// Walk a rendered tree collecting every node, so a caption is found wherever
// the component chose to put it.
function flatten(node, out = [], parent = null) {
  if (!node || typeof node !== "object") return out;
  node.__parent = parent;
  out.push(node);
  for (const c of node.children || []) flatten(c, out, node);
  return out;
}
// What the user actually sees: opacity multiplies down the tree, so a caption
// is only as readable as its dimmest ancestor allows.
function effectiveOpacity(node) {
  let o = 1;
  for (let n = node; n; n = n.__parent) {
    const s = n.props && n.props.style;
    if (s && typeof s.opacity === "number") o *= s.opacity;
  }
  return o;
}
function textOf(node) {
  return (node.children || []).filter((c) => typeof c === "string").join("");
}
function findByProp(nodes, prop) {
  return nodes.filter((n) => n.props && n.props[prop]);
}
function morphTrack(nodes) {
  return nodes.find((n) => n.props && n.props.id === "spectr-snapshot-morph");
}

function check(label, condition, detail) {
  if (condition) return;
  failures.push(detail ? `${label} -- ${detail}` : label);
}

// ------------------------------------------------------------- measurements

const CASES = [
  { name: "neither slot", hasA: false, hasB: false, hint: "SET A + B" },
  { name: "only A set", hasA: true, hasB: false, hint: "SET B" },
  { name: "only B set", hasA: false, hasB: true, hint: "SET A" },
  { name: "both set", hasA: true, hasB: true, hint: null },
];

for (const c of CASES) {
  const rig = makeRig();
  const tree = rig.render({ bankRef: { current: null }, hasA: c.hasA, hasB: c.hasB });
  const nodes = flatten(tree);
  const enabled = c.hasA && c.hasB;

  // 1 + 2. REASONED -- only while a slot is empty, naming that slot, and
  // readable rather than sharing the dim of the control it explains.
  const hints = findByProp(nodes, "data-spectr-morph-hint");
  if (c.hint === null) {
    check(`[reasoned] ${c.name}`, hints.length === 0,
      `an enabled morph must not explain itself, found ${hints.length} hint(s)`);
  } else {
    check(`[reasoned] ${c.name}`, hints.length === 1,
      `expected exactly 1 reason caption, found ${hints.length}`);
    if (hints.length === 1) {
      check(`[reasoned] ${c.name} text`, textOf(hints[0]) === c.hint,
        `reason reads ${JSON.stringify(textOf(hints[0]))}, expected ${JSON.stringify(c.hint)}`);
      const eff = effectiveOpacity(hints[0]);
      check(`[reasoned] ${c.name} legible`, eff >= 0.5,
        `reason caption renders at effective opacity ${eff.toFixed(3)}`);
      // It must cost the transport row no width: the row has none to give,
      // and a caption in flex flow crushed the 90px track to 39.5px.
      check(`[reasoned] ${c.name} costs no width`,
        hints[0].props.style.position === "absolute",
        `caption position is ${hints[0].props.style.position}`);
    }
  }

  // 3. STILL GATED -- the rule is unchanged, only explained.
  const track = morphTrack(nodes);
  check(`[gated] ${c.name} track present`, !!track, "no #spectr-snapshot-morph");
  if (track) {
    check(`[gated] ${c.name} disabled flag`, track.props.disabled === !enabled,
      `disabled=${track.props.disabled}, expected ${!enabled}`);
    check(`[gated] ${c.name} state attr`,
      track.props["data-spectr-morph-state"] === (enabled ? "enabled" : "disabled"),
      `state=${track.props["data-spectr-morph-state"]}`);
    check(`[gated] ${c.name} cursor`,
      track.props.style.cursor === (enabled ? "pointer" : "not-allowed"),
      `cursor=${track.props.style.cursor}`);
    // The paint dims; the track box itself must not, or the caption inside
    // it dims too.
    const thumb = findByProp(flatten(track), "data-spectr-morph-thumb")[0];
    check(`[gated] ${c.name} thumb still rendered`, !!thumb,
      "the native parity test drives the thumb in the DEFAULT disabled state");
    // A disabled morph has no position to indicate, and the thumb overhangs
    // its track by 7px -- straight onto the "A" end label, which it swallowed
    // whole in the capture. It is hidden rather than removed so the parity
    // test still resolves the node and its 14/18px geometry.
    check(`[gated] ${c.name} thumb hidden while disabled`, thumb
      && thumb.props.style.opacity === (enabled ? 1 : 0),
      `thumb opacity=${thumb && thumb.props.style.opacity}`);
    if (thumb) {
      check(`[gated] ${c.name} thumb keeps its geometry`,
        thumb.props.style.width === 14 && thumb.props.style.height === 14,
        `thumb is ${thumb.props.style.width}x${thumb.props.style.height}`);
    }
    // Geometry the native parity test resolves by id and asserts.
    check(`[gated] ${c.name} geometry`,
      track.props.style.width === 90 && track.props.style.height === 16,
      `track is ${track.props.style.width}x${track.props.style.height}`);
  }
}

// The pointer path, driven for real. This is the scope control: a component
// that parsed but never wired its handlers cannot move these counters.
function drivePointer(hasA, hasB) {
  const calls = [];
  const bankRef = { current: { setMorph: (v, defer) => calls.push([v, defer]) } };
  const rig = makeRig();
  const tree = rig.render({ bankRef, hasA, hasB });
  const track = morphTrack(flatten(tree));
  if (!track) throw new Error("pointer drive: no morph track");
  const target = {
    getBoundingClientRect: () => ({ left: 0, width: 90 }),
    setPointerCapture: () => {},
  };
  track.props.onPointerDown({ clientX: 45, pointerId: 1, currentTarget: target });
  return calls;
}

const gatedCalls = drivePointer(false, false);
check("[gated] disabled morph refuses a pointer commit", gatedCalls.length === 0,
  `bank.setMorph was called ${gatedCalls.length} time(s)`);
const singleCalls = drivePointer(true, false);
check("[gated] half-set morph refuses a pointer commit", singleCalls.length === 0,
  `bank.setMorph was called ${singleCalls.length} time(s)`);

// Positive control: the identical drive on an enabled control MUST commit, so
// the two zeros above are measuring the gate and not a dead rig.
const liveCalls = drivePointer(true, true);
check("[gated] enabled morph accepts a pointer commit", liveCalls.length === 1,
  `bank.setMorph was called ${liveCalls.length} time(s), expected 1`);
if (liveCalls.length === 1) {
  check("[gated] enabled morph commits the pointer position",
    Math.abs(liveCalls[0][0] - 0.5) < 1e-9,
    `committed t=${liveCalls[0][0]}, expected 0.5`);
}

// The clear-after-set case the user named: a slot going empty on a LIVE
// component -- same hooks, same refs -- must disable it again and restore the
// reason. A fresh mount would not prove this.
{
  const rig = makeRig();
  const bankRef = { current: { setMorph: () => {} } };
  const before = flatten(rig.render({ bankRef, hasA: true, hasB: true }));
  check("[cleared] starts enabled",
    morphTrack(before).props.disabled === false, "did not start enabled");
  check("[cleared] starts unexplained",
    findByProp(before, "data-spectr-morph-hint").length === 0,
    "an enabled morph explained itself");
  const after = flatten(rig.render({ bankRef, hasA: true, hasB: false }));
  check("[cleared] re-disables when B is cleared",
    morphTrack(after).props.disabled === true, "stayed enabled after B cleared");
  const hints = findByProp(after, "data-spectr-morph-hint");
  check("[cleared] re-explains when B is cleared", hints.length === 1
    && textOf(hints[0]) === "SET B",
    `reason after clear: ${hints.length ? JSON.stringify(textOf(hints[0])) : "none"}`);
}

// ------------------------------------------------------------------ verdict

const passed = failures.length === 0;
for (const f of failures) console.error(`FAIL: ${f}`);
if (expectFail) {
  if (passed) {
    console.error("FAIL: the planted defect was ACCEPTED -- this row proves nothing");
    process.exit(1);
  }
  console.log(`OK: planted defect rejected (${failures.length} contract failure(s))`);
  process.exit(0);
}
if (!passed) process.exit(1);
console.log("OK: morph states its precondition legibly and is still gated");
process.exit(0);
