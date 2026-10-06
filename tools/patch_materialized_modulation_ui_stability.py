#!/usr/bin/env python3
"""Keep BANDS anchored, draw one knob indicator, and paint the audible viewport.

The display viewport is separate from authored state: modulation cannot become
an editor publication, snapshot, host parameter write, or LFO baseline.
Exact escaped substitutions keep the shipping document diff replayable.
"""
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / 'native-ui/materialized/materialized-document.runtime.json'
EDITS = [
    ('bands label fixed box',
     'const base = { style: { lineHeight: 1, whiteSpace: "nowrap" } };',
     'const base = { style: { lineHeight: 1, whiteSpace: "nowrap", width: 70, flexShrink: 0, textAlign: "center" } };'),
    ('remove stationary knob tick',
     '    if (played !== null) {\n      const b = at(base) * Math.PI / 180, r1 = r0 - 2.2, r2 = r0 + 2.2;\n      tickD = "M " + (c0 + r1 * Math.sin(b)).toFixed(2) + " " + (c0 - r1 * Math.cos(b)).toFixed(2)\n        + " L " + (c0 + r2 * Math.sin(b)).toFixed(2) + " " + (c0 - r2 * Math.cos(b)).toFixed(2);\n    }',
     '    // The moving needle is the sole position indicator; no stationary base tick.'),
    ('separate display viewport',
     '  const viewRef = useRef({ ...initialView });',
     '  const viewRef = useRef({ ...initialView });'),
    ('apply audible viewport before field',
     '      applyModulationFrame: (state) => {\n        const hadViewport = modulationViewportRef.current !== null;\n        modulationViewportRef.current = state.viewport || null;\n        if (hadViewport || state.viewport) wakeDraw();\n        if (!state.active) {',
     '      applyModulationFrame: (state) => {\n        const hadViewport = globalThis.__spectrModulationViewport !== null\n          && globalThis.__spectrModulationViewport !== undefined;\n        globalThis.__spectrModulationViewport = state.viewport || null;\n        if (hadViewport || state.viewport) wakeDraw();\n        if (!state.active) {'),
    ('parse optional audible viewport',
     'function parseSpectrModulationFrame(payload) {\n  const n =',
     'function parseSpectrModulationFrame(payload) {\n  let viewport = null;\n  if (payload && payload.viewport_on === true) {\n    const lo = Number(payload.min_hz), hi = Number(payload.max_hz);\n    if (!Number.isFinite(lo) || !Number.isFinite(hi) || lo < 20 || hi > 20001 || hi <= lo) return null;\n    viewport = { lmin: Math.log10(lo), lmax: Math.log10(hi) };\n  }\n  const n ='),
    ('carry parsed viewport',
     '    gains: gainDb.map((db, index) => muted[index] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),\n  };',
     '    gains: gainDb.map((db, index) => muted[index] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),\n    viewport,\n  };'),
]
# All frequency-dependent painters use the audible projection. Input handlers,
# publications and snapshots continue reading the canonical viewRef.
for anchor in ['  const renderAll = useCallback(() => {',
               '  const bandFreqRange = (i) => {', '  const bandCenterFreq = (i) => {',
               '  function drawGrid(ctx, g) {', '  function drawSpectrum(ctx, g) {',
               '  function drawRulers(ctx, g) {', '  function drawBands(ctx, g) {',
               '  function drawMinimap(ctx, g) {']:
    EDITS.append(('paint viewport in '+anchor.strip(), anchor,
                  anchor+'\n    const view = globalThis.__spectrModulationViewport || viewRef.current;'))

def escaped(s):
    return json.dumps(s, ensure_ascii=False)[1:-1]

def main():
    raw = PATH.read_text()
    for label, old, new in EDITS:
        a, b = escaped(old), escaped(new)
        if b in raw and a not in raw.replace(b, ''):
            continue
        if raw.count(a) != 1:
            raise SystemExit(f'{label}: expected one anchor, got {raw.count(a)}')
        raw = raw.replace(a, b, 1)
    json.loads(raw)
    PATH.write_text(raw)

if __name__ == '__main__':
    main()
