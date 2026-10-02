#!/usr/bin/env python3
"""Header level controls: MIX, INTENSITY and OUTPUT as knobs, plus AUTO.

The header's right-hand cluster carried one level control, the Output trim,
as a 96pt range slider. Testers asked for two more -- "a knob for overall gain
scaling so I could turn the effect up and down" (Intensity, host parameter
5000) and "auto compensate" (Auto Gain, 5001) -- and Mix (parameter 1) has
never had an editor control at all. Three sliders do not fit in the header,
so all three become compact knobs sharing one component, and Auto Gain is a
small AUTO pill attached to the Output knob it sits in front of.

    SPECTR | LIVE | LENGTH [1 bar v] | MIX (o) 100% | INTENSITY (o) 100% |
    OUTPUT (o) 0.0 [AUTO] | PEAK -- | BARS RESPONSE BOTH | 32 BANDS v . ZOOM

THE KNOB (`SpectrKnob`). A 26pt dark body with a 270-degree track arc and a
value arc in the slider fill's blue (hsl(200,80%,60%)), drawn from the knob's
ORIGIN (0 dB for the bipolar Output, the minimum otherwise) to the value, and
a white pointer. Label left in the header's caption style, mono readout right
in a fixed box so nothing moves as digits change. Every decoration is
pointer-events none, so the first press lands on the knob itself.

  * Vertical OR horizontal drag: 160pt of travel is the full range. Shift or
    Option for fine (x0.1, finer quantisation). One host gesture per drag:
    param_gesture_begin on press, param_edit values inside, end on release.
  * Double press (500 ms, the AppKit interval; detected from the press stream
    because React `onDoubleClick` never fires in this runtime) resets to the
    parameter default: Mix 100 %, Intensity 100 %, Output 0.0 dB.
  * Wheel / trackpad: steps the value; consecutive wheel events share one
    gesture that closes 350 ms after the last one.
  * Arrow keys when focused (Shift for fine), Home/End: one complete gesture
    per press.
  * Host automation moves it: the values ride the `output_meter` publication
    (Spectr::read_output_level), ignored for 400 ms after a local write so a
    round trip cannot fight a drag.

GEOMETRY. Three knobs do not fit beside the "ZOOMABLE FILTER BANK" tagline:
from x=282 the cluster would end near x=1000, over BARS at 859.5. Without the
tagline the cluster starts at 113 (18pt after SPECTR, the header's own gap)
and ends at ~830, inside the divider before BARS at 839.5. PEAK narrows from
96 to 84pt, the widest reading ("PEAK -99.9") plus its padding.

Variants for comparison renders: --tagline / --no-tagline, --mix / --no-mix
rewrite the two constants below. The shipped state is --no-tagline --mix.

Idempotent like the other patch_materialized_* scripts: exact substitutions,
each asserted unique, "already applied" on a second run.
"""

import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def escaped(value):
    return json.dumps(value)[1:-1]


