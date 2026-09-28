#!/usr/bin/env python3
"""A host automation change re-renders nothing that did not change.

Every host parameter change that reaches the editor arrives as one live-state
projection, and every projection carries the whole modulation block. The
modulation state hook answered each one with
`setValue(current => ({ ...current, ...next }))` -- a new object every time,
whether or not any field moved -- and the hook is mounted by the Settings
panel, which stays mounted while closed. So a viewport or LFO automation
change re-rendered the Settings panel, and in this captured import a React
commit re-applies captured metadata across the whole document: Perfetto
measured `spectr_host_automation_project` at 22-127 ms per host change.

Two rules, applied here:

  * A projection never calls a React setter with a value equal to the one it
    holds. The hook compares every field it would merge and keeps the current
    object when none moved, so React bails out.
  * A consumer that is not showing its state does not follow the live
    projection. The Settings panel passes `listening: open`; while closed it
    does not subscribe, and when it opens it catches up once from the last
    native modulation block, which `spectrRecordModulationFrame` now keeps
    (every projection records it before anything else reads it).

The viewport and band parts of a projection go through refs (the bank's
`applyHostAutomationState`) with no setter. The viewport is now published the
way a drag publishes it: live on every change (the zoom readout ignores live
notifications; the minimap paints the value every frame), and settled once,
120 ms after the last host change, which commits the readout once per burst
instead of once per change. A projection that does not move the viewport
publishes nothing.

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

EDITS = [
    ('the modulation hook can stop listening',
     'function useSpectrModulationState() {\n'
     '  // Seeded from the last frame native sent',
     'function useSpectrModulationState() {\n'
     '  // Optional: a consumer that is not showing its state passes false.\n'
     '  const listening = arguments.length > 0 ? arguments[0] !== false : true;\n'
     '  // Seeded from the last frame native sent'),

    ('an unchanged modulation projection keeps the current state',
     '    setValue((current) => ({ ...current, ...next }));\n  }, []);\n',
     '    // A projection that moves nothing must not re-render: each host\n'
     '    // automation change republishes the whole modulation block.\n'
     '    setValue((current) => Object.keys(next).every(\n'
     '      (key) => current[key] === next[key]) ? current : { ...current, ...next });\n'
     '  }, []);\n'),

    ('a consumer that is not showing follows nothing, and catches up when it shows',
     '  React.useEffect(() => {\n'
     '    if (!spectrModulationBridge || typeof spectrModulationBridge.on !== "function") return undefined;\n'
     '    const accept = (message) => {\n',
     '  React.useEffect(() => {\n'
     '    if (!spectrModulationBridge || typeof spectrModulationBridge.on !== "function") return undefined;\n'
     '    if (!listening) return undefined;\n'
     '    if (globalThis.__spectrModulationLastNative)\n'
     '      readNativeModulation(globalThis.__spectrModulationLastNative);\n'
     '    const accept = (message) => {\n'),

    ('the live subscription follows the listening flag',
     '      if (typeof unsubscribeLive === "function") unsubscribeLive();\n'
     '    };\n'
     '  }, [spectrModulationBridge, readNativeModulation]);\n',
     '      if (typeof unsubscribeLive === "function") unsubscribeLive();\n'
     '    };\n'
     '  }, [spectrModulationBridge, readNativeModulation, listening]);\n'),

    ('the Settings modulation panel listens only while shown',
     'function SpectrModulationSettings() {\n'
     '  const { value, publish, publishTargetMask, publishMorphViewport, publishTargets } = useSpectrModulationState();\n',
     'function SpectrModulationSettings() {\n'
     '  const listening = !arguments[0] || arguments[0].listening !== false;\n'
     '  const { value, publish, publishTargetMask, publishMorphViewport, publishTargets } = useSpectrModulationState(listening);\n'),

    ('Settings passes whether it is open',
     'React.createElement(SpectrModulationSettings, null)',
     'React.createElement(SpectrModulationSettings, { listening: open })'),

    ('the last native modulation block is kept for a late listener',
     'window.spectrRecordModulationFrame = (modulation) => {\n'
     '  const next = spectrModulationFromNative(modulation);\n',
     'window.spectrRecordModulationFrame = (modulation) => {\n'
     '  if (modulation && typeof modulation === "object")\n'
     '    globalThis.__spectrModulationLastNative = modulation;\n'
     '  const next = spectrModulationFromNative(modulation);\n'),

    ('a host viewport change publishes live and settles once the burst ends',
     '        viewRef.current.lmin = Math.log10(state.minHz);\n'
     '        viewRef.current.lmax = Math.log10(state.maxHz);\n'
     '        notifyViewportListeners(false);\n',
     '        const hostViewMoved = viewRef.current.lmin !== Math.log10(state.minHz)\n'
     '          || viewRef.current.lmax !== Math.log10(state.maxHz);\n'
     '        viewRef.current.lmin = Math.log10(state.minHz);\n'
     '        viewRef.current.lmax = Math.log10(state.maxHz);\n'
     '        // Like a drag: published live (the minimap paints the value each\n'
     '        // frame), settled once when the automation burst pauses. Settling\n'
     '        // every change committed the zoom readout per host change.\n'
     '        if (hostViewMoved) {\n'
     '          notifyViewportListeners(true);\n'
     '          clearTimeout(hostViewSettleRef.current);\n'
     '          hostViewSettleRef.current = setTimeout(settleViewport, 120);\n'
     '        }\n'),

    ('the host viewport settle timer',
     '  const wheelCommitRef = useRef(0);\n'
     '  const viewportListenersRef = useRef(null);\n',
     '  const wheelCommitRef = useRef(0);\n'
     '  const hostViewSettleRef = useRef(0);\n'
     '  const viewportListenersRef = useRef(null);\n'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    for label, old, new in EDITS:
        if old in new:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)
    raw = open(PATH, encoding='utf-8').read()
    applied = already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(old_e) == 0 and raw.count(new_e) == 1:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        applied += 1
        print('applied         ', label)
    if already and applied:
        sys.exit('FAIL: the document is half patched; refusing to write')
    if not applied:
        return 0
    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
