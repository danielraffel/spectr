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
// The first attempt at explaining it put the sentence INSIDE the slider's own
// groove, so a half-configured control read as `A ---- SET B ---- B`. Reported
// as "it seems like a bug the way it's displayed I like the intent" -- the
// intent was right, the placement read as a rendering fault. The Pulp Design
// System settles it: a disabled control is OPACITY ONLY with no instructional
// text on it, and guidance goes in a mono/10px/faint caption beside it.
//
// Four contracts:
//
// 1. REASONED. While a slot is empty the control renders a caption naming the
//    slot that is missing -- "SET A + B TO MORPH", "SET A TO MORPH", or
//    "SET B TO MORPH" -- readable rather than sharing the dim of the control
//    it explains, and absolutely positioned so it costs the transport row
//    neither width nor height.
// 2. NOT IN THE GROOVE. That caption must not be a descendant of the track.
//    This is the defect itself, and it is the one claim a pixel comparison
//    against the shipped build could never make: the old form rendered
//    perfectly, it was just text in a slider.
// 3. DIMMED, ONCE. The disabled state is carried by a single opacity on the
//    row -- 0.42, the value the guideline names -- and by nothing else. The
//    thumb in particular must not erase itself: "opacity only" means the
//    control still looks like a slider while it is unavailable.
// 4. STILL GATED. Explaining the precondition must not relax it. The disabled
//    control must still refuse a pointer commit, the enabled one must still
//    accept it, and a slot going empty after both were set must disable it
//    again -- the clear-after-set case.
//
// Contract 4 is also the scope control: it drives the extracted component
// through its real pointer path, so a render that parsed but did not actually
// wire its handlers cannot pass.
//
// Usage:
//   node test_materialized_morph_affordance.mjs <materialized-document.runtime.json>
//        [--plant-wrapper-dim] [--plant-nohint] [--plant-hint-in-track]
//        [--expect-fail]
//
// Three plants, each a DIFFERENT wrong implementation:
//   --plant-nohint        drops the caption entirely (the silent disabled
//                         control the affordance was written to replace).
//   --plant-hint-in-track puts the sentence back inside the 90x16 groove --
//                         the exact reported defect, verbatim. A suite that
//                         cannot reject this does not cover the bug.
//   --plant-caption-into-groove
//                         re-parents TODAY'S caption -- same wording, same
//                         mono/10px treatment -- into the groove and changes
//                         nothing else. It isolates the PLACEMENT rule from
//                         the wording rule, so the row above cannot be
//                         passing on the strength of a changed string.
//   --plant-wrapper-dim   moves the disabled dim up onto the outer box, so
//                         the caption is dimmed along with the control it
//                         explains and is present but not legible.
// --expect-fail inverts the verdict, so a control row is green only when the
// planted defect is REJECTED, and a missing file, usage error, or thrown
// extractor still fails it.

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantWrapperDim = args.includes("--plant-wrapper-dim");
const plantNoHint = args.includes("--plant-nohint");
const plantHintInTrack = args.includes("--plant-hint-in-track");
const plantIntoGroove = args.includes("--plant-caption-into-groove");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_morph_affordance.mjs <runtime.json> "
    + "[--plant-wrapper-dim] [--plant-nohint] [--plant-hint-in-track] "
    + "[--plant-caption-into-groove] [--expect-fail]");
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

// The outer box, which owns the margin and anchors the caption.
const OUTER_SHIPPED =
  '{ style: { position: "relative", marginLeft: 6, flexShrink: 0, '
  + 'justifyContent: "center" } }';
const OUTER_DIMMED =
  '{ style: { position: "relative", marginLeft: 6, flexShrink: 0, '
  + 'justifyContent: "center", opacity: hasBoth ? 1 : 0.42 } }';

// The caption, exactly as the patch script emits it: below the row, mono/10px
// at the transport bar's own faint level.
const CAPTION =
  'hasBoth ? null : /* @__PURE__ */ React.createElement("div", '
  + '{ "data-spectr-morph-hint": true, style: { position: "absolute", '
  + 'left: 0, top: 20, whiteSpace: "nowrap", pointerEvents: "none", '
  + 'fontFamily: "var(--mono)", fontSize: 10, lineHeight: "13px", '
  + 'opacity: 0.55 } }, '
  + 'hasA ? "SET B TO MORPH" : (hasB ? "SET A TO MORPH" '
  + ': "SET A + B TO MORPH"))';

