# Spectr UI Park Notes — 2026-04-22

Status: **V1 editor UI parked pending `danielraffel/pulp#651`.**

## What happened

V1's audio + state layers landed cleanly through Milestone 4. When
Milestone 5 reached "build the editor that matches the prototype," the
native-C++ canvas path produced something that looked nothing like the
prototype (five auto-generated knobs on a black panel vs. the
zoomable-filter-bank canvas with chrome, rail, and gradient spectrum).

The right tool for prototype parity is **embed the existing prototype
HTML via Pulp's WebView bridge** — the HTML already renders pixel-perfect,
and the framework has a `WebViewPanel` class that wraps CHOC WebView.

The attempt compiled and linked cleanly (after rebuilding the local
`0.34.0` SDK with `PULP_BUILD_WEBVIEW=ON`). It runtime-failed at attach
time: `View::window_host()` returns `nullptr` for views inside a plugin
editor, because Pulp's plugin editor path uses `PluginViewHost` (distinct
from `WindowHost`) and doesn't expose the host handle to `View`
subclasses. Without that NSView handle, the `WebViewPanel`'s
`native_handle()` has nowhere to attach.

## What exists

- **`main`** — unchanged. M4 state registration + M5 native-canvas
  skeleton. Builds. 44/44 tests green. Editor shows the skeleton knob
  panel, not the prototype.
- **`feature/webview-editor-parked`** — full webview-embed implementation.
  HTML copied as a bundled asset, `EditorView` constructed, bridge wired.
  Blocked at runtime by the Pulp API gap. Ready to resume once `#651`
  lands.

## The framework gap — `danielraffel/pulp#651`

Title: *Views have no accessor back to their PluginViewHost / WindowHost
— blocks WebView embedding in plugin editors.*

The issue proposes three API options; we're OK with any of them:

1. Add `View::plugin_view_host()` that plugin editors populate and `add_child` propagates.
2. Add `Processor::on_editor_host_ready(PluginViewHost&)` hook.
3. Unify `PluginViewHost` under the `WindowHost` interface and wire `set_window_host()` through.

Acceptance criteria in the issue include a new `examples/webview-plugin`
showing a minimal Processor with a WebView editor.

## Resume checklist

When `#651` lands:

1. `git fetch origin` on both Pulp and Spectr.
2. Rebuild the Spectr SDK with the new Pulp (will pick up the fix).
3. On Spectr: `git checkout feature/webview-editor-parked && git rebase origin/main`.
4. Replace the `attach_now()` walk-to-root hack with whichever accessor
   the new API provides.
5. Add a minimal JS↔C++ bridge for:
   - band gain / mute per slot
   - `band_count` layout
   - `view_min_hz` / `view_max_hz` viewport
   - `response_mode` / `engine_mode`
   - pattern save/load, snapshot A/B, morph
6. Screenshot-verify against the prototype before claiming parity.
7. `shipyard pr` to merge back to main.

## What NOT to do while parked

- Do **not** start a parallel native-canvas port. Prototype-faithful
  native would be weeks of work and the webview path will close the gap
  in a single commit once `#651` lands.
- Do **not** install the current M5 build into DAWs as "ready to try" —
  it's an audio-working skeleton, not a product.
- Do **not** touch `main`'s audio/state layer — M4 is correct and tested;
  we resume the editor on top of it.

## Non-UI work that CAN proceed in parallel

All of this is route-agnostic to the editor:

- **DSP improvements** in M11 polish (windowed STFT for deeper cuts on
  non-aligned tones).
- **Preset file format (M9)** — Spectr-owned JSON wrapper per handoff §7.
- **Snapshot / A-B / morph plumbing (M8)** at the data layer (no UI needed).
- **Pattern library data model (M7)** — factory patterns + JSON
  import/export of user patterns, tested headlessly.
- **Format validation (M10)** — `pulp validate` green.

These run on main, don't need the editor, and set up the data plumbing
the webview bridge will sync against when `#651` lands.
