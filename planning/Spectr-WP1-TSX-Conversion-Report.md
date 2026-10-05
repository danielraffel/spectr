# Spectr WP-1 bounded TSX conversion report

`tools/wp1_tsx_conversion_report.mjs` inventories the frozen native artifact
against the existing fail-closed leaf converter. It parses the artifact with
pinned `@babel/parser` 7.28.4, discovers all top-level function components,
and records source byte ranges and SHA-256 values. Each component is converted
independently; successful output gets its output digest, while rejected output
gets a structured reason. The report is read-only and never rewrites the
canonical runtime JSON.

Run it with:

```sh
npm ci --ignore-scripts --prefix tools/wp1-parser
node tools/wp1_tsx_conversion_report.mjs \
  --artifact native-ui/materialized/materialized-document.runtime.json \
  --out /path/to/tsx-conversion-report.json
python3 -m unittest -v tools/test_wp1_tsx_conversion_report.py
```

Current artifact evidence:

- 63 top-level function components discovered.
- All 63 pass the bounded object-prop JSX codemod, including conditional
  returns, nested callback return trees, the six authored `React.Fragment`
  member tags, two computed data-attribute prop cases, and the one authored
  `Object.assign` props expression.
- Arbitrary dynamic/member/lowercase tags, arbitrary spread props, and
  unknown non-object props expressions remain fail-closed. The new shapes are
  explicit allowlists derived from the frozen artifact rather than a general
  relaxation of the converter.
- The report is byte-identical across repeated runs and carries artifact and
  per-component source hashes.
- A planted spread-prop mutation changes artifact identity and moves `MBtn`
  into the `spread-props` blocked class; malformed artifact JSON is rejected.

The report does not claim that the 63 converted components can already be
built as a shared application. Dependency resolution, owner scopes,
TSX type-checking, runtime regeneration, and native/browser parity remain
separate gates. The canonical runtime artifact, patch scripts, and merge
driver are unchanged by this experiment.
