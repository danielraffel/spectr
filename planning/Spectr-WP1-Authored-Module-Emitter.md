# Spectr WP-1 authored module emitter experiment

`tools/wp1_authored_module_emitter.mjs` is the first read-only production-build
seam after the parser-backed dependency manifest and bounded TSX converter. It
consumes an artifact manifest produced by
`tools/wp1_dependency_manifest.mjs`, validates the artifact and every
component declaration slice by exact UTF-8 byte hash, walks the closure in
dependency-first authored order, and invokes the pinned converter for each
component.

The emitter writes a staging directory and moves it into place only after
every output report, module byte count, output SHA-256, dependency edge, owner,
capture, and source identity passes validation. `--verify` rechecks the
emission report and generated files later. Existing output directories are
never overwritten. A changed artifact, stale manifest, missing dependency,
cycle, unresolved identifier, converter rejection, or tampered module fails
closed before a completed output is published.

Example for the former blocker roots:

```sh
node tools/wp1_dependency_manifest.mjs \
  --artifact native-ui/materialized/materialized-document.runtime.json \
  --root ContextMenu --root PatternManager \
  --root SpectrModulationSettings \
  --out /path/to/three-roots.manifest.json
node tools/wp1_authored_module_emitter.mjs \
  --artifact native-ui/materialized/materialized-document.runtime.json \
  --manifest /path/to/three-roots.manifest.json \
  --out /path/to/authored-modules
node tools/wp1_authored_module_emitter.mjs \
  --artifact native-ui/materialized/materialized-document.runtime.json \
  --manifest /path/to/three-roots.manifest.json \
  --out /path/to/authored-modules --verify
```

The current frozen artifact emits 14 modules for those three roots and 57
modules for the `App` root. The generated modules are readable TSX source
artifacts only; this experiment does not add imports, compile TypeScript,
regenerate `materialized-document.runtime.json`, run native/browser parity, or
retire patch scripts and the merge driver.
