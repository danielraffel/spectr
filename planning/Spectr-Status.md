# Spectr Status — Live Handoff Dashboard

_Last updated: 2026-04-22 (M7 complete) — read this first if resuming work in a new session._

This doc is the single-page state-of-the-world for Spectr. Every commit
to `main` should refresh the dates + "what landed" bullets at the top.

## Branch state

- **`main`** — audio + state layer landed (M1-M4 + M5 native skeleton).
  Editor is a knob-panel stub; **do not install this to DAWs for user
  review**, it's not prototype-faithful.
- **`feature/webview-editor-parked`** — webview editor embedding the
  prototype HTML. Renders pixel-faithful except for two cosmetic
  artifacts (see "Open cosmetic debt" below). **Holding the merge until
  upstream Pulp fixes land so we ship clean, not hacky.**

## In-flight Pulp issues (all OPEN)

The user is landing these fixes upstream. Spectr picks them up via
Task #32.

| # | Title | Status | Unblocks |
|---|---|---|---|
| [#661](https://github.com/danielraffel/pulp/issues/661) | `WindowHost` content-size + resize callback | OPEN | Proper attach sizing; live window resize |
| [#662](https://github.com/danielraffel/pulp/issues/662) | WebView pre-paint white/chrome flash | OPEN | 200ms load flash |
| [#663](https://github.com/danielraffel/pulp/issues/663) | Standalone TabPanel opt-out | OPEN | 32pt bottom strip; tab-header flash artifact |
| [#664](https://github.com/danielraffel/pulp/issues/664) | App icon pipeline | OPEN | V1 Dock icon |

Already landed this session:
- [#625](https://github.com/danielraffel/pulp/issues/625) → PR #628 → Pulp v0.34.0 (supplemental plugin-state hooks). Spectr uses these in M4.
- [#651](https://github.com/danielraffel/pulp/issues/651) → PR #653 → Pulp v0.36.0 (`View::plugin_view_host()` + `PluginViewHost::attach_native_child_view()`). Spectr uses these in M5b (park branch).

## Open cosmetic debt on the park branch

1. **~32pt purple strip at the window bottom.** Caused by the standalone's
   `TabPanel` adding a tab-bar row above our editor area, so the
   `WebView` attach at `(0, 0, w, h)` with `h = preferred_height` leaves
   the row uncovered. Closes when `#661` (true content size) **or**
   `#663` (no TabPanel) merges.
2. **~200ms flash at window open.** The TabPanel's "Editor / Settings"
   tab header shows through the transparent WebView during its
   pre-first-paint window. Closes with `#663` (no TabPanel) or `#662`
   (pre-paint suppression).

**Known-bad short-term hack we DON'T want:** attaching the WebView at
`h += 128` to over-cover the strip. We tried this at commit `b18756e`;
it clipped the bottom rail off the visible window. Reverted at `1509e0d`.
Do not reapply without also reducing the magic number and testing.

## The "fix it right" plan — Task #32

When **#661 AND #663 merge** (in either order):

1. Rebuild the Pulp SDK. If Pulp bumped past `0.36.0`, update Spectr's
   `pulp.toml` / `CMakeLists.txt` pin and re-bootstrap the SDK under
   the new version in `~/.pulp/sdk-local/darwin-arm64/<version>/`.
2. Re-verify `libpulp-view.a` has `WebViewPanel`, `set_plugin_view_host`,
   AND the new `get_content_size` / TabPanel-opt-out symbols:
   `nm ~/.pulp/sdk-local/darwin-arm64/<v>/lib/libpulp-view.a | grep -E "get_content_size|show_settings_tab|tab_panel_optional"`
3. On `feature/webview-editor-parked`: `git rebase origin/main`.
4. In `src/ui/editor_view.cpp` — delete the `if (sz.w <= 0 || sz.h <= 0) { ... 1320/860 fallback }` block and call `WindowHost::get_content_size()` / its `PluginViewHost` counterpart.
5. In Spectr's standalone `main.cpp` — opt out of the `TabPanel` via the
   new config flag from `#663`.
6. If `#662` landed, drop `transparent_background = true` and use the
   new pre-paint option instead.
7. Build, launch standalone, screenshot-compare against prototype.
8. Build AU, install to `~/Library/Audio/Plug-Ins/Components/`, open in a
   DAW, screenshot, visually confirm.
9. Only if both look clean: `git merge --no-ff feature/webview-editor-parked`
   onto main, push, delete the park branch, close Spectr issue #1.
10. Update this doc's "Branch state" and remove this whole section.
11. Update the Pulp `webview-ui` skill (path: `/tmp/pulp-main-<version>/.agents/skills/webview-ui/SKILL.md` on a new worktree) with the proven clean pattern: dual-host adapter, on_view_opened / on_view_resized / on_view_closed lifecycle, content-size API. PR to Pulp.
12. Then resume M6–M11 — if any are already done on main (see "In-progress milestones" below), wire their data to the JS via postMessage in subsequent commits.

## In-progress milestones (route-agnostic work that can proceed on main)

The editor chrome being parked does not block these — they're all data
layer, headless-testable, and set up the bridge the webview will bind to.

- [x] **M6 — Edit modes.** ✅ Landed on main at a6affd9 + followup. All
  five modes (Sculpt / Level / Boost / Flare / Glide) dispatch through
  `spectr::dispatch_edit()` in `include/spectr/edit_engine.hpp`.
  Snapshot-at-drag-start for Boost / Flare / Glide preserved across
  multi-step gestures. 52/52 tests green. UI bindings (keybindings
  S/L/B/F/G → EditMode) ready for JS bridge to call.
- [x] **M7 — Pattern library data model.** ✅ Landed on main. All eight
  factory patterns implemented (Flat, Harmonic series, Alternating,
  Comb, Vocal formants, Sub only, Downward tilt, Air lift — prototype
  generators ported from `patterns.js`). `PatternLibrary` class with
  user-pattern CRUD (save_current, rename, duplicate,
  update_from_current, remove), default-id management, JSON
  import/export with version gate + name-clash (N) suffixing.
  Prototype→dB mapping (`[-1, +1]` / -Infinity → Spectr's `[-60, +12]`
  / muted) preserved. 68/68 tests green. Ready for JS bridge to wire
  pattern-manager modal actions to these methods.
- [ ] **M8 — Snapshot A/B + morph** at the data layer. `ABCompare`
  slots + morph interpolation on canonical band gains.
- [ ] **M9 — Spectr-owned preset file format** per handoff §7. Schema
  v1 JSON wrapper.
- [ ] **M11 — Windowed STFT engine upgrade** to hit the -80 dB mute
  target on non-aligned tones (planning spec §11.4). Replaces the
  block-FFT from M2. Keeps block-FFT as `EngineKind::Fft` fallback;
  new `EngineKind::Hybrid` becomes the default Precision path.

**NOT doing on main while park branch is unmerged:**

- M5b editor work (webview bridge, JS↔C++ message handlers) — lives
  on the park branch.
- M10 format validation — best after M7-M9 to avoid re-running auval
  after every milestone.

## What the new agent should do on resume

1. Read this doc.
2. Read `planning/Spectr-V1-Build-Plan.md` for milestone scope.
3. Read `planning/Spectr-V2-Product-Spec.md` §6.5 / §6.8 for edit-mode
   + pattern-manager contracts.
4. `git -C /Users/danielraffel/Code/spectr log --oneline -5` — check
   for progress since this doc's last-updated date.
5. Check Pulp issue states: `gh issue view 661 662 663 664 -R danielraffel/pulp`.
6. If all four are CLOSED: run Task #32 playbook above.
7. Otherwise: pick up the next unchecked milestone above.

## Quick reference — file paths

- Status doc: `planning/Spectr-Status.md` (this file)
- Milestone plan: `planning/Spectr-V1-Build-Plan.md`
- Product spec: `planning/Spectr-V2-Product-Spec.md`
- Pulp handoff: `planning/Spectr-V2-Pulp-Handoff.md`
- Park rationale: `planning/Spectr-UI-Park-Notes.md`
- Sampler (future): `planning/Spectr-Sampler-Phase-Spec.md`
- Build signoff: `planning/Spectr-Build-Signoff.md`
- Public repo: https://github.com/danielraffel/spectr
- Pulp checkout + SDK source: `/tmp/pulp-main-628` (updated on each SDK rebuild)
- Pulp SDK install: `~/.pulp/sdk-local/darwin-arm64/<version>/`
