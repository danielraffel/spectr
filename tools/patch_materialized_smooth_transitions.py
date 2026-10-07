#!/usr/bin/env python3
"""Make band-count and vertical-range visual changes settle instead of flashing."""
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
data = json.loads(PATH.read_text())
html = data["html"]
if "bandTransitionCanvasRef" in html:
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
'''  const overlayRef = useRef(null);
  // The plot's static layer: background, grid and rulers, repainted only
''',
'''  const overlayRef = useRef(null);
  // A one-frame snapshot lets a band-count change crossfade its previous
  // geometry over the new layout. The snapshot is paint-only and never enters
  // the processor or the authored gain arrays.
  const bandTransitionCanvasRef = useRef(null);
  const bandTransitionRef = useRef(null);
  // The plot's static layer: background, grid and rulers, repainted only
''',
"band transition refs",
)
repl(
'''      const st = staticRef.current;
      staticKeyRef.current = "";
      if (!wrap || !c) return;
''',
'''      const st = staticRef.current;
      const bt = bandTransitionCanvasRef.current;
      staticKeyRef.current = "";
      if (!wrap || !c) return;
''',
"resize transition canvas local",
)
repl(
'''      for (const cv of [c, o, st]) {
''',
'''      for (const cv of [c, o, st, bt]) {
''',
"resize transition canvas",
)
repl(
'''    setGains((prev) => {
      if (prev.length === N) return prev;
''',
'''    const source = canvasRef.current;
    const fade = bandTransitionCanvasRef.current;
    // A modulated band count can arrive every frame. Snapshotting the whole
    // plot for each arrival continually reintroduces stale geometry and reads
    // as a flash. Only non-modulated structural changes get one snapshot, and
    // an active fade is never restarted.
    if (source && fade && source.width > 0 && source.height > 0
        && !modulationActiveRef.current && !bandTransitionRef.current) {
      fade.width = source.width;
      fade.height = source.height;
      fade.style.width = source.style.width;
      fade.style.height = source.style.height;
      const fctx = fade.getContext("2d");
      fctx.setTransform(1, 0, 0, 1, 0, 0);
      fctx.clearRect(0, 0, fade.width, fade.height);
      fctx.drawImage(source, 0, 0);
      fade.style.opacity = "1";
      bandTransitionRef.current = { startedAt: performance.now(), duration: 180 };
    }
    setGains((prev) => {
      if (prev.length === N) return prev;
''',
"band count snapshot",
)
repl(
'''      // Read AFTER the paint. Both of these are analyzer-DERIVED
      // state with its own hold and decay, so both keep moving for
''',
'''      const bandTransition = bandTransitionRef.current;
      if (bandTransition) busy = true;
      if (rangeTransitionRef.current) busy = true;
      // Read AFTER the paint. Both of these are analyzer-DERIVED
      // state with its own hold and decay, so both keep moving for
''',
"transition keeps draw loop awake",
)
repl(
'''    const g = getGeom();
    if (!g) return;
    // Reset here and cleared below by any analyzer-derived paint that
''',
'''    const g = getGeom();
    if (!g) return;
    const rangeTransition = rangeTransitionRef.current;
    let visualRange = typeof globalThis.spectrRangeDb === "function" ? globalThis.spectrRangeDb() : 24;
    if (rangeTransition) {
      const t = Math.max(0, Math.min(1, (performance.now() - rangeTransition.startedAt) / rangeTransition.duration));
      const eased = 1 - Math.pow(1 - t, 3);
      visualRange = rangeTransition.from + (rangeTransition.to - rangeTransition.from) * eased;
      rangeVisualRef.current = visualRange;
      if (t >= 1) {
        visualRange = rangeTransition.to;
        rangeVisualRef.current = visualRange;
        rangeTransitionRef.current = null;
      }
    } else {
      rangeVisualRef.current = visualRange;
    }
    g.rulerRange = visualRange;
    const bandTransition = bandTransitionRef.current;
    if (bandTransition) {
      const t = Math.max(0, Math.min(1, (performance.now() - bandTransition.startedAt) / bandTransition.duration));
      const fade = bandTransitionCanvasRef.current;
      if (fade) fade.style.opacity = String(1 - t);
      if (t >= 1) {
        if (fade) {
          const fctx = fade.getContext("2d");
          fctx.setTransform(1, 0, 0, 1, 0, 0);
          fctx.clearRect(0, 0, fade.width, fade.height);
          fade.style.opacity = "0";
        }
        bandTransitionRef.current = null;
      }
    }
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
    /* @__PURE__ */ React.createElement("canvas", { ref: bandTransitionCanvasRef, style: { position: "absolute", inset: 0, pointerEvents: "none", zIndex: 1, opacity: 0 } }),
    /* @__PURE__ */ React.createElement("canvas", { ref: overlayRef, style: { position: "absolute", inset: 0, pointerEvents: "none", zIndex: 2 } }),
''',
"transition canvas JSX",
)
data["html"] = html
PATH.write_text(json.dumps(data, separators=(",", ":"), ensure_ascii=False) + "\n")
print("patched", PATH)
