#!/usr/bin/env python3
"""Paint the band plot's static layer on its own canvas, only when it changes.

`renderAll` repainted the plot's background gradient, frequency/dB grid and
rulers on every frame the draw loop ran, although they depend only on the
view, the plot geometry, the band count, the theme, the rulers setting and the
analyzer's dB scale -- none of which moves while audio plays or a band is
drawn.

They now live on a separate canvas behind the band canvas. It is appended as
the LAST child of the plot surface, so no captured sibling path moves, and
drawn in an explicit stacking order: static grid/background at 0, dynamic bands at 1, and interaction overlay at 2. renderAll repaints it only when its
inputs change (compared as one key string), and clears the band canvas to
transparent instead of painting the background into it. The resize handler
sizes the new canvas with the other two.

The canvas mounts after the first render (a flag set by a mount effect), so
it takes a native widget id after every captured node's: appending it at
mount would renumber the auto-assigned ids of everything created after it,
which hit-target probes and tests address. renderAll sizes it to the band
canvas the first time it sees it, since the resize handler ran before it
existed.

This is a Spectr-side stopgap. The intended long-term replacement is a
retained layer in the SAME canvas: the SDK's Canvas::begin_layer / end_layer /
draw_layer exposed to the Canvas2D API as a keyed cached group, so the grid
and rulers are recorded once and replayed in place with the original blending
order, with no second canvas at all.

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

EDITS = [
    ('the static layer has a ref',
     '  const overlayRef = useRef(null);\n',
     '  const overlayRef = useRef(null);\n'
     '  // The plot\'s static layer: background, grid and rulers, repainted only\n'
     '  // when the key of everything they read changes.\n'
     '  const staticRef = useRef(null);\n'
     '  const staticKeyRef = useRef("");\n'
     '  // Mounted after the first render, so the canvas takes a native id after\n'
     '  // every captured node\'s and no existing widget id moves.\n'
     '  const [staticMounted, setStaticMounted] = useState(false);\n'
     '  useEffect(() => { setStaticMounted(true); }, []);\n'),

    ('the resize handler sizes the static canvas too',
     '      const o = overlayRef.current;\n'
     '      if (!wrap || !c) return;\n',
     '      const o = overlayRef.current;\n'
     '      const st = staticRef.current;\n'
     '      staticKeyRef.current = "";\n'
     '      if (!wrap || !c) return;\n'),
    ('the static canvas is resized with the others',
     '      for (const cv of [c, o]) {\n',
     '      for (const cv of [c, o, st]) {\n'),

    ('renderAll paints the static layer only when it changes',
     '    ctx.clearRect(0, 0, w, h);\n'
     '    const bg = ctx.createLinearGradient(0, 0, 0, h);\n'
     '    bg.addColorStop(0, "rgba(8,12,18,0.0)");\n'
     '    bg.addColorStop(1, "rgba(0,0,0,0.35)");\n'
     '    ctx.fillStyle = bg;\n'
     '    ctx.fillRect(0, 0, w, h);\n'
     '    drawGrid(ctx, g);\n'
     '    drawSpectrum(ctx, g);\n'
     '    if (showRulers) drawRulers(ctx, g);\n',
     '    const staticCanvas = staticRef.current;\n'
     '    const scale = window.SpectrAnalyzer && window.SpectrAnalyzer.scale\n'
     '      ? window.SpectrAnalyzer.scale() : { floor: 0, ceiling: 0 };\n'
     '    const staticKey = [w, h, zeroY, halfH, view.lmin, view.lmax, N, theme,\n'
     '      showRulers, scale.floor, scale.ceiling].join("|");\n'
     '    if (staticCanvas && canvasRef.current\n'
     '        && (staticCanvas.width !== canvasRef.current.width\n'
     '          || staticCanvas.height !== canvasRef.current.height)) {\n'
     '      const dpr = Math.min(2, window.devicePixelRatio || 1);\n'
     '      staticCanvas.width = canvasRef.current.width;\n'
     '      staticCanvas.height = canvasRef.current.height;\n'
     '      staticCanvas.style.width = w + "px";\n'
     '      staticCanvas.style.height = h + "px";\n'
     '      staticCanvas.getContext("2d").setTransform(dpr, 0, 0, dpr, 0, 0);\n'
     '      staticKeyRef.current = "";\n'
     '    }\n'
     '    if (staticCanvas && staticKeyRef.current !== staticKey) {\n'
     '      staticKeyRef.current = staticKey;\n'
     '      const sctx = staticCanvas.getContext("2d");\n'
     '      sctx.clearRect(0, 0, w, h);\n'
     '      const bg = sctx.createLinearGradient(0, 0, 0, h);\n'
     '      bg.addColorStop(0, "rgba(8,12,18,0.0)");\n'
     '      bg.addColorStop(1, "rgba(0,0,0,0.35)");\n'
     '      sctx.fillStyle = bg;\n'
     '      sctx.fillRect(0, 0, w, h);\n'
     '      drawGrid(sctx, g);\n'
     '      if (showRulers) drawRulers(sctx, g);\n'
     '    }\n'
     '    ctx.clearRect(0, 0, w, h);\n'
     '    drawSpectrum(ctx, g);\n'),

    ('the dynamic canvas has an explicit middle stacking layer',
     '    /* @__PURE__ */ React.createElement("canvas", { "data-spectr-filter-canvas": true, ref: canvasRef, style: { position: "absolute", inset: 0 } }),\n',
     '    /* @__PURE__ */ React.createElement("canvas", { "data-spectr-filter-canvas": true, ref: canvasRef, style: { position: "absolute", inset: 0, zIndex: 1 } }),\n'),

    ('the static canvas is the plot surface\'s last child, drawn behind',
     '    /* @__PURE__ */ React.createElement("canvas", { ref: overlayRef, style: { position: "absolute", inset: 0, pointerEvents: "none" } }),\n',
     '    /* @__PURE__ */ React.createElement("canvas", { ref: overlayRef, style: { position: "absolute", inset: 0, pointerEvents: "none", zIndex: 2 } }),\n'
     '    staticMounted && /* @__PURE__ */ React.createElement("canvas", { "data-spectr-static-canvas": true, ref: staticRef, style: { position: "absolute", inset: 0, pointerEvents: "none", zIndex: 0 } }),\n'),
]


FILTER_CANVAS_OLD = ('/* @__PURE__ */ React.createElement("canvas", '
                     '{ "data-spectr-filter-canvas": true, ref: canvasRef, style: '
                     '{ position: "absolute", inset: 0 } })')
FILTER_CANVAS_NEW = FILTER_CANVAS_OLD.replace('inset: 0 }', 'inset: 0, zIndex: 1 }')
OVERLAY_CANVAS_OLD = ('/* @__PURE__ */ React.createElement("canvas", '
                      '{ ref: overlayRef, style: '
                      '{ position: "absolute", inset: 0, pointerEvents: "none" } })')
OVERLAY_CANVAS_NEW = OVERLAY_CANVAS_OLD.replace('pointerEvents: "none" }',
                                                  'pointerEvents: "none", zIndex: 2 }')
STATIC_CANVAS_OLD = ('staticMounted && /* @__PURE__ */ React.createElement("canvas", '
                     '{ "data-spectr-static-canvas": true, ref: staticRef, style: '
                     '{ position: "absolute", inset: 0, pointerEvents: "none", '
                     'zIndex: -1 } })')
STATIC_CANVAS_NEW = STATIC_CANVAS_OLD.replace('zIndex: -1', 'zIndex: 0')

def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    document = json.loads(raw)
    html = document.get('html')
    if not isinstance(html, str):
        sys.exit('FAIL: the document has no html payload')
    old_count = html.count(STATIC_CANVAS_OLD)
    current_count = html.count(STATIC_CANVAS_NEW)
    filter_old_count = html.count(FILTER_CANVAS_OLD)
    overlay_old_count = html.count(OVERLAY_CANVAS_OLD)
    if (current_count == 1 or old_count == 1) and filter_old_count == 1 and overlay_old_count == 1:
        html = html.replace(STATIC_CANVAS_OLD, STATIC_CANVAS_NEW, 1)
        html = html.replace(FILTER_CANVAS_OLD, FILTER_CANVAS_NEW, 1)
        html = html.replace(OVERLAY_CANVAS_OLD, OVERLAY_CANVAS_NEW, 1)
        document['html'] = html
        open(PATH, 'w', encoding='utf-8').write(json.dumps(document, separators=(',', ':')))
        print('migrated        explicit static, dynamic and overlay canvas stacking order')
        return 0
    if current_count == 1 and old_count == 0 and filter_old_count == 0 and overlay_old_count == 0:
        print('already current  explicit canvas stacking order is present')
        return 0
    if old_count != 0 or current_count != 0:
        sys.exit('FAIL: static canvas z-index patch point is ambiguous')
    if raw.count(escaped('"data-spectr-static-canvas": true')) == 1:
        print('already applied  the plot\'s static layer has its own canvas')
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
