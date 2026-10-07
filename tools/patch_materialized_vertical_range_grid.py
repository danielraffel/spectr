#!/usr/bin/env python3
"""Make vertical Range changes legible without adding persistent controls."""
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
data = json.loads(PATH.read_text())
html = data["html"]
marker = "  globalThis.spectrRangeGridTicks ="
if marker in html:
    print("already patched", PATH)
    raise SystemExit(0)

def replace_once(old: str, new: str, label: str):
    global html
    count = html.count(old)
    if count != 1:
        raise SystemExit(f"anchor {label!r} count={count}")
    html = html.replace(old, new, 1)

replace_once(
'''  globalThis.spectrRangeDb = () => {\n''',
'''  // Pure tick projection shared by the painter and focused tests. Major\n  // ticks match the ruler labels; minor ticks provide the visual movement\n  // between them while the eased Range transition is in flight.\n  globalThis.spectrRangeGridTicks = (range) => {\n    const r = Number(range);\n    if (!Number.isFinite(r) || r <= 0) return { major: [], minor: [] };\n    const make = (step) => {\n      const values = [];\n      for (let db = -r; db <= r + 1e-9; db += step)\n        values.push(Number(db.toFixed(6)));\n      return values;\n    };\n    return { major: make(r / 4), minor: make(r / 8) };\n  };\n  globalThis.spectrRangeDb = () => {\n''',
"range grid tick helper")

old = '''    ctx.strokeStyle = "rgba(255,255,255,0.05)";\n    const gridRange = Number.isFinite(g.rulerRange)\n      ? g.rulerRange : (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24);\n    for (let db = -gridRange; db <= gridRange; db += gridRange / 4) {\n      const y = g.zeroY - db / gridRange * (g.plotHalfH ?? g.halfH) + 0.5;\n      if (y < g.inner.y || y > g.inner.y + g.inner.h) continue;\n      ctx.beginPath();\n      ctx.moveTo(g.inner.x, y);\n      ctx.lineTo(g.inner.x + g.inner.w, y);\n      ctx.stroke();\n    }\n    ctx.strokeStyle = "rgba(255,255,255,0.20)";\n'''
new = '''    const gridRange = Number.isFinite(g.rulerRange)\n      ? g.rulerRange : (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24);\n    const ticks = typeof globalThis.spectrRangeGridTicks === 'function'\n      ? globalThis.spectrRangeGridTicks(gridRange)\n      : { major: [], minor: [] };\n    const plotHalfH = g.plotHalfH ?? g.halfH;\n    const yForDb = (db) => g.zeroY - db / gridRange * plotHalfH + 0.5;\n    // Minor divisions make the scale visibly travel during a vertical zoom.\n    ctx.strokeStyle = "rgba(255,255,255,0.035)";\n    for (const db of ticks.minor) {\n      if (ticks.major.includes(db) || Math.abs(db) < 1e-9) continue;\n      const y = yForDb(db);\n      if (y < g.inner.y || y > g.inner.y + g.inner.h) continue;\n      ctx.beginPath();\n      ctx.moveTo(g.inner.x, y);\n      ctx.lineTo(g.inner.x + g.inner.w, y);\n      ctx.stroke();\n    }\n    // Major divisions align with the numeric labels and get a little more\n    // contrast than the frequency grid, while remaining understated.\n    ctx.strokeStyle = "rgba(255,255,255,0.095)";\n    for (const db of ticks.major) {\n      if (Math.abs(db) < 1e-9) continue;\n      const y = yForDb(db);\n      if (y < g.inner.y || y > g.inner.y + g.inner.h) continue;\n      ctx.beginPath();\n      ctx.moveTo(g.inner.x, y);\n      ctx.lineTo(g.inner.x + g.inner.w, y);\n      ctx.stroke();\n      ctx.beginPath();\n      ctx.moveTo(g.inner.x - 5, y);\n      ctx.lineTo(g.inner.x, y);\n      ctx.stroke();\n    }\n    ctx.strokeStyle = "rgba(255,255,255,0.20)";\n'''
replace_once(old, new, "vertical dB grid")

data["html"] = html
PATH.write_text(json.dumps(data, separators=(",", ":"), ensure_ascii=False) + "\n")
print("patched", PATH)
