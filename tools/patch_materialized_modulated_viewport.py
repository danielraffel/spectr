#!/usr/bin/env python3
"""Draw the viewport an LFO is moving.

THE DEFECT
  The audio owner publishes the post-LFO window (`slot.viewport`) with every
  modulated field, but the editor's `modulation_frame` carried only band gains
  and mutes. Band shift and Band spread were audible and invisible in every
  native format: the bars moved, the window they sit in never did.

THE FIX
  The frame now carries `viewport_driven`, `min_hz`, `max_hz` (evaluated at
  display time like the control pulls). The bank keeps the drawn window in a
  paint-only ref and `renderAll` swaps it into the live view object for the
  duration of one paint pass, restoring the user's window before returning.
  Nothing a publication reads is touched: the swap is synchronous and the
  canonical values are back before any handler runs, so a user edit made
  while the LFO runs still publishes the window the user set. The falling
  edge clears the ref, so the paint returns to the user's window with the
  overlay release.

Idempotent raw-text surgery on the materialized document; re-running is a
no-op once applied.
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DOC = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"

def encode(text):
    return json.dumps(text)[1:-1]

EDITS = [
    # 1. parse the optional drawn viewport
    ("""  return {
    n,
    active,
    muted: muted.slice(),
    gains: gainDb.map((db, index) => muted[index] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
  };
}
function parseSpectrNativeLiveState(payload) {""",
     """  // The window the LFO is moving. Optional: an older producer, or a frame
  // from a target that leaves the window alone, carries none.
  let viewport = null;
  if (payload.viewport_driven === true) {
    const minHz = Number(payload.min_hz);
    const maxHz = Number(payload.max_hz);
    if (!Number.isFinite(minHz) || !Number.isFinite(maxHz) || minHz <= 0 || maxHz <= minHz) return null;
    viewport = { lmin: Math.log10(minHz), lmax: Math.log10(maxHz) };
  }
  return {
    n,
    active,
    viewport,
    muted: muted.slice(),
    gains: gainDb.map((db, index) => muted[index] ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
  };
}
function parseSpectrNativeLiveState(payload) {"""),
    # 2. the paint-only ref
    ("""  const modulationActiveRef = useRef(false);
""",
     """  const modulationActiveRef = useRef(false);
  // The window an LFO is drawing, paint only. Never written into viewRef:
  // a publication reads the live view, and the LFO's window must not be
  // mistaken for a user edit.
  const modulationViewRef = useRef(null);
"""),
    # 3a. falling edge clears it
    ("""        if (!state.active) {
          if (!modulationActiveRef.current) return false;
          modulationActiveRef.current = false;
          modTargetRef.current = null;
          trailRef.current = [];
""",
     """        if (!state.active) {
          modulationViewRef.current = null;
          if (!modulationActiveRef.current) return false;
          modulationActiveRef.current = false;
          modTargetRef.current = null;
          trailRef.current = [];
"""),
    # 3b. active frame carries the window (or null when the target is not routed)
    ("""        const wasModulating = modulationActiveRef.current;
        modulationActiveRef.current = true;
        const modulated = state.gains.map((value, index) => state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);
""",
     """        const wasModulating = modulationActiveRef.current;
        modulationActiveRef.current = true;
        modulationViewRef.current = state.viewport || null;
        const modulated = state.gains.map((value, index) => state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);
"""),
    # 4. renderAll paints through the drawn window, then puts the user's back
    ("""  const renderAll = useCallback(() => {
    const g = getGeom();
    if (!g) return;
""",
     """  const renderAll = useCallback(() => {
    // Paint through the window the LFO is drawing (tools/patch_materialized_modulated_viewport.py).
    // Synchronous swap on the live view object so every draw pass below sees
    // it; restored before this function returns, so nothing that publishes
    // the view can observe it.
    const paintView = modulationViewRef.current;
    const userLmin = view.lmin, userLmax = view.lmax;
    if (paintView) { view.lmin = paintView.lmin; view.lmax = paintView.lmax; }
    try {
    const g = getGeom();
    if (!g) return;
"""),
    ("""    span("drawHUD", () => drawHUD(octx, g));
  }, [view, N, bloom,""",
     """    span("drawHUD", () => drawHUD(octx, g));
    } finally {
      if (paintView) { view.lmin = userLmin; view.lmax = userLmax; }
    }
  }, [view, N, bloom,"""),
]

def main():
    raw = DOC.read_text(encoding="utf-8")
    applied = 0
    for old, new in EDITS:
        o, n = encode(old), encode(new)
        if raw.count(n) == 1:
            continue
        if raw.count(o) != 1:
            print(f"modulated viewport: anchor not unique ({raw.count(o)}): {old[:60]!r}", file=sys.stderr)
            return 1
        raw = raw.replace(o, n)
        applied += 1
    if applied:
        DOC.write_text(raw, encoding="utf-8")
        print(f"modulated viewport: applied {applied} edit(s)")
    else:
        print("modulated viewport: already applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