KNOB_COMPONENT = r'''// ── Header level knobs (tools/patch_materialized_level_controls.py) ──
// Header variants: whether the "ZOOMABLE FILTER BANK" tagline shows, and
// whether MIX has a header knob (false leaves Mix a host-only lane). One
// constant each, so either is a one-line change.
const SPECTR_HEADER_SHOW_TAGLINE = true;
const SPECTR_HEADER_SHOW_MIX = true;
// Host parameter IDs (spectr.hpp / level_controls.hpp).
const SPECTR_PARAM_MIX = 1;
const SPECTR_PARAM_OUTPUT = 2;
const SPECTR_PARAM_INTENSITY = 5000;
const SPECTR_PARAM_AUTO_GAIN = 5001;
// A polyline arc: 0 degrees is straight up, positive is clockwise. Drawn as
// line segments rather than an SVG arc command so it renders the same in
// every renderer this document meets.
function spectrKnobArc(cx, cy, r, from, to) {
  const steps = Math.max(1, Math.ceil(Math.abs(to - from) / 6));
  let d = "";
  for (let i = 0; i <= steps; i++) {
    const a = (from + (to - from) * i / steps) * Math.PI / 180;
    d += (i ? " L " : "M ") + (cx + r * Math.sin(a)).toFixed(2) + " "
      + (cy - r * Math.cos(a)).toFixed(2);
  }
  return d;
}
function SpectrKnob({ paramId, name, label, value, min, max, step, fineStep,
                      defaultValue, origin, format, ariaLabel, title,
                      readoutWidth, onEdit, after }) {
  const SIZE = 26;
  const SWEEP = 135;
  // Points of drag travel for the whole range; Shift/Option is ten times finer.
  const TRAVEL = 160;
  const FINE = 0.1;
  const DOUBLE_PRESS_MS = 500;
  const WHEEL_IDLE_MS = 350;
  const span = max - min;
  const valueRef = React.useRef(value);
  valueRef.current = value;
  const dragRef = React.useRef(null);
  const lastPressRef = React.useRef(0);
  const wheelRef = React.useRef({ open: false, timer: 0 });
  const quantise = (v, q) => {
    const snapped = Math.round((v - min) / q) * q + min;
    return Math.max(min, Math.min(max, Number(snapped.toFixed(6))));
  };
  const write = (next, fine) => {
    if (!isFinite(next)) return;
    const v = quantise(next, fine ? (fineStep || step) : step);
    if (v === valueRef.current) return;
    valueRef.current = v;
    onEdit(v);
  };
  const gesture = (open) => globalThis.spectrParamGesture
    && globalThis.spectrParamGesture(paramId, open);
  const isFine = (e) => !!(e && (e.shiftKey || e.altKey));
  const endDrag = () => {
    dragRef.current = null;
    gesture(false);
  };
  const onPointerDown = (e) => {
    const node = e && e.currentTarget;
    if (node && node.setPointerCapture && e.pointerId !== undefined) {
      try { node.setPointerCapture(e.pointerId); } catch (err) {}
    }
    gesture(true);
    const now = Date.now();
    if (now - lastPressRef.current <= DOUBLE_PRESS_MS) {
      // The second press of a pair: back to the default, inside the bracket
      // the press just opened, which its release closes.
      lastPressRef.current = 0;
      dragRef.current = null;
      write(defaultValue, true);
      return;
    }
    lastPressRef.current = now;
    dragRef.current = {
      x: e && typeof e.clientX === "number" ? e.clientX : 0,
      y: e && typeof e.clientY === "number" ? e.clientY : 0,
      from: valueRef.current, fine: isFine(e)
    };
  };
  const onPointerMove = (e) => {
    const d = dragRef.current;
    if (!d || !e) return;
    const x = typeof e.clientX === "number" ? e.clientX : d.x;
    const y = typeof e.clientY === "number" ? e.clientY : d.y;
    const fine = isFine(e);
    if (fine !== d.fine) {
      // Re-anchor when the modifier changes, so pressing Shift mid-drag
      // never jumps the value.
      d.x = x; d.y = y; d.from = valueRef.current; d.fine = fine;
      return;
    }
    const travel = (d.y - y) + (x - d.x);
    write(d.from + travel / TRAVEL * span * (fine ? FINE : 1), fine);
  };
  const onWheel = (e) => {
    const dy = e && typeof e.deltaY === "number" ? e.deltaY : 0;
    if (!dy) return;
    if (typeof e.preventDefault === "function") e.preventDefault();
    if (typeof e.stopPropagation === "function") e.stopPropagation();
    const fine = isFine(e);
    const w = wheelRef.current;
    const canIdle = typeof setTimeout === "function";
    if (!w.open) { gesture(true); w.open = true; }
    // A line-stepped wheel moves one step per notch; a trackpad's small
    // deltas move in proportion, never by less than the finest step.
    const amount = Math.min(Math.abs(dy), 40) / TRAVEL * span * (fine ? FINE : 1);
    const q = fine ? (fineStep || step) : step;
    write(valueRef.current - Math.sign(dy) * Math.max(q, amount), fine);
    if (canIdle) {
      if (w.timer && typeof clearTimeout === "function") clearTimeout(w.timer);
      w.timer = setTimeout(() => { w.timer = 0; w.open = false; gesture(false); },
                           WHEEL_IDLE_MS);
    } else {
      w.open = false;
      gesture(false);
    }
  };
  const onKeyDown = (e) => {
    if (!e) return;
    const fine = isFine(e);
    const q = fine ? (fineStep || step) : step;
    let next = null;
    if (e.key === "ArrowUp" || e.key === "ArrowRight") next = valueRef.current + q;
    else if (e.key === "ArrowDown" || e.key === "ArrowLeft") next = valueRef.current - q;
    else if (e.key === "PageUp") next = valueRef.current + q * 10;
    else if (e.key === "PageDown") next = valueRef.current - q * 10;
    else if (e.key === "Home") next = min;
    else if (e.key === "End") next = max;
    if (next === null) return;
    if (typeof e.preventDefault === "function") e.preventDefault();
    if (typeof e.stopPropagation === "function") e.stopPropagation();
    gesture(true);
    write(next, fine);
    gesture(false);
  };
  React.useEffect(() => () => {
    const w = wheelRef.current;
    if (w.timer && typeof clearTimeout === "function") clearTimeout(w.timer);
    if (w.open) gesture(false);
    if (dragRef.current) gesture(false);
  }, []);
  const fraction = span > 0 ? Math.max(0, Math.min(1, (value - min) / span)) : 0;
  const angle = -SWEEP + 2 * SWEEP * fraction;
  const originValue = typeof origin === "number" ? origin : min;
  const originAngle = -SWEEP + 2 * SWEEP * Math.max(0, Math.min(1, (originValue - min) / span));
  const c = SIZE / 2;
  const radius = c - 2.5;
  const rad = angle * Math.PI / 180;
  const text = format(value);
  const caption = {
    fontFamily: SPECTR_HEADER_MONO, fontSize: 10, letterSpacing: 0.8,
    color: "rgba(255,255,255,0.72)", whiteSpace: "nowrap",
    flexShrink: 0, pointerEvents: "none"
    // No lineHeight: 1 here. The native text path does not apply a negative
    // half-leading, so a 10pt line box set its baseline 1.6pt below LENGTH's.
  };
  return React.createElement("span", {
    ["data-spectr-" + name + "-group"]: true,
    style: { display: "inline-flex", alignItems: "center", gap: 6, flexShrink: 0,
             pointerEvents: "box-none" }
  },
    React.createElement("span", { ["data-spectr-" + name + "-label"]: true, style: caption }, label),
    React.createElement("div", {
      ["data-spectr-" + name]: true,
      "data-spectr-knob": name,
      role: "slider",
      tabIndex: 0,
      "aria-label": ariaLabel,
      "aria-valuemin": min,
      "aria-valuemax": max,
      "aria-valuenow": value,
      "aria-valuetext": text,
      title,
      onPointerDown,
      onPointerMove,
      onPointerUp: endDrag,
      onPointerCancel: endDrag,
      onLostPointerCapture: () => { if (dragRef.current) endDrag(); },
      onWheel,
      onKeyDown,
      style: {
        position: "relative", width: SIZE, height: SIZE, flexShrink: 0,
        borderRadius: SIZE / 2, boxSizing: "border-box",
        background: "rgba(255,255,255,0.03)",
        border: "1px solid rgba(255,255,255,0.10)",
        cursor: "ns-resize"
      }
    }, React.createElement("svg", {
      width: SIZE, height: SIZE, viewBox: "0 0 " + SIZE + " " + SIZE,
      "aria-hidden": true,
      style: { position: "absolute", left: -1, top: -1, pointerEvents: "none" }
    },
      React.createElement("path", { d: spectrKnobArc(c, c, radius, -SWEEP, SWEEP),
        stroke: "rgba(255,255,255,0.14)", strokeWidth: 2, fill: "none",
        strokeLinecap: "round" }),
      Math.abs(angle - originAngle) > 0.5 && React.createElement("path", {
        d: spectrKnobArc(c, c, radius, Math.min(originAngle, angle), Math.max(originAngle, angle)),
        stroke: "hsl(200,80%,60%)", strokeWidth: 2, fill: "none",
        strokeLinecap: "round" }),
      React.createElement("path", {
        d: "M " + (c + 3.5 * Math.sin(rad)).toFixed(2) + " " + (c - 3.5 * Math.cos(rad)).toFixed(2)
          + " L " + (c + (radius - 3) * Math.sin(rad)).toFixed(2) + " "
          + (c - (radius - 3) * Math.cos(rad)).toFixed(2),
        stroke: "#fff", strokeWidth: 1.6, fill: "none", strokeLinecap: "round" }))),
    React.createElement("span", {
      ["data-spectr-" + name + "-readout"]: true,
      className: "tnum",
      style: Object.assign({}, caption, { width: readoutWidth, textAlign: "left",
                                          letterSpacing: 0 })
    }, text),
    after || null);
}
// Auto Gain's switch, attached to the Output knob it feeds. Lit when on; the
// tooltip says what it is applying now.
function SpectrAutoGainPill({ on, appliedDb, onToggle }) {
  const applied = typeof appliedDb === "number" && isFinite(appliedDb) ? appliedDb : 0;
  const shown = (applied > 0 ? "+" : applied < 0 ? "−" : "") + Math.abs(applied).toFixed(1);
  return React.createElement("button", {
    "data-spectr-auto-gain": true,
    "data-spectr-auto-gain-state": on ? "on" : "off",
    "aria-pressed": on ? "true" : "false",
    "aria-label": "Auto Gain " + (on ? "on" : "off"),
    title: on
      ? "Auto Gain is on: " + shown + " dB keeps boosts and cuts near the input's "
        + "loudness. Output trims on top. Click to turn off."
      : "Auto Gain is off. Click to keep boosts and cuts near the input's loudness.",
    onClick: onToggle,
    style: {
      background: on ? "rgba(120,180,255,0.18)" : "rgba(255,255,255,0.03)",
      border: "1px solid " + (on ? "rgba(180,210,255,0.55)" : "rgba(255,255,255,0.10)"),
      color: on ? "#fff" : "rgba(255,255,255,0.55)",
      padding: "0 5px", height: 18, minHeight: 18, width: 38, minWidth: 38,
      boxSizing: "border-box", borderRadius: 3, flexShrink: 0,
      fontFamily: SPECTR_HEADER_MONO, fontSize: 9, letterSpacing: 0.8,
      cursor: "pointer", display: "inline-flex", alignItems: "center",
      justifyContent: "center", lineHeight: 1
    }
  }, React.createElement("span", { style: { pointerEvents: "none", lineHeight: 1 } }, "AUTO"));
}
'''

