#!/usr/bin/env node
// Proves the Latency control exists, mounts, and changes the mode by name.
//
// The render mode was saved, recalled and migrated three PRs before anything
// could set it, so the risk this file exists for is not "does the mode work"
// but "is it reachable, and does reaching it say the right thing". Three
// properties, each of which reads green if you check the wrong thing:
//
//   BY TOKEN    The chip posts the mode's stable token, never its index in the
//               option list. An index-based control works perfectly until
//               somebody reorders the chips, at which point every saved
//               project's mode silently means the other mode. Asserting that
//               a message was posted does not catch that; asserting WHAT was
//               posted does.
//
//   ON PRESENCE The control is not a host parameter, so it rides the hydration
//               payload and a live automation frame OMITS it. Hydrating on
//               truthiness rather than presence makes the control reset itself
//               on the next automation write. A test that only hydrates once
//               never sees this, so this drives a second frame WITHOUT the
//               block and requires the mode to survive it.
//
//   DERIVED     Every figure and label comes from the payload. A millisecond
//               figure typed into the panel is right at 48 kHz and wrong at
//               96 kHz, and a label typed here drifts from the one the About
//               guide and its detector agree on. So the assertions feed
//               deliberately unusual values and require them to appear.
//
// usage: test_materialized_latency_mode.mjs <runtime.json>
//        [--plant-index | --plant-post-index | --plant-no-notify
//         | --plant-truthiness
//         | --plant-typed-label]
//        [--expect-fail]
//
// Each plant restores one specific wrong implementation and fails a different
// assertion, so a single plant that trips everything cannot hide which check is
// load bearing. --expect-fail inverts the verdict: a control row is green only
// when this suite REJECTS that document.
import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantIndex = args.includes("--plant-index");
const plantTruthiness = args.includes("--plant-truthiness");
const plantTypedLabel = args.includes("--plant-typed-label");
const plantPostIndex = args.includes("--plant-post-index");
const plantNoNotify = args.includes("--plant-no-notify");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_latency_mode.mjs <runtime.json> "
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
  const hits = html.split(from).length - 1;
  if (hits !== 1) {
    console.error(`FAIL: plant "${label}" found ${hits} sites, expected `
      + "exactly 1 -- the control cannot prove anything");
    process.exit(2);
  }
  html = html.replace(from, to);
  console.log("planted   %s", label);
};

if (plantIndex) {
  plant("a control that posts the option's index instead of its token",
    "      return [option.mode, option.label];",
    "      return [String(options.indexOf(option)), option.label];");
}
if (plantTruthiness) {
  plant("hydration that updates on truthiness rather than presence",
    "    if (payload && payload.latency\n"
    + "        && typeof payload.latency.mode === 'string')\n",
    "    if (true)\n");
}
if (plantPostIndex) {
  // Leaves the chip labels correct and breaks only the WIRE value, so this is
  // the only plant the "posts the TOKEN" assertion can catch. --plant-index
  // fails the label assertion first and never reaches it, which would leave
  // the wire check unexercised.
  plant("a control that sends the option's index over the wire",
    "    Promise.resolve(window.pulp.postMessage(\"render_mode_set\","
    + " { mode: next }, \"spectr-render-mode\"))",
    "    Promise.resolve(window.pulp.postMessage(\"render_mode_set\","
    + " { mode: 1 }, \"spectr-render-mode\"))");
}
if (plantNoNotify) {
  // The ORIGINAL defect: hydration stores the payload but wakes nobody, so a
  // control mounted before the processor answered stays invisible forever.
  // Nothing else in this suite can catch it -- every other assertion hydrates
  // before mounting, which is the one ordering the shipping editor never uses.
  plant("hydration that stores the payload without waking the control",
    "        (latencyStore.listeners || []).forEach(function (fn) {",
    "        ([]).forEach(function (fn) {");
}
if (plantTypedLabel) {
  plant("a panel that types the option labels instead of reading them",
    "      return [option.mode, option.label];",
    "      return [option.mode, option.mode === \"linear_phase\" "
    + "? \"Linear Phase\" : \"Zero Latency\"];");
}

const failures = [];
const check = (name, ok, detail) => {
  if (ok) console.log("  PASS %s", name);
  else { console.log("  FAIL %s  %s", name, detail === undefined ? "" : detail);
         failures.push(name); }
};

// ---- Execute the document's OWN component, not a copy of it. -------------
const componentStart = html.indexOf("function SpectrLatencySettings()");
const componentEnd = html.indexOf("function SettingsModal(");
if (componentStart < 0 || componentEnd < 0 || componentEnd <= componentStart) {
  console.error("FAIL: the document carries no SpectrLatencySettings "
    + "component ahead of SettingsModal; the control is not present");
  process.exit(2);
}
const componentSource = html.slice(componentStart, componentEnd);

