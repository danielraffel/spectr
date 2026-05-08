# Spectr Native Bridge Hardening Status - 2026-05-08

Last updated: 2026-05-08 16:25 PDT.

## Current status

This work is not done yet. The native bridge is now launching the real Spectr Claude Design / HTML editor path and it is clearly closer than the earlier placeholder UI, but it is not yet a one-to-one import/runtime match with the WebView/original HTML.

Most important current blockers:

- Native bridge cannot draw/edit bands with the mouse, while the WebView version can draw smoothly.
- User still sees a black rectangular layer over the graph on mouse movement. This may be the status/minimap layer being materialized or hit-tested incorrectly, but it is not proven yet.
- The analyzer/waveform continuous animation is now better supported by evidence: debug logs show rAF callbacks and canvas drawing advancing without mouse movement. Keep watching this, but the main current blocker is pointer/hit-test/edit behavior.
- Native bridge sluggishness is still open for interaction. Idle CPU after the latest non-debug launch settled around `1.4-2.5%`, but debug logging and interaction can spike; do not mark performance fixed until drag/popup flows are sampled.
- Preset/pattern manager popup is missing the shape previews that are visible in WebView.
- Some SVG/icon graphics are missing in native, including the settings gear.
- Some popup/dropdown layout is still off versus WebView.

The goal remains: import the Spectr Claude Design HTML and run it through the native Pulp GPU bridge, not WebView, with visual and behavioral parity against the WebView/original HTML.

## Active pass - 2026-05-08 14:57 PDT

User reports installed Pulp CLI is `v0.78.4`. The current Spectr `build-gpu` cache still points at `/tmp/pulp-sdk-gpu-test/lib/cmake/Pulp`, not the installed CLI SDK path:

```text
Pulp_DIR=/tmp/pulp-sdk-gpu-test/lib/cmake/Pulp
```

Do not assume the running Spectr app is using the newly installed CLI. For this pass, validate against the staged SDK first, then decide whether to reconfigure Spectr to the installed SDK or reinstall the Pulp worktree into `/tmp/pulp-sdk-gpu-test`.

Current merge stance:

- Do not merge the Spectr native-editor code until the real-port validation has been rerun against the current staged SDK and the remaining blockers are either fixed or explicitly filed as Pulp issues.
- Do not merge the broad Pulp import/runtime diff as one opaque change. Split and validate at least the focused pointer/rAF/event fixes separately from import-design CLI/harness/docs work.
- The planning/status doc is safe to merge independently; commit `4177247` already landed it in this worktree.

## Active pass - 2026-05-08 15:10 PDT

Latest user-visible state:

- User still cannot draw/edit bands in native. After the latest relaunch, moving the mouse still shows a black rectangular bar over the graph and band editing does not visibly happen.
- The latest normal native app launch still proved the native GPU path: Metal surface, Dawn initialized, Skia Graphite initialized, and `gpu=true`. It also launched without the previous wheel exception spam until user interaction.
- A debug run with `SPECTR_NATIVE_DEBUG_LOG=1` showed continuous `[rAF-cb#...]` callbacks and ongoing canvas drawing without relying on mouse movement. That suggests the earlier "waveform only updates on mouse move" issue is at least partly resolved by the current Pulp/Spectr staged build.
- The debug process was no longer running when status was updated. Next step is to relaunch debug mode and have the user perform one short drag, then inspect `/tmp/spectr-native-debug.log` for `[pdown]` / `[pmove]` traces.

New focused Pulp fixes added in this pass:

- macOS `scrollWheel:` now populates `MouseEvent.window_position` and modifiers so wheel zoom can know `clientX/clientY`.
- macOS `mouseUp:` now dispatches a bridge `MouseEvent` before legacy `on_mouse_up`, so JS receives `pointerup`.
- `registerWheel` now dispatches deltas plus client/offset coordinates and modifier keys.
- The low-level `__dispatch__` path now synthesizes a wheel event object for one-argument callbacks such as React-style `onWheel(e)`, while preserving the legacy `on(id, "wheel", fn(dx, dy))` contract.
- DOM `addEventListener("wheel", fn)` now receives `deltaX/deltaY`, `clientX/clientY`, offsets, and modifiers.

New focused Spectr port fix added in this pass:

