#!/usr/bin/env node
// SETTINGS SHOWS "CHECK FOR UPDATES" IN THE STANDALONE APP AND NOWHERE ELSE.
//
// The editor document is one artifact shared by Spectr.app and every plug-in
// format, so the UPDATES group cannot be gated on how it was built. It asks
// the native update service through the editor bridge
// (tools/patch_materialized_check_for_updates.py), and this suite drives the
// SHIPPING component out of the runtime document against a scripted bridge:
//
//   (a) standalone (service available): the group renders after MODULATION
//       and before ABOUT, shows the version and last-check time, and its
//       toggle / button / releases link send the right bridge messages and
//       show the result;
//   (b) plug-in (available: false), an SDK without the bridge ("unknown
//       message type"), and no dispatch at all: the group renders NOTHING;
//   (c) the once-a-second poll commits only when the status changed.
//
// Controls, each expected to FAIL (run with --expect-fail):
//   --plant-always-show   the availability gate removed
//   --plant-dead-button   the button no longer sends pulp_updates_check
//
// Usage: test_materialized_check_for_updates.mjs DOCUMENT_JSON [--plant-*] [--expect-fail]

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const documentPath = args.find((a) => !a.startsWith("--"));
const expectFail = args.includes("--expect-fail");
const plantAlwaysShow = args.includes("--plant-always-show");
const plantDeadButton = args.includes("--plant-dead-button");
if (!documentPath) {
  console.error("usage: test_materialized_check_for_updates.mjs DOCUMENT_JSON [--plant-*] [--expect-fail]");
  process.exit(2);
}

const failures = [];
const check = (ok, message) => { if (!ok) failures.push(message); };

const html = JSON.parse(readFileSync(documentPath, "utf8")).html;

function extractFunction(name) {
  const start = html.indexOf(`function ${name}(`);
  if (start < 0) throw new Error(`function ${name} not found`);
  const end = html.indexOf("\n}\n", start);
  if (end < 0) throw new Error(`function ${name} has no end`);
  return html.slice(start, end + 2);
}

// ---- wiring: exactly once, after MODULATION and before ABOUT ----
const mount = "React.createElement(SpectrUpdatesSettings, { listening: open })";
check(html.split(mount).length - 1 === 1, "Settings mounts SpectrUpdatesSettings exactly once");
const at = html.indexOf(mount);
const modulationAt = html.lastIndexOf("React.createElement(SpectrModulationSettings, { listening: open })", at);
const aboutAt = html.indexOf("React.createElement(SpectrBuildInfo", at);
check(modulationAt >= 0 && at - modulationAt < 120,
  "UPDATES follows MODULATION directly");
check(aboutAt > at && aboutAt - at < 160, "ABOUT follows UPDATES directly");

let component = [
  extractFunction("SpectrSettingsGroup"),
  extractFunction("SpectrSettingsField"),
  extractFunction("SpectrSettingsToggle"),
  extractFunction("spectrUpdatesCall"),
  extractFunction("SpectrUpdatesSettings"),
].join("\n");
if (plantAlwaysShow) {
  const gate = 'if (!status || status.available !== true) return null;';
  if (!component.includes(gate)) throw new Error("plant: availability gate not found");
  component = component.replace(gate, "if (!status) return null;");
}
if (plantDeadButton) {
  const send = 'onClick: () => take(spectrUpdatesCall("pulp_updates_check"))';
  if (!component.includes(send)) throw new Error("plant: check button wiring not found");
  component = component.replace(send, "onClick: () => {}");
}

// ---- a one-component renderer with real hook slots ----
function makeRig(dispatch) {
  const hooks = { slots: [], index: 0, effects: [], cleanups: [] };
  const timers = [];
  let commits = 0;
  let tree = null;
  const React = {
    createElement: (type, props, ...children) => ({ type, props: props || {}, children }),
    useState(initial) {
      const i = hooks.index++;
      if (hooks.slots.length <= i)
        hooks.slots[i] = typeof initial === "function" ? initial() : initial;
      return [hooks.slots[i], (next) => {
        const value = typeof next === "function" ? next(hooks.slots[i]) : next;
        if (Object.is(value, hooks.slots[i])) return;
        hooks.slots[i] = value;
        ++commits;
        render();
      }];
    },
    useRef(initial) {
      const i = hooks.index++;
      if (hooks.slots.length <= i) hooks.slots[i] = { current: initial };
      return hooks.slots[i];
    },
    useEffect(fn, deps) {
      const i = hooks.index++;
      const prev = hooks.slots[i];
      const changed = !prev || !deps || deps.some((d, k) => !Object.is(d, prev.deps[k]));
      hooks.slots[i] = { deps };
      if (changed) hooks.effects.push(fn);
    },
  };
  const sandbox = {
    React, JSON, Object, Array, String, Boolean, Math,
    setInterval: (fn) => { timers.push(fn); return timers.length; },
    clearInterval: () => {},
    console: { log() {}, warn() {}, error() {} },
  };
  sandbox.globalThis = sandbox;
  if (dispatch) sandbox.__spectrEditorDispatch = dispatch;
  const context = vm.createContext(sandbox);
  // Running the block is the parse proof; `node --check` would not be.
  vm.runInContext(component, context, { filename: "spectr-updates-settings.js" });
  const expand = (node) => {
    if (node === null || node === undefined || node === false) return null;
    if (Array.isArray(node)) return node.map(expand);
    if (typeof node !== "object") return node;
    if (typeof node.type === "function") {
      const props = { ...node.props, children: node.children };
      return expand(node.type(props));
    }
    return { ...node, children: node.children.map(expand) };
  };
  function render() {
    hooks.index = 0;
    tree = expand(React.createElement(context.SpectrUpdatesSettings, { listening: true }));
    const effects = hooks.effects.splice(0);
    for (const fn of effects) fn();
  }
  render();
  const all = () => {
    const out = [];
    const walk = (n) => {
      if (!n || typeof n !== "object") return;
      if (Array.isArray(n)) return n.forEach(walk);
      out.push(n);
      n.children.forEach(walk);
    };
    walk(tree);
    return out;
  };
  return {
    get tree() { return tree; },
    get commits() { return commits; },
    find: (attr) => all().find((n) => n.props && n.props[attr] !== undefined),
    text: () => {
      const parts = [];
      const walk = (n) => {
        if (n === null || n === undefined || n === false) return;
        if (Array.isArray(n)) return n.forEach(walk);
        if (typeof n !== "object") return parts.push(String(n));
        n.children.forEach(walk);
      };
      walk(tree);
      return parts.join(" ");
    },
    tick: () => timers.forEach((fn) => fn()),
  };
}

