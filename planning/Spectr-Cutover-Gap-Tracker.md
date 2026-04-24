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

## ~~Integration Plan: pulp#711 cutover diff~~ ✅ Executed 2026-04-24

**Executed in Spectr PR #17, merged at 14:51 UTC on 2026-04-24.** Kept
below for historical reference + as a template for the next cutover
(pulp#468).

Outcome:
- Spectr now uses `pulp::view::EditorBridge` (Pulp v0.41.1+)
- `host_bridge.{hpp,cpp}` deleted (~250 LOC)
- 109/109 tests pass
- Net LOC: -125 (matched the prediction)
- Follow-up gap identified: no symmetric `detach_webview()` on
  `EditorBridge` — a defensive teardown step would close the race
  between panel_'s destructor and bridge_'s destructor. Filed as a
  pulp FR; cross-linked as `Open Gaps` row below.

---

Original runbook (pulp#711 prediction):

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

## Learnings (from the pulp#711 cutover)

Durable patterns surfaced during this integration. Each one saved me or
will save a future agent hours next time.

1. **Stand-in-then-cutover is safer than waiting.** Writing
   `spectr::HostBridge` as a local implementation that mirrored the
   proposed Pulp API let me (a) validate the upstream design against a
   real consumer before it landed, and (b) made the eventual cutover a
   mechanical rename instead of a design exercise under time pressure.
   Cost: ~1 hour of stand-in code that got deleted. Benefit: caught the
   `get_int` missing helper in upstream review, pre-wrote the Integration
   Plan as a runbook, absorbed a failed design before it shipped.

2. **Framework fixes live upstream; plugins don't hack around them.**
   Code reviewer flagged the missing `bridge_.detach_webview()` as a
   tear-down race window. Did NOT add a local workaround. Filed as
   pulp#726 for the symmetric API and documented the current state
   as a comment in `editor_view.hpp` so the gap is visible. This is
   how the downstream/upstream contract stays clean.

3. **Don't edit source during `shipyard run`.** The configure stage
   reads the live working tree. Deleting or renaming files during a
   30+ minute run produces non-deterministic failures that look like
   regressions but are just races (see
   [Shipyard#238](https://github.com/danielraffel/Shipyard/issues/238)).
   Mitigation until that lands: park edits until a run settles, or
   use a separate worktree for the in-progress work.

4. **Member destruction order is load-bearing around bridges.** A
   `pulp::view::EditorBridge` must outlive its `WebViewPanel` or
   in-flight messages teardown into a dead bridge. Declare
   `EditorBridge` BEFORE the `unique_ptr<WebViewPanel>` in the
   owning class so reverse-declaration-order destruction runs the
   panel first. Non-movable/non-copyable is load-bearing too — it's
   a pulp#711 compile-time guarantee against accidentally landing
   in a moveable container. See comment block at the top of the
   `EditorView` private section.

5. **Pin to downloadable SDK as soon as a release lands.** Every
   time a local-built SDK gets baked into a `pulp.toml` or shipyard
   config, it's a hidden cross-machine portability break. As soon
   as the release-cli pipeline catches up, flip back to
   `~/.pulp/sdk/<version>` (downloaded) so anyone checking out the
   repo can reproduce the build without custom Pulp setup.

6. **Async-via-GitHub is the actual agent IPC.** Cross-session
   agent coordination worked best as checkpoint comments on the
   tracking issue (pulp#709 / #468 / #711). The pulp-side agent
   posted "API frozen", "tests green", "PR opened"; I responded
   with the consumer-side divergence audit. No native Claude
   agent-to-agent channel was needed — GitHub queues the back-and-forth
   and gives a durable audit trail.

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