- `native-react/dom-adapter.tsx` now forwards `pointerEvents: 'none'` to Pulp via `setPointerEvents(id, 'none')` using a ref callback. This is intended to prevent noninteractive overlays such as the status banner from stealing graph hits.
- This is not yet proven by user validation because the black rectangle and no-draw behavior still reproduce.

Validation completed in this pass:

```bash
cmake --build build-gpu --target pulp-test-widget-bridge -j 8
./build-gpu/test/pulp-test-widget-bridge "[view][bridge][events]"
./build-gpu/test/pulp-test-widget-bridge
./build-gpu/test/pulp-test-view-host-bridge "PulpView NSEvent mouseUp dispatches JS pointerup subscriber"
cmake --install build-gpu --prefix /tmp/pulp-sdk-gpu-test
npm --prefix native-react run build:port
cmake --build build-gpu --target Spectr-test Spectr_Standalone -j 8
ctest --test-dir build-gpu --output-on-failure
```

Results:

- Pulp focused bridge event test passed: `38 assertions in 1 test case`.
- Pulp full `pulp-test-widget-bridge` passed: `1780 assertions in 309 test cases`.
- Pulp macOS pointer-up integration test passed: `8 assertions in 1 test case`.
- Spectr passed: `109/109` tests.

Current merge stance:

- Do not merge Spectr native-editor parity as complete. Drawing/editing is still user-broken.
- The focused Pulp event fixes are plausible PR material after splitting from the broad import/runtime diff and running the relevant Pulp CI/test set. They address concrete regressions with tests.
- The Spectr `pointerEvents: none` adapter workaround is useful but should stay in the parity branch until the black rectangle / no-draw behavior is explained.
- The broad Pulp import/runtime/harness diff still should not merge as one opaque PR.

Most likely next debugging path:

- Relaunch with `SPECTR_NATIVE_DEBUG_LOG=1`.
- Have the user drag once over the graph.
- If no `[pdown]`/`[pmove]` logs appear, debug native hit-testing and overlay/pointer-events routing. Add temporary macOS hit-test logs around `_dragTarget->id()`.
- If `[pdown]`/`[pmove]` logs appear, debug Spectr coordinate math and state commits (`findBand`, `pxToGain`, `commitGain`, and canvas redraw).
- Separately inspect the black rectangle source by logging the status banner/minimap layer bounds and by confirming `setPointerEvents('none')` actually hits the expected view id.

## Correction from this session

The original `resources/editor.html` / Claude Design export is the real Spectr editor: canvas-driven `FilterBank`, `Chrome`, `TweaksPanel`, `PatternManager`, dropdowns, sliders, modal surfaces, SVG icons, and small canvas previews.

It is not the knob-row UI. The knob-row UI is the older hand-authored `native-react/editor.tsx` skeleton / placeholder path. Do not use that placeholder as evidence for import parity.

The real validation target is:

```bash
npm --prefix native-react run build:port
cmake --build build-gpu --target Spectr-test Spectr_Standalone -j 8
ctest --test-dir build-gpu --output-on-failure
open -n build-gpu/Spectr.app
```

That path bundles `native-react/editor-port.tsx`, `dom-adapter.tsx`, `canvas2d-shim.ts`, and `spectr-editor-extracted.js`.

## Active worktrees

- Pulp: `/tmp/pulp-spectr-import-fix`
  - Branch: `fix/spectr-import-native-bridge-regressions`
  - Purpose: import runtime, native bridge materialization, GPU validation, event/canvas bridge hardening, and validation harness work.
  - Commit/landing state: no local commits on this branch as of 2026-05-08 16:03 PDT; the branch is currently `origin/main` plus a broad dirty worktree.
  - Main state: none of the Pulp fixes from this worktree should be assumed merged to `main`.
- Spectr: `/tmp/spectr-native-pump-fix`
  - Branch: `fix/native-editor-no-pump-33`
  - Purpose: native standalone validation against a staged Pulp GPU SDK.
  - Commit/landing state: only the planning/status doc commits are committed on this branch: `4177247` and `9257166`. The Spectr native-editor code changes are still uncommitted.
  - Main state: these commits are not on `origin/main`; they sit on top of `origin/feature/native-react-editor`.
- Staged Pulp SDK used by Spectr: `/tmp/pulp-sdk-gpu-test`

## Landing / main status

Nothing in this hardening pass should be described as "landed on main" yet.

