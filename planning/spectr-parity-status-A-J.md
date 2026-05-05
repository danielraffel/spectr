# Spectr ↔ WebView parity — open issues A–J

**Started:** 2026-05-04
**Owner:** ongoing collaboration with @danielraffel
**Build under test:** `/Users/danielraffel/Code/spectr/build/Spectr.app` (rebuilt against pulp v0.74.x; v0.75.2 rebuild pending)

## Protocol — read before modifying

1. **Nothing on this list gets marked resolved without explicit user confirmation.** A merged pulp PR does not equal a resolved Spectr-side issue. Headless-screenshot evidence is supportive, not authoritative.
2. **No issue letter is reused or renumbered.** If a fix lands and the symptom recurs later, the letter stays open.
3. **Candidate fixes get a `[<letter>-candidate]` tag** in commits and PR titles so the user can map them to issues without retyping.
4. **Three-line update format per issue** (Status / Last action / Next):
   - Status: `open` / `candidate-pending-confirm` / `confirmed-fixed` (last only after explicit user say-so)
   - Last action: one line, what was tried + commit/PR ref
   - Next: one line, the next concrete step
5. **Cross-reference open Spectr task IDs**: A=#101, B=#100, C=#102, D=#103, E=#104, F=#105, G=#106, H=#107, I=#108, J=#109.

## Issues

