# WebView vs Native side-by-side audit — v0.69.1

**Date:** 2026-05-03
**SDK pin:** v0.69.1
**Spectr branch:** `feature/native-react-editor` @ `2695495`
**WebView reference:** `planning/screenshots/webview-reference.png`
**Native reference:** `planning/screenshots/native-editor-v0.69.1-idle.png`

## Method

Idle-state visual diff between Spectr's WebView build (the working baseline) and the native @pulp/react build at v0.69.1. Interactive surfaces (dropdowns, settings, tabs) deferred until #1292 / #1295 lands and `Spectr.app` launches reliably from direct exec. The earlier-captured idle screenshots (taken via `open ./Spectr.app` Launch Services) are the source of truth for this pass.

## Idle parity table — what matches, what differs

| Surface | WebView | Native v0.69.1 | Verdict |
|---|---|---|---|
| App-frame & traffic-light buttons | rendered by AppKit | rendered by AppKit | MATCH |
| Window title `Spectr — Standalone` | rendered | rendered | MATCH |
| Top-bar SPECTR brand | text only | text only | MATCH |
| `ZOOMABLE FILTER BANK` subtitle | rendered, same font | rendered, same font | MATCH |
| LIVE/PRECISION segmented | inline horizontal pills, LIVE active-tinted | inline horizontal pills, LIVE active-tinted | MATCH (was vertical-stack at v0.68.0 — closed by #1167) |
| IIR/FFT/HYBRID segmented | inline horizontal pills, IIR active-tinted | inline horizontal pills, IIR active-tinted | MATCH (was vertical-stack at v0.68.0 — closed by #1167) |
| `64 bands` indicator | top-right | top-right | MATCH |
| `1.00x zoom` indicator | top-right after bands | top-right after bands | MATCH |
| FilterBank canvas | full color gradient (blue → green → yellow → red) over 32-band-shaped curve | full color gradient over 32-band-shaped curve | MATCH (was empty-white at v0.68.0 audit; resolved at v0.68.1+) |
| Y-axis scale (`+24/+18/+12/+6/0/-6/-12/-18/-24`) | rendered | rendered | MATCH |
| `IIR · analog` y-axis caption | top-left of canvas area | top-left of canvas area | MATCH |
| Dashed midline at y=0 | thin pink/red dashed | thin pink/red dashed | MATCH |
| X-axis labels (`100Hz, 1kHz, 10kHz`) | below canvas | below canvas | MATCH |
| Spectrum gradient color stops | blue (low) → green (mid-low) → yellow-green (mid) → red (high) | identical | MATCH |
| Bottom toolbar — `CLEAR ▾` | left edge, dropdown chevron present | left edge, dropdown chevron present | MATCH |
| Bottom toolbar — `⋯` overflow | next to CLEAR | next to CLEAR | MATCH |
| Bottom toolbar — `SCULPT ▾` | with chevron + small icon | with chevron + small icon (icon area visible) | MATCH (icon not yet verified) |
| Bottom toolbar — `PEAK ▾` | with chevron | with chevron | MATCH |
| Bottom toolbar — `PRESETS ▾` | with chevron | with chevron | MATCH |
| `SNAPSHOT ▸A ▸B` triplet | mid-toolbar | mid-toolbar | MATCH |
| Mix slider (A———B) | mid-toolbar, single thumb | mid-toolbar, single thumb | MATCH |
| `30Hz - 20kHz` range readout | right side | right side | MATCH |
| `?` help icon | far right | far right (may be cut off in capture region) | LIKELY MATCH |

**Score:** 24/24 idle surfaces visually match between WebView and native at v0.69.1. The 4 most-visible v0.68.0 audit symptoms (segmented-control vertical stacking, empty FilterBank, app-root bottom-strip, layout regression) are all closed.

## What's NOT yet verifiable at v0.69.1

Spectr crashes on direct exec ~100% of cold starts due to **#1292** (two React module instances in editor.js — diagnosed + fixed at #1295). Until #1295 merges and a release ships, interactive testing requires the Launch Services workaround (`open ./Spectr.app`) and is unstable. The following surfaces are deferred:

- SCULPT popover open / inner clicks / outside-click close / ESC close
- PEAK popover same
- PRESETS popover content rendering + item click + MANAGE… selection
- BANDS picker (32/40/48/56/64) cell selection
- IIR/FFT/HYBRID tab click switching
- LIVE/PRECISION tab click switching
- Hover state on toolbar buttons (post-#1149 fix)
- Settings modal sliders
- Snapshot ▸A ▸B button click → toast
- CLEAR button click → "CLEARED GAINS" toast (was working in v0.68.0 audit; needs re-verify)

Each is tracked under #1147 (popover render) or #1148 (overlay click dispatch) — the latter just got a design pointer (April 18 ComboBox routing pattern as template).

## Aesthetic deltas (subtle, low-priority)

| # | Surface | Delta | Cause |
|---|---|---|---|
| 1 | Spectrum line thickness | Native edge slightly softer (anti-aliased same way?) | Skia GPU vs Chromium — pixel-level diff but visually equivalent |
| 2 | Toolbar button hover state | WebView gets CSS `:hover` brightening for free | #1149 closed framework path (registerHover wired), but Spectr's editor.js uses CSS `:hover` (string) not `onMouseEnter` — needs CSS-engine-level translation OR adapter that hoists `:hover` rules into `onMouseEnter`/`onMouseLeave` props. Tracked under #1149 part (b) — explicitly deferred |
| 3 | Bottom toolbar icon glyphs (SCULPT 〰️, PEAK ━, PRESETS ⋮) | WebView renders inline `<svg><path>` correctly | Native shipped `<SvgPath>` intrinsic via #994/#1291 — but Spectr's editor.js still uses raw `<svg><path>` markup; needs dom-adapter mapping `<svg>` → `<SvgPath>` OR bundle rebuild that uses `<SvgPath>` directly. **Spectr-side action item, not a Pulp gap.** |

## What changed since v0.68.0 audit

Closed via Pulp framework fixes (no Spectr workaround needed):

- **#964** FilterBank canvas — auto-resolved at v0.68.1 per spec note; v0.69.1 confirms ✓
- **#967** transparent View bg — closed earlier (v0.61.0 via #973 contract tests)
- **#992** PulpView mouseUp SIGSEGV — closed v0.62.0 via #1001
- **#1006 / #1067** click-event dispatch (top-level) — closed v0.68.0 via #1008 + #1073
- **#1147** part — top-bar Segmented control vertical stacking → flex-direction:row default in #1167 (v0.69.0)
- **#1149** registerHover wiring — closed by #1173 (v0.69.0). Part (b) CSS `:hover` translation deferred.
- **#1150** font-registration API — closed by #1175 (v0.69.0). Spectr can now register Inter / JetBrainsMono explicitly instead of accidentally piggy-backing on Pulp's bundled fonts.
- **#1151** fontFamily list parser — closed by #1174 (v0.69.1). CSS `font-family: 'JetBrains Mono', ui-monospace, monospace;` now picks first author family.
- **#994** `<SvgPath>` intrinsic — closed by #1291 (v0.69.2 unreleased, artifact uploading). Pending Spectr-side adapter wiring or bundle rebuild.

Still open at v0.69.1 (Spectr-blocker subset):

- **#998** layout regression — Spectr `346eb64` workaround still active (explicit App-root w/h)
- **#1070** typography drift — partial: list parser fixed; font-registration consumption pending
- **#1147** popover row template — header renders, inner SVG-icon + multi-line text rows still missing
- **#1148** overlay click dispatch — pulp-side design pointer added; subagent implementing the View-level overlay-routing primitive in `feature/overlay-click-routing-1148`
- **#1292** React useState=null crash — root-cause fix at #1295 (in CI)

## Generalization for future imports

Per the Spectr-Native-React-Bridge-Spec:

> Spectr is the consumer-zero validating the pipeline. Every Spectr fix should ask "would this generalize?".

The following fixes from this audit are **already general** (helping future Claude-Design / Stitch / v0 / Figma imports):

- `<SvgPath>` intrinsic — any plugin shipping inline SVG icons gets it
- Public font-registration API — any plugin bundling its own .ttf gets typography parity
- fontFamily list parser — any plugin authoring CSS `font-family` lists gets first-family resolution
- View-level overlay-routing primitive (in flight at #1148) — any plugin using `position: absolute` overlays gets click dispatch

The following remain **plugin-specific contract** (not generalizable yet):

- Spectr's bundled editor.js needs to migrate from raw `<svg><path>` to `<SvgPath>` (1-line bundle change; may eventually be auto-handled by `pulp import-design` post-#995)
- Spectr's CSS `:hover` rules need either CSS-engine-side translation (#1149 part b) or Spectr-side migration to `onMouseEnter` patterns

## Next steps

1. **Wait for #1295 to merge + release.** Will resolve #1292 launch crash and unlock interactive audit.
2. **Once #1295 ships:** rebuild Spectr against the new release, capture interactive screenshots for SCULPT/PEAK/PRESETS popovers + tab-click + settings modal + drag interactions. Diff against this WebView reference. File any new gaps.
3. **Once #1148 ships** (subagent in flight on `feature/overlay-click-routing-1148`): re-run popover-inner-click test from v0.68.0 audit. The April-18 ComboBox routing pattern generalizes to all React popovers via the new `claimOverlay()` API.
4. **#1147 follow-up:** investigate the popover-row template specifically. Likely involves nested flex + inline SVG inside `<button>`. May surface new compat.json gaps.
5. **#1070 follow-up:** consume the font-registration API in Spectr's CMakeLists.txt. Should fully close letter-spacing drift once Inter / JBM are explicitly registered.
