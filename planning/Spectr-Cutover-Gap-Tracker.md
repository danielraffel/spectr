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

## Phased migration plan

The native work must not overwrite or silently evolve the current WebView
implementation. The two renderers remain independently buildable until the
native path passes the complete cutover gate.

### Phase W0 — Freeze the WebView reference

Land the completed WebView product as an immutable comparison baseline before
starting native renderer work.

Required receipt:

- exact Spectr commit and Pulp SDK/source identity;
- signed AUv2, VST3, CLAP, and Standalone artifact hashes;
- Release/native/browser/host validation results;
- representative screenshots at minimum, preferred, authored, and enlarged
  sizes;
- scripted receipts for reopen, persistence, band editing, minimap gestures,
  A/B capture and morph, presets, undo/redo, automation, and modulation; and
- explicit rollback/install instructions.

This baseline is retained after native cutover as a behavioral oracle. Native
migration fixes do not land on the reference branch.

### Phase N0 — Ultra architecture and importer audit

Create a separate worktree and branch from the frozen baseline. Use an Ultra
design pass to classify every editor surface as one of:

1. already materializable through Pulp DesignIR/native import;
2. requiring a reusable Pulp widget, canvas, input, accessibility, or runtime
   capability;
3. requiring a renderer-neutral Spectr controller/binding; or
4. intentionally product-specific behavior that should remain small, explicit
   Spectr code.

The output is a source-to-native mapping, a measured gap ledger, and an ordered
implementation plan. It must not be a one-off visual rewrite proposal.

N0 result (2026-08-11): **static DesignIR is not the runtime architecture.**
Browser capture is screenshot-backed, while the offline runtime walker
serializes only a settled element tree. Neither retains React hooks, closures,
effects, timers, event listeners, the analyzer loop, or the imperative canvas
program. The negative oracle rendered only a small inert control stack at the
upper-left of a 1320x860 native frame with a blank canvas. The native product
therefore uses a live QuickJS controller and `@pulp/react` native View /
CanvasWidget tree rendered by Skia Graphite on Dawn. DesignIR remains a
structural audit artifact, never the runtime behavior authority.

The original Claude HTML is also not the shipping source by itself. The frozen
editor is the original plus checked-in adapter transformations for hydration,
analyzer frames, mute semantics, snapshots/morph, patterns, focus, shortcuts,
Shift-drag brushing, and scaling. N1 first canonicalizes that behavior into
stable source modules; runtime exact-string replacement is not a native
architecture.

### Phase N1 — Establish the parallel native shell

- Canonicalize frozen original-plus-adapter behavior into checked-in source
  modules with no runtime `replaceSpectrSource` transform.
- Build a separate native-only artifact. It must not link, embed, or reference
  WebView/WKWebView, `editor.html`, browser capture, screenshot controls, or a
  per-surface browser fallback.
- Run the canonical controller in live QuickJS through `@pulp/react`; create
  native View and CanvasWidget nodes rather than a serialized static snapshot.
- Render through Skia Graphite/Dawn and require `use_gpu=true`,
  `is_gpu_backed=true`, and the fixed 1320x860 design viewport. A CPU renderer,
  blank/error View, static DesignIR fallback, or unavailable bridge fails.
- First vertical slice: minimal native chrome, the real 32-band canvas, one real
  analyzer frame, finite C++ hydration, one single-tap mute, and one sculpt drag
  using the frozen formula. Each gesture must reach authoritative C++ state,
  receive a monotonic revision, and redraw.
- Render later N2/N3 surfaces as visibly disabled native placeholders. Never
  route an unavailable region back to WebView.
- Add a build-time/runtime developer selector so the WebView and native lanes
  can be launched against the same state during comparison. The shipping
  renderer remains explicit; there is no silent runtime fallback.

### Phase N2 — Generalize the missing interactive primitives

Implement missing behavior as reusable Pulp/importer capabilities wherever it
is not inherently Spectr-specific. Expected surfaces include:

- the high-density spectrum/mask canvas and truthful response overlay;
- minimap pan, resize, and zoom interactions;
- paint, sculpt, marquee, group selection, and Shift-drag mute brushes;
- menus, dialogs, text entry, focus, keyboard ownership, clipboard, and file
  workflows;
- animation/timing and categorical mute transitions; and
- accessibility semantics and automation-facing value descriptions.

Confirmed framework prerequisites include a supported native
`ScriptedUiSession`/EditorBridge attachment; deterministic release-capable
ReactDOM-to-`@pulp/react` source transformation; single-delivery pointer
propagation/cancellation and real capture retargeting; context-menu and
double-click events; focused keyboard/key-up/Tab routing; native accessibility
properties and Invoke/Toggle/range actions; modal focus trapping; and explicit
file/clipboard/durable-storage capabilities. These are framework fixtures first,
then Spectr integration tests.

