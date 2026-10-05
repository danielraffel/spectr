# Spectr WP-1 dependency-first module plan experiment

`tools/wp1_module_plan.mjs` consumes the parser-backed
`spectr-owned-component-dependency-v1` manifest and emits a deterministic
`spectr-owned-tsx-module-plan-v1` plan. It is deliberately plan-only: it does
not rewrite `native-ui/materialized/materialized-document.runtime.json`, emit a
new runtime bundle, or remove patch scripts.

The manifest is treated as an integrity boundary. The tool requires exact
component IDs and hashes, rejects unresolved identifiers, unknown roots and
unknown dependency IDs, detects dependency cycles, rejects components that are
not reachable from the declared roots, and can verify every extracted
`components/<Name>.tsx` byte count and SHA-256. Dependency order is stable and
uses source script/index offsets as the tie-break, with the component ID as the
final tie-break. Owner/capture metadata is preserved for a later builder to
handle nested lexical components without flattening them incorrectly.

Run it with the pinned parser toolchain installed:

```sh
npm ci --ignore-scripts --prefix tools/wp1-parser
python3 -m unittest -v tools/test_wp1_module_plan.py
```

Evidence on the WP-1 parser branch:

- Nested fixture order is `MBtn -> Inner -> Panel`, with `Inner`'s `label`
  capture and owner retained.
- The real `MBtn` manifest validates against the extracted 682-byte module.
- Repeat plans are byte-identical.
- Seven focused tests pass, including tampered module, unknown dependency,
  dependency cycle, and unreachable component negative controls.
- Existing extractor, parser, and bounded TSX leaf suites remain green (22/22
  in the combined run).

This is not yet a dependency-resolved TSX rebuild. The next builder must use
this order to assemble converted modules, preserve owner scopes, type-check the
result, regenerate the runtime artifact, and compare native/browser receipts.
