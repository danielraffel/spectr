#!/usr/bin/env python3
"""Header tooltips: one properly sized panel, and a Setting to turn them off.

THE DEFECT

  Hovering FROZEN or INTENSITY showed a small dark square at the left with the
  text spilling out of it. The tip is an absolutely positioned box with no
  width around a one-line span. The runtime stamps `white-space: normal` on
  every span and the SDK did not inherit `nowrap` from the box, so the span
  soft-wrapped to nothing and the box laid out at its padding and border alone
  (20 x 27 design px) while the text painted at its natural width
  (Generous-Corp/pulp#9317 makes `nowrap` reach descendant text). It also sat
  at a fixed 32 px, over the lower edge of the control.

THE FIX

  * The panel is sized from its text: JetBrains Mono at 10 px with its 0.3 px
    letter spacing measures 6.5 px a glyph here (the freeze chord's modifier
    symbols included), plus 12 px of padding a side. One rounded
    panel, 9 px side padding, mono text.
  * Placed 6 px below the control it describes, centred on it, and clamped
    inside the editor so it never runs off either side.
  * Settings > FEEDBACK > "Show tooltips" (default on). Saved with the
    session in the plugin state, like "Keyboard shortcuts in DAW": each
    project keeps its own choice, a new instance shows them.

The box shrink-fits its text by itself (pulp#9317, Pulp SDK 0.901.0: the
box's `nowrap` reaches the span), so the panel carries no explicit width; the
estimate `w` only centres it and clamps it inside the editor.

Idempotent like the other patch_materialized_* scripts. Run after
tools/patch_materialized_header_tooltips.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrTooltipPanel"


def escaped(value):
    return json.dumps(value, ensure_ascii=False)[1:-1]


EDITS = [
    ('the editor caches the Show tooltips preference with the keyboard policy',
     "    if (typeof policy.shortcuts_in_daw === 'boolean')\n"
     "      keyboard.inDaw = policy.shortcuts_in_daw;\n",
     "    if (typeof policy.shortcuts_in_daw === 'boolean')\n"
     "      keyboard.inDaw = policy.shortcuts_in_daw;\n"
     "    // Show tooltips (__spectrTooltipPanel): the processor's copy, saved\n"
     "    // with the session; absent means on.\n"
     "    if (typeof policy.show_tooltips === 'boolean')\n"
     "      globalThis.__spectrShowTooltips = policy.show_tooltips;\n"),
    ('a tip is sized, placed below its control and clamped to the editor',
     '      const x = Math.max(0, box.left - origin.left);\n',
     '      // __spectrTooltipPanel: sized from the text (JetBrains Mono 10 px is\n'
     '      // measured 6.5 px a glyph with its 0.3 px letter\n'
     '      // spacing), centred under the control 6 px below it, clamped inside the\n'
     '      // editor. `w` only centres and clamps the panel: the box shrink-fits\n'
     '      // its text itself.\n'
     '      if (globalThis.__spectrShowTooltips === false) return;\n'
     '      const glyphs = Array.from(String(text)).length;\n'
     '      const w = Math.ceil(glyphs * 6.5) + 24;\n'
     '      // The editor\'s width: the outermost laid-out ancestor of the cluster\n'
     '      // (window.innerWidth is a stale 800 in this runtime).\n'
     '      let outer = cluster;\n'
     '      while (outer && outer.parentElement && outer.parentElement.offsetWidth > 0)\n'
     '        outer = outer.parentElement;\n'
     '      const editorW = (outer && outer.offsetWidth) || 1320;\n'
     '      const centre = box.left + box.width / 2;\n'
     '      const left = Math.max(8, Math.min(editorW - 8 - w, centre - w / 2));\n'
     '      const x = left - origin.left;\n'
     '      const y = box.bottom - origin.top + 6;\n'),
    ('AUTO is off for new instances, and its tooltip says when to use it',
     'globalThis.spectrHeaderTip("Auto Gain: keeps the level steady as you boost or cut.", "[data-spectr-auto-gain]")',
     'globalThis.spectrHeaderTip("Auto Gain \u2014 keeps the volume steady as you boost or cut.", "[data-spectr-auto-gain]")'),
    ('the tip state carries its box',
     '        setTip({ text, x });\n',
     '        setTip({ text, x, y, w });\n'),
    ('the panel is one sized, rounded box under the control',
     '        position: "absolute", left: tip.x, top: 32, zIndex: 60,\n'
     '        pointerEvents: "none", whiteSpace: "nowrap",\n',
     '        position: "absolute", left: tip.x, top: tip.y === undefined ? 32 : tip.y, zIndex: 60,\n'
     '        height: 26, display: "flex", alignItems: "center",\n'
     '        pointerEvents: "none", whiteSpace: "nowrap",\n'),
]

SETTING_OLD = ('/* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Status info", '
               'hint: "Hover, mute, and drag feedback" }')
SETTING_NEW = ('React.createElement(SpectrSettingsField, { label: "Show tooltips", hint: '
               '"Hovering a header control says what it does" }, React.createElement(SpectrTooltipSetting, null)), '
               + SETTING_OLD)

COMPONENT_ANCHOR = 'function SpectrSettingsCloseButton({ onClose }) {\n'
COMPONENT = r'''// Settings > FEEDBACK > Show tooltips (__spectrTooltipPanel). The processor
// owns and saves the value with the session; this is the switch.
function SpectrTooltipSetting() {
  const [on, setOn] = React.useState(globalThis.__spectrShowTooltips !== false);
  React.useEffect(() => {
    const keyboard = globalThis.__spectrKeyboard;
    if (!keyboard || !Array.isArray(keyboard.listeners)) return undefined;
    const sync = () => setOn(globalThis.__spectrShowTooltips !== false);
    keyboard.listeners.push(sync);
    return () => {
      const at = keyboard.listeners.indexOf(sync);
      if (at >= 0) keyboard.listeners.splice(at, 1);
    };
  }, []);
  return React.createElement("div", { "data-spectr-show-tooltips": on ? "on" : "off" },
    React.createElement(SpectrSettingsToggle, { value: on, onChange: (next) => {
      setOn(next);
      globalThis.__spectrShowTooltips = next === true;
      if (!next && typeof globalThis.spectrHeaderTipHide === "function") globalThis.spectrHeaderTipHide();
      if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
      Promise.resolve(window.pulp.postMessage("tooltips_set", { enabled: next === true },
        "spectr-tooltips")).catch((error) => console.error("[Spectr] tooltip setting failed", error));
    } }));
}
'''


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 3:
        print('already applied  tooltips are one sized panel, with a Setting')
        return 0
    if raw.count(escaped(MARKER)):
        sys.exit('FAIL: the document is half patched by this script')
    edits = EDITS + [
        ('Settings > FEEDBACK gains Show tooltips', SETTING_OLD, SETTING_NEW),
        ('the Show tooltips switch', COMPONENT_ANCHOR, COMPONENT + COMPONENT_ANCHOR),
    ]
    for label, old, new in edits:
        if raw.count(escaped(old)) != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, raw.count(escaped(old))))
    for label, old, new in edits:
        raw = raw.replace(escaped(old), escaped(new), 1)
        print('applied         ', label)
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
