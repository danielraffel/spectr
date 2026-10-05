# Spectr WP-1 bounded TSX leaf experiment

This experiment converts the actual `MBtn` component from
`native-ui/materialized/materialized-document.runtime.json` into readable TSX.
It is intentionally narrow: it proves the first codemod seam without treating
one easy leaf as evidence that all 63 components can be converted safely.

Run it after installing the pinned parser dependency:

```sh
npm ci --ignore-scripts --prefix tools/wp1-parser
python3 -m unittest -v tools/test_tsx_leaf_experiment.py
```

The converter is `tools/tsx_leaf_experiment.mjs`. It parses the HTML artifact's
JavaScript script blocks with `@babel/parser` 7.28.4, finds exactly one named
component, converts one returned `React.createElement` tree to JSX, and parses
the result with both the JSX and TypeScript plugins before writing it. Source
expressions are copied into JSX braces; no formatter or second code generator
can change them. The output is deterministic and reports source/output SHA-256
values.

The converter rejects spread props, dynamic element tags, non-object props,
computed/method properties, and components with zero or multiple returned
`createElement` trees. Those are the cases that need a full AST codemod and
semantic fixture before they can enter the production path.

Evidence on the 2026-10-05 WP-1 parser branch:

- `MBtn` converts to 674-byte TSX and parses successfully.
- Two conversions produce identical bytes and output SHA-256.
- Four focused tests pass, including spread-prop and multiple-return negative
  controls.
- Existing owned-slice and parser dependency suites remain green (18/18).

This does not claim a full authored TSX tree, dependency-resolved rebuild,
artifact regeneration, browser/native identity parity, or patch-script
retirement. The next implementation must compose parser dependency manifests,
convert nested/conditional trees, type-check the owned modules, rebuild the
runtime artifact, and compare native and Chromium receipts before broadening
this experiment.