Committed locally:

- Spectr planning doc commit `4177247` — `Document Spectr native bridge hardening status`.
- Spectr planning doc commit `9257166` — `Update native bridge hardening status`.

Uncommitted locally:

- Pulp event/import/runtime/native bridge changes in `/tmp/pulp-spectr-import-fix`.
- Spectr native-editor build/bridge changes in `/tmp/spectr-native-pump-fix`.

Do not close GitHub issues based only on this worktree state. Close or mark fixed only after the relevant focused PR is merged and the stated validation is rerun against the merged branch or released SDK.

## Issue close matrix

| Issue | Current local state | Close when |
| --- | --- | --- |
| `pulp#1687` darwin-arm64 SDK omits `MacGpuWindowHost` / `use_gpu=true` silently CG-fallbacks | Likely addressed by local Pulp SDK install/release/skia-cache work, but still uncommitted and not on main. | A Pulp PR lands and a packaged SDK/release is verified to include the GPU host/Skia cache; a downstream Spectr build logs actual Dawn/Skia `gpu=true` from the resolved host. |
| `pulp#1689` SDK ships no `pulp-import-design` binary | Local Pulp diff includes import-design tool/install work, but it is uncommitted and not on main. | SDK install/release artifact includes `pulp-import-design`, and install-layout/release smoke tests pass from the packaged SDK. |
| `pulp#1690` `--execute-bundle` stops at loader shell, misses post-`replaceWith` React tree | Local Pulp diff includes runtime/import execution hardening and tests, but it is uncommitted and not on main. | Focused import-design/runtime PR lands with tests proving bundled React output materializes past the loader shell. |
| `pulp#1688` standalone `gpu=true` log reflects requested flag, not resolved host | Local Pulp diff includes `WindowHost::is_gpu()` and host reporting work, but it is uncommitted and not on main. | Pulp PR lands and standalone logs/reporting are verified to reflect the resolved host, including fallback cases. |
| `pulp#1691` static-lane import slurps `<script>` source as `createLabel` text | Local Pulp diff includes native generated-output filtering for script/style/head noise, but it is uncommitted and not on main. | Import PR lands with regression tests proving inert script/style/head content is not emitted as visible native labels while useful JSON script data remains preserved. |
| `spectr#33` native bridge ANR / obsolete 60 Hz pump thread | Spectr worktree has uncommitted code removing the local pump thread and relying on Pulp host/frame pumping. Planning docs are committed, code is not. | Spectr PR lands the native-editor code change and a native standalone launch confirms no ANR. If `spectr#33` scope is only the ANR/pump thread, it can close then; keep separate parity issues open for drawing, black rectangle, SVG/icons, popup previews/layout, and performance. |

Newer follow-up Pulp issues also remain open unless their focused PRs land: `pulp#1693` native GPU materialization loses live canvas/click behavior, `pulp#1694` import-design native GPU parity/perf validation, `pulp#1695` Claude import async/parser hardening, and `pulp#1696` frontend ecosystem semantics.

## GPU/native evidence

The latest successful real-port launch proved the native GPU path, not WebView:

- `Spectr native editor: loaded editor.js (459462 bytes)`
- `GpuSurface: created Metal surface from CAMetalLayer`
- `GpuSurface: Dawn initialized (surface: presentable)`
- `SkiaSurface: Graphite initialized on shared Dawn device (presentable: yes)`
- `Standalone: editor window open (... gpu=true, mode=autoui, chrome=editor-only, inspector=ready)`
- Visible real editor labels included `SPECTR`, `ZOOMABLE FILTER BANK`, `LIVE`, `PRECISION`, `IIR`, `FFT`, `HYBRID`, `32 bands`, `CLEAR`, `SCULPT`, `PEAK`, `PRESETS`, and `SNAPSHOT`.

Pulp capability state for the user-facing stack:

- WebGPU Native via Dawn: supported as native GPU infrastructure and in the native Three.js/WebGPU bridge lane. This is not yet a drop-in browser `navigator.gpu` surface for arbitrary imported apps.
- WGSL: supported in native bridge/custom GPU paths where those bridge APIs are used.
- Skia Graphite: supported and used for GPU 2D rendering over Dawn/Metal in this build.
- SDL3: present for cross-platform windowing/input, with Apple native hosts currently the most validated path.
- JS engine abstraction: supported across QuickJS/JSC/V8. Current staged SDK is QuickJS, which is likely a major contributor to sluggishness for this large React/canvas bundle.
- Flexible assets/resources: supported through Pulp's asset/resource system, but the Spectr import still has SVG/icon/canvas-preview materialization gaps.

