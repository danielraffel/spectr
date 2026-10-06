#!/usr/bin/env python3
"""A modulated knob or Morph slider shows ONE indicator: the value playing.

WHAT WAS WRONG. patch_materialized_modulation_controls_follow.py drew the
value an LFO plays ON TOP of the base: a knob kept its white needle and blue
arc at the base and gained a violet arc and needle at the value playing, and
Morph kept its white thumb and blue fill at the base under a violet bar. Two
selections on one control read as two controls, and neither says which one is
the sound.

WHAT THIS DOES. One indicator, the common synth pattern:

  knobs   the one needle and the one value arc show the value PLAYING, in the
          modulation violet while an LFO drives the knob (white and blue
          otherwise). The base -- what a drag, a wheel notch or an arrow key
          edits and what host automation records -- is a short tick across
          the ring. The readout prints the value playing, and the base while
          the knob is being dragged; the drag moves the tick.
  MORPH   one thumb and one fill, at the morph position playing, in violet;
          the React thumb and fill (the base) are hidden while the LFO drives
          Morph, and the base is a short tick on the track.

Still painted, never rendered: each control's paths and readout are written
straight to their native widgets, and a value that did not change is not sent.
A React render of the knob (a drag, a new base) rewrites the base geometry, so
the painter runs after every render and starts from a clean slate.

Raw-text surgery on the escaped document, applied after
patch_materialized_modulation_controls_follow.py. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "function spectrSetPathStroke(node, color) {"

HELPERS = r'''// One SVG path's stroke colour, written straight to its native widget.
function spectrSetPathStroke(node, color) {
  if (!node) return;
  const native = globalThis.__pulpNativeBridgeFunctions__;
  const fn = (native && native.setSvgStroke) || globalThis.setSvgStroke;
  const id = node._id !== undefined ? node._id : node.id;
  if (typeof fn === "function" && id !== undefined && id !== null && id !== "") {
    fn(String(id), color);
    return;
  }
  if (typeof node.setAttribute === "function") node.setAttribute("stroke", color);
}
// Writes `value` through `write` only when it differs from what `drawn[key]`
// last recorded, so an unchanged frame sends nothing.
function spectrDrawIf(drawn, key, value, write) {
  if (drawn[key] === value) return;
  drawn[key] = value;
  write(value);
}
'''

KNOB_PAINTER_OLD = '''  // The value playing, drawn over the base (see spectrModControlsStore). The
  // base needle and arc stay React's; this one path is painted directly.
  const modPathRef = React.useRef(null);
  const paintModulated = () => {
    const node = modPathRef.current;
    if (!node) return;
    const played = span > 0 ? spectrModulatedKnobValue(name, valueRef.current, min, max) : null;
    let d = "";
    if (played !== null) {
      const at = (v) => -SWEEP + 2 * SWEEP * Math.max(0, Math.min(1, (v - min) / span));
      const baseAngle = at(valueRef.current);
      const playAngle = Math.round(at(played) * 4) / 4;
      const c0 = SIZE / 2, r0 = c0 - 2.5, a = playAngle * Math.PI / 180;
      d = (Math.abs(playAngle - baseAngle) > 0.5
          ? spectrKnobArc(c0, c0, r0, Math.min(baseAngle, playAngle), Math.max(baseAngle, playAngle)) + " "
          : "")
        + "M " + (c0 + 3.5 * Math.sin(a)).toFixed(2) + " " + (c0 - 3.5 * Math.cos(a)).toFixed(2)
        + " L " + (c0 + (r0 - 3) * Math.sin(a)).toFixed(2) + " " + (c0 - (r0 - 3) * Math.cos(a)).toFixed(2);
    }
    const drawn = spectrModControlsStore().drawn;
    if (drawn[name] === d) return;
    drawn[name] = d;
    spectrSetPathD(node, d);
  };
  React.useEffect(() => spectrModControlsSubscribe(paintModulated), []);
  React.useEffect(() => { paintModulated(); });
'''

KNOB_PAINTER_NEW = '''  // ONE indicator (tools/patch_materialized_modulated_single_indicator.py).
  // While an LFO drives this knob, its one needle and value arc show the
  // value PLAYING, in violet; the base (what a drag edits, what automation
  // records) is a short tick across the ring; the readout prints the value
  // playing, or the base while the knob is dragged. Painted directly; React
  // draws the base and owns the knob when nothing drives it.
  const arcPathRef = React.useRef(null);
  const needlePathRef = React.useRef(null);
  const basePathRef = React.useRef(null);
  const readoutRef = React.useRef(null);
  const knobPropsRef = React.useRef(null);
  knobPropsRef.current = { format, origin, min, max };
  const paintModulated = (fresh) => {
    const p = knobPropsRef.current;
    const lo = p.min, hi = p.max, range = hi - lo;
    const store = spectrModControlsStore();
    const drawn = store.drawn[name] || (store.drawn[name] = {});
    // A render just rewrote the base geometry: nothing drawn here still holds.
    if (fresh === true) for (const key of Object.keys(drawn)) if (key !== "active") delete drawn[key];
    const base = valueRef.current;
    const played = range > 0 ? spectrModulatedKnobValue(name, base, lo, hi) : null;
    if (played === null && !drawn.active) return;
    drawn.active = played !== null;
    const at = (v) => -SWEEP + 2 * SWEEP * Math.max(0, Math.min(1, (v - lo) / range));
    const c0 = SIZE / 2, r0 = c0 - 2.5;
    const angle = played === null ? at(base) : Math.round(at(played) * 4) / 4;
    const originAngle = at(typeof p.origin === "number" ? p.origin : lo);
    const a = angle * Math.PI / 180;
    const arcD = Math.abs(angle - originAngle) > 0.5
      ? spectrKnobArc(c0, c0, r0, Math.min(originAngle, angle), Math.max(originAngle, angle))
      : "";
    const needleD = "M " + (c0 + 3.5 * Math.sin(a)).toFixed(2) + " " + (c0 - 3.5 * Math.cos(a)).toFixed(2)
      + " L " + (c0 + (r0 - 3) * Math.sin(a)).toFixed(2) + " " + (c0 - (r0 - 3) * Math.cos(a)).toFixed(2);
    let tickD = "";
    if (played !== null) {
      const b = at(base) * Math.PI / 180, r1 = r0 - 2.2, r2 = r0 + 2.2;
      tickD = "M " + (c0 + r1 * Math.sin(b)).toFixed(2) + " " + (c0 - r1 * Math.cos(b)).toFixed(2)
        + " L " + (c0 + r2 * Math.sin(b)).toFixed(2) + " " + (c0 - r2 * Math.cos(b)).toFixed(2);
    }
    const violet = "rgb(205,180,255)";
    spectrDrawIf(drawn, "arc", arcD, (d) => spectrSetPathD(arcPathRef.current, d));
    spectrDrawIf(drawn, "needle", needleD, (d) => spectrSetPathD(needlePathRef.current, d));
    spectrDrawIf(drawn, "base", tickD, (d) => spectrSetPathD(basePathRef.current, d));
    spectrDrawIf(drawn, "arcStroke", played === null ? "hsl(200,80%,60%)" : violet,
      (c) => spectrSetPathStroke(arcPathRef.current, c));
    spectrDrawIf(drawn, "needleStroke", played === null ? "#fff" : violet,
      (c) => spectrSetPathStroke(needlePathRef.current, c));
    const text = p.format(played === null || dragRef.current ? base : played);
    spectrDrawIf(drawn, "text", text, (t) => {
      const el = readoutRef.current;
      if (el) el.textContent = t;
    });
  };
  React.useEffect(() => spectrModControlsSubscribe(paintModulated), []);
  React.useEffect(() => { paintModulated(true); });
'''

EDITS = [
    (
        "helpers after spectrSetPathD",
        '''  if (typeof node.setAttribute === "function") node.setAttribute("d", d);
}
function SpectrKnob(''',
        '''  if (typeof node.setAttribute === "function") node.setAttribute("d", d);
}
''' + HELPERS + '''function SpectrKnob(''',
    ),
    ("knob painter: one indicator", KNOB_PAINTER_OLD, KNOB_PAINTER_NEW),
    (
        "knob ring: quiet tint while modulated",
        '''        stroke: modulated ? "rgba(190,150,255,0.55)" : "rgba(255,255,255,0.14)", strokeWidth: 2, fill: "none",''',
        '''        stroke: modulated ? "rgba(190,150,255,0.24)" : "rgba(255,255,255,0.14)", strokeWidth: 2, fill: "none",''',
    ),
    (
        "knob rim: a quiet violet while an LFO drives it",
        '''        border: "1px solid " + (modulated ? "rgba(190,150,255,0.65)" : "rgba(255,255,255,0.10)"),''',
        '''        border: "1px solid " + (modulated ? "rgba(190,150,255,0.38)" : "rgba(255,255,255,0.10)"),''',
    ),
    (
        "knob value arc: always present, painted while modulated",
        '''      Math.abs(angle - originAngle) > 0.5 && React.createElement("path", {
        d: spectrKnobArc(c, c, radius, Math.min(originAngle, angle), Math.max(originAngle, angle)),
        stroke: "hsl(200,80%,60%)", strokeWidth: 2, fill: "none",
        strokeLinecap: "round" }),''',
        '''      React.createElement("path", {
        ref: arcPathRef, "data-spectr-knob-arc": name,
        d: Math.abs(angle - originAngle) > 0.5
          ? spectrKnobArc(c, c, radius, Math.min(originAngle, angle), Math.max(originAngle, angle))
          : "",
        stroke: "hsl(200,80%,60%)", strokeWidth: 2, fill: "none",
        strokeLinecap: "round" }),''',
    ),
    (
        "knob needle ref",
        '''      React.createElement("path", {
        d: "M " + (c + 3.5 * Math.sin(rad)).toFixed(2) + " " + (c - 3.5 * Math.cos(rad)).toFixed(2)''',
        '''      React.createElement("path", {
        ref: needlePathRef, "data-spectr-knob-needle": name,
        d: "M " + (c + 3.5 * Math.sin(rad)).toFixed(2) + " " + (c - 3.5 * Math.cos(rad)).toFixed(2)''',
    ),
    (
        "knob base tick replaces the second indicator",
        '''      React.createElement("path", {
        ref: modPathRef, d: "", "data-spectr-knob-played": name,
        stroke: "rgb(205,180,255)", strokeWidth: 2, fill: "none",
        strokeLinecap: "round" }))),''',
        '''      React.createElement("path", {
        ref: basePathRef, d: "", "data-spectr-knob-base": name,
        stroke: "rgba(255,255,255,0.85)", strokeWidth: 1.4, fill: "none",
        strokeLinecap: "round" }))),''',
    ),
    (
        "knob readout ref",
        '''    React.createElement("span", {
      ["data-spectr-" + name + "-readout"]: true,''',
        '''    React.createElement("span", {
      ref: readoutRef,
      ["data-spectr-" + name + "-readout"]: true,''',
    ),
    (
        "morph painter: one thumb",
        '''  const morphPlayedRef = React.useRef(null);
  const paintMorphPlayed = () => {
    const node = morphPlayedRef.current;
    if (!node) return;
    if (typeof spectrModControlsStore !== "function") return;
    const s = spectrModControlsStore().state;
    let d = "";
    if (s.morphOn && morphBothRef.current) {
      const W = 90;
      const base = Math.max(0, Math.min(1, morphBaseRef.current));
      const played = Math.max(0, Math.min(1, base + s.morphOffset));
      const x = Math.round(played * W * 4) / 4, xb = base * W;
      d = (Math.abs(x - xb) > 0.5 ? "M " + xb.toFixed(2) + " 8 L " + x.toFixed(2) + " 8 " : "")
        + "M " + x.toFixed(2) + " 2.5 L " + x.toFixed(2) + " 13.5";
    }
    const drawn = spectrModControlsStore().drawn;
    if (drawn.morph === d) return;
    drawn.morph = d;
    spectrSetPathD(node, d);
  };''',
        '''  // ONE thumb (tools/patch_materialized_modulated_single_indicator.py):
  // while an LFO drives Morph the thumb and fill sit at the position playing,
  // in violet, the React thumb and fill (the base) are hidden, and the base
  // is a short tick on the track.
  const morphPlayedRef = React.useRef(null);
  const morphThumbPlayedRef = React.useRef(null);
  const morphBasePathRef = React.useRef(null);
  const morphFillRef = React.useRef(null);
  const morphThumbRef = React.useRef(null);
  const paintMorphPlayed = () => {
    if (!morphPlayedRef.current) return;
    if (typeof spectrModControlsStore !== "function") return;
    const store = spectrModControlsStore();
    const s = store.state;
    const drawn = store.drawn.morph || (store.drawn.morph = {});
    let fillD = "", thumbD = "", baseD = "";
    const on = s.morphOn && morphBothRef.current;
    if (on) {
      const W = 90, TRAVEL = 68, HALF = 11;
      const base = Math.max(0, Math.min(1, morphBaseRef.current));
      const played = Math.max(0, Math.min(1, base + s.morphOffset));
      const xf = Math.round(played * W * 4) / 4;
      const xt = Math.round((HALF + played * TRAVEL) * 4) / 4;
      const xb = HALF + base * TRAVEL;
      fillD = xf > 0.5 ? "M 2 8 L " + xf.toFixed(2) + " 8" : "";
      // The React thumb's pill: 22 x 14 with round ends, one unit down.
      const l = (xt - 4).toFixed(2), r = (xt + 4).toFixed(2);
      thumbD = "M " + l + " 1 L " + r + " 1 A 7 7 0 0 1 " + r + " 15 L " + l
        + " 15 A 7 7 0 0 1 " + l + " 1 Z";
      baseD = "M " + xb.toFixed(2) + " 3 L " + xb.toFixed(2) + " 13";
    }
    spectrDrawIf(drawn, "fill", fillD, (d) => spectrSetPathD(morphPlayedRef.current, d));
    spectrDrawIf(drawn, "thumb", thumbD, (d) => spectrSetPathD(morphThumbPlayedRef.current, d));
    spectrDrawIf(drawn, "base", baseD, (d) => spectrSetPathD(morphBasePathRef.current, d));
    spectrDrawIf(drawn, "hidden", on ? "1" : "", (h) => {
      for (const el of [morphFillRef.current, morphThumbRef.current])
        if (el && el.style) el.style.opacity = h ? "0" : "1";
    });
  };''',
    ),
    (
        "morph fill ref",
        '''    /* @__PURE__ */ React.createElement("div", { style: { position: "absolute", left: 0, top: 6, height: 4, borderRadius: 2, pointerEvents: "none", background: "hsl(200,80%,60%)", width: (100 * ratio) + "%" } }),''',
        '''    /* @__PURE__ */ React.createElement("div", { ref: morphFillRef, style: { position: "absolute", left: 0, top: 6, height: 4, borderRadius: 2, pointerEvents: "none", background: "hsl(200,80%,60%)", width: (100 * ratio) + "%" } }),''',
    ),
    (
        "morph thumb ref",
        '''React.createElement("div", { "data-spectr-morph-thumb": true, "data-spectr-morph-thumb-state":''',
        '''React.createElement("div", { ref: morphThumbRef, "data-spectr-morph-thumb": true, "data-spectr-morph-thumb-state":''',
    ),
    (
        "morph played fill, thumb and base tick",
        '''      React.createElement("path", { ref: morphPlayedRef, d: "", "data-spectr-morph-played": true,
        stroke: "rgb(205,180,255)", strokeWidth: 3, fill: "none", strokeLinecap: "round" }))''',
        '''      React.createElement("path", { ref: morphPlayedRef, d: "", "data-spectr-morph-played": true,
        stroke: "rgb(205,180,255)", strokeWidth: 4, fill: "none", strokeLinecap: "round" }),
      React.createElement("path", { ref: morphBasePathRef, d: "", "data-spectr-morph-base": true,
        stroke: "rgba(255,255,255,0.85)", strokeWidth: 1.4, fill: "none", strokeLinecap: "round" }),
      React.createElement("path", { ref: morphThumbPlayedRef, d: "", "data-spectr-morph-played-thumb": true,
        stroke: "none", fill: "rgb(205,180,255)" }))''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("modulated single indicator already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("modulated single indicator applied (%d edits)" % len(EDITS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
