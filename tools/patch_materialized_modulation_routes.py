#!/usr/bin/env python3
"""Per-LFO multi-target routing in the band menu, and the audible viewport.

Each LFO now drives any set of destinations (Bank, Snapshot A, Snapshot B,
Morph, Viewport position, Viewport zoom), each with its own Amount, through
host lanes 4020..4055 (see docs/modulation.md). This patch:

  * reads the per-LFO routing out of the native modulation payload
    (`modulation.routes`) into flat scalar keys -- `routeOn<lfo>_<t>` and
    `routeAmt<lfo>_<t>` -- so the hook's per-key pending merge and its
    "a projection that moves nothing must not re-render" check keep working
    (an array would compare unequal on every projection);
  * replaces the band menu's pick-one SHARED TARGET rows with LFO <n> TARGETS:
    for each destination a toggle row (the same switch the LFO 1 / LFO 2 rows
    use) and, directly under it, an Amount slider in the Rate/Depth style.
    The Amount row stays in place, dimmed and inert, while its destination is
    off, so toggling never shifts the menu. Each control writes its own lane
    through `param_edit` (toggle: one complete gesture) or a drag bracket
    (`gestureId`: begin on press, end on release);
  * keeps the editable plot on the USER's viewport and draws the AUDIBLE one
    (when a viewport destination moves it) as an overlay: a dashed bracket on
    the minimap and a thin bracket across the top of the plot. Paint-only --
    the modulation frame writes a ref and wakes the draw loop, no React
    render -- and pointer hit-testing never sees it, so the band under the
    pointer is always the one edited.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "function spectrReadModulationRoutes("

EDITS = [
    (
        "routes are read from the native payload",
        '''function spectrModulationFromNative(modulation) {''',
        '''function spectrModulationFromNative(modulation) {
  // Per-LFO routing as flat scalars: routeOn<lfo>_<t> / routeAmt<lfo>_<t>, t in
  // Bank, A, B, Morph, Viewport position, Viewport zoom. Absent from a payload
  // that does not carry it, so a stale writer leaves the keys alone. Nested so
  // it travels with this function wherever the function is lifted. See
  // tools/patch_materialized_modulation_routes.py.
  function spectrReadModulationRoutes(source, next) {
    const routes = source && source.routes;
    if (!Array.isArray(routes)) return;
    routes.slice(0, 2).forEach((route, index) => {
      if (!route || typeof route !== "object") return;
      const mask = Number(route.mask);
      const amounts = Array.isArray(route.amounts) ? route.amounts : [];
      for (let t = 0; t < 6; t++) {
        if (Number.isFinite(mask)) next["routeOn" + (index + 1) + "_" + t] = ((mask >> t) & 1) === 1;
        const amount = Number(amounts[t]);
        if (Number.isFinite(amount)) next["routeAmt" + (index + 1) + "_" + t] = Math.max(0, Math.min(1, amount));
      }
    });
  }
''',
    ),
    (
        "the reader runs on every native frame",
        '''    next.targetSelection = next.targetMask === 15 ? "all" : next.targetMask === 0 ? "none" : "custom";
  return next;
}''',
        '''    next.targetSelection = next.targetMask === 15 ? "all" : next.targetMask === 0 ? "none" : "custom";
  spectrReadModulationRoutes(modulation, next);
  return next;
}''',
    ),
    (
        "the destinations the menu lists",
        '''  const spectrModulationShapes = ["Sin", "Tri", "Square", "Saw"];''',
        '''  const spectrModulationShapes = ["Sin", "Tri", "Square", "Saw"];
  // [enum index, action key, label], in ModulationTarget order.
  const spectrModulationRouteTargets = [
    [0, "bank", "Bank"], [1, "a", "Snapshot A"], [2, "b", "Snapshot B"],
    [3, "morph", "Morph"], [4, "viewport-position", "Viewport position"],
    [5, "viewport-zoom", "Viewport zoom"]];''',
    ),
    (
        "per-LFO toggles and Amount rows replace the pick-one rows",
        '''        React.createElement(Divider, { label: "SHARED TARGET" }),
        [[0, "bank", "Bank"], [1, "a", "Snapshot A"], [2, "b", "Snapshot B"], [3, "morph", "Morph"]].map(([target, key, label]) => Item({
          key, action: "modulation-target-" + key, label,
          checked: modulation.targetMask === (1 << target),
          sub: modulation.targetMask === (1 << target) ? "Active" : undefined,
          disabled: !modulationReady,
          keepOpen: true,
          onClick: () => publishModulation("target", 4004, target)
        }))
      );''',
        '''        React.createElement(Divider, { label: "LFO " + modulationSource + " TARGETS" }),
        // Each destination is two rows: its switch, then its Amount. The
        // Amount row is always there -- dimmed and inert while the switch is
        // off -- so toggling never moves the rows under the pointer. Effective
        // modulation = LFO Depth x Amount.
        spectrModulationRouteTargets.flatMap(([target, key, label]) => {
          const lfo = modulationSource;
          const on = modulation["routeOn" + lfo + "_" + target] === true;
          const stored = modulation["routeAmt" + lfo + "_" + target];
          const amount = Number.isFinite(stored) ? stored : 1;
          const lane = (lfo - 1) * 20 + target;
          return [
            Item({
              key, action: "modulation-target-" + key, label,
              sub: on ? "On" : "Off", toggle: on, checked: on,
              disabled: !modulationReady, keepOpen: true,
              onClick: () => publishModulation("routeOn" + lfo + "_" + target, 4020 + lane, !on)
            }),
            SliderRow({
              key: key + "-amount", action: "modulation-amount-" + key, label: "Amount",
              value: amount, min: 0, max: 1, step: 0.01,
              fmt: (v) => Math.round(v * 100) + "%",
              disabled: !modulationReady || !on,
              gestureId: 4030 + lane,
              onChange: (v) => publishModulation("routeAmt" + lfo + "_" + target, 4030 + lane,
                Math.round(v * 100) / 100)
            })
          ];
        })
      );''',
    ),
    (
        "the submenu estimate covers the target rows",
        '''const modulationTop = submenuTopFor(modulationH, 420, entryOffsets.modulation);''',
        '''const modulationTop = submenuTopFor(modulationH, 620, entryOffsets.modulation);''',
    ),
    (
        "the audible viewport is a paint ref",
        '''  const modTargetRef = useRef(null);
''',
        '''  const modTargetRef = useRef(null);
  // The AUDIBLE window while a viewport destination moves it ({lmin, lmax},
  // log10 Hz), else null. Drawn as an overlay; never the plot's own view, so
  // every pointer hit stays on the window the user set.
  const modViewportRef = useRef(null);
''',
    ),
    (
        "a released overlay drops the audible window",
        '''        if (!state.active) {
          if (!modulationActiveRef.current) return false;
          modulationActiveRef.current = false;
          modTargetRef.current = null;
''',
        '''        if (!state.active) {
          modViewportRef.current = null;
          if (!modulationActiveRef.current) return false;
          modulationActiveRef.current = false;
          modTargetRef.current = null;
''',
    ),
    (
        "a running frame carries the audible window",
        '''        const wasModulating = modulationActiveRef.current;
        modulationActiveRef.current = true;
''',
        '''        const wasModulating = modulationActiveRef.current;
        modulationActiveRef.current = true;
        modViewportRef.current = state.viewport || null;
''',
    ),
    (
        "the frame parser reads the audible window",
        '''    gains: gainDb.map((db, index) => muted[index] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
  };
}''',
        '''    gains: gainDb.map((db, index) => muted[index] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
    viewport: Number(payload.min_hz) > 0 && Number(payload.max_hz) > Number(payload.min_hz)
      ? { lmin: Math.log10(Number(payload.min_hz)), lmax: Math.log10(Number(payload.max_hz)) }
      : null,
  };
}''',
    ),
    (
        "the minimap brackets the audible window",
        '''    drawHandle(wx2, mmHover === "right");
''',
        '''    drawHandle(wx2, mmHover === "right");
    const audible = modViewportRef.current;
    if (audible) {
      const ax1 = mx + (audible.lmin - fullMin) / fullSpan * mw;
      const ax2 = mx + (audible.lmax - fullMin) / fullSpan * mw;
      ctx.save();
      ctx.strokeStyle = "rgba(255,214,140,0.9)";
      ctx.setLineDash([3, 2]);
      ctx.lineWidth = 1;
      ctx.strokeRect(ax1 + 0.5, my - 3.5, Math.max(1, ax2 - ax1 - 1), mh + 7);
      ctx.setLineDash([]);
      ctx.restore();
    }
''',
    ),
    (
        "the plot shows where the audible window lies",
        '''    drawEdgeWalls(octx, g);
''',
        '''    drawEdgeWalls(octx, g);
    drawAudibleViewport(octx, g);
''',
    ),
    (
        "the plot overlay itself",
        '''  function drawEdgeWalls(ctx, g) {''',
        '''  // A thin bracket along the top of the plot spanning the AUDIBLE window,
  // mapped into the window the user set (which the bands stay drawn in), with
  // an arrow where it runs off either side. Overlay only; hit-testing never
  // reads it.
  function drawAudibleViewport(ctx, g) {
    const audible = modViewportRef.current;
    if (!audible) return;
    const { inner } = g;
    const span = view.lmax - view.lmin;
    if (!(span > 0)) return;
    const toX = (l) => inner.x + (l - view.lmin) / span * inner.w;
    const x1 = Math.max(inner.x, toX(audible.lmin));
    const x2 = Math.min(inner.x + inner.w, toX(audible.lmax));
    const y = inner.y + 3;
    ctx.save();
    ctx.strokeStyle = "rgba(255,214,140,0.75)";
    ctx.fillStyle = "rgba(255,214,140,0.06)";
    ctx.lineWidth = 1.5;
    if (x2 > x1) {
      ctx.fillRect(x1, inner.y, x2 - x1, inner.h);
      ctx.beginPath();
      ctx.moveTo(x1, y + 6);
      ctx.lineTo(x1, y);
      ctx.lineTo(x2, y);
      ctx.lineTo(x2, y + 6);
      ctx.stroke();
    }
    const arrow = (x, dir) => {
      ctx.beginPath();
      ctx.moveTo(x, y - 3);
      ctx.lineTo(x + dir * 6, y);
      ctx.lineTo(x, y + 3);
      ctx.stroke();
    };
    if (toX(audible.lmin) < inner.x) arrow(inner.x + 7, -1);
    if (toX(audible.lmax) > inner.x + inner.w) arrow(inner.x + inner.w - 7, 1);
    ctx.restore();
  }
  function drawEdgeWalls(ctx, g) {''',
    ),
    (
        "Settings says what the legacy target controls now do",
        '''label: "Target", hint: "Automatable; clears Destinations" }''',
        '''label: "Target", hint: "Both LFOs; one field target" }''',
    ),
    (
        "Settings destinations hint",
        '''label: "Destinations", hint: "Both LFOs; overrides Target" }''',
        '''label: "Destinations", hint: "Both LFOs; per-LFO in band menu" }''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("modulation routes already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("modulation routes applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