## What changed in Pulp

The Pulp worktree contains the broader import/runtime hardening slice:

- Claude import runtime can execute bundled React exports instead of only capturing the loader shell.
- Runtime settling was expanded for React-style async/event/lifecycle assumptions.
- Web-compat shims were expanded around `window`, `document`, `EventTarget`, `createElementNS`, namespace attrs, event listener behavior, and scheduler diagnostics.
- Native generated output skips inert script/style/head noise while preserving useful JSON script data.
- Native common style emission improved for inline CSS mapped into Pulp primitives.
- `pulp import-design --validate` now defaults toward native GPU validation and records native bridge/webview/timing/click diff metadata.
- `WindowHost::is_gpu()` exists and macOS/iOS hosts report GPU when backed by Skia/Dawn surfaces.
- SDK install/release flow now includes the Skia cache needed by downstream native bridge consumers.
- Repo-reference review notes from `htmlparser2`, `react-testing-library`, `jsdom`, and `happy-dom` are captured in `docs/reports/claude-import-runtime-js-dom-review-2026-05-08.md`.

Additional focused Pulp fix in this pass:

- `core/view/src/widget_bridge.cpp` now sends `clientX` and `clientY` on pointer drag/move events, not only `offsetX` and `offsetY`.
- `test/test_widget_bridge.cpp` now asserts pointer move payloads include `clientX/clientY`.
- Targeted validation passed:

```bash
cmake --build build-gpu --target pulp-test-widget-bridge -j 8
./build-gpu/test/pulp-test-widget-bridge "[view][bridge][events]"
```

Important: this fix has not yet been manually confirmed against a relaunched native Spectr app. If drawing still fails after relaunching against the updated SDK, next suspects are pointer capture, event target/bubbling across the two canvas overlays, `document`-level pointerup listeners, `getBoundingClientRect()`, `buttons/button` fields, and coordinate basis mismatches.

## What changed in Spectr

The Spectr worktree now has a native standalone build that avoids WebView symbols when `SPECTR_NATIVE_EDITOR=ON`:

- `CMakeLists.txt` only includes the WebView `EditorView` sources when native editor mode is off.
- `src/spectr.cpp` gates WebView includes and casts behind `!SPECTR_NATIVE_EDITOR`.
- `NativeEditorView` no longer runs its own local repaint thread. Repaint/frame pumping is owned by Pulp's `WindowHost` and `WidgetBridge`.
- `NativeEditorView` debug JS logging is now gated behind `SPECTR_NATIVE_DEBUG_LOG`, so normal runs do not flood the console/log file.

Current local validation after removing the placeholder-knob path:

```bash
cmake --build build-gpu --target Spectr-test Spectr_Standalone -j 8
ctest --test-dir build-gpu --output-on-failure
```

Result: `109/109` Spectr tests passed.

## GitHub tracking

Filed in `danielraffel/pulp`:

- `#1693` Claude import native GPU materialization loses live canvas/click behavior.
- `#1694` import-design validation should enforce native GPU parity/perf budgets.
- `#1695` harden Claude import runtime with async quiescence/parser coverage.
- `#1696` Design import metadata: track frontend ecosystem semantics beyond React.

Potential follow-up issues to file if not folded into the above:

- Native bridge pointer parity for canvas editing: pointer capture, `clientX/clientY`, `buttons`, document-level pointer routing, and overlay canvas hit-testing.
- Native bridge rAF/invalidation parity: continuous animation must not depend on mouse movement.
- Native bridge SVG materialization parity: imported `svg/path/circle/rect/line`, `currentColor`, stroke/fill inheritance, and icon sizing.
- Native canvas preview parity: small canvas previews inside popups/dropdowns must paint after mount and repaint when opened.
- Native bridge performance budget for large imported React/canvas bundles: record JS engine, frame time, CPU sample, command count, and whether V8/JSC is required.

## Open work

### 1. Band drawing

WebView can draw bands smoothly. Native currently cannot draw bands at all from the user's latest manual test.