STATE_ANCHOR = '  const [trim, setTrim] = React.useState(0);\n'
STATE_NEW = STATE_ANCHOR + '''  // The header's other level controls (tools/patch_materialized_level_controls.py).
  // Host parameters, published with the meter so automation moves them.
  const [mix, setMix] = React.useState(100);
  const [intensity, setIntensity] = React.useState(100);
  const [autoGain, setAutoGain] = React.useState(false);
  const [autoGainDb, setAutoGainDb] = React.useState(0);
  const localLevelAtRef = React.useRef({});
'''

HANDLER_ANCHOR = '''      const published = payload.trim_db;
'''
HANDLER_NEW = '''      // Mix, Intensity and Auto Gain, under the same local-write guard as
      // the trim below.
      const fresh = (key) => Date.now() - (localLevelAtRef.current[key] || 0) > 400;
      const adopt = (key, raw, set) => {
        if (typeof raw === "number" && isFinite(raw) && fresh(key))
          set((previous) => Math.abs(previous - raw) < 0.001 ? previous : raw);
      };
      adopt("mix", payload.mix_pct, setMix);
      adopt("intensity", payload.intensity_pct, setIntensity);
      if (typeof payload.auto_gain === "boolean" && fresh("autoGain"))
        setAutoGain((previous) => previous === payload.auto_gain ? previous : payload.auto_gain);
      if (typeof payload.auto_gain_db === "number" && isFinite(payload.auto_gain_db))
        setAutoGainDb((previous) => Math.abs(previous - payload.auto_gain_db) < 0.05
          ? previous : payload.auto_gain_db);
''' + HANDLER_ANCHOR