// Also execute the hydration site, so "on presence" is measured against the
// shipped code rather than restated here.
const hydrateStart = html.indexOf("    if (payload && payload.latency");
const hydrateAlt = html.indexOf("    if (true)\n");   // the truthiness plant
const hydrateEnd = html.indexOf("    const n = payload && Number(payload.n_visible);");
const hydrateFrom = hydrateStart >= 0 ? hydrateStart : hydrateAlt;
if (hydrateFrom < 0 || hydrateEnd <= hydrateFrom) {
  console.error("FAIL: the document carries no latency hydration site");
  process.exit(2);
}
const hydrateSource = html.slice(hydrateFrom, hydrateEnd);

const posted = [];
const makeSandbox = () => {
  const created = [];
  const effects = [];
  const React = {
    createElement: (type, props, ...children) => {
      const node = { type, props: props || {}, children: children.flat() };
      created.push(node);
      return node;
    },
    useState: (initial) => {
      let v = initial;
      return [v, (next) => { v = typeof next === "function" ? next(v) : next; }];
    },
    // Run effects immediately. A stub that swallowed them would leave the
    // subscription untested, which is exactly the hole that shipped a control
    // nobody could see.
    useEffect: (fn) => { const cleanup = fn(); effects.push(cleanup); },
  };
  return { React, created, effects };
};

// Deliberately unusual values: a label or a figure the panel typed rather than
// read would not reproduce these, so the assertions discriminate.
const PAYLOAD = {
  control_label: "Latency",
  mode: "linear_phase",
  samples: 10240,
  ms: 213.33333333333334,
  options: [
    { mode: "linear_phase", label: "Mixing",
      description: "Deepest cuts. Adds latency your DAW lines up automatically.",
      samples: 10240, ms: 213.33333333333334 },
    { mode: "zero_latency", label: "Tracking",
      description: "Plays in time with you. Very narrow bands cut less deeply.",
      samples: 64, ms: 1.3333333333333333 },
  ],
};

const run = (globals, hydratePayload) => {
  const { React, created } = makeSandbox();
  const sandboxWindow = {
    pulp: { postMessage: (kind, body, tag) => {
      posted.push({ kind, body, tag });
      return Promise.resolve();
    } },
  };
  const SpectrSettingsGroup = "SpectrSettingsGroup";
  const SpectrSettingsField = "SpectrSettingsField";
  const SpectrSettingsChips = "SpectrSettingsChips";
  const body = `
    ${hydratePayload !== undefined
      ? `const payload = ${JSON.stringify(hydratePayload)};
         ${hydrateSource}`
      : ""}
    ${componentSource}
    return { node: SpectrLatencySettings(), created: __created };
  `;
  const fn = new Function(
    "React", "window", "globalThis",
    "SpectrSettingsGroup", "SpectrSettingsField", "SpectrSettingsChips",
    "__created",
    body);
  return fn(React, sandboxWindow, globals,
            SpectrSettingsGroup, SpectrSettingsField, SpectrSettingsChips,
            created);
};

// ---- 1. Unhydrated: renders nothing, rather than an empty control ---------
{
  const globals = {};
  const out = run(globals, undefined);
  check("an unhydrated panel renders no Latency group",
    out.node === null, `got ${out.node === null ? "null" : "an element"}`);
}

// ---- 2. Hydrated: the group exists with both options, read from payload ---
let chips = null;
{
  const globals = {};
  const out = run(globals, { latency: PAYLOAD, snapshots: {} });
  check("hydration on presence seeds the control",
    globals.__spectrLatency && globals.__spectrLatency.state
      && globals.__spectrLatency.state.mode === "linear_phase",
    JSON.stringify(globals.__spectrLatency));
  const node = out.node;
  check("a Latency group renders", node !== null && node.type === "SpectrSettingsGroup",
    node === null ? "null" : String(node.type));
  if (node) {
    check("the group is titled LATENCY", node.props.title === "LATENCY",
      String(node.props.title));
    // The subtitle must say the switch is a setup choice, not a gesture.
    const sub = String(node.props.subtitle || "");
    check("the group says switching is a setup choice, not a gesture",
      /before a take rather than during one/.test(sub), sub);
  }
  chips = out.created.filter((n) => n.type === "SpectrSettingsChips")[0];
  check("a chip row renders", !!chips);
  if (chips) {
    const opts = chips.props.opts;
    check("both modes are offered", Array.isArray(opts) && opts.length === 2,
      JSON.stringify(opts));
    // Labels come from the payload, exactly as the ruling names them.
    check('the options are labelled "Mixing" and "Tracking", read from the payload',
      JSON.stringify(opts) === JSON.stringify([["linear_phase", "Mixing"],
                                               ["zero_latency", "Tracking"]]),
      JSON.stringify(opts));
    check("the live mode is selected", chips.props.value === "linear_phase",
      String(chips.props.value));
  }
  const field = out.created.filter((n) => n.type === "SpectrSettingsField")[0];
  if (field) {
    const hint = String(field.props.hint || "");
    // 213 from the payload's ms, not typed: the payload carries 213.3333...,
    // so a panel that printed a typed "213 ms" would still pass -- but one
    // that printed the RAW number, or the wrong mode's number, would not.
    check("the hint carries the live mode's derived figure",
      hint.includes("213 ms") && !hint.includes("1.3 ms"), hint);
    check("the hint carries the live mode's guidance line",
      hint.includes("Deepest cuts"), hint);
  } else {
    check("a settings field renders", false);
  }
}