Required behavior:

- Drag on the filter bank to edit bands in `SCULPT` and other edit modes.
- Mouse wheel zooms the frequency viewport.
- Pointer interaction should not corrupt or pause the analyzer/waveform.

Likely contracts to validate:

- `pointerdown`, `pointermove`, `pointerup`, `pointercancel`
- `clientX/clientY`, `offsetX/offsetY`, `buttons`, `button`, `pointerId`, `pointerType`
- `setPointerCapture` / `releasePointerCapture`
- `getBoundingClientRect()` on the wrapper and both canvas layers
- event routing when the pointer lands on either of the two overlay canvases
- document/window listeners used for outside click and pointer-up completion

### 2. Waveform/analyzer animation

The original/WebView editor renders a live simulated analyzer/waveform even without mouse input. Native currently appears to update only in response to mouse movement.

Next checks:

- Instrument `requestAnimationFrame` scheduling and callback execution in the real port.
- Verify `__requestFrame__` asks the native host for another frame even when there is no input.
- Verify `WindowHost` invalidation actually schedules repaint continuously while rAF callbacks are queued.
- Confirm the draw loop remains active after the first frame and after opening popups.

### 3. Popup previews and SVG/icon graphics

WebView shows pattern/metaphor shape previews in popups. Native does not.

Likely gaps:

- `MetaphorPreview` uses tiny `<canvas>` elements inside dropdown rows; those may not paint after mount/open.
- Inline SVG icons rely on `svg`, `path`, `circle`, stroke/fill inheritance, `currentColor`, and viewBox sizing. The current adapter maps some SVG tags to placeholders and only partially upgrades `path`.
- Gear icon specifically includes both a `<path>` and a `<circle>`, so missing `circle` support or inherited stroke color can make it incomplete or invisible.

### 4. Layout parity

Popup/dropdown layout still differs from WebView.

Next checks:

- Screenshot compare WebView/original HTML against native at 1320x860 and one smaller size.
- Focus on PatternManager, edit-mode popover, analyzer popover, settings modal, and bottom toolbar overflow.
- Attribute mismatches to either Yoga layout limitations, missing CSS property mapping, or adapter translation bugs.

### 5. Sluggishness

Performance is a blocker.

Current evidence:

- Prior sample showed roughly 60% CPU while running the native real port.
- Hot path was primarily `MacGpuWindowHost::render_frame` -> `NativeEditorView::paint` -> `WidgetBridge::service_frame_callbacks()` -> QuickJS evaluation, with heavy canvas bridge traffic.
- Debug logging has since been gated off, but the app needs a fresh sample after relaunch.

Next checks:

- Relaunch native without `SPECTR_NATIVE_DEBUG_LOG`.
- Run `ps` and `sample` again.
- Record JS engine, frame callback count, canvas command count, and frame pacing.
- Try JSC or V8 build only after confirming the current QuickJS baseline; do not silently change engines.

## Final push status - 2026-05-08 16:25 PDT

All outstanding work committed and pushed:

- **Spectr** (`fix/native-editor-no-pump-33`): 4 commits pushed — 2 planning doc commits + 1 code commit removing the local pump thread, gating debug logging behind `SPECTR_NATIVE_DEBUG_LOG`, excluding WebView sources when `SPECTR_NATIVE_EDITOR=ON`, and forwarding `pointerEvents: 'none'` to the Pulp bridge.
- **Pulp** (`fix/spectr-import-native-bridge-regressions`): 5 focused commits pushed:
  1. `fix(bridge)`: pointer/wheel event hardening and WindowHost GPU reporting
  2. `feat(import)`: expand CSS parsing, add validation renderer modes
  3. `feat(runtime)`: harden import runtime execution and web-compat shims
  4. `feat(sdk)`: include Skia cache in install/release, GPU standalone reporting
  5. `docs`: Claude import runtime JS DOM review notes

The Pulp SDK was reinstalled into `/tmp/pulp-sdk-gpu-test` and Spectr was rebuilt against it. 109/109 Spectr tests pass.

### Band drawing root cause

The black rectangle on mouse movement is the **hover tooltip** drawn by `drawHover()` in `spectr-editor-extracted.js` (line 1577). It draws `rgba(10,14,20,0.88)` at the mouse position. The overlay canvas's `clearRect` is isolated from sibling canvases by `save_layer` in `canvas_widget.cpp` (lines 196-199), so the main canvas bands should not be erased. However:

