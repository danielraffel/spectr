# Spectr Native-React Bridge — Product Spec

_Created 2026-04-29. Single-page state-of-the-world for the WebView-to-native bridge port. Read this first if resuming work in a new session._

> **Tooling north star:** Use **RepoPrompt** for all code analysis on this project (cross-repo investigation, framework diagnosis, reference-pattern lookups). See **Appendix A** for the recommended workflow. Use `/codex` for second-opinion review of plans, designs, and tricky diagnoses. Default to these over ad-hoc Read/Grep.

## Context in one paragraph

Spectr's editor today is a Claude-Design-exported React HTML bundle (1.86 MB, self-bundling) rendered inside Pulp's WebView (Chromium-class embedded browser). **WebView works great.** The replacement target is the same React bundle running natively through Pulp — JavaScript via QuickJS (later V8/JSC), React 18 reconciler via `@pulp/react`, layout via Yoga, GPU rendering via Skia + Dawn/WebGPU. **No browser engine, no DOM, no CSS engine.** The native path closes ~72% of styling today; the visible center FilterBank is empty (canvas2D draws fire but don't reach the visible surface — see #964 retitle below). The general-purpose import pipeline (`pulp import-design`) is the long-term home for this work; Spectr is the consumer-zero validating the pipeline.

## North star

1. **Parity** — Spectr's native rendering is visually indistinguishable from the WebView for every editor view (FilterBank, settings, manage-plugin, presets, snapshot A/B, morph). Same typography, same gradients, same compositing, same interactivity.
2. **Generalized import** — `pulp import-design --from claude` ingests *any* Claude-Design HTML export and produces a compiled native UI without per-app handwork. Spectr is the proof; the next plugin gets the path for free.

Non-goals (for v1): exact pixel-level binary equality, WebView's specific font metrics, cross-engine perf parity beyond QuickJS.

## Workspaces, branches, and key paths (the recovery map)

**Active branch:** `feature/native-react-editor` on `https://github.com/danielraffel/spectr` — currently HEAD `8f1df47` (range→Fader workaround).

**Worktrees / repos:**

| Path | What | Notes |
|---|---|---|
| `/Users/danielraffel/Code/spectr` | Spectr repo, branch `feature/native-react-editor` | Primary worktree for this work |
| `/Users/danielraffel/Code/pulp` | Pulp repo, branch `fix/cg-canvas-concat-transform-933-takeover` | Framework-side; mostly used to read code, not edit |
| `/Users/danielraffel/.pulp/sdk/0.60.0/` | Currently pinned SDK | Built `Pulp_DIR=$HOME/.pulp/sdk/0.60.0/lib/cmake/Pulp` |
| `/Users/danielraffel/Code/spectr-design/Spectr-2/` | Source HTML exports from Claude Design | See "Source HTML files" below |

**Spectr build state:**

- `/Users/danielraffel/Code/spectr/build/` — `SPECTR_NATIVE_EDITOR=ON`, the native-bridge build
- `/Users/danielraffel/Code/spectr/build-webview/` — `SPECTR_NATIVE_EDITOR=OFF`, the WebView reference build
- Standalone binary: `build/Spectr.app/Contents/MacOS/Spectr`
- Plugin formats: `build/AU/Spectr.component`, `build/CLAP/Spectr.clap`, `build/VST3/Spectr.vst3`

**Source HTML files** (`/Users/danielraffel/Code/spectr-design/Spectr-2/`):

- `Spectr (standalone).html` — **1.86 MB** — the full original Claude Design export with the entire React bundle inlined. **This is the canonical input.**
- `Spectr (standalone source).html` — **169 KB** — a stripped variant. Use only for diffing structural changes against the canonical export.
- `Spectr Sampler.html` — 177 KB — sampler variant (separate workstream, not in this spec's scope)
- `Spectr.html` — 169 KB — duplicate of source variant

## Architecture (8 layers, 3 build-time tools)

**The bridge stack** (memory: `project_pulp_react_architecture.md`):

```
┌─────────────────────────────────────────────────────────────┐
│ 1. JavaScript engine (QuickJS today; V8 / JSC follow)        │
│    └─ from @pulp/runtime, embedded in every Pulp plugin      │
├─────────────────────────────────────────────────────────────┤
│ 2. React 18 reconciler (@pulp/react custom renderer)         │
│    └─ Hosts function components, hooks, context, refs        │
├─────────────────────────────────────────────────────────────┤
│ 3. Yoga layout                                               │
│    └─ Flexbox/box-model; no browser cascade                  │
├─────────────────────────────────────────────────────────────┤
│ 4. CSS adapter (@pulp/css-adapt, 100 vitest tests)           │
│    └─ ~200 CSS props → bridge setX calls                     │
│    └─ Shorthand expansion, color/length parsers              │
│    └─ registerProperty/registerShorthand extension API       │
├─────────────────────────────────────────────────────────────┤
│ 5. dom-adapter (Spectr-side, native-react/dom-adapter.tsx)   │
│    └─ DOM-tag → bridge-widget mapping                         │
│    └─ var() resolution, className → style object             │
│    └─ position:absolute + inset:0 → "fill parent" Yoga       │
├─────────────────────────────────────────────────────────────┤
│ 6. WidgetBridge (Pulp framework, JS-side handle)             │
│    └─ View, Row, Label, Spectrum, Knob, Fader, ...           │
│    └─ canvas2d ctx surface (setLineWidth, beginPath, ...)    │
├─────────────────────────────────────────────────────────────┤
│ 7. C++ widget impl (core/view/, core/canvas/)                │
│    └─ Yoga layout pass, Skia paint pass                      │
├─────────────────────────────────────────────────────────────┤
│ 8. Skia + Dawn/WebGPU                                        │
│    └─ GPU rasterization                                       │
└─────────────────────────────────────────────────────────────┘
```

**Three build-time tools** (`native-react/tools/`):

| Tool | Purpose | Kind |
|---|---|---|
| `extract-html-bundle/` | Decodes `<script type="__bundler/template">` JSON-encoded HTML → `tokens.json` + `classnames.json` + `main.js` | Static, deterministic, no AI |
| `pulp-css-analyze/` | AST-walks JS bundle, extracts every inline `style={{...}}`, cross-refs against bridge surface, emits Markdown coverage report | Static. Optional `--ai` flag for unmapped-prop suggestions |
| `pulp-bridge-coverage/` | AST-walks JS bundle, reports W3C-spec coverage (Canvas 2D / DOM / SVG / form controls) against the WidgetBridge surface | Sibling of pulp-css-analyze; uses `known-canvas2d.ts` (W3C spec list) |

**Why these matter for recovery:** if/when coverage drops between SDK bumps, that's a regression signal *before* you screenshot. Run them on every editor.js build.

## Tokens & CSS mapping (the W3C piece)

The 1.86 MB HTML carries:

- **25 CSS custom properties × 4 themes** in `tokens.json` (e.g. `--accent: oklch(0.78 0.14 220)`, `--mono: 'JetBrains Mono', ui-monospace, monospace`)
- **2 class rules** in `classnames.json` (`.mono`, `.tnum`) flattened to JSX style objects
- **161 KB original React bundle** in `main.js`

**Tokens are the visual leverage point.** Without `var(--accent)` resolution, every styled chrome element silently drops to default. With it, the design lights up.

**W3C-shaped runtime ≠ CSS engine.** Pulp exposes W3C-shaped JS APIs (`ctx.fillRect`, `requestAnimationFrame`, `MessageChannel`, `document` / `Element` polyfills) so unmodified React + canvas2D code runs. But there's no parser, cascade, specificity, inheritance, `@media`, or `:hover`. CSS support means "translate each prop to a per-View `setX` bridge call" — that's `@pulp/css-adapt`'s job (200-ish props, shorthand expanders, value parsers, effect lowering).

**The seam:** `@pulp/css-adapt` between the design's CSS intent and Pulp's primitives. Coverage today is **72% mapped** of 60 unique props (160 inline style objects). Remaining 13% (8 unmapped props) are filed as framework gaps under umbrella **#924**.

W3C surface integrated:
- Canvas 2D — full spec list at `native-react/tools/pulp-bridge-coverage/src/known-canvas2d.ts`
- DOM Element / Document polyfills — partial
- SVG — `<svg>` + path-d via #965 (in flight)
- Form controls — `<input type=range>` via #966 (workaround in place)

## Build, test, and screenshot loop

**Quick loop** (used dozens of times per session):

```bash
# 1. Edit native-react sources
cd /Users/danielraffel/Code/spectr/native-react
vim editor-port.tsx dom-adapter.tsx ...

# 2. Build the React bundle (uses real port, not the editor.tsx stub)
npm run build:port           # → dist/editor.js (445K-ish)

# 3. Re-bake editor.js into the standalone binary
cd /Users/danielraffel/Code/spectr
cmake --build build --target Spectr_Standalone
# This re-runs `pulp_add_binary_data` to embed the new editor.js.
# Important: bare `cmake --build build` won't pick up asset edits — must
# rebuild the standalone target.

# 4. Launch + screencap
pkill -f Spectr; sleep 2
build/Spectr.app/Contents/MacOS/Spectr > /tmp/spectr-out.log 2>&1 &
sleep 5
osascript -e 'tell application "System Events" to tell process "Spectr" \
  to set position of front window to {200, 50}'
osascript -e 'tell application "System Events" to tell process "Spectr" \
  to set size of front window to {1100, 700}'
sleep 2
screencapture -o -R200,50,1100,700 /tmp/spectr-latest.png
pkill -f Spectr

# 5. Inspect — visual + stdout
open /tmp/spectr-latest.png
tail -100 /tmp/spectr-out.log     # canvas2D log probes, JSX tree, etc.
```

**Cheap pulp-screenshot loop** (when you only need to validate primitives, not full app):

```bash
cd /Users/danielraffel/Code/spectr/native-react && npm run smoke
# Renders editor.tsx (the stub) via /Users/danielraffel/Code/pulp/.claude/worktrees/agent-a7f7a033/build/tools/screenshot/pulp-screenshot
# Useful for isolating bridge bugs from React-port bugs.
```

**Reference for visual diff:** `planning/screenshots/_REFERENCE_webview.png` — the WebView baseline. Every native screenshot should be diffed against this.

**Latest committed state:** `planning/screenshots/native-editor-v0.60.0-fader.png` — current native render at v0.60.0 with all today's workarounds in place.

## Current state — what works, what doesn't (Apr 29 2026, v0.60.0)

**Works ✅:**
- Top toolbar — SPECTR · ZOOMABLE FILTER BANK · LIVE · PRECISION · IIR · FFT · HYBRID · "32 bands ▾" · "1.00x zoom"
- Bottom toolbar — CLEAR · SCULPT · PEAK · PRESETS · SNAPSHOT · A/B · morph slider
- Window chrome with traffic lights, "Spectr — Standalone" title
- Token resolution (`var(--accent)`, `var(--mono)`, ...)
- Class merging (`.mono`, `.tnum`)
- Shorthand expansion (padding/margin/flex/border)
- Color parsing (rgb/rgba/hsl/hex/oklch via colord)
- `position: absolute; inset: 0` → Yoga "fill parent"
- `<input type=range>` → Fader (workaround `8f1df47`)
- Default `<canvas>` View bg transparent (workaround `fa74d5f`)
- Canvas2D draw calls fire and reach the bridge (logged: `canvasTranslate`, `canvasLineTo`, `canvasBeginPath`, `canvasSetRadialGradient` — every frame)

**Doesn't work ❌:**
- **Empty FilterBank center** — canvas2D commands fire on `pr_1`/`pr_2` (the two `<canvas position:absolute inset:0>` at `extracted.js:2181-2182`) but output never reaches the visible Skia surface. **Three live hypotheses**: (1) wrong target surface — CanvasWidget's Skia surface not composited into parent View's layer; (2) drawn-but-obscured by sibling layer; (3) coordinate-frame mismatch.
- **Inline `<svg><path>` icons** in PRESETS / SCULPT / PEAK / etc — blocked on framework #965 (no SVG-path widget). 8+ usage sites in extracted bundle.
- **Dropdown/popover panels** — `bandsMenu` at line 2847 (`position:absolute; top:28; right:0; zIndex:20; backdropFilter:blur(10px)`) likely hidden behind canvas. Probably the same compositing class as the FilterBank issue.
- **Settings + manage-plugin views** — never validated; reachable only via PRESETS dropdown's "MANAGE…" menu, blocked by dropdowns.

**Spectr-side workarounds in place (revert when upstream fix lands):**

| Commit | What | Will revert when |
|---|---|---|
| `8f1df47` | `<input type=range>` → Fader widget mapping | #966 lands (range-slider widget) |
| `fa74d5f` | Default `<canvas>` View bg transparent | #967 lands (View transparent default) |
| `def0c9c` | Canvas size + gradient fillRect semantics | If #968 (canvasRect color fallback) lands clean |

## Framework gaps (Pulp umbrella #924)

| # | Pri | Title | State | Spectr workaround |
|---|---|---|---|---|
| **964** | P0 | FilterBank canvas content not reaching visible surface (re-titled, premise updated 2026-04-29 — was "child Views opaque-white over canvas") | OPEN | none — investigating |
| **965** | P1 | Standalone SVG-path widget for inline `<svg><path>` icons | OPEN | none — 8+ icons missing |
| **966** | P1 | Range-slider widget for HTML `<input type="range">` | OPEN | `8f1df47` |
| **967** | P0 | View widget default background should be transparent | OPEN — premise stale per other agent's regression-test finding | `fa74d5f` |
| **968** | P2 | `canvasRect` falls back to active set_fill_color when no color arg | OPEN | partial via `def0c9c` |
| **969** | P2 | CSS-style typography inheritance (parent View → child Label) | OPEN | none |

**Closed earlier in this push** (do not re-open): #925 boxShadow · #926 backdropFilter · #927 Label fonts · #928 Label auto-grow · #929 Canvas visibility · #930 setTransform · #932 SkFontMgr font registration.

**Cron polling** active in this session: `7,37 * * * *` (job ID `ce66f381`) — checks #964-#969 every 30 min, integrates each merged PR (SDK pin bump, workaround revert, rebuild, re-screenshot). Stops after 6 hours OR when all 6 close.

## CLI integration goal

The long-term home for this pipeline is the Pulp CLI. Two commands relevant:

- `pulp import-design --from claude` — Ingest a Claude-Design HTML export. Today routes to `tools/import-design/pulp-import-design`. Should accept `Spectr (standalone).html` and produce a compiled native bundle equivalent to what Spectr's `native-react/` does by hand. Linked: pulp #468, #729.
- `pulp export-tokens` — Export theme tokens as W3C Design Tokens. Already works.

**Goal:** every step the Spectr-side `native-react/` directory does by hand should be reachable through `pulp import-design`. When that's true, `native-react/` becomes deprecated machinery and a fresh consumer (next plugin) just runs:

```bash
pulp import-design --from claude --input "MyPlugin (standalone).html" --execute-bundle
```

…and gets a buildable native UI. **Spectr is the proof; we should design every Spectr fix asking "would this generalize?".**

## Task list (concrete next actions)

Ranked by impact:

1. **Drop a sentinel `clearRect` red-fill probe** at the start of FilterBank's canvas2d render fn → re-screencap → if red is visible, #964 is draw-call ordering (canvas2D issue inside Spectr's React tree); if red is also invisible, #964 is surface-composition (Pulp framework, narrow audit to `canvas_widget.cpp` + `view.cpp::paint_all` + `native_gpu_texture_provider`). **This is the next experiment.**
2. **Coordinate with the other agent** on #964 retitle + #967 close per "premise stale" finding. Tests-only contract PR on `framework/spectr-parity-967`. Other agent then pivots to additive #965.
3. **File new umbrella sub-issue** for popover/dropdown overlay compositing if (1) confirms it's a separate class from #964.
4. **Add CI smoke test** on the Spectr side: every commit runs `npm run build:port` + standalone-rebuild + screencap, posts diff to PR. Catches visual regressions before merge.
5. **Validate settings + manage-plugin views** once dropdowns work.
6. **W3C bridge-coverage report** — run `pulp-bridge-coverage` every editor.js build, fail CI if drops.
7. **CLI parity check** — define what `pulp import-design --from claude --input <html>` should produce. List every step `native-react/` does today; check each against the CLI's output.

The cron loop (`ce66f381`) handles each framework-PR landing automatically.

## Decision log

- **2026-04-25** — chose @pulp/react over a hand-rolled custom React renderer (less novel surface area; reuse upstream React 18 internals).
- **2026-04-25** — chose Yoga over CSS-engine (no browser cascade; predictable; bgfx/RNS pattern).
- **2026-04-26** — chose to ship Spectr-side workarounds in parallel to upstream framework fixes (don't block on multi-day SDK release cycles).
- **2026-04-28** — split build into stub (`editor.tsx` for primitive validation) and port (`editor-port.tsx` for full app validation). Both compile to `dist/editor.js`. Standardize on `:port` for "is the app working?" checks.
- **2026-04-29** — chose to do the standalone screencap loop instead of `pulp-screenshot --script`; standalone exercises the full bridge plumbing including C++ side of CanvasWidget.
- **2026-04-29** — empirical finding: canvas2D draws fire but don't surface. Updated #964 framing. Pivoted other agent to additive #965.

## Appendix A — Reference repos to study (RepoPrompt-eligible)

> **🔍 Use RepoPrompt for all code analysis on this project.** Reading whole files into the conversation, grepping by hand, or re-deriving structure from scratch is wasteful when working on the bridge stack. RepoPrompt's `context_builder`, `file_search`, `get_code_structure`, and `read_file` are the right tools for cross-repo investigation, framework diagnosis, and reference-pattern lookups. **This is not optional ergonomics — it is the recommended workflow for any non-trivial code question on this work.** Default to RepoPrompt over ad-hoc Read/Grep.

User hunch (preserved verbatim): "I suspect mystralnative, react-native-skia-yoga, react-native-skia are most similar but that's just a hunch."

Ranked by relevance to *our* problem (React + Yoga + Skia + custom renderer + no browser):

| Rank | Path | Why it's relevant |
|---|---|---|
| 1 | `/Users/danielraffel/Code/react-native-skia-yoga` | **Closest match.** React + Yoga + Skia, custom renderer, no DOM. Exact pattern of our stack. Read for: render-tree → Skia paint, Yoga measure-fn integration, ref-callback wiring. |
| 2 | `/Users/danielraffel/Code/react-native-skia` | Production-quality React + Skia. Read for: canvas2D-equivalent API surface, paint primitive ergonomics, image/font caching. |
| 3 | `/Users/danielraffel/Code/mystralnative` | Same architectural family (renderer + Skia/native). Read for: bridge layer between JS and native, message-protocol design. |
| 4 | `/Users/danielraffel/Code/ink` | React custom renderer, terminal target instead of GPU. Read for: minimal reconciler scaffolding, tree-diffing patterns. |
| 5 | `/Users/danielraffel/Code/react-three-fiber` | React custom renderer over Three.js. Read for: declarative scene graph, prop-to-mutation mapping (analogous to our prop-to-bridge-call). |
| 6 | `/Users/danielraffel/Code/BabylonNative` | Native rendering with JS scripting; less direct overlap. Read for: how a native engine exposes a JS API surface that mirrors a W3C surface. |

### How to use RepoPrompt on this work

1. **Start with `context_builder`** for any "how does X work" question. Set `response_type="question"` for Q&A, `"plan"` for implementation plans, `"review"` for code review. Scope to one subsystem (e.g. "renderer" / "yoga integration" / "canvas paint"). Don't try to read a whole repo.
2. **Use `file_search`** instead of Grep — combines content + path + regex search across all workspace roots in one call.
3. **Use `get_code_structure`** to get function/type signatures of unfamiliar files before reading bodies.
4. **Use `manage_selection`** to curate a file context, then `oracle_send` for cross-cutting questions over that context. Continue with `chat_id` for follow-ups.
5. **Cross-repo lookups:** add the relevant ranked repo above as a workspace root, then `file_search` or `context_builder` against it to find the analogous pattern. E.g. "How does react-native-skia-yoga wire Yoga measure callbacks to Skia paint?" → `context_builder` against `/Users/danielraffel/Code/react-native-skia-yoga`.
6. **CRITICAL caveat:** RepoPrompt reads the local worktree, not `origin/main`. Before any audit, `git fetch origin main` and either rebase or create a fresh worktree (see CLAUDE.md "RepoPrompt explores the local worktree, not origin/main").

When `context_builder` returns a `chat_id`, save it — follow-up questions on the same context are far cheaper than rebuilding.

## Appendix B — Memories that apply to this work

User-memory hits that matter for this spec (re-read before resuming):

- `project_pulp_react_architecture.md` — the 8-layer architecture, RNS as closest match
- `project_react_engine_targets.md` — QuickJS first, V8 next (then JSC)
- `project_claude_design_manual_export.md` — pulp #468 doesn't wait on Anthropic; user manually exports HTML/zip
- `feedback_design_import_loop.md` — Screenshot-compare original vs Pulp render in automated loops
- `feedback_screenshot_workflow.md` — Save to planning/screenshots/ + commit + share GitHub blob URL
- `feedback_pulp_screenshot_validation.md` — `pulp-screenshot` for fast iteration; bypasses standalone window issues
- `feedback_yoga_layout.md` — Every container needs explicit height/flex_grow
- `feedback_codex_alignment.md` — Frequent /codex consults for direction validation; reference Ink + R3F + RNS
- `feedback_pulp_add_binary_data_configure.md` — Embedded assets baked at cmake configure time

## Appendix C — Maintenance

This doc is the recovery point. Update it when:

- A framework PR lands → row in "Framework gaps" table moves to "Closed", workaround commit goes in "will revert when" history.
- A new gap is filed → new row in the table.
- The 8-layer stack changes → update the diagram.
- A decision is made → append to "Decision log" with date.
- A reference repo proves more or less relevant → update Appendix A's ranking.

**One-line refresh** at the top with date + last-event whenever the doc is touched.
