#!/usr/bin/env node
// Guards the contract that keeps band drawing and minimap interaction cheap:
// the zoom readout is a LEAF that subscribes to the viewport, never a value
// the app root polls into root state.
//
// The app root used to run setInterval(..., 150) that read the live viewport
// off the bank handle and wrote it into root state. A memo guard kept the
// write out of unchanged frames, which made it look harmless -- but zoom is
// precisely the value that moves while the minimap is being dragged, so during
// the one interaction that has to stay cheap the root committed every 150ms,
// and a root commit re-applies the whole captured import document.
//
// Two halves, both checked here, because either alone reads green while the
// defect is present:
//
// Killing the interval is necessary and NOT sufficient. Republishing the
// readout on the viewport and coalescing it to one commit per FRAME measured
// WORSE than the poll it replaced -- a resize gesture moves the second decimal
// place nearly every frame, so per-frame coalescing is a commit per frame, and
// minimap frame gaps >= 25ms went 16 -> 29 (Perfetto) and 17 -> 42 (cadence
// probe). So the publication carries a `live` flag, the leaf ignores live
// samples outright, and the readout repaints when the gesture settles. That
// guard is the load-bearing part, and it gets its own control.
//
//   STATIC  the app root owns no interval that reads a viewport, the bank
//           publishes viewport changes with a live/settle split, and the
//           declarations sit in the function bodies they claim to (a
//           block-scoped declaration in the wrong body parses clean -- syntax
//           is not scope).
//   RUNTIME the leaf is executed for real in a vm with a hook runtime, and
//           has to subscribe, repaint when a gesture settles, and commit
//           NOTHING across a whole live gesture -- neither a resize, which
//           moves the printed value every sample, nor a pan, which does not.
//
// The runtime half cannot be delegated to Spectr-native-shot. That harness
// mounts the real document in real QuickJS and does prove mount-time scope --
// a ReferenceError planted in the leaf's render makes it fail closed -- but it
// never publishes a viewport, so the same error planted inside onViewport
// leaves it green. It proves the document loads, not that the publication
// path behaves.
//
// Usage:
//   node test_materialized_zoom_readout.mjs <materialized-document.runtime.json>
//        [--plant-poller | --plant-live-commit] [--expect-fail]
//
// --plant-poller restores the pre-fix shape: the root interval comes back and
// the readout reverts to a value read from root state.
// --plant-live-commit restores the measured REGRESSION instead: the leaf keeps
// its subscription but drops the live guard, so it commits on every sample of
// a gesture. Both are separate rows because they fail different assertions and
// a single plant that trips everything cannot show which check is load
// bearing. --expect-fail inverts the verdict, so a control is green only when
// this suite REJECTS that document, and red when the plant silently fails to
// apply or anything else goes wrong. The inversion lives here rather than in
// WILL_FAIL because WILL_FAIL accepts any non-zero exit -- a usage error or an
// unreadable file satisfied it and proved nothing.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const plantPoller = args.includes("--plant-poller");
const plantLiveCommit = args.includes("--plant-live-commit");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_zoom_readout.mjs <runtime.json> "
    + "[--plant-poller] [--expect-fail]");
  process.exit(2);
}

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

const ROOT_POLLER = `  useAppE(() => {
    const iv = setInterval(() => {
      const b = bankRef.current;
      if (!b) return;
      const v = b.view;
      const full = Math.log10(2e4) - Math.log10(20);
      const span = v.lmax - v.lmin;
      const nextInfo = { N: b.N, zoom: (full / span).toFixed(2) };
      setInfo((previous) => previous.N === nextInfo.N && previous.zoom === nextInfo.zoom ? previous : nextInfo);
    }, 150);
    return () => clearInterval(iv);
  }, []);
`;
const LEAF_USE = 'React.createElement(SpectrZoomReadout, { bankRef })';
const POLLED_SPAN = 'React.createElement("span", { className: "tnum", style: '
  + '{ whiteSpace: "nowrap", flexShrink: 0, minWidth: 84 } }, info.zoom, "\\xD7 zoom")';
const SUBSCRIBE_CALL = "unsubscribe = bank.subscribeViewport(onViewport);";

if (plantPoller) {
  // Every plant must be observed to apply. A plant whose needle has drifted
  // silently produces the CURRENT document, and the control then passes while
  // proving nothing at all.
  const plants = [
    ["root poller", "  const [dspMode, setDspMode] = useAppS(\"fft\");\n",
      ROOT_POLLER + "  const [dspMode, setDspMode] = useAppS(\"fft\");\n"],
    ["polled readout", LEAF_USE, POLLED_SPAN],
    ["dropped subscription", SUBSCRIBE_CALL,
      "unsubscribe = null; void bank;"],
  ];
  for (const [label, from, to] of plants) {
    if (html.split(from).length - 1 !== 1) {
      console.error(`FAIL: plant "${label}" found ${html.split(from).length - 1}`
        + " sites, expected exactly 1 -- the control cannot prove anything");
      process.exit(2);
    }
    html = html.replace(from, to);
  }
  console.log("planted   the pre-fix root poller and polled readout");
}

