#!/usr/bin/env python3
"""Ask before overriding modulation: saved with the session.

THE DEFECT

  The Setting (Settings > MODULATION, the header context menus, and "Don't
  ask again" in the override dialog) lived only in the document's React
  settings object, which nothing persists. Turning it off lasted until the
  editor closed; reopening the editor or the project asked again.

THE FIX

  The processor owns the value, like Show tooltips
  (tools/patch_materialized_tooltip_panel.py): `ask_before_override` rides the
  keyboard policy payload the editor already caches, every change goes back
  through `override_ask_set`, and the plugin state saves it (absent = on).
  The document adopts the processor's copy at mount and whenever the policy
  changes, and posts every change the user makes.

Idempotent like the other patch_materialized_* scripts. Run after
tools/patch_materialized_tooltip_panel.py and
tools/patch_materialized_modulation_freeze_override.py.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

MARKER = "__spectrAskPersist"


def escaped(value):
    return json.dumps(value, ensure_ascii=False)[1:-1]


EDITS = [
    ('the editor caches the processor\'s Ask before overriding with the keyboard policy',
     "    if (typeof policy.show_tooltips === 'boolean')\n"
     "      globalThis.__spectrShowTooltips = policy.show_tooltips;\n",
     "    if (typeof policy.show_tooltips === 'boolean')\n"
     "      globalThis.__spectrShowTooltips = policy.show_tooltips;\n"
     "    // Ask before overriding modulation (__spectrAskPersist): the\n"
     "    // processor's copy, saved with the session; absent means on.\n"
     "    if (typeof policy.ask_before_override === 'boolean')\n"
     "      globalThis.__spectrAskPolicy = policy.ask_before_override;\n"),
    ('every change of the Setting is posted to the processor',
     'function spectrOverrideAsks() {\n'
     '  return globalThis.__spectrAskBeforeOverride !== false;\n'
     '}\n',
     'function spectrOverrideAsks() {\n'
     '  return globalThis.__spectrAskBeforeOverride !== false;\n'
     '}\n'
     '// __spectrAskPersist: the processor owns "Ask before overriding" and saves\n'
     '// it with the session; every change the user makes goes there.\n'
     'function spectrSaveAskBeforeOverride(on) {\n'
     '  globalThis.__spectrAskPolicy = on === true;\n'
     '  if (!window.pulp || typeof window.pulp.postMessage !== "function") return;\n'
     '  try {\n'
     '    Promise.resolve(window.pulp.postMessage("override_ask_set", { enabled: on === true },\n'
     '      "spectr-ask-override")).catch((error) =>\n'
     '        console.error("[Spectr] ask-before-override setting failed", error));\n'
     '  } catch (error) {\n'
     '    console.error("[Spectr] ask-before-override setting failed", error);\n'
     '  }\n'
     '}\n'),
    ('the Settings row and the context menu save what they set, and the dialog adopts the saved choice',
     '  window.spectrSetAskBeforeOverride = (on) => {\n'
     '    globalThis.__spectrAskBeforeOverride = on === true;\n'
     '    setSettings((s) => ({ ...s, askBeforeOverride: on === true }));\n'
     '  };\n',
     '  window.spectrSetAskBeforeOverride = (on) => {\n'
     '    globalThis.__spectrAskBeforeOverride = on === true;\n'
     '    setSettings((s) => ({ ...s, askBeforeOverride: on === true }));\n'
     '    spectrSaveAskBeforeOverride(on === true);\n'
     '  };\n'
     '  // __spectrAskPersist: adopt the processor\'s saved choice at mount and\n'
     '  // whenever the keyboard policy (which carries it) changes.\n'
     '  React.useEffect(() => {\n'
     '    const adopt = () => {\n'
     '      const saved = globalThis.__spectrAskPolicy;\n'
     '      if (typeof saved !== "boolean") return;\n'
     '      globalThis.__spectrAskBeforeOverride = saved;\n'
     '      setSettings((s) => (s.askBeforeOverride === saved ? s : { ...s, askBeforeOverride: saved }));\n'
     '    };\n'
     '    adopt();\n'
     '    const keyboard = globalThis.__spectrKeyboard;\n'
     '    if (!keyboard || !Array.isArray(keyboard.listeners)) return undefined;\n'
     '    keyboard.listeners.push(adopt);\n'
     '    return () => {\n'
     '      const at = keyboard.listeners.indexOf(adopt);\n'
     '      if (at >= 0) keyboard.listeners.splice(at, 1);\n'
     '    };\n'
     '  }, []);\n'),
    ('"Don\'t ask again" is saved too',
     '    if (dontAsk) {\n'
     '      globalThis.__spectrAskBeforeOverride = false;\n'
     '      setSettings((s) => ({ ...s, askBeforeOverride: false }));\n'
     '    }\n',
     '    if (dontAsk) {\n'
     '      globalThis.__spectrAskBeforeOverride = false;\n'
     '      setSettings((s) => ({ ...s, askBeforeOverride: false }));\n'
     '      spectrSaveAskBeforeOverride(false);\n'
     '    }\n'),
]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 3:
        print('already applied  Ask before overriding is saved with the session')
        return 0
    if raw.count(escaped(MARKER)):
        sys.exit('FAIL: the document is half patched by this script')
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
