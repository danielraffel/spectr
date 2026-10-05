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
- 53 pass the bounded one-return/object-prop JSX codemod.
- 10 fail closed: 6 multiple-return trees, 2 dynamic element tags, and 2
  unsupported prop shapes.
- Blocked components are `ContextMenu`, `MiniPreview`,
  `SpectrLengthScrollbar`, `SpectrFreezeLength`, `SpectrKnob`, `Chrome`,
  `EditModePopover`, `AnalyzerPopover`, `PickerDropdown`, and
  `HelpGuideOverlay`.
- The report is byte-identical across repeated runs and carries artifact and
  per-component source hashes.
- A planted spread-prop mutation changes artifact identity and moves `MBtn`
  into the `spread-props` blocked class; malformed artifact JSON is rejected.

The report does not claim that the 53 converted components can already be
built as a shared application, because dependency resolution, owner scopes,
TSX type-checking, runtime regeneration, and native/browser parity remain
separate gates. The ten blocked rows are the next codemod experiments.
