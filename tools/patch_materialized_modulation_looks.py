#!/usr/bin/env python3
"""Switchable looks for how the bands animate while an LFO moves them.

WHY THE MOTION READ AS JARRING. The native editor already evaluates the LFO at
each display tick (see Spectr::publish_modulation_frame_), so the oscillator is
not being resampled into a staircase. But the editor's `applyModulationFrame`
wrote every frame's heights straight into the paint refs, bypassing the easing
the draw loop applies to everything else. Any uneven gap between frames -- a
busy UI thread, a bridge dispatch landing late -- therefore showed as a jolt,
and thirty-two bands jolting in lockstep is a lot of screen change. The
Columns top line was also snapped to whole pixels, so a moving top stepped.

THE LOOKS. `settings.modulationLook` picks one; each changes drawing only.
The audio is never touched.

  classic  today's behaviour: heights follow each frame exactly
  glide    exponential ease toward the modulated height (~70 ms)
  float    a slower, softer ease (~170 ms)
  spring   critically damped spring: smooth starts and stops, no overshoot
  calm     glide at HALF the visual excursion -- reduced motion
  trail    glide plus a fading afterimage of recent heights
  contour  bars stay at their authored level; a glowing line carries the
           modulation across the band tops
  glow     glide plus a halo at each band top that brightens with how far the
           LFO has pushed it
  ribbon   glide plus a smooth filled curve through the band tops
  hybrid   spring plus trail plus glow

Every look except `classic` also paints the Columns top line at sub-pixel
positions, so a moving top glides instead of stepping a pixel at a time.

The picker lives in Settings > Appearance ("Modulation look"), as wrapped
chips. Classic stays the default until a look is chosen.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "window.SPECTR_MODULATION_LOOKS = "

LOOKS = [
    ("classic", "Classic"), ("glide", "Glide"), ("float", "Float"),
    ("spring", "Spring"), ("calm", "Calm"), ("trail", "Trail"),
    ("contour", "Contour"), ("glow", "Glow"), ("ribbon", "Ribbon"),
    ("hybrid", "Hybrid"),
]
LOOKS_JS = "[" + ", ".join('["%s", "%s"]' % pair for pair in LOOKS) + "]"

EDITS = [
    (
        "default setting",
        '''  "waveStyle": "smooth"
''',
        '''  "waveStyle": "smooth",
  "modulationLook": "classic"
''',
    ),
    (
        "look list and state refs",
        '''  const canvasRef = useRef(null);
''',
        '''  const canvasRef = useRef(null);
  // How the bands animate while an LFO moves them -- drawing only. See
  // tools/patch_materialized_modulation_looks.py for what each look does.
  window.SPECTR_MODULATION_LOOKS = ''' + LOOKS_JS + ''';
  const modulationLookRef = useRef("classic");
  modulationLookRef.current = (settings && settings.modulationLook) || "classic";
  const modTargetRef = useRef(null);
  const modEasedRef = useRef([]);
  const modVelRef = useRef([]);
  const trailRef = useRef([]);
  const trailClockRef = useRef(0);
''',
    ),
    (
        "modulation frames set a target",
        '''        modulationActiveRef.current = true;
        renderGainsRef.current = state.gains.map((value, index) => state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);''',
        '''        const wasModulating = modulationActiveRef.current;
        modulationActiveRef.current = true;
        const modulated = state.gains.map((value, index) => state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);
        modTargetRef.current = modulated;
        if (modulationLookRef.current === "classic") {
          renderGainsRef.current = modulated;
        } else if (!wasModulating || modEasedRef.current.length !== N) {
          // Start the eased state where the bars are, so a look never begins
          // with a jump of its own.
          modEasedRef.current = renderGainsRef.current.map((v) => Number.isFinite(v) ? v : 0);
          modVelRef.current = new Array(N).fill(0);
          trailRef.current = [];
        }''',
    ),
    (
        "release eases back unless classic",
        '''          modulationActiveRef.current = false;
          // Release the overlay back to canonical state rather than freezing
          // on the last modulated frame.
          renderGainsRef.current = targetGainsRef.current.map((value) => isMuted(value) ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);''',
        '''          modulationActiveRef.current = false;
          modTargetRef.current = null;
          trailRef.current = [];
          // Release the overlay back to canonical state rather than freezing
          // on the last modulated frame. Classic snaps, as it always has; the
          // other looks let the draw loop's own easing carry the bars home.
          if (modulationLookRef.current === "classic")
            renderGainsRef.current = targetGainsRef.current.map((value) => isMuted(value) ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);''',
    ),
    (
        "eased state sized with the bank",
        '''      const k = motionMode === "precision" ? 6 : 22;
      for (let i = 0; i < rg.length; i++) {''',
        '''      const k = motionMode === "precision" ? 6 : 22;
      if (modEasedRef.current.length !== rg.length) {
        modEasedRef.current = rg.map((v) => Number.isFinite(v) ? v : 0);
        modVelRef.current = new Array(rg.length).fill(0);
      }
      for (let i = 0; i < rg.length; i++) {''',
    ),
    (
        "per-look motion in the draw loop",
        '''            if (Math.abs(rg[i] - target) > 2e-4) busy = true;
          }
        }
        unmutePulseRef.current[i] = Math.max(''',
        '''            if (Math.abs(rg[i] - target) > 2e-4) busy = true;
          } else if (modulationLookRef.current !== "classic") {
            // The modulated height is a TARGET here, eased per look, rather
            // than written straight into the paint refs.
            const look = modulationLookRef.current;
            const mt = modTargetRef.current && Number.isFinite(modTargetRef.current[i])
              ? modTargetRef.current[i] : target;
            const eased = modEasedRef.current;
            const vel = modVelRef.current;
            if (!Number.isFinite(eased[i])) eased[i] = Number.isFinite(rg[i]) ? rg[i] : 0;
            if (look === "spring" || look === "hybrid") {
              // Critically damped: a smooth start and stop with no overshoot.
              const w = 16;
              vel[i] += (w * w * (mt - eased[i]) - 2 * w * vel[i]) * dt;
              eased[i] += vel[i] * dt;
            } else {
              const rate = look === "float" ? 6 : look === "calm" ? 9 : 14;
              eased[i] = smooth(eased[i], mt, dt * rate);
            }
            rg[i] = look === "contour" ? smooth(rg[i], target, dt * k)
              : look === "calm" ? target + (eased[i] - target) * 0.5
              : eased[i];
          }
        }
        unmutePulseRef.current[i] = Math.max(''',
    ),
    (
        "trail history is sampled",
        '''    if (visualizationMode !== "response") drawBands(ctx, g);
''',
        '''    if (visualizationMode !== "response") drawBands(ctx, g);
    if (visualizationMode !== "response") drawModulationExtras(ctx, g);
''',
    ),
    (
        "extras painter",
        '''  function drawBands(ctx, g) {
''',
        '''  // The visual layer of the non-classic looks: trail, contour, glow and
  // ribbon. Reads the paint refs and the eased modulation, draws nothing when
  // no modulator is running.
  function drawModulationExtras(ctx, g) {
    const look = modulationLookRef.current;
    if (look === "classic" || !modulationActiveRef.current) return;
    const { inner, zeroY, halfH, bandW } = g;
    const tg = targetGainsRef.current;
    const eased = modEasedRef.current;
    if (!eased || eased.length !== N) return;
    const yOf = (v, i) => zeroY - clamp(macroAdjustedGain(v, i), -1.02, 1.02) * halfH;
    const barW = (i) => bandW * (i === 0 || i === N - 1 ? 0.72 : 0.78);
    ctx.save();
    ctx.beginPath();
    ctx.rect(inner.x - 4, inner.y - 4, inner.w + 8, inner.h + 8);
    ctx.clip();
    if (look === "trail" || look === "hybrid") {
      // Sample the painted heights every ~45 ms and draw the last few as a
      // fading afterimage behind the bars' current tops.
      trailClockRef.current += 1;
      const hist = trailRef.current;
      for (let k = 0; k < hist.length; k++) {
        const alpha = 0.26 * (k + 1) / (hist.length + 1);
        for (let i = 0; i < N; i++) {
          if (isMuted(tg[i]) || !Number.isFinite(hist[k][i])) continue;
          const y = yOf(hist[k][i], i);
          const w = barW(i) * 0.9;
          ctx.fillStyle = specColor((i + 0.5) / N, alpha, theme);
          ctx.fillRect(bandCenterX(i, g) - w / 2, Math.min(y, zeroY), w, Math.abs(zeroY - y));
        }
      }
    }
    if (look === "contour" || look === "ribbon") {
      const pts = [];
      for (let i = 0; i < N; i++) {
        if (isMuted(tg[i])) continue;
        pts.push({ x: bandCenterX(i, g), y: yOf(eased[i], i), p: (i + 0.5) / N });
      }
      if (pts.length > 1) {
        const path = () => {
          ctx.beginPath();
          ctx.moveTo(pts[0].x, pts[0].y);
          for (let j = 1; j < pts.length - 1; j++) {
            const mx = (pts[j].x + pts[j + 1].x) / 2, my = (pts[j].y + pts[j + 1].y) / 2;
            ctx.quadraticCurveTo(pts[j].x, pts[j].y, mx, my);
          }
          ctx.lineTo(pts[pts.length - 1].x, pts[pts.length - 1].y);
        };
        // Opacity rides in the gradient's own colours: this canvas does not
        // apply globalAlpha to gradient paints the way a browser does, which
        // measured as a near-opaque ribbon and a dim contour.
        const gradient = (alpha) => {
          const grad = ctx.createLinearGradient(inner.x, 0, inner.x + inner.w, 0);
          grad.addColorStop(0, specColor(0.05, alpha, theme));
          grad.addColorStop(0.5, specColor(0.5, alpha, theme));
          grad.addColorStop(1, specColor(0.95, alpha, theme));
          return grad;
        };
        if (look === "ribbon") {
          path();
          ctx.lineTo(pts[pts.length - 1].x, zeroY);
          ctx.lineTo(pts[0].x, zeroY);
          ctx.closePath();
          ctx.fillStyle = gradient(0.30);
          ctx.fill();
        }
        // A wide faint stroke under a thin bright one reads as a glow. Solid
        // colours: a gradient STROKE painted dim here while a gradient fill
        // did not, so the line keeps to a plain light tone.
        path();
        ctx.lineJoin = "round";
        ctx.lineCap = "round";
        ctx.strokeStyle = "rgba(150,205,255,0.20)";
        ctx.lineWidth = 9;
        ctx.stroke();
        ctx.strokeStyle = "rgba(225,240,255,0.92)";
        ctx.lineWidth = 2;
        ctx.stroke();
      }
    }
    if (look === "glow" || look === "hybrid") {
      // A halo at each band top whose strength follows how far the LFO has
      // pushed that band from its authored level.
      for (let i = 0; i < N; i++) {
        if (isMuted(tg[i]) || !Number.isFinite(eased[i])) continue;
        const push = Math.min(1, Math.abs(eased[i] - (Number.isFinite(tg[i]) ? tg[i] : 0)) * 2.2);
        if (push < 0.02) continue;
        const y = yOf(eased[i], i);
        const w = barW(i);
        const cx = bandCenterX(i, g);
        for (let r = 3; r >= 1; r--) {
          ctx.fillStyle = specColor((i + 0.5) / N, 0.11 * push * (4 - r), theme);
          ctx.fillRect(cx - w / 2 - r * 2, y - r * 3, w + r * 4, r * 6);
        }
      }
    }
    ctx.restore();
    if ((look === "trail" || look === "hybrid") && trailClockRef.current % 3 === 0) {
      const hist = trailRef.current;
      hist.push(renderGainsRef.current.slice());
      if (hist.length > 5) hist.shift();
    }
  }
  function drawBands(ctx, g) {
''',
    ),
    (
        "sub-pixel top line off classic",
        '''ctx.fillRect(Math.round(G.cx - G.innerW / 2), Math.round(G.topY) - 1, Math.round(G.innerW), 2);''',
        '''ctx.fillRect(Math.round(G.cx - G.innerW / 2), (modulationLookRef.current === "classic" ? Math.round(G.topY) : G.topY) - 1, Math.round(G.innerW), 2);''',
    ),
    (
        "chips can wrap",
        '''function SpectrSettingsChips({ value, onChange, opts }) {
  return /* @__PURE__ */ React.createElement("div", { style: { display: "flex", gap: 3 } }''',
        '''function SpectrSettingsChips({ value, onChange, opts, wrap }) {
  return /* @__PURE__ */ React.createElement("div", { style: wrap ? { display: "flex", gap: 3, flexWrap: "wrap", maxWidth: 270, justifyContent: "flex-end" } : { display: "flex", gap: 3 } }''',
    ),
    (
        "settings picker",
        '''  )), /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Bloom", hint: "Halo intensity when bands react to signal" }''',
        '''  )), /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Modulation look", hint: "How bands animate while an LFO moves them (experimental)" }, /* @__PURE__ */ React.createElement(
    SpectrSettingsChips,
    {
      value: settings.modulationLook || "classic",
      onChange: (v) => persist({ modulationLook: v }),
      wrap: true,
      opts: window.SPECTR_MODULATION_LOOKS || ''' + LOOKS_JS + '''
    }
  )), /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Bloom", hint: "Halo intensity when bands react to signal" }''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("modulation looks already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("modulation looks applied (%d looks)" % len(LOOKS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
