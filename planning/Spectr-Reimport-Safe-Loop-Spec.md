# Spectr (and future Pulp plugins) — Reimport-Safe Design Loop Spec

**Author:** investigation agent, 2026-05-01
**Status:** draft for review
**Scope:** `pulp import-design`, the canonical AST + adapters, reimport flow, DSP-binding
preservation, compat.json gating, asset/attribute drift handling, CLI surface for agents.
**Constraint:** must not block today's UX work in
`/Users/danielraffel/Code/spectr/feature/native-react-editor`.

## Why this exists

Spectr's `feature/native-react-editor` branch hand-coded the path from
`Spectr (standalone).html` → native-React → Pulp WidgetBridge once. Each subsequent design
revision (and there have been many — see `audit-2026-05-03-webview-vs-native-v0.69.1.md`) is
a fresh ingestion that risks:

1. **Silent loss of DSP wiring.** Today, Pulp binds DSP params to widgets via
   `widget_id == param_name` string match. If an importer relabels `Knob_3` → `Knob_4`
   because a sibling moved, the cutoff knob silently disconnects from `cutoff_param` —
   no error, no log, no diff.
2. **Owned-vs-generated boundary collisions.** `bridge_handlers.cpp` is meant to be
   hand-edited. Today's CLI overwrites it on every run.
3. **No 3-way reconciliation.** A user-edited `tokens.json` (e.g. clamped accent for
   accessibility) is lost on the next import.
4. **Heuristic detection that can't ratchet.** `--from claude` is the only routing knob;
   Stitch / Figma / Pencil / open-design / v0 each have their own export-format drift,
   and the parser just trusts `--from` blindly.

The proposal: turn the import path from a generate-then-forget tool into a deterministic
**lockfile-driven reimport** loop, with the canonical IR as the source of truth and stable
IDs as the primary invariant.

## Section a. Reimport flow (end-to-end)

### Steady-state diagram

```
External design (Claude / Stitch / Figma / MCP / HTML / open-design / v0)
        │
        ▼
[1] source detection — fingerprint + parser-version + format-version (#1031)
        │
        ▼
[2] adapter — source-specific parse to canonical IR
        │
        ▼
[3] stable-ID pass — inject ids based on (path × tag × role × ordinal × text-tie-break)
        │
        ▼
[4] compat-classify pass — every prop/asset is supported / partial / missing / dropped
        │
        ▼
[5] canonical IR (versioned, hashable, serializable to .pulp-import.json lockfile)
        │
        ├── first run: emit ui.js + tokens.json + classnames.json + assets/ + bridge_handlers.cpp.scaffold
        │
        └── reimport:
              │
              ▼
        [6] load previous IR from lockfile — call this BASE
            │
            ▼
        [7] 3-way diff: BASE  vs  GENERATED-NOW  vs  WORKING-OUTPUT
            │           ─┬─        ─┬─                 ─┬─
            │            │           │                  └── what's on disk now
            │            │           └── new IR from re-running adapter on new input
            │            └── what we generated last time
            ▼
        [8] classify each delta:
            • design-only (BASE → NOW, WORKING == BASE)        → auto-apply
            • local-only (BASE → WORKING, NOW == BASE)         → preserve
            • conflicting (all three differ)                   → block + report
            • compat-missing (NOW introduces unsupported prop) → warn or block per gate
            ▼
        [9] merge → emit new ui.js + assets + UPDATED .pulp-import.json
        ▼
        [10] post-merge validation: render screenshot, compat-score, DSP-binding integrity check
```

### What's auto-applied

- Tokens added in BASE→NOW with no local override → applied.
- Style props on stable-id nodes that match BASE in WORKING → applied.
- New nodes in NOW that have no BASE/WORKING counterpart → applied.
- Nodes deleted in NOW that match BASE in WORKING → deleted (with confirmation in `--strict`).
- Asset additions where filename doesn't collide → applied.

### What's blocked (hard fail, exit 1)

- A node carrying a DSP binding (param_id mapped to its stable-id) was deleted upstream.
  Block with: "Reimport would orphan DSP binding `cutoff_param` (was wired to stable id
  `kn_envelope_cutoff_3a8f`)."
