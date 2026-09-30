#!/usr/bin/env python3
"""Wire the header's freeze toggle to the Freeze parameter.

The toggle added by patch_materialized_freeze_toggle.py kept its state to
itself. It now reads and writes host parameter 3 (Freeze):

- A press writes the parameter through `param_set`, the same route the Output
  trim takes, so the host records it and automation can replay it.
- The processor's projections carry `freeze.frozen`: hydration when the
  editor opens, and the LIVE per-revision projection that host automation
  already produces. One shared store listens to both and wakes only the
  toggle, and only when the value moves -- a projection that leaves Freeze
  where it was commits nothing, and one that moves it re-renders this one
  button, never the document.
- Q toggles it, under the plain-key policy every other letter follows: live in
  the standalone, and in a DAW only with "Keyboard shortcuts in DAW" on. The
  tooltip names the key only while the key would work, and the SHORTCUTS
  panel lists it on the same condition.
- Settings gains a FREEZE group with a "Hold length" slider. Like Latency it
  is not a host parameter: it rides the hydration payload, writes through
  `freeze_hold_set`, and renders nothing until the processor has answered.

WHY A SCRIPT: the shipping document is one minified line and the materialized
generator cannot rebuild it. Each patch point is asserted to occur exactly
once, a half-patched document is refused, and a second run reports "already
applied".

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "function spectrFreezeStore()"

TOGGLE_START = "function SpectrFreezeToggle() {\n"
TOGGLE_END = "function SpectrOutputMeter({ latchOver = false } = {}) {\n"

TOGGLE_BODY = '''// Freeze (host parameter 3). The processor owns the value; this is the
// editor's cached copy, fed by hydration and by the live projection host
// automation produces, and written on a press or the Q key. Listeners are
// woken only when the value actually moves, so a projection that leaves
// Freeze alone commits nothing and one that moves it re-renders the toggle
// alone.
function spectrFreezeStore() {
  const store = globalThis.__spectrFreeze
    || (globalThis.__spectrFreeze = { frozen: false, hold: null, listeners: [] });
  if (!store.subscribed && window.pulp && typeof window.pulp.on === "function") {
    store.subscribed = true;
    const accept = (message) => spectrFreezeAccept(message && message.payload);
    window.pulp.on("processing_state_hydrate", accept);
    window.pulp.on("processing_state_live", accept);
  }
  return store;
}
function spectrFreezeNotify(store) {
  store.listeners.slice().forEach((fn) => {
    try { fn(); } catch (error) {
      console.error("[Spectr] freeze listener failed", error);
    }
  });
}
function spectrFreezeAccept(payload) {
  const freeze = payload && payload.freeze;
  if (!freeze || typeof freeze !== "object") return;
  const store = spectrFreezeStore();
  let moved = false;
  if (typeof freeze.frozen === "boolean" && freeze.frozen !== store.frozen) {
    store.frozen = freeze.frozen;
    moved = true;
  }
  // The hold length rides hydration only; update on presence.
  if (typeof freeze.hold_seconds === "number" && isFinite(freeze.hold_seconds)) {
    const hold = {
      seconds: freeze.hold_seconds,
      min: Number(freeze.min_hold_seconds),
      max: Number(freeze.max_hold_seconds),
      fallback: Number(freeze.default_hold_seconds)
    };
    const was = store.hold;
    if (!was || was.seconds !== hold.seconds || was.min !== hold.min
        || was.max !== hold.max) {
      store.hold = hold;
      moved = true;
    }
  }
  if (moved) spectrFreezeNotify(store);
}
// Cached copy first, so the face flips on the press, then the processor,
// which owns the value and reports it to the host.
function spectrSetFrozen(frozen) {
  const store = spectrFreezeStore();
  const next = frozen === true;
  if (store.frozen !== next) {
    store.frozen = next;
    spectrFreezeNotify(store);
  }
  if (window.pulp && typeof window.pulp.postMessage === "function") {
    try {
      Promise.resolve(window.pulp.postMessage("param_set",
        { id: 3, value: next ? 1 : 0 }, "spectr-freeze")).catch((error) =>
          console.error("[Spectr] freeze write failed", error));
    } catch (error) {
      console.error("[Spectr] freeze write failed", error);
    }
  }
  return next;
}
function spectrToggleFreeze() {
  return spectrSetFrozen(!spectrFreezeStore().frozen);
}
window.spectrToggleFreeze = spectrToggleFreeze;
function SpectrFreezeToggle() {
  // Two faces from the design's FreezeToggle, token for token. The face
  // follows the Freeze parameter through the shared store, and the store
  // wakes this button alone.
  const store = spectrFreezeStore();
  const [frozen, setFrozen] = React.useState(store.frozen === true);
  const keyboardPolicy = typeof useSpectrKeyboardPolicy === "function"
    ? useSpectrKeyboardPolicy() : { active: true };
  React.useEffect(() => {
    const sync = () => setFrozen(store.frozen === true);
    store.listeners.push(sync);
    sync();
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  const accent = frozen ? "hsl(35,90%,65%)" : "hsl(150,75%,60%)";
  return /* @__PURE__ */ React.createElement("button", {
    "data-spectr-freeze-toggle": true,
    "data-spectr-freeze-state": frozen ? "frozen" : "live",
    "aria-pressed": frozen,
    "aria-label": "Freeze",
    // The key is named only where it works: a DAW keeps its letter keys
    // unless the user gave them to Spectr.
    title: "Freeze the incoming sound" + (keyboardPolicy.active ? " (Q)" : ""),
    onClick: () => spectrToggleFreeze(),
    // Fixed at the FROZEN face's width -- 2 border + 20 padding + 6 glyph +
    // 6 gap + six 10pt mono glyphs at 1pt tracking (42) -- so a press never
    // moves the output controls beside it.
    style: { width: 76, minWidth: 76, flexShrink: 0, height: 24, boxSizing: "border-box", padding: "0 10px", borderRadius: 3, display: "flex", alignItems: "center", justifyContent: "center", gap: 6, cursor: "pointer", background: frozen ? "rgba(200,140,60,0.18)" : "rgba(255,255,255,0.03)", border: "1px solid " + (frozen ? "rgba(240,180,110,0.45)" : "rgba(255,255,255,0.1)"), color: frozen ? "hsl(35,90%,75%)" : "rgba(255,255,255,0.75)", fontFamily: @FACE@, fontSize: 10, letterSpacing: 1, lineHeight: 1 }
  }, /* @__PURE__ */ React.createElement("span", {
    "data-spectr-freeze-glyph": frozen ? "square" : "dot",
    "aria-hidden": true,
    style: { width: 6, height: 6, flexShrink: 0, borderRadius: frozen ? 1 : "50%", background: accent, boxShadow: "0 0 6px " + accent }
  }), /* @__PURE__ */ React.createElement("span", {
    "data-spectr-freeze-label": true,
    // No line-height override: a one-line box at the face's natural height
    // centres its capitals on the same line as the captions beside it.
    style: { fontWeight: 600, whiteSpace: "nowrap", flexShrink: 0 }
  }, frozen ? "FROZEN" : "LIVE"));
}
'''

# Defined beside the Latency group, inside the Settings surface, so the
# Settings panel's own scripts carry it.
SETTINGS_FUNCTION = '''// Settings > FREEZE. Not a host parameter, so like Latency it reads the
// hydration payload and renders nothing until the processor has answered.
function SpectrFreezeSettings() {
  // Guarded because this component is also evaluated on its own, away from
  // the store's script: there it has no hydration and renders nothing.
  const store = typeof spectrFreezeStore === "function"
    ? spectrFreezeStore() : globalThis.__spectrFreeze;
  const [, setRevision] = React.useState(0);
  React.useEffect(() => {
    if (!store || !Array.isArray(store.listeners)) return undefined;
    const sync = () => setRevision((n) => n + 1);
    store.listeners.push(sync);
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  const hold = store && store.hold;
  if (!hold || !(hold.max > hold.min)) return null;
  const write = (seconds) => {
    const next = Math.max(hold.min, Math.min(hold.max, seconds));
    store.hold = Object.assign({}, hold, { seconds: next });
    setRevision((n) => n + 1);
    if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
    Promise.resolve(window.pulp.postMessage("freeze_hold_set", { seconds: next },
      "spectr-freeze-hold")).catch((error) =>
        console.error("[Spectr] hold length write failed", error));
  };
  return /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: "freeze", title: "FREEZE", subtitle: "Freeze holds the sound coming in and keeps playing it through everything you draw." }, /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Hold length", hint: "How much of the incoming sound the next freeze takes in" }, /* @__PURE__ */ React.createElement("div", { "data-spectr-freeze-hold": true }, /* @__PURE__ */ React.createElement(SpectrSettingsSlider, {
    value: hold.seconds,
    min: hold.min,
    max: hold.max,
    step: 0.01,
    onChange: write,
    fmt: (seconds) => seconds < 1 ? Math.round(seconds * 1000) + " ms" : seconds.toFixed(2) + " s"
  }))));
}
'''
SETTINGS_FUNCTION_ANCHOR = ("// The close button owns its hover and press look, so pointer-enter and\n")

SHORTCUT_OLD = ('keyboardPolicy.active && /* @__PURE__ */ React.createElement(Hrow, '
                '{ k: "M" }, "Mute/unmute selection"), ')
SHORTCUT_NEW = ('keyboardPolicy.active && /* @__PURE__ */ React.createElement(Hrow, '
                '{ k: "M" }, "Mute/unmute selection"), keyboardPolicy.active && '
                '/* @__PURE__ */ React.createElement(Hrow, { k: "Q" }, '
                '"Freeze / unfreeze"), ')

KEY_OLD = '''      // No key cycles the analyzer. A and 6 did, and A is a Musical Typing
      // note; the ANALYZER menu is the one way to change it.
'''
KEY_NEW = '''      // Freeze, on the key the SHORTCUTS panel advertises. Q is not a
      // Musical Typing key, and the guard above already applied the
      // plain-key policy, every modifier, text fields and overlays. It goes
      // through the same write the header toggle makes, so the host hears it.
      if (k === "q") {
        e.preventDefault();
        closeBandMenu();
        const frozen = typeof window.spectrToggleFreeze === "function"
          ? window.spectrToggleFreeze() : null;
        if (frozen === null) fireStatus("FREEZE UNAVAILABLE");
        else fireStatus(frozen ? "FROZEN" : "LIVE");
        return;
      }
''' + KEY_OLD

SETTINGS_OLD = ('React.createElement(SpectrLatencySettings, null), ')
SETTINGS_NEW = ('React.createElement(SpectrLatencySettings, null), '
                '/* @__PURE__ */ React.createElement(SpectrFreezeSettings, null), ')


def bound_mono_face(document):
    faces = [binding.get("runtime_family")
             for binding in document.get("font_bindings", [])
             if binding.get("family") == "JetBrains Mono"
             and str(binding.get("weight")) == "400"
             and binding.get("style") == "normal"
             and str(binding.get("unicode_range", "")).startswith("U+0000-00FF")]
    if len(faces) != 1 or not faces[0]:
        sys.exit("FAIL: expected one bound JetBrains Mono 400 Basic Latin face, "
                 "found %d" % len(faces))
    return faces[0]


def once(html, text, label):
    count = html.count(text)
    if count != 1:
        sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))


def main():
    raw = open(PATH, encoding="utf-8").read()
    document = json.loads(raw)
    html = document["html"]

    if MARKER in html:
        for marker in (SHORTCUT_NEW, KEY_NEW, SETTINGS_NEW, SETTINGS_FUNCTION):
            if html.count(marker) != 1:
                sys.exit("FAIL: the freeze store is present but %r is not; the "
                         "document is half patched" % marker[:60])
        print("already applied  freeze parameter, Q, SHORTCUTS row, hold length")
        return 0

    for text, label in ((TOGGLE_START, "freeze toggle"),
                        (TOGGLE_END, "output meter"),
                        (SHORTCUT_OLD, "SHORTCUTS M row"),
                        (KEY_OLD, "App keydown tail"),
                        (SETTINGS_OLD, "Settings latency group"),
                        (SETTINGS_FUNCTION_ANCHOR, "Settings close button")):
        once(html, text, label)

    start = html.index(TOGGLE_START)
    end = html.index(TOGGLE_END)
    if end < start:
        sys.exit("FAIL: the output meter precedes the freeze toggle")
    face = ('\'"%s", "JetBrains Mono", ui-monospace, monospace\''
            % bound_mono_face(document))
    html = html[:start] + TOGGLE_BODY.replace("@FACE@", face) + html[end:]
    html = html.replace(SHORTCUT_OLD, SHORTCUT_NEW, 1)
    html = html.replace(KEY_OLD, KEY_NEW, 1)
    html = html.replace(SETTINGS_OLD, SETTINGS_NEW, 1)
    html = html.replace(SETTINGS_FUNCTION_ANCHOR,
                        SETTINGS_FUNCTION + SETTINGS_FUNCTION_ANCHOR, 1)

    document["html"] = html
    # Keep every byte outside the html payload as it was, and encode the html
    # the way tools/git/merge_materialized_runtime.py does (ensure_ascii=False).
    head, tail = raw.split('"html":"', 1)
    rest = tail.split('","mime_type":', 1)
    if len(rest) != 2:
        sys.exit("FAIL: the html payload is not followed by mime_type")
    out = (head + '"html":' + json.dumps(html, ensure_ascii=False)
           + ',"mime_type":' + rest[1])
    if json.loads(out) != document:
        sys.exit("FAIL: re-encoding the document changed more than its html")
    open(PATH, "w", encoding="utf-8").write(out)
    print("applied          freeze parameter, Q, SHORTCUTS row, hold length")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