if (plantLiveCommit) {
  // The measured regression, exactly: subscription intact, live guard gone,
  // so every sample of a gesture commits.
  const from = "      if (live) return;\n";
  if (html.split(from).length - 1 !== 1) {
    console.error(`FAIL: plant "live guard" found ${html.split(from).length - 1}`
      + " sites, expected exactly 1 -- the control cannot prove anything");
    process.exit(2);
  }
  html = html.replace(from, "      if (live && false) return;\n");
  console.log("planted   a readout that commits on every live sample");
}

const failures = [];
const fail = (msg) => failures.push(msg);

// ---------------------------------------------------------------- utilities

function balancedBody(source, header) {
  // Return the brace-balanced BODY that follows `header`, so a containment
  // test is about the function body and not about proximity in the file.
  // These components take a destructured parameter object, so the first brace
  // after the name opens the PARAMETER LIST, not the body -- balance the
  // parameter parens first and only then look for the body brace. Reading the
  // parameter object as the body reports every declaration as out of scope,
  // which is a loud wrong answer rather than a quiet one, but still wrong.
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
// A suite that cannot find its subject reports every document as clean. Prove
// the payload is the one we think it is before rendering any verdict.
const controls = {
  "script blocks": scriptBlocks.length,
  "app root": html.split("function App()").length - 1,
  "filter bank": html.split("function FilterBank(").length - 1,
  "chrome": html.split("function Chrome({").length - 1,
  "live viewport commits": html.split("const commitLiveViewport = ").length - 1,
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

const appBlock = scriptBlocks.find((b) => b.includes("function App()"));
const bankBody = balancedBody(html, "function FilterBank(");
const chromeBody = balancedBody(html, "function Chrome({");
const readoutBody = balancedBody(html, "function SpectrZoomReadout(");

if (!bankBody) fail("could not extract the body of FilterBank()");
if (!chromeBody) fail("could not extract the body of Chrome()");

// S1. No interval in the app root may read a viewport. The readout is the
// only thing that ever wanted one, and it is a leaf now.
if (appBlock) {
  for (const m of appBlock.matchAll(/setInterval\(\(\) => \{([\s\S]*?)\n    \}, \d+\);/g)) {
    const body = m[1];
    if (/\blmax\b|\blmin\b|\.view\b/.test(body)) {
      fail("the app root polls the viewport on an interval, so every tick "
        + "that moves the value commits the whole imported document: "
        + body.trim().split("\n")[0]);
    }
  }
} else {
  fail("no script block declares function App()");
}

// S2/S3. Scope, not proximity: the publisher and its call sites have to live
// in FilterBank's own body. A const declared in a sibling body still parses.
if (bankBody) {
  if (!bankBody.includes("const notifyViewportListeners = ")) {
    fail("FilterBank() does not declare notifyViewportListeners");
  }
  if (!bankBody.includes("const subscribeViewport = ")) {
    fail("FilterBank() does not declare subscribeViewport");
  }
  // The declaration reads `const notifyViewportListeners = (live) => {`, so a
  // name-plus-paren needle counts call sites only.
  const callsInBank = bankBody.split("notifyViewportListeners(").length - 1;
  const callsInDoc = html.split("notifyViewportListeners(").length - 1;
  if (callsInBank !== callsInDoc) {
    fail(`${callsInDoc - callsInBank} notifyViewportListeners() call site(s) `
      + "sit outside FilterBank(), where the declaration is not in scope");
  }
  // Four viewport writers: setView, the settle helper the deferred wheel
  // commit shares, the live drag commit, and the native projection. Missing
  // any one leaves a readout that silently stops moving.
  if (callsInBank < 4) {
    fail(`only ${callsInBank} of the 4 viewport writers notify subscribers`);
  }
  // Exactly one writer may publish as live: the per-pointer-sample path. If a
  // settle path marked itself live the readout would never repaint; if the
  // sample path stopped marking itself live, every sample would commit, which
  // is the regression this split exists to prevent.
  const liveCalls = bankBody.split("notifyViewportListeners(true);").length - 1;
  const settleCalls = bankBody.split("notifyViewportListeners(false);").length - 1;
  if (liveCalls !== 1 || settleCalls < 3) {
    fail(`the publisher marks ${liveCalls} call site(s) live and ${settleCalls} `
      + "settled; expected exactly 1 live (the per-sample drag commit) and at "
      + "least 3 settled");
  }
  if (!bankBody.includes("notifyViewportListeners(true);\n  };")
      || !bankBody.includes("const commitLiveViewport = ")) {
    fail("the live publication is not the per-pointer-sample commit path");
  }
  // The deferred wheel commit settles without going through setView, so it
  // has to share the settle helper or a wheel gesture never repaints.
  if (bankBody.split("setTimeout(settleViewport, 80)").length - 1 !== 2) {
    fail("the deferred wheel commits do not settle through the publisher");
  }
  if (!bankBody.includes("subscribeViewport,")) {
    fail("the bank handle does not expose subscribeViewport, so nothing can "
      + "subscribe and the readout would sit at its initial value forever");
  }
}

// S4. The readout is rendered by Chrome as a component, not inlined as a
// value Chrome received from the root.
if (chromeBody && !chromeBody.includes(LEAF_USE)) {
  fail("Chrome() does not render SpectrZoomReadout; the readout is still a "
    + "value handed down from the app root");
}
if (!readoutBody) fail("the document declares no SpectrZoomReadout component");
else if (!/const onViewport = \(view, live\) => \{\s*\n\s*if \(live\) return;/.test(readoutBody)) {
  fail("the readout does not drop live samples outright. Coalescing them "
    + "instead -- even to one commit per frame -- measured WORSE than the "
    + "150ms poll this replaced, because a resize moves the printed value on "
    + "nearly every frame");
}
if (readoutBody && /requestAnimationFrame/.test(
      readoutBody.slice(readoutBody.indexOf("const onViewport"),
                        readoutBody.indexOf("const attach")))) {
  fail("the readout schedules a frame from the publication path; the live "
    + "path must cost nothing at all");
}
if (html.includes("info.zoom")) {
  fail("info.zoom survives: some surface still reads the polled root state");
}

// ----------------------------------------------------------- runtime check
// Syntax is not scope. Execute the block that declares the leaf, with a real
// hook runtime, and drive it.

const leafBlock = scriptBlocks.find((b) => b.includes("function SpectrZoomReadout("));
if (!leafBlock) {
  fail("no script block declares SpectrZoomReadout");
} else {
  const log = [];
  let rafQueue = [];
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
        log.push("commit:" + String(resolved));
        rerender();
      };
      return [hooks.slots[i], set];
    },
    useEffect(fn, deps) { hooks.effects.push({ fn, deps }); },
    useRef(initial) {
      const i = hooks.index++;
      if (hooks.slots.length <= i) hooks.slots[i] = { current: initial };
      return hooks.slots[i];
    },
    useMemo(fn) { return fn(); },
    useCallback(fn) { return fn; },
  };
  const sandbox = {
    React,
    console: { log() {}, warn() {}, error(...a) { log.push("console.error:" + a.join(" ")); } },
    Math,
    requestAnimationFrame(fn) { rafQueue.push(fn); return rafQueue.length; },
    cancelAnimationFrame(id) { rafQueue[id - 1] = null; },
    window: {}, document: { getElementById: () => null, querySelector: () => null,
      addEventListener() {}, removeEventListener() {} },
    Array, Object, Set, Map, String, Number, Boolean, JSON, Date, Error, Promise,
  };
  sandbox.globalThis = sandbox;
  sandbox.window = sandbox;
  const flush = () => {
    for (let i = 0; i < 12 && rafQueue.length; i++) {
      const due = rafQueue;
      rafQueue = [];
      for (const fn of due) if (fn) fn();
    }
  };

  try {
    const context = vm.createContext(sandbox);
    vm.runInContext(leafBlock, context, { filename: "spectr-leaf-block.js" });
    const Readout = context.SpectrZoomReadout;
    if (typeof Readout !== "function") {
      fail("SpectrZoomReadout is not reachable after evaluating its own "
        + "script block -- it was declared in another scope");
    } else {
      // A bank that publishes, exactly like FilterBank now does.
      const listeners = new Set();
      let unsubscribed = 0;
      const view = { lmin: Math.log10(20), lmax: Math.log10(20000) };
      const bank = {
        view,
        subscribeViewport(fn) {
          listeners.add(fn);
          return () => { listeners.delete(fn); unsubscribed++; };
        },
      };
      const bankRef = { current: bank };
      const publish = (live) => {
        for (const fn of [...listeners]) fn(view, live === true);
      };

      let element = null;
      const render = () => {
        hooks.index = 0;
        hooks.effects = [];
        element = Readout({ bankRef });
        return element;
      };
      rerender = () => { render(); };
      render();
      const cleanups = hooks.effects.map(({ fn }) => fn());
      flush();

      const text = () => String(element.children.join(""));

      if (listeners.size !== 1) {
        fail(`the leaf subscribed ${listeners.size} times, expected 1 -- it `
          + "is not driven by the viewport publisher");
      }
      if (text() !== "1.00\xD7 zoom") {
        fail(`the leaf's initial readout is ${JSON.stringify(text())}, `
          + 'expected "1.00\xD7 zoom"');
      }

      // A LIVE RESIZE gesture. This is the workload that regressed: the
      // printed value moves on nearly every sample, so anything that
      // coalesces rather than suppresses still commits per frame. Count what
      // a whole gesture costs, and require zero.
      log.length = 0;
      const printed = new Set();
      const full = Math.log10(2e4) - Math.log10(20);
      for (let i = 0; i < 200; i++) {
        view.lmax = Math.log10(20) + (full * (1 - i / 260));
        printed.add((full / (view.lmax - view.lmin)).toFixed(2));
        publish(true);
        flush();
      }
      // Control on the STIMULUS: if the gesture did not actually move the
      // printed value, "0 commits" would be trivially true and would prove
      // nothing. A resize has to sweep many distinct values.
      if (printed.size < 20) {
        fail(`the live-resize stimulus only produced ${printed.size} distinct `
          + "zoom strings, so a zero-commit result proves nothing");
      }
      const commitsOnLiveResize = log.filter((l) => l.startsWith("commit:")).length;
      if (commitsOnLiveResize !== 0) {
        fail(`a live resize of 200 samples across ${printed.size} distinct `
          + `zoom values produced ${commitsOnLiveResize} React commits; a `
          + "gesture in flight must produce none");
      }

      // A live PAN preserves the span, and must likewise cost nothing.
      log.length = 0;
      for (let i = 0; i < 200; i++) {
        view.lmin += 0.0005;
        view.lmax += 0.0005;
        publish(true);
        flush();
      }
      const commitsOnPan = log.filter((l) => l.startsWith("commit:")).length;
      if (commitsOnPan !== 0) {
        fail(`200 live pan samples produced ${commitsOnPan} React commits`);
      }

      // Settling repaints, exactly once, with the value the gesture reached.
      log.length = 0;
      publish(false);
      flush();
      const expected = (full / (view.lmax - view.lmin)).toFixed(2);
      const commitsOnSettle = log.filter((l) => l.startsWith("commit:")).length;
      if (commitsOnSettle !== 1) {
        fail(`settling produced ${commitsOnSettle} commits, expected exactly `
          + "1 -- the readout does not catch up when the gesture ends");
      }
      if (!text().startsWith(expected)) {
        fail(`after settling the readout reads ${JSON.stringify(text())}, `
          + `expected it to start with ${expected}`);
      }
      // Settling again on an unchanged viewport must be free.
      log.length = 0;
      publish(false);
      flush();
      if (log.filter((l) => l.startsWith("commit:")).length !== 0) {
        fail("an unchanged settle still commits");
      }

      // Unmount releases the subscription.
      for (const c of cleanups) if (typeof c === "function") c();
      if (listeners.size !== 0 || unsubscribed !== 1) {
        fail(`unmount left ${listeners.size} listener(s) attached `
          + `(${unsubscribed} unsubscribe call(s)) -- the bank would hold the `
          + "unmounted readout alive and keep calling into it");
      }
      console.log(`runtime   subscribed=${listeners.size + unsubscribed} `
        + `liveResizeCommits=${commitsOnLiveResize}/${printed.size}values `
        + `livePanCommits=${commitsOnPan} settleCommits=${commitsOnSettle} `
        + `unsubscribed=${unsubscribed} readout=${JSON.stringify(text())}`);
    }
  } catch (error) {
    fail(`evaluating the readout's script block threw ${error.constructor.name}`
      + `: ${error.message}`);
  }
}

// ----------------------------------------------------------------- verdict

const passed = failures.length === 0;
for (const f of failures) console.log("FAIL:", f);
if (passed) {
  console.log("PASS: the zoom readout is a leaf driven by viewport "
    + "notifications, and the app root polls nothing.");
}

if (expectFail) {
  if (passed) {
    console.error("CONTROL FAIL: the planted pre-fix document was accepted, "
      + "so this suite cannot detect the defect it claims to guard");
    process.exit(1);
  }
  console.log("CONTROL PASS: the planted pre-fix document was rejected.");
  process.exit(0);
}
process.exit(passed ? 0 : 1);
