#!/usr/bin/env python3
"""The editor paints, and republishes its status, when NOTHING has changed.

Three costs, measured on the shipping editor, all of them paid while the user
is doing nothing in particular.

ONE: THE STATUS BANNER GOES THROUGH REACT.  `updateLiveHoverStatus` runs on
every frame, and once the reading has moved it published the new label back to
the parent through `onStatus` -- throttled to 700ms, but a publication all the
same.  `onStatus` is the PARENT'S state, and this document is a captured
import: a parent state change re-applies the whole captured document.  So a
pointer resting on a band, with a gain still smoothing under it, bought a
whole-document re-application roughly twice a second for as long as it rested
there.  Measured across two arms of the same session: an idle editor had zero
of these, and one with the pointer parked on a band had 42.  That is the
"drawing gets sluggish, then snaps back" that was reported.

It cannot be fixed by tagging the commit paint-only.  The pill's WIDTH is a
function of the string it paints, so the commit is geometric by construction
and no paint-only exemption can ever cover it.  It has to stop being a
whole-document event.  The direct-DOM fast path immediately above it already
writes the text AND re-places and re-sizes the pill, so React's copy buys
nothing on this path; the `hoverBand` effect below still publishes on every
band CROSSING, which is what re-shows a banner that has dismissed itself.

TWO: THE DRAW LOOP NEVER IDLES.  `draw` re-armed unconditionally and
`renderAll()` repainted the entire canvas on every one of those frames.
Measured on a genuinely idle editor -- nothing hovered, nothing playing,
nothing animating -- that is 826 canvas bridge calls and 2.82ms of JS per
frame, at 60Hz, for as long as the editor is open.  In a DAW that is a
continuous CPU floor burning next to the audio thread.

A stale canvas is a worse bug than a busy one, so the gate is built the
careful way round: the loop keeps painting while ANYTHING it can see is
moving, paints a tail of settled frames after that, and every path that can
change the canvas without moving React state wakes it explicitly.

  WHAT KEEPS IT AWAKE (checked every frame, in `draw`):
    * a render gain still smoothing toward its target (`smooth` is
      exponential and never lands, so the test is a sub-pixel threshold);
    * a band collapsing into the zero line on its way to muted;
    * an unmute pulse still decaying;
    * an edge-wall glow still decaying;
    * the modulation overlay being active at all;
    * a pointer gesture in progress;
    * a NEW analyzer frame, by object identity -- `acceptAnalyzerFrame`
      installs a fresh object per accepted frame, so this covers every
      analyzer consumer in the painter, not just the spectrum;
    * analyzer-derived paint that is still settling AFTER the last frame --
      the peak hold's 0.88 decay, the average's 0.035 approach, and the
      per-band energy glow's 0.9 decay, all of which keep moving for about a
      second after the analyzer goes quiet.  `spectrumSettledRef` is reset by
      `renderAll` and cleared by `drawSpectrum`; the per-band glow is read
      from OUTSIDE the band painter (`window.__spectrBandEnergy`), so this
      change does not touch `drawBands` at all.

  WHAT RE-ARMS IT AFTER IT PARKS:
    * EVERY React render, via an effect with no dependency array.  `renderAll`
      is a useCallback over sixteen pieces of state and a render is the only
      way any of them can move, so this covers all sixteen without naming one
      and cannot be left behind when a seventeenth is added;
    * pointer / wheel / key / focus events, on the window in the capture
      phase.  The interaction handlers mutate `pointerRef`, `marqueeRef`,
      `hoverRef`, `edgeGlowRef`, `unmutePulseRef`, `viewRef` and the paint
      gains directly and deliberately -- routing them through React is what
      made a marquee expensive -- so none of them schedules a render;
    * the resize handler, which resizes the backing store and must repaint;
    * `commitMany`, whose `deferReact` path exists precisely to change every
      painted gain WITHOUT a setState;
    * `applyModulationFrame`, the one publication path that calls no state
      setter at all, by design, so a derived LFO value can never feed back to
      native as a host edit;
    * `applyHostAutomationState`, which writes the paint refs and the
      viewport directly for the same reason.

  AND IT REFUSES TO PARK AT ALL when it cannot see the analyzer's frame
  identity (`debugSnapshot` absent) or when the analyzer does not declare
  itself native -- a synthetic analyzer that samples TIME rather than a
  published frame animates on its own, and this loop has no way to know that.

THREE: A PER-FRAME ALLOCATION IN THE HOT LOOP.  `drawSpectrum` built a
`new Float32Array(steps + 1)` every frame -- 321 floats at 60Hz, 19k floats a
second of pure churn for QuickJS's collector -- while `peaksRef` and
`avgSpectrumRef` sitting on the next two lines were already hoisted.  It is
hoisted the same way, and its sibling `spectrumPrevRef` is what lets the
settle test above see the samples themselves move (the filled peak curve is
drawn straight from `arr`, so a change there is visible even when the peak
hold and the average barely move).

NOTE: the document is compiled into the binary by `pulp_add_binary_data`
(CMakeLists.txt `spectr_native_assets`), so a rebuild is REQUIRED before any
native test reflects this patch.  `Encoding binary asset
materialized-document.runtime.json` in the build log is the proof; "Built
target" is not.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def enc(snippet):
    """The document stores the page as a JSON string, so every needle is
    escaped the way the file stores it. Raw-text surgery, never a load/dump
    round trip: this file's escaping is not uniform, so re-serialising would
    rewrite bytes that have nothing to do with this change."""
    return json.dumps(snippet)[1:-1]


# -- 1. the banner stops being a whole-document event ----------------------

STATUS_PUBLISH = (
    '    const now = performance.now();\n'
    '    if (onStatus && now - statusRefreshAtRef.current >= 700) {\n'
    '      statusRefreshAtRef.current = now;\n'
    '      onStatus(label);\n'
    '    }\n'
    '  };\n'
)
STATUS_PUBLISH_NEW = (
    '    // NO React publication here, deliberately. `onStatus` is the\n'
    '    // PARENT\'S state and this document is a captured import, so every\n'
    '    // publication re-applies the whole captured document. Throttled to\n'
    '    // 700ms that is still one whole-document re-application roughly\n'
    '    // twice a second for as long as a pointer rests on a band with a\n'
    '    // gain smoothing under it: 42 of them on a pointer-resting arm\n'
    '    // against zero on an idle one. No paint-only exemption can ever\n'
    '    // cover it either -- the pill\'s width is a function of the string\n'
    '    // it paints, so the commit is geometric by construction. The direct\n'
    '    // write above already owns the text AND the geometry; the effect\n'
    '    // below still publishes on every band CROSSING, which is what\n'
    '    // re-shows a banner that has dismissed itself.\n'
    '  };\n'
)

STATUS_REF = '  const statusRefreshAtRef = useRef(0);\n'

# -- 2. hoisted spectrum buffers, the idle-gate refs, and the wake ---------

PEAKS_REF = '  const peaksRef = useRef(new Float32Array(512));\n'
IDLE_REFS = (
    '  // Hoisted for the same reason `peaksRef` and `avgSpectrumRef` are:\n'
    '  // `drawSpectrum` runs on every frame, and a `new Float32Array(321)`\n'
    '  // per frame at 60Hz is 19k floats a second of churn for QuickJS\'s\n'
    '  // collector. `spectrumPrevRef` holds the previous frame\'s samples so\n'
    '  // the settle test can see the samples themselves move -- the filled\n'
    '  // peak curve is drawn straight from `arr`, so a change there is\n'
    '  // visible even when the peak hold and the average barely move.\n'
    '  const spectrumArrRef = useRef(null);\n'
    '  const spectrumPrevRef = useRef(null);\n'
    '  // Cleared by drawSpectrum while its own curves are still moving,\n'
    '  // reset by renderAll. `bandEnergyRef` asks the same question of\n'
    '  // the per-band glow, read from OUTSIDE the band painter so this\n'
    '  // change does not touch it.\n'
    '  const spectrumSettledRef = useRef(true);\n'
    '  const bandEnergyRef = useRef(null);\n'
    '  const drawRef = useRef(null);\n'
    '  const idleFramesRef = useRef(0);\n'
    '  const analyzerFrameRef = useRef(void 0);\n'
    '  // Re-arm the draw loop. It parks itself when nothing it can see is\n'
    '  // moving, so every path that changes the canvas WITHOUT moving React\n'
    '  // state has to call this -- a parked loop repaints nothing, and a\n'
    '  // stale canvas is a worse bug than a busy one. The React paths are\n'
    '  // covered wholesale by the no-dependency effect below. Touches only\n'
    '  // refs, so a stale closure held by a listener is still correct.\n'
    '  const wakeDraw = () => {\n'
    '    idleFramesRef.current = 0;\n'
    '    if (rafRef.current || !drawRef.current) return;\n'
    '    rafRef.current = requestAnimationFrame(drawRef.current);\n'
    '  };\n'
)

# -- 3. the loop learns what "still moving" means --------------------------

BUSY_DECL = (
    '      timeRef.current += dt;\n'
    '      const tg = targetGainsRef.current;\n'
)
BUSY_DECL_NEW = (
    '      timeRef.current += dt;\n'
    '      // Set by anything still moving. A frame with nothing moving\n'
    '      // paints exactly what the frame before it painted.\n'
    '      let busy = false;\n'
    '      const tg = targetGainsRef.current;\n'
)

COLLAPSE = (
    '            if (!Number.isFinite(rg[i]) || Math.abs(rg[i]) < 0.004) rg[i] = -Infinity;\n'
)
COLLAPSE_NEW = (
    '            if (!Number.isFinite(rg[i]) || Math.abs(rg[i]) < 0.004) rg[i] = -Infinity;\n'
    '            busy = true;\n'
)

SMOOTH = (
    '          if (!modulationActiveRef.current) rg[i] = smooth(rg[i], target, dt * k);\n'
)
SMOOTH_NEW = (
    '          if (!modulationActiveRef.current) {\n'
    '            rg[i] = smooth(rg[i], target, dt * k);\n'
    '            // `smooth` is exponential and never lands exactly on its\n'
    '            // target. 2e-4 of the [-1,1] gain range is under a twentieth\n'
    '            // of a pixel at any plot height this editor uses, so a band\n'
    '            // this close is already painted where it will stay.\n'
    '            if (Math.abs(rg[i] - target) > 2e-4) busy = true;\n'
    '          }\n'
)

PULSE = (
    '        unmutePulseRef.current[i] = Math.max(\n'
    '          0,\n'
    '          unmutePulseRef.current[i] - dt * 3.5\n'
    '        );\n'
    '      }\n'
)
PULSE_NEW = (
    '        unmutePulseRef.current[i] = Math.max(\n'
    '          0,\n'
    '          unmutePulseRef.current[i] - dt * 3.5\n'
    '        );\n'
    '        if (unmutePulseRef.current[i] > 0) busy = true;\n'
    '      }\n'
)

TAIL = (
    '      const eg = edgeGlowRef.current;\n'
    '      eg.left = Math.max(0, eg.left - dt * 2.5);\n'
    '      eg.right = Math.max(0, eg.right - dt * 2.5);\n'
    '      eg.top = Math.max(0, eg.top - dt * 2.5);\n'
    '      eg.bottom = Math.max(0, eg.bottom - dt * 2.5);\n'
    '      (renderAllRef.current || renderAll)();\n'
    '      updateLiveHoverStatus();\n'
    '      rafRef.current = requestAnimationFrame(draw);\n'
    '    };\n'
    '    renderAll();\n'
    '    rafRef.current = requestAnimationFrame(draw);\n'
    '    return () => cancelAnimationFrame(rafRef.current);\n'
    '  }, [motionMode]);\n'
)
TAIL_NEW = (
    '      const eg = edgeGlowRef.current;\n'
    '      eg.left = Math.max(0, eg.left - dt * 2.5);\n'
    '      eg.right = Math.max(0, eg.right - dt * 2.5);\n'
    '      eg.top = Math.max(0, eg.top - dt * 2.5);\n'
    '      eg.bottom = Math.max(0, eg.bottom - dt * 2.5);\n'
    '      if (eg.left > 0 || eg.right > 0 || eg.top > 0 || eg.bottom > 0)\n'
    '        busy = true;\n'
    '      if (modulationActiveRef.current) busy = true;\n'
    '      if (pointerRef.current && pointerRef.current.mode) busy = true;\n'
    '      (renderAllRef.current || renderAll)();\n'
    '      updateLiveHoverStatus();\n'
    '      // Read AFTER the paint. Both of these are analyzer-DERIVED\n'
    '      // state with its own hold and decay, so both keep moving for\n'
    '      // about a second after the analyzer goes quiet: the loop cannot\n'
    '      // park on the arrival of the last frame alone.\n'
    '      if (!spectrumSettledRef.current) busy = true;\n'
    '      // The per-band energy glow decays at 0.9 a frame inside the\n'
    '      // band painter, which this change deliberately does not touch,\n'
    '      // so its decay is read from the outside. 5e-4 is far below the\n'
    '      // 0.02 activity at which the glow is drawn at all.\n'
    '      const bandEnergy = window.__spectrBandEnergy;\n'
    '      if (bandEnergy) {\n'
    '        let seen = bandEnergyRef.current;\n'
    '        if (!seen || seen.length !== bandEnergy.length)\n'
    '          seen = bandEnergyRef.current = new Float32Array(bandEnergy.length);\n'
    '        for (let i = 0; i < bandEnergy.length; i++) {\n'
    '          if (Math.abs(bandEnergy[i] - seen[i]) > 5e-4) busy = true;\n'
    '          seen[i] = bandEnergy[i];\n'
    '        }\n'
    '      }\n'
    '      const analyzer = window.SpectrAnalyzer;\n'
    '      const frame = analyzer && typeof analyzer.debugSnapshot === "function"\n'
    '        ? analyzer.debugSnapshot()\n'
    '        : void 0;\n'
    '      if (frame === void 0 || !analyzer || analyzer.native !== true) {\n'
    '        // An analyzer with no frame identity to compare, or one that\n'
    '        // does not declare itself native -- a synthetic analyzer\n'
    '        // samples TIME and animates on its own, and this loop has no\n'
    '        // way to see that. Refuse to park rather than freeze it.\n'
    '        busy = true;\n'
    '      } else if (frame !== analyzerFrameRef.current) {\n'
    '        // acceptAnalyzerFrame installs a fresh object per accepted\n'
    '        // frame, so identity covers every analyzer consumer in the\n'
    '        // painter -- spectrum, band energy, mask and minimap alike.\n'
    '        analyzerFrameRef.current = frame;\n'
    '        busy = true;\n'
    '      }\n'
    '      idleFramesRef.current = busy ? 0 : idleFramesRef.current + 1;\n'
    '      // A tail of settled frames before parking. The gate reads state\n'
    '      // the previous frame left behind, so the tail costs a few frames\n'
    '      // of paint and buys immunity to a ref write that lands between\n'
    '      // two checks. 0 means parked; requestAnimationFrame never\n'
    '      // returns 0, so rafRef doubles as the armed flag.\n'
    '      rafRef.current = idleFramesRef.current < IDLE_TAIL_FRAMES\n'
    '        ? requestAnimationFrame(draw)\n'
    '        : 0;\n'
    '    };\n'
    '    drawRef.current = draw;\n'
    '    idleFramesRef.current = 0;\n'
    '    renderAll();\n'
    '    rafRef.current = requestAnimationFrame(draw);\n'
    '    return () => {\n'
    '      cancelAnimationFrame(rafRef.current);\n'
    '      rafRef.current = 0;\n'
    '      drawRef.current = null;\n'
    '    };\n'
    '  }, [motionMode]);\n'
    '  // EVERY React render re-arms the loop. No dependency array,\n'
    '  // deliberately: `renderAll` is a useCallback over sixteen pieces of\n'
    '  // state and a render is the only way any of them can move, so this\n'
    '  // covers all sixteen without naming one and cannot be left behind\n'
    '  // when a seventeenth is added.\n'
    '  useEffect(() => {\n'
    '    wakeDraw();\n'
    '  });\n'
    '  // The ref-only paths. Pointer, wheel and key handling mutate\n'
    '  // pointerRef, marqueeRef, hoverRef, edgeGlowRef, unmutePulseRef,\n'
    '  // viewRef and the paint gains directly -- deliberately, because\n'
    '  // routing them through React is what made a marquee expensive -- so\n'
    '  // none of them schedules the render the effect above would catch.\n'
    '  // Listening on the window in the CAPTURE phase arms the frame before\n'
    '  // the handler runs, and covers handlers added later with no new call.\n'
    '  useEffect(() => {\n'
    '    const wake = () => wakeDraw();\n'
    '    const events = [\n'
    '      "pointerdown", "pointermove", "pointerup", "pointercancel",\n'
    '      "wheel", "keydown", "keyup", "blur", "focus"\n'
    '    ];\n'
    '    for (const name of events) window.addEventListener(name, wake, true);\n'
    '    return () => {\n'
    '      for (const name of events)\n'
    '        window.removeEventListener(name, wake, true);\n'
    '    };\n'
    '  }, []);\n'
    '  // A new analyzer frame arriving while the loop is PARKED. The painters\n'
    '  // read the analyzer by polling it and nothing else subscribes to this\n'
    '  // message, so without this an editor that parks in silence and then\n'
    '  // starts receiving audio would hold a frozen spectrum until the next\n'
    '  // click. `emit` reaches listeners only for frames acceptAnalyzerFrame\n'
    '  // accepted, so this is one wake per genuinely new frame, and the\n'
    '  // identity check inside the loop keeps it awake while they keep\n'
    '  // coming.\n'
    '  useEffect(() => {\n'
    '    if (!window.pulp || typeof window.pulp.on !== "function") return;\n'
    '    const off = window.pulp.on("analyzer_frame", () => wakeDraw());\n'
    '    return () => { if (typeof off === "function") off(); };\n'
    '  }, []);\n'
)

LOOP_HEAD = (
    '  useEffect(() => {\n'
    '    let last = performance.now();\n'
    '    const draw = (now) => {\n'
)
LOOP_HEAD_NEW = (
    '  useEffect(() => {\n'
    '    let last = performance.now();\n'
    '    const IDLE_TAIL_FRAMES = 12;\n'
    '    const draw = (now) => {\n'
)

# -- 4. renderAll owns the settle verdict ----------------------------------

RENDER_ALL_HEAD = (
    '  const renderAll = useCallback(() => {\n'
    '    const g = getGeom();\n'
    '    if (!g) return;\n'
)
RENDER_ALL_HEAD_NEW = (
    '  const renderAll = useCallback(() => {\n'
    '    const g = getGeom();\n'
    '    if (!g) return;\n'
    '    // Reset here and cleared below by any analyzer-derived paint that\n'
    '    // is still moving: the peak hold\'s decay, the average\'s approach,\n'
    '    // the per-band energy glow. All three keep moving for about a\n'
    '    // second after the analyzer goes quiet, so the draw loop cannot\n'
    '    // park on the arrival of the last frame alone.\n'
    '    spectrumSettledRef.current = true;\n'
)

# -- 5. drawSpectrum: hoisted buffer + its half of the settle verdict ------

SPECTRUM = (
    '    const arr = new Float32Array(steps + 1);\n'
    '    for (let i = 0; i <= steps; i++) {\n'
    '      const lf = view.lmin + i / steps * span;\n'
    '      arr[i] = window.SpectrAnalyzer.sample(lf, t, "visible");\n'
    '    }\n'
    '    if (peaksRef.current.length !== steps + 1) peaksRef.current = new Float32Array(steps + 1);\n'
    '    const peaks = peaksRef.current;\n'
    '    for (let i = 0; i < peaks.length; i++) peaks[i] = Math.max(arr[i], peaks[i] * 0.88);\n'
    '    if (!avgSpectrumRef.current || avgSpectrumRef.current.length !== steps + 1) {\n'
    '      avgSpectrumRef.current = new Float32Array(steps + 1);\n'
    '    }\n'
    '    const avg = avgSpectrumRef.current;\n'
    '    const aK = 0.035;\n'
    '    for (let i = 0; i < avg.length; i++) avg[i] = avg[i] + (arr[i] - avg[i]) * aK;\n'
)
SPECTRUM_NEW = (
    '    if (!spectrumArrRef.current || spectrumArrRef.current.length !== steps + 1) {\n'
    '      spectrumArrRef.current = new Float32Array(steps + 1);\n'
    '      spectrumPrevRef.current = new Float32Array(steps + 1);\n'
    '    }\n'
    '    const arr = spectrumArrRef.current;\n'
    '    const prevArr = spectrumPrevRef.current;\n'
    '    // How far anything this function paints moved. 5e-4 of a normalised\n'
    '    // amount is under a tenth of a pixel at any plot height this editor\n'
    '    // uses, so a reading below it cannot move a rendered sample.\n'
    '    let settle = 0;\n'
    '    for (let i = 0; i <= steps; i++) {\n'
    '      const lf = view.lmin + i / steps * span;\n'
    '      arr[i] = window.SpectrAnalyzer.sample(lf, t, "visible");\n'
    '      const d = Math.abs(arr[i] - prevArr[i]);\n'
    '      if (d > settle) settle = d;\n'
    '      prevArr[i] = arr[i];\n'
    '    }\n'
    '    if (peaksRef.current.length !== steps + 1) peaksRef.current = new Float32Array(steps + 1);\n'
    '    const peaks = peaksRef.current;\n'
    '    for (let i = 0; i < peaks.length; i++) {\n'
    '      const next = Math.max(arr[i], peaks[i] * 0.88);\n'
    '      const d = Math.abs(next - peaks[i]);\n'
    '      if (d > settle) settle = d;\n'
    '      peaks[i] = next;\n'
    '    }\n'
    '    if (!avgSpectrumRef.current || avgSpectrumRef.current.length !== steps + 1) {\n'
    '      avgSpectrumRef.current = new Float32Array(steps + 1);\n'
    '    }\n'
    '    const avg = avgSpectrumRef.current;\n'
    '    const aK = 0.035;\n'
    '    for (let i = 0; i < avg.length; i++) {\n'
    '      const next = avg[i] + (arr[i] - avg[i]) * aK;\n'
    '      const d = Math.abs(next - avg[i]);\n'
    '      if (d > settle) settle = d;\n'
    '      avg[i] = next;\n'
    '    }\n'
    '    if (settle > 5e-4) spectrumSettledRef.current = false;\n'
)

# -- 7. the wake calls on the paths that bypass React ----------------------

RESIZE = (
    '      if (renderAllRef.current) renderAllRef.current();\n'
    '    };\n'
    '    resize();\n'
)
RESIZE_NEW = (
    '      if (renderAllRef.current) renderAllRef.current();\n'
    '      wakeDraw();\n'
    '    };\n'
    '    resize();\n'
)

COMMIT_MANY = (
    '    targetGainsRef.current = nextTarget;\n'
    '    if (!deferReact) setGains((prev) => {\n'
)
COMMIT_MANY_NEW = (
    '    targetGainsRef.current = nextTarget;\n'
    '    // `deferReact` exists precisely to skip the setState, so this commit\n'
    '    // can change every painted gain with no render behind it.\n'
    '    wakeDraw();\n'
    '    if (!deferReact) setGains((prev) => {\n'
)

MODULATION = (
    '          renderGainsRef.current = targetGainsRef.current.map((value) => isMuted(value) ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);\n'
    '          return true;\n'
    '        }\n'
    '        modulationActiveRef.current = true;\n'
    '        renderGainsRef.current = state.gains.map((value, index) => state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);\n'
    '        return true;\n'
    '      },\n'
)
MODULATION_NEW = (
    '          renderGainsRef.current = targetGainsRef.current.map((value) => isMuted(value) ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);\n'
    '          wakeDraw();\n'
    '          return true;\n'
    '        }\n'
    '        modulationActiveRef.current = true;\n'
    '        renderGainsRef.current = state.gains.map((value, index) => state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);\n'
    '        // The one publication path that calls no state setter at all, by\n'
    '        // design -- so it is the one path with no render behind it, and\n'
    '        // it has to arm the frame that draws what it just wrote.\n'
    '        wakeDraw();\n'
    '        return true;\n'
    '      },\n'
)

AUTOMATION = (
    '        notifyViewportListeners(false);\n'
    '        return true;\n'
    '      },\n'
    '      getGains: () => Array.from(targetGainsRef.current),\n'
)
AUTOMATION_NEW = (
    '        notifyViewportListeners(false);\n'
    '        // Writes the paint refs and the viewport with no state setter,\n'
    '        // deliberately (see the comment above about the one-shot flag).\n'
    '        wakeDraw();\n'
    '        return true;\n'
    '      },\n'
    '      getGains: () => Array.from(targetGainsRef.current),\n'
)


EDITS = [
    ('the status banner stops re-applying the whole captured document',
     (STATUS_PUBLISH, STATUS_PUBLISH_NEW),
     "// NO React publication here, deliberately."),

    ('the throttle clock it needed goes with it',
     (STATUS_REF, ""),
     None),

    ('hoisted spectrum buffers, the idle-gate refs, and the wake',
     (PEAKS_REF, PEAKS_REF + IDLE_REFS),
     "const wakeDraw = () => {"),

    ('the loop declares what it is measuring',
     (LOOP_HEAD, LOOP_HEAD_NEW),
     "const IDLE_TAIL_FRAMES = 12;"),

    ('a frame with nothing moving is a frame that need not exist',
     (BUSY_DECL, BUSY_DECL_NEW),
     "      let busy = false;"),

    ('a band collapsing into the zero line is still moving',
     (COLLAPSE, COLLAPSE_NEW),
     "            busy = true;"),

    ('a gain still smoothing toward its target is still moving',
     (SMOOTH, SMOOTH_NEW),
     "if (Math.abs(rg[i] - target) > 2e-4) busy = true;"),

    ('an unmute pulse still decaying is still moving',
     (PULSE, PULSE_NEW),
     "if (unmutePulseRef.current[i] > 0) busy = true;"),

    ('the loop parks on a settled frame, and everything re-arms it',
     (TAIL, TAIL_NEW),
     "rafRef.current = idleFramesRef.current < IDLE_TAIL_FRAMES"),

    ('renderAll owns the settle verdict its painters clear',
     (RENDER_ALL_HEAD, RENDER_ALL_HEAD_NEW),
     "    spectrumSettledRef.current = true;"),

    ('drawSpectrum stops allocating and reports its own settling',
     (SPECTRUM, SPECTRUM_NEW),
     "const prevArr = spectrumPrevRef.current;"),

    ('a resize re-arms the loop',
     (RESIZE, RESIZE_NEW),
     "      wakeDraw();\n    };\n    resize();"),

    ("commitMany's deferReact path re-arms the loop",
     (COMMIT_MANY, COMMIT_MANY_NEW),
     "// `deferReact` exists precisely to skip the setState"),

    ('the modulation overlay re-arms the loop',
     (MODULATION, MODULATION_NEW),
     "// The one publication path that calls no state setter at all, by"),

    ('host automation re-arms the loop',
     (AUTOMATION, AUTOMATION_NEW),
     "// Writes the paint refs and the viewport with no state setter,"),
]

# Asserted present after every run. The last four are the CONTROLS on this
# script's own reach: they are the code this change depends on and does not
# author, so if a later restructure moved them the gate above would be
# measuring a loop that no longer exists.
REQUIRED_AFTER = (
    "const wakeDraw = () => {",
    "rafRef.current = idleFramesRef.current < IDLE_TAIL_FRAMES",
    "spectrumSettledRef.current = true;",
    "const bandEnergy = window.__spectrBandEnergy;",
    "const arr = spectrumArrRef.current;",
    "wakeDraw();",
    "(renderAllRef.current || renderAll)();",
    "updateLiveHoverStatus();",
    "const hoverBandOf = (h) =>",
    "debugSnapshot() { return analyzerFrame; }",
    'window.pulp.on("analyzer_frame"',
)

# Must be GONE after every run: the two shapes this change exists to remove.
REQUIRED_ABSENT = (
    "      onStatus(label);\n    }\n  };",
    "const arr = new Float32Array(steps + 1);",
    "      rafRef.current = requestAnimationFrame(draw);\n    };\n    renderAll();",
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. Every edit is anchored inside
    # the bank's draw loop, its spectrum painter and its status banner; a
    # document without all three is one this script must refuse rather than
    # no-op into "already applied".
    anchors = {
        "filter bank": raw.count(enc("function FilterBank(")),
        "draw loop": raw.count(enc("    const draw = (now) => {")),
        "spectrum painter": raw.count(enc("  function drawSpectrum(ctx, g) {")),
        # The band painter is a CONTROL here, never a patch site: its
        # energy glow is read from outside, so this script must still
        # refuse a document that does not have one.
        "band painter": raw.count(enc("  function drawBands(ctx, g) {")),
        "status banner": raw.count(enc("  const updateLiveHoverStatus = () => {")),
        "render all": raw.count(enc("  const renderAll = useCallback(() => {")),
    }
    for label, count in anchors.items():
        print("control: %-18s %d" % (label, count))
    missing = [k for k, v in anchors.items() if v != 1]
    if missing:
        print("FAIL: %s not found exactly once -- wrong document"
              % ", ".join(missing), file=sys.stderr)
        return 1

    applied, already = [], []
    for label, (find, replace), done in EDITS:
        # `done` alone decides where there is one: an insertion's replacement
        # CONTAINS its own needle, so a rule that also required `find == 0`
        # would re-apply it every run. A pure DELETION has no `done` token to
        # leave behind, so for those (done is None) the absence of `find` IS
        # the applied state.
        if done is None:
            if raw.count(enc(find)) == 0:
                already.append(label)
                continue
        elif raw.count(enc(done)) >= 1:
            already.append(label)
            continue
        found = raw.count(enc(find))
        if found != 1:
            print("FAIL: %r matched %d times, expected exactly 1"
                  % (label, found), file=sys.stderr)
            return 1
        raw = raw.replace(enc(find), enc(replace), 1)
        applied.append(label)

    for token in REQUIRED_AFTER:
        if raw.count(enc(token)) == 0:
            print("FAIL: %r is absent after patching" % (token,), file=sys.stderr)
            return 1
    for token in REQUIRED_ABSENT:
        if raw.count(enc(token)) != 0:
            print("FAIL: %r is still present after patching" % (token,),
                  file=sys.stderr)
            return 1

    # Parse to adjudicate, never to write: a broken payload here is an editor
    # that does not load at all, and the artifact is one logical line so a
    # human diff will not catch it.
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        print("FAIL: the patched document no longer carries an html payload",
              file=sys.stderr)
        return 1
    for binding, expected in (("text_bindings", 24), ("layout_bindings", 81),
                              ("paint_bindings", 17)):
        actual = len(document.get(binding) or [])
        if actual != expected:
            print("FAIL: %s is %d, expected %d -- the bindings address nodes "
                  "by positional DOM path and this change adds none"
                  % (binding, actual, expected), file=sys.stderr)
            return 1

    if not applied:
        print("already applied: %d edit(s), nothing written" % len(already))
        return 0

    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied %d edit(s), %d already present; %d -> %d bytes"
          % (len(applied), len(already), before, len(raw)))
    for label in applied:
        print("  + " + label)
    return 0


if __name__ == "__main__":
    sys.exit(main())
