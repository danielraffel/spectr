#!/usr/bin/env python3
"""Freeze and Length modulation in the editor, and asking before overriding.

With the Freeze target on, an LFO switches LIVE/FROZEN; with the Length target
on, it picks each new freeze's loop length. This patch:

  * shows the LIVE/FROZEN state the audio owner is actually playing. The
    processor sends `freeze_display` only when it changes (who drives Freeze
    and Length, and the frozen state); the toggle writes its own DOM nodes
    for it -- paint-only, no React render per gate transition -- and its React
    render reads the same store, so a later render agrees with the paint;
  * makes a press of a modulated Freeze (button, Q, the chord: everything
    goes through spectrToggleFreeze) flip what is SHOWN, which the processor
    holds until the gate's next change;
  * backs the Setting "Ask before overriding modulation" (default ON; its row
    is in Settings > MODULATION, see
    tools/patch_materialized_modulation_settings_targets.py). While on, operating a modulated control -- Freeze,
    or a LENGTH pick while the Length target is on -- first asks, in the
    preset dialogs' style: "Freeze is being modulated by LFO 1. Turn off its
    Freeze target?" [KEEP MODULATING] [TURN OFF], with "Don't ask again".
    TURN OFF writes that LFO's target lane off through `param_edit` (a
    complete host gesture, so it records), then applies the action; KEEP
    MODULATING applies it and leaves modulation running. Return = TURN OFF,
    Escape = KEEP MODULATING. "Don't ask again" turns the Setting off.

Raw-text surgery on the escaped document. Idempotent: each edit is skipped
when its replacement is already present.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

EDITS = [
    (
        "the setting defaults on",
        '''  "holdModulationWhileEditing": true
}''',
        '''  "holdModulationWhileEditing": true,
  "askBeforeOverride": true
}''',
    ),
    (
        "the freeze store learns who drives it",
        '''    window.pulp.on("processing_state_hydrate", accept);
    window.pulp.on("processing_state_live", accept);
  }
  return store;
}''',
        '''    window.pulp.on("processing_state_hydrate", accept);
    window.pulp.on("processing_state_live", accept);
    // The processor's LIVE/FROZEN while an LFO's Freeze target drives it,
    // and which LFOs drive Freeze and Length. Sent on change only; painted,
    // never rendered. See tools/patch_materialized_modulation_freeze_override.py.
    window.pulp.on("freeze_display", (message) => {
      const p = message && message.payload;
      if (!p || typeof p.driven !== "boolean") return;
      store.modulated = p.driven ? p.frozen === true : null;
      store.freezeLfos = Array.isArray(p.freeze_lfos) ? p.freeze_lfos.map(Number) : [];
      store.lengthLfos = Array.isArray(p.length_lfos) ? p.length_lfos.map(Number) : [];
      if (typeof store.paint === "function") store.paint();
    });
  }
  return store;
}
// What the LIVE/FROZEN face shows: the modulated state while an LFO drives
// Freeze, else the parameter.
function spectrFreezeShown(store) {
  return store.modulated === true || store.modulated === false
    ? store.modulated : store.frozen === true;
}
// One request at a time, for the override dialog the App mounts.
function spectrOverrideStore() {
  return globalThis.__spectrOverride
    || (globalThis.__spectrOverride = { request: null, listeners: [] });
}
function spectrAskOverride(request) {
  const store = spectrOverrideStore();
  store.request = request;
  store.listeners.slice().forEach((fn) => { try { fn(); } catch (error) {} });
}
function spectrOverrideAsks() {
  return globalThis.__spectrAskBeforeOverride !== false;
}
// Run `action` for a control an LFO is driving: straight away when nobody
// drives it or the Setting is off, else after the user has answered. Generic
// over controls: `control` names it in the question ("Freeze", "Length", and
// any knob that becomes a target, e.g. "Intensity"), `target` is its
// ModulationTarget index (its lanes are 4020 + 20 (lfo - 1) + target), and
// `lfos` are the LFO numbers driving it.
window.spectrOverrideModulated = spectrOverrideModulated;
function spectrOverrideModulated(control, target, lfos, action) {
  if (!lfos || !lfos.length || !spectrOverrideAsks()) { action(); return; }
  spectrAskOverride({ control, target, lfos: lfos.slice(), action });
}''',
    ),
    (
        "a press flips what is shown",
        '''function spectrToggleFreeze() {
  return spectrSetFrozen(!spectrFreezeStore().frozen);
}''',
        '''function spectrToggleFreeze() {
  // The press flips what the face SHOWS. While an LFO drives Freeze that is
  // the modulated state, and the processor holds the press until the gate's
  // next change; spectrSetFrozen always reports the press, even when the
  // parameter already had that value.
  const store = spectrFreezeStore();
  const next = !spectrFreezeShown(store);
  const lfos = store.modulated === true || store.modulated === false ? store.freezeLfos : [];
  spectrOverrideModulated("Freeze", 6, lfos, () => {
    if (store.modulated === true || store.modulated === false) {
      store.modulated = next;
      if (typeof store.paint === "function") store.paint();
    }
    spectrSetFrozen(next);
  });
  return next;
}''',
    ),
    (
        "the face reads and paints the shown state",
        '''  React.useEffect(() => {
    const sync = () => setFrozen(store.frozen === true);
    store.listeners.push(sync);
    sync();
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  const accent = frozen ? "hsl(35,90%,65%)" : "hsl(150,75%,60%)";''',
        '''  React.useEffect(() => {
    const sync = () => setFrozen(store.frozen === true);
    store.listeners.push(sync);
    sync();
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  // A gate transition paints these three nodes and renders nothing.
  const buttonRef = React.useRef(null);
  const glyphRef = React.useRef(null);
  const labelRef = React.useRef(null);
  globalThis.__spectrFreezeToggleRenders = (globalThis.__spectrFreezeToggleRenders || 0) + 1;
  React.useEffect(() => {
    store.paint = () => {
      const shown = spectrFreezeShown(store);
      const button = buttonRef.current, glyph = glyphRef.current, label = labelRef.current;
      const accent = shown ? "hsl(35,90%,65%)" : "hsl(150,75%,60%)";
      if (button) {
        button.setAttribute("data-spectr-freeze-state", shown ? "frozen" : "live");
        button.setAttribute("aria-pressed", shown ? "true" : "false");
        button.setAttribute("data-spectr-freeze-modulated",
          store.modulated === true || store.modulated === false ? "true" : "false");
        button.style.background = shown ? "rgba(200,140,60,0.18)" : "rgba(255,255,255,0.03)";
        button.style.border = "1px solid " + (shown ? "rgba(240,180,110,0.45)" : "rgba(255,255,255,0.1)");
        button.style.color = shown ? "hsl(35,90%,75%)" : "rgba(255,255,255,0.75)";
      }
      if (glyph) {
        glyph.setAttribute("data-spectr-freeze-glyph", shown ? "square" : "dot");
        glyph.style.borderRadius = shown ? "1px" : "50%";
        glyph.style.background = accent;
        glyph.style.boxShadow = "0 0 6px " + accent;
      }
      if (label) label.textContent = shown ? "FROZEN" : "LIVE";
    };
    return () => { store.paint = null; };
  }, []);
  // Every render agrees with what the paint would show.
  frozen = spectrFreezeShown(store);
  const accent = frozen ? "hsl(35,90%,65%)" : "hsl(150,75%,60%)";''',
    ),
    (
        "frozen is reassignable",
        '''  const [frozen, setFrozen] = React.useState(store.frozen === true);''',
        '''  let [frozen, setFrozen] = React.useState(store.frozen === true);''',
    ),
    (
        "the button carries its ref",
        '''    "data-spectr-freeze-toggle": true,
    "data-spectr-freeze-state": frozen ? "frozen" : "live",''',
        '''    "data-spectr-freeze-toggle": true,
    ref: buttonRef,
    "data-spectr-freeze-state": frozen ? "frozen" : "live",''',
    ),
    (
        "the glyph carries its ref",
        '''    "data-spectr-freeze-glyph": frozen ? "square" : "dot",''',
        '''    "data-spectr-freeze-glyph": frozen ? "square" : "dot",
    ref: glyphRef,''',
    ),
    (
        "the label carries its ref",
        '''    "data-spectr-freeze-label": true,''',
        '''    "data-spectr-freeze-label": true,
    ref: labelRef,''',
    ),
    (
        "a LENGTH pick asks while Length is modulated",
        '''function spectrCommitFreezeLength(bars, fraction) {''',
        '''function spectrCommitFreezeLength(bars, fraction) {
  // The user's LENGTH stays the centre the Length target steps around, so a
  // pick always changes it; while an LFO drives Length it may ask first.
  const freezeStore = spectrFreezeStore();
  const lfos = freezeStore.lengthLfos || [];
  if (lfos.length && spectrOverrideAsks()) {
    spectrOverrideModulated("Length", 7, lfos,
      () => { spectrCommitFreezeLengthNow(bars, fraction); });
    return true;
  }
  return spectrCommitFreezeLengthNow(bars, fraction);
}
function spectrCommitFreezeLengthNow(bars, fraction) {''',
    ),
    (
        "the dialog is mounted at the root",
        '''  ), /* @__PURE__ */ React.createElement(TweaksPanel, { settings, setSettings }), ''',
        '''  ), /* @__PURE__ */ React.createElement(TweaksPanel, { settings, setSettings }), React.createElement(SpectrModulationOverrideDialog, { settings, setSettings }), ''',
    ),
    (
        "the dialog",
        '''function PatternSaveDialog({ open, defaultName, onSave, onCancel }) {''',
        '''// "Freeze is being modulated by LFO 1. Turn off its Freeze target?" Asked
// before a control an LFO drives takes the user's action, while the Setting
// "Ask before overriding" is on. The preset dialogs' frame and buttons.
function SpectrModulationOverrideDialog({ settings, setSettings }) {
  const store = spectrOverrideStore();
  const [request, setRequest] = React.useState(store.request);
  const [dontAsk, setDontAsk] = React.useState(false);
  // The Setting, mirrored where the asking code (outside React) reads it, and
  // a setter for the Settings > MODULATION row that shows it.
  globalThis.__spectrAskBeforeOverride = settings.askBeforeOverride !== false;
  window.spectrSetAskBeforeOverride = (on) => {
    globalThis.__spectrAskBeforeOverride = on === true;
    setSettings((s) => ({ ...s, askBeforeOverride: on === true }));
  };
  React.useEffect(() => {
    const sync = () => { setRequest(store.request); setDontAsk(false); };
    store.listeners.push(sync);
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  const finish = React.useCallback((turnOff) => {
    const r = store.request;
    store.request = null;
    setRequest(null);
    if (!r) return;
    if (dontAsk) {
      globalThis.__spectrAskBeforeOverride = false;
      setSettings((s) => ({ ...s, askBeforeOverride: false }));
    }
    if (turnOff && window.pulp && typeof window.pulp.postMessage === "function") {
      // That LFO's target lane, off, as one recordable host gesture.
      r.lfos.forEach((lfo) => {
        try {
          Promise.resolve(window.pulp.postMessage("param_edit",
            { id: 4020 + (lfo - 1) * 20 + r.target, value: 0 },
            "spectr-override-target")).catch((error) =>
              console.error("[Spectr] target write failed", error));
        } catch (error) {
          console.error("[Spectr] target write failed", error);
        }
      });
    }
    r.action();
  }, [dontAsk]);
  React.useEffect(() => {
    if (!request) return undefined;
    // Return turns the target off, Escape keeps modulating. Captured and
    // stopped, so the press does not also reach the control underneath.
    const onKey = (event) => {
      if (event.key !== "Escape" && event.key !== "Enter") return;
      event.preventDefault();
      event.stopPropagation();
      finish(event.key === "Enter");
    };
    document.addEventListener("keydown", onKey, true);
    return () => document.removeEventListener("keydown", onKey, true);
  }, [request, finish]);
  if (!request) return null;
  const names = request.lfos.map((lfo) => "LFO " + lfo);
  const who = names.length > 1 ? names.slice(0, -1).join(", ") + " and " + names[names.length - 1] : names[0];
  const prompt = request.control + " is being modulated by " + who + ". Turn off "
    + (names.length > 1 ? "their " : "its ") + request.control + " target"
    + (names.length > 1 ? "s" : "") + "?";
  return React.createElement("div", {
    "data-spectr-overlay": "true",
    "data-spectr-override-dialog": true,
    role: "dialog",
    "aria-modal": "true",
    "aria-label": "Modulated control",
    onClick: (event) => { event.stopPropagation(); finish(false); },
    style: { position: "absolute", inset: 0, zIndex: 2147483100, background: "rgba(5,7,10,.72)",
      display: "flex", alignItems: "center", justifyContent: "center" }
  }, React.createElement("div", {
    "data-spectr-override-panel": true, "data-spectr-overlay": "true", overlay: true,
    onDismiss: () => finish(false), onClick: (event) => event.stopPropagation(),
    style: { width: 380, padding: 18, background: "rgba(12,16,22,.99)",
      border: "1px solid rgba(140,190,240,.35)", borderRadius: 5,
      fontFamily: "var(--mono)", color: "#fff" }
  }, React.createElement("div", { style: { fontSize: 11, letterSpacing: 2, marginBottom: 12 } }, "MODULATED CONTROL"),
    React.createElement("div", { "data-spectr-override-prompt": true,
      style: { fontSize: 11, lineHeight: 1.5, opacity: 0.8 } }, prompt),
    React.createElement("button", {
      "data-spectr-override-dont-ask": dontAsk ? "on" : "off",
      role: "checkbox", "aria-checked": dontAsk,
      onClick: () => setDontAsk(!dontAsk),
      style: { display: "flex", alignItems: "center", gap: 8, marginTop: 12, padding: 0,
        background: "transparent", border: "none", color: "rgba(255,255,255,0.72)",
        fontFamily: "var(--mono)", fontSize: 10, letterSpacing: 0.5, cursor: "pointer" }
    }, React.createElement("span", { style: { width: 12, height: 12, borderRadius: 2, flexShrink: 0,
        border: "1px solid rgba(255,255,255,0.35)",
        background: dontAsk ? "hsl(200,70%,45%)" : "transparent" } }), "Don't ask again"),
    React.createElement("div", { style: { display: "flex", justifyContent: "flex-end", gap: 7, marginTop: 14 } },
      React.createElement(MBtn, { action: "override-keep", onClick: () => finish(false) }, "KEEP MODULATING"),
      React.createElement(MBtn, { action: "override-turn-off", primary: true, onClick: () => finish(true) }, "TURN OFF"))));
}
function PatternSaveDialog({ open, defaultName, onSave, onCancel }) {''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    applied = 0
    for name, old, new in EDITS:
        if encode(new) in raw:
            continue
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
        applied += 1
    if applied == 0:
        print("modulation freeze/override already applied")
        return 0
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("modulation freeze/override applied (%d edits)" % applied)
    return 0


if __name__ == "__main__":
    sys.exit(main())