// ---- 3. Changing the mode posts the TOKEN, not an index ------------------
{
  posted.length = 0;
  if (chips && typeof chips.props.onChange === "function") {
    chips.props.onChange("zero_latency");
    const msg = posted[0];
    check("changing the mode posts exactly one message", posted.length === 1,
      JSON.stringify(posted));
    check("it posts render_mode_set", !!msg && msg.kind === "render_mode_set",
      msg && msg.kind);
    check("it posts the mode's TOKEN, not its index",
      !!msg && msg.body && msg.body.mode === "zero_latency",
      msg && JSON.stringify(msg.body));
  } else {
    check("the chip row exposes an onChange", false);
  }
}

// ---- 4. Re-selecting the live mode posts nothing -------------------------
{
  posted.length = 0;
  if (chips && typeof chips.props.onChange === "function") {
    chips.props.onChange("linear_phase");
    check("re-selecting the live mode posts nothing",
      posted.length === 0, JSON.stringify(posted));
  }
}

// ---- 5. A live automation frame omits the block and must not clobber -----
{
  const globals = {};
  run(globals, { latency: PAYLOAD, snapshots: {} });
  const before = globals.__spectrLatency && globals.__spectrLatency.state
    && globals.__spectrLatency.state.mode;
  // The second frame is a live projection: no latency block at all.
  run(globals, { snapshots: {} });
  const after = globals.__spectrLatency && globals.__spectrLatency.state
    && globals.__spectrLatency.state.mode;
  check("a frame without the latency block leaves the mode alone",
    before === "linear_phase" && after === "linear_phase",
    `before=${before} after=${after}`);
}

// ---- 6. THE ORDERING THE SHIPPING EDITOR ACTUALLY USES -------------------
// The editor mounts before the processor answers, so the payload arrives
// AFTER first paint. The first version of this control read the global once at
// mount and never asked again, so it stayed invisible forever -- and every
// assertion above passed, because they all hydrate before mounting.
{
  const globals = {};
  const { React, created, effects } = makeSandbox();
  const sandboxWindow = { pulp: { postMessage: () => Promise.resolve() } };
  const mount = new Function(
    "React", "window", "globalThis",
    "SpectrSettingsGroup", "SpectrSettingsField", "SpectrSettingsChips",
    componentSource + "\n return SpectrLatencySettings();");
  const first = mount(React, sandboxWindow, globals,
    "SpectrSettingsGroup", "SpectrSettingsField", "SpectrSettingsChips");
  check("before any payload the control renders nothing", first === null,
    first === null ? "null" : "an element");
  check("mounting registers a hydration listener",
    !!(globals.__spectrLatency && Array.isArray(globals.__spectrLatency.listeners)
       && globals.__spectrLatency.listeners.length === 1),
    JSON.stringify(globals.__spectrLatency && globals.__spectrLatency.listeners
      && globals.__spectrLatency.listeners.length));

  // Now deliver the payload, exactly as the processor does, and require the
  // mounted control to be woken.
  let woke = 0;
  if (globals.__spectrLatency && globals.__spectrLatency.listeners) {
    globals.__spectrLatency.listeners.length = 0;
    globals.__spectrLatency.listeners.push(() => { woke += 1; });
  }
  const hydrate = new Function("payload", "globalThis", "console",
    hydrateSource + "\n return true;");
  hydrate({ latency: PAYLOAD, snapshots: {} }, globals, console);
  check("a payload arriving after mount wakes the control", woke === 1,
    `listener fired ${woke} time(s)`);
}

console.log("");
if (failures.length === 0) {
  if (expectFail) {
    console.error("CONTROL FAILED: the suite ACCEPTED the planted document, "
      + "so the assertions it was meant to exercise prove nothing");
    process.exit(1);
  }
  console.log("all checks passed");
  process.exit(0);
}
console.log("%d check(s) failed: %s", failures.length, failures.join(", "));
if (expectFail) {
  console.log("CONTROL OK: the planted defect was rejected");
  process.exit(0);
}
process.exit(1);
