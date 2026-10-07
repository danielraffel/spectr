#!/usr/bin/env python3
"""Add detented dB-gutter range scrolling to the materialized Spectr graph.

This is an idempotent source patch. It keeps the existing Range store and
Settings path authoritative, so the gutter only publishes range_set and never
writes gains, viewport, or DSP state.
"""
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"

data = json.loads(PATH.read_text())
html = data["html"]
if "globalThis.spectrRangeWheelAdvance" in html:
    print("already patched", PATH)
    raise SystemExit(0)


def replace_once(old: str, new: str, label: str):
    global html
    count = html.count(old)
    if count == 0:
        raise SystemExit(f"anchor missing: {label}")
    if count > 1:
        raise SystemExit(f"anchor ambiguous ({count}): {label}")
    html = html.replace(old, new, 1)

# Pure helpers are intentionally global so focused tests can exercise the
# stepping/accumulation contract without mounting the full editor.
replace_once(
    "  globalThis.spectrRangeChoices = [3, 6, 12, 24];\n",
    r'''  globalThis.spectrRangeChoices = [3, 6, 12, 24];
  // Gutter range interaction: discrete, symmetric scales, with the same
  // values Settings exposes. A positive direction widens the scale.
  globalThis.spectrRangeStep = (current, direction) => {
    const choices = globalThis.spectrRangeChoices;
    const index = choices.indexOf(current);
    if (index < 0) return choices[choices.length - 1];
    const next = Math.max(0, Math.min(choices.length - 1, index + (direction < 0 ? -1 : 1)));
    return choices[next];
  };
  globalThis.spectrRangeGutterHit = (x, y, inner) => !!inner
    && Number.isFinite(x) && Number.isFinite(y)
    && x >= 0 && x < inner.x
    && y >= inner.y && y <= inner.y + inner.h;
  globalThis.spectrRangeWheelAdvance = (state, delta, discrete, threshold = 48) => {
    const nextState = state || { accumulated: 0 };
    const amount = Number(delta);
    if (!Number.isFinite(amount) || amount === 0) return { state: nextState, steps: 0 };
    const direction = amount > 0 ? 1 : -1;
    if (discrete) {
      nextState.accumulated = 0;
      return { state: nextState, steps: direction };
    }
    if (nextState.accumulated && Math.sign(nextState.accumulated) !== direction)
      nextState.accumulated = 0;
    nextState.accumulated += amount;
    let steps = 0;
    while (Math.abs(nextState.accumulated) >= threshold) {
      const step = nextState.accumulated > 0 ? 1 : -1;
      steps += step;
      nextState.accumulated -= step * threshold;
    }
    return { state: nextState, steps };
  };
  // The current Pulp SDK has no cross-platform haptic bridge. Keep this
  // platform-gated hook graceful and allow a future native bridge to provide
  // the one-tick alignment feedback without changing range behavior.
  globalThis.spectrRangeHaptic = () => {
    if (typeof navigator === "undefined" || !/Mac/i.test(navigator.platform || "")) return;
    const native = globalThis.__pulpNativeBridgeFunctions__;
    const tick = native && (native.hapticAlignmentTick || native.rangeHaptic);
    if (typeof tick === "function") {
      try { tick(); } catch (_) { /* optional feedback must never affect input */ }
    }
  };
''',
    "range helpers",
)

replace_once(
    "  const wheelCommitRef = useRef(0);\n",
    "  const wheelCommitRef = useRef(0);\n  const rangeWheelRef = useRef({ accumulated: 0, lastAt: 0, timer: 0 });\n",
    "range wheel ref",
)

