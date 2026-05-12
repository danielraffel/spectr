# pulp-css-analyze

AOT analyzer that reads a JS bundle and reports CSS coverage against the
[`@pulp/react`](../../dom-adapter.tsx) bridge surface. Helps spot
framework gaps and silently-dropped CSS without reactive
whack-a-mole.

## Why

The `@pulp/react` bridge exposes a small, fixed set of `setX` and
`createX` functions. Anything else a designer dropped into a `style={…}`
literal is silently discarded by the dom-adapter. This tool walks a
shipped JS bundle, classifies every CSS property as **mapped**,
**intentionally dropped**, or **unmapped**, and prints a Markdown
report. Run it on every bundle build to keep a running tally of gaps.

## Requirements

- Node 20+

## Install

```bash
cd tools/pulp-css-analyze
npm install
npm run build
```

The build step writes a single `dist/cli.js` (with `#!/usr/bin/env node`
shebang) that you can invoke directly.

## Usage

```bash
# Stdout report
node dist/cli.js path/to/bundle.js

# Write the report to a file
node dist/cli.js path/to/bundle.js -o coverage.md

# Use a custom bridge surface (JSON array of strings)
node dist/cli.js bundle.js --bridge-list ./bridge.json -o coverage.md

# Enrich unmapped suggestions via the Anthropic API
ANTHROPIC_API_KEY=sk-ant-... node dist/cli.js bundle.js --ai -o coverage.md
```

When `--ai` is passed, the tool calls the Anthropic Messages API to
classify each unmapped property as `mappable`, `lower-to`, or
`framework-gap`. The model output is written next to the report as a
sidecar `*.suggestions.json`. If `ANTHROPIC_API_KEY` is missing the
`--ai` step is skipped gracefully — the rest of the report still runs.

## Output format

Markdown report with:

- `## Summary` — counts and percentages.
- `## Top unmapped properties (by occurrence)` — biggest offenders first.
- `## Mapped properties` — visibility into what the bridge is consuming.
- `## Intentionally dropped properties` — confirms drops that are on
  purpose (cursor, transition, …).
- `## Suggested fixes` — deterministic per-prop heuristics.
- `## AI-augmented suggestions` (only with `--ai`).

## Bridge surface

The bundled snapshot of `setX` / `createX` functions lives in
`src/known-bridge.ts`. Refresh it whenever
[`native-react/dom-adapter.tsx`](../../dom-adapter.tsx) grows or shrinks.

The CSS-prop ↔ bridge cross-reference lives in `src/known-css.ts` —
that's the file to edit when you add a new mapped property or
deliberately drop one.

## Development

```bash
npm test          # vitest run
npm run start -- path/to/bundle.js   # tsx-run, no build step
npm run build     # tsup → dist/cli.js
```

Tests use synthetic JSX bundles (jsx-runtime + classic
`createElement`) to verify the AST walker.
