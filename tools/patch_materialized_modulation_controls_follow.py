#!/usr/bin/env python3
"""Modulated controls move: INTENSITY, MIX, OUTPUT, MORPH and the BANDS plot.

WHAT WAS WRONG. An LFO routed to Intensity, Mix or Output was audible and
invisible: the knob kept painting the user's value and nothing on screen moved.
The same for Morph (the A-B slider stayed put while the field morphed) and for
Bands (the plot kept drawing the user's band count while the mask played
another). Two causes. The processor never published anything for those
targets -- the modulated-field publication ran only for targets that reshape
the field -- and the editor had nowhere to draw a modulated value: every
control drew exactly one value, the base.

THE VISUAL. One rule for every modulated control: the base stays where the
user put it and stays editable; the value being played is drawn on top, in the
modulation violet, and moves.

  knobs   the white needle and the blue value arc are the base (what a drag,
          a wheel notch or an arrow key starts from, what the readout prints
          and what host automation records). A violet arc runs from the base
          to the value playing, and a violet needle sits at the value playing.
  MORPH   the white thumb is the base; a violet bar on the track marks the
          morph position playing, with a violet segment back to the thumb.
  BANDS   the plot draws the band count playing: the drawn slots are the
          audible ones. Band i keeps its gain (the mask re-lays the same
          gains over a different count); slots past the user's count are
          neutral and flat. Pointer edits address the slot under the pointer;
          a press on a neutral slot edits nothing.

Modulation never writes a host lane: the drawn indicator is display only,
built from the base value the control is showing and the LFO's coordinate, so
a control automation is moving shows the automated base and the LFO's swing
around it in the same frame (docs/automation.md, "Modulation and automation").

THE COST. Nothing here re-renders React. The processor sends
`modulation_controls` (four coordinates, four flags) only when a quantised
value moved -- 1/2048 of a knob's range is a twentieth of a degree -- and
each control repaints one SVG path node through `setSvgPath`; a path whose
string did not change is not sent. The band count changes a few times a
second at most and wakes the plot's draw loop once per change. Measured in
docs/modulation.md ("Modulated controls, measured").

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "function spectrModControlsStore() {"

STORE = r'''// Header controls an LFO moves, drawn at their modulated value
// (tools/patch_materialized_modulation_controls_follow.py). The processor
// sends each driven target's coordinate on change; every control registered
// here repaints its own SVG path from it. Painted, never rendered.
function spectrModControlsStore() {
  const g = globalThis;
  if (g.__spectrModControls) return g.__spectrModControls;
  const store = {
    state: { intensityOn: false, mixOn: false, outputOn: false, morphOn: false,
             intensityPull: 0, mixPull: 0, outputDb: 0, morphOffset: 0 },
    painters: [], frames: 0, drawn: {}
  };
  g.__spectrModControls = store;
  if (window.pulp && typeof window.pulp.on === "function") {
    window.pulp.on("modulation_controls", (message) => {
      const p = message && message.payload;
      if (!p || typeof p.intensity_on !== "boolean") return;
      const s = store.state;
      const num = (v) => { const n = Number(v); return Number.isFinite(n) ? n : 0; };
      s.intensityOn = p.intensity_on === true;
      s.mixOn = p.mix_on === true;
      s.outputOn = p.output_on === true;
      s.morphOn = p.morph_on === true;
      s.intensityPull = Math.max(-1, Math.min(1, num(p.intensity_pull)));
      s.mixPull = Math.max(-1, Math.min(1, num(p.mix_pull)));
      s.outputDb = num(p.output_db);
      s.morphOffset = num(p.morph_offset);
      store.frames++;
      for (let i = 0; i < store.painters.length; i++) {
        try { store.painters[i](); } catch (error) {}
      }
    });
  }
  return store;
}
function spectrModControlsSubscribe(paint) {
  const store = spectrModControlsStore();
  store.painters.push(paint);
  return () => {
    const at = store.painters.indexOf(paint);
    if (at >= 0) store.painters.splice(at, 1);
  };
}
// The value a knob is playing, in its own units, or null when no LFO drives
// it. Every target coordinate is normalized: -1 reaches the parameter
// minimum and +1 reaches its maximum from the authored base value.
function spectrModulatedKnobValue(name, base, min, max) {
  const s = spectrModControlsStore().state;
  if (!Number.isFinite(base)) return null;
  const normalized = (c) => c < 0
    ? base + c * (base - min) : base + c * (max - base);
  if (name === "intensity") return s.intensityOn ? normalized(s.intensityPull) : null;
  if (name === "mix") return s.mixOn ? normalized(s.mixPull) : null;
  if (name === "output-trim")
    return s.outputOn ? Math.max(min, Math.min(max, base + s.outputDb)) : null;
  return null;
}
// One SVG path's geometry, written straight to its native widget: no React
// commit, no layout. Falls back to the element's setAttribute.
function spectrSetPathD(node, d) {
  if (!node) return;
  const native = globalThis.__pulpNativeBridgeFunctions__;
  const fn = (native && native.setSvgPath) || globalThis.setSvgPath;
  const id = node._id !== undefined ? node._id : node.id;
  if (typeof fn === "function" && id !== undefined && id !== null && id !== "") {
    fn(String(id), d);
    return;
  }
  if (typeof node.setAttribute === "function") node.setAttribute("d", d);
}
'''

PLOT_HELPERS = r'''// The band count the plot draws: the count the Bands target plays while an
// LFO drives it (freeze_display), else the user's
// (tools/patch_materialized_modulation_controls_follow.py).
function spectrDrawnBandCount(n) {
  const store = typeof spectrFreezeStore === "function" ? spectrFreezeStore() : null;
  const shown = store && store.display ? Number(store.display.bands) : 0;
  return [32, 40, 48, 56, 64].includes(shown) ? shown : n;
}
function spectrPadBandRow(row, n, fill) {
  if (!row || row.length >= n) return row;
  const out = Array.from(row);
  while (out.length < n) out.push(fill);
  return out;
}
'''

EDITS = [
    (
        "plot helpers in the plot's own script",
        '''const { useRef, useEffect, useState, useCallback, useMemo } = React;
''',
        '''const { useRef, useEffect, useState, useCallback, useMemo } = React;
''' + PLOT_HELPERS,
    ),
    (
        "store before the knob",
        '''function SpectrKnob({ paramId, name, label, value, min, max, step, fineStep,''',
        STORE + '''function SpectrKnob({ paramId, name, label, value, min, max, step, fineStep,''',
    ),
    (
        "knob painter",
        '''  const dragRef = React.useRef(null);
  const lastPressRef = React.useRef(0);
''',
        '''  const dragRef = React.useRef(null);
  const lastPressRef = React.useRef(0);
  // The value playing, drawn over the base (see spectrModControlsStore). The
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
''',
    ),
    (
        "knob modulated path",
        '''        stroke: "#fff", strokeWidth: 1.6, fill: "none", strokeLinecap: "round" }))),''',
        '''        stroke: "#fff", strokeWidth: 1.6, fill: "none", strokeLinecap: "round" }),
      React.createElement("path", {
        ref: modPathRef, d: "", "data-spectr-knob-played": name,
        stroke: "rgb(205,180,255)", strokeWidth: 2, fill: "none",
        strokeLinecap: "round" }))),''',
    ),
    (
        "morph base ref",
        '''  const dragRef = React.useRef(null);
  const publishedRef = React.useRef(v);
''',
        '''  const dragRef = React.useRef(null);
  const publishedRef = React.useRef(v);
  // The morph position playing while an LFO drives Morph: a violet bar on
  // the track, the white thumb staying at the base (spectrModControlsStore).
  const morphBaseRef = React.useRef(v);
  morphBaseRef.current = v;
  const morphBothRef = React.useRef(hasBoth);
  morphBothRef.current = hasBoth;
  const morphPlayedRef = React.useRef(null);
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
  };
  useEffectChrome(() => typeof spectrModControlsSubscribe === "function"
    ? spectrModControlsSubscribe(paintMorphPlayed) : undefined, []);
  useEffectChrome(() => { paintMorphPlayed(); });
''',
    ),
    (
        "morph played path",
        '''marginLeft: -((grown ? 26 : 22) * ratio), left: (100 * ratio) + "%" } })
  ),''',
        '''marginLeft: -((grown ? 26 : 22) * ratio), left: (100 * ratio) + "%" } }),
    /* @__PURE__ */ React.createElement("svg", { width: 90, height: 16, viewBox: "0 0 90 16", "aria-hidden": true, style: { position: "absolute", left: 0, top: 0, pointerEvents: "none" } },
      React.createElement("path", { ref: morphPlayedRef, d: "", "data-spectr-morph-played": true,
        stroke: "rgb(205,180,255)", strokeWidth: 3, fill: "none", strokeLinecap: "round" }))
  ),''',
    ),
    (
        "plot geometry at the drawn count",
        '''    const bandGap = 2;
    const bandW = (inner.w - bandGap * (N - 1)) / N;
    return { w, h, pad, inner, zeroY, halfH, plotHalfH, bandW, bandGap };''',
        '''    const bandGap = 2;
    // The plot draws the band count playing (spectrDrawnBandCount); `nUser`
    // is the user's count, which every per-band array is sized to.
    const nd = typeof spectrDrawnBandCount === "function" ? spectrDrawnBandCount(N) : N;
    const bandW = (inner.w - bandGap * (nd - 1)) / nd;
    return { w, h, pad, inner, zeroY, halfH, plotHalfH, bandW, bandGap, nd, nUser: N };''',
    ),
    (
        "bands draw at the drawn count",
        '''  function drawBands(ctx, g) {
    const { inner, zeroY, halfH, bandW, bandGap } = g;
    const tg = targetGainsRef.current;
    const rg = renderGainsRef.current;''',
        '''  function drawBands(ctx, g) {
    // Slots past the user's count are neutral: flat, never muted.
    const N = g.nd;
    const { inner, zeroY, halfH, bandW, bandGap } = g;
    const tg = spectrPadBandRow(targetGainsRef.current, N, 0);
    const rg = spectrPadBandRow(renderGainsRef.current, N, NaN);''',
    ),
    (
        "response at the drawn count",
        '''  function drawMaskResponse(ctx, g) {
    const tg = targetGainsRef.current;
    const rg = renderGainsRef.current;''',
        '''  function drawMaskResponse(ctx, g) {
    const N = g.nd;
    const tg = spectrPadBandRow(targetGainsRef.current, N, 0);
    const rg = spectrPadBandRow(renderGainsRef.current, N, NaN);''',
    ),
    (
        "modulation extras only at the user's count",
        '''  function drawModulationExtras(ctx, g) {
    const look = modulationLookRef.current;''',
        '''  function drawModulationExtras(ctx, g) {
    const N = g.nd;
    const look = modulationLookRef.current;''',
    ),
    (
        "range overflow marks only drawn bands",
        '''  function drawRangeOverflow(ctx, g) {''',
        '''  function drawRangeOverflow(ctx, g) {
    const N = Math.min(g.nd, g.nUser);''',
    ),
    (
        "a press on a neutral slot edits nothing",
        '''    return clamp(i, 0, N - 1);''',
        '''    return g.nd > N && i >= N ? -1 : clamp(i, 0, N - 1);''',
    ),
    (
        "draw loop reachable from the freeze display",
        '''  const wakeDraw = () => {
    idleFramesRef.current = 0;
    if (rafRef.current || !drawRef.current) return;
    rafRef.current = requestAnimationFrame(drawRef.current);
  };
''',
        '''  const wakeDraw = () => {
    idleFramesRef.current = 0;
    if (rafRef.current || !drawRef.current) return;
    rafRef.current = requestAnimationFrame(drawRef.current);
  };
  // The Bands target re-lays the plot (spectrDrawnBandCount) without moving
  // React state; the freeze display wakes the loop when the count changes.
  globalThis.__spectrPlotWakeDraw = wakeDraw;
''',
    ),
    (
        "band count change redraws the plot and paints the label",
        '''      store.display = shown;
      if (typeof store.paint === "function") store.paint();
      if (was.lengthIndex !== shown.lengthIndex || was.bands !== shown.bands
          || was.presetDriven !== shown.presetDriven || was.presetName !== shown.presetName)
        spectrFreezeNotify(store);''',
        '''      store.display = shown;
      // A band count the Bands target moves changes several times a second:
      // it repaints the plot and the BANDS label, and renders nothing. A
      // notify here re-rendered the whole toolbar per step -- a React commit
      // that re-applies the captured document, ~30 ms each.
      if (was.bands !== shown.bands) {
        if (typeof globalThis.__spectrPlotWakeDraw === "function") globalThis.__spectrPlotWakeDraw();
        if (typeof globalThis.__spectrBandsLabelPaint === "function") globalThis.__spectrBandsLabelPaint();
      }
      if (typeof store.paint === "function") store.paint();
      if (was.lengthIndex !== shown.lengthIndex
          || was.presetDriven !== shown.presetDriven || was.presetName !== shown.presetName)
        spectrFreezeNotify(store);''',
    ),
    (
        "BANDS label painted on a count change",
        '''function SpectrBandsLabel({ count }) {
  const shown = useSpectrModulatedDisplay();
  const driven = shown.bands > 0;''',
        '''function SpectrBandsLabel({ count }) {
  const shown = useSpectrModulatedDisplay();
  const driven = shown.bands > 0;
  // The count the Bands target plays, painted straight onto the span when it
  // moves (freeze_display); a render reads the same store, so the two agree.
  const labelRef = React.useRef(null);
  const countRef = React.useRef(count);
  countRef.current = count;
  React.useEffect(() => {
    const paint = () => {
      const el = labelRef.current;
      if (!el) return;
      const d = spectrFreezeStore().display || {};
      const on = d.bands > 0;
      const n = on ? d.bands : countRef.current;
      el.textContent = n + " BANDS \u25be";
      el.setAttribute("data-spectr-bands-shown", String(n));
      el.setAttribute("data-spectr-bands-modulated", on ? "1" : "");
      el.style.color = on ? "rgb(205,180,255)" : "rgba(255,255,255,0.7)";
    };
    globalThis.__spectrBandsLabelPaint = paint;
    return () => { if (globalThis.__spectrBandsLabelPaint === paint) globalThis.__spectrBandsLabelPaint = null; };
  }, []);''',
    ),
    (
        "BANDS label carries the ref",
        '''  return React.createElement("span", {
    className: "tnum", "data-spectr-bands-shown": String(driven ? shown.bands : count),''',
        '''  return React.createElement("span", {
    ref: labelRef,
    className: "tnum", "data-spectr-bands-shown": String(driven ? shown.bands : count),''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("modulated controls already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("modulated controls applied (%d edits)" % len(EDITS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