- The hover tooltip text may not render in native (possible font fallback or `fillText` bridge issue).
- Pointer events may still not route correctly for band editing even after the `clientX/clientY` and `pointerEvents:'none'` fixes.

### Next validation steps

1. Rebuild against the updated SDK:
   ```bash
   cd /tmp/spectr-native-pump-fix
   cmake --install /tmp/pulp-spectr-import-fix/build-gpu --prefix /tmp/pulp-sdk-gpu-test
   cmake --build build-gpu --target Spectr-test Spectr_Standalone -j 8
   ctest --test-dir build-gpu --output-on-failure
   ```
2. Launch with debug logging:
   ```bash
   SPECTR_NATIVE_DEBUG_LOG=1 open -n build-gpu/Spectr.app
   ```
3. Test band drawing: click and drag over the filter bank canvas.
4. Check `/tmp/spectr-native-debug.log` for `[pdown]`/`[pmove]` traces.
5. If pointer events arrive but drawing still fails, the next suspects are the hover tooltip font rendering and coordinate math in `findBand`/`pxToGain`/`commitGain`.

### Remaining open items (not blockable from this session)

- **Band drawing**: blocked on user validation with updated SDK + pointer event fixes. Hover tooltip text rendering (Inter font in native bridge) may also need fixing.
- **Waveform/analyzer animation**: debug logs show rAF callbacks running continuously; user verification needed.
- **Popup previews and SVG/icon graphics**: still open.
- **Layout parity**: still open.
- **Sluggishness**: needs fresh sample after non-debug relaunch.

## Current runnable commands

Rebuild and launch the real native bridge editor:

```bash
cd /tmp/spectr-native-pump-fix
npm --prefix native-react run build:port
cmake --build build-gpu --target Spectr-test Spectr_Standalone -j 8
ctest --test-dir build-gpu --output-on-failure
open -n build-gpu/Spectr.app
```

Capture logs:

```bash
rm -f /tmp/spectr-native-port.log
open -F -n -o /tmp/spectr-native-port.log --stderr /tmp/spectr-native-port.log build-gpu/Spectr.app
tail -120 /tmp/spectr-native-port.log
```

Sample CPU after launch:

```bash
pgrep -fl '/tmp/spectr-native-pump-fix/build-gpu/Spectr.app/Contents/MacOS/Spectr'
ps -p <pid> -o pid,%cpu,%mem,time,command
sample <pid> 3 -file /tmp/spectr-native-port-sample-after-pointer.txt
```

## Recommended next-agent path

1. Start from the two worktrees above.
2. Reinstall the updated Pulp SDK if the Spectr app is not linked against the latest Pulp pointer-event fix:
   ```bash
   cd /tmp/pulp-spectr-import-fix
   cmake --install build-gpu --prefix /tmp/pulp-sdk-gpu-test
   ```
3. Rebuild the real Spectr port with `npm --prefix native-react run build:port`.
4. Launch native and manually validate the exact user blockers:
   - band drawing
   - wheel zoom
   - continuous waveform/analyzer motion without mouse movement
   - preset/pattern popup previews
   - settings gear and other SVG icons
   - popup/dropdown layout
   - sluggishness
5. For each mismatch, decide whether it is:
   - a Pulp bridge/materialization gap, to fix in `/tmp/pulp-spectr-import-fix`;
   - a Spectr adapter issue, to fix in `/tmp/spectr-native-pump-fix`;
   - an expected design-import limitation, to record against a Pulp issue.
6. Keep commits split by repo and purpose:
   - Pulp PR: import runtime/GPU validation/harness/event/canvas bridge work.
   - Spectr PR: native-editor build/wiring/real-port validation work.

## Commit status

No commits have been created yet in either worktree.

Current local dirty state:

- Pulp has the broad import/runtime hardening diff plus the pointer-move `clientX/clientY` fix and repo review report.
- Spectr has native-editor build/wiring changes, debug-log gating, and this status document.
- `/tmp/spectr-native-pump-fix/build-gpu/` is untracked build output and should not be committed.

Before committing, run:

```bash
git -C /tmp/pulp-spectr-import-fix diff --check
git -C /tmp/spectr-native-pump-fix diff --check
```
