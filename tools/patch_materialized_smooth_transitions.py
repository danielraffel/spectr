#!/usr/bin/env python3
"""Make band-count and vertical-range visual changes settle instead of flashing."""
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
data = json.loads(PATH.read_text())
html = data["html"]
if "rangeVisualRef" in html:
    print("already patched", PATH)
    raise SystemExit(0)

def repl(old, new, label):
    global html
    n = html.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected 1 anchor, found {n}")
    html = html.replace(old, new, 1)

repl(
'''  const [, setRangeDb] = React.useState(0);
  React.useEffect(() => typeof globalThis.spectrOnRange === 'function'
    ? globalThis.spectrOnRange((db) => setRangeDb(db)) : void 0, []);
''',
'''  const [, setRangeDb] = React.useState(0);
  const rangeVisualRef = useRef(typeof globalThis.spectrRangeDb === "function" ? globalThis.spectrRangeDb() : 24);
  const rangeTransitionRef = useRef(null);
  React.useEffect(() => typeof globalThis.spectrOnRange === 'function'
    ? globalThis.spectrOnRange((db) => {
        const from = Number.isFinite(rangeVisualRef.current) ? rangeVisualRef.current : db;
        if (from !== db) {
          rangeTransitionRef.current = { from, to: db, startedAt: performance.now(), duration: 180 };
          globalThis.__spectrPlotWakeDraw && globalThis.__spectrPlotWakeDraw();
        }
        setRangeDb(db);
      }) : void 0, []);
''',
"range visual transition",
)
repl(
'''      // Read AFTER the paint. Both of these are analyzer-DERIVED
      // state with its own hold and decay, so both keep moving for
''',
'''      if (typeof rangeTransitionRef !== "undefined" && rangeTransitionRef.current) busy = true;
      // Read AFTER the paint. Both of these are analyzer-DERIVED
      // state with its own hold and decay, so both keep moving for
''',
"range transition keeps draw loop awake",
)
repl(
'''    const g = getGeom();
    if (!g) return;
    // Reset here and cleared below by any analyzer-derived paint that
''',
'''    const g = getGeom();
    if (!g) return;
    const rangeTransition = typeof rangeTransitionRef !== "undefined" ? rangeTransitionRef.current : null;
    let visualRange = typeof globalThis.spectrRangeDb === "function" ? globalThis.spectrRangeDb() : 24;
    if (rangeTransition) {
      const t = Math.max(0, Math.min(1, (performance.now() - rangeTransition.startedAt) / rangeTransition.duration));
      const eased = 1 - Math.pow(1 - t, 3);
      visualRange = rangeTransition.from + (rangeTransition.to - rangeTransition.from) * eased;
      rangeVisualRef.current = visualRange;
      if (t >= 1) {
        visualRange = rangeTransition.to;
        rangeVisualRef.current = visualRange;
        if (typeof rangeTransitionRef !== "undefined") rangeTransitionRef.current = null;
      }
    } else {
      rangeVisualRef.current = visualRange;
    }
    g.rulerRange = visualRange;
    // Reset here and cleared below by any analyzer-derived paint that
''',
"render transition progression",
)
repl(
'''    const staticKey = [w, h, zeroY, halfH, view.lmin, view.lmax, N, theme,
''',
'''    const staticKey = [w, h, zeroY, halfH, g.rulerRange, view.lmin, view.lmax, N, theme,
''',
"static key visual range",
)
repl(
'''    const gridRange = (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24);
''',
'''    const gridRange = Number.isFinite(g.rulerRange)
      ? g.rulerRange : (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24);
''',
"animated vertical grid range",
)
repl(
'''    const rulerRange = (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24);
''',
'''    const rulerRange = Number.isFinite(g.rulerRange)
      ? g.rulerRange : (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24);
''',
"animated vertical ruler range",
)
repl(
'''    /* @__PURE__ */ React.createElement("canvas", { "data-spectr-filter-canvas": true, ref: canvasRef, style: { position: "absolute", inset: 0 } }),
    /* @__PURE__ */ React.createElement("canvas", { ref: overlayRef, style: { position: "absolute", inset: 0, pointerEvents: "none" } }),
''',
'''    /* @__PURE__ */ React.createElement("canvas", { "data-spectr-filter-canvas": true, ref: canvasRef, style: { position: "absolute", inset: 0 } }),
    /* @__PURE__ */ React.createElement("canvas", { ref: overlayRef, style: { position: "absolute", inset: 0, pointerEvents: "none", zIndex: 1 } }),
''',
"overlay canvas stacking",
)
data["html"] = html
PATH.write_text(json.dumps(data, separators=(",", ":"), ensure_ascii=False) + "\n")
print("patched", PATH)
