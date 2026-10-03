#!/usr/bin/env python3
"""Intensity, Mix and Output as LFO targets, and the scrolling target list.

Applied after the gain-control patches (patch_materialized_level_controls.py,
_display_setting.py, _range.py, _header_tooltips.py) and the routing patches
(patch_materialized_modulation_routes.py, _settings_targets.py,
_freeze_override.py). This patch:

  * adds the three level destinations (ModulationTarget 8..10) to the one
    target list, in the planned order -- Bank, Band shift, Band spread,
    Intensity, Mix, Morph, Freeze, Length, Output, Snapshot A, Snapshot B --
    so the band menu and Settings both show them;
  * computes every lane id as `(t >= 8 ? 4052 : 4020) + 20 (lfo - 1) + t`
    (Depth + 10): the level destinations live in their own block (4060.. /
    4070..; docs/parameter-surface.md). Inline at each site rather than a
    helper, because the tests lift these components into sandboxes alone;
  * reads all eleven bits of the native route mask;
  * makes the MIX, INTENSITY and OUTPUT knobs ask before overriding an LFO
    that drives them (`spectrOverrideModulated`, the same question LENGTH and
    LIVE/FROZEN ask). The question opens on the RELEASE of the press that
    grabbed the knob, as it does for a button, so the press and release stay
    on one view. "Keep modulating" lets the knob turn from then on without
    asking again until the LFOs driving it change; "Turn off" switches the
    target off. The knob always shows its own (base) value; while an LFO
    drives it, its track ring is tinted violet (an existing path recoloured,
    no new node);
  * replaces the Modulation submenu's single column with a fixed head (Back,
    the LFO switches, EDIT LFO, Shape, Rate and the sticky "LFO n TARGETS"
    heading) over a clipping viewport whose rows move by a negative margin --
    the help guide's scroller, because Pulp does not scroll an overflow
    container. Wheel and trackpad deltas move it directly (no re-render per
    event), a 4pt Spectr scrollbar shows the position, toggling a target keeps
    the offset, a keyboard move scrolls the cursor's row into view, and the
    offset starts at the top each time the submenu opens;
  * trims a menu slider track's vertical hit slop from 8 to 6pt: at 8 it
    reached 1.5pt up into the switch row above, so a press on the bottom edge
    of "Bank" moved Bank's Depth instead of switching Bank (found by the
    all-controls sweep once it reached this submenu).

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"


def escaped(value):
    # ensure_ascii=False: the artifact stores non-ASCII literally (see
    # tools/git/merge_materialized_runtime.py), so must every edit.
    return json.dumps(value, ensure_ascii=False)[1:-1]


HELPERS = r'''// The LFOs (1, 2) driving `target` in a normalized modulation frame: on, and
// with that target switched on.
function spectrLfosDrivingIn(frame, target) {
  const m = frame || {};
  return [1, 2].filter((lfo) => (lfo === 1 ? m.enabled : m.lfo2Enabled) === true
    && m["routeOn" + lfo + "_" + target] === true);
}
function spectrLfosDriving(target) {
  return spectrLfosDrivingIn(globalThis.__spectrModulationLast, target);
}
// A knob's view of which LFOs drive it: a key ("", "1", "2", "1,2") that
// changes only when that set does, so a modulation frame that leaves it alone
// re-renders nothing. Listens to the frames the App already receives; it
// never asks the processor for a hydration of its own.
function useSpectrLfosDriving(target) {
  const enabled = typeof target === "number";
  const [key, setKey] = React.useState(() => enabled ? spectrLfosDriving(target).join(",") : "");
  React.useEffect(() => {
    if (!enabled || !window.pulp || typeof window.pulp.on !== "function") return undefined;
    const accept = (message) => {
      const body = message && message.payload ? message.payload : message;
      const frame = spectrModulationFromNative(body && body.modulation);
      if (!frame) return;
      const next = spectrLfosDrivingIn(frame, target).join(",");
      setKey((current) => current === next ? current : next);
    };
    const offHydrate = window.pulp.on("processing_state_hydrate", accept);
    const offLive = window.pulp.on("processing_state_live", accept);
    return () => {
      if (typeof offHydrate === "function") offHydrate();
      if (typeof offLive === "function") offLive();
    };
  }, [enabled, target]);
  return key;
}
'''

ROUTE_ROWS_OLD = r'''        React.createElement(Divider, { label: "LFO " + modulationSource + " TARGETS" }),
        // Each destination is two rows: its switch, then its Depth. The
        // Depth row is always there -- dimmed and inert while the switch is
        // off -- so toggling never moves the rows under the pointer. There is
        // no LFO-level depth: effective modulation = wave x this Depth.
        spectrModulationRouteTargets.flatMap(([target, key, label]) => {
          const lfo = modulationSource;
          const on = modulation["routeOn" + lfo + "_" + target] === true;
          const stored = modulation["routeAmt" + lfo + "_" + target];
          const amount = Number.isFinite(stored) ? stored : 0.5;
          const lane = (lfo - 1) * 20 + target;
          return [
            Item({
              key, action: "modulation-target-" + key, label,
              sub: on ? "On" : "Off", toggle: on, checked: on,
              disabled: !modulationReady, keepOpen: true,
              onClick: () => publishModulation("routeOn" + lfo + "_" + target, 4020 + lane, !on)
            }),
            SliderRow({
              key: key + "-depth", action: "modulation-target-depth-" + key, label: "Depth",
              value: amount, min: 0, max: 1, step: 0.01,
              fmt: (v) => Math.round(v * 100) + "%",
              disabled: !modulationReady || !on,
              gestureId: 4030 + lane,
              onChange: (v) => publishModulation("routeAmt" + lfo + "_" + target, 4030 + lane,
                Math.round(v * 100) / 100)
            })
          ];
        })
      );'''

ROUTE_ROWS_NEW = r'''        React.createElement(Divider, { label: "LFO " + modulationSource + " TARGETS" })),
      // The target rows scroll inside a clipping viewport under the fixed
      // head above (the help guide's scroller: Pulp does not scroll an
      // overflow container, and a viewport that clips at a numeric height
      // also keeps a scrolled-away row from taking a press).
      React.createElement("div", {
        "data-spectr-modulation-viewport": true,
        onWheel: modulationWheel,
        style: { position: "relative", height: modulationViewportH, flexShrink: 0,
                 overflow: "hidden" }
      },
        React.createElement("div", {
          ref: modulationRowsRef,
          "data-spectr-modulation-rows": true,
          "data-spectr-modulation-offset": String(modulationOffsetRef.current),
          "data-spectr-modulation-viewport-height": String(modulationViewportH),
          style: { marginTop: -modulationOffsetRef.current, flexShrink: 0,
                   display: "flex", flexDirection: "column" }
        },
        // Each destination is two rows: its switch, then its Depth. The
        // Depth row is always there -- dimmed and inert while the switch is
        // off -- so toggling never moves the rows under the pointer. There is
        // no LFO-level depth: effective modulation = wave x this Depth.
        spectrModulationRouteTargets.flatMap(([target, key, label]) => {
          const lfo = modulationSource;
          const on = modulation["routeOn" + lfo + "_" + target] === true;
          const stored = modulation["routeAmt" + lfo + "_" + target];
          const amount = Number.isFinite(stored) ? stored : 0.5;
          // Host lanes: targets 0..7 at 4020 + 20 (lfo - 1) + target, the level
          // targets (Intensity 8, Mix 9, Output 10) in their own block at
          // 4060 + 20 (lfo - 1) + (target - 8); Depth is always on/off + 10.
          // Inline, not a helper: this component is lifted whole into the
          // tests' sandboxes, and a free helper would not travel with it.
          const onLane = (target >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + target;
          const depthLane = onLane + 10;
          return [
            Item({
              key, action: "modulation-target-" + key, label,
              sub: on ? "On" : "Off", toggle: on, checked: on,
              disabled: !modulationReady, keepOpen: true,
              onClick: () => publishModulation("routeOn" + lfo + "_" + target, onLane, !on)
            }),
            SliderRow({
              key: key + "-depth", action: "modulation-target-depth-" + key, label: "Depth",
              value: amount, min: 0, max: 1, step: 0.01,
              fmt: (v) => Math.round(v * 100) + "%",
              disabled: !modulationReady || !on,
              gestureId: depthLane,
              onChange: (v) => publishModulation("routeAmt" + lfo + "_" + target, depthLane,
                Math.round(v * 100) / 100)
            })
          ];
        })),
        modulationScrolls && React.createElement("div", {
          "data-spectr-modulation-scrollbar": true,
          "aria-hidden": true,
          style: { position: "absolute", top: 2, right: 3, width: 4,
                   height: modulationViewportH - 4, borderRadius: 2,
                   background: "rgba(255,255,255,0.06)", pointerEvents: "none" }
        }, React.createElement("div", {
          ref: modulationThumbRef,
          "data-spectr-modulation-scrollbar-thumb": true,
          style: { position: "absolute", left: 0, width: 4, borderRadius: 2,
                   top: modulationThumbTop(modulationOffsetRef.current),
                   height: modulationThumbH,
                   background: "rgba(150,200,255,0.45)", pointerEvents: "none" }
        })))
      );'''

SCROLLER_STATE = r'''  // ── The Modulation submenu's target scroller ──
  // A fixed head (through the sticky "LFO n TARGETS" heading) over a viewport
  // that clips the target rows at a numeric height; the rows move by a
  // negative margin. The offset lives in a ref and the wheel moves the nodes
  // directly, so a trackpad scroll is smooth and re-renders nothing; a render
  // (a toggle, an LFO switch) reads the same ref, so it keeps the offset.
  // See tools/patch_materialized_modulation_level_targets.py.
  const modulationHeadRef = React.useRef(null);
  const modulationRowsRef = React.useRef(null);
  const modulationThumbRef = React.useRef(null);
  const modulationOffsetRef = React.useRef(0);
  const [modulationMeasure, setModulationMeasure] = React.useState({ head: null, rows: null });
  // A render after a keyboard reveal: the wheel's direct node writes are
  // laid out by the pointer dispatch that carried them, a key's are not.
  const [, setModulationRevealTick] = React.useState(0);
  // Until measured: the head's rows (Back, divider, two LFO rows, the EDIT
  // LFO tabs, divider, Shape, Rate, the TARGETS heading) and two rows per
  // target, at their usual pitch.
  const modulationHeadH = modulationMeasure.head !== null ? modulationMeasure.head : 264;
  const modulationRowsH = modulationMeasure.rows !== null ? modulationMeasure.rows
    : spectrModulationRouteTargets.length * 63;
  // The panel stays clear of the 44 pt top bar: a submenu pushed up over it
  // loses its Back row, because a press there resolves to the bar in the
  // root hit test. Then the panel's 6px padding top and bottom and its 1px
  // border.
  const modulationPanelMax = Math.max(120, menuMaxHeight - 44);
  const modulationViewportH = Math.max(60, Math.min(modulationRowsH,
    modulationPanelMax - modulationHeadH - 14));
  const modulationMaxOffset = Math.max(0, modulationRowsH - modulationViewportH);
  const modulationScrolls = modulationMaxOffset > 0.5;
  const modulationThumbH = Math.max(24, Math.round((modulationViewportH - 4)
    * modulationViewportH / Math.max(1, modulationRowsH)));
  // The geometry of the LATEST render, for handlers a listener captured in
  // an earlier one (the menu's keydown handler is re-bound only when a
  // submenu opens or closes, before the panel has been measured).
  const modulationGeomRef = React.useRef(null);
  modulationGeomRef.current = { viewportH: modulationViewportH, maxOffset: modulationMaxOffset,
                                thumbH: modulationThumbH, scrolls: modulationScrolls };
  const modulationThumbTop = (offset) => {
    const g = modulationGeomRef.current;
    return Math.round((g.maxOffset > 0 ? offset / g.maxOffset : 0)
      * (g.viewportH - 4 - g.thumbH));
  };
  const modulationScrollTo = (next) => {
    const offset = Math.max(0, Math.min(modulationGeomRef.current.maxOffset, Math.round(next)));
    if (offset === modulationOffsetRef.current) return;
    modulationOffsetRef.current = offset;
    const rows = modulationRowsRef.current;
    if (rows && rows.style) rows.style.marginTop = -offset;
    if (rows && typeof rows.setAttribute === "function")
      rows.setAttribute("data-spectr-modulation-offset", String(offset));
    const thumb = modulationThumbRef.current;
    if (thumb && thumb.style) thumb.style.top = modulationThumbTop(offset);
  };
  const modulationWheel = (event) => {
    const delta = event && typeof event.deltaY === "number" ? event.deltaY : 0;
    if (!delta || !modulationGeomRef.current.scrolls) return;
    if (typeof event.preventDefault === "function") event.preventDefault();
    if (typeof event.stopPropagation === "function") event.stopPropagation();
    modulationScrollTo(modulationOffsetRef.current + delta);
  };
  // Bring the target row `node` fully into the viewport (a keyboard move onto
  // a row the viewport has scrolled away). Measured against the rows
  // container, whose box already carries the current offset.
  const modulationReveal = (node) => {
    const rows = modulationRowsRef.current;
    if (!rows || !node || typeof node.getBoundingClientRect !== "function"
        || typeof rows.getBoundingClientRect !== "function") return;
    if (typeof rows.contains === "function" && !rows.contains(node)) return;
    const r = node.getBoundingClientRect(), box = rows.getBoundingClientRect();
    if (!r || !box || !(r.height > 0)) return;
    const top = r.top - box.top, bottom = top + r.height;
    const offset = modulationOffsetRef.current;
    const viewportH = modulationGeomRef.current.viewportH;
    const before = offset;
    if (top < offset) modulationScrollTo(top);
    else if (bottom > offset + viewportH) modulationScrollTo(bottom - viewportH);
    if (modulationOffsetRef.current !== before) setModulationRevealTick((n) => n + 1);
  };
  React.useLayoutEffect(() => {
    const head = modulationHeadRef.current, rows = modulationRowsRef.current;
    const headH = head ? head.offsetHeight || head.clientHeight || 0 : 0;
    const rowsH = rows ? rows.offsetHeight || rows.clientHeight || 0 : 0;
    const next = { head: headH > 0 ? headH : modulationMeasure.head,
                   rows: rowsH > 0 ? rowsH : modulationMeasure.rows };
    if (next.head !== modulationMeasure.head || next.rows !== modulationMeasure.rows)
      setModulationMeasure(next);
    // A shorter list or a taller viewport can leave the offset past the end.
    if (modulationOffsetRef.current > modulationMaxOffset)
      modulationScrollTo(modulationMaxOffset);
  });
  // Each opening starts at the top.
  React.useEffect(() => {
    if (!modulationOpen) modulationOffsetRef.current = 0;
  }, [modulationOpen]);
'''

KNOB_LOGIC = r'''  const valueRef = React.useRef(value);
  valueRef.current = value;
  // An LFO driving this knob (modTarget: its ModulationTarget index). The
  // knob shows its own value; the ring is tinted while an LFO moves it, and
  // grabbing it asks first (spectrOverrideModulated). "Keep modulating"
  // lets it turn from then on, until the LFOs driving it change.
  // Guarded: a sandbox that lifts this component alone has no hook.
  const drivingKey = typeof useSpectrLfosDriving === "function"
    ? useSpectrLfosDriving(modTarget) : "";
  const modulated = drivingKey !== "";
  const acknowledgedRef = React.useRef("");
  const askPendingRef = React.useRef(false);
  const pressOpenRef = React.useRef(false);
  const overrideAsks = (then) => {
    if (typeof modTarget !== "number" || typeof spectrLfosDriving !== "function") return false;
    const lfos = spectrLfosDriving(modTarget);
    const key = lfos.join(",");
    if (!lfos.length) { acknowledgedRef.current = ""; return false; }
    if (acknowledgedRef.current === key || typeof spectrOverrideAsks !== "function"
        || !spectrOverrideAsks()) return false;
    spectrOverrideModulated(modName || label, modTarget, lfos, () => {
      acknowledgedRef.current = key;
      if (then) then();
    });
    return true;
  };
  const wouldAsk = () => {
    if (typeof modTarget !== "number" || typeof spectrLfosDriving !== "function"
        || typeof spectrOverrideAsks !== "function" || !spectrOverrideAsks()) return false;
    const key = spectrLfosDriving(modTarget).join(",");
    return key !== "" && acknowledgedRef.current !== key;
  };
'''

EDITS = [
    (
        "lane-id helpers and the knobs' modulation hook",
        "function spectrModulationFromNative(modulation) {\n",
        HELPERS + "function spectrModulationFromNative(modulation) {\n",
    ),
    (
        "Intensity, Mix and Output join the target list",
        '''// order names targets a build may not have yet (Intensity, Mix and Output
// arrive with the gain controls); those are skipped.
function spectrModulationRouteList() {
  const table = {
    bank: [0, "Bank"], "band-shift": [4, "Band shift"], "band-spread": [5, "Band spread"],
    morph: [3, "Morph"], freeze: [6, "Freeze"], length: [7, "Length"],
    a: [1, "Snapshot A"], b: [2, "Snapshot B"]};''',
        '''// order is the planned one; a key the table lacks is skipped.
function spectrModulationRouteList() {
  const table = {
    bank: [0, "Bank"], "band-shift": [4, "Band shift"], "band-spread": [5, "Band spread"],
    intensity: [8, "Intensity"], mix: [9, "Mix"],
    morph: [3, "Morph"], freeze: [6, "Freeze"], length: [7, "Length"],
    output: [10, "Output"], a: [1, "Snapshot A"], b: [2, "Snapshot B"]};''',
    ),
    (
        "all eleven route bits are read",
        '''      for (let t = 0; t < 8; t++) {
        if (Number.isFinite(mask)) next["routeOn" + (index + 1) + "_" + t] = ((mask >> t) & 1) === 1;''',
        '''      for (let t = 0; t < 11; t++) {
        if (Number.isFinite(mask)) next["routeOn" + (index + 1) + "_" + t] = ((mask >> t) & 1) === 1;''',
    ),
    (
        "the reader's comment names every target",
        '''  // Per-LFO routing as flat scalars: routeOn<lfo>_<t> / routeAmt<lfo>_<t>, t in
  // Bank, A, B, Morph, Band shift, Band spread. Absent from a payload''',
        '''  // Per-LFO routing as flat scalars: routeOn<lfo>_<t> / routeAmt<lfo>_<t>, t the
  // ModulationTarget index (Bank, A, B, Morph, Band shift, Band spread, Freeze,
  // Length, Intensity, Mix, Output). Absent from a payload''',
    ),
    (
        "Settings writes through the lane helper",
        '''  const lane = (t) => (lfo - 1) * 20 + t;''',
        '''  const lane = (t) => (t >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + t;  // level targets: own block''',
    ),
    (
        "Settings target switch lane",
        '''onChange: (next) => publish("routeOn" + lfo + "_" + t, 4020 + lane(t), next) }))));''',
        '''onChange: (next) => publish("routeOn" + lfo + "_" + t, lane(t), next) }))));''',
    ),
    (
        "Settings target Depth lane",
        '''gestureId: on ? 4030 + lane(t) : undefined, value: depth, min: 0, max: 1, step: 0.01, fmt: (v) => Math.round(v * 100) + "%", onChange: (next) => { if (on) publish("routeAmt" + lfo + "_" + t, 4030 + lane(t), Math.round(next * 100) / 100); } })));''',
        '''gestureId: on ? lane(t) + 10 : undefined, value: depth, min: 0, max: 1, step: 0.01, fmt: (v) => Math.round(v * 100) + "%", onChange: (next) => { if (on) publish("routeAmt" + lfo + "_" + t, lane(t) + 10, Math.round(next * 100) / 100); } })));''',
    ),
    (
        "Turn off writes the target's own lane",
        '''            { id: 4020 + (lfo - 1) * 20 + r.target, value: 0 },''',
        '''            { id: (r.target >= 8 ? 4052 : 4020) + (lfo - 1) * 20 + r.target, value: 0 },''',
    ),
    (
        "the override comment names the lane helper",
        '''// ModulationTarget index (its lanes are 4020 + 20 (lfo - 1) + target), and''',
        '''// ModulationTarget index (its on/off lane is 4020 + 20 (lfo - 1) + target,
// 4060 + 20 (lfo - 1) + (target - 8) for the level targets), and''',
    ),
    (
        "the knob takes a modulation target",
        '''                      readoutWidth, onEdit, after, tip }) {''',
        '''                      readoutWidth, onEdit, after, tip, modTarget, modName }) {''',
    ),
    (
        "the knob asks before overriding an LFO",
        '''  const valueRef = React.useRef(value);
  valueRef.current = value;
  const dragRef = React.useRef(null);
  const lastPressRef = React.useRef(0);''',
        KNOB_LOGIC + '''  const dragRef = React.useRef(null);
  const lastPressRef = React.useRef(0);''',
    ),
    (
        "a drag only closes the gesture it opened",
        '''  const endDrag = () => {
    dragRef.current = null;
    gesture(false);
  };
  const onKnobPointerDown = (e) => {
    if (globalThis.spectrHeaderTipHide) globalThis.spectrHeaderTipHide();''',
        '''  const endDrag = () => {
    dragRef.current = null;
    if (pressOpenRef.current) gesture(false);
    pressOpenRef.current = false;
    // The release of a press that met a modulated knob asks the question,
    // as a button's click would: press and release stay on this view.
    if (askPendingRef.current) {
      askPendingRef.current = false;
      overrideAsks(null);
    }
  };
  const onKnobPointerDown = (e) => {
    if (globalThis.spectrHeaderTipHide) globalThis.spectrHeaderTipHide();
    if (wouldAsk()) {
      askPendingRef.current = true;
      dragRef.current = null;
      return;
    }''',
    ),
    (
        "the press marks its gesture open",
        '''    gesture(true);
    const now = Date.now();
    if (now - lastPressRef.current <= DOUBLE_PRESS_MS) {''',
        '''    gesture(true);
    pressOpenRef.current = true;
    const now = Date.now();
    if (now - lastPressRef.current <= DOUBLE_PRESS_MS) {''',
    ),
    (
        "a wheel step asks first",
        '''    const q = fine ? (fineStep || step) : step;
    gesture(true);
    write(valueRef.current - Math.sign(dy) * Math.max(q, amount), fine);
    gesture(false);
  };''',
        '''    const q = fine ? (fineStep || step) : step;
    const apply = () => {
      gesture(true);
      write(valueRef.current - Math.sign(dy) * Math.max(q, amount), fine);
      gesture(false);
    };
    if (!overrideAsks(apply)) apply();
  };''',
    ),
    (
        "a key step asks first",
        '''    if (typeof e.stopPropagation === "function") e.stopPropagation();
    gesture(true);
    write(next, fine);
    gesture(false);
  };''',
        '''    if (typeof e.stopPropagation === "function") e.stopPropagation();
    const apply = () => {
      gesture(true);
      write(next, fine);
      gesture(false);
    };
    if (!overrideAsks(apply)) apply();
  };''',
    ),
    (
        "the knob says it is modulated",
        '''      "aria-valuetext": text,
      title,
      onPointerDown: onKnobPointerDown,''',
        '''      "aria-valuetext": text,
      "data-spectr-knob-modulated": modulated ? drivingKey : "",
      title,
      onPointerDown: onKnobPointerDown,''',
    ),
    (
        "the ring is tinted while an LFO moves the knob",
        '''      React.createElement("path", { d: spectrKnobArc(c, c, radius, -SWEEP, SWEEP),
        stroke: "rgba(255,255,255,0.14)", strokeWidth: 2, fill: "none",''',
        '''      React.createElement("path", { d: spectrKnobArc(c, c, radius, -SWEEP, SWEEP),
        stroke: modulated ? "rgba(190,150,255,0.55)" : "rgba(255,255,255,0.14)", strokeWidth: 2, fill: "none",''',
    ),
    (
        "MIX is the Mix target",
        '''      onEdit: (v) => writeLevelParam("mix", SPECTR_PARAM_MIX, v, setMix)
    }),''',
        '''      onEdit: (v) => writeLevelParam("mix", SPECTR_PARAM_MIX, v, setMix),
      modTarget: 9, modName: "Mix"
    }),''',
    ),
    (
        "INTENSITY is the Intensity target",
        '''      onEdit: (v) => writeLevelParam("intensity", SPECTR_PARAM_INTENSITY, v, setIntensity)
    }),''',
        '''      onEdit: (v) => writeLevelParam("intensity", SPECTR_PARAM_INTENSITY, v, setIntensity),
      modTarget: 8, modName: "Intensity"
    }),''',
    ),
    (
        "OUTPUT is the Output target",
        '''      onEdit: (v) => writeTrim(v),
      after: /* @__PURE__ */ React.createElement(SpectrAutoGainPill, {''',
        '''      onEdit: (v) => writeTrim(v),
      modTarget: 10, modName: "Output",
      after: /* @__PURE__ */ React.createElement(SpectrAutoGainPill, {''',
    ),
    (
        "the scroller's state lives with the submenu",
        '''  const modulationTop = submenuTopFor(modulationH, 760, entryOffsets.modulation);
''',
        '''  const modulationTop = submenuTopFor(modulationH, 760, entryOffsets.modulation);
''' + SCROLLER_STATE,
    ),
    (
        "the panel no longer caps itself",
        '''          backdropFilter: "blur(12px)", zIndex: 2147483002,
          maxHeight: menuMaxHeight, overflowY: "auto",
          overscrollBehavior: "contain", pointerEvents: "auto"
        }
      },
        Item({ action: "modulation-back", label: "‹ Back", keepOpen: true, onClick: () => closeSubmenuLevel("modulation") }),''',
        '''          backdropFilter: "blur(12px)", zIndex: 2147483002,
          pointerEvents: "auto"
        }
      },
      React.createElement("div", { ref: modulationHeadRef, "data-spectr-modulation-head": true,
        style: { display: "flex", flexDirection: "column", flexShrink: 0 } },
        Item({ action: "modulation-back", label: "‹ Back", keepOpen: true, onClick: () => closeSubmenuLevel("modulation") }),''',
    ),
    (
        "the target rows scroll under a sticky heading",
        ROUTE_ROWS_OLD,
        ROUTE_ROWS_NEW,
    ),
    (
        "a slider track's reach stays inside its own row",
        'cursor: disabled ? "default" : "pointer", hitSlop: "8 6" }',
        'cursor: disabled ? "default" : "pointer", hitSlop: "6 6" }',
    ),
    (
        "a keyboard move reveals its row",
        '''          : key === "ArrowDown" ? (index + 1) % n : (index - 1 + n) % n;
        moveCursor(level, next);
        return;''',
        '''          : key === "ArrowDown" ? (index + 1) % n : (index - 1 + n) % n;
        moveCursor(level, next);
        if (level === "modulation") modulationReveal(rows[next]);
        return;''',
    ),
]


def main():
    raw = PATH.read_text(encoding="utf-8")
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
    for token in ("function spectrLfosDrivingIn(", "function useSpectrLfosDriving(",
                  '"data-spectr-modulation-viewport": true', "const modulationScrollTo = ",
                  'output: [10, "Output"]'):
        if html.count(token) != 1:
            sys.exit("FAIL: %r appears %d times, expected 1" % (token, html.count(token)))
    for stale in ("4020 + lane", "4030 + lane", "4020 + (lfo - 1) * 20"):
        if stale in html:
            sys.exit("FAIL: stale lane arithmetic %r survives" % stale)
    if not changed:
        print("no change needed")
        return 0
    PATH.write_text(raw, encoding="utf-8")
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
