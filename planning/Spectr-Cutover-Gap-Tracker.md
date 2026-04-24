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
- Tests: `test/test_editor_bridge.cpp`

The bridge is **renderer-agnostic by construction**. WebView JS issues
messages today; the native-imported JS will issue the same messages
tomorrow. Nothing in `editor_bridge.cpp` needs to change during the
cutover.

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