replace_once(
'''    } else if (bandH >= 0 && y >= g.inner.y && y <= g.inner.y + g.inner.h) {
      updatePointerHover({ band: bandH, x, y });
      setCursor("crosshair");
      wrapRef.current.style.cursor = "crosshair";
    } else {
''',
'''    } else if (globalThis.spectrRangeGutterHit(x, y, g.inner)) {
      // The complete numeric dB gutter is interactive, including the space
      // between labels. It never becomes a drag target.
      updatePointerHover(null);
      setCursor("ns-resize");
      wrapRef.current.style.cursor = "ns-resize";
    } else if (bandH >= 0 && y >= g.inner.y && y <= g.inner.y + g.inner.h) {
      updatePointerHover({ band: bandH, x, y });
      setCursor("crosshair");
      wrapRef.current.style.cursor = "crosshair";
    } else {
''',
    "gutter cursor",
)

old = '''  const onWheel = (e) => {
    e.preventDefault();
    if (spectrModalDialogBlocksPlot()) return;
    const g = getGeom();
    if (!g) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const x = (e.clientX - rect.left) * wrapRef.current.clientWidth / rect.width;
    if (x < g.inner.x || x > g.inner.x + g.inner.w) return;
    const liveView = viewRef.current;
'''
new = '''  const onWheel = (e) => {
    e.preventDefault();
    if (spectrModalDialogBlocksPlot()) return;
    const g = getGeom();
    if (!g) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const x = (e.clientX - rect.left) * wrapRef.current.clientWidth / rect.width;
    const y = (e.clientY - rect.top) * wrapRef.current.clientHeight / rect.height;
    if (globalThis.spectrRangeGutterHit(x, y, g.inner) && Math.abs(Number(e.deltaY) || 0) > 0) {
      const wheel = rangeWheelRef.current;
      const now = typeof performance !== "undefined" ? performance.now() : Date.now();
      // A pause marks a new trackpad gesture. Mouse wheels (line/page mode or
      // large pixel deltas) remain one discrete step per event.
      if (wheel.lastAt && now - wheel.lastAt > 180) wheel.accumulated = 0;
      const discrete = e.deltaMode !== 0 || Math.abs(Number(e.deltaY) || 0) >= 40;
      const result = globalThis.spectrRangeWheelAdvance(wheel, e.deltaY, discrete, 48);
      wheel.lastAt = now;
      clearTimeout(wheel.timer);
      wheel.timer = setTimeout(() => { wheel.accumulated = 0; }, 180);
      let remaining = result.steps;
      while (remaining !== 0) {
        const direction = remaining > 0 ? 1 : -1;
        const current = typeof globalThis.spectrRangeDb === "function" ? globalThis.spectrRangeDb() : 24;
        const next = globalThis.spectrRangeStep(current, direction);
        if (next === current) {
          remaining = direction > 0 ? Math.max(0, remaining - 1) : Math.min(0, remaining + 1);
          continue;
        }
        globalThis.spectrSetRangeDb(next, true);
        globalThis.spectrRangeHaptic();
        remaining -= direction;
      }
      return;
    }
    if (x < g.inner.x || x > g.inner.x + g.inner.w) return;
    const liveView = viewRef.current;
'''
replace_once(old, new, "range gutter wheel")

replace_once(
'''      onPointerLeave: () => {
        updatePointerHover(null);
        if (!pointerRef.current || !pointerRef.current.mode)
          setCursor("default");
          wrapRef.current.style.cursor = "default";
      },
''',
'''      onPointerLeave: () => {
        updatePointerHover(null);
        if (!pointerRef.current || !pointerRef.current.mode) {
          setCursor("default");
          wrapRef.current.style.cursor = "default";
        }
      },
''',
    "cursor restore",
)

# The timer is owned by the component and must not survive unmount.
replace_once(
"  useEffect(() => () => clearTimeout(wheelCommitRef.current), []);\n",
"  useEffect(() => () => {\n    clearTimeout(wheelCommitRef.current);\n    clearTimeout(rangeWheelRef.current.timer);\n  }, []);\n",
"range timer cleanup",
)

data["html"] = html
PATH.write_text(json.dumps(data, separators=(",", ":"), ensure_ascii=False) + "\n")
print("patched", PATH)