WRITE_ANCHOR = '  const resetHold = () => {\n'
WRITE_NEW = '''  // One write path for the knobs and the pill: local state at once, the
  // host through param_edit -- inside an open drag gesture a value, outside
  // one a complete begin/value/end bracket.
  const writeLevelParam = (key, id, value, set) => {
    if (!isFinite(value)) return;
    localLevelAtRef.current[key] = Date.now();
    set(value);
    try {
      window.pulp.postMessage("param_edit", { id, value }, "spectr-level-" + key);
    } catch (error) {
      console.error("[Spectr] " + key + " write failed", error);
    }
  };
  const percentText = (v) => Math.round(v) + "%";
''' + WRITE_ANCHOR

PEAK_OLD = '''      padding: "5px 8px",
      width: 96,
      minWidth: 96,'''
PEAK_NEW = '''      padding: "5px 8px",
      // 84, not 96: the widest reading ("PEAK -99.9") plus padding, which
      // is what lets three level knobs share the header.
      width: 84,
      minWidth: 84,'''
PEAK_LABEL_OLD = '''      lineHeight: 1, whiteSpace: "nowrap", width: 78, minWidth: 78,'''
PEAK_LABEL_NEW = '''      lineHeight: 1, whiteSpace: "nowrap", width: 66, minWidth: 66,'''