### A — Dropdown text escapes the panel width
- **Symptom:** Inner span text overflows the dropdown panel boundary; explicit `width:230/240` and `overflow:hidden` on the panel are both ignored on Spectr's chrome dropdowns.
- **Suspected root:** Pulp's `Label` / `View` widget bridge does not honor `width` constraint or `overflow:hidden` for child text content.
- **Status:** open
- **Last action:** filed under pulp #1387 umbrella; explicit widths added on Spectr-side spans.
- **Next:** retest after rebuilding against pulp v0.75.2 (PR #1380 — position:absolute skips flex-line — may help).

### B — Band drag → solid green canvas (inner area only)
- **Symptom:** Dragging a band fills the filterbank inner area with solid green; chrome border untouched. Only "Reset all" recovers.
- **Suspected root:** Initially hypothesised non-finite arg (Infinity) reaching CG canvas — DISPROVED by stderr inspection 2026-05-04 (no Inf values in log). Actual root TBD.
- **Status:** open — green-screen not currently reproducing in idle drag; still need user-side reproduction to capture stderr.
- **Sub-issues now distinct:**
  - **B-1: bands invisible at rest** — at gain=0 the column metaphor's fillRect had height=0; CG rounds to nothing while WebView shows a sub-pixel line. Patched 2026-05-04: `Math.max(2, ...)` floor in spectr-editor-extracted.js:1303-1311. User reports bands now visible at rest ✓ (not yet user-confirmed-fixed).
  - **B-2: drag doesn't adjust bands** — surfaced after B-1 fix made bands visible. Pointer events bound (issue #98 closed) but bands don't change on drag. Probe added at onPointerDown to log `[pdown] cx=X cy=Y tgt=...`. Awaiting user-side manual drag.
  - **B-3: green-screen** — original B symptom. Currently not reproducing. Re-test after B-2 is fixed.
- **Last action:** filed **pulp #1390** with full diagnosis of B-2. Local pulp at `/tmp/pulp-main` patched with two stderr probes (`[pulp:mouseDown]` in window_host_mac.mm, `[pulp:ptr-fire]` in widget_bridge.cpp registerPointer lambda); installed to `~/.pulp/sdk-local/darwin-arm64/0.75.0`. Spectr rebuilt against patched pulp.
- **Next:** user runs `Spectr 2> /tmp/spectr-pulp-probe.log`, drags, sends log. Three outcomes:
  1. `target=pr_1/pr_2` AND `[pulp:ptr-fire]` fires → JS routing broken downstream
  2. `target=pr_1/pr_2` but no `[pulp:ptr-fire]` → pulp lambda overwritten
  3. `target` is non-canvas → hit_test miss; canvas bounds wrong

### C — Mouse-move drives waveform animation (should be procedural-only)
**Likely closed by:** pulp #1400 (per pulp-side agent audit 2026-05-04). Re-test once #1400 merges + lands in a release.


- **Symptom:** Cursor movement over the filterbank changes the waveform output. Per user: mouse should ONLY zoom + adjust bands.
- **Suspected root:** Same as D — rAF chain doesn't self-sustain, so the only thing forcing renderAll() to run is input events. Mouse moves trigger repaint via state change, which advances `timeRef.current` and re-samples the spectrum.
- **Status:** open
- **Last action:** none; depends on D.
- **Next:** fix D first; if symptom remains, gate the spectrum sampling so it only advances on real time-tick events (not state-change repaints).

### D — Spectrum doesn't auto-animate on idle
**Likely closed by:** pulp #1400 (per pulp-side agent audit 2026-05-04). Same root as C. Re-test once #1400 merges + lands in a release.


- **Symptom:** Procedural SpectrSignal time evolution should animate the spectrum continuously. Doesn't — only ticks on input events. `rAF-cb` count = 1 in 12s of idle.
- **Suspected root:** Spectr's `NativeEditorView::pump_thread` calls `host->repaint()` every 16ms, and `paint()` calls `bridge_->service_frame_callbacks()`, but `pending_frame_ids_` doesn't drain. Either (a) the JS rAF callback throws and breaks the recursive arming, or (b) `service_frame_callbacks` doesn't see the queued ID, or (c) `__flushFrames__` isn't actually called when expected.
- **Status:** open
- **Last action:** verified pump_thread is firing (paint count = 420+ in 12s), service_frame_callbacks runs each paint, yet rAF chain stops after first call.
- **Next:** add JS-side probe that logs `[rAF-cb#N enter]` / `[rAF-cb#N exit]` / `[rAF-cb#N THREW <msg>]` so we can see whether the recursive `requestAnimationFrame(draw)` at line 697 ever runs.

### E — Mouse-wheel zoom doesn't fire
- **Symptom:** User scrolls over filterbank, expects view zoom-in/out, nothing happens.
- **Suspected root:** Either wheel events don't reach JS, or they reach JS but don't update `view.lmin/lmax`, or the update doesn't trigger a re-render.
- **Status:** open
- **Last action:** registerWheel hook added in dom-adapter (commit 0c26d5e); wheel handler bound to canvases in JSX.
- **Next:** add `[wheel] dy=X y=Y` log on the JS side; verify wheel events flow.

### F — Preset selection → green canvas (likely same root as B)
- **Symptom:** Clicking a PRESETS dropdown item fills the filterbank inner area with green.
- **Suspected root:** Same Infinity-reaches-CG-canvas as B. Bulk gain update during preset apply produces an Infinity in geometry math, which paints green.
- **Status:** open
- **Last action:** none — gated on B.
- **Next:** retest after B's Infinity guard lands.

### G — Preset manager doesn't show band-shape thumbnails
- **Symptom:** Preset entries show in the manager modal but the mini-canvas band-shape preview is missing/blank.
- **Suspected root:** Either (a) the thumbnail canvas commands trigger the same Infinity bug as B (only first frame, then state corruption blanks subsequent frames), or (b) thumbnail rendering isn't being invoked at all in the native bridge.
- **Status:** open
- **Last action:** none.
- **Next:** find thumbnail render path in editor JS; confirm whether canvas commands flow.

### H — No hover/tap visual feedback on chrome elements
- **Symptom:** Buttons/dropdown rows don't show :hover or :active visual states. CSS :hover translation supposedly landed in pulp #1345.
- **Suspected root:** Pulp's :hover translation may not match Spectr's specific selectors, or synthetic hover-event dispatch isn't reaching the affected View instances.
- **Status:** open (was task #61 marked completed prematurely on the pulp side; Spectr-side behavior never confirmed).
- **Last action:** none.
- **Next:** identify a single hoverable element (e.g., a dropdown row) and trace whether `register_hover` was called for it; if not, find why.

### I — Preset manager items render outside the overlay
- **Symptom:** Items inside the preset manager render outside the overlay's bounds (clipping/layout broken).
- **Suspected root:** Likely same family as A — pulp's overlay/View doesn't enforce bounding-box clipping. Pulp #1380 (v0.75.2) for absolute children may help.
- **Status:** open
- **Last action:** none.
- **Next:** rebuild against v0.75.2; if still broken, add explicit clip on Spectr-side overlay container.

### J — Window resize doesn't scale proportionally
- **Symptom:** Resizing the window cuts off contents instead of scaling proportionally. User wants min-window-size enforced + proportional scale.
- **Suspected root:** (1) `min_width/min_height` propagation from `ViewSize` hints to macOS `setContentMinSize:` may not be wired correctly (pulp #1362 was supposed to do this — needs verification on current build), (2) Spectr's editor root may need `flex_grow`/`flex_shrink` set so children scale with the window.
- **Status:** open
- **Last action:** none.
- **Next:** verify pulp #1362 actually clamps interactive resize on the running Spectr binary; if yes, audit Spectr's flex tree for fixed-vs-flexible sizing.

## Cross-cutting findings (from log analysis 2026-05-04)

These came out of stderr inspection during a synthetic band-drag and are worth keeping handy:

1. **Hue can go negative.** `hsla(-55.3125, 80%, 62%, ...)` is emitted by `specColor` for the rightmost band (hue = 240 - 1.0*300 = -60). Pulp's hsla parser likely needs to mod-360 or normalize. Possibly silent rendering failure for that band.
2. **Hover text contains literal "NaN"**: `canvasFillText(pr_2, NaN, NaN, "18.0Hz   NaN dB   band 0/32")`. A `value.toFixed(1)` somewhere in the hover-text formatter doesn't sanitize.
3. **Pulp CG canvas has a no-op `set_blend_mode`**: `canvas.hpp:338` is `virtual void set_blend_mode(BlendMode mode) { (void)mode; }`. CG canvas inherits this default — `globalCompositeOperation = 'lighter'` is silently ignored. Skia path is fine. Tracked as pending pulp #1377. Not the cause of B (CG silently using source-over wouldn't paint green).

## How to resume cold

1. Read this file.
2. Read `MEMORY.md` for user prefs (validation cadence, ralph rules, etc.).
3. Run TaskList — A–J are tasks #100–#109.
4. Open issues are whatever has Status: `open` here.
5. Never mark `confirmed-fixed` without user say-so.
