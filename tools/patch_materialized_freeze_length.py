#!/usr/bin/env python3
"""Freeze's musical LENGTH control in the header; Hold length leaves Settings.

The header reads  [LIVE / FROZEN] | LENGTH [ 1 bar v ] | OUTPUT --o-- 0.0 [PEAK]

- LENGTH is a dropdown after the freeze toggle, between two dividers: 1, 2, 4
  and 8 bars, a separator, and "Custom length...". A committed custom length
  shows as an extra checked row under "Custom length...", which stays there
  for the next edit. The collapsed control always shows the resolved length
  ("1 1/8 bars"), never "Custom...".
- "Custom length..." opens a small editor: Bars (a whole number, stepped by
  its arrows or Up/Down, or typed) + Fraction (a fixed list), a live preview
  ("= 1 1/8 bars"), CANCEL and APPLY. Tab moves Bars -> Fraction -> CANCEL ->
  APPLY, Up/Down step the focused field, Return applies a valid length (or
  cancels from CANCEL), Escape cancels. It consumes those keys only while it
  is open, and stops them, so Escape never also clears the band selection.
- The editor formats and validates nothing itself. The vocabulary (common
  lengths, fractions, the bar limit) arrives with hydration, every label and
  verdict comes from the processor (`freeze_length_describe`), and a commit
  is `freeze_length_set`, which validates again and answers with the length
  in force. The bar limit is shown only to explain a refused value.
- When the length at the host tempo is longer than the loop memory holds, the
  menu and the editor say so (and only then).
- The output trim's track gives up the room: 156pt -> 96pt, the shortest
  that still moves one 0.5 dB step per point (0.5 dB/pt).
- Settings > FREEZE (the Hold length slider) is removed.

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

MARKER = "function SpectrFreezeLength()"

# ── the freeze store: the Length replaces the hold length ──────────────────

STORE_INIT_OLD = '{ frozen: false, hold: null, listeners: [] }'
STORE_INIT_NEW = '{ frozen: false, length: null, listeners: [] }'

ACCEPT_START = '  // The hold length rides hydration only; update on presence.\n'
ACCEPT_END = '  if (moved) spectrFreezeNotify(store);\n}\n'
ACCEPT_BODY = '''  // The Length is host parameter 4, so it rides hydration and the live
  // projection alike; its vocabulary rides hydration only. Woken only when
  // something the control shows moves.
  const length = freeze.length;
  if (length && typeof length === "object" && typeof length.label === "string") {
    const was = store.length;
    const next = {
      preset: Number(length.preset),
      bars: Number(length.bars),
      fraction: String(length.fraction),
      label: length.label,
      customBars: Number(length.custom_bars),
      customFraction: String(length.custom_fraction),
      seconds: Number(length.seconds),
      capSeconds: Number(length.cap_seconds),
      capped: length.capped === true,
      tempo: Number(length.tempo_bpm)
    };
    if (!was || was.preset !== next.preset || was.label !== next.label
        || was.customBars !== next.customBars
        || was.customFraction !== next.customFraction
        || was.capped !== next.capped || was.capSeconds !== next.capSeconds
        || was.tempo !== next.tempo) {
      store.length = next;
      moved = true;
    }
  }
  if (Array.isArray(freeze.length_presets)) {
    store.lengthPresets = freeze.length_presets.map((p) => ({
      bars: Number(p.bars), fraction: String(p.fraction), label: String(p.label) }));
    moved = true;
  }
  if (Array.isArray(freeze.length_fractions)) {
    store.lengthFractions = freeze.length_fractions.map(String);
    moved = true;
  }
  if (typeof freeze.length_max_bars === "number")
    store.lengthMaxBars = freeze.length_max_bars;
'''

# ── the control ────────────────────────────────────────────────────────────

LENGTH_ANCHOR = "function SpectrOutputMeter({ latchOver = false } = {}) {\n"

LENGTH_BODY = r'''// Freeze Length (host parameter 4): the header's LENGTH control. The
// processor owns the length, its vocabulary, every label and every verdict;
// this keeps the store's copy and asks.
//
// It asks SYNCHRONOUSLY, through the processor's dispatcher itself (the
// call window.pulp.postMessage wraps in a promise): the editor's preview is
// computed while it renders, and a key that applies a length closes the
// editor in the same event. A promise's continuation waits for the job
// queue, which a key dispatched by the host does not always drain before
// the next frame -- the preview froze one edit behind, and Return applied
// without closing.
function spectrFreezeLengthCall(type, payload) {
  const dispatch = globalThis.__spectrEditorDispatch;
  if (typeof dispatch !== "function") return { ok: false, error: "no processor" };
  try {
    const response = JSON.parse(dispatch(JSON.stringify({
      type, payload: payload || {}, id: "spectr-freeze-length" })));
    return { ok: !!response && response.ok === true, payload: response,
             error: response && response.error };
  } catch (error) {
    return { ok: false, error: String(error) };
  }
}
// Commit a length. True once the processor took it; the store then carries
// the length in force.
function spectrCommitFreezeLength(bars, fraction) {
  const r = spectrFreezeLengthCall("freeze_length_set", { bars, fraction });
  if (r.ok && r.payload) spectrFreezeAccept({ freeze: r.payload });
  return r.ok;
}
window.spectrCommitFreezeLength = spectrCommitFreezeLength;
const SPECTR_LENGTH_FACE = @FACE@;
const SPECTR_LENGTH_ACCENT = "rgba(120,180,255,0.14)";
const SPECTR_LENGTH_ACCENT_EDGE = "rgba(180,210,255,0.4)";
function SpectrLengthCheck({ on }) {
  // A drawn check: the bound mono face covers Basic Latin only.
  return /* @__PURE__ */ React.createElement("svg", {
    width: 10, height: 10, viewBox: "0 0 10 10", "aria-hidden": true,
    style: { flex: "none", opacity: on ? 1 : 0 }
  }, /* @__PURE__ */ React.createElement("path", {
    d: "M1.5 5.2 L4 7.6 L8.6 2.4", stroke: "hsl(200,85%,75%)", strokeWidth: 1.5,
    fill: "none", strokeLinecap: "round", strokeLinejoin: "round"
  }));
}
function SpectrLengthRow({ label, selected, action, onPick, value }) {
  return /* @__PURE__ */ React.createElement("button", {
    role: "option",
    "aria-selected": selected ? "true" : "false",
    "data-spectr-length-option": value,
    "data-spectr-length-action": action || undefined,
    onClick: onPick,
    style: {
      background: selected ? SPECTR_LENGTH_ACCENT : "rgba(255,255,255,0.025)",
      border: "1px solid " + (selected ? SPECTR_LENGTH_ACCENT_EDGE : "transparent"),
      color: selected ? "#fff" : "rgba(255,255,255,0.85)",
      fontFamily: SPECTR_LENGTH_FACE, fontSize: 10.5, letterSpacing: 0.3,
      padding: "0 10px 0 8px", height: 26, minHeight: 26, width: "100%",
      boxSizing: "border-box", display: "flex", alignItems: "center",
      justifyContent: "flex-start", gap: 7,
      textAlign: "left", cursor: "pointer", borderRadius: 3, whiteSpace: "nowrap"
    }
  }, /* @__PURE__ */ React.createElement(SpectrLengthCheck, { on: selected }),
     /* @__PURE__ */ React.createElement("span", null, label));
}
// The custom editor: Bars + Fraction, a preview from the processor, CANCEL
// and APPLY. `initial` is the custom length to start from.
function SpectrLengthEditor({ store, initial, onClose }) {
  const fractions = Array.isArray(store.lengthFractions) && store.lengthFractions.length
    ? store.lengthFractions : ["0"];
  const [barsText, setBarsText] = React.useState(String(initial.bars));
  const [fraction, setFraction] = React.useState(
    fractions.indexOf(initial.fraction) >= 0 ? initial.fraction : fractions[0]);
  const [focus, setFocus] = React.useState(0); // 0 bars, 1 fraction, 2 cancel, 3 apply
  const [fractionOpen, setFractionOpen] = React.useState(false);
  const [refused, setRefused] = React.useState("");
  const draftKey = barsText + "|" + fraction;
  // The draft as the editor holds it: an empty or non-numeric Bars field is
  // an editing state, never a length, and never reaches the processor.
  const parsed = /^\d+$/.test(barsText) ? Math.min(parseInt(barsText, 10), 100000) : null;
  const local = barsText === "" ? "Enter a number of bars"
    : parsed === null ? "Bars must be a whole number" : "";
  // The processor's verdict and label for the draft, asked once per draft.
  const preview = React.useMemo(() => {
    if (parsed === null) return { valid: false, label: "", message: "" };
    const r = spectrFreezeLengthCall("freeze_length_describe", { bars: parsed, fraction });
    const body = r.payload || {};
    return { valid: r.ok && body.valid === true, label: String(body.label || ""),
             message: String(body.message || (r.ok ? "" : "Spectr could not check that length")),
             capped: body.capped === true, capSeconds: Number(body.cap_seconds),
             tempo: Number(body.tempo_bpm) };
  }, [draftKey]);
  React.useEffect(() => { setRefused(""); }, [draftKey]);
  const current = parsed !== null;
  const valid = current && preview.valid;
  const message = local || refused || (current && !preview.valid ? preview.message : "");
  const apply = () => {
    if (!valid) return;
    if (spectrCommitFreezeLength(parsed, fraction)) onClose(true);
    else setRefused("Spectr could not take that length");
  };
  const stepBars = (delta) => {
    const max = typeof store.lengthMaxBars === "number" ? store.lengthMaxBars : 128;
    const base = parsed === null ? 0 : parsed;
    setBarsText(String(Math.max(0, Math.min(max, base + delta))));
  };
  const stepFraction = (delta) => {
    const at = Math.max(0, fractions.indexOf(fraction));
    setFraction(fractions[Math.max(0, Math.min(fractions.length - 1, at + delta))]);
  };
  const moveFocus = (next) => setFocus((next + 4) % 4);
  // Bars is typed into directly while it has the focus: digits only (a
  // sign, a point or a letter is not part of a whole number of bars and is
  // ignored), Backspace takes the last digit, Delete empties it. It is not
  // a native text field, which would keep the arrow keys for its caret.
  const typeBars = (key) => {
    if (/^[0-9]$/.test(key)) {
      setBarsText((text) => (text.length >= 4 ? text : (text === "0" ? "" : text) + key));
      return true;
    }
    if (key === "Backspace") { setBarsText((text) => text.slice(0, -1)); return true; }
    if (key === "Delete") { setBarsText(""); return true; }
    return false;
  };
  // The editor's keys, captured and stopped while it is open: nothing else
  // in Spectr (Escape clears the band selection) or behind it sees them. An
  // open Fraction list keeps its own arrows, Return and Escape.
  const keysRef = React.useRef(null);
  keysRef.current = { apply, stepBars, stepFraction, moveFocus, typeBars, focus, fractionOpen, onClose };
  React.useEffect(() => {
    const onKey = (event) => {
      const k = keysRef.current;
      if (!k || k.fractionOpen) return;
      const key = event.key;
      const take = () => { event.preventDefault(); event.stopPropagation(); };
      if (key === "Escape") { take(); k.onClose(false); return; }
      if (key === "Enter") { take(); if (k.focus === 2) k.onClose(false); else k.apply(); return; }
      if (key === "Tab") { take(); k.moveFocus(k.focus + (event.shiftKey ? -1 : 1)); return; }
      if (key === "ArrowUp" || key === "ArrowDown") {
        const delta = key === "ArrowUp" ? 1 : -1;
        if (k.focus === 0) { take(); k.stepBars(delta); }
        else if (k.focus === 1) { take(); k.stepFraction(delta); }
        return;
      }
      if (event.metaKey || event.ctrlKey || event.altKey) return;
      if (k.focus === 0 && typeof key === "string" && key.length === 1) {
        // Every printable key is the field's while it has the focus, taken
        // or ignored -- none of them is a Spectr shortcut here.
        take();
        k.typeBars(key);
        return;
      }
      if (k.focus === 0 && (key === "Backspace" || key === "Delete")) { take(); k.typeBars(key); }
    };
    document.addEventListener("keydown", onKey, true);
    return () => document.removeEventListener("keydown", onKey, true);
  }, []);
  const ring = (index) => focus === index ? "1px solid rgba(140,190,240,0.75)" : "1px solid rgba(255,255,255,0.14)";
  const caption = { fontFamily: SPECTR_LENGTH_FACE, fontSize: 8.5, letterSpacing: 2, color: "rgba(255,255,255,0.5)" };
  const field = { height: 28, boxSizing: "border-box", borderRadius: 3, background: "rgba(255,255,255,0.04)",
                  color: "#fff", fontFamily: SPECTR_LENGTH_FACE, fontSize: 11, letterSpacing: 0.3 };
  const stepper = (delta, d) => /* @__PURE__ */ React.createElement("button", {
    "data-spectr-length-bars-step": delta > 0 ? "up" : "down",
    "aria-label": delta > 0 ? "More bars" : "Fewer bars",
    onClick: () => { setFocus(0); stepBars(delta); },
    style: { width: 18, height: 13, padding: 0, border: "none", background: "transparent",
             display: "flex", alignItems: "center", justifyContent: "center", cursor: "pointer" }
  }, /* @__PURE__ */ React.createElement("svg", { width: 9, height: 6, viewBox: "0 0 9 6", "aria-hidden": true },
    /* @__PURE__ */ React.createElement("path", { d, stroke: "rgba(255,255,255,0.75)", strokeWidth: 1.3,
      fill: "none", strokeLinecap: "round", strokeLinejoin: "round" })));
  return /* @__PURE__ */ React.createElement("div", {
    "data-spectr-length-editor": true,
    "data-spectr-overlay": "true",
    overlay: true,
    onDismiss: () => onClose(false),
    role: "dialog",
    "aria-label": "Custom length",
    onClick: (event) => event.stopPropagation(),
    style: {
      position: "absolute", top: 32, left: -38, width: 236, zIndex: 30,
      boxSizing: "border-box", padding: "12px 12px 12px",
      background: "rgba(12,16,22,0.97)", border: "1px solid rgba(255,255,255,0.12)",
      borderRadius: 5, boxShadow: "0 10px 34px rgba(0,0,0,0.55)",
      display: "flex", flexDirection: "column", gap: 8, color: "#fff"
    }
  },
    /* @__PURE__ */ React.createElement("div", { style: { display: "flex", alignItems: "center", height: 12 } },
      /* @__PURE__ */ React.createElement("span", { style: { ...caption, width: 104 } }, "BARS"),
      /* @__PURE__ */ React.createElement("span", { style: caption }, "FRACTION")),
    /* @__PURE__ */ React.createElement("div", { style: { display: "flex", alignItems: "center", height: 28 } },
      /* @__PURE__ */ React.createElement("div", { style: { ...field, width: 84, border: ring(0), display: "flex", alignItems: "center", paddingLeft: 9 } },
        /* @__PURE__ */ React.createElement("div", {
          "data-spectr-length-bars": barsText,
          role: "spinbutton",
          "aria-label": "Bars",
          "aria-valuenow": parsed === null ? undefined : parsed,
          "aria-valuemin": 0,
          onClick: () => setFocus(0),
          style: { flex: 1, minWidth: 0, height: 22, display: "flex", alignItems: "center", gap: 1,
                   cursor: "text", fontFamily: SPECTR_LENGTH_FACE, fontSize: 11, color: "#fff" }
        }, /* @__PURE__ */ React.createElement("span", null, barsText),
           focus === 0 && /* @__PURE__ */ React.createElement("span", {
             "aria-hidden": true, "data-spectr-length-caret": true,
             style: { width: 1, height: 13, background: "rgba(160,200,245,0.9)" } })),
        /* @__PURE__ */ React.createElement("div", { style: { display: "flex", flexDirection: "column", width: 18 } },
          stepper(1, "M1 5 L4.5 1.5 L8 5"), stepper(-1, "M1 1 L4.5 4.5 L8 1"))),
      /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { width: 20, textAlign: "center", fontFamily: SPECTR_LENGTH_FACE, fontSize: 12, color: "rgba(255,255,255,0.7)" } }, "+"),
      /* @__PURE__ */ React.createElement("div", { "data-spectr-length-fraction-root": true, style: { position: "relative" } },
        /* @__PURE__ */ React.createElement("button", {
          "data-spectr-length-fraction": fraction,
          "aria-haspopup": "listbox",
          "aria-expanded": fractionOpen,
          "aria-label": "Fraction of a bar, " + fraction,
          onClick: () => { setFocus(1); setFractionOpen((v) => !v); },
          style: { ...field, width: 108, border: ring(1), padding: "0 8px 0 10px", display: "flex",
                   alignItems: "center", justifyContent: "space-between", cursor: "pointer" }
        }, /* @__PURE__ */ React.createElement("span", null, fraction),
           /* @__PURE__ */ React.createElement("span", { style: { fontSize: 9, opacity: 0.7 } }, "▾")),
        fractionOpen && /* @__PURE__ */ React.createElement("div", {
          "data-spectr-length-fraction-options": true,
          "data-spectr-overlay": "true",
          overlay: true,
          onDismiss: () => setFractionOpen(false),
          role: "listbox",
          style: { position: "absolute", top: 31, left: 0, width: 108, zIndex: 31, padding: 3,
                   boxSizing: "border-box", display: "flex", flexDirection: "column", gap: 1,
                   background: "rgba(12,16,22,0.98)", border: "1px solid rgba(255,255,255,0.12)",
                   borderRadius: 4, boxShadow: "0 8px 30px rgba(0,0,0,0.5)" }
        }, fractions.map((f) => /* @__PURE__ */ React.createElement("button", {
          key: f,
          role: "option",
          "aria-selected": f === fraction ? "true" : "false",
          "data-spectr-length-fraction-option": f,
          onClick: () => { setFraction(f); setFractionOpen(false); setFocus(1); },
          style: { height: 20, minHeight: 20, width: "100%", boxSizing: "border-box", padding: "0 8px",
                   display: "flex", alignItems: "center", justifyContent: "flex-start", gap: 6,
                   borderRadius: 2, cursor: "pointer",
                   background: f === fraction ? SPECTR_LENGTH_ACCENT : "transparent",
                   border: "1px solid " + (f === fraction ? SPECTR_LENGTH_ACCENT_EDGE : "transparent"),
                   color: "rgba(255,255,255,0.88)", fontFamily: SPECTR_LENGTH_FACE, fontSize: 10.5 }
        }, /* @__PURE__ */ React.createElement(SpectrLengthCheck, { on: f === fraction }),
           /* @__PURE__ */ React.createElement("span", null, f)))))),
    /* @__PURE__ */ React.createElement("div", {
      "data-spectr-length-preview": true,
      "data-spectr-length-valid": valid ? "true" : "false",
      "data-spectr-length-message": message,
      role: "status",
      style: { height: 30, boxSizing: "border-box", borderRadius: 3, background: "rgba(255,255,255,0.045)",
               display: "flex", alignItems: "center", justifyContent: "center",
               fontFamily: SPECTR_LENGTH_FACE, fontSize: 11, letterSpacing: 0.3,
               color: message ? "hsl(35,90%,72%)" : "rgba(255,255,255,0.9)" }
    }, message ? message : current ? "= " + preview.label : " "),
    valid && preview.capped && /* @__PURE__ */ React.createElement("div", {
      "data-spectr-length-cap-note": true,
      style: { fontFamily: "var(--sans)", fontSize: 9.5, lineHeight: 1.45, color: "rgba(255,255,255,0.6)" }
    }, "Longer than Spectr can hold at " + Math.round(preview.tempo) + " BPM. A freeze loops the last "
       + Math.round(preview.capSeconds) + " s."),
    /* @__PURE__ */ React.createElement("div", { style: { display: "flex", gap: 8, marginTop: 2 } },
      /* @__PURE__ */ React.createElement("button", {
        "data-spectr-length-cancel": true,
        onClick: () => onClose(false),
        style: { flex: 1, height: 28, borderRadius: 3, cursor: "pointer", fontFamily: SPECTR_LENGTH_FACE,
                 fontSize: 10, letterSpacing: 1, color: "rgba(255,255,255,0.85)",
                 background: "rgba(255,255,255,0.04)",
                 border: focus === 2 ? "1px solid rgba(140,190,240,0.75)" : "1px solid rgba(255,255,255,0.12)" }
      }, "CANCEL"),
      /* @__PURE__ */ React.createElement("button", {
        "data-spectr-length-apply": true,
        "aria-disabled": valid ? "false" : "true",
        onClick: apply,
        style: { flex: 1, height: 28, borderRadius: 3, cursor: valid ? "pointer" : "default",
                 fontFamily: SPECTR_LENGTH_FACE, fontSize: 10, letterSpacing: 1,
                 color: valid ? "#fff" : "rgba(255,255,255,0.4)",
                 background: valid ? "rgba(80,140,210,0.32)" : "rgba(80,140,210,0.1)",
                 border: focus === 3 ? "1px solid rgba(190,220,255,0.95)"
                   : "1px solid " + (valid ? "rgba(140,190,240,0.55)" : "rgba(140,190,240,0.2)") }
      }, "APPLY")));
}
function SpectrFreezeLength() {
  const store = spectrFreezeStore();
  const [, setRevision] = React.useState(0);
  const [menuOpen, setMenuOpen] = React.useState(false);
  const [editorOpen, setEditorOpen] = React.useState(false);
  React.useEffect(() => {
    const sync = () => setRevision((n) => n + 1);
    store.listeners.push(sync);
    return () => {
      const at = store.listeners.indexOf(sync);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  }, []);
  const length = store.length;
  const presets = Array.isArray(store.lengthPresets) ? store.lengthPresets : [];
  const custom = !!length && length.preset >= presets.length;
  const label = editorOpen ? "Custom…" : length ? length.label : "";
  // A fixed box, so a long custom length never moves OUTPUT and PEAK: a
  // label past what 10.5pt fits in its 60pt ("1 1/8 bars", "128 15/16
  // bars") steps the size down to fit instead (a mono glyph is 0.6 em).
  const glyphs = Math.max(1, label.length);
  const fontSize = Math.min(10.5, Math.floor(600 / (0.6 * glyphs)) / 10);
  const pick = (preset) => {
    setMenuOpen(false);
    spectrCommitFreezeLength(preset.bars, preset.fraction);
  };
  const openEditor = () => { setMenuOpen(false); setEditorOpen(true); };
  return /* @__PURE__ */ React.createElement("span", {
    "data-spectr-freeze-length": true,
    "data-spectr-freeze-length-label": length ? length.label : "",
    "data-spectr-freeze-length-preset": length ? String(length.preset) : "",
    style: { position: "relative", display: "inline-flex", alignItems: "center", flexShrink: 0 }
  }, /* @__PURE__ */ React.createElement("div", { "data-spectr-menu-root": "length", style: { position: "relative" } },
    /* @__PURE__ */ React.createElement("button", {
      "data-spectr-menu-trigger": true,
      "data-spectr-length-trigger": true,
      "aria-haspopup": "listbox",
      "aria-expanded": menuOpen,
      "aria-label": "Freeze length, " + (length ? length.label : ""),
      title: "How much of the incoming sound a freeze takes in"
        + (length && length.capped ? ". Longer than Spectr can hold at this tempo: a freeze loops the last "
           + Math.round(length.capSeconds) + " s." : ""),
      onClick: () => {
        if (editorOpen) { setEditorOpen(false); return; }
        setMenuOpen((v) => !v);
      },
      style: {
        width: 88, minWidth: 88, flexShrink: 0, height: 24, boxSizing: "border-box",
        padding: "0 7px 0 8px", borderRadius: 3, display: "flex", alignItems: "center",
        justifyContent: "space-between", gap: 4, cursor: "pointer",
        background: menuOpen || editorOpen ? "rgba(255,255,255,0.08)" : "rgba(255,255,255,0.03)",
        border: "1px solid " + (menuOpen || editorOpen ? "rgba(255,255,255,0.18)" : "rgba(255,255,255,0.1)"),
        color: "rgba(255,255,255,0.85)", fontFamily: SPECTR_LENGTH_FACE, lineHeight: 1
      }
    }, /* @__PURE__ */ React.createElement("span", {
      "data-spectr-length-value": true,
      style: { fontSize, letterSpacing: 0, whiteSpace: "nowrap", flexShrink: 0,
               color: length && length.capped ? "hsl(35,90%,75%)" : "rgba(255,255,255,0.88)" }
    }, label),
       /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { fontSize: 9, opacity: 0.7, flexShrink: 0 } }, "▾")),
    menuOpen && /* @__PURE__ */ React.createElement("div", {
      "data-spectr-menu-options": true,
      "data-spectr-overlay": "true",
      overlay: true,
      onDismiss: () => setMenuOpen(false),
      role: "listbox",
      "aria-label": "Freeze length",
      style: {
        position: "absolute", top: 29, left: 0, width: 150, zIndex: 20, padding: 4,
        boxSizing: "border-box", display: "flex", flexDirection: "column", gap: 2,
        background: "rgba(12,16,22,0.96)", border: "1px solid rgba(255,255,255,0.12)",
        borderRadius: 4, boxShadow: "0 8px 30px rgba(0,0,0,0.5)"
      }
    }, presets.map((preset, index) => /* @__PURE__ */ React.createElement(SpectrLengthRow, {
      key: preset.label, label: preset.label, value: preset.label,
      selected: !!length && length.preset === index, onPick: () => pick(preset)
    })), /* @__PURE__ */ React.createElement("div", {
      "aria-hidden": true, "data-spectr-length-separator": true,
      style: { height: 1, margin: "3px 4px", background: "rgba(255,255,255,0.1)" }
    }), /* @__PURE__ */ React.createElement(SpectrLengthRow, {
      label: "Custom length…", value: "custom-editor", action: "custom", selected: false,
      onPick: openEditor
    }), custom && /* @__PURE__ */ React.createElement(SpectrLengthRow, {
      label: length.label, value: "custom", selected: true,
      onPick: () => setMenuOpen(false)
    }), length && length.capped && /* @__PURE__ */ React.createElement("div", {
      "data-spectr-length-cap-note": true,
      style: { padding: "5px 8px 3px", fontFamily: "var(--sans)", fontSize: 9.5, lineHeight: 1.45,
               color: "rgba(255,255,255,0.6)", whiteSpace: "normal" }
    }, "At " + Math.round(length.tempo) + " BPM this is longer than Spectr can hold. A freeze loops the last "
       + Math.round(length.capSeconds) + " s."))),
    editorOpen && /* @__PURE__ */ React.createElement(SpectrLengthEditor, {
      store,
      initial: custom ? { bars: length.bars, fraction: length.fraction }
        : length ? { bars: length.customBars, fraction: length.customFraction }
        : { bars: 1, fraction: "0" },
      onClose: () => setEditorOpen(false)
    }));
}
'''

# ── the header: | LENGTH [..] | after the toggle; the trim's track narrows ──

CLUSTER_OLD = '''  } }, /* @__PURE__ */ React.createElement(SpectrFreezeToggle, null),
'''
DIVIDER = ('/* @__PURE__ */ React.createElement("span", { "aria-hidden": true, '
           '"data-spectr-header-divider": "%s", style: { width: 1, height: 20, '
           'flexShrink: 0, margin: "0 -9px", background: "rgba(255,255,255,0.08)" } }),\n')
CLUSTER_NEW = ('''  } }, /* @__PURE__ */ React.createElement(SpectrFreezeToggle, null),
        // | LENGTH [ 1 bar v ] |. The dividers and the caption are not
        // controls, so they sit 5pt from their neighbours (the cluster's
        // 14 less 9) and the caption 6pt from its dropdown: the gap that
        // matters for hit slop is the one between two controls, and that
        // stays wide (54pt toggle -> dropdown, 57pt dropdown -> trim).
        ''' + DIVIDER % "freeze" + '''        /* @__PURE__ */ React.createElement("span", {
      "data-spectr-freeze-length-caption": true,
      style: { fontFamily: '@FACE_RAW@', fontSize: 10, letterSpacing: 0.8, color: "rgba(255,255,255,0.72)", whiteSpace: "nowrap", flexShrink: 0, marginRight: -8 }
    }, "LENGTH"),
        /* @__PURE__ */ React.createElement(SpectrFreezeLength, null),
        ''' + DIVIDER % "length")

TRACK_OLD = 'style: { width: 156, flexShrink: 0, accentColor: "hsl(200,80%,60%)" }'
TRACK_NEW = 'style: { width: 96, flexShrink: 0, accentColor: "hsl(200,80%,60%)" }'

COMMENT_OLD = '''  // the authored 1320x860 box). This sits at 282 and runs 76 freeze + 14 +
  // ~41 OUTPUT + 14 + 156 track + 14 + 31 readout + 14 + 96 peak, ending at
  // ~738 -- ~101pt clear of the divider, and out of the flow that cannot
  // absorb it. PEAK keeps the same 14pt gap to the value that it used to
  // keep to OUTPUT.'''
COMMENT_NEW = '''  // the authored 1320x860 box). This sits at 282 and runs 76 freeze + 5 +
  // divider + 5 + ~41 LENGTH + 6 + 88 length + 5 + divider + 5 + ~41 OUTPUT
  // + 14 + 96 track + 14 + 31 readout + 14 + 96 peak, ending at ~823 --
  // ~16pt clear of the divider, and out of the flow that cannot absorb it.
  // The trim's track gave LENGTH its room: 156pt became 96, the shortest
  // that still moves one 0.5 dB step per point of travel. PEAK keeps the
  // same 14pt gap to the value that it used to keep to OUTPUT.'''

# ── Settings: the FREEZE group goes ─────────────────────────────────────────

SETTINGS_OLD = ('React.createElement(SpectrLatencySettings, null), '
                '/* @__PURE__ */ React.createElement(SpectrFreezeSettings, null), ')
SETTINGS_NEW = 'React.createElement(SpectrLatencySettings, null), '
SETTINGS_FN_START = ('// Settings > FREEZE. Not a host parameter, so like Latency it reads the\n'
                     '// hydration payload and renders nothing until the processor has answered.\n'
                     'function SpectrFreezeSettings() {\n')
SETTINGS_FN_END = ('// The close button owns its hover and press look, so pointer-enter and\n')


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
    face_raw = '"%s", "JetBrains Mono", ui-monospace, monospace' % bound_mono_face(document)
    face = "'" + face_raw + "'"
    body = LENGTH_BODY.replace("@FACE@", face)
    cluster_new = CLUSTER_NEW.replace("@FACE_RAW@", face_raw)

    if MARKER in html:
        for marker, label in ((body, "length control"), (ACCEPT_BODY, "store accept"),
                              (cluster_new, "header cluster"), (TRACK_NEW, "trim track"),
                              (COMMENT_NEW, "cluster comment")):
            if html.count(marker) != 1:
                sys.exit("FAIL: the Length control is present but its %s is not; "
                         "the document is half patched" % label)
        if "function SpectrFreezeSettings()" in html or "freeze_hold_set" in html:
            sys.exit("FAIL: the Length control is present but Settings still "
                     "carries the Hold length")
        print("already applied  freeze length control, Settings hold length removed")
        return 0

    for text, label in ((STORE_INIT_OLD, "freeze store init"),
                        (ACCEPT_START, "hold length accept"),
                        (LENGTH_ANCHOR, "output meter"),
                        (CLUSTER_OLD, "output cluster head"),
                        (TRACK_OLD, "trim track"),
                        (COMMENT_OLD, "cluster comment"),
                        (SETTINGS_OLD, "Settings freeze group"),
                        (SETTINGS_FN_START, "Settings freeze function")):
        once(html, text, label)

    html = html.replace(STORE_INIT_OLD, STORE_INIT_NEW, 1)
    start = html.index(ACCEPT_START)
    end = html.index(ACCEPT_END, start)
    html = html[:start] + ACCEPT_BODY + html[end:]

    html = html.replace(LENGTH_ANCHOR, body + LENGTH_ANCHOR, 1)
    html = html.replace(CLUSTER_OLD, cluster_new, 1)
    html = html.replace(TRACK_OLD, TRACK_NEW, 1)
    html = html.replace(COMMENT_OLD, COMMENT_NEW, 1)

    html = html.replace(SETTINGS_OLD, SETTINGS_NEW, 1)
    start = html.index(SETTINGS_FN_START)
    end = html.index(SETTINGS_FN_END, start)
    html = html[:start] + html[end:]
    if "freeze_hold_set" in html:
        sys.exit("FAIL: a freeze_hold_set write survived the removal")

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
    print("applied          freeze length control, Settings hold length removed")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
