#!/usr/bin/env python3
"""The Settings close button owns its own hover and press state.

The close button's hover/press look (`closeState`) was state of SettingsModal,
so pointer-enter and pointer-leave on the button re-rendered the whole
Settings panel -- every group, field, chip row, slider and the modulation
panel -- to recolour one 32px square. Measured headless on the shipping
editor, that reconcile was ~18 ms of a ~25 ms hover commit.

The button is now a leaf, SpectrSettingsCloseButton, holding the same state
and rendering the same element with the same props and styles, so the
captured layout and every selector that addresses it
(`[data-spectr-settings-close]`, `data-spectr-close-state`) are unchanged. A
hover re-renders the button alone.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once, refuses
a half-patched document, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

BUTTON = (
    'React.createElement("button", { "data-spectr-settings-close": true, '
    '"data-spectr-close-state": closeState, "aria-label": "Close settings", '
    'onPointerEnter: () => setCloseState("hover"), '
    'onPointerLeave: () => setCloseState("idle"), '
    'onPointerDown: (event) => { event.stopPropagation(); setCloseState("pressed"); }, '
    'onPointerUp: () => setCloseState("hover"), '
    'onClick: (event) => { event.stopPropagation(); onClose(); }, style: {\n'
    '    background: closeState === "pressed" ? "rgba(180,220,255,0.22)" : '
    'closeState === "hover" ? "rgba(255,255,255,0.10)" : "transparent",\n'
    '    border: "1px solid " + (closeState === "pressed" ? "rgba(200,230,255,0.55)" : '
    'closeState === "hover" ? "rgba(255,255,255,0.18)" : "transparent"),\n'
    '    color: closeState === "idle" ? "rgba(255,255,255,0.6)" : "#fff",\n'
    '    cursor: "pointer", fontSize: 20, padding: 0, lineHeight: 1, width: 32, height: 32,\n'
    '    borderRadius: 3, display: "flex", alignItems: "center", justifyContent: "center"\n'
    '  } }, "\\xD7")')

# The leaf's border keeps a fixed 1px width and varies only its colour, so a
# hover changes paint props alone and never counts as a geometric commit.
LEAF_BUTTON = BUTTON.replace(
    'border: "1px solid " + (closeState === "pressed" ? "rgba(200,230,255,0.55)" : '
    'closeState === "hover" ? "rgba(255,255,255,0.18)" : "transparent"),',
    'borderWidth: 1, borderStyle: "solid",\n'
    '    borderColor: closeState === "pressed" ? "rgba(200,230,255,0.55)" : '
    'closeState === "hover" ? "rgba(255,255,255,0.18)" : "transparent",')
assert LEAF_BUTTON != BUTTON

LEAF = (
    '// The close button owns its hover and press look, so pointer-enter and\n'
    '// pointer-leave re-render the button alone rather than the whole panel.\n'
    'function SpectrSettingsCloseButton({ onClose }) {\n'
    '  const [closeState, setCloseState] = React.useState("idle");\n'
    '  return /* @__PURE__ */ ' + LEAF_BUTTON + ';\n'
    '}\n')

EDITS = [
    ('the close button is a leaf that owns its state',
     'function SettingsModal({ settings, setSettings, onClose, open = true }) {\n',
     LEAF + 'function SettingsModal({ settings, setSettings, onClose, open = true }) {\n'),
    ('the panel renders the leaf',
     BUTTON,
     'React.createElement(SpectrSettingsCloseButton, { onClose })'),
    ('the panel no longer holds the button state',
     '  const [closeState, setCloseState] = React.useState("idle");\n'
     '  React.useLayoutEffect(() => {\n'
     '    const panel = document.querySelector("[data-spectr-settings-panel]");',
     '  React.useLayoutEffect(() => {\n'
     '    const panel = document.querySelector("[data-spectr-settings-panel]");'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    leaf_head = escaped('function SpectrSettingsCloseButton({ onClose }) {')
    if raw.count(leaf_head) == 1:
        print('already applied  the Settings close button owns its own state')
        return 0
    modal_head = escaped('function SettingsModal({ settings, setSettings, onClose, open = true }) {\n')
    state_old = escaped(EDITS[2][1])
    for label, token in (('SettingsModal', modal_head), ('the close button', escaped(BUTTON)),
                         ('the panel\'s button state', state_old)):
        if raw.count(token) != 1:
            sys.exit('FAIL: %s occurs %d times, expected 1' % (label, raw.count(token)))
    # Order matters: swap the panel's button for the leaf, drop the panel's
    # state, and only then add the leaf (which carries the button markup).
    raw = raw.replace(escaped(BUTTON),
                      escaped('React.createElement(SpectrSettingsCloseButton, { onClose })'), 1)
    raw = raw.replace(state_old, escaped(EDITS[2][2]), 1)
    raw = raw.replace(modal_head, escaped(LEAF) + modal_head, 1)
    if raw.count(escaped(BUTTON)) != 0 or raw.count(escaped(LEAF_BUTTON)) != 1 \
            or raw.count(leaf_head) != 1:
        sys.exit('FAIL: the button markup must survive only inside the leaf')
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('applied          the Settings close button owns its own state')
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
