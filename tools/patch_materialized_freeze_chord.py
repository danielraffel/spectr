#!/usr/bin/env python3
"""Freeze gets a modifier chord that works in a DAW by default: Ctrl+Opt+Cmd+F.

WHY A CHORD

  Q toggles Freeze, but Q is a plain-key shortcut: inside a plug-in the DAW
  owns the plain keys (Logic's Musical Typing plays notes on the letter rows),
  so Q is off there unless the user turns on "Keyboard shortcuts in DAW". A
  chord with modifiers is not a Musical Typing key and cannot type into a
  field, so it can be live everywhere.

WHY CTRL+OPT+CMD+F (macOS: Control-Option-Command-F)

  Checked against the default key commands of Logic Pro 12.3, Ableton Live 12,
  Cubase 12 and the available REAPER 7 lists, it is the one F chord none of
  them assigns: Opt+Cmd+F (Logic Convert Regions, Live Create Fade), Ctrl+Opt+F
  (Logic nudge value), Cmd+Shift+F (Logic Folder Stack, Live note filters),
  Ctrl+Shift+F (Logic select) and Cmd+Opt+Shift+F (Live's own Freeze Tracks)
  are all taken. So if a host keeps the chord -- its window is key, not the
  plug-in's, or it dispatches key commands before the plug-in -- the chord
  does nothing there rather than something else.

WHAT CHANGES

  * The key handler toggles Freeze on the chord in every context, through the
    same write the toggle makes (one host edit gesture). A focused text field
    or an overlay that owns the keyboard keeps it, as for the Cmd chords.
  * The SHORTCUTS panel lists "CTRL+OPT+CMD+F  Freeze / unfreeze" in every
    context, above the Q row it keeps where Q is live.
  * The toggle's tooltip names the chord everywhere and Q where Q is live.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts each patch point occurs exactly once, refuses a
half-patched document, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrFreezeChord"

CHORD_LABEL = "CTRL+OPT+CMD+F"

EDITS = [
    ('the key handler toggles Freeze on the chord in every context',
     '    const onKey = (e) => {\n'
     '      const t = e.target;\n'
     '      // Cmd/Ctrl chords, handled BEFORE the bare-letter guard below --\n',
     '    const onKey = (e) => {\n'
     '      const t = e.target;\n'
     '      // Freeze on Control-Option-Command-F (' + MARKER + '), in a\n'
     '      // plug-in as in the standalone: a chord is no Musical Typing key and\n'
     '      // types nothing, and no DAW\'s default key commands use it, so a host\n'
     '      // that keeps it does nothing with it. A focused field or an overlay\n'
     '      // that owns the keyboard keeps it, as for the Cmd chords below.\n'
     '      // The key is matched by position as well as by name: Option turns\n'
     '      // the F key\'s character into "\\u0192".\n'
     '      if (e.ctrlKey && e.altKey && e.metaKey && !e.shiftKey && !e.repeat\n'
     '          && (e.code === "KeyF" || e.keyCode === 70\n'
     '            || (typeof e.key === "string"\n'
     '              && (e.key.toLowerCase() === "f" || e.key === "\\u0192")))) {\n'
     '        const typing = t && (t.tagName === "INPUT" || t.tagName === "TEXTAREA"\n'
     '          || t.tagName === "SELECT" || t.isContentEditable);\n'
     '        if (typing || overlayBlocksShortcut()) return;\n'
     '        e.preventDefault();\n'
     '        const dismiss = window.spectrDismissBandMenu;\n'
     '        if (typeof dismiss === "function") dismiss();\n'
     '        const frozen = typeof window.spectrToggleFreeze === "function"\n'
     '          ? window.spectrToggleFreeze() : null;\n'
     '        if (frozen === null) fireStatus("FREEZE UNAVAILABLE");\n'
     '        else fireStatus(frozen ? "FROZEN" : "LIVE");\n'
     '        return;\n'
     '      }\n'
     '      // Cmd/Ctrl chords, handled BEFORE the bare-letter guard below --\n'),

    ('the SHORTCUTS panel lists the chord everywhere and Q where it is live',
     'keyboardPolicy.active && /* @__PURE__ */ React.createElement(Hrow, { k: "Q" }, "Freeze / unfreeze"), ',
     '/* @__PURE__ */ React.createElement(Hrow, { k: "' + CHORD_LABEL + '" }, "Freeze / unfreeze"), '
     'keyboardPolicy.active && /* @__PURE__ */ React.createElement(Hrow, { k: "Q" }, "Freeze / unfreeze"), '),

    ('the toggle tooltip names the chord, and Q where Q is live',
     '    title: "Freeze the incoming sound" + (keyboardPolicy.active ? " (Q)" : ""),\n',
     '    title: "Freeze the incoming sound (Ctrl+Opt+Cmd+F"\n'
     '      + (keyboardPolicy.active ? " or Q" : "") + ")",\n'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 1:
        for label, old, new in EDITS:
            if raw.count(escaped(new)) != 1:
                sys.exit('FAIL %s: document is half patched' % label)
        print('already applied  Freeze on Ctrl+Opt+Cmd+F')
        return 0
    for label, old, new in EDITS:
        if raw.count(escaped(old)) != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, raw.count(escaped(old))))
    for label, old, new in EDITS:
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
