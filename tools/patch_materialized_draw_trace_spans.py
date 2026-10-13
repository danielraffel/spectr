#!/usr/bin/env python3
"""Trace spans around the filter bank's per-frame draw helpers.

WHAT WAS MISSING. A Pro Tools Perfetto capture showed the editor spending
~2.8 ms per frame inside `__flushFrames__` (the rAF callbacks) at 57 fps while
the canvas natives themselves cost ~0.3 ms, and nothing below `raf_flush`
said which draw pass the time went to. Pulp exposes `__traceBegin__(name)` /
`__traceEnd__()` to scripts (a `js`-category span stack the dispatch boundary
force-closes if unbalanced); the bank never called them.

WHAT THIS DOES. `renderAll` wraps each draw helper call in a span named after
the helper. When the hooks are absent (a non-tracing build, the browser
capture) the wrapper is a plain call. Draw order and arguments are unchanged.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

HEAD_OLD = r'''  const renderAll = useCallback(() => {
    const g = getGeom();
    if (!g) return;
'''
HEAD_NEW = r'''  const renderAll = useCallback(() => {
    const g = getGeom();
    if (!g) return;
    // One trace span per draw pass (tools/patch_materialized_draw_trace_spans.py).
    const span = typeof globalThis.__traceBegin__ === "function" && typeof globalThis.__traceEnd__ === "function"
      ? (name, fn) => { globalThis.__traceBegin__(name); try { fn(); } finally { globalThis.__traceEnd__(); } }
      : (name, fn) => fn();
'''

CALLS = [
    ('    drawGrid(sctx, g);', '    span("drawGrid", () => drawGrid(sctx, g));'),
    ('    if (showRulers) drawRulers(sctx, g);', '    if (showRulers) span("drawRulers", () => drawRulers(sctx, g));'),
    ('    drawSpectrum(ctx, g);', '    span("drawSpectrum", () => drawSpectrum(ctx, g));'),
    ('    if (visualizationMode !== "response") drawBands(ctx, g);', '    if (visualizationMode !== "response") span("drawBands", () => drawBands(ctx, g));'),
    ('    if (visualizationMode !== "response") drawModulationExtras(ctx, g);', '    if (visualizationMode !== "response") span("drawModulationExtras", () => drawModulationExtras(ctx, g));'),
    ('    if (visualizationMode !== "bars") drawMaskResponse(ctx, g);', '    if (visualizationMode !== "bars") span("drawMaskResponse", () => drawMaskResponse(ctx, g));'),
    ('    drawRangeOverflow(ctx, g);', '    span("drawRangeOverflow", () => drawRangeOverflow(ctx, g));'),
    ('    drawSelection(octx, g);', '    span("drawSelection", () => drawSelection(octx, g));'),
    ('    drawMarquee(octx, g);', '    span("drawMarquee", () => drawMarquee(octx, g));'),
    ('    drawEdgeWalls(octx, g);', '    span("drawEdgeWalls", () => drawEdgeWalls(octx, g));'),
    ('    drawHover(octx, g);', '    span("drawHover", () => drawHover(octx, g));'),
    ('    if (showMinimap) drawMinimap(octx, g);', '    if (showMinimap) span("drawMinimap", () => drawMinimap(octx, g));'),
    ('    drawHUD(octx, g);', '    span("drawHUD", () => drawHUD(octx, g));'),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if raw.count(encode(HEAD_NEW)) == 1:
        print("draw trace spans: already applied")
        return 0
    for label, old in [("renderAll head", HEAD_OLD)] + [(o.strip(), o) for o, _ in CALLS]:
        count = raw.count(encode(old))
        if count != 1:
            print(f"draw trace spans: anchor `{label}` found {count} times (expected 1)", file=sys.stderr)
            return 1
    raw = raw.replace(encode(HEAD_OLD), encode(HEAD_NEW), 1)
    for old, new in CALLS:
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print(f"draw trace spans: applied ({len(CALLS)} passes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