Every generalized capability needs a smaller framework-level fixture in
addition to its Spectr integration test. Exact-string HTML patching is not an
acceptable native architecture.

### Phase N3 — Shared behavioral and visual parity

Drive both renderers with the same canonical state fixtures and interaction
scripts. Compare at least:

- screenshots and layout geometry at every declared size and backing scale;
- pointer, wheel, keyboard, focus, menu, modal, and text-input behavior;
- analyzer publication, drawing, mute/unmute, selection, viewport, presets,
  undo/redo, automation, modulation, A/B capture, and morph;
- close/reopen, host save/restore, malformed-state rejection, and multi-instance
  isolation; and
- AUv2, VST3, CLAP, and Standalone lifecycle behavior.

Visual similarity alone is insufficient. Each lane must produce equivalent
state-transition and interaction receipts.

### Phase N4 — Performance and operational comparison

Measure the WebView and native lanes under the same M5 scenarios:

- cold/warm editor open time and first meaningful frame;
- steady and animated CPU/GPU use;
- memory per open editor and with multiple plugin instances;
- resize latency, frame pacing, and input latency;
- analyzer-on/off cost and high-rate automation/modulation cost; and
- teardown cleanliness, process count, and host-session stability.

Record differences rather than assuming the native result wins every metric.
Any regression accepted for cutover needs an explicit product rationale.

### Phase N5 — Cutover decision

Native becomes the default only when:

- all blocking rows in this tracker are closed;
- the shared parity suite is green;
- native host validation is complete for all four formats;
- no WebView-only persistence, editing, preset, automation, modulation, or
  accessibility behavior remains; and
- the performance/operational report shows the native path is ready for normal
  production use.

The native shipping artifact must also pass a resource, dependency, and symbol
scan proving that it contains no WebView implementation or hidden runtime
fallback. The frozen WebView comparison remains a separately selected artifact,
not an alternate renderer inside the native binary.

Keep the frozen WebView baseline and its evidence available for regression and
historical comparison. Removing the shipping WebView dependency is a separate,
reviewed decision after native qualification, not an automatic consequence of
the first matching screenshot.

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

| Gap | Pulp issue | Filed | Severity | Blocks cutover? | Notes |
|---|---|---|---|---|---|
| Static DesignIR loses hooks, listeners, timers, and canvas programs | pending | 2026-08-11 | P0 | yes | Native uses live release QuickJS / `@pulp/react`; strict mode rejects static or screenshot fallback. |
| Native `EditorBridge` cannot attach through the public `ScriptedUiSession` lifecycle | pending | 2026-08-11 | P0 | yes | Working engine overload exists, but public native runtime attachment is incomplete. |
| Pointer delivery/propagation/cancellation/capture are not browser-equivalent | pending | 2026-08-11 | P0 | yes | Current paths can duplicate delivery; capture is bookkeeping rather than input retargeting. |
| Context menu, double-click, focused key-up/Tab routing are incomplete | pending | 2026-08-11 | P0 | yes | Required for menus, shortcuts, text entry, and host keyboard ownership. |
| Native accessibility properties and actions are incomplete | pending | 2026-08-11 | P0 | yes | Canvas bands require semantic peers; controls need press/toggle/range actions. |
| Browser file/clipboard/modal/storage assumptions need native capabilities | pending | 2026-08-11 | P1 | yes | Pattern import/export cannot rely on FileReader, downloads, or global temporary storage. |
| Frozen adapter behavior is not canonical source | product | 2026-08-11 | P0 | yes | Fold exact shipping behavior into stable native source modules before parity. |
| Browser edit formulas diverge from dormant C++ `EditEngine` | product | 2026-08-11 | P0 | yes | Preserve frozen algorithms or unify implementations behind a mutation-sensitive oracle. |

## Closed Gaps

| Gap | Pulp issue | Filed | Closed | Consumer PR | Notes |
|-----|-----------|-------|--------|-------------|-------|
| No symmetric `EditorBridge::detach_webview()` — teardown-race window between `set_message_handler` clear and last in-flight WebView callback | [pulp#726](https://github.com/danielraffel/pulp/issues/726) | 2026-04-24 | 2026-04-24 (pulp#728 → v0.42.0) | [Spectr PR #21](https://github.com/danielraffel/spectr/pull/21) | Consumer calls `bridge_.detach_webview(*panel_)` in `EditorView::detach_if_needed()` before native child-view detach. Closed within hours of filing — fastest framework-fix cycle recorded to date. |

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
