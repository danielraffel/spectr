# Band-group macros — working findings (feature/band-group-macros)

## Landed and proven (committed)
- `ed8e9fd` feat: C++ core. Params 4200-4203, group 8 "Macros", ±24 dB.
  `include/spectr/param_macro.hpp` (pure, Pulp-shaped, no Spectr includes),
  `include/spectr/macro_field.hpp` (band-domain rules), composition on BOTH
  threads via one shared `apply_macro_offsets`, `macro_members` in the
  supplemental blob at the EXISTING schema version, bridge commands.
- `HEAD~` test: 15 macro cases + surface/state/CLAP updates.
  Full `Spectr-test` suite green: 265 cases, 241540 assertions.

## confirm_failure.sh verdicts (recompile observed each step — not stale objects)
- Learn-shaped test "macro drag emits one gesture bracket on one parameter":
  broke it by re-pointing `set_macro_value` at `band_gain_param_id(macro)`.
  **CONFIRMED** (passes with fix, fails without).
- Parity test "the audio owner composes macros exactly as the control thread
  does": broke it by zeroing the audio owner's macro values.
  **CONFIRMED**.

## Verified design claims (not assumed)
- Pinned SDK is v0.854.1 = `084d4afbd23c70040881dd7271f460d17ceca9e1`,
  extracted at /Users/danielraffel/Code/spectr-sdk-0854/sdk/pulp-sdk.
- `6867ef9c` ("feat(format): project parameter groups to hosts") IS an
  ancestor of the pin (2630 commits back). `au_v2_common.cpp:126-136` sets
  `kAudioUnitParameterFlag_HasClump` + `clumpID`. So the comment in
  `src/param_surface.cpp` claiming otherwise was stale; it is corrected.
  Positive control ran: `clump` 20 hits / `kAudioUnitParameterFlag` 12 hits.

## Pre-existing failures on origin/main (NOT mine — controlled on both axes)
`Spectr-native-n1-test`: 42 cases, 2 failed, both on origin/main:
  - "native host automation projects through the compact live frame lane"
    (`gain7=-Infinity` where a gain was expected — a mute question)
  - "the settings copy button centres its feedback and answers a press"
Controls run: (a) original runtime JSON + my C++ → same 2; (b) my runtime
JSON + macros payload removed from `make_editor_state_payload` → same 2.
Recompile of editor_bridge.cpp observed in (b).

## Test-lane notes
- ctest names for `Spectr-test` are Catch2 TEST_CASE names registered WITH
  literal quotes → acceptance patterns need the `^"?` prefix.
- `tools/ci/ctest_pattern_gate.py` already asserts every pattern matches >=1
  test and has a `--plant` self-check. That IS the reverse check for
  `ctest -R` matching nothing and exiting 0.
- `Spectr-band-drag-cadence` (add_test, launches the Standalone) must be
  excluded — not run in this lane.

## Editor (materialized runtime) — patched via tools/patch_materialized_band_macros.py
Idempotent, asserts each anchor occurs exactly once, plus FORBIDDEN/REQUIRED/
COUNTS post-conditions. Applied:
  - `parseNativeMacros` in the head script + exported as `parseMacros`;
    absence -> null, never a rejection (old payloads still parse).
  - all THREE parsers carry `macros` (head live, bundle live, hydration) —
    they whitelist, so an unknown member would have been silently dropped.
  - `macroAdjustedGain(value, index)`: ONE read-time rule, four call sites
    (bars `effectiveGains`, response curve, both hover readouts).
    Gated on `modulationActiveRef` because the published modulated field
    ALREADY has macros composed in (processor applies macros before LFOs) —
    adding them again would double-count, and only while an LFO ran.
    NOT written into `renderGainsRef`: both `applyModulationFrame` and
    `applyHostAutomationState` overwrite that ref wholesale.
  - drag re-route: `macroDrag` recorded on pointer down, a member drag calls
    `driveMacro` (optimistic local + `macro_set`), `macro_drag_start` /
    `macro_drag_end` bracket it. Alt/Cmd opts out to direct member editing.

## Known hazards carried into this lane
- Context menu is contested: #150 (shortcuts/dismissal) + a menu-validation
  lane. Rebase onto whichever lands first. NEVER `git checkout --theirs` on
  the runtime artifact.
- The menu container carries a stale layout solve and does not grow when
  rows are added — extra rows paint over each other and a press can land on
  the wrong element. Pulp #8430 (child add/remove) and #8447 (typography)
  are queued. Adding macro rows makes that WORSE until those land. Expected,
  not mine, must be stated in the report.
- `text_bindings`/`layout_bindings`/`paint_bindings` address nodes by
  POSITIONAL DOM PATH. Insert children LAST or every later sibling silently
  re-points.

## Rebase onto advanced origin/main (43a005a, after #146 and #155)
Two conflicts, both expected:
- `native-ui/materialized/materialized-document.runtime.json` — one minified
  line, so any two edits conflict. Resolved by taking origin/main's artifact
  wholesale and RE-RUNNING the idempotent script. NOT `checkout --theirs`.
  The script then caught a genuinely stale anchor: the band-readout lane
  changed `hoverRef.current = { band, x, y }` to `{ band, x, y, n: N }`, so
  the pointer-down anchor was re-pointed at the enclosing block instead.
  That is the anchor assertion doing its job — a looser patch would have
  silently no-op'd and shipped a drag that never opened a gesture.
- `tools/ci/acceptance-ctest-patterns.txt` — resolved by keeping BOTH sides.

