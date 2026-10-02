#!/usr/bin/env python3
"""Per-LFO multi-target routing in the band menu, and the audible viewport.

Each LFO now drives any set of destinations (Bank, Snapshot A, Snapshot B,
Morph, Band shift, Band spread), each with its own Amount, through
host lanes 4020..4055 (see docs/modulation.md). This patch:

  * reads the per-LFO routing out of the native modulation payload
    (`modulation.routes`) into flat scalar keys -- `routeOn<lfo>_<t>` and
    `routeAmt<lfo>_<t>` -- so the hook's per-key pending merge and its
    "a projection that moves nothing must not re-render" check keep working
    (an array would compare unequal on every projection);
  * replaces the band menu's pick-one SHARED TARGET rows with LFO <n> TARGETS:
    for each destination a toggle row (the same switch the LFO 1 / LFO 2 rows
    use) and, directly under it, a Depth slider in the Rate style. The Depth
    row stays in place, dimmed and inert, while its destination is off, so
    toggling never shifts the menu. Each control writes its own lane through
    `param_edit` (toggle: one complete gesture) or a drag bracket
    (`gestureId`: begin on press, end on release);
  * removes the LFO-level Depth row: each target carries its own depth, and
    the LFO Depth lanes are legacy commands (docs/modulation.md);
  * adds a test hook naming the band under a pointer, which the editor test
    uses to prove viewport modulation never moves the band being edited (the
    plot always shows the user's viewport; viewport modulation is audible
    only).

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

DONE_MARKER = 'action: "modulation-target-depth-" + key'

EDITS = [
    (
        "routes are read from the native payload",
        '''function spectrModulationFromNative(modulation) {''',
        '''function spectrModulationFromNative(modulation) {
  // Per-LFO routing as flat scalars: routeOn<lfo>_<t> / routeAmt<lfo>_<t>, t in
  // Bank, A, B, Morph, Band shift, Band spread. Absent from a payload
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
      for (let t = 0; t < 8; t++) {
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
  // Each target this build has: action key -> [ModulationTarget index,
  // label]. Displayed most-modulated first, so the common ones show without
  // scrolling. The order names targets a build may not have yet (Intensity,
  // Mix and Output arrive with the gain controls); those are skipped.
  const spectrModulationRouteTable = {
    bank: [0, "Bank"], "band-shift": [4, "Band shift"], "band-spread": [5, "Band spread"],
    morph: [3, "Morph"], freeze: [6, "Freeze"], length: [7, "Length"],
    a: [1, "Snapshot A"], b: [2, "Snapshot B"]};
  const spectrModulationRouteOrder = ["bank", "band-shift", "band-spread", "intensity",
    "mix", "morph", "freeze", "length", "output", "a", "b"];
  const spectrModulationRouteTargets = spectrModulationRouteOrder
    .filter((key) => spectrModulationRouteTable[key])
    .map((key) => [spectrModulationRouteTable[key][0], key, spectrModulationRouteTable[key][1]]);''',
    ),
    (
        "the LFO-level Depth row goes: each target carries its own",
        '''        })(),
        SliderRow({
          action: modulationSource === 1 ? "lfo1-depth" : "lfo2-depth", label: "Depth",
          value: modulationSource === 1 ? modulation.depth : modulation.lfo2Depth,
          min: 0, max: 1, step: 0.01,
          fmt: (v) => Math.round(v * 100) + "%",
          disabled: !modulationReady,
          gestureId: modulationSource === 1 ? 4003 : 4013,
          onChange: (v) => publishModulation(modulationSource === 1 ? "depth" : "lfo2Depth",
            modulationSource === 1 ? 4003 : 4013, Math.round(v * 100) / 100)
        }),
        React.createElement(Divider, { label: "SHARED TARGET" }),''',
        '''        })(),
        React.createElement(Divider, { label: "SHARED TARGET" }),''',
    ),
    (
        "per-LFO toggles and Depth rows replace the pick-one rows",
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
      );''',
    ),
    (
        "the submenu estimate covers the target rows",
        '''const modulationTop = submenuTopFor(modulationH, 420, entryOffsets.modulation);''',
        '''const modulationTop = submenuTopFor(modulationH, 760, entryOffsets.modulation);''',
    ),
    (
        "the test hook names the band under a pointer",
        '''    window.__spectrTestHooks.minimapHit = (x, y) => minimapHit(x, y, getGeom());
''',
        '''    window.__spectrTestHooks.minimapHit = (x, y) => minimapHit(x, y, getGeom());
    // The band a press at clientX lands on, through the same transform and
    // findBand the pointer handlers use.
    window.__spectrTestHooks.bandAtClientX = (clientX) => {
      const g = getGeom(), wrap = wrapRef.current;
      if (!g || !wrap) return null;
      const rect = wrap.getBoundingClientRect();
      return findBand((clientX - rect.left) * wrap.clientWidth / rect.width, g);
    };
''',
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
    applied = 0
    for name, old, new in EDITS:
        # Per edit, so a document patched by an earlier version of this script
        # takes only the edits it is missing.
        if encode(new) in raw:
            continue
        count = raw.count(encode(old))
        # An edit a LATER edit rewrote leaves neither its anchor nor its own
        # text behind; the finished target rows are the proof it ran.
        if count == 0 and encode(DONE_MARKER) in raw:
            continue
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
        applied += 1
    if applied == 0:
        print("modulation routes already applied")
        return 0
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("modulation routes applied (%d edits)" % applied)
    return 0


if __name__ == "__main__":
    sys.exit(main())
