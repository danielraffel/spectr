#!/usr/bin/env python3
"""Restore history and macro actions after inheriting dynamic menu layout.

Unique anchors preserve the retained document and all positional bindings.
Reapplying this script verifies every replacement without rewriting the file.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / 'native-ui/materialized/materialized-document.runtime.json'

HISTORY = 'canUndo: payload.can_undo === true, canRedo: payload.can_redo === true,'
OLD_PUBLICATION = '''    if (['undo', 'redo', 'undo_gesture_end', 'macro_set_members'].includes(type)) {
      return dispatch(type, payload, id).then(result => {
        if (result.ok) emit('processing_state_live', result.payload, id);
        return result;
      });
    }
    return dispatch(type, payload, id);'''
EDITS = [
    ('command state publication', '    return dispatch(type, payload, id);', '''    if (['processing_state_set', 'undo_gesture_end'].includes(type)) {
      return dispatch(type, payload, id).then(result => {
        // A gesture may finish before its final React publication. Replaying
        // that older field here would overwrite the local pointer result.
        if (result.ok) emit('history_state', result.payload, id);
        return result;
      });
    }
    if (['undo', 'redo', 'macro_set_members'].includes(type)) {
      return dispatch(type, payload, id).then(result => {
        if (result.ok) emit('processing_state_live', result.payload, id);
        return result;
      });
    }
    return dispatch(type, payload, id);'''),
    ('bank history method', '    sharedState.current = {',
     '    sharedState.current = {\n      updateHistoryAvailability,'),
    ('history subscription', '    const unsubscribeModulation = window.pulp.on("modulation_frame", (message) => {', '''    const unsubscribeHistory = window.pulp.on("history_state", (message) => {
      const payload = message && message.payload;
      const bank = bankRef.current;
      if (payload && bank && typeof bank.updateHistoryAvailability === "function")
        bank.updateHistoryAvailability({ canUndo: payload.can_undo === true,
                                         canRedo: payload.can_redo === true });
    });
    const unsubscribeModulation = window.pulp.on("modulation_frame", (message) => {'''),
    ('history unsubscribe', '      if (typeof unsubscribeModulation === "function") unsubscribeModulation();',
     '      if (typeof unsubscribeHistory === "function") unsubscribeHistory();\n      if (typeof unsubscribeModulation === "function") unsubscribeModulation();'),
    ('hydrated history', '      revision: Number(payload.revision) || 0,',
     '      ' + HISTORY + '\n      revision: Number(payload.revision) || 0,'),
    ('live history', '      motionMode, analyzerMode, editMode, visualizationMode,',
     '      motionMode, analyzerMode, editMode, visualizationMode,\n      ' + HISTORY),
    ('app live history', '      ? window.SpectrNativeState.parseMacros(payload, n) : null,',
     '      ? window.SpectrNativeState.parseMacros(payload, n) : null,\n    ' + HISTORY),
    ('history availability', '  const macroStateRef = useRef(null);', '''  const macroStateRef = useRef(null);
  const [historyAvailability, setHistoryAvailability] = useState({ canUndo: false, canRedo: false });
  const updateHistoryAvailability = (state) => {
    const next = { canUndo: state.canUndo === true, canRedo: state.canRedo === true };
    setHistoryAvailability(old => old.canUndo === next.canUndo && old.canRedo === next.canRedo ? old : next);
  };'''),
    ('hydrated history update', '        setMacroState(state.macros);\n        snapshotsRef.current',
     '        setMacroState(state.macros);\n        updateHistoryAvailability(state);\n        snapshotsRef.current'),
    ('live history update', '        setMacroState(state.macros);\n        mutedGainDbRef.current',
     '        setMacroState(state.macros);\n        updateHistoryAvailability(state);\n        mutedGainDbRef.current'),
    ('menu handlers', '        editMode: editModeRef.current,\n        onClose: () => setCtxMenu(null),', '''        editMode: editModeRef.current,
        canUndo: historyAvailability.canUndo,
        canRedo: historyAvailability.canRedo,
        macros: macroStateRef.current,
        onUndo: () => postNative("undo", {}),
        onRedo: () => postNative("redo", {}),
        onMacroMembers: (macro, clear) => {
          const slots = clear ? [] : [...selectionRef.current].filter(i => i >= 0 && i < N);
          if (!clear && !slots.length) return;
          postNative("macro_set_members", { macro, slots });
          if (onStatus) onStatus(clear ? `MACRO ${macro + 1} CLEARED` : `${slots.length} BANDS ASSIGNED TO MACRO ${macro + 1}`);
        },
        onClose: () => setCtxMenu(null),'''),
    ('memo dependencies', 'previous.editMode === next.editMode);\nwindow.FilterBank',
     'previous.editMode === next.editMode && previous.canUndo === next.canUndo && previous.canRedo === next.canRedo && previous.macros === next.macros);\nwindow.FilterBank'),
    ('menu props', 'onMuteSel, onFitView }) {',
     'onMuteSel, onFitView, canUndo, canRedo, macros, onUndo, onRedo, onMacroMembers }) {'),
    ('menu bounds', '''  const W = 230, H = 380;
  const left = Math.min(x, vw - W - 8);
  const top = Math.min(y, vh - H - 8);
  const hasBand = band >= 0;
  const hasSel = selection && selection.size > 0;''', '''  const hasBand = band >= 0;
  const hasSel = selection && selection.size > 0;
  const assigned = [0, 1, 2, 3].filter(i => macros && macros[i] && macros[i].slots.length);
  const W = 250;
  const H = 420 + (hasBand ? 100 : 0) + (hasSel ? 160 : 0) + assigned.length * 26;
  const left = Math.max(8, Math.min(x, vw - W - 8));
  const top = Math.max(8, Math.min(y, vh - H - 8));'''),
    ('macro and history rows', '''    /* @__PURE__ */ React.createElement(Item, { label: "Fit full range", onClick: onFitView, sub: "20 Hz \\u2013 20 kHz" })
  );
}
window.ContextMenu''', '''    /* @__PURE__ */ React.createElement(Item, { label: "Fit full range", onClick: onFitView, sub: "20 Hz \\u2013 20 kHz" }),
    /* @__PURE__ */ React.createElement(Item, { action: "undo", label: "Undo", hint: "⌘Z", disabled: !canUndo, onClick: onUndo }),
    /* @__PURE__ */ React.createElement(Item, { action: "redo", label: "Redo", hint: "⌘⇧Z", disabled: !canRedo, onClick: onRedo }),
    /* @__PURE__ */ React.createElement(Divider, { label: "MACROS" }),
    hasSel && [0, 1, 2, 3].map(i => /* @__PURE__ */ React.createElement(Item, {
      key: "assign-" + i, action: "assign-macro-" + i,
      label: `Assign selection to Macro ${i + 1}`, onClick: () => onMacroMembers(i, false)
    })),
    assigned.map(i => /* @__PURE__ */ React.createElement(Item, {
      key: "clear-" + i, action: "clear-macro-" + i,
      label: `Clear Macro ${i + 1}`, onClick: () => onMacroMembers(i, true)
    }))
  );
}
window.ContextMenu'''),
]


def main():
    raw = PATH.read_text()
    original = json.loads(raw)
    old = json.dumps(OLD_PUBLICATION)[1:-1]
    if old in raw:
        assert raw.count(old) == 1
        raw = raw.replace(old, '    return dispatch(type, payload, id);', 1)
    for label, old, new in EDITS:
        old, new = json.dumps(old)[1:-1], json.dumps(new)[1:-1]
        if raw.count(new) == 1:
            continue
        if raw.count(old) != 1:
            raise SystemExit(f'{label}: expected one anchor, found {raw.count(old)}')
        raw = raw.replace(old, new, 1)
    result = json.loads(raw)
    for key in ('text_bindings', 'layout_bindings', 'paint_bindings'):
        assert result[key] == original[key], key
    if raw != PATH.read_text():
        PATH.write_text(raw)
    print('History and macro menu actions verified; positional bindings unchanged.')


if __name__ == '__main__':
    main()
