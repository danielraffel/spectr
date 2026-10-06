#!/usr/bin/env python3
"""Hover tooltips for the header controls.

The materialized runtime does not draw `title` attributes (they are read by
accessibility only), and Pulp's `TooltipWindow` is state without a renderer in
this document, so a person hovering MIX, INTENSITY, OUTPUT, AUTO, LIVE,
LENGTH or PEAK learnt nothing. This adds one small Spectr-styled tooltip owned
by the header's output cluster:

  * appears 500 ms after the pointer settles on a control;
  * hides on leave and on press (a press is never delayed or intercepted);
  * pointer-events none, never focusable, so it cannot take a click or focus;
  * dark panel, mono text: the control's full name and one line.

`globalThis.spectrHeaderTip(text, node)` schedules it and
`globalThis.spectrHeaderTipHide()` cancels it; the knobs (SpectrKnob's `tip`
prop) and the AUTO pill call them, and the LIVE toggle, LENGTH dropdown and
PEAK chip gain enter/leave handlers here.

Follow-up for Pulp: a renderer for `TooltipWindow` (or `title`) in the
materialized runtime would replace this per-product component.

Idempotent like the other patch_materialized_* scripts. Run after
tools/patch_materialized_level_controls.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def escaped(value):
    # ensure_ascii=False: the artifact stores non-ASCII literally (see
    # tools/git/merge_materialized_runtime.py), so must every edit.
    return json.dumps(value, ensure_ascii=False)[1:-1]


TIPS = {
    "mix": "Mix: blend Spectr's sound with the original. Great with Freeze.",
    "intensity": "Intensity: how strong the effect is. 0% is flat.",
    "output": "Output: final volume (dB).",
    "auto": "Auto Gain: keeps the level steady as you boost or cut.",
    "length": "Length: how much audio a freeze captures and loops, in bars.",
    "freeze": "Freeze: hold the sound. Shortcut Q (standalone) / ⌃⌥⌘F (DAW).",
    "peak": "Peak: the level leaving Spectr. Click to clear.",
}


SELECTORS = {
    "auto": "[data-spectr-auto-gain]",
    "freeze": "[data-spectr-freeze-toggle]",
    "length": "[data-spectr-freeze-length]",
    "peak": "[data-spectr-output-peak]",
}


def hover_props(key):
    text = json.dumps(TIPS[key], ensure_ascii=False)
    # Anchored by selector: a synthesised or semantic pointer event may carry
    # no currentTarget, and the tip must still know where to sit.
    return ('onPointerEnter: () => globalThis.spectrHeaderTip && globalThis.spectrHeaderTip('
            + text + ', ' + json.dumps(SELECTORS[key]) + '), '
            'onPointerLeave: () => globalThis.spectrHeaderTipHide && globalThis.spectrHeaderTipHide(), ')


STATE_ANCHOR = '  const localLevelAtRef = React.useRef({});\n'
STATE_NEW = STATE_ANCHOR + r'''  // Header tooltips (tools/patch_materialized_header_tooltips.py): one at a
  // time, shown 500 ms after the pointer settles, gone on leave or press.
  const [tip, setTip] = React.useState(null);
  const tipTimerRef = React.useRef(0);
  React.useEffect(() => {
    const TIP_DELAY_MS = 500;
    const cancel = () => {
      if (tipTimerRef.current && typeof clearTimeout === "function")
        clearTimeout(tipTimerRef.current);
      tipTimerRef.current = 0;
    };
    globalThis.spectrHeaderTipHide = () => { cancel(); setTip(null); };
    globalThis.spectrHeaderTip = (text, node) => {
      cancel();
      setTip(null);
      const cluster = document.querySelector("[data-spectr-output-cluster]");
      const anchor = typeof node === "string" ? document.querySelector(node) : node;
      const box = anchor && anchor.getBoundingClientRect ? anchor.getBoundingClientRect() : null;
      const origin = cluster && cluster.getBoundingClientRect ? cluster.getBoundingClientRect() : null;
      // A probe's view of the last request, since the tip itself only exists
      // after the delay.
      globalThis.__spectrHeaderTipRequest = { text, anchored: !!box, cluster: !!origin, shown: false };
      if (!box || !origin || typeof setTimeout !== "function") return;
      const x = Math.max(0, box.left - origin.left);
      tipTimerRef.current = setTimeout(() => {
        tipTimerRef.current = 0;
        if (globalThis.__spectrHeaderTipRequest) globalThis.__spectrHeaderTipRequest.shown = true;
        setTip({ text, x });
      }, TIP_DELAY_MS);
    };
    return () => {
      cancel();
      globalThis.spectrHeaderTip = void 0;
      globalThis.spectrHeaderTipHide = void 0;
    };
  }, []);
'''

TIP_ELEMENT = r'''    tip && /* @__PURE__ */ React.createElement("div", {
      "data-spectr-header-tooltip": true,
      // The text as an attribute too: the runtime's element shim reads
      // textContent as empty, so a probe could not tell a tip from none.
      "data-spectr-header-tooltip-text": tip.text,
      "aria-hidden": true,
      style: {
        position: "absolute", left: tip.x, top: 32, zIndex: 60,
        pointerEvents: "none", whiteSpace: "nowrap",
        background: "rgba(12,16,22,0.96)", border: "1px solid rgba(255,255,255,0.12)",
        borderRadius: 4, padding: "6px 9px", boxShadow: "0 8px 24px rgba(0,0,0,0.45)",
        fontFamily: SPECTR_HEADER_MONO, fontSize: 10, letterSpacing: 0.3,
        color: "rgba(235,240,248,0.92)"
      }
    }, /* @__PURE__ */ React.createElement("span", { style: { pointerEvents: "none" } }, tip.text)),
'''

EDITS = [
    ("tooltip state and scheduler", STATE_ANCHOR, STATE_NEW),
    ("the knob takes a tooltip",
     '                      readoutWidth, onEdit, after }) {',
     '                      readoutWidth, onEdit, after, tip }) {'),
    ("a knob press hides it",
     '  const onKnobPointerDown = (e) => {\n',
     '  const onKnobPointerDown = (e) => {\n'
     '    if (globalThis.spectrHeaderTipHide) globalThis.spectrHeaderTipHide();\n'),
    ("the knob shows it on hover",
     '      onWheel: onKnobWheel,\n',
     '      onWheel: onKnobWheel,\n'
     '      onPointerEnter: () => tip && globalThis.spectrHeaderTip && globalThis.spectrHeaderTip(tip, "[data-spectr-" + name + "]"),\n'
     '      onPointerLeave: () => globalThis.spectrHeaderTipHide && globalThis.spectrHeaderTipHide(),\n'),
    ("MIX tip", 'format: percentText, readoutWidth: 26,\n      ariaLabel: "Mix,',
     'format: percentText, readoutWidth: 26, tip: ' + json.dumps(TIPS["mix"]) + ',\n      ariaLabel: "Mix,'),
    ("INTENSITY tip", 'format: percentText, readoutWidth: 26,\n      ariaLabel: "Intensity,',
     'format: percentText, readoutWidth: 26, tip: ' + json.dumps(TIPS["intensity"]) + ',\n      ariaLabel: "Intensity,'),
    ("OUTPUT tip", 'readoutWidth: 31,\n      ariaLabel: "Output trim, decibels",',
     'readoutWidth: 31, tip: ' + json.dumps(TIPS["output"]) + ',\n      ariaLabel: "Output trim, decibels",'),
    ("AUTO tip",
     '    onClick: onToggle,\n    style: {\n      background: on ? "rgba(120,180,255,0.18)"',
     '    onClick: () => { if (globalThis.spectrHeaderTipHide) globalThis.spectrHeaderTipHide(); onToggle(); },\n'
     '    ' + hover_props("auto") + '\n'
     '    style: {\n      background: on ? "rgba(120,180,255,0.18)"'),
    ("LIVE tip",
     '    onClick: () => spectrToggleFreeze(),\n',
     '    onClick: () => { if (globalThis.spectrHeaderTipHide) globalThis.spectrHeaderTipHide(); spectrToggleFreeze(); },\n'
     '    ' + hover_props("freeze") + '\n'),
    ("LENGTH tip",
     '    "data-spectr-freeze-length": true,\n',
     '    "data-spectr-freeze-length": true,\n    ' + hover_props("length") + '\n'),
    ("PEAK tip",
     '    onClick: resetHold,\n',
     '    onClick: () => { if (globalThis.spectrHeaderTipHide) globalThis.spectrHeaderTipHide(); resetHold(); },\n'
     '    ' + hover_props("peak") + '\n'),
    ("the tooltip renders inside the cluster",
     '  /* @__PURE__ */ React.createElement("button", {\n    "data-spectr-output-peak": true,',
     TIP_ELEMENT + '  /* @__PURE__ */ React.createElement("button", {\n    "data-spectr-output-peak": true,'),
]


def main():
    raw = open(PATH, encoding="utf-8").read()
    # patch_materialized_modulation_level_targets.py runs after this script
    # and rewrites some of the text it wrote (the target list, lane ids, the
    # knob). Its marker proves this script already ran.
    if escaped("function spectrLfosDrivingIn(") in raw:
        print("already applied (superseded in part by patch_materialized_modulation_level_targets.py)")
        return 0
    changed = False
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1:
            print("already applied ", label)
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        print("applied         ", label)
    html = json.loads(raw)["html"]
    if html.count('"data-spectr-header-tooltip": true') != 1:
        sys.exit("FAIL: the tooltip element is not present exactly once")
    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