// The reported defect, verbatim: the same sentence painted across the inside
// of the 90x16 groove.
const CAPTION_IN_GROOVE =
  'hasBoth ? null : /* @__PURE__ */ React.createElement("div", '
  + '{ "data-spectr-morph-hint": true, style: { position: "absolute", '
  + 'left: 0, top: 1, width: "100%", height: 14, lineHeight: "14px", '
  + 'textAlign: "center", pointerEvents: "none", fontSize: 9, '
  + 'letterSpacing: 0.3, whiteSpace: "nowrap", '
  + 'color: "rgba(255,255,255,0.72)" } }, '
  + 'hasA ? "SET B" : (hasB ? "SET A" : "SET A + B"))';

// The caption is the outer box's SECOND child, so dropping it means dropping
// one argument, not deleting text -- the parens have to stay balanced or the
// component never parses and every row fails for the wrong reason.
const TAIL_WITH_CAPTION = ', "B")), ' + CAPTION + ');';
const TAIL_WITHOUT_CAPTION = ', "B")));';
// Unique end of the thumb declaration, i.e. the last child of the track.
const TRACK_LAST_CHILD =
  'marginLeft: -((grown ? 26 : 22) * ratio), left: (100 * ratio) + "%" } })';

if (plantNoHint) {
  html = replaceExactlyOnce(html, TAIL_WITH_CAPTION, TAIL_WITHOUT_CAPTION,
    "--plant-nohint");
  notes.push("planted the silent disabled control (no reason caption at all)");
}
if (plantHintInTrack) {
  html = replaceExactlyOnce(html, TAIL_WITH_CAPTION, TAIL_WITHOUT_CAPTION,
    "--plant-hint-in-track (take the caption out from under the row)");
  html = replaceExactlyOnce(html, TRACK_LAST_CHILD,
    TRACK_LAST_CHILD + ',\n    ' + CAPTION_IN_GROOVE,
    "--plant-hint-in-track (paint it inside the groove instead)");
  notes.push("planted the reported defect (instructional text inside the groove)");
}
if (plantIntoGroove) {
  html = replaceExactlyOnce(html, TAIL_WITH_CAPTION, TAIL_WITHOUT_CAPTION,
    "--plant-caption-into-groove (take it out from under the row)");
  html = replaceExactlyOnce(html, TRACK_LAST_CHILD,
    TRACK_LAST_CHILD + ',\n    ' + CAPTION,
    "--plant-caption-into-groove (re-parent it into the track unchanged)");
  notes.push("planted the caption into the groove with its wording and "
    + "treatment untouched");
}
if (plantWrapperDim) {
  // The disabled dim moved up onto the outer box, which drags the caption
  // down with everything else -- a reason rendered at 23% is present but not
  // legible.
  html = replaceExactlyOnce(html, OUTER_SHIPPED, OUTER_DIMMED,
    "--plant-wrapper-dim");
  notes.push("planted the caption inside the dim (dimmed with the control)");
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
// `flatten` stamps __parent on every node, so ancestry is readable without
// re-walking. This is how the defect is stated: the caption may live anywhere
// EXCEPT inside the control it describes.
function isDescendantOf(node, ancestor) {
  for (let n = node && node.__parent; n; n = n.__parent) {
    if (n === ancestor) return true;
  }
  return false;
}

function check(label, condition, detail) {
  if (condition) return;
  failures.push(detail ? `${label} -- ${detail}` : label);
}

// ------------------------------------------------------------- measurements

const CASES = [
  { name: "neither slot", hasA: false, hasB: false, hint: "SET A + B TO MORPH" },
  { name: "only A set", hasA: true, hasB: false, hint: "SET B TO MORPH" },
  { name: "only B set", hasA: false, hasB: true, hint: "SET A TO MORPH" },
  { name: "both set", hasA: true, hasB: true, hint: null },
];
// The one value the design system fixes for a disabled control.
const DISABLED_OPACITY = 0.42;

for (const c of CASES) {
  const rig = makeRig();
  const tree = rig.render({ bankRef: { current: null }, hasA: c.hasA, hasB: c.hasB });
  const nodes = flatten(tree);
  const enabled = c.hasA && c.hasB;
  const track = morphTrack(nodes);

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
      // It must cost the transport row no width AND no height: the row has
      // no width to give (a caption in flex flow crushed the 90px track to
      // 39.5px), and an in-flow caption would also grow the control's box
      // from 20 to ~35px, riding the track off the centreline its 26px
      // button neighbours sit on and then dropping it back the moment the
      // second slot was captured.
      check(`[reasoned] ${c.name} costs no width or height`,
        hints[0].props.style.position === "absolute",
        `caption position is ${hints[0].props.style.position}`);

      // 2. NOT IN THE GROOVE -- the defect this change exists to fix. The old
      // form rendered flawlessly; it was simply a sentence inside a slider,
      // which no pixel comparison against the shipped build could ever call
      // wrong.
      check(`[reasoned] ${c.name} not painted inside the groove`,
        !!track && !isDescendantOf(hints[0], track),
        "the reason caption is a descendant of #spectr-snapshot-morph");
      // ...and it is not shaped like the thing that lived in there: an
      // in-groove caption spanned the track's full width and centred itself.
      check(`[reasoned] ${c.name} is a caption, not a track overlay`,
        hints[0].props.style.width === undefined
          && hints[0].props.style.textAlign === undefined,
        `caption declares width=${hints[0].props.style.width} `
        + `textAlign=${hints[0].props.style.textAlign}`);
      // The design system's caption treatment, in the app's own vocabulary:
      // the transport bar's mono token at 10px, faint.
      const st = hints[0].props.style;
      check(`[reasoned] ${c.name} caption treatment`,
        st.fontFamily === "var(--mono)" && st.fontSize === 10,
        `caption is ${st.fontFamily} at ${st.fontSize}px`);
    }
  }

  // 4. STILL GATED -- the rule is unchanged, only explained.
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
    // 3. DIMMED, ONCE. The disabled state is one opacity on the row, at the
    // value the guideline names, and the whole control inherits it.
    const trackOpacity = effectiveOpacity(track);
    check(`[dimmed] ${c.name} control`,
      Math.abs(trackOpacity - (enabled ? 1 : DISABLED_OPACITY)) < 1e-9,
      `control renders at effective opacity ${trackOpacity.toFixed(3)}, `
      + `expected ${enabled ? 1 : DISABLED_OPACITY}`);
    // Resolved out of the ALREADY-WALKED tree, not by re-flattening `track`.
    // `flatten` stamps __parent as it descends, so `flatten(track)` would
    // re-root the subtree and null out the thumb's path to the row -- which
    // reads as a thumb at full opacity inside a dimmed control, and the dim
    // assertion below would then be measuring the walk rather than the
    // component. Measured: it reported 1.000 against a row at 0.420.
    const thumb = findByProp(nodes, "data-spectr-morph-thumb")[0];
    check(`[gated] ${c.name} thumb still rendered`, !!thumb,
      "the native parity test drives the thumb in the DEFAULT disabled state");
    check(`[gated] ${c.name} thumb is inside the track`,
      !!thumb && isDescendantOf(thumb, track),
      "the thumb is not a descendant of #spectr-snapshot-morph");
    // "Opacity only" means the control still LOOKS like a slider while it is
    // unavailable. The thumb used to set `opacity: 0` on itself, because the
    // caption was painted across the same track and the thumb would have sat
    // on the text. With the caption out from under the row there is nothing
    // to hide from, and a groove with nothing in it reads as unfinished
    // rather than as disabled. So the thumb declares no opacity of its own
    // and takes the row's.
    check(`[dimmed] ${c.name} thumb declares no dim of its own`,
      thumb && thumb.props.style.opacity === undefined,
      `thumb declares opacity=${thumb && thumb.props.style.opacity}`);
    check(`[dimmed] ${c.name} thumb shares the control dim`, thumb
      && Math.abs(effectiveOpacity(thumb) - (enabled ? 1 : DISABLED_OPACITY)) < 1e-9,
      `thumb renders at effective opacity `
      + `${thumb ? effectiveOpacity(thumb).toFixed(3) : "n/a"}`);
    if (thumb) {
      // A PILL, not a circle: wider than it is tall, and fully rounded so the
      // ends are semicircular rather than merely soft. A square thumb of any
      // size fails the first clause, and a rounded-rect one fails the second.
      const { width, height, borderRadius } = thumb.props.style;
      check(`[gated] ${c.name} thumb is a pill`,
        width === 22 && height === 14 && borderRadius === 7,
        `thumb is ${width}x${height} r${borderRadius}`);
      // ...and it stays INSIDE its 90px track. The circle used a fixed
      // half-width margin, so it hung 7px past each end -- at ratio 0
      // straight onto the flanking "A" label. This reads the margin the
      // component actually rendered at its default value rather than
      // restating the formula: -7 for the old circle, 0 for an inset travel.
      check(`[gated] ${c.name} thumb does not overhang the track at min`,
        thumb.props.style.left === "0%"
          && Object.is(Math.abs(thumb.props.style.marginLeft), 0),
        `at min the thumb is at left=${thumb.props.style.left} `
        + `marginLeft=${thumb.props.style.marginLeft}`);
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
    && textOf(hints[0]) === "SET B TO MORPH",
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
