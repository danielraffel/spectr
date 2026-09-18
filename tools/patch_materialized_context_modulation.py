#!/usr/bin/env python3
"""Reuse Settings modulation state in a compact, persistently mounted menu panel."""
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / 'native-ui/materialized/materialized-document.runtime.json'
MARKER = 'const __spectrSharedModulationMenu = true;'

def once(source, before, after):
    assert source.count(before) == 1, repr(before[:100])
    return source.replace(before, after, 1)

def transform(html):
    if MARKER in html:
        assert html.count(MARKER) == 1
        assert html.count('window.useSpectrModulationState = useSpectrModulationState;') == 1
        assert 'data-spectr-modulation-panel' in html
        return html
    start = html.index('function SpectrModulationSettings() {')
    end = html.index('  return /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-tabs"', start)
    original = html[start:end]
    hook = once(original, 'function SpectrModulationSettings() {',
                'function useSpectrModulationState() {\n  const [ready, setReady] = React.useState(false);')
    hook = once(hook, '    if (!modulation || typeof modulation !== "object") return;',
                '    if (!modulation || typeof modulation !== "object") return;\n    setReady(true);')
    hook += '''  return { value, ready, publish, publishTargetMask, publishMorphViewport, publishTargets };
}
window.useSpectrModulationState = useSpectrModulationState;
function SpectrModulationSettings() {
  const { value, publish, publishTargetMask, publishMorphViewport, publishTargets } = useSpectrModulationState();
'''
    html = once(html, original, hook)
    start = html.index('function ContextMenu(')
    end = html.index('window.ContextMenu', start)
    original = html[start:end]
    menu = original
    brace = menu.index(') {') + len(') {')
    menu = menu[:brace] + '''
  const __spectrSharedModulationMenu = true;
  const [modulationOpen, setModulationOpen] = React.useState(false);
  const { value: modulation, ready: modulationReady, publish: publishModulation } = window.useSpectrModulationState();
''' + menu[brace:]
    modes_start = menu.index('  const modes = [')
    modes_end = menu.index('  const Item =', modes_start)
    menu = menu[:modes_start] + menu[modes_end:]
    menu = once(menu, 'danger, sub })', 'danger, sub, keepOpen, checked })')
    menu = once(menu, '      role: "menuitem",',
                '      role: checked === undefined ? "menuitem" : "menuitemcheckbox",\n      "aria-checked": checked,')
    menu = once(menu, '          onClose();', '          if (!keepOpen) onClose();')
    menu = once(menu, 'hint: "⌘Z"', 'hint: "Cmd+Z"')
    menu = once(menu, 'hint: "⌘⇧Z"', 'hint: "Cmd+Shift+Z"')
    # Keep both panels mounted: late-mounting rows append in the native bridge.
    first = '    hasBand && /* @__PURE__ */ React.createElement(React.Fragment'
    menu = once(menu, first, '''    React.createElement("div", { "data-spectr-main-menu-panel": true, style: { display: modulationOpen ? "none" : "flex", flexDirection: "column" } },
''' + first)
    view = '    /* @__PURE__ */ React.createElement(Divider, { label: "VIEW" }),'
    menu = once(menu, view, '''    React.createElement(Item, { action: "modulation-toggle", label: "Modulation", sub: ">", keepOpen: true, onClick: () => setModulationOpen(true) }),
''' + view)
    tail = '''    }))
  );
}'''
    replacement = '''    }))),
    React.createElement("div", { "data-spectr-modulation-panel": true, style: { display: modulationOpen ? "flex" : "none", flexDirection: "column" } },
      React.createElement(Item, { action: "modulation-back", label: "< Back", keepOpen: true, onClick: () => setModulationOpen(false) }),
      React.createElement(Divider, { label: "MODULATION" }),
      React.createElement(Item, { action: "lfo1-enable", label: "LFO 1", sub: modulation.enabled ? "On" : "Off", checked: modulation.enabled, disabled: !modulationReady, keepOpen: true, onClick: () => publishModulation("enabled", 4000, !modulation.enabled) }),
      React.createElement(Item, { action: "lfo2-enable", label: "LFO 2", sub: modulation.lfo2Enabled ? "On" : "Off", checked: modulation.lfo2Enabled, disabled: !modulationReady, keepOpen: true, onClick: () => publishModulation("lfo2Enabled", 4010, !modulation.lfo2Enabled) }),
      React.createElement(Divider, { label: "SHARED TARGET" }),
      [[0, "bank", "Bank"], [1, "a", "Snapshot A"], [2, "b", "Snapshot B"], [3, "morph", "Morph"]].map(([target, key, label]) => React.createElement(Item, {
        key, action: "modulation-target-" + key, label,
        checked: modulation.targetMask === (1 << target),
        sub: modulation.targetMask === (1 << target) ? "Active" : undefined,
        disabled: !modulationReady,
        onClick: () => publishModulation("target", 4004, target)
      }))
    )
  );
}'''
    menu = once(menu, tail, replacement)
    return once(html, original, menu)

def main():
    document = json.loads(PATH.read_text())
    updated = transform(document['html'])
    if updated == document['html']:
        print('Shared modulation menu already applied')
        return
    document['html'] = updated
    PATH.write_text(json.dumps(document, separators=(',', ':'), ensure_ascii=False) + '\n')
    print('Added shared modulation state and compact menu panel')

if __name__ == '__main__':
    main()