- A `status: missing` prop appears in NOW that wasn't in BASE (per `compat.json`), and the
  user did not pass `--allow-missing-props`.
- 3-way conflict on the same node's same prop in BASE / NOW / WORKING.
- Lockfile fingerprint mismatch with on-disk generated files (means someone hand-edited a
  `@generated`-banner file).

### What warns but applies

- A node moved without its DSP binding — same stable id, new parent. Warn but auto-apply.
- An asset was renamed (same hash, new filename). Apply with note in the import-report.
- Tokens were renamed (`--accent` → `--brand-accent`) — heuristic match by value; warn.

### What requires explicit `--accept-format-version-bump`

- Source-detection confidence drops below 80% (per #1031). Falls back to most-recent-known
  parser, but only after the user opts in. Otherwise, exit with detector hint.

## Section b. Canonical AST + adapter contract

### Schema (additive on top of today's `IRNode` in `core/view/include/pulp/view/design_import.hpp`)

```cpp
struct IRNode {
    // Existing fields …
    std::string type;
    std::string name;
    std::string text_content;
    IRStyle style;
    IRLayout layout;
    AudioWidgetType audio_widget;
    std::string audio_label;
    std::vector<IRNode> children;
    std::unordered_map<std::string, std::string> attributes;

    // NEW — additive, non-breaking. All optional.
    std::optional<std::string> stable_id;     // populated by stable-ID pass
    std::optional<std::string> source_id;     // upstream id if source provides one
    std::optional<std::string> role;          // ARIA role / semantic class hint
    std::optional<size_t> ordinal_in_role;    // computed by pass
    std::optional<std::string> source_path;   // originating path "/root/section[2]/button[0]"
    std::vector<std::string> compat_warnings; // populated by compat pass
};

struct IRAsset {                              // NEW
    std::string id;        // stable id (often UUID from source)
    std::string mime;
    std::vector<uint8_t> data;
    std::optional<std::string> source_path;   // originating filename in upstream export
    std::optional<std::string> sha256;
};

struct DesignIRLockfile {                     // NEW — the .pulp-import.json shape
    std::string compat_schema_version;
    std::string parser_version;
    std::string format_version;
    std::string source_fingerprint;           // sha256 of input HTML / JSON
    std::string ir_hash;                      // sha256 of canonical IR
    DesignSource source;
    std::string source_file;
    DesignIR ir;                              // canonical IR
    std::vector<IRAsset> assets;
    std::unordered_map<std::string, std::string> dsp_bindings;
        // stable_id → param_id, persisted across runs
    std::vector<std::string> owned_files;     // hand-edited files we won't touch
    std::vector<std::string> generated_files; // files we re-emit (have @generated banner)
};
```

### Adapter contract

Every source (`claude`, `stitch`, `v0`, `pencil`, `figma`, `mcp:figma`, `mcp:stitch`,
`mcp:pencil`, `open_design`, `html`) implements one function:

```cpp
DesignIR parse_<source>(std::string_view input,
                        const ParseOptions& opts,
                        ParseReport& out_report);
```

`ParseOptions` includes the parser-version pin (so adapters can stay multi-version). `ParseReport`
collects per-prop status, unrecognized markers (for `--report-new-format`), and any
warnings the adapter wants to surface. The IR coming out of `parse_<source>` MUST NOT contain
`stable_id`s — those are minted by a single shared pass after parsing, so the heuristic stays
identical across sources.

### Prop normalization (the CSS/RN/Yoga unification)

The adapter writes idiomatic source props into `IRStyle`. A new pass — call it
`normalize_props(IRNode&, compat_json&)` — runs after all adapters and BEFORE codegen:

- expands shorthand (margin: 1 2 3 4)
- camelCases CSS property names (font-family → fontFamily) for compat lookup
- resolves `var(--…)` against `IRTokens`
- classifies each prop against `compat.json` and stamps `compat_warnings` on the node when
  status ≠ `supported`
- drops props with `status: dropped` (emit warning in report)
- replaces props with explicit `mapsTo` aliases (e.g. `flex-start` → `start` for justify)

This is the seam where the existing `compat.json` becomes load-bearing instead of decorative.

### Asset extraction

Every adapter emits `IRAsset[]` alongside the IR. The codegen pass writes them to
`./assets/<asset.id>.<ext>` and rewrites IR references (`<img src="…">`, font URLs) to point
at the local copy. The asset id is stable across runs when source provides one (Claude
bundle's UUID); otherwise it's `sha256(content)[:12]`.

## Section c. Versioned import + 3-way reconciliation

### The lockfile

`.pulp-import.json` lives in the project root next to `package.json`. It captures everything
needed to do a meaningful 3-way diff on the next run. JSON for human-readability;
deterministic field order so git diffs are small. Includes:

- canonical IR (for the BASE side of the 3-way)
- generated-files manifest with `@generated` banner sha256 (for drift detection)
- DSP-binding ledger (`stable_id` → `param_id`) — see section e
- input fingerprint (so a `--no-op` reimport can detect "nothing changed upstream")
- compat-schema-version + parser-version + format-version (per #1031)

### 3-way merge rules (formal)

For each node, identified by `stable_id`:

| BASE | NOW (re-parsed) | WORKING (on-disk emitted) | Action |
|------|-----------------|---------------------------|--------|
| ✓    | ✓ same           | ✓ same                    | no-op |
| ✓    | ✓ changed       | ✓ same as BASE            | apply NOW (design-only edit) |
| ✓    | ✓ same           | ✓ different from BASE     | preserve WORKING (local edit on a generated file → block in default mode, allow with `--accept-overrides`) |
| ✓    | ✓ changed       | ✓ different from BASE     | conflict — exit with diff |
| ✗    | ✓                | n/a                       | apply NOW (added node) |
| ✓    | ✗                | ✓                         | DELETE — but check DSP binding first; if bound, BLOCK |
| ✓    | ✗                | ✗                         | clean delete (already gone) |

For tokens (top-level map): same rules, but conflicts on tokens are warn-by-default rather
than block-by-default — tokens are usually safe to override.

### Hard-fail boundaries

1. DSP-binding orphan (see above).
2. compat regression: `compat.json` says `status: supported` for a prop that the parser tags
   as no-op-on-current-Pulp (e.g. `marginInlineStart` was supposedly supported, but
   widget_bridge has no branch — combined with finding 4 in repo-audit.md, this is a real
   risk).
3. Lockfile schema mismatch (e.g. running new CLI against an old project's lockfile without
   `--migrate`).

### Soft-fail (warn) boundaries

- Stable-ID drift > N% of nodes (suggests heuristic broke; user should re-baseline).
- Asset rename without sha256 match.
- Token rename suspected via heuristic.

## Section d. Generated vs owned

### Generated files (overwriteable, banner-marked)

```
ui.js                 — header: /* @generated by pulp import-design — do not edit */
tokens.json           — (W3C tokens), header in `_meta.generated_by`
classnames.json       — header in `_meta.generated_by`
assets/<id>.<ext>     — opaque binaries, drift checked by sha256
import-report.json    — every-run output, never read on next run
```

### Owned files (never touched by reimport)

```
bridge_handlers.cpp   — emitted ONLY when the file does not exist or has @scaffold banner
CMakeLists.txt        — emitted as a snippet in `pulp_import_design_init.cmake`, included by user's CMakeLists
package.json          — emitted only on init; reimport diffs but never overwrites
src/<owned-code>/     — anything outside the import root is owned
```

### Drift detection

Every generated file gets a header banner with the `ir_hash` it was emitted from. On reimport,
the CLI computes the file's content sha256 and compares against the lockfile entry. Mismatch
means "user hand-edited a generated file" → block-by-default, override with
`--accept-overrides`.

The `@scaffold` banner (used for `bridge_handlers.cpp`) means "we generated the first version,
but treat this as owned from now on." Subsequent runs respect this and never re-emit unless
`--regen-scaffolds` is passed.

## Section e. DSP binding preservation

### The ledger

`.pulp-import.json` contains:

```json
{
  "dsp_bindings": {
    "kn_filter_cutoff_3a8f": "filter_cutoff",
    "fd_envelope_attack_b2c1": "env_attack",
    "kn_envelope_release_e9d4": "env_release"
  }
}
```

Keys are stable IDs. Values are param IDs from `core/state/include/pulp/state/store.hpp`.

### Wiring at codegen time

Today, codegen emits `createKnob('Knob_3')` and `widget_bridge.cpp` resolves `Knob_3` against
`StateStore::all_params()` by name match. Reimport-safe alternative:

1. The codegen emits a `bindParam('kn_filter_cutoff_3a8f', 'filter_cutoff')` call after each
   widget's `create*` — sourced from the lockfile ledger.
2. `widget_bridge.cpp` gains a per-widget map `stable_id → param_id` populated by `bindParam`.
   `sync_from_store` and `getParam`/`setParam` consult this map first, fall back to the legacy
   name-match path.

### Validation on reimport

Before emitting NOW's `ui.js`, the CLI walks the ledger and confirms every bound stable_id
still exists in NOW. If any binding would be orphaned, exit 1 with:

```
ERROR: reimport would orphan 2 DSP bindings:
  • stable_id 'kn_envelope_cutoff_3a8f' (param 'cutoff_param') — node disappeared upstream
  • stable_id 'fd_lfo_rate_b2c1' (param 'lfo_rate') — node moved BUT stable id matched, OK
       (this one's a warn, not an error)

Options:
  • Re-run with --orphan-bindings=delete to remove the bindings
  • Re-run with --orphan-bindings=keep to retain them on hidden parent (next valid widget)
  • Edit the upstream design to keep these nodes
```

### Ambiguous-match handling

If two NOW nodes both heuristically match a single BASE node, exit 1 with the candidate list
and require `--bind-resolve <stable_id>=<chosen_node_path>`. Never silently pick.

## Section f. compat.json integration

### Loaded by the CLI at startup

`compat.json` is read once at the start of `pulp import-design`. It gets cross-referenced with
both:

1. The IR coming out of the adapter — every prop in `IRStyle` is looked up by surface
   (`css/`, `rn/`, `yoga/`) and its `status` recorded.
2. The DOM tags in the parsed bundle (for Claude's runtime path) — every tag is checked
   against `compat.json[html/<tag>]`.

### import-report.json

```json
{
  "compatibility": {
    "props_seen": 187,
    "supported": 164,
    "partial": 12,
    "missing": 10,
    "dropped_intentionally": 1,
    "by_category": {
      "css": { "supported": 142, "missing": 8 },
      "yoga": { "supported": 22, "missing": 2 }
    },
    "missing_props": [
      { "prop": "css/marginInlineStart", "occurrences": 12, "issue": "" },
      { "prop": "css/aspectRatio", "occurrences": 4, "issue": "" }
    ]
  },
  "assets": {
    "extracted": 7,
    "stable_ids_assigned": 247,
    "drift_warnings": []
  },
  "dsp_bindings": {
    "preserved": 12,
    "orphaned": 0,
    "ambiguous": 0
  }
}
```

### CI gates

- `pulp compat check` exits non-zero if any `status: supported` prop in `compat.json` lacks
  adapter code OR has zero tests.
- `pulp import-design` exits non-zero if the user-supplied `--strict` flag is set AND any
  prop in the input is `status: missing`.
- A new `pulp ui validate` command in section h uses compat.json + the import-report to
  produce a single PASS/FAIL.

## Section g. Attribute + asset drift

### Prop renames / format changes

When `compat.json` lists an alias (`mapsTo: "padding via shorthand"`), the import path
respects it. When the upstream source renames a prop without us knowing (e.g. Stitch
2026.06 changes `gap` → `flex-gap`), the new prop falls into `missing` with a
`--report-new-format` hint per #1031.

### Asset additions

New asset IDs that don't appear in BASE → write to `./assets/`, list in the report. No conflict.

### Asset renames (same hash, new filename)

Detected by sha256 match. Apply rename in the lockfile, keep the file in place. Emit a
warning so users see it.

### Asset removals

If WORKING still references a removed asset (e.g. an `<img src="…">` survives in a hand-owned
JSX file), warn but don't delete the file. User can `pulp ui validate` to confirm the
reference is dangling.

### Asset versioning

Every asset stored as `assets/<sha256[:12]>.<ext>` so two versions of the same file can
co-exist if a hand-owned file pins the old one. Lockfile records both.

## Section h. CLI surface (draft)

All commands JSON-output-friendly via `--json`. Designed for agent invocation. Exit codes:
0 = success, 1 = blocking failure (with diff/report), 2 = config error, 3 = format-version
drift requiring user opt-in.

### `pulp import-design --from <source> --file <path> [--init | --reimport]`

`--init` (default when no lockfile present): runs the full first-time emission. Writes the
lockfile.

`--reimport` (default when lockfile present): runs the 3-way merge flow. Implies `--strict`.
Writes a fresh lockfile only if all gates pass.

JSON output: `{ "phase": "reimport" | "init", "status": "ok" | "blocked", "report": <import-report.json>, "lockfile": <path>, "blocked_reasons": [...] }`.

### `pulp reimport`

Shorthand for `pulp import-design --reimport` using lockfile to recover `--from` and
`--file`. Most common agent invocation.

### `pulp diff`

Three-way diff of BASE / NOW / WORKING without writing anything. JSON output is the same
shape as `import-report.json` plus per-node deltas. Used by agents to ask "what would change
if I reimported?" without committing.

### `pulp compat check [--strict]`

Walks `compat.json`, validates every entry (`supported` has tests + adapter code; `missing`
links to issue). Without `--strict`, prints summary. With `--strict`, exits non-zero on any
discrepancy. Mirrors #1027 acceptance criteria 1-2.

### `pulp ui validate [--screenshot <ref.png>] [--similarity-threshold 0.85]`

Single-shot end-to-end validation. Renders the current `ui.js` headlessly (via existing
`pulp-screenshot`), compares against `<ref.png>`, runs compat check, runs DSP-binding
integrity check, emits a single PASS/FAIL.

### `pulp import-design --detect-only --file <path>`

Per #1031. Outputs source + format-version + parser-version + confidence. Agent-friendly
JSON. Exits 0 if confidence ≥ 80%, exits 3 if lower (with `--accept-format-version-bump`
hint).

### `pulp import-design --report-new-format > new-detector.json`

Per #1031. Emits a structured fingerprint diff for a new detector entry. User reviews,
commits as a fixture, opens a PR.

## Open decisions for human review

1. **Where does `.pulp-import.json` live?** Project root or `.pulp/` subdirectory? Spectr's
   `native-react/` directory shape suggests a top-level `imports/spectr.lockfile.json`
   pattern, since a project may have multiple imports (editor + sampler).
2. **Is the stable-ID heuristic a feature flag or always-on?** Recommendation: always-on once
   landed; it has no downside on first-run, and reimport is the only path that depends on it.
3. **Should DSP bindings be inverted in the IR?** Today the ledger lives in the lockfile.
   Alternative: an `IRNode::param_binding` field, serialized into `ui.js` as
   `bindParam(stable_id, param_id)`. Pro: codegen owns it. Con: IR has runtime knowledge.
4. **Do we ship `pulp reimport` as a verb, or stay with `pulp import-design --reimport`?**
   The verb is more agent-friendly; the long form is more discoverable.
5. **`--strict` default?** Recommend on for `--reimport`, off for `--init`.

## Incremental landing plan (do not over-engineer)

1. **Slice 1 — stable IDs (no behavior change yet).** Add `stable_id` to `IRNode`. Run the
   heuristic in a new pass. Emit them in the generated JS as comments only. No lockfile yet.
   Validates the heuristic on real Spectr exports.
2. **Slice 2 — lockfile + drift detection.** Write `.pulp-import.json`. On reimport, compare
   input fingerprints. Block on `@generated` banner mismatch.
3. **Slice 3 — 3-way merge for tokens only.** Limit blast radius. Tokens are easy: a flat
   map. Validates the merge logic before tackling node-level merges.
4. **Slice 4 — node-level 3-way merge + DSP binding ledger.** The big one.
5. **Slice 5 — compat.json gating.** Loaded at startup, surfaces in import-report.
6. **Slice 6 — `pulp diff` + `pulp ui validate`.** Agent-friendly verbs.
7. **Slice 7 — `--detect-only` / `--report-new-format` (#1031 plumbing).**
8. **Slice 8 — open-design adapter (per assessment doc).**

Each slice is independently shippable to main. Spectr's existing native-react flow keeps
working throughout — the new behavior only activates when `.pulp-import.json` is present.
