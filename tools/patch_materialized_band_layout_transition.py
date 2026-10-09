#!/usr/bin/env python3
"""Cross-fade the painted plot when an LFO changes the displayed band count.

The audio renderer already restages a 32/64 mask click-free.  The editor used
to replace the canvas geometry in one paint, so the same modulation frame was
visually re-packed into a new set of columns.  Keep the previous plot bitmap
for one short visual transition and composite it over the new geometry while
the new modulation frames continue underneath.
"""
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "__spectrBandLayoutTransition"

doc = json.loads(PATH.read_text())
html = doc["html"]
if MARKER in html:
    print("already applied")
    raise SystemExit(0)

old_refs = """  const canvasRef = useRef(null);\n  // How the bands animate while an LFO moves them -- drawing only. See\n"""
new_refs = """  const canvasRef = useRef(null);\n  // A band-count modulation changes the column packing. Preserve the last\n  // painted plot and blend it out over the new packing so the modulation\n  // frame itself is never reset or exposed as a one-frame jump.\n  const bandLayoutTransitionRef = useRef(null);\n  globalThis.__spectrBeginBandLayoutTransition = (from, to) => {\n    if (from === to) return;\n    const source = canvasRef.current;\n    if (!source || !source.width || !source.height) return;\n    const snapshot = document.createElement("canvas");\n    snapshot.width = source.width;\n    snapshot.height = source.height;\n    const sctx = snapshot.getContext("2d");\n    if (!sctx) return;\n    sctx.drawImage(source, 0, 0);\n    bandLayoutTransitionRef.current = {\n      snapshot, startedAt: performance.now(), duration: 180, from, to\n    };\n    if (typeof globalThis.__spectrPlotWakeDraw === "function")\n      globalThis.__spectrPlotWakeDraw();\n  };\n  // How the bands animate while an LFO moves them -- drawing only. See\n"""
if html.count(old_refs) != 1:
    raise SystemExit("canvas ref anchor missing or ambiguous")
html = html.replace(old_refs, new_refs, 1)

old_freeze = """      store.display = shown;\n      // A band count the Bands target moves changes several times a second:\n"""
new_freeze = """      const previousBands = was.bands;\n      store.display = shown;\n      if (previousBands > 0 && shown.bands > 0 && previousBands !== shown.bands\n          && typeof globalThis.__spectrBeginBandLayoutTransition === "function")\n+        globalThis.__spectrBeginBandLayoutTransition(previousBands, shown.bands);\n      // A band count the Bands target moves changes several times a second:\n"""
if html.count(old_freeze) != 1:
    raise SystemExit("freeze display anchor missing or ambiguous")
html = html.replace(old_freeze, new_freeze, 1)

old_render = """    drawRangeOverflow(ctx, g);\n    octx.clearRect(0, 0, w, h);\n"""
new_render = """    drawRangeOverflow(ctx, g);\n    const bandLayoutTransition = bandLayoutTransitionRef.current;\n    if (bandLayoutTransition) {\n      const t = Math.max(0, Math.min(1,\n        (performance.now() - bandLayoutTransition.startedAt)\n          / bandLayoutTransition.duration));\n      if (t < 1) {\n        ctx.save();\n        ctx.globalAlpha = 1 - (1 - Math.pow(1 - t, 3));\n        ctx.drawImage(bandLayoutTransition.snapshot, 0, 0, w, h);\n        ctx.restore();\n      } else {\n        bandLayoutTransitionRef.current = null;\n      }\n    }\n    octx.clearRect(0, 0, w, h);\n"""
if html.count(old_render) != 1:
    raise SystemExit("render anchor missing or ambiguous")
html = html.replace(old_render, new_render, 1)

old_busy = """      if (rangeTransitionRef.current) busy = true;\n      // Read AFTER the paint. Both of these are analyzer-DERIVED\n"""
new_busy = """      if (rangeTransitionRef.current) busy = true;\n      if (typeof bandLayoutTransitionRef !== "undefined" && bandLayoutTransitionRef.current) busy = true;\n      // Read AFTER the paint. Both of these are analyzer-DERIVED\n"""
if html.count(old_busy) != 1:
    raise SystemExit("busy anchor missing or ambiguous")
html = html.replace(old_busy, new_busy, 1)

doc["html"] = html
PATH.write_text(json.dumps(doc, ensure_ascii=False, separators=(",", ":")))
print("applied")
