#!/usr/bin/env python3
"""LENGTH and the preset label follow their LFO without a React commit.

WHAT WAS WRONG. While an LFO drove Length or Preset, each step of the shown
value (the processor's freeze_display) notified every freeze-store listener,
and one of them is the header's Chrome component: a step re-rendered the
whole toolbar, a React commit that re-applies the captured document (~24 ms
p95 a step for Bands before the same fix landed there). A Length or Preset
LFO steps several times a second.

WHAT THIS DOES. Like the BANDS label (patch_materialized_modulation_controls_
follow.py), a step is painted straight onto the nodes that show it:

  LENGTH  the value span's text, font size (the fixed box steps the size
          down for a long label), colour and modulated flag, and the
          trigger's violet border. Skipped while its menu or Custom editor is
          open: those own the trigger and re-render it themselves.
  preset  the label's text, colour and modulated / shown attributes.

A render reads the same store, so a later render agrees with what was
painted. Until a painter has mounted (an editor still opening) the step
falls back to the notify it always had.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "globalThis.__spectrLengthLabelPaint = paintShown;"

EDITS = [
    (
        "freeze display paints LENGTH and preset steps",
        '''      if (typeof store.paint === "function") store.paint();
      if (was.lengthIndex !== shown.lengthIndex
          || was.presetDriven !== shown.presetDriven || was.presetName !== shown.presetName)
        spectrFreezeNotify(store);''',
        '''      if (typeof store.paint === "function") store.paint();
      // LENGTH and the preset label are painted where they show a step
      // (tools/patch_materialized_modulated_labels_paint.py); only a step
      // no mounted painter can show falls back to notifying the store.
      const lengthMoved = was.lengthIndex !== shown.lengthIndex;
      const presetMoved = was.presetDriven !== shown.presetDriven
        || was.presetName !== shown.presetName;
      const lengthPaint = globalThis.__spectrLengthLabelPaint;
      const presetPaint = globalThis.__spectrPresetLabelPaint;
      if (lengthMoved && typeof lengthPaint === "function") lengthPaint();
      if (presetMoved && typeof presetPaint === "function") presetPaint();
      if ((lengthMoved && typeof lengthPaint !== "function")
          || (presetMoved && typeof presetPaint !== "function"))
        spectrFreezeNotify(store);''',
    ),
    (
        "LENGTH value span ref",
        '''    }, /* @__PURE__ */ React.createElement("span", {
      "data-spectr-length-value": true,''',
        '''    }, /* @__PURE__ */ React.createElement("span", {
      ref: lengthValueRef,
      "data-spectr-length-value": true,''',
    ),
    (
        "LENGTH painter",
        '''function SpectrFreezeLength() {
  const store = spectrFreezeStore();
  const [, setRevision] = React.useState(0);
  const [menuOpen, setMenuOpen] = React.useState(false);
  const [editorOpen, setEditorOpen] = React.useState(false);''',
        '''function SpectrFreezeLength() {
  const store = spectrFreezeStore();
  const [, setRevision] = React.useState(0);
  const [menuOpen, setMenuOpen] = React.useState(false);
  const [editorOpen, setEditorOpen] = React.useState(false);
  // The length the Length target plays, painted onto the closed control when
  // it steps (freeze_display); the render below computes the same thing.
  const lengthValueRef = React.useRef(null);
  const lengthOpenRef = React.useRef(false);
  lengthOpenRef.current = menuOpen || editorOpen;
  React.useEffect(() => {
    const paintShown = () => {
      if (lengthOpenRef.current) return;
      const el = lengthValueRef.current;
      if (!el) return;
      const presetsNow = Array.isArray(store.lengthPresets) ? store.lengthPresets : [];
      const lengthNow = store.length;
      const d = store.display || {};
      const shownNow = d.lengthIndex >= 0 && presetsNow[d.lengthIndex] ? presetsNow[d.lengthIndex] : null;
      const text = shownNow ? shownNow.label : lengthNow ? lengthNow.label : "";
      const cappedNow = !!lengthNow && lengthNow.capped;
      el.textContent = text;
      const size = Math.min(10.5, Math.floor(600 / (0.6 * Math.max(1, text.length))) / 10);
      if (el.__spectrFontSize !== size) {
        el.__spectrFontSize = size;
        el.style.fontSize = size + "px";
      }
      el.style.color = cappedNow ? "hsl(35,90%,75%)" : shownNow ? "rgb(205,180,255)" : "rgba(255,255,255,0.88)";
      el.setAttribute("data-spectr-length-modulated", shownNow ? "1" : "");
      const trigger = el.parentElement || el.parentNode;
      if (trigger && trigger.style)
        trigger.style.border = "1px solid " + (shownNow ? "rgba(190,150,255,0.6)" : "rgba(255,255,255,0.08)");
    };
    globalThis.__spectrLengthLabelPaint = paintShown;
    return () => {
      if (globalThis.__spectrLengthLabelPaint === paintShown) globalThis.__spectrLengthLabelPaint = null;
    };
  }, []);''',
    ),
    (
        "LENGTH value span keeps a fixed width",
        '''      style: { fontSize, letterSpacing: 0, whiteSpace: "nowrap", flexShrink: 0,''',
        '''      // Pinned to the 60pt the label is sized into, so a step that keeps
      // the font size changes its text without re-laying the header.
      style: { fontSize, width: 60, letterSpacing: 0, whiteSpace: "nowrap", flexShrink: 0,''',
    ),
    (
        "preset label ref",
        '''React.createElement("span", { "data-spectr-selected-preset": true, "data-spectr-preset-modulated":''',
        '''React.createElement("span", { ref: presetLabelRef, "data-spectr-selected-preset": true, "data-spectr-preset-modulated":''',
    ),
    (
        "preset painter",
        '''  const spectrPresetShown = useSpectrModulatedDisplay();
  const spectrPresetDriven = spectrPresetShown.presetDriven === true && !!spectrPresetShown.presetName;''',
        '''  const spectrPresetShown = useSpectrModulatedDisplay();
  const spectrPresetDriven = spectrPresetShown.presetDriven === true && !!spectrPresetShown.presetName;
  // The preset the Preset target is nearest, painted onto the label when it
  // steps (freeze_display) instead of re-rendering this whole toolbar.
  const presetLabelRef = useRefChrome(null);
  const presetNameRef = useRefChrome(selectedPatternName);
  presetNameRef.current = selectedPatternName;
  useEffectChrome(() => {
    const paint = () => {
      const el = presetLabelRef.current;
      if (!el) return;
      const d = spectrFreezeStore().display || {};
      const driven = d.presetDriven === true && !!d.presetName;
      el.textContent = spectrPresetLabelText(presetNameRef.current, d);
      el.setAttribute("data-spectr-preset-modulated", driven ? "1" : "");
      el.setAttribute("data-spectr-preset-shown", driven ? d.presetName : presetNameRef.current);
      el.style.color = driven ? "rgb(205,180,255)" : "rgba(255,255,255,0.85)";
    };
    globalThis.__spectrPresetLabelPaint = paint;
    return () => {
      if (globalThis.__spectrPresetLabelPaint === paint) globalThis.__spectrPresetLabelPaint = null;
    };
  }, []);''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("modulated labels paint already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("modulated labels paint applied (%d edits)" % len(EDITS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