## Pattern gate
`ctest_pattern_gate.py` exits 1 on my build with 2 DEAD patterns, `^native:`
and `Built Spectr`. Controlled: the SAME 2 are dead running my base's
unmodified patterns file against the same build, so they are pre-existing and
not mine (they may simply be targets this build dir does not build).
All 5 macro patterns match (12 + 1 + 1 + 1 + 1). `--plant` exits 1 and names
the planted row, and a raw `ctest -R` on a non-matching pattern printed
"Total Tests: 0" and exited 0 — the documented trap, which is why the gate
rather than a bare ctest run is the reverse check.

## fix-manifest tokens (screened)
`macro_set_members`, `macroAdjustedGain`, `macro_members`, `parseNativeMacros`
— all 0 files on origin/main (control `modulation_target_mask` = 15 files, so
the instrument works), and all present in `strings -a` of the built binary
(2/1/1/1; control token `SPECTR` = 47).

## Post-rebase verification (base 43a005a)
- `Spectr-test`: 272 cases, 241965 assertions, ALL PASS.
- Learn-shaped test re-confirmed by `confirm_failure.sh` after the rebase:
  CONFIRMED, with the recompile observed at baseline/broken/restored.
- Scope control on the editor patch (syntax alone is not enough — a
  block-scoped declaration in the wrong function body compiles clean):
  brace-matched `FilterBank`'s body (chars 64128..161214) and proved all 13
  new symbols live inside it — `macroStateRef`, `postNative`, `driveMacro`,
  `macroOwning`, `macroAdjustedGain`, the menu props, the drag hooks, and the
  display call sites.
- `Spectr-native-n1-test`: the copy-button failure is GONE (fixed by #155);
  the compact-live-frame-lane failure remains, pre-existing.

## Delivered
- PR #156 opened. Validation queued as `sy-20260916-b97443`.
  `main` is unprotected with no auto-merge, so the merge is explicit and must
  be proven with `git merge-base --is-ancestor`, never from an exit code.
- Follow-up issue #157 filed for #46's residue (an LFO `BandSubset`
  destination pointing at a macro's members), including the one open design
  question: reuse a macro's membership, or carry a separate mask.

## Coordination left open
- `native-ui/materialized/help-content.js` is NOT touched here. The
  `docs/host-automation-help` lane (branch `fa19f4e`) rewrites it (+46 lines)
  and owns the Automation section. Its `help_overlay_contract.py` checks an
  explicit `PROMISED_PARAMS` allowlist, NOT every registered parameter, so
  macros landing does not break that gate either way. Whoever lands second
  should add a Macros entry to the Automation section and, if they want it
  enforced, add "Macro 1" to `PROMISED_PARAMS`.

## Native-failure control re-run on the NEW base (43a005a) — decisive
My parser change touches the live-state path, so "pre-existing" could not be
carried over from the old base by assumption. Re-controlled: with
origin/main's UNMODIFIED artifact rebuilt into the test binary, both
  - "native host automation projects through the compact live frame lane"
  - "the settings copy button centres its feedback and answers a press"
still fail (each 1 case, 1 of 12 assertions). Neither is caused by this lane.
Artifact restored afterwards and re-verified: the patch script reports
"no change needed" against the committed artifact.

## Gate
Run 35057383424, event `pull_request`, head_sha 31ab891 — matches HEAD exactly,
so the docs push re-triggered rather than leaving a stale-SHA validation.

## The context-menu rows were MEASURED and WITHDRAWN (second rebase, base 946cd2e)
Both contested menu lanes landed while this was in flight: #150 (shortcuts and
dismissal) and #154 (menu-item validation, which added
`tools/menu_scenario_check.py` -- a gate that drives every named band-menu row
in the real standalone and judges it on the state it changes).

The macro rows were applied and driven through that gate:
  - origin/main's artifact, same binary: 19 checks, 0 failures
  - with the macro rows:                 19 checks, 6 failures
Two failures are presses landing on the WRONG row (`Zero selection` and
`Sculpt` fire something else). Three are structural child counts the gate pins
exactly (17 rows with a selection live, 14 without) -- any added row breaks
those at any position, so tail placement does not help.

Root cause is upstream: the menu container carries a stale layout solve and
does not grow when children are added. Not this lane's to fix -- but not a
licence to regress a surface two lanes had just repaired either.

WITHDRAWN. The bridge command `macro_set_members` stays, with its validation
and tests, so the affordance is one patch away. The patch script's docstring
records exactly what was removed and instructs the next author to RE-MEASURE
with the menu gate rather than assume the pinned misaim map still holds.

Honest consequence: until that follow-up lands there is no in-product way to
ASSIGN a macro. Parameters are live and automatable and the drag re-route works
for any macro that has members; membership can only be set via the bridge today.

## Third-party test suites this lane had to re-point (all caught by the gate)
Routing every displayed gain through `macroAdjustedGain` moved three lines that
other suites quote verbatim. Each refused to render a verdict rather than
reporting a pass it could not prove -- the gate working exactly as designed:
  - materialized_curve_edge_span: two plants reported "found 0 sites,
    expected 1" (exit 2). All four plant strings now carry the macro call on
    BOTH sides so each plant changes only its own geometry. Five plants
    re-verified to exit 1.
  - materialized_mute_collapse_direction: its own `sentinel normalisation`
    CONTROL read 0 and the suite aborted by design. Needle updated at 2 sites.
  - materialized_band_readout: lifts the readout source into an isolated scope
    where `macroAdjustedGain` was a free identifier -> ReferenceError. The
    harness now supplies the rule FAITHFULLY, and a new assertion checks that a
    macro offset moves the readout and is NOT double-counted while the
    modulation overlay owns the paint refs. Confirmed failing when the rule is
    reduced to the identity.