LEFT_OLD = '''    position: "absolute",
    left: 282,
    top: 9,
    height: 26,'''
LEFT_NEW = '''    position: "absolute",
    // 18pt after SPECTR (the header's own gap) without the tagline; after
    // the tagline otherwise.
    left: SPECTR_HEADER_SHOW_TAGLINE ? 282 : 113,
    top: 9,
    height: 26,'''

TAGLINE_OLD = ('/* @__PURE__ */ React.createElement("span", { style: { opacity: 0.4 } }, "\\xB7"), '
               '/* @__PURE__ */ React.createElement("span", { style: { opacity: 0.55 } }, "ZOOMABLE FILTER BANK")')
TAGLINE_NEW = ('/* @__PURE__ */ React.createElement("span", { style: { opacity: 0.4, display: SPECTR_HEADER_SHOW_TAGLINE ? undefined : "none" } }, "\\xB7"), '
               '/* @__PURE__ */ React.createElement("span", { "data-spectr-header-tagline": true, style: { opacity: 0.55, display: SPECTR_HEADER_SHOW_TAGLINE ? undefined : "none" } }, "ZOOMABLE FILTER BANK")')

GEOMETRY_OLD_START = "  // The header is empty from x=281.7 (the end of the title block) to the\n"
GEOMETRY_OLD_END = "  // same 14pt gap to the value that it used to keep to OUTPUT.\n"
GEOMETRY_NEW = '''  // The header is empty from the title block (x=281.7) to the divider before
  // the band-count menu (BARS / RESPONSE / BOTH moved to Settings, see
  // tools/patch_materialized_display_setting.py). This sits at 282 and runs
  // 74 freeze + LENGTH + its 86pt dropdown, then MIX, INTENSITY and OUTPUT
  // knob groups (caption, 26pt knob, fixed-width readout; AUTO rides the
  // Output group) and the 96pt PEAK chip, ending near x=1000. Without the
  // tagline it starts at 113 instead.
'''


def knob_block(font):
    return '''        SPECTR_HEADER_SHOW_MIX && /* @__PURE__ */ React.createElement(SpectrKnob, {
      paramId: SPECTR_PARAM_MIX, name: "mix", label: "MIX", value: mix,
      min: 0, max: 100, step: 1, fineStep: 0.1, defaultValue: 100, origin: 0,
      format: percentText, readoutWidth: 26,
      ariaLabel: "Mix, percent of Spectr's output against the original input",
      title: "Mix: blends Spectr with the original input. Double-click for 100%.",
      onEdit: (v) => writeLevelParam("mix", SPECTR_PARAM_MIX, v, setMix)
    }),
        /* @__PURE__ */ React.createElement(SpectrKnob, {
      paramId: SPECTR_PARAM_INTENSITY, name: "intensity", label: "INTENSITY", value: intensity,
      min: 0, max: 100, step: 1, fineStep: 0.1, defaultValue: 100, origin: 0,
      format: percentText, readoutWidth: 26,
      ariaLabel: "Intensity, percent of the drawn shape",
      title: "Intensity: scales the whole shape toward flat. Double-click for 100%.",
      onEdit: (v) => writeLevelParam("intensity", SPECTR_PARAM_INTENSITY, v, setIntensity)
    }),
        /* @__PURE__ */ React.createElement(SpectrKnob, {
      paramId: SPECTR_PARAM_OUTPUT, name: "output-trim", label: "OUTPUT", value: trim,
      min: -24, max: 24, step: 0.5, fineStep: 0.1, defaultValue: 0, origin: 0,
      format: (v) => (v > 0 ? "+" : "") + v.toFixed(1), readoutWidth: 31,
      ariaLabel: "Output trim, decibels",
      title: "Output trim, dB. Applied after Auto Gain. Double-click for 0.0.",
      onEdit: (v) => writeTrim(v),
      after: /* @__PURE__ */ React.createElement(SpectrAutoGainPill, {
        on: autoGain, appliedDb: autoGainDb,
        onToggle: () => writeLevelParam("autoGain", SPECTR_PARAM_AUTO_GAIN, autoGain ? 0 : 1,
                                   (v) => setAutoGain(v >= 0.5))
      })
    }),
'''