const STANDALONE = {
  ok: true, available: true, canCheckNow: true, automaticChecks: true,
  automaticInstall: false, stub: false, appName: "Spectr", version: "1.0.7",
  build: "1.0.7", lastCheckUnixSeconds: 0,
  releasesUrl: "https://github.com/danielraffel/spectr/releases", feedHost: "github.com",
  installer: "package",
  note: "Updates download from Spectr's GitHub releases. Installing an update quits and reopens Spectr, briefly stopping its audio, and asks for an administrator password. Updates are never installed automatically.",
  versionText: "Version 1.0.7", lastCheckText: "Last checked: never",
};

function standaloneBridge(log) {
  const state = { ...STANDALONE };
  const dispatch = (json) => {
    const request = JSON.parse(json);
    log.push(request);
    switch (request.type) {
      case "pulp_updates_get": return JSON.stringify(state);
      case "pulp_updates_check":
        state.lastCheckUnixSeconds = 1790000000;
        state.lastCheckText = "Last checked: 2026-09-21 10:13";
        return JSON.stringify({ ...state, started: true });
      case "pulp_updates_set_automatic":
        state.automaticChecks = request.payload.on === true;
        return JSON.stringify({ ...state, applied: true });
      case "pulp_updates_open_releases":
        return JSON.stringify({ ...state, opened: true });
      default: return JSON.stringify({ ok: false, error: "unknown message type" });
    }
  };
  dispatch.state = state;
  return dispatch;
}

// ---- (a) standalone ----
{
  const log = [];
  const bridge = standaloneBridge(log);
  const rig = makeRig(bridge);
  const group = rig.find("data-spectr-settings-group");
  check(group && group.props["data-spectr-settings-group"] === "updates",
    "standalone: the UPDATES group renders");
  check(rig.text().includes("Version 1.0.7 · Last checked: never"),
    "standalone: subtitle shows version and last check");
  check(rig.text().includes("asks for an administrator password"),
    "standalone: the native note is shown");
  const automatic = rig.find("data-spectr-updates-automatic");
  check(automatic && automatic.props["data-spectr-updates-automatic"] === "on",
    "standalone: the toggle reflects Sparkle's automatic-check setting");

  const button = rig.find("data-spectr-check-for-updates");
  check(button && button.props.disabled === false, "standalone: the button is enabled");
  button && button.props.onClick();
  check(log.some((r) => r.type === "pulp_updates_check"),
    "standalone: CHECK FOR UPDATES sends pulp_updates_check");
  check(rig.text().includes("Last checked: 2026-09-21 10:13"),
    "standalone: the last-check time updates after a check");

  const toggle = rig.find("data-spectr-setting-toggle");
  toggle && toggle.props.onClick();
  const set = log.find((r) => r.type === "pulp_updates_set_automatic");
  check(set && set.payload.on === false, "standalone: the toggle sends {on:false}");
  const after = rig.find("data-spectr-updates-automatic");
  check(after && after.props["data-spectr-updates-automatic"] === "off",
    "standalone: the toggle shows the new setting");

  const releases = rig.find("data-spectr-updates-releases");
  releases && releases.props.onClick();
  check(log.some((r) => r.type === "pulp_updates_open_releases"),
    "standalone: VIEW RELEASES asks the app to open the releases page");

  // (c) the poll commits only on change
  const before = rig.commits;
  rig.tick();
  check(rig.commits === before, "poll: an unchanged status makes no commit");
  // ...and a change made elsewhere (Sparkle's own prompt) does show.
  bridge.state.automaticChecks = true;
  rig.tick();
  check(rig.commits === before + 1, "poll: a changed status commits once");
  const polled = rig.find("data-spectr-updates-automatic");
  check(polled && polled.props["data-spectr-updates-automatic"] === "on",
    "poll: the toggle follows a setting changed outside the panel");
}

// ---- (b) everywhere else: nothing ----
const unavailable = {
  plugin: () => JSON.stringify({ ok: true, ...STANDALONE, available: false, note: "" }),
  "sdk-without-bridge": () => JSON.stringify({ ok: false, error: "unknown message type" }),
  "no-dispatch": null,
  "throwing-dispatch": () => { throw new Error("bridge gone"); },
};
for (const [name, dispatch] of Object.entries(unavailable)) {
  const rig = makeRig(dispatch);
  check(rig.tree === null, `${name}: the UPDATES group renders nothing`);
  check(!rig.find("data-spectr-check-for-updates"), `${name}: no Check for Updates button`);
}

if (failures.length) {
  for (const f of failures) console.error("FAIL", f);
  if (expectFail) { console.log("expected failure observed"); process.exit(0); }
  process.exit(1);
}
if (expectFail) {
  console.error("FAIL: the planted control did not fail");
  process.exit(1);
}
console.log("check-for-updates settings: OK");
