#!/usr/bin/env python3
"""Plain-key shortcuts belong to the DAW inside a plug-in.

THE DEFECT

  Spectr's editor bound single letters and digits as global shortcuts:
  S L B F G and 1-5 for the edit mode, A and 6 for the analyzer, M for group
  mute, T for the latency mode, and Escape for clearing a band selection. The
  handler called preventDefault on each, which is what tells the plug-in view
  the editor consumed the key -- so in Logic the key never reached Musical
  Typing (A S D F G H J K L ; ' W E T Y U O P and the digits), and playing
  notes with the computer keyboard kept changing Spectr's edit mode and
  flipping its analyzer to OFF. A host offers no way to ask whether Musical
  Typing is open, so the editor cannot detect the conflict.

THE POLICY

  * In a plug-in the plain-key global shortcuts are OFF by default and the key
    events go back to the host unconsumed. Settings > FEEDBACK gains "Keyboard
    shortcuts in DAW", persisted in the plugin state, which turns them back on.
  * The standalone owns its window and keeps them.
  * Keys that were never shown anywhere in the UI are removed in both
    contexts: the digit aliases 1-5 for the edit modes, and Escape clearing a
    selection. (Pressing outside the selection still clears it.)
  * The analyzer-cycle shortcut (A, and its alias 6) is removed in both
    contexts, with its "A to cycle" header and its SHORTCUTS row: A is a
    Musical Typing note, and flipping the analyzer to OFF mid-performance
    changed the whole display. The ANALYZER menu is the way to change it.
  * Every hint tied to a gated key -- S L B F G, M, T -- shows only while the
    key is live: the EDIT MODE badges, the latency chip's "press T to switch",
    and the SHORTCUTS rows. With them off the SHORTCUTS popover says where the
    keys went and how to get them back.
  * Keyboard work inside something the user explicitly opened -- arrows,
    Enter and Escape in a menu or dialog, typing in a field -- is unchanged,
    as are the Cmd/Ctrl chords.

  The processor owns the answer (`keyboard_policy_get`, and the `keyboard`
  member of the hydration payload); the document caches it in
  `globalThis.__spectrKeyboard` and seeds it SYNCHRONOUSLY in the head script,
  before the first render, so a hosted editor never binds a letter the DAW
  owns, not even for the frames before hydration. An editor with no processor
  behind it (a browser preview, the screenshot oracle) reports "unknown" and
  keeps the shortcuts, as the standalone does.

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

MARKER = "__spectrKeyboardPolicyApplied"

EDITS = [
    ('the head script owns the keyboard policy cache',
     "  globalThis.__spectrMorphViewport = globalThis.__spectrMorphViewport\n"
     "    || { enabled: true };\n",
     "  globalThis.__spectrMorphViewport = globalThis.__spectrMorphViewport\n"
     "    || { enabled: true };\n"
     "  // Plain-key shortcut policy (" + MARKER + "). A DAW owns its plain\n"
     "  // keys -- Logic's Musical Typing plays notes on the letter rows -- so\n"
     "  // inside a plug-in the single-letter shortcuts are off unless the user\n"
     "  // turned on \"Keyboard shortcuts in DAW\"; the standalone keeps them.\n"
     "  // The processor owns the answer; this is the editor's cached copy,\n"
     "  // seeded synchronously before the first render (end of this script)\n"
     "  // and refreshed by hydration. No processor behind the document (a\n"
     "  // browser preview, the screenshot oracle) leaves it \"unknown\", which\n"
     "  // keeps the shortcuts, as the standalone does.\n"
     "  globalThis.__spectrKeyboard = globalThis.__spectrKeyboard\n"
     "    || { hostKind: 'unknown', inDaw: false, listeners: [] };\n"
     "  globalThis.spectrPlainKeysActive = () => {\n"
     "    const keyboard = globalThis.__spectrKeyboard;\n"
     "    return !keyboard || keyboard.hostKind !== 'plugin'\n"
     "      || keyboard.inDaw === true;\n"
     "  };\n"
     "  const applyKeyboardPolicy = policy => {\n"
     "    if (!policy || typeof policy !== 'object') return;\n"
     "    const keyboard = globalThis.__spectrKeyboard;\n"
     "    if (policy.host_kind === 'plugin' || policy.host_kind === 'standalone')\n"
     "      keyboard.hostKind = policy.host_kind;\n"
     "    if (typeof policy.shortcuts_in_daw === 'boolean')\n"
     "      keyboard.inDaw = policy.shortcuts_in_daw;\n"
     "    keyboard.listeners.slice().forEach(fn => {\n"
     "      try { fn(); } catch (error) {\n"
     "        console.error(\"[Spectr] keyboard policy listener failed\", error);\n"
     "      }\n"
     "    });\n"
     "  };\n"
     "  globalThis.__spectrApplyKeyboardPolicy = applyKeyboardPolicy;\n"),

    ('hydration refreshes the keyboard policy on presence',
     "    const projections = payload.snapshots || {};\n",
     "    // Hydration-only, like the morph switch: update on presence.\n"
     "    if (payload && payload.keyboard) applyKeyboardPolicy(payload.keyboard);\n"
     "    const projections = payload.snapshots || {};\n"),

    ('the policy is read synchronously before the first render',
     "  globalThis.__spectrPublishNativeMessage = emit;\n"
     "  globalThis.pulp = pulp;\n"
     "  if (typeof window !== 'undefined') window.pulp = pulp;\n",
     "  globalThis.__spectrPublishNativeMessage = emit;\n"
     "  globalThis.pulp = pulp;\n"
     "  if (typeof window !== 'undefined') window.pulp = pulp;\n"
     "  if (typeof globalThis.__spectrEditorDispatch === 'function') {\n"
     "    try {\n"
     "      const policy = JSON.parse(globalThis.__spectrEditorDispatch(JSON.stringify({\n"
     "        type: 'keyboard_policy_get', payload: {}, id: 'spectr-keyboard-policy',\n"
     "      })));\n"
     "      if (policy && policy.ok === true) applyKeyboardPolicy(policy);\n"
     "    } catch (error) {\n"
     "      console.error(\"[Spectr] keyboard policy read failed\", error);\n"
     "    }\n"
     "  }\n"),

    ('the edit-mode lookup drops the undocumented digit aliases',
     '    const modeKeys = {\n'
     '      s: "sculpt",\n'
     '      l: "level",\n'
     '      b: "boost",\n'
     '      f: "flare",\n'
     '      g: "glide",\n'
     '      "1": "sculpt",\n'
     '      "2": "level",\n'
     '      "3": "boost",\n'
     '      "4": "flare",\n'
     '      "5": "glide"\n'
     '    };\n',
     '    const modeKeys = {\n'
     '      s: "sculpt",\n'
     '      l: "level",\n'
     '      b: "boost",\n'
     '      f: "flare",\n'
     '      g: "glide"\n'
     '    };\n'),

    ('plain keys go back to the host unless the policy makes them live',
     '      // Escape leaves a band selection. The band menu passes the guard above\n'
     '      // on purpose, so while it is mounted its own Escape closes it and this\n'
     '      // stays out of the way. Consumed only when there was something to\n'
     '      // clear, so an idle Escape still reaches the host.\n'
     '      if (e.key === "Escape") {\n'
     '        const __spectrSelectionDeselectGestures = true;\n'
     '        if (typeof window.spectrDismissBandMenu === "function") return;\n'
     '        const bank = bankRef.current;\n'
     '        const had = bank && typeof bank.selectionSize === "function"\n'
     '          ? bank.selectionSize() : 0;\n'
     '        if (!had) return;\n'
     '        e.preventDefault();\n'
     '        bank.selectNone();\n'
     '        fireStatus("SELECTION CLEARED");\n'
     '        return;\n'
     '      }\n',
     '      // Every key below is a plain-key global shortcut. Inside a plug-in\n'
     '      // they are the DAW\'s -- Musical Typing plays notes on these rows --\n'
     '      // unless the user turned on "Keyboard shortcuts in DAW". Returning\n'
     '      // WITHOUT preventDefault is what hands the key back to the host.\n'
     '      // (Escape no longer clears a selection -- the key was never shown\n'
     '      // anywhere -- which retires __spectrSelectionDeselectGestures; a press\n'
     '      // outside the selection still clears it.)\n'
     '      if (typeof globalThis.spectrPlainKeysActive === "function"\n'
     '          && !globalThis.spectrPlainKeysActive()) return;\n'),

    ('the analyzer-cycle shortcut is removed',
     '      // Both surfaces that name this key are now true: the ANALYZER\n'
     '      // popover header says "A to cycle" and the SHORTCUTS row says\n'
     '      // "A / 6". `k` is already lower-cased above, so this matches A\n'
     '      // however the keyboard delivered it.\n'
     '      if (k === "a" || k === "6") {\n'
     '        e.preventDefault();\n'
     '        closeBandMenu();\n'
     '        setAnalyzerMode((m) => {\n'
     '          const next = m === "peak" ? "avg" : m === "avg" ? "both" : m === "both" ? "off" : "peak";\n'
     '          window.spectrPublishMode("analyzer", next);\n'
     '          fireStatus("ANALYZER \\u2192 " + next.toUpperCase());\n'
     '          return next;\n'
     '        });\n'
     '      }\n'
     '    };\n',
     '      // No key cycles the analyzer. A and 6 did, and A is a Musical Typing\n'
     '      // note; the ANALYZER menu is the one way to change it.\n'
     '    };\n'),

    ('the reactive hook every hint reads',
     'function spectrEditModeShortcut(key) {\n'
     '  const map = {\n'
     '    s: "sculpt", l: "level", b: "boost", f: "flare", g: "glide",\n'
     '    "1": "sculpt", "2": "level", "3": "boost", "4": "flare", "5": "glide"\n'
     '  };\n',
     '// Whether the plain-key shortcuts are live here, re-rendering when the\n'
     '// processor\'s answer or the Settings switch changes it. Every hint that\n'
     '// names a plain key reads this, so no surface names a key that goes to\n'
     '// the DAW instead.\n'
     'function useSpectrKeyboardPolicy() {\n'
     '  const read = () => {\n'
     '    const keyboard = globalThis.__spectrKeyboard || {};\n'
     '    return {\n'
     '      hostKind: keyboard.hostKind || "unknown",\n'
     '      inDaw: keyboard.inDaw === true,\n'
     '      active: typeof globalThis.spectrPlainKeysActive === "function"\n'
     '        ? globalThis.spectrPlainKeysActive() : true\n'
     '    };\n'
     '  };\n'
     '  const [policy, setPolicy] = React.useState(read);\n'
     '  React.useEffect(() => {\n'
     '    const keyboard = globalThis.__spectrKeyboard;\n'
     '    if (!keyboard || !Array.isArray(keyboard.listeners)) return undefined;\n'
     '    const sync = () => setPolicy((current) => {\n'
     '      const next = read();\n'
     '      return current.hostKind === next.hostKind && current.inDaw === next.inDaw\n'
     '        && current.active === next.active ? current : next;\n'
     '    });\n'
     '    keyboard.listeners.push(sync);\n'
     '    sync();\n'
     '    return () => {\n'
     '      const at = keyboard.listeners.indexOf(sync);\n'
     '      if (at >= 0) keyboard.listeners.splice(at, 1);\n'
     '    };\n'
     '  }, []);\n'
     '  return policy;\n'
     '}\n'
     '// The Settings switch: cached copy first so the next key already obeys\n'
     '// it, then the processor, which owns and persists the value.\n'
     'function spectrSetKeyboardShortcutsInDaw(enabled) {\n'
     '  const apply = globalThis.__spectrApplyKeyboardPolicy;\n'
     '  if (typeof apply === "function") apply({ shortcuts_in_daw: enabled });\n'
     '  if (!window.pulp || typeof window.pulp.postMessage !== "function") return;\n'
     '  Promise.resolve(window.pulp.postMessage("keyboard_shortcuts_set", { enabled },\n'
     '    "spectr-keyboard-shortcuts")).catch((error) =>\n'
     '      console.error("[Spectr] keyboard shortcut setting failed", error));\n'
     '}\n'
     'function spectrEditModeShortcut(key) {\n'
     '  const map = {\n'
     '    s: "sculpt", l: "level", b: "boost", f: "flare", g: "glide"\n'
     '  };\n'),

    ('the open EDIT MODE menu obeys the same policy',
     'function EditModePopover({ value, onChange, onClose }) {\n'
     '  React.useEffect(() => {\n'
     '    const onKey = (e) => {\n'
     '      if (e.metaKey || e.ctrlKey || e.altKey || e.repeat || e.isComposing) return;\n',
     'function EditModePopover({ value, onChange, onClose }) {\n'
     '  const keyboardPolicy = useSpectrKeyboardPolicy();\n'
     '  React.useEffect(() => {\n'
     '    const onKey = (e) => {\n'
     '      if (e.metaKey || e.ctrlKey || e.altKey || e.repeat || e.isComposing) return;\n'
     '      if (typeof globalThis.spectrPlainKeysActive === "function"\n'
     '          && !globalThis.spectrPlainKeysActive()) return;\n'),

    ('the EDIT MODE badges show only while the keys are live',
     '/* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": m.k, style: spectrShortcutChipStyle() }, m.hint)',
     'keyboardPolicy.active && /* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": m.k, style: spectrShortcutChipStyle() }, m.hint)'),

    ('the analyzer header no longer names a key',
     '"ANALYZER \\xB7 A to cycle")',
     '"ANALYZER")'),

    ('the latency chip names T only while it is live',
     'function SpectrLatencyRail() {\n'
     '  const store = globalThis.__spectrLatency\n',
     'function SpectrLatencyRail() {\n'
     '  // Guarded because this component is also evaluated on its own, away\n'
     '  // from the hook\'s script; the answer is fixed for the document\'s life,\n'
     '  // so the hook order cannot change between renders.\n'
     '  const keyboardPolicy = typeof useSpectrKeyboardPolicy === "function"\n'
     '    ? useSpectrKeyboardPolicy() : { active: true };\n'
     '  const store = globalThis.__spectrLatency\n'),
    ('the latency chip tooltip',
     'title: "Latency \\u00B7 " + (current.label || "") + " \\u00B7 press T to switch",',
     'title: "Latency \\u00B7 " + (current.label || "")\n'
     '      + (keyboardPolicy.active ? " \\u00B7 press T to switch" : ""),'),

    ('the SHORTCUTS popover reads the policy',
     'function HelpPopover({ onClose, onLearnMore }) {\n'
     '  return ',
     'function HelpPopover({ onClose, onLearnMore }) {\n'
     '  const keyboardPolicy = useSpectrKeyboardPolicy();\n'
     '  return '),
    ('the SHORTCUTS letter rows show only while live',
     '/* @__PURE__ */ React.createElement(Hrow, { k: "S / L / B" }, "Sculpt \\xB7 Level \\xB7 Boost"), '
     '/* @__PURE__ */ React.createElement(Hrow, { k: "F / G" }, "Flare \\xB7 Glide"), '
     '/* @__PURE__ */ React.createElement(Hrow, { k: "A / 6" }, "Cycle analyzer"), ',
     'keyboardPolicy.active && /* @__PURE__ */ React.createElement(Hrow, { k: "S / L / B" }, "Sculpt \\xB7 Level \\xB7 Boost"), '
     'keyboardPolicy.active && /* @__PURE__ */ React.createElement(Hrow, { k: "F / G" }, "Flare \\xB7 Glide"), '),
    ('the SHORTCUTS mute row, and where the keys went when they are off',
     '/* @__PURE__ */ React.createElement(Hrow, { k: "M" }, "Mute/unmute selection"), ',
     'keyboardPolicy.active && /* @__PURE__ */ React.createElement(Hrow, { k: "M" }, "Mute/unmute selection"), '
     '!keyboardPolicy.active && /* @__PURE__ */ React.createElement("div", { "data-spectr-shortcuts-daw-note": true, style: { width: 320, marginTop: 6, fontSize: 9.5, lineHeight: 1.5, opacity: 0.6, fontFamily: "var(--sans)", letterSpacing: 0.1 } }, '
     '"Letter-key shortcuts are off in a DAW so its own keys, like Musical Typing, keep working. Turn on Keyboard shortcuts in DAW in Settings to use them here."), '),

    ('Settings carries the switch',
     'function SettingsModal({ settings, setSettings, onClose, open = true }) {\n',
     'function SettingsModal({ settings, setSettings, onClose, open = true }) {\n'
     '  // Guarded like the latency rail: the Settings surface is also evaluated\n'
     '  // on its own, away from the hook\'s script.\n'
     '  const keyboardPolicy = typeof useSpectrKeyboardPolicy === "function"\n'
     '    ? useSpectrKeyboardPolicy() : { hostKind: "unknown", inDaw: false, active: true };\n'),
    ('the switch sits in FEEDBACK, in a plug-in only',
     'overLatch: true, value: settings.overLatch === true, onChange: (v) => persist({ overLatch: v }) }))), ',
     'overLatch: true, value: settings.overLatch === true, onChange: (v) => persist({ overLatch: v }) })), '
     'keyboardPolicy.hostKind !== "standalone" && /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Keyboard shortcuts in DAW", hint: "Letter keys go to Spectr" }, '
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-keyboard-shortcuts-in-daw": keyboardPolicy.inDaw ? "on" : "off" }, '
     '/* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: keyboardPolicy.inDaw, onChange: (v) => spectrSetKeyboardShortcutsInDaw(v) })))), '),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) == 1:
        print('already applied  plain-key shortcuts follow the keyboard policy')
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