def main(argv):
    tagline = None
    mix = None
    for arg in argv:
        if arg == "--tagline": tagline = True
        elif arg == "--no-tagline": tagline = False
        elif arg == "--mix": mix = True
        elif arg == "--no-mix": mix = False
        else:
            sys.exit("usage: %s [--tagline|--no-tagline] [--mix|--no-mix]" % sys.argv[0])

    raw = open(PATH, encoding="utf-8").read()
    html = json.loads(raw)["html"]
    changed = False

    def sub(label, old, new):
        nonlocal raw, changed
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1:
            print("already applied ", label)
            return
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        print("applied         ", label)

    # The header's caption font, taken from the Output caption so the knobs
    # cannot drift from it.
    font_match = (re.search(r'const SPECTR_HEADER_MONO = (\'[^\']+\');', html)
                  or re.search(r'"data-spectr-output-trim-label": true,.*?fontFamily: (\'[^\']+\')',
                               html, re.S))
    if not font_match:
        sys.exit("FAIL: the Output caption's font declaration is missing")
    font = font_match.group(1)

    if "function SpectrKnob(" not in html:
        anchor = "function SpectrOutputMeter({ latchOver = false } = {}) {\n"
        sub("the knob and AUTO pill components",
            anchor, "const SPECTR_HEADER_MONO = " + font + ";\n" + KNOB_COMPONENT + anchor)
    else:
        print("already applied  the knob and AUTO pill components")

    sub("level state", STATE_ANCHOR, STATE_NEW)
    sub("level values follow the host", HANDLER_ANCHOR, HANDLER_NEW)
    sub("one level write path", WRITE_ANCHOR, WRITE_NEW)

    # The Output slider (caption, range input, readout) becomes the knob row.
    html = json.loads(raw)["html"]
    if 'name: "output-trim", label: "OUTPUT"' in html:
        print("already applied  the slider becomes MIX / INTENSITY / OUTPUT knobs")
    else:
        start_token = ('        /* @__PURE__ */ React.createElement("span", {\n'
                       '      "data-spectr-output-trim-label": true,')
        end_token = '    }, trimText),\n'
        start = html.find(start_token)
        end = html.find(end_token, start)
        if start < 0 or end < 0 or html.count(start_token) != 1:
            sys.exit("FAIL: the Output slider block is not where expected")
        old = html[start:end + len(end_token)]
        sub("the slider becomes MIX / INTENSITY / OUTPUT knobs", old, knob_block(font))

    sub("cluster starts after the brand", LEFT_OLD, LEFT_NEW)
    sub("tagline follows the header variant", TAGLINE_OLD, TAGLINE_NEW)

    html = json.loads(raw)["html"]
    if GEOMETRY_NEW in html:
        print("already applied  geometry comment")
    else:
        start = html.find(GEOMETRY_OLD_START)
        end = html.find(GEOMETRY_OLD_END, start)
        if start < 0 or end < 0:
            sys.exit("FAIL: the cluster geometry comment is not where expected")
        sub("geometry comment", html[start:end + len(GEOMETRY_OLD_END)], GEOMETRY_NEW)

    # Variant constants.
    for flag, value in (("SPECTR_HEADER_SHOW_TAGLINE", tagline), ("SPECTR_HEADER_SHOW_MIX", mix)):
        if value is None:
            continue
        want = "const %s = %s;" % (flag, "true" if value else "false")
        other = "const %s = %s;" % (flag, "false" if value else "true")
        if raw.count(escaped(want)) == 1:
            continue
        if raw.count(escaped(other)) != 1:
            sys.exit("FAIL: %s is not declared exactly once" % flag)
        raw = raw.replace(escaped(other), escaped(want), 1)
        changed = True
        print("set             ", want)

    html = json.loads(raw)["html"]
    for token, want in (('"data-spectr-output-trim": true', 0),
                        ('function SpectrKnob(', 1),
                        ('function SpectrAutoGainPill(', 1),
                        ('name: "intensity", label: "INTENSITY"', 1),
                        ('name: "mix", label: "MIX"', 1),
                        ('name: "output-trim", label: "OUTPUT"', 1)):
        if html.count(token) != want:
            sys.exit("FAIL: %r appears %d times after patching, expected %d"
                     % (token, html.count(token), want))

    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
