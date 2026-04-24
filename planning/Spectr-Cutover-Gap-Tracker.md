# Spectr WebView → Native Cutover Gap Tracker

> Living doc. Every Pulp-side gap that blocks Spectr from removing its WebView
> dependency lands here with a cross-reference.

## Why this doc exists

Spectr's editor ships a Claude-Design-export HTML
(`resources/editor.html`) rendered via `pulp::view::WebViewPanel`. The
WebView is a stand-in. The destination is Spectr's editor running
through Pulp's native path — DOM imported via
[pulp#468](https://github.com/danielraffel/pulp/issues/468), JS
runtime via QuickJS, rendering via Dawn/Skia.

Everything the WebView does that the native path can't yet do is a
concrete gap. Each one gets an issue filed against Pulp and tracked
here until closure.

## Contract (the stable seam)

Spectr's C++ side talks to whatever drives the UI through the editor
bridge:

- Schema: `include/spectr/editor_bridge.hpp`
- Dispatcher: `src/editor_bridge.cpp` — `dispatch_editor_message_json()`
- Generic framework (stand-in for pulp#709): `include/spectr/host_bridge.hpp`
  + `src/host_bridge.cpp`
- Tests: `test/test_editor_bridge.cpp`

The bridge is **renderer-agnostic by construction**. WebView JS issues
messages today; the native-imported JS will issue the same messages
tomorrow. Nothing in `editor_bridge.cpp`'s Spectr-specific handlers
needs to change during the cutover — only the generic framework
moves from in-repo (`spectr::HostBridge`) to Pulp SDK
(`pulp::view::EditorBridge`).

## Integration Plan: pulp#711 cutover diff

[pulp#711](https://github.com/danielraffel/pulp/pull/711) lifts
`spectr::HostBridge` into Pulp as `pulp::view::EditorBridge`. Design
verified against Spectr's fixture; the pulp-side agent audited all
11 message types and confirmed cutover is mechanical.

When pulp#711 merges **and** a Pulp SDK release ships with it:

### Branch setup

```
git checkout main && git pull
git checkout -b feature/editor-bridge-cutover
```

### Pin bump

```diff
# pulp.toml (gitignored, local-only)
-sdk_version = "0.40.0"
-sdk_path = "/Users/danielraffel/.pulp/sdk-local/darwin-arm64/0.40.0"
+sdk_version = "0.41.0"   # or whichever Pulp SDK release ships #711
+sdk_path = "/Users/danielraffel/.pulp/sdk/0.41.0"

# CMakeLists.txt
-find_package(Pulp 0.40.0 REQUIRED)
+find_package(Pulp 0.41.0 REQUIRED)

# .shipyard/config.toml
-  -DCMAKE_PREFIX_PATH=$HOME/.pulp/sdk-local/darwin-arm64/0.40.0
+  -DCMAKE_PREFIX_PATH=$HOME/.pulp/sdk/0.41.0
```

### Delete the stand-in (2 files, ~250 LOC removed)

```
rm include/spectr/host_bridge.hpp
rm src/host_bridge.cpp
```

### CMakeLists.txt — drop stand-in from sources

```diff
 set(SPECTR_SOURCES
     src/spectr.cpp
     src/block_fft_engine.cpp
     src/edit_engine.cpp
     src/editor_bridge.cpp
-    src/host_bridge.cpp
     src/pattern.cpp
     src/preset_format.cpp
     src/snapshot.cpp
     src/windowed_stft_engine.cpp
     src/ui/editor_view.cpp
 )

 set(SPECTR_HEADERS
     include/spectr/spectr.hpp
     ...
     include/spectr/editor_bridge.hpp
-    include/spectr/host_bridge.hpp
     include/spectr/preset_format.hpp
     ...
 )
```

### `include/spectr/editor_bridge.hpp` — drop `EditorBridgeState`

The struct held a single `std::optional<BandSnapshot> drag_snap`.
Pulp#711's `pulp::view::EditorBridge` explicitly keeps drag state on
the consumer. Move the field onto `EditorView` (or wherever the
handler closures capture from). Drop the struct + its param on the
public dispatch functions; callers no longer pass it.

### `src/editor_bridge.cpp` — mechanical rename

```diff
-#include "spectr/host_bridge.hpp"
+#include <pulp/view/editor_bridge.hpp>
```

Then search-and-replace across the file:

```
spectr::HostBridge → pulp::view::EditorBridge
```

…and drop the envelope-rebuilding path in `dispatch_editor_message`
(framework takes the envelope JSON string directly via
`dispatch_json()`). The handler bodies stay **identical**. Example
`paint` handler diff:

```diff
 bridge.add_handler("paint",
-    [&state, &plugin](const choc::value::ValueView& p) -> std::string {
-        if (!state.drag_snap) return HostBridge::err_response("paint without paint_start");
+    [this](const choc::value::ValueView& p) -> std::string {
+        if (!drag_snap_) return pulp::view::EditorBridge::err_response("paint without paint_start");
         const auto mode = parse_edit_mode_(
-            HostBridge::get_string(p, "mode"));
+            pulp::view::EditorBridge::get_string(p, "mode"));
         if (!mode) return
-            HostBridge::err_response("unknown edit mode");
+            pulp::view::EditorBridge::err_response("unknown edit mode");
         // ... rest of the handler unchanged ...
     });
```

### `src/ui/editor_view.cpp` — replace `set_message_handler` with `attach_webview`

```diff
-panel_->set_message_handler([this](const pulp::view::WebViewMessage& m) -> std::string {
-    return handle_message_(m);
-});
+bridge_.attach_webview(*panel_);
+// bridge_ is an EditorView member, owns the lifetime of the handlers
+// registered at EditorView construction.
```

### Drop `dispatch_editor_message` / `dispatch_editor_message_json` free functions

These are no-ops once the `attach_webview` path routes through the
framework directly. Tests that called them go through
`bridge_.dispatch_json(envelope)` on a `pulp::view::EditorBridge`
instance built in the test setup.

### Test delta

`test/test_editor_bridge.cpp` — minor changes:

- `Rig` struct builds a `pulp::view::EditorBridge` instead of
  `EditorBridgeState bridge`.
- Test assertions against response substrings (`"malformed JSON"`,
  `"unknown message type"`, etc.) stay **identical** — the pulp agent
  confirmed substring compatibility in [pulp#709 checkpoint
  comment](https://github.com/danielraffel/pulp/issues/709#issuecomment-4311373819).

All 12 bridge test cases + the plugin-state persistence cases should
pass verbatim.

### Expected diff magnitude

| Files | Delta |
|---|---|
| Deleted | 2 (`host_bridge.hpp`, `host_bridge.cpp`) |
| Modified | 5 (`editor_bridge.hpp/cpp`, `editor_view.hpp/cpp`, `CMakeLists.txt`) |
| Net LOC | ~−200 (stand-in goes away; handlers stay) |
| Tests | 110/110 pass without modification |
| Pin bump | `pulp.toml` + `CMakeLists.txt` + `.shipyard/config.toml` |

### `get_int` follow-up

Pulp#711 doesn't ship `get_int` (I asked on #709, agent deferred). My
current `param_set` handler uses `get_uint` which works since
`pulp::state::ParamID` is `uint32_t`. No cutover-blocking issue;
revisit if a signed-integer payload field ever appears. Flagged in
[pulp#711 comment](https://github.com/danielraffel/pulp/pull/711#issuecomment-4311702341)
for a follow-up PR.

## Integration driver

**Spectr side:** this repo's maintainer. When the pulp#468 import PR
lands (or the Pulp SDK release that ships it), we open
`feature/native-editor-cutover`, add a parallel `EditorView` native
path alongside the WebView one, and A/B screenshot-compare against
every key UI state.

**Pulp side:** whoever's taking pulp#468 (currently the agent in that
thread — see
[pulp#468 comment 4311183959](https://github.com/danielraffel/pulp/issues/468#issuecomment-4311183959)
and the follow-up locking the collaboration model at
[comment 4311225456](https://github.com/danielraffel/pulp/issues/468#issuecomment-4311225456)).

## Workflow

When a gap is found during integration:

1. File an issue on `danielraffel/pulp` with the minimal repro against
   `resources/editor.html`.
2. Add a row to the **Open Gaps** table below.
3. Continue integration around it if possible (feature-flag, fall back
   to WebView for the affected surface).
4. When the Pulp fix merges + ships in an SDK release, bump Spectr's
   pin, re-run the screenshot A/B, close the row (move to **Closed
   Gaps**).

When every row is in the closed table AND the full screenshot A/B
matches visually, the WebView path can be removed. That's the
cutover.

## Expected gap categories

These are educated guesses about where the native path is likely to
diverge from WebView — not filed issues yet, just the map we'll use
while the integration lane runs. Each turns into a real issue as the
first repro surfaces it.

| Category | What to compare | Likely subsystem on Pulp side |
|---|---|---|
| HTML import fidelity | DOM shape after import vs live DOM in WebView | pulp#468 itself |
| CSS layout subset | Flex/grid/position edge cases the prototype uses | Yoga coverage |
| Font loading + rendering | Inter font family + weights the prototype embeds | Pulp text shaper |
| SVG fidelity | `#__bundler_thumbnail` decoration, any inline SVG the prototype uses | Canvas + SVG parser |
| Canvas 2D APIs | Drawing ops the prototype uses for the spectrogram / band rail | Pulp canvas |
| Pointer event model | Paint-drag start/move/end vs WebView pointer events | View input |
| Animation timing | CSS transitions, rAF-driven render loops | Animation system |
| JS runtime surface | DOM APIs + fetch + timers the prototype's bundler uses | QuickJS host bindings |
| Bundler bootstrap | `<script type="__bundler/manifest">` + `<script type="__bundler/template">` unpacking | Import-time DOM transforms |
| postMessage primitive | Native equivalent of `window.webkit.messageHandlers.<name>.postMessage` | JS runtime binding |

## Open Gaps

<!-- Add rows as issues surface. Format:
| Gap | Pulp issue | Filed | Severity | Blocks cutover? | Notes |
-->

_(none yet — first gaps surface once M9.5 slice 3 is wired and pulp#468
reaches Phase 1 implementation)_

## Closed Gaps

_(none yet)_

## Related issues and PRs

- [pulp#468](https://github.com/danielraffel/pulp/issues/468) —
  HTML-design-export importer (this is the prime mover)
- [Spectr PR #2](https://github.com/danielraffel/spectr/pull/2) —
  original WebView editor embed
- [Spectr PR #8](https://github.com/danielraffel/spectr/pull/8) —
  editor bridge foundation (M9.5 slice 1)
- [Spectr PR #11](https://github.com/danielraffel/spectr/pull/11) —
  preset/param bridge + PatternLibrary persistence (M9.5 slice 2)
- pulp SDK subsystem gaps surfaced via this tracker — link each as a
  row in **Open Gaps** when filed.
