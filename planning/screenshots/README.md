# Spectr editor — native render proof points

Captured on 2026-04-25 during the pulp #468 native-runtime workstream.
End-to-end validation that Spectr's editor *can* run on Pulp's native
Dawn / Skia / Yoga stack with no browser in the render path.

## The three screenshots

### 1. `spectr-editor-chrome-target.png` — the goal

What Spectr's `resources/editor.html` looks like when rendered in real
Chromium. This is the visual fidelity target: a pixel-faithful Spectr
editor with the spectrum analyzer, top header bar, mode toggles, and
bottom action rail. **Captured via chrome-devtools MCP for reference
only — Pulp will not ship Chromium as a runtime dependency.**

### 2. `spectr-editor-native-from-walker.png` — current import-design output

The same editor.html, run through:

1. Materialize React DOM (today: real-Chrome capture; eventually:
   pulp's QuickJS harness once pulp #763 closes the React-mount gaps)
2. `pulp import-design --from claude --file <materialized.html>`
3. `pulp-screenshot --script ui.js` (renders via ScriptEngine →
   WidgetBridge → Yoga → Skia → Dawn → PNG)

24 elements captured but all stacked vertically as labels — the
walker emits `createCol` for every container regardless of CSS
flex-direction, and routes all `<button>` elements to `createLabel`.
**The render stack is doing exactly what the JS asks for.** The gap
is in pulp-import-design's translation. Tracked in pulp #764.

### 3. `spectr-editor-native-handcrafted.png` — what the renderer can do

Hand-crafted `ui.js` that uses the same WidgetBridge primitives the
walker has access to (`createRow`, `createPanel`, `setBackground`,
`setFlex(..., 'flex_grow', N)`) — what the walker SHOULD emit after
pulp #764 lands. Same `pulp-screenshot` command, same render stack.

Result: the actual Spectr layout — top header bar with logo + mode
toggles, central area placeholder for the analyzer, bottom action
rail with all the chrome buttons. **No browser. No QuickJS-React
fragility. Just Pulp's native primitives.**

## What this proves

The Pulp render stack (Dawn + Skia + Yoga + WidgetBridge +
ScriptEngine) is **already capable** of producing the Spectr editor at
near-WebView fidelity. The remaining work is:

- **pulp #763** — close the QuickJS gaps so the materialization step
  doesn't need a real browser as a fixture-capture aid.
- **pulp #764** — improve the import-design walker so it auto-emits
  the shape of #3 above instead of #2.
- **Spectr-side** — wire the spectrum-analyzer canvas placeholder to
  `pulp::view::VisualizationBridge` (M3 already publishes the data).

After those three land, the loop closes: Claude design HTML →
pulp-import-design → native render at near-pixel fidelity, end to end.

## Reference paths (local dev artifacts)

- `/tmp/spectr-rendered-dom.html` — 12.6 KB materialized DOM from real
  Chrome (canonical fixture for testing)
- `/tmp/spectr-claude-rendered/ui.js` — current walker output (78 lines,
  24 labels)
- `/tmp/spectr-handcrafted/ui.js` — what the walker should emit
- `/tmp/spectr-native-render.png` — current walker render (≈ #2 above)
- `/tmp/spectr-handcrafted-render.png` — handcrafted render (≈ #3 above)
