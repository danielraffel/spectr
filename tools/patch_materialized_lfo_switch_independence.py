#!/usr/bin/env python3
"""An LFO's switch and its targets are independent controls, and the menus
say so.

THE CONFUSION
  A fresh instance routed Bank from both LFOs, so switching an LFO on moved
  every band at once: the switch read as "LFO on = Bank on". And a target
  switched on while its LFO was off showed a lit switch captioned "LFO off",
  which reads as the switch being off.

THE RULE
  The LFO switch turns the LFO on; a target's switch routes the LFO to it;
  neither writes the other. The native default now routes nothing (see
  src/param_surface.cpp). Each menu names the half that is missing:
  - the LFO 1 / LFO 2 switches read "On, no target" while that LFO drives
    nothing, so an LFO switched on in a fresh instance explains its silence;
  - a target switched on under an LFO that is off reads "On, LFO off" in the
    band menu's full list and in a control's own menu alike.

Idempotent raw-text surgery on the materialized document; re-running is a
no-op once applied.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DOC = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"

def encode(text):
    return json.dumps(text)[1:-1]

EDITS = [
    # 1. the shared head: an LFO on with nothing routed says so
    ("""  const { Item, ShapeRow, SliderRow, Divider } = kit;
  const modulationReady = ready, publishModulation = publish;
""",
     """  const { Item, ShapeRow, SliderRow, Divider } = kit;
  const modulationReady = ready, publishModulation = publish;
  // The switch is the LFO's own: it routes nothing. While nothing is routed
  // the caption says why the LFO is silent.
  const lfoRouted = (lfo) => Object.keys(modulation).some((key) =>
    key.startsWith("routeOn" + lfo + "_") && modulation[key] === true);
"""),
    ("""Item({ action: "lfo1-enable", label: "LFO 1", sub: modulation.enabled ? "On" : "Off",""",
     """Item({ action: "lfo1-enable", label: "LFO 1", sub: modulation.enabled ? (lfoRouted(1) ? "On" : "On, no target") : "Off","""),
    ("""Item({ action: "lfo2-enable", label: "LFO 2", sub: modulation.lfo2Enabled ? "On" : "Off",""",
     """Item({ action: "lfo2-enable", label: "LFO 2", sub: modulation.lfo2Enabled ? (lfoRouted(2) ? "On" : "On, no target") : "Off","""),
    # 2. the band menu's full target list: a target on under an LFO that is off
    ("""    const lfo = modulationSource;
    const on = modulation["routeOn" + lfo + "_" + target] === true;
""",
     """    const lfo = modulationSource;
    const on = modulation["routeOn" + lfo + "_" + target] === true;
    const lfoOn = lfo === 1 ? modulation.enabled === true : modulation.lfo2Enabled === true;
"""),
    ("""        sub: on ? "On" : "Off", toggle: on, checked: on,
        accent: !!modulationFocus && modulationFocus.target === target,""",
     """        sub: on ? (lfoOn ? "On" : "On, LFO off") : "Off", toggle: on, checked: on,
        accent: !!modulationFocus && modulationFocus.target === target,"""),
    # 3. a control's own menu
    ("""          sub: on ? (lfoOn ? "On" : "LFO off") : "Off",
          subTone: on && !lfoOn ? "rgb(255,205,150)" : void 0,""",
     """          sub: on ? (lfoOn ? "On" : "On, LFO off") : "Off",
          subTone: on && !lfoOn ? "rgb(255,205,150)" : void 0,"""),
]

def main():
    raw = DOC.read_text(encoding="utf-8")
    applied = 0
    for old, new in EDITS:
        o, n = encode(old), encode(new)
        if raw.count(n) == 1:
            continue
        if raw.count(o) != 1:
            print(f"lfo switch independence: anchor not unique ({raw.count(o)}): {old[:60]!r}", file=sys.stderr)
            return 1
        raw = raw.replace(o, n)
        applied += 1
    if applied:
        DOC.write_text(raw, encoding="utf-8")
        print(f"lfo switch independence: applied {applied} edit(s)")
    else:
        print("lfo switch independence: already applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
