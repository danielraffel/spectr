# Spectr Status — Live Handoff Dashboard

_Last updated: 2026-04-24 (M8 + M11 engine landed, detach_webview pickup, format-validation first pass, pulp native-runtime harness merged) — read this first if resuming work in a new session._

This doc is the single-page state-of-the-world for Spectr. Every commit
to `main` should refresh the dates + "what landed" bullets at the top.

## Branch state

- **`main`** — fully unified. M1-M8 shipped, M11 windowed STFT engine
  landed, WebView editor embedded via `pulp::view::EditorBridge`
  (post-cutover per PR #17 on 2026-04-24), SDK pinned at Pulp v0.42.0
  with explicit `EditorBridge::detach_webview()` teardown (PR #21).
  Current HEAD: `3730a21`.
- **No parked branches.** The old `feature/webview-editor-parked`
  branch was merged and deleted. No open cosmetic debt on main.
- **Open feature branches**:
  - `fix/clap-state-reproducibility-flush` — 1-line fix in
    `serialize_plugin_state()` to read `kBandCount` from StateStore
    rather than cached `layout_`. Catches the clap-validator
    `state-reproducibility-flush` test. Not yet PR'd.
  - `planning/closed-gap-726-pickup` — small planning addition
    recording the pulp#726 closure (Spectr PR #22, shipyard in flight).

## Current SDK pin

- **Pulp v0.42.0** (tagged 2026-04-24 18:41 UTC). Brings:
  - `pulp::view::EditorBridge` (pulp#711) — renderer-agnostic JSON
    dispatch used by Spectr's `src/editor_bridge.cpp`
  - `pulp::view::EditorBridge::detach_webview(WebViewPanel&)` (pulp#728,
    fixes pulp#726) — closes the teardown-race window
  - `WindowHost::get_content_size()` (pulp#670) — real content-area
    sizing for the standalone
- **Next bump target: Pulp v0.44.x** (auto-release in flight as of
  2026-04-24 22:59 UTC). Brings the full pulp#468 native-runtime
  import harness (PR #730 shims + PR #731 parser/harness). Unlocks
  `pulp import-design --from claude --execute-bundle` producing real
  DesignIR from Spectr's bundled-React `editor.html`.
- **Shipyard pin** at v0.46.0 (Spectr PR #20). Tracks pulp's own pin.

## In-flight Pulp issues/PRs Spectr is tracking

| # | Title | State | Impact on Spectr |
|---|---|---|---|
| [pulp#468](https://github.com/danielraffel/pulp/issues/468) | `pulp import-design --from claude` native-runtime harness | ✅ closed workstream (PR #730 + #731 merged 2026-04-24) | Unlocks real `pulp::view` output from Spectr's bundled-React `editor.html` — awaiting SDK v0.44.x release to validate |
| [pulp#662](https://github.com/danielraffel/pulp/issues/662) | WebView pre-paint white/chrome flash | OPEN | 200ms load flash still present; mitigated via `initial_html` in `attach_if_needed` (Pulp v0.38.0+) |
| [pulp#663](https://github.com/danielraffel/pulp/issues/663) | Standalone TabPanel opt-out | OPEN | Standalone-only; plugin format editors unaffected |
| [pulp#729](https://github.com/danielraffel/pulp/issues/729) | `import-design` bridge-first default | OPEN (filed by Spectr) | Consumer proof of native-view + bridge-scaffold bias. Actionable now that #731 landed |
| [pulp#743](https://github.com/danielraffel/pulp/issues/743) | `pulp doctor --validators` | OPEN (filed by Spectr) | Auto-heal broken-signature pluginval installs |
| [pulp#744](https://github.com/danielraffel/pulp/issues/744) | `pulp doctor --caches` | OPEN (filed by Spectr) | Auto-heal dangling FetchContent symlinks |
| [pulp#748](https://github.com/danielraffel/pulp/pull/748) | CLAP event-space filter + state_save short-write loop | OPEN (filed by Spectr) | Two of three M10 CLAP bugs, fixed framework-side |

## In-flight Spectr PRs

| # | Title | State |
|---|---|---|
| [spectr#22](https://github.com/danielraffel/spectr/pull/22) | planning closure for pulp#726 | OPEN (shipyard in flight) |
| `fix/clap-state-reproducibility-flush` (local) | Last M10 CLAP bug | pushed, not yet PR'd |

## Milestone status (source of truth — supersedes build plan)

| # | Name | Status | Where it lives |
|---|---|---|---|
| M1 | Foundation | ✅ landed | `include/spectr/*.hpp`, `src/spectr.cpp` scaffold |
| M2 | DSP truth spike | ✅ landed | `src/fft_engine.cpp`, `src/iir_engine.cpp`, `src/block_fft_engine.cpp` |
| M3 | Analyzer bridge wiring | ✅ landed | `src/bridge_process.cpp` (VisualizationBridge) |
| M4 | State registration (GATE) | ✅ landed | `Spectr::define_parameters` + `serialize_plugin_state` / `deserialize_plugin_state` |
| M5 | UI skeleton | ✅ landed | `src/ui/band_field_view.cpp` + supporting |
| M5b | WebView editor via prototype HTML | ✅ landed (cutover PR #17) | `src/ui/editor_view.cpp` + `src/editor_bridge.cpp` + `resources/editor.html` |
| M6 | Edit modes | ✅ landed | `include/spectr/edit_engine.hpp` (Sculpt/Level/Boost/Flare/Glide) |
| M7 | Pattern library data model | ✅ landed | `src/pattern.cpp` (8 factories + user CRUD + JSON export) |
| M8 | Snapshot / A-B / morph | ✅ landed | `src/spectr.cpp` snapshots_ + `apply_morph_to_live`; tests #104-106 green |
| M9 | Preset file format | ✅ landed | `src/preset_format.cpp` + `include/spectr/preset_format.hpp` + `test/test_preset.cpp` |
| M10 | Format validation | 🚧 in progress | `tools/validate-formats.sh`; AU PASS, VST3 PASS (use cask pluginval path), CLAP 1 Spectr bug left (this branch fixes it) + 2 pulp bugs in pulp#748 |
| M11 | Windowed STFT engine | 🟡 engine landed, polish pending | `src/block_fft_engine.cpp` + tests #107-109 green; CPU budget + DAW smoke not started |

## M10 format validation — current state (2026-04-24)

| Format | Validator | Result |
|--------|-----------|--------|
| AU v2 (`aufx Spec Pulp`) | `auval` | ✅ PASS (Render, Connection, BadMaxFrames, Parameter, MIDI all pass) |
| VST3 | `pluginval --strictness-level 10` | ✅ PASS (all lanes green incl. Fuzz Parameters) — **must use `/Applications/pluginval.app/Contents/MacOS/pluginval`, NOT `/usr/local/bin/pluginval` which has a corrupted signature on macOS 26.x** |
| CLAP | `clap-validator validate` | 🚧 3 bugs found, 2 fixed upstream in pulp (PR #748 — `space_id` filter + short-write loop), 1 Spectr-side fix on `fix/clap-state-reproducibility-flush` branch (cached `layout_` vs StateStore) |

After Spectr's state-repro fix + pulp#748 land, clap-validator should
go 21/21 green. At that point M10 completes.

## Recent landings (2026-04-23 → 2026-04-24)

- **pulp#711 EditorBridge cutover** (Spectr PR #17, 2026-04-24) —
  replaced `spectr::HostBridge` stand-in with `pulp::view::EditorBridge`.
  Net -125 LOC, 109/109 tests green.
- **pulp#728 detach_webview pickup** (Spectr PR #21, 2026-04-24) —
  explicit teardown: `bridge_.detach_webview(*panel_)` before native
  child detach. Requires Pulp SDK > v0.41.1.
- **Shipyard pin bumps** — v0.29.0 → v0.44.0 (PR #14) → v0.46.0 (PR #20).
- **Agent-coordination protocol** (Spectr PR #19) — formalized the
  checkpoint-comment pattern the pulp-side + Spectr-side agents used
  for the pulp#711 cutover. Links to pulp#727 (MCP relay future).
- **pulp#468 native-runtime import** (upstream, 2026-04-24) — full
  workstream merged: PR #730 (web-compat shims) + PR #731 (envelope
  parser + harness). Spectr is the prime consumer; when v0.44.x SDK
  ships, `pulp import-design --from claude --execute-bundle` will
  produce real DesignIR from `editor.html`.

## Format-validation gotchas codified

- `/usr/local/bin/pluginval` on macOS 26.x is a ripped-out copy from
  the `.app` bundle; its signature validates against a bundle that
  doesn't exist around it, so `spctl` rejects and amfid SIGKILLs
  pluginval before any output. Use `/Applications/pluginval.app/Contents/MacOS/pluginval`.
  Filed as pulp#743 for framework-level auto-heal.
- `~/Library/Caches/Pulp/fetchcontent-src/threejs-*` may be a symlink
  to a deleted dev clone — dangling-symlink state breaks every
  subsequent `cmake` configure with a misleading "source directory
  missing" error. Filed as pulp#744 for auto-heal.
- Shipyard #249 — tree-hash drift detection during `shipyard run`
  (self-inflicted race from mid-run edits in the same tree).

## What the new agent should do on resume

1. Read this doc.
2. Read `planning/Spectr-V1-Build-Plan.md` for original milestone
   scope (V1 Build Plan — M10/M11 section is the only actively-moving
   part; everything before M8 is historical).
3. `git -C /Users/danielraffel/Code/spectr log --oneline -10` —
   check for progress since this doc's last-updated date.
4. Check in-flight Pulp PRs: `gh pr view 748 -R danielraffel/pulp`
   and pulp release list for v0.44.x.
5. If the `fix/clap-state-reproducibility-flush` branch has merged,
   run `tools/validate-formats.sh` (via the cask pluginval path) to
   confirm M10 is fully green.
6. If pulp v0.44.x is released, bump Spectr's SDK pin + re-run
   `pulp-import-design --from claude --file resources/editor.html
   --execute-bundle` to see if the native-runtime harness produces
   real output for the bundled-React export. If yes: decide whether
   to migrate Spectr's editor from WebView to native `pulp::view`
   per pulp#729.

## CI direction (when we get to M10 close-out)

**No CI is set up yet.** Matches the user's `feedback_spectr_ci_namespace`
preference: "When adding CI to Spectr/Pulp-based projects, go Namespace
first; M10 is the natural trigger." Once M10 closes (CLAP reproducibility
fix + upstream merges), wire `.github/workflows/build.yml` modelled on
pulp's — Namespace runners as the default (pulp flipped the default on
2026-04-24 per the `ci/namespace-default` PR in flight), with local
macOS in parallel via `shipyard ship`.

## Post-UX-parity work queue (2026-05-03)

Sequencing of the next three Spectr-relevant phases, in priority order.

### Active phase — close WebView↔Native UX parity

The single gating phase. Until this closes, no design-loop / reimport / open-design work begins.

| pulp # | Title | State |
|---|---|---|
| #1147 | Popover render parity | OPEN |
| #1148 | Overlay click dispatch | PR #1297 in flight (subagent landed 2026-05-03) |
| #1070 | Typography drift | OPEN |
| #1292 | @pulp/react useState=null crash | CLOSED via #1295 (root-cause React-dedup), shipping in v0.69.3 (PR #1309) |
| #998 | Layout regression Spectr workaround removal | OPEN |

When all five close: umbrella **#1307** (reimport-safe design loop) unblocks.

### Unblocked next phase — reimport-safe design loop (umbrella #1307)

Filed 2026-05-03 by the reimport-investigation subagent. Spec at `planning/Spectr-Reimport-Safe-Loop-Spec.md`; assessment artifacts in `planning/reimport/`. Sub-issues, in suggested implementation order:

| pulp # | Title | Notes |
|---|---|---|
| #1299 | Stable IR node IDs (path × tag × role × ordinal × text-tie-break) | Foundational seam |
| #1300 | Lockfile + 3-way merge for reimport (`.pulp-import.json`) | Foundational seam |
| #1301 | DSP binding ledger keyed by stable id | Foundational seam |
| #1303 | Generated-vs-owned file boundary + `@generated` banners | Foundational seam |
| #1302 | Load `compat.json` at runtime, classify props, emit `import-report.json` | Parallel slice |
| #1304 | `--from open-design` adapter (parser-only) | Parallel slice — narrowed (see #1311) |
| #1305 | `pulp diff` + `pulp ui validate` agent-friendly verbs | Parallel slice |
| #1306 | Emit `assets/` directory + asset-sha256 lockfile entries | Parallel slice |

### Subsequent phase — open-design CLI/plugin extension

Filed 2026-05-03 as **#1311** (companion to #1304's parser slice). Adds the multi-type artifact dispatch surface (`--type <kind>` orthogonal to `--from`), stdin streaming (`--file -`), manifest-based artifact selection (`--manifest` + `--artifact-id`), and plugin slash-command + skill updates. Borrows 4 additional patterns from open-design (anti-slop linter, manifest-version field, skill front-matter `pulp:` block, multi-CLI agent detection probe) — captured as a comment on #1307, not filed as separate issues.

### Future phases (placeholders)

- **Sampler Phase 4** — binary-asset-drop API + sampler-specific bridge handlers, file when triggered (`spectr/planning/Spectr-Sampler-Phase-Spec.md`, links pulp #709)
- **Spectr CI** — Namespace-first wiring once M10 closes (per `feedback_spectr_ci_namespace`)
- **WebView → Native editor migration** — pending pulp v0.44.x / native-runtime harness validation (per the v1 plan and pulp #729)

## compat.json — claim every supported prop, plan every missing one (2026-05-03)

**Goal:** For every CSS / RN / Yoga / React / HTML / Canvas2D property our consumers might use, make the support claim AND keep it provably synced with code. The pulp `compat.json` matrix is the single source of truth — but only if (a) it stays current with main, (b) every claim has a test, and (c) docs that link from prop → mapsTo → code stay in sync automatically.

### Current state (snapshot 2026-05-03)

| Section | Total | supported | partial | missing | wontfix |
|---|---|---|---|---|---|
| **CSS** | 194 | 70 (36%) | 30 | 63 | 31 |
| **RN** | 114 | 39 (34%) | 3 | 69 | 3 |
| **Yoga** | 53 | 28 (53%) | 7 | 18 | — |
| **TOTAL** | **361** | 137 (38%) | 40 | **150** | 34 |

The directive: aim for support of everything possible; prove each support claim with a linked test; treat `wontfix` as last-resort, not default.

### Enforcement state (where compat.json is consumed)

- ✅ **Edit-time gate** — `tools/scripts/compat_sync_check.py` invoked from `.githooks/pre-push:27-64` AND `.github/workflows/version-skill-check.yml`. Catches when bridge / view / @pulp/react source files are touched without compat.json updates. Bypass trailer: `Compat-Update: skip prefix=<...> reason="..."`.
- ❌ **Import-time runtime load** — pulp #1302 OPEN. `pulp import-design` does NOT load compat.json today, so plugin authors don't get warned at import time about props that will silently no-op. The runtime should classify imported props by status and emit `import-report.json`.
- ❌ **Per-prop `tests` field is empty** — every entry has `"tests": []`. Should link the Catch2 / vitest case that pinned each `supported` claim. Refresh agent in flight (2026-05-03) to populate.
- ❌ **`mapsTo` → docs link** — the original plan was to surface each entry's `mapsTo` string as a clickable link to the specific source line in `core/view/src/widget_bridge.cpp` / `core/view/js/web-compat-style-decl.js` etc., so doc readers can drill into the actual implementation. NOT yet implemented. Track under #1302's import-report or as a `pulp docs build` step (#1027 follow-up).

### Active triage (2026-05-03 — agents in flight)

Two agents are working compat.json proactively (gated on UX parity #924):

1. **Refresh agent** (`/tmp/pulp-compat-refresh/`) — re-validates all 361 entries against `origin/main`, populates `tests` field with real test refs, bumps schema 0.1 → 0.2, comments on #1029.
2. **Triage agent** (`/tmp/pulp-compat-triage/`) — turns the 40 partial / 150 missing / 34 wontfix into 4-6 actionable batches under one umbrella issue. Each batch gets specific props + completion recipe + test plan. Wontfix re-evaluated for "wontfix-by-omission" (default to flip back to missing unless there's a hard architectural reason).

When those agents complete: their umbrella + sub-issues become the long-running queue for "support everything possible".

### Sync rules

To prevent compat.json from falling out of sync again:

- Compat-sync gate (#1029) is now in pre-push + CI, advisory mode → flip to hard-fail once the triage agent completes the schema 0.2 + tests-field populate
- Schema bump (0.1 → 0.2) lands with the refresh PR — locks the new richer entry shape
- Tests field MUST be populated for every `supported` claim (refresh agent enforces)
- Docs build (`mkdocs` or `docs/reference/compat/<prefix>.md`) should auto-generate from compat.json so every prop has a public doc page with mapsTo + test link — pulp #1027 follow-up

### Reference specs (sources of truth)

- Yoga: https://www.yogalayout.dev/docs/styling
- React Native: https://reactnative.dev/docs/view-style-props
- CSS: https://developer.mozilla.org/en-US/docs/Web/CSS/Reference/Properties

### Cross-refs

- Umbrella: pulp #1027 (compat matrix + CI gate + docs)
- Per-PR gate: pulp #1029 (the script lives at `tools/scripts/compat_sync_check.py`; the issue tracks finishing the hard-fail flip + per-section hardening)
- Runtime load: pulp #1302 (gated on UX parity #924 via #1307 umbrella)
- Versioned schema: pulp #1031 (closed via #1047, schema 0.2 wire)

## Quick reference — file paths

- Status doc: `planning/Spectr-Status.md` (this file)
- Milestone plan: `planning/Spectr-V1-Build-Plan.md`
- Cutover gap tracker: `planning/Spectr-Cutover-Gap-Tracker.md`
- Agent-coordination protocol: `planning/Spectr-Agent-Coordination-Protocol.md`
- Product spec: `planning/Spectr-V2-Product-Spec.md`
- Pulp handoff: `planning/Spectr-Pulp-Handoff.md`
- Sampler (future): `planning/Spectr-Sampler-Phase-Spec.md`
- Build signoff: `planning/Spectr-Build-Signoff.md`
- Public repo: https://github.com/danielraffel/spectr
- Pulp SDK install: `~/.pulp/sdk/0.42.0/` (currently pinned — v0.44.x next)
- Pulp main worktree (for SDK rebuilds): `/tmp/pulp-main-628` (may be stale; fetch + checkout before use)
