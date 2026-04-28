# Retrospective: leveraged-prototype loop on spectr#28 / pulp#924

*Written 2026-04-28. Reads like a blog post on purpose so future agents and consumers can pick up the playbook quickly.*

## What happened

Over a single afternoon, Spectr's WebView-to-native editor port went from
"chrome renders as outline boxes, FilterBank is empty white" to "chrome
typography in JetBrains Mono, FilterBank renders the spectrum gradient,
text no longer truncates, panels have proper elevation." Five Pulp framework
PRs landed and an SDK shipped (v0.54.0 → v0.56.0) in roughly two hours.

The interesting part isn't the speed — it's that **almost none of the
acceleration came from writing code faster**. It came from a process where
the consumer (Spectr) and the framework (Pulp) iterated in parallel,
connected by a tight evidence-driven feedback loop.

This document captures the loop so we can re-enter it deliberately next
time.

## Why tokens and W3C-runtime support matter (and where they end)

The original Spectr design ships as a 1.86 MB self-bundling HTML file from
Claude Design. Unpacking it via `tools/extract-html-bundle/extract.mjs`
yields three artifacts:

- `tokens.json` — 25 CSS custom properties × 4 themes (default, paper,
  dusk, terminal). E.g. `--accent: oklch(0.78 0.14 220)`,
  `--mono: 'JetBrains Mono', ui-monospace, monospace`.
- `classnames.json` — 2 class rules (`.mono`, `.tnum`) flattened to JSX
  style objects.
- `main.js` — the original 161 KB React bundle, scaffolding intact.

**Tokens are the visual leverage point.** Once `var(--mono)`, `var(--accent)`,
`var(--panel)` etc. resolve to their concrete hex/rgba/oklch values, every
color and font-stack reference in the bundle suddenly *means something*.
Without token resolution, every `style={{ background: 'var(--panel)' }}`
silently drops to nothing. With it, the chrome looks like it was designed.

**W3C-runtime support is necessary but not sufficient.** Pulp's bridge
exposes W3C-shaped APIs (`canvas2d`, `requestAnimationFrame`,
`MessageChannel`, `document`/`Element` polyfills), so a JS bundle that
calls `ctx.fillRect(...)` works without modification. But "W3C-shaped
runtime" is *not* a CSS engine. There's no parser, no cascade, no
specificity, no inheritance, no `@media`, no `:hover`. CSS support means
"per-View `setX` bridge calls" — each property has to be translated.

That's what `@pulp/css-adapt` does: 200-ish CSS properties → bridge calls,
with shorthand expanders, value parsers, and effect lowering. It's the
seam between the design's intent and the framework's primitives.

## How this generalizes beyond React

The loop isn't React-specific. Anything that produces "JS that calls
DOM-shaped APIs and uses inline-style objects with CSS values" can ride
the same path:

- **Pencil.dev exports** — a `.pen` file is design data; export to HTML
  produces inline-styled DOM. Same pipeline: extract `<style>` blocks
  → tokens, extract `<script>` → app, run through @pulp/css-adapt at
  render time.
- **Figma → HTML exports** (via plugins like Anima, FigmaToCode, or our
  own `figma-to-pulp` skill) — same shape. The Figma component →
  inline-styled HTML conversion gives us `style={{...}}` objects per
  node, and Figma's design tokens map directly to our `tokens.json`.
- **Stitch, v0, plain handwritten HTML** — same pipeline, smaller
  bundles. The extractor doesn't care about the source.

The non-trivial cases are: `:hover` / `@media` / `@keyframes` /
class-based pseudo selectors. Those need either runtime evaluation
(more framework work) or AOT flattening to multiple style states
(adapter work). Most current Pulp bridges don't handle them; the
analyzer flags them as drops with telemetry.

## What the Pulp CLI did

The CLI's role today was indirect but critical. We didn't add new
commands; we used the *existing* shape:

- `pulp doctor` to confirm the local SDK install and find the
  `~/.pulp/sdk/0.52.0/` tree we were swapping into.
- `cmake --build build --target Spectr_Standalone` for the Spectr
  edge of the loop. Pulp's CMake `find_package(Pulp X.Y.Z)` made the
  SDK pin one-line trivial to bump.
- `gh pr` / `gh issue` / `gh release` (via `gh` CLI, not Pulp's, but
  in the same spirit) for the framework edge — file issues, watch PRs,
  fetch SDK release tarballs.

What's *missing* and worth adding (filed as pulp#940):

- `pulp loop --platform=macos|linux|windows` — explicit "focus mode"
  entry that pins to one platform's tooling and skips cross-platform
  configure.
- An `ar-swap` helper that validates header-vs-library ABI before
  patching .o files into a pinned SDK. We hit an ABI mismatch with
  `Label::font_family_` because we patched the .a but forgot to sync
  the SDK header — Spectr crashed at runtime because `sizeof(Label)`
  changed. Tooling would have caught it.

## How we filed tasks to the framework

Every framework gap became a Pulp issue with three properties:

1. **Concrete bridge-fn signature suggestion** — e.g.
   ```
   register_function("setBoxShadow", [](ArgumentList args) {
     // (id, dx, dy, blur, spread, color, inset?)
   });
   ```
   Upstream agents don't have to invent the API; they implement it.

