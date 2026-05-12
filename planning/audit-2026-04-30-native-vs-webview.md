# Native vs WebView UX-parity audit — v0.68.0 SDK

**Date:** 2026-04-30
**Branch:** `feature/native-react-editor` @ `233f57e`
**Build:** `/Users/danielraffel/Code/spectr/build/Spectr.app` (rebuilt against pulp v0.68.0)
**Reference:** `planning/screenshots/_REFERENCE_webview.png`
**Audit screenshots:** `planning/screenshots/audit-2026-04-30/`

## Method

Launched native build, brought Spectr to front via `osascript -e 'tell application "Spectr" to activate'`, captured Spectr window region via `screencapture -x -R100,33,1320,892 …`, drove clicks via `cliclick c:X,Y` after each re-activate to combat focus-stealing. Each toolbar surface tested for: opens, renders content, inner click works, outside-click closes, ESC closes.

## Status table — UX-parity inventory rows

| # | Surface | Status | Evidence |
|---|---------|--------|----------|
| 1 | FilterBank canvas | **MATCHES (still BROKEN per #964)** — empty white area, no spectrum | `native-idle-fresh.png` |
| 2 | App-root layout | **DIFFERS:partial** — 346eb64 mitigation in place; main canvas area renders white not transparent (probably the white-fill mitigation hiding canvas), bottom strip and toolbar render. Top dark titlebar + bottom dark toolbar correct. | `native-idle-fresh.png` |
| 3 | PRESETS dropdown | **MATCHES (#1070)** — content renders (FACTORY label, ★ FLAT, HARMONIC SERIES, ALTERNATING, COMB, VOCAL FORMANTS, SUB ONLY (≤ 160 Hz), DOWNWARD TILT, AIR LIFT (4k+), MANAGE…), letter-spacing/typography differs from WebView. | `native-presets-fresh.png` |
| 4 | PEAK / ANALYZER popover | **MATCHES (#1147)** — header "ANALYZER · A to cycle" renders. Active row at top is empty stroke-only outline. Description text "Instant" / "Roll" / "Overlay" overflows to the right of the panel boundary. Row labels and SVG icons missing. | `native-peak-fresh.png`, cropped `/tmp/peak-panel.png` |
| 5 | SCULPT / EDIT MODE popover | **MATCHES (#1147)** — header "EDIT MODE · how dragging affects bands" renders. One empty active-row outline at top. The other 4 rows (icon + label + tagline + desc) are missing entirely; panel body is just black. | `native-sculpt-fresh.png` |
| 6 | Click on dropdown item (BANDS, SCULPT row, PRESET) | **MATCHES (#1148)** — confirmed BROKEN. Bands "64" cell click → bands stays 32. SCULPT row click → no selection. PRESETS row click → no selection. Click also tends to fall through to apps behind Spectr when the popover is paint-only overlay. | `native-bands-click-64.png`, `native-sculpt-inner-fresh.png`, `native-presets-inner-click.png` |
| 7 | Click outside dropdown | **DIFFERS:per-popover-inconsistent** — Spec says "stays open" on all. Reality: BANDS outside-click closes ✓; PRESETS outside-click closes ✓; PEAK outside-click does NOT close ✗; SCULPT outside-click does NOT close ✗. Inconsistent across popovers. | `native-bands-after-outside-click.png`, `native-presets-outside-fresh.png`, `native-peak-outside-fresh.png`, `native-sculpt-outside-click.png` |
| 8 | ESC with dropdown open | **DIFFERS:per-popover-inconsistent** — Spec says "nothing happens". Reality: PEAK ESC closes ✓; SCULPT ESC closes ✓; BANDS ESC does NOT close ✗; PRESETS ESC does NOT close ✗. Inverse of outside-click. The two popovers that don't respond to outside-click DO respond to ESC, and vice versa. | `native-peak-esc-fresh.png`, `native-sculpt-esc.png`, `native-bands-after-esc.png`, `native-presets-esc-fresh.png` |
| 9 | Inline `<svg><path>` icons | **MATCHES (#994)** — no SVG icons render in any popover row, in toolbar buttons, or anywhere in the body. | All popover screenshots |
| 10 | Typography (`var(--mono)` / `var(--sans)`) | **MATCHES (#932/#1070)** — Inter / IBM Plex Mono not active; falls back to system. Letter-spacing visibly looser than WebView ref. | All screenshots |
| 11 | `<input type=range>` faders | **N/A** — no range surface visible in idle; A-B mix slider at bottom-right renders as RangeSlider widget per `8f1df47`. | `native-idle-fresh.png` (right of toolbar, "A ●———— B") |
| 12 | Canvas background | **MATCHES** — transparent default holds; main body shows the white fallback fill (from `346eb64` mitigation), not a colored canvas paint. | `native-idle-fresh.png` |
| 13 | Top-bar tabs (LIVE / PRECISION / 8R / FFT / HYBRID) | **MATCHES (mostly)** — clicks work, active-state styling propagates correctly. Verified LIVE→PRECISION, IIR→FFT→HYBRID toggling. Note: there is no "8R" tab on the native build at v0.68.0 — top bar shows "IIR / FFT / HYBRID" only. (Spec row says "LIVE / PRECISION / 8R / FFT / HYBRID" — "8R" appears to be a misread; native and WebView both show IIR.) | `native-live-clicked.png`, `native-fft-clicked.png`, `native-hybrid-clicked.png` |
| 14 | Bands picker dropdown ("32 40 48 56 64") | **MATCHES (#1148)** — opens correctly with 5 cells, active "32" highlighted, but selecting a number does nothing. (Spec was correct.) | `native-bands-open.png`, `native-bands-click-64.png` |

## Newly observed deltas

(For your triage — I did not file issues.)

- **⋯ overflow toolbar button** — Not in the parity table. Opens a 4-row panel: "RESET ALL · gains · view · snapshots", "INVERT", "MUTE ALL", "FIT VIEW". Renders cleanly with sublabels visible — much better than SCULPT/PEAK rendering. Inner click on INVERT did NOT trigger any action (#1148 inner-click broken here too). Worth adding as its own row in the parity table. Screenshot: `native-overflow-fresh.png`. Inner click: `native-overflow-inner-fresh.png`.
- **CLEAR toolbar button** — Not in the parity table. Inner click DOES dispatch and produces a "CLEARED GAINS" toast in the top-left. So clicks on `<button>` elements (CLEAR) work fine; the breakage is specific to popover/dropdown row dispatch. Screenshot: `native-clear-clicked.png`.
- **Dropdown click fall-through to background apps** — When a popover is open, clicks intended for popover rows can hit the application *behind* Spectr (terminal, Discord, etc.). This goes beyond "click does nothing" — the click dispatches to whatever desktop app is at those screen coordinates. This is the worst behavior of #1148 and explains why some test sessions ended with Spectr losing focus mid-audit. Screenshots: `native-presets-after-item-click.png` (terminal text visible), `native-sculpt-inner-click.png` (terminal visible). May warrant treating as a separate severity bump on #1148, since it lets clicks escape the plugin window entirely.
- **Per-popover dismiss-handler inconsistency** — BANDS and PRESETS only respond to outside-click; PEAK and SCULPT only respond to ESC. No single popover has both behaviors working, which suggests two different popover code paths in Spectr's React tree (one wired to keyboard but not pointer dismissal, one wired to pointer but not keyboard). Worth adding as a row to `#1148` notes — the inconsistency is itself the bug, not just "all dismissal broken".
- **Snapshot row ▸A / ▸B buttons activate accidentally during off-target clicks** — During the bands inner-click test at logical (985,93), the toolbar `▸A` and `▸B` highlighted and a "SNAPSHOT A CAPTURED" toast appeared. Looks like a stray click on the snapshot buttons during ESC/outside-click tests. Not necessarily a bug — those buttons sit in the toolbar between PRESETS and the A/B slider — but worth being aware of for future automated audits.
- **CLEARED GAINS / SNAPSHOT A CAPTURED toasts render correctly** — Top-left toast strip renders text in a dark pill with proper padding. Suggests the toast widget is one of the few non-canvas surfaces that's working end-to-end. Not a delta; just a positive observation.

## Three most surprising findings

1. **Dismiss handlers are split exactly down the middle, not uniformly broken** — BANDS+PRESETS close on outside-click (no ESC); PEAK+SCULPT close on ESC (no outside-click). #1148 isn't "no dismissal works" — it's "each popover has half its dismissal wired". Two code paths.
2. **Clicks on closed popover items fall through to the desktop app behind Spectr**, not just "do nothing". This is a security/UX hole worse than the dispatch bug alone — the plugin window isn't capturing its own pointer events when an overlay is paint-only.
3. **The ⋯ overflow popover renders almost perfectly** while SCULPT and PEAK render almost nothing — same trigger, same SDK, same theme. Strong signal that the SCULPT/PEAK row template is the broken surface (probably the `<svg>` + multi-line label layout), not the popover infrastructure itself. #1147 fix should focus on the row template, not the panel chrome.

## Deliverables

- Report: `/Users/danielraffel/Code/spectr/planning/audit-2026-04-30-native-vs-webview.md`
- Screenshots: `/Users/danielraffel/Code/spectr/planning/screenshots/audit-2026-04-30/` (46 files, all `native-*.png`)
