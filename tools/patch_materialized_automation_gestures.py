#!/usr/bin/env python3
"""Every LFO, Output trim and Morph edit records in host automation, and plays back.

THE DEFECTS

  1. The modulation controls -- LFO 1 / LFO 2 on/off, shape, rate, depth and
     the shared target, in the band menu and in Settings -- and the Output trim
     wrote their parameter through `param_set`, a bare value write. The DSP and
     the host's lane moved, but a host recording in Touch, Latch or Write keys
     on the edit GESTURE (begin, value, end) and had nothing to record. Turning
     an LFO on while recording left no automation behind.

  2. A drag on one of those sliders, on Morph, or on the plot (band paint,
     minimap pan) had no drag bracket. Morph and the plot sent each move as
     its own complete begin/value/end, so a host in Touch saw the control
     released between every two moves and snapped back to the existing lane
     in each gap.

  3. Host playback could not move a modulation value the user had once
     edited. The editor keeps its own write "pending" until the processor
     reports the same value back, and compared with `===`. Depth travels as a
     float and is reported as a double, so 0.37 came back as
     0.3700000047683716, never matched, and the pending 0.37 overrode every
     automation frame after it. And host playback never moved the Morph
     slider at all: the lane was not in the editor's projection.

THE FIX

  1. The modulation `publish` and `writeTrim` post `param_edit`, which the
     processor applies as a complete gesture outside a drag and as the value
     alone inside one (Spectr::edit_param_from_editor).
  2. The band-menu and Settings sliders take a `gestureId`; a press opens the
     parameter's bracket (`param_gesture_begin`) and the release closes it
     (`param_gesture_end`), so a drag of any length is one recorded gesture.
     The Output trim input does the same where the runtime delivers its
     pointer events. A Morph press, and a plot press once it has captured
     the pointer, opens the processor's drag epoch (`param_drag_start`) and
     the release closes it after the final value, so every lane the drag
     writes gets one bracket per drag.
  3. A pending write is acknowledged by a reported value within float
     precision of it, not only by an identical one. The Morph slider follows
     the `morph` the processor now projects, unless a drag owns it.

  `globalThis.spectrParamGesture(id, open)` and `globalThis.spectrParamDrag(open)`
  track what is open, so the three release handlers a pointer can fire (up,
  cancel, lost capture) close a bracket once.

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

MARKER = "__spectrAutomationGestures"

HELPERS = (
    '// Host edit gestures (' + MARKER + '). A host recording in Touch, Latch\n'
    '// or Write keys on begin/end around a value. A press that is one act\n'
    '// (a toggle, a shape, a target) is a complete bracket on its own, posted\n'
    '// by `param_edit`; a drag opens the bracket on press and closes it on\n'
    '// release so the host sees ONE gesture per drag. Tracked here so the\n'
    '// several release events a pointer can fire close a bracket once.\n'
    'globalThis.spectrParamGesture = function (id, open) {\n'
    '  const held = globalThis.__spectrParamGestures\n'
    '    || (globalThis.__spectrParamGestures = new Set());\n'
    '  if ((open === true) === held.has(id)) return;\n'
    '  if (open === true) held.add(id); else held.delete(id);\n'
    '  if (!window.pulp || typeof window.pulp.postMessage !== "function") return;\n'
    '  try {\n'
    '    Promise.resolve(window.pulp.postMessage(\n'
    '      open === true ? "param_gesture_begin" : "param_gesture_end", { id },\n'
    '      "spectr-param-gesture-" + id)).catch((error) =>\n'
    '        console.error("[Spectr] parameter gesture failed", error));\n'
    '  } catch (error) {\n'
    '    console.error("[Spectr] parameter gesture failed", error);\n'
    '  }\n'
    '};\n'
    '// A drag on something the processor writes as a derived value -- Morph,\n'
    '// or the bands and viewport a plot drag republishes every frame. Its\n'
    '// bracket is the processor\'s drag epoch: one begin/end per parameter\n'
    '// the drag touches. The two never drag at once, so one flag serves both.\n'
    'globalThis.spectrParamDrag = function (open) {\n'
    '  if ((open === true) === (globalThis.__spectrParamDragOpen === true)) return;\n'
    '  globalThis.__spectrParamDragOpen = open === true;\n'
    '  if (!window.pulp || typeof window.pulp.postMessage !== "function") return;\n'
    '  try {\n'
    '    Promise.resolve(window.pulp.postMessage(\n'
    '      open === true ? "param_drag_start" : "param_drag_end", {},\n'
    '      "spectr-param-drag")).catch((error) =>\n'
    '        console.error("[Spectr] drag bracket failed", error));\n'
    '  } catch (error) {\n'
    '    console.error("[Spectr] drag bracket failed", error);\n'
    '  }\n'
    '};\n'
    'function useSpectrModulationState() {'
)

EDITS = [
    ('modulation writes go out as host edits',
     'Promise.resolve(window.pulp.postMessage("param_set", { id, value: typeof next === "boolean" ? next ? 1 : 0 : next }, "spectr-modulation-" + key))',
     'Promise.resolve(window.pulp.postMessage("param_edit", { id, value: typeof next === "boolean" ? next ? 1 : 0 : next }, "spectr-modulation-" + key))'),
    ('a reported value acknowledges a pending write within float precision',
     '      if (pending[key] === next[key]) delete pending[key];',
     '      // The processor reports a parameter as the float it stores, widened\n'
     '      // to a double: a written 0.37 comes back as 0.3700000047683716. That\n'
     '      // is the same value, and acknowledges the write.\n'
     '      const written = pending[key], reported = next[key];\n'
     '      if (written === reported || (typeof written === "number" && typeof reported === "number"\n'
     '          && Math.abs(written - reported) <= 1e-5 * Math.max(1, Math.abs(written))))\n'
     '        delete pending[key];'),
    ('gesture helpers',
     'function useSpectrModulationState() {',
     HELPERS),

    # Band-menu slider rows (LFO rate and depth).
    ('band-menu slider takes a gesture id',
     '  const SliderRow = ({ key, action, label, value, min, max, step, fmt, disabled, onChange }) => {',
     '  const SliderRow = ({ key, action, label, value, min, max, step, fmt, disabled, onChange, gestureId }) => {'),
    ('band-menu slider press opens the bracket',
     '          sliderDragRef.current = event && event.currentTarget ? event.currentTarget : null;',
     '          sliderDragRef.current = event && event.currentTarget ? event.currentTarget : null;\n'
     '          if (!disabled && gestureId !== undefined) globalThis.spectrParamGesture && globalThis.spectrParamGesture(gestureId, true);'),
    ('band-menu slider release closes it',
     '        onPointerUp: () => { sliderDragRef.current = null; },',
     '        onPointerUp: () => { sliderDragRef.current = null; if (gestureId !== undefined) globalThis.spectrParamGesture && globalThis.spectrParamGesture(gestureId, false); },'),
    ('band-menu slider cancel closes it',
     '        onPointerCancel: () => { sliderDragRef.current = null; },',
     '        onPointerCancel: () => { sliderDragRef.current = null; if (gestureId !== undefined) globalThis.spectrParamGesture && globalThis.spectrParamGesture(gestureId, false); },'),
    ('band-menu slider lost capture closes it',
     '        onLostPointerCapture: () => { sliderDragRef.current = null; },',
     '        onLostPointerCapture: () => { sliderDragRef.current = null; if (gestureId !== undefined) globalThis.spectrParamGesture && globalThis.spectrParamGesture(gestureId, false); },'),
    ('band-menu rate row names its parameter',
     '            disabled: !modulationReady,\n            onChange: (i) => publishModulation(key, modulationSource === 1 ? 4002 : 4012,',
     '            disabled: !modulationReady,\n            gestureId: modulationSource === 1 ? 4002 : 4012,\n            onChange: (i) => publishModulation(key, modulationSource === 1 ? 4002 : 4012,'),
    ('band-menu depth row names its parameter',
     '          disabled: !modulationReady,\n          onChange: (v) => publishModulation(modulationSource === 1 ? "depth" : "lfo2Depth",',
     '          disabled: !modulationReady,\n          gestureId: modulationSource === 1 ? 4003 : 4013,\n          onChange: (v) => publishModulation(modulationSource === 1 ? "depth" : "lfo2Depth",'),

    # Settings sliders (LFO rate and depth).
    ('settings slider takes a gesture id',
     'function SpectrSettingsSlider({ value, min, max, step, onChange, fmt }) {',
     'function SpectrSettingsSlider({ value, min, max, step, onChange, fmt, gestureId }) {'),
    ('settings slider press opens the bracket',
     '      onPointerDown: (event) => {\n        dragRef.current = event && event.currentTarget ? event.currentTarget : null;',
     '      onPointerDown: (event) => {\n        if (gestureId !== undefined) globalThis.spectrParamGesture && globalThis.spectrParamGesture(gestureId, true);\n        dragRef.current = event && event.currentTarget ? event.currentTarget : null;'),
    ('settings slider release closes it',
     '      onPointerUp: () => { dragRef.current = null; },',
     '      onPointerUp: () => { dragRef.current = null; if (gestureId !== undefined) globalThis.spectrParamGesture && globalThis.spectrParamGesture(gestureId, false); },'),
    ('settings slider cancel closes it',
     '      onPointerCancel: () => { dragRef.current = null; },',
     '      onPointerCancel: () => { dragRef.current = null; if (gestureId !== undefined) globalThis.spectrParamGesture && globalThis.spectrParamGesture(gestureId, false); },'),
    ('settings slider lost capture closes it',
     '      onLostPointerCapture: () => { dragRef.current = null; },',
     '      onLostPointerCapture: () => { dragRef.current = null; if (gestureId !== undefined) globalThis.spectrParamGesture && globalThis.spectrParamGesture(gestureId, false); },'),
    ('settings LFO rate names its parameter',
     'SpectrSettingsSlider, { value: value.rate,',
     'SpectrSettingsSlider, { gestureId: 4002, value: value.rate,'),
    ('settings LFO depth names its parameter',
     'SpectrSettingsSlider, { value: value.depth,',
     'SpectrSettingsSlider, { gestureId: 4003, value: value.depth,'),
    ('settings LFO 2 rate names its parameter',
     'SpectrSettingsSlider, { value: value.lfo2Rate || 4,',
     'SpectrSettingsSlider, { gestureId: 4012, value: value.lfo2Rate || 4,'),
    ('settings LFO 2 depth names its parameter',
     'SpectrSettingsSlider, { value: value.lfo2Depth || 0,',
     'SpectrSettingsSlider, { gestureId: 4013, value: value.lfo2Depth || 0,'),

    # Output trim.
    ('output trim writes go out as host edits',
     '      window.pulp.postMessage("param_set",\n        { id: 2, value: next },\n        "spectr-output-trim");',
     '      window.pulp.postMessage("param_edit",\n        { id: 2, value: next },\n        "spectr-output-trim");'),
    # Anchored after the track's width, which the Freeze LENGTH control
    # (patch_materialized_freeze_length.py) narrows: either may run first.
    ('output trim drag is one bracket',
     ' flexShrink: 0, accentColor: "hsl(200,80%,60%)" }\n    }),',
     ' flexShrink: 0, accentColor: "hsl(200,80%,60%)" },\n'
     '      onPointerDown: () => globalThis.spectrParamGesture && globalThis.spectrParamGesture(2, true),\n'
     '      onPointerUp: () => globalThis.spectrParamGesture && globalThis.spectrParamGesture(2, false),\n'
     '      onPointerCancel: () => globalThis.spectrParamGesture && globalThis.spectrParamGesture(2, false),\n'
     '      onLostPointerCapture: () => globalThis.spectrParamGesture && globalThis.spectrParamGesture(2, false)\n'
     '    }),'),

    # Plot drags: band paint, minimap pan/resize. Opened only once the press
    # has captured the pointer -- a right-click opens the band menu instead
    # and may never see its release.
    ('a plot drag opens the drag bracket once it owns the pointer',
     '    wrapRef.current.setPointerCapture(e.pointerId);\n    const mm = minimapHit(x, y, g);',
     '    wrapRef.current.setPointerCapture(e.pointerId);\n    globalThis.spectrParamDrag && globalThis.spectrParamDrag(true);\n    const mm = minimapHit(x, y, g);'),
    ('a plot release closes it',
     '    postNative("undo_gesture_end", {});\n    if (p && p.macroDrag',
     '    postNative("undo_gesture_end", {});\n    globalThis.spectrParamDrag && globalThis.spectrParamDrag(false);\n    if (p && p.macroDrag'),

    # Morph.
    ('the morph slider follows host playback',
     '  const publishedRef = React.useRef(0);',
     '  const publishedRef = React.useRef(0);\n'
     '  // Host playback of the Morph lane moves the thumb. The processor\n'
     '  // projects the lane with every hydration and live frame; a drag in\n'
     '  // progress owns the thumb and ignores them.\n'
     '  useEffectChrome(() => {\n'
     '    if (!window.pulp || typeof window.pulp.on !== "function") return undefined;\n'
     '    const accept = (message) => {\n'
     '      const body = message && message.payload ? message.payload : message;\n'
     '      const t = body ? Number(body.morph) : NaN;\n'
     '      if (!Number.isFinite(t) || dragRef.current) return;\n'
     '      publishedRef.current = t;\n'
     '      setV((current) => current === t ? current : t);\n'
     '    };\n'
     '    const off = [window.pulp.on("processing_state_hydrate", accept),\n'
     '                 window.pulp.on("processing_state_live", accept)];\n'
     '    return () => off.forEach((fn) => { if (typeof fn === "function") fn(); });\n'
     '  }, []);'),
    ('a morph press opens the drag bracket',
     '      onPointerDown: (event) => {\n        if (!hasBoth) return;',
     '      onPointerDown: (event) => {\n        if (!hasBoth) return;\n        globalThis.spectrParamDrag && globalThis.spectrParamDrag(true);'),
    ('a morph release closes it after the final value',
     '    if (bankRef.current) bankRef.current.setMorph(val, false);\n  };',
     '    if (bankRef.current) bankRef.current.setMorph(val, false);\n'
     '    globalThis.spectrParamDrag && globalThis.spectrParamDrag(false);\n  };'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    if raw.count(escaped(MARKER)) >= 1:
        for label, old, new in EDITS:
            if raw.count(escaped(new)) != 1:
                sys.exit('FAIL %s: document is half patched' % label)
        print('already applied  editor edits record in host automation')
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