2. **Occurrence evidence** — the analyzer's report gave us numbers like
   "27× setFontFamily, 7× setFontWeight, 40× letter-spacing." That made
   prioritization mechanical: 50% of unmapped occurrences were
   `boxShadow + backdropFilter`, so those went first.

3. **Acceptance criteria + reproducer** — the umbrella issue (#924)
   linked to Spectr's branch and the coverage report so anyone could
   run `pulp-css-analyze` against the bundle and see the same numbers.

The umbrella + 6 sub-issues went up at one moment in time. Within ~25
minutes, an upstream agent had read the analysis and started filing
PRs. PR #938 (Label fonts) even cited my analyzer numbers verbatim in
its body — the agent was reading the issue's data, not just its title.

## How we tested and confirmed

Two layers, in order:

**Framework PR side** — every PR shipped Catch2 unit tests in the same
PR. PR #938 added 5 cases under `[issue-927]` covering family/weight/
letter-spacing forwarding, default state, and getter round-trip.
PR #934 added 195 lines of test for clear_rect with stacked-views and
Skia raster verification. None of these merged with "CI green is
enough" — every PR shipped behavior tests too.

**Consumer side** — visual validation via screencap of the standalone
Spectr binary at each SDK pin bump, plus re-running `pulp-css-analyze`
on the bundle to confirm the coverage number moved (72% → expected
~95% post-bump). That separation is what makes the focus-mode loop
safe: framework PRs prove their behavior in unit tests; consumer
proves the behavior reaches the rendered pixels.

## Where we codified the work

Three pieces became reusable infrastructure, not Spectr-specific:

- **`@pulp/css-adapt`** — `/tmp/pulp-react/packages/pulp-css-adapt/`,
  shipped as `pulp-css-adapt-0.0.1.tgz`. 100 vitest tests passing.
  Generic CSS-property → Pulp-bridge translator. Any future React
  app gets the same shorthand expansion, color parsing, and effect
  lowering for free.

- **`pulp-css-analyze`** — `spectr/native-react/tools/pulp-css-analyze/`.
  CLI that AST-walks any JS bundle, extracts inline-style objects,
  cross-references with Pulp's bridge surface, emits Markdown
  coverage report. `--ai` flag for LLM-suggested mappings on
  unmapped props.

- **`extract-html-bundle`** — `spectr/native-react/tools/extract-html-bundle/`.
  Pure Node, no deps. Decodes the bundler/template inside a Claude
  Design HTML, lifts `<style>` → tokens.json, lifts class rules →
  classnames.json, lifts `<script>` → main.js.

The pulp#940 meta-issue proposes lifting these into Pulp itself
(skill, CLI, slash command) so any future consumer following this
pattern doesn't have to rebuild the tooling.

## Learnings + speedups

**Quantified gap-finding beats anecdote-finding.** "Buttons look bad"
gets nowhere. "boxShadow appears 8× across 5 components, no bridge
equivalent" gets a PR within an hour.

**Single-platform iteration + cross-platform PR validation = best of
both.** The framework PRs all ran on macOS/Linux/Windows via Pulp's
shipyard CI before merging. Spectr iterated on macOS only. No
platform was harmed.

**ar-swap is a useful unsafe trick.** Patch .o files directly into a
pinned SDK's `.a` to validate framework changes without a full SDK
release cycle. But ABI compat (vtables, struct layouts) requires
syncing the header alongside the .o. Skip the header → runtime crash.

**Auto-release closes the loop.** Pulp's PR-merge → tagged-SDK-release
flow runs in <1 hour. That's what makes the consumer-side pin bump
cheap. Without it, "wait for an SDK release" would dominate the
timeline.

**Filing preemptive sub-issues during prototype work pays off.** While
prototyping #927 locally I hit a font-resolution crash that no one
had filed yet — surfaced it as #932 (SkFontMgr registration). When
upstream picked up #927, their solution went straight to the right
shape (typeface cache keyed on family+weight+slant) because the gap
was already documented.

## What "focus mode" looks like as a future Pulp feature

The dev experience we want to ship (per pulp#940):

```
$ pulp loop --platform=macos     # enter focus mode
$ # ... iterate, ar-swap, screencap, file framework issues ...
$ pulp loop --watch-issues 924   # monitor PR state flips on a tracker
$ # ... upstream lands fixes, SDK release, bump pin ...
$ pulp loop --off                # exit, restore cross-platform configure
$ shipyard pr                    # land the consumer change with full CI
```

The mode is *iteration*; landing always requires cross-platform CI.

## Closing

This pattern delivered ~5 framework primitives + a visible parity
breakthrough in 2 hours. The acceleration came from the loop, not
from typing speed. Codifying it is high-leverage work.

If you're a future Pulp consumer reading this and want to ride the
same pattern: read pulp#924 + pulp#940, run `pulp-css-analyze` on
your bundle, file framework gaps with concrete numbers, monitor
upstream PRs, bump the pin after a batch lands. The infrastructure
to do this cleanly is mostly already built.
