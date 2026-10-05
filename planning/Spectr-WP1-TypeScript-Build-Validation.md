# Spectr WP-1 TypeScript build validation

`tools/wp1_typecheck_authored_modules.mjs` validates an emitted authored
closure after `tools/wp1_authored_module_emitter.mjs` has verified the exact
artifact, dependency manifest, declaration slices, and module output hashes.
It assembles the modules in the manifest's dependency-first order into a
temporary TSX program, generates ambient declarations for the external runtime
surface, and runs the pinned TypeScript 5.9.3 compiler with `--noEmit`.

The default mode is a syntax/build check (`--noCheck`) for the full closure.
It proves that every emitted TSX module parses and can be assembled into one
dependency-resolved program without changing the canonical runtime. The
`--semantic` mode runs TypeScript's semantic checker and is intended for
small, typed slices while authored prop and runtime contracts are still being
introduced. A semantic diagnostic is reported with the compiler's exact
missing-name, missing-import, or type error and no receipt is written.

Example:

```sh
node tools/wp1_typecheck_authored_modules.mjs \
  --artifact native-ui/materialized/materialized-document.runtime.json \
  --manifest /path/to/app.manifest.json \
  --emission /path/to/authored-modules/authored-modules.manifest.json \
  --out-report /path/to/app-build-report.json
```

Current evidence covers a 57-module `App` closure in syntax/build mode and a
semantic `MBtn` slice. A planted `MissingWp1Type` declaration is rejected by
semantic mode after the module output hash is updated, proving the negative
path catches missing types while retaining the emission integrity boundary.
This experiment does not claim isolated ES module import contracts, complete
React typings, runtime artifact regeneration, or native/browser parity.
