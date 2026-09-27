#!/usr/bin/env python3
"""The LFO display holds still while bands are being drawn.

With a modulator running, every modulation frame rewrites the painted bars,
so a band being dragged or mute-brushed kept jumping away from the pointer:
the user could not see the shape they were drawing. A new setting, `Hold
while editing` (Settings > Modulation look, default ON), holds the DISPLAY
for the length of a drawing gesture:

  * the bars ease to the canonical shape under the pointer and follow it,
  * the per-look extras (trail, contour, glow, ribbon) stand down, and
  * macro offsets are drawn from the canonical path again, since the
    published modulated field that already carries them is not being shown.

Only the paint is held. The audio owner keeps modulating, host automation is
untouched, and the next frame after the pointer is released resumes the
modulated display -- the eased looks from where the bars are, not with a
jump. Viewport pans, marquees and minimap drags are not band edits and do
not hold.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const modulationHeld = "

EDITS = [
    (
        "default is on",
        '''  "modulationLook": "classic"
}''',
        '''  "modulationLook": "classic",
  "holdModulationWhileEditing": true
}''',
    ),
    (
        "held predicate",
        '''  modulationLookRef.current = (settings && settings.modulationLook) || "classic";''',
        '''  modulationLookRef.current = (settings && settings.modulationLook) || "classic";
  // A drawing gesture under a running LFO holds the modulation DISPLAY (the
  // audio keeps modulating) so the band under the pointer stays where it is
  // drawn. See tools/patch_materialized_modulation_hold_while_editing.py.
  const modulationHoldRef = useRef(true);
  modulationHoldRef.current = !(settings && settings.holdModulationWhileEditing === false);
  const modulationHeld = () => {
    if (!modulationActiveRef.current || !modulationHoldRef.current) return false;
    const pointer = pointerRef.current;
    return !!pointer && (pointer.mode === "gain" || pointer.mode === "mute-brush");
  };''',
    ),
    (
        "macros drawn canonically while held",
        '''  const macroAdjustedGain = (value, index) => {
    if (modulationActiveRef.current) return value;''',
        '''  const macroAdjustedGain = (value, index) => {
    if (modulationActiveRef.current && !modulationHeld()) return value;''',
    ),
    (
        "draw loop eases to the drawn shape while held",
        '''          if (!modulationActiveRef.current) {
            rg[i] = smooth(rg[i], target, dt * k);''',
        '''          if (!modulationActiveRef.current || modulationHeld()) {
            // Held: the eased look state rides along with the bars, so the
            // look resumes from where they are when the gesture ends.
            if (modulationActiveRef.current && modEasedRef.current.length === N) {
              modEasedRef.current[i] = Number.isFinite(rg[i]) ? rg[i] : 0;
              if (modVelRef.current.length === N) modVelRef.current[i] = 0;
            }
            rg[i] = smooth(rg[i], target, dt * k);''',
    ),
    (
        "look extras stand down while held",
        '''    if (look === "classic" || !modulationActiveRef.current) return;''',
        '''    if (look === "classic" || !modulationActiveRef.current || modulationHeld()) return;''',
    ),
    (
        "classic frames do not overwrite a held display",
        '''        if (modulationLookRef.current === "classic") {
          renderGainsRef.current = modulated;
        } else if''',
        '''        if (modulationLookRef.current === "classic") {
          if (!modulationHeld()) renderGainsRef.current = modulated;
        } else if''',
    ),
    (
        "settings toggle",
        '''      opts: window.SPECTR_MODULATION_LOOKS || [["classic", "Classic"], ["glide", "Glide"], ["float", "Float"], ["spring", "Spring"], ["calm", "Calm"], ["trail", "Trail"], ["contour", "Contour"], ["glow", "Glow"], ["ribbon", "Ribbon"], ["hybrid", "Hybrid"]]
    }
  )),''',
        '''      opts: window.SPECTR_MODULATION_LOOKS || [["classic", "Classic"], ["glide", "Glide"], ["float", "Float"], ["spring", "Spring"], ["calm", "Calm"], ["trail", "Trail"], ["contour", "Contour"], ["glow", "Glow"], ["ribbon", "Ribbon"], ["hybrid", "Hybrid"]]
    }
  )), /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Hold while editing", hint: "Bands stop moving while you draw them. The sound keeps modulating" }, /* @__PURE__ */ React.createElement("div", { "data-spectr-hold-edit": settings.holdModulationWhileEditing !== false ? "on" : "off" }, /* @__PURE__ */ React.createElement(
    SpectrSettingsToggle,
    {
      value: settings.holdModulationWhileEditing !== false,
      onChange: (v) => persist({ holdModulationWhileEditing: v })
    }
  ))),''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("modulation hold while editing already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("modulation hold while editing applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
