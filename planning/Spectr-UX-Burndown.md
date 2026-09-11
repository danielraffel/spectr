# Spectr UX and Automation Burn-down

Last updated: 2026-09-04

## 2026-09-06 wrap-up: PKG critical path and corrected proof labels

This section supersedes older "CLOSED"/"Done" labels below wherever the two
disagree. It records what actually gates the PKG today and which rows were
labelled from an instrument that cannot see the surface they claim to prove.

### The PKG is gated on two Pulp fixes reaching an OFFICIAL SDK

Spectr never pins an unofficial SDK build, so the PKG cannot be built until a
tagged release carries both fixes for the defects Daniel reported:

| Pulp PR | Defect it fixes | State |
| --- | --- | --- |
| #8097 | `SNAPSHOT` painted truncated (`SNAPSHO`) — measure walked `own -> inherited -> "Inter"` while paint resolved `own -> "Inter"`, so a pinned box measured right and painted wrong | MERGED 2026-09-06T23:18:48Z |
| #8094 | Settings body collapsed (blank / ~50px) — the retained-scroll split cleared `flex_grow`/`flex_shrink` but left `flex_basis: 0` behind, so content main size resolved to `0 + padding` | OPEN, auto-merge armed |

The latest official SDK **v0.835.0 was tagged 2026-09-06T01:40Z and contains
NEITHER**. A PKG built against it today would ship the exact two defects the
burn-down exists to close. Verified over git transport (zero API cost):

    git merge-base --is-ancestor b1ef8b9112e7c6f831bb9c31324b0862f8ab0f5b v0.835.0   # 8097 -> false

`tools/ci/pulp-sdk-release.json` is still pinned to v0.829.0. Three fields move
on the bump: `asset_sha256`, `release_tag`, `source_git_sha`.

Order matters: if the pending `release/version-bump` PR (#8099) tags before
#8094 merges, the resulting SDK carries the SNAPSHOT fix but not the Settings
fix, and the PKG is still half-defective. The bump has to land after #8094.

### Corrected proof labels

The adversarial exact-head review (DEL-1) found rows marked closed on an
instrument that cannot observe the surface the row is about. A browser lane
runs the materialized document in Chromium; it never builds or runs the SDK,
so it cannot prove anything about native Skia raster.

| Row | Was | Is |
| --- | --- | --- |
| MOD-1 | "Automated proof CLOSED" | Browser-only. The driven oracle is real and green, but it drives the DOM. The native modulation capture still SKIPs on the shipping path and only produces a frame through the diagnostic `-UNWEDGED` route until #8094 lands in an SDK |
| PRE-3, OVL-1..5, SET-4, SET-5, SET-7, COR-3 | "Done" | Done with BROWSER-ONLY proof. Already labelled as such in the rows themselves; repeated here so the summary does not read as native coverage |

`Spectr-native-shot` is the branch's only native/Skia instrument and it is
**not wired into CMake `add_test` or `m5-product-acceptance.yml`** — so no CI
lane exercises it. That is why browser-only labels drifted into reading as
closure.

### Landed since the last doc update

| Commit | What |
| --- | --- |
| `3f45692` | `tools/native_shot.cpp`: region-cropped captures with a PER-REGION content floor (a whole-frame floor passes on the dimmed editor behind the modal alone, so it cannot certify the modulation group painted), root-space `absolute_origin()` because bounds are parent-relative, DOM + native state readback that pumps audio first (`ModulationSettings` is rebuilt on the param-sync lane `process()` drives), and live element-id resolution because the shipping asset gives both toggles the same `[data-spectr-setting-toggle]` attribute |
| `ae1011e5` | SET-4/SET-5 flaky oracle closed, 6/6 |
| `fef56c4`, `d28de22`, `c1f3eb4`, `2e42824` | earlier UX fixes, previously unrecorded here |

### Still open

PRE-1 (blocked on Daniel's threshold decision), PRE-2, PRE-4, PRE-6 (open
product decision), SET-1 + SET-6 (blocked on #8094 reaching an SDK), AUT-3
(needs a real Logic host; not observable from any automated oracle), MOD-3,
COR-1, COR-3, DEL-2, DEL-3, DEL-4.

COR-1 has a concrete fix ready: the right analyzer ruler's `0` is given the
same emphasis colour the left gain ruler's `0` uses, but the two zeros sit
`0.7916666 * halfH` apart and mean different things. Stop sharing the
emphasis, label the heads `dBFS (analyzer)` vs `dB (gain)`, regenerate
`native-ui/materialized/materialized-document.runtime.json`.

## 2026-09-06 round-trip architecture: which direction actually works

Answering "do we always need to go through the React import flow, or can we
just change the native code?" — measured, not assumed.

**Direction B (edit the native artifact) is the ONLY direction that works
today, and it works by hand-patching a committed blob.**

`native-ui/materialized/materialized-document.runtime.json` is tracked in git
and nothing in the build produces it:

| probe | result |
|---|---|
| `add_custom_command`/`add_custom_target` in `CMakeLists.txt` | 0 |
| ...that OUTPUT the runtime json | 0 |
| control: file referenced in `CMakeLists.txt` | 3 (consumed, never produced) |
| `git ls-files` the runtime json | COMMITTED |
| files in the repo that write `materialized-document.json` | 1 — the patcher itself |

**Direction A (change the design, re-import) does not exist as a producer.**
`tools/patch_materialized_editor.py` says so in its own docstring: *"no recipe
in this repo reproduces the checked-in pair (danielraffel/spectr#48), so
editor-behaviour fixes have to be applied to it by hand."* The single SDK
importer invocation in the whole repo is
`tools/validate_native_import_parity.mjs:76`, and it is a **validator**:

```
pulp import-design --from claude --file resources/editor.html \
  --mode baked --emit ir-json --materialized-canvas-composition --validate
```

It emits `ir-json` to compare against, never the shipping pair.

**The gap is in the Pulp SDK, not only in Spectr.** `--emit` accepts
`js, ir-json, cpp, swiftui, classnames`
(`tools/import-design/pulp_import_design.cpp:1787`). There is **no emit target
that produces a materialized runtime document**. So the materialized lane —
the lane Spectr ships — is import-only-once by construction: you can capture
into it, and you can validate against it, but you cannot regenerate it.

### What this costs, and why the last few days felt slow

Every UX change had to be expressed as a literal string patch in a
**5,642-line** hand-maintained script whose needles break whenever the
document drifts. That is the mechanism behind "this is not validating our
framework if it is this difficult to make small changes" — the difficulty is
real and structural, not a tooling-familiarity problem.

### The fix, at the right layer

1. **SDK (durable):** add a `--emit materialized-runtime` target so the pair is
   reproducible. Then re-import becomes possible, the patcher becomes the
   no-op its docstring predicts, and both directions exist.
2. **Spectr (interim, already true):** hand-edits to the committed runtime doc
   survive a build, so native-side iteration is safe today — it is just
   expensive. The patcher now fails loudly on a drifted needle rather than
   silently no-opping (`96058ef`), which removes the worst failure mode.

Until (1) lands, "change the design and re-import" is not a supported
workflow for Spectr, and any plan that assumes it is will stall.

### Release mechanics — corrected

An earlier note in this file implied #8094 and #8099 must merge in a
particular order. Measured correction:

- Version-at-Land runs on every push to main and **succeeded** on #8097's
  merge (`b1ef8b911`, run 34066531966) — but main's `VERSION` is still
  `0.835.0`. On this repo it opens a `release/version-bump` PR rather than
  pushing; #8099 was created 23:19:29, one minute after #8097 merged.
- So a bump follows *each* merge as its own PR. If #8099 lands first, #8094
  gets its own bump PR afterwards. Ordering changes **latency, not
  correctness** — worst case is two tags instead of one.
- Newest tag is `v0.835.0` and `git merge-base --is-ancestor b1ef8b911 v0.835.0`
  is false: **no released SDK carries either UX fix.** That, not ordering, is
  the PKG blocker.

## 2026-09-04 review-only PKG 1.0.9

- Built four Release payloads from exact Spectr head
  `35ee93cbc2a5460e2a71e39be948be9c3ba63b0a` against distribution-eligible
  Pulp SDK 0.829.0 (`cc75fa91cf6942a197b2fb00b38ac679de3cbcd1`).
- Review artifact: `artifacts/review-1.0.9/Spectr-1.0.9.pkg`, SHA-256
  `9a45ada469d475f1f64f32579788501333da196569f90d3aca4f09ab2429302b`.
  Notarization `8673de53-22be-45e6-9bdb-2efe3fbb6594` is accepted; staple,
  Gatekeeper, installer signature, and bundle relocatability checks pass.
- This is not final acceptance. Exact-current tests report: core 167/169 cases,
  native 22/24 cases (Settings body/ScrollView topology), and browser UX 6/7
  groups (analyzer live-hover status). The artifact is for structured human UX
  review while the generic Pulp imported-view lifecycle solution is reviewed.

## 2026-09-04 exact-head framework follow-up (not an SDK/release gate)

- Pulp successor `efdac7578df8557a04c83f254cd58c4ea17256f7` is clean and
  lineage-active. It adds a liveness re-check before retained-child lifecycle
  dispatch, rejects duplicate retained ScrollView aliases without disturbing
  the existing alias, and makes focus-restoration failure independent from
  overlay/popup restoration.
- The focused WidgetBridge suite remains green at 119 cases / 1,167
  assertions. Ultra review is being repinned to this exact SHA; SDK rebuild,
  Spectr reruns, and package work remain held until that verdict is explicit.

- Follow-up Pulp head `dad9f7e8efbd314db126d84b8a5e308d892723cc` additionally
  preserves interaction state across ordinary retained-widget reparenting and
  uses a bounded, allocation-free frame-clock walk that tolerates shifted
  siblings. The same WidgetBridge proof remains green (119 cases / 1,167
  assertions). Ultra review is required before SDK refresh.

- The exact-head Ultra review remains blocked on a deeper callback-lifetime
  contract: frame-clock callbacks may self-remove their executing view, and
  retained descendant lifecycle hooks are still root-only. The Pulp view suite
  also remains green (57 cases / 319 assertions), but this does not authorize
  an SDK refresh or Spectr package.

## 2026-09-04 Pulp successor progress (not an SDK/release gate)

- Pulp successor `0cc311d66` now preserves a retained ScrollView wrapper on
  repeated and ordinary reparent operations, rejects self/descendant cycles,
  attaches the wrapper before retained content, publishes aliases after the
  structural attach, and routes overflow/scroll-behavior/overscroll writes to
  the wrapper. The focused WidgetBridge suite is green (119 cases, 1158
  assertions).
- This does **not** authorize an SDK rebuild or Spectr package yet. Ultra still
  requires exception-atomic attach/rollback coverage and a complete policy for
  generic style/visibility/geometry/ARIA proxying before the Pulp head can be
  landed. Spectr remains parked on the official SDK with N1, Settings topology,
  and analyzer gates unresolved.

- Follow-up Pulp head `782a01397` adds a live `style_target` policy so retained
  scroll wrappers receive visibility, pointer-events, opacity, background,
  cursor, and transform updates; its regression suite is green at 119 cases /
  1162 assertions. SDK rebuild and Spectr native reruns remain held until the
  exception-atomic and lifecycle/focus restoration review clears.

- Pulp follow-up `2363beba7` extends that policy to flex, box-sizing, and
  position updates, with the retained-wrapper regression still green (30
  assertions in the targeted case). This is framework progress only; the
  rollback, focus/gesture, stale-pointer, and legacy-reparent gates remain
  open, so no SDK or PKG is authorized.

- Pulp `dec0500f5` adds transactional rollback in `View::add_child` when an
  `on_attached()` hook throws, with a two-assertion regression; the WidgetBridge
  suite remains green at 119 cases / 1162 assertions. This reduces—but does
  not yet eliminate—the wrapper-upgrade rollback gate, which still needs an
  end-to-end failure-injection test plus focus/gesture restoration proof.

- Pulp `e3fef1ab3` now restores retained-subtree focus and overlay claims after
  the internal detach/reattach. The WidgetBridge suite remains green at 119
  cases / 1162 assertions. Gesture-state and full upgrade rollback coverage
  remain under adversarial review; SDK/package work stays parked.

- Pulp `cbc14d487` adds `View::add_child_transactional`, preserving the caller's
  owned child when an attach hook throws, and applies it to the retained
  ScrollView upgrade rollback path. The WidgetBridge suite remains green at
  119 cases / 1162 assertions. Descendant frame-clock teardown, gesture-arbiter
  restoration, and stale-alias hardening are still open.

- Pulp `8c7c104e9` hardens attach rollback against reentrant child mutation and
  makes non-owning ScrollView aliases fail closed unless their wrapper remains
  live in the bridge identity set. WidgetBridge proof remains green at 119
  cases / 1162 assertions. Descendant frame-clock and gesture preservation are
  still under review; no SDK/package claim is made.

- Pulp `ba8b20179` re-runs the derived focus-gain hook when restoring a focused
  retained child, so TextEditor/InlineValueEditor caret state can recover in
  addition to the root focus slot. Targeted wrapper identity proof remains
  green (30 assertions); descendant lifecycle and gesture restoration are not
  yet accepted.

- Pulp `571c02552` fixes `InlineValueEditor::on_focus_changed` to call the base
  hook, preserving `has_focus()` during restoration alongside its edit commit /
  cancel behavior. WidgetBridge proof remains green at 119 cases / 1162
  assertions. The exact head is awaiting a fresh Ultra review after the latest
  interaction-reparent changes.

## 2026-09-03 browser mute-mode repair (latest)

- Commit `4e05f47` keeps the generated pointer-hover helpers in the live
  `FilterBank` closure and makes redraw-unmute policy read the live Settings
  value at draw-commit time. This removes the `liveHoverLabel`/
  `updatePointerHover` runtime errors that prevented draw publication.
- The complete browser UX matrix is green on this head: resize, popups,
  mute-modes, build-info-timeout, UX-polish, and preset-parity (6/6).
- This is source-level/browser proof only. Native exact-SDK dispatch,
  current-head screenshots, Logic host acceptance, and release packaging remain
  outstanding as recorded below.

## 2026-09-03 dispatch owner/target diagnosis (not a fix)

- Rebuilt the Pulp successor bridge target (122 assertions / 9 cases passed)
  and reran Spectr N1 against a freshly installed SDK from that worktree.
- Instrumentation proves `bindCanvasBehaviorAt` binds
  `browser:canvas:0 -> Browser_canvas_11` from `__behavior_pr_1` and
  `browser:canvas:1 -> Browser_canvas_22` from `__behavior_pr_2`, both relaying
  to `__behavior_pr_3`.
- The candidate runtime registration/owner-transparency experiment changed the
  native hit target between `__behavior_pr_3` and `Browser_canvas_22`, but the
  tap still produced `requests=[]`; it is therefore **not accepted**.
- The remaining defect is narrower than callback registration: the runtime's
  materialized canvas registry does not identify the anchored Browser canvas
  objects used by native binding, and React can restore the full-size wrapper's
  hit policy after the bind. No product change is claimed from this experiment;
  diagnostic edits were reverted.

## 2026-09-03 modulation settings update

- Settings now ships as one scroll surface with modulation inline; the legacy
  tab rail and tab state are removed from the emitted modulation component.
- Each LFO has an always-visible enable toggle. Main LFO Shape/Rate/Depth rows
  render only when `enabled` is true; LFO 2 Shape/Rate/Depth rows render only
  when `lfo2Enabled` is true, so disabling either source collapses its options.
- Both enable toggles publish their host parameters (`4000` and `4010`) and
  re-expand synchronously when turned back on. Emitted application scripts
  pass `node --check`; native/browser release gates remain blocked by the
  pre-existing N1, frozen-atlas, and analyzer failures recorded below.

Pulp successor follow-up `5b11945a3` broadens materialized owner discovery to
include click, wheel, mouse, pointer, and split-channel registrations. Its
focused WidgetBridge, animation, and removal-lifetime suites pass (122, 162,
and 429 assertions respectively). Spectr still requires an SDK rebuilt from
that exact commit before this can be treated as an end-to-end N1 repair.

Pulp follow-up `2de6d5efa20e4895530ac0a76b940eb02a2fe7e8` adds post-callback
lifetime checks to the pointer, wheel, and click relays. The focused bridge and
removal-lifetime suites still pass (122 and 429 assertions). This closes relay-
local teardown UAFs; outer dispatcher root destruction remains outside the
current contract and is not claimed as solved.

## 2026-09-03 exact-head SDK retest

The Release SDK install from Pulp `2de6d5efa20e4895530ac0a76b940eb02a2fe7e8`
completed successfully at `/tmp/pulp-sdk-2de6d5efa-v3`, and Spectr rebuilt its
native N1 target against that prefix. N1 remains **FAIL**: the wrapper wins the
native hit test and the center tap produces `requests=[]` despite the live
canvas being visible. Settings frozen-atlas remains **FAIL** (root-sized panel,
no body ScrollView), so no exact-head package or visual sign-off is authorized.

## 2026-09-03 restore verification (latest)

- **Not release-ready; no new PKG or PNG proof.** The only artifact remains
  `artifacts/Spectr-1.0.8.pkg` and is not evidence for this worktree.
- `native frozen state atlas interactions and persistence` is still red: the
  native tree contains a retained/root-sized Settings subtree, so the fixed
  520x679 panel/body topology assertions fail.
- `native N1 mounts live QuickJS widgets without an editor fallback` is red on
  the rebuilt SDK: the center canvas tap hits `__behavior_pr_1` but publishes
  no editor request (hit-tree shows the live canvas `pe=0`).
- `Spectr-browser-analyzer` is red: synthetic canvas hover still times out
  waiting for `BAND n/32` in the unified status banner.
- Passing focused evidence remains limited to build-info timeout, browser UX
  polish, and preset parity; these do not clear the native/input blockers.
- Pulp successor `fd6480394` is committed locally and its WidgetBridge suite
  passes (162 assertions / 28 cases); Spectr N1 still fails against that SDK,
  so this is not yet an end-to-end dispatch landing.
- A bounded trace confirmed callback aliases can be present for the live canvas
  (`__behavior_pr_1:pointerdown/move/up`), yet the native tap still publishes
  no request; the remaining defect is inside native event delivery/dispatch,
  not callback registration-map creation.

## 2026-09-03 coordinator recheck (current)

The three-gate recheck was run against `build-settings-pulpfix2` at the
current worktree state. All three remain red, so the UX burn-down is not ready
for a new package or visual sign-off:

- `native N1 mounts live QuickJS widgets without an editor fallback`: **FAIL**;
  the live canvas target is `__behavior_pr_1`, callback aliases exist for its
  pointer events, but the center tap publishes no request (`requests=[]`).
- `native frozen state atlas interactions and persistence`: **FAIL**; the
  retained Settings root is still `0,0,1320x860` instead of the authored
  `400,90.5,520x679` panel, and the body-only `ScrollView` is not discoverable.
- `Spectr-browser-analyzer`: **FAIL**; synthetic pointer motion still times
  out waiting for `BAND n/32` in the unified status banner.

These are implementation/proof blockers, not human-installation issues. The
existing `artifacts/Spectr-1.0.8.pkg` remains old and is not evidence for this
state. Do not produce or request install testing until the native dispatch,
Settings topology, and analyzer gates pass and exact-head PNGs are regenerated.

### Dispatch repair landed in the active Pulp successor

Pulp successor commit `d6c123307` makes the anchored live CanvasWidget the
visible, hit-testable owner (and hides the source command canvas), while
explicitly keeping the behavior owner `PointerEvents::auto`. Its focused
WidgetBridge sole-owner test passes. Spectr still needs an SDK rebuild against
that exact commit and a passing N1 end-to-end request assertion before CUR,
DDM, and the native overlay rows can advance.

The first SDK-linked Spectr rerun against the d6c123307 library archives still
fails N1 (`requests=[]`); the live target is now opacity `0.0` in the native hit
tree. That is concrete evidence that the Pulp ownership handoff is not yet
correctly wired for Spectr's anchored target. The successor audit further found
that the target's native pointer callback is cleared while Spectr only aliases
the React callback map; the generic callback-registration seam still needs an
end-to-end root-click proof. The commit is preserved, but it is not yet an
end-to-end fix and must not be treated as a green dispatch landing.

## 2026-09-03 latest verification (current worktree)

The managed-Chrome UX-polish oracle now passes, including Settings body
overflow behavior at normal and tall host sizes. Native focused checks also
pass for N1 mounting, Settings command/cursor routing, and Settings
Escape/outside dismissal. The remaining native parity failure is structural:
the frozen state atlas predates the authored `data-spectr-settings-body`
wrapper, so `APPEARANCE` is not present in the native tree. No package is
release-ready until that atlas is regenerated and the body-only ScrollView /
fixed-header-tab proof passes.

The owning Pulp dispatch seam was repaired in the preserved dispatch worktree
(`8a183432c` / SDK copy `4fce448a`): `bindCanvasBehaviorAt` no longer hides the
anchored live CanvasWidget or disables its hit testing. Its focused Pulp test
passes (31 assertions), and Spectr's rebuilt N1, Settings-command, and
Escape/outside native checks pass against the rebuilt SDK. The frozen-atlas
topology check and browser analyzer oracle remain the only red gates in this
slice.

The analyzer oracle is still red on the current head: synthetic pointer motion
does not produce the expected `BAND n/32` status text. This is an unresolved
hover event-path defect, not a headless unpacking failure. The standalone
Release review build succeeds, and `Spectr-browser-ux-polish` remains green;
those results do not clear the analyzer or frozen-atlas gates.

## 2026-09-03 current-head verification (official SDK 0.829.0)

The current branch is `ef62a9b` (clean apart from the pre-existing untracked
`artifacts/` directory). `Spectr-native-n1-test` was rebuilt against the
immutable SDK source SHA `cc75fa91cf6942a197b2fb00b38ac679de3cbcd1` after the
Settings portal-parent recovery fix. The focused native sweep is **not green**:

- Passed: semantic popup navigation, selected-tab hover, host automation and
  the non-UX native bridge cases.
- Failed: live N1 mount (authored `spectral_label` absent), Settings command and
  Escape/outside dismissal (materialized state/overlay unavailable), Flare
  mixed-sign oracle (positive-band contract), frozen state-atlas Settings
  interaction (authored title absent), and whole-bound button hit testing
  (overflow control absent).

The targeted portal-parent recovery and native-lifecycle restoration (`4ba0eb0`,
`31f1139`, `6d59463`)
did not clear the failures, so Settings,
dropdown native inheritance, cursor native proof, and package delivery remain
blocked on the materialized-runtime/official-SDK lifecycle. The browser source
adapter now tolerates the already-applied hover patch (`d9abc22`), addressing
the previous unpacker stop. The CTest wrappers still do not produce an accepted
passing oracle on this macOS headless host: Chrome emits
`CVDisplayLinkCreateWithCGDisplay` setup errors and popup/resize results remain
unaccepted. After making the source adapter tolerate the already-applied hover
patch, the popup harness now reaches the application but times out opening the
Bands menu; that is a genuine current harness/runtime failure, not a pass.
This remains unverified, not a feature pass.

The current official-SDK rerun now passes `every native dropdown dismisses by
Escape and outside press` and `remaining native modal panels share Escape and
outside dismissal`. Settings remains the exception: its live panel is still
unclaimable after the portal transition (`active_overlay == nullptr`).
The current exact-head rerun also reproduces the Settings command materialized
state mismatch and the missing live overlay (`active_overlay == nullptr`).
Commit `ef62a9b` adds stale-widget detection and subtree rematerialization;
it fixes first-open overlay acquisition in diagnostics, but strict close/reopen
and state-replay checks remain red (the reopened native `SETTINGS` title is
missing).
The browser popup harness still times out waiting for the Bands menu, the UX
polish harness reports that the shipping root did not mount, and the preset
parity harness times out waiting for the React mount. These are not accepted
proof for those surfaces.
The checked-in Settings screenshot is non-black and structurally complete, but
it is artifact evidence only, not proof of live interaction.

### Current visual-proof rule

Every new visual surface must have a PNG rendered from the exact current head
and an accepted interaction oracle. The checked-in `states/settings.png` is an
older capture and does not show the newer General/Modulation tabs; it is not
current-head proof. No Settings, cursor, typography, preset, overlay, or
package row is complete until its replacement PNG and matching oracle are
recorded.

This is the durable landing ledger for the final Spectr native-editor UX,
automation, host-acceptance, and package work. A checked box requires committed
implementation plus the named evidence; a local visual impression is not
completion evidence.

## Status vocabulary

Do not report checkbox ratios as implementation progress. Report every item
using four independent fields:

- **Implementation** — whether the shipping code exists.
- **Automated proof** — exactly what passed, and whether an official-SDK rerun
  is still required.
- **Human confirmation** — visual, interaction, audible, or real-host sign-off.
- **Overall** — `Done` only when all required fields are complete; otherwise
  `Waiting automated proof`, `Waiting human confirmation`, or `Open`.

Most visual and interaction rows require Daniel to confirm the new packaged
build. A browser test or older development-SDK run cannot silently substitute
for that confirmation.

## Shareable item-by-item status

| ID | Item from the UX burn-down | Implementation | Automated proof | Human confirmation | Overall |
| --- | --- | --- | --- | --- | --- |
| CUR-1 | Crosshair over band-editing canvas | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native settings command and minimap cursors reach the shipping runtime` PASSES, plus browser `Spectr-browser-analyzer` PASSES. The recorded blocker ('official-SDK rerun pending') is stale | Pending in new PKG and hosts | Waiting human/host confirmation only |
| CUR-2 | Open hand over movable viewport | Done (`grab`) | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native settings command and minimap cursors reach the shipping runtime` PASSES, plus browser `Spectr-browser-analyzer` PASSES. The recorded blocker ('official-SDK rerun pending') is stale | Pending in new PKG and hosts | Waiting human/host confirmation only |
| CUR-3 | Closed/grabbing hand while moving viewport | Done (`grabbing`) | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native settings command and minimap cursors reach the shipping runtime` PASSES, plus browser `Spectr-browser-analyzer` PASSES. The recorded blocker ('official-SDK rerun pending') is stale | Pending in new PKG and hosts | Waiting human/host confirmation only |
| CUR-4 | Left/right resize cursor over viewport trims | Done (horizontal resize) | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native settings command and minimap cursors reach the shipping runtime` PASSES, plus browser `Spectr-browser-analyzer` PASSES. The recorded blocker ('official-SDK rerun pending') is stale | Pending in new PKG and hosts | Waiting human/host confirmation only |
| PRE-1 | Preset Manager matches original source HTML | Mostly implemented | STILL BLOCKED ON A DECISION, not on tooling. The A/B comparison never runs because CMake passes `--shipping-only` (test_preset_parity_browser.mjs:12), so a CANONICAL document must be named; and even enabled the script asserts nothing (`No parity threshold is asserted; human review of the A/B sheet remains required`). Pillow 12.2.0 is installed, so this is purely a call on canonical-document + threshold | Pending in new PKG | BLOCKED on Daniel's threshold decision |
| PRE-2 | Selected-preset detail layout and action overlap | Implemented for factory and saved-user states | Registered real-Chromium matrix passes all 8 factory + saved-user selection and rename/edit states. CORRECTION: the planted overlap control was never wired — `--plant-overlap` is the 5th positional argument and the CMake registration supplied three, so it had never run. Now registered as `Spectr-browser-preset-parity-negative-control` (WILL_FAIL) and verified via `ctest -V` to fail on the intended message | Pending in new PKG | Waiting human confirmation |
| PRE-3 | Long names truncate before Snapshot controls | Done | CORRECTION: there was NO truncation oracle — the only scrollWidth/clientWidth check in the repo asserts the opposite condition for SET-5, and the parity script's 20 assertions contained none about truncation, so "matrix passed" did not cover this row. Now covered: the check forces overflow (every shipped name fits, so an assertion on the default fixtures would be vacuous), leads with a positive control that reds if no overflow occurred, and has its own WILL_FAIL control (`--plant-untruncated` -> "spills 308px past the detail panel"). A geometric "title runs under the actions" model was tried first and rejected: measured, title y=211..235 vs actions y=514..566 | Pending in new PKG | Waiting human confirmation |
| PRE-4 | Icons/text vertically centered for every selection | Implemented | Registered real-Chromium matrix passed every factory + saved-user selection and rename/edit state alignment | Pending in new PKG | Waiting human confirmation |
| PRE-5 | Selected preset name and SVG both update | Done | Exact-head real-Chromium matrix passed; official-SDK native frozen-state parity now passes 886 assertions after commit `11731c6` (native title identity is reconciled from selected pattern ID) | Pending in new PKG | Waiting human confirmation |
| PRE-6 | Flare correctly handles negative, positive, and zero bands | Current sign-preserving behavior implemented | Unit and native oracles pass current contract | Required: decide whether stopping at 0 dB is desired | Open product decision |
| DDM-1 | Escape closes every dropdown and modal | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `every native dropdown dismisses by Escape and outside press` + `remaining native modal panels share Escape and outside dismissal` + browser `Spectr-browser-popups` all PASS | Pending in new PKG and hosts | Waiting human/host confirmation only |
| DDM-2 | Outside click closes and consumes without mute/draw | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `every native dropdown dismisses by Escape and outside press` PASSES; browser `Spectr-browser-popups` PASSES with its no-underlying-mutation control | Pending in new PKG and hosts | Waiting human/host confirmation only |
| DDM-3 | Up/down changes highlighted item | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native semantic popup navigation owns one visible highlight and selection` PASSES; browser `Spectr-browser-popups` PASSES | Pending in new PKG and hosts | Waiting human/host confirmation only |
| DDM-4 | Return selects and closes | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native semantic popup navigation owns one visible highlight and selection` PASSES; browser `Spectr-browser-popups` PASSES | Pending in new PKG and hosts | Waiting human/host confirmation only |
| DDM-5 | Hover feedback differs from selection | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native selected tabs inherit hover through their label ancestry` PASSES. GAP UNCHANGED: both oracles prove hover does not CHANGE selection, not that hover is visually DISTINCT from it | Pending in new PKG and hosts | Waiting human confirmation (distinctness unoracled) |
| OVL-1 | Status text vertically centered | Done | BROWSER-ONLY PROOF (no native/Skia coverage; the SDK build is not exercised by this suite): `Spectr-browser-analyzer` PASSES (it had been red, blocking every assertion after the drag check) | Pending in new PKG | Waiting human confirmation only |
| OVL-2 | Status overlay below graph top ruler | Done | BROWSER-ONLY PROOF (no native/Skia coverage; the SDK build is not exercised by this suite): `Spectr-browser-analyzer` PASSES | Pending in new PKG | Waiting human confirmation only |
| OVL-3 | Status updates immediately while dragging | FIXED (was broken) | CORRECTION: coverage existed and was FAILING — `Spectr-browser-analyzer` red on "live hover gain status did not follow the drag". Root cause was not the status logic: the patch defining `updateLiveHoverStatus` had silently stopped applying because an earlier patch began inserting the cursor state between the two lines its needle expected adjacent, and `replaceSpectrSource` carried a hardcoded exemption for that one label. Both call sites then resolved to the module-level `(() => {})` stub, so the banner kept its pre-gesture text (probed mid-drag: target=0.319 rendered=0.261 react=0, banner "0.0 dB"). Needle repaired and the exemption removed so a missing needle now throws. Analyzer exits 0; restoring the old needle+exemption reproduces the failure verbatim | Pending in new PKG | Waiting human confirmation |
| OVL-4 | Latest status remains visible longer | Done (2.2 s normal; 2.8 s mute/unmute) | BROWSER-ONLY PROOF (no native/Skia coverage; the SDK build is not exercised by this suite): `Spectr-browser-analyzer` PASSES; `Spectr-browser-ux-polish` asserts the authored 2200/2800 ms schedule with its own planted control. NOTE: ux-polish is FLAKY (3 pass / 1 fail over four serial runs) and that flake is worth fixing on its own | Pending in new PKG | Waiting human confirmation only |
| OVL-5 | Status disappears without an empty box | Done | BROWSER-ONLY PROOF (no native/Skia coverage; the SDK build is not exercised by this suite): `Spectr-browser-analyzer` PASSES | Pending in new PKG | Waiting human confirmation only |
| SET-1 | Fixed header with content scrolling beneath | Done | BLOCKED, and the blocker CHANGED on official SDK v0.834.0 (source 688a709b). Before: `REQUIRE(scroll_view != nullptr)` failed at test_native_state_parity.cpp:925 — the retained ScrollView did not exist. Now it exists (Pulp lifecycle contract f9bb36025 shipped in v0.834.0) and the test reaches :933, where the body viewport measures 50.0px against an authored 529.184px; the frozen-atlas case agrees (settings_body and feedback are the SAME 466x50 rect at :2301). The topology is fixed; the sizing is not | Pending in new PKG | BLOCKED on a 50px body viewport (newly reachable, not newly caused) |
| SET-2 | Fixed close button with hover/press feedback | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): covered at test_native_state_parity.cpp:2348-2354 via data-spectr-close-state hover/pressed assertions, inside the frozen-atlas case which currently reds LATER at :2301 on Settings geometry — the close-state assertions themselves execute and pass | Pending in new PKG | Blocked by SET-1 geometry (same test case) |
| SET-3 | Escape/outside click closes Settings | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native settings modal dismisses by Escape and outside press` PASSES (historically the flakiest native row) | Pending in new PKG and hosts | Waiting human confirmation only |
| SET-4 | Copy is centered and retains Copied feedback | Implemented | BROWSER-ONLY PROOF (no native/Skia coverage; the SDK build is not exercised by this suite): `Spectr-browser-ux-polish` covers COPY->COPYING->COPIED centering at every state; suite is FLAKY (3/4 serial) rather than failing | Pending in new PKG | Waiting human confirmation; flaky oracle to fix |
| SET-5 | Status Info description is not truncated | Implemented | BROWSER-ONLY PROOF (no native/Skia coverage; the SDK build is not exercised by this suite): `Spectr-browser-ux-polish` asserts scrollWidth>clientWidth plus four-sided containment; suite FLAKY (3/4 serial) | Pending in new PKG | Waiting human confirmation; flaky oracle to fix |
| SET-6 | No unnecessary scrollbar when content fits | Implemented | BLOCKED by the same defect as SET-1 on official SDK v0.834.0 (source 688a709b): the browser half (`Spectr-browser-ux-polish`, overflow at 860 vs fit at 1800) exercises DOM overflow and is flaky-not-failing, but the NATIVE scroll-track behaviour rides on the same ScrollView that is currently 50px tall | Pending in new PKG | BLOCKED with SET-1 |
| SET-7 | Loading build info cannot remain stuck | Done | BROWSER-ONLY PROOF (no native/Skia coverage; the SDK build is not exercised by this suite): `Spectr-browser-build-info-timeout` PASSES with its planted no-timeout control | Pending in new PKG | Waiting human confirmation only |
| AUT-1 | Recorded automation/playback is sample-accurate | Processor playback implemented | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `Spectr renders scheduled band automation without control-worker latency`, `scheduled processor playback changes gain on the exact sample across partitions`, and `Spectr output automation is smoothed and block-partition invariant` all PASS | Logic test required | Waiting human/host confirmation only |
| AUT-2 | Bands and viewport animate during host playback | Done | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `native host automation projects through the compact live frame lane` PASSES; `"#37: host mode automation advances the editor projection"` PASSES | Pending in Logic with new PKG | Waiting human/host confirmation only |
| AUT-3 | Logic automation lanes are visible and behave correctly | Parameter surface implemented | NO AUTOMATED ORACLE IS POSSIBLE. Whether Logic renders automation lanes is not observable headlessly; auval/clap-validator prove scan+load, not lane rendering. Needs a real host and therefore a built PKG. `src/param_surface.cpp` proves the lanes are DECLARED, which is necessary but not sufficient | Pending in Logic with new PKG | BLOCKED: needs a real Logic host (decision: accept as human-confirmation-only?) |
| MOD-1 | Internal modulation/LFO Settings UI | Done, and a REAL BUG found and fixed: the single-scroll release hid the Settings tab RAIL with display:none, but that container also held the MODULATION group, so every LFO and target control was mounted and UNREACHABLE | CLOSED. The static-regex assertions passed on the hidden markup -- they read emitted source text and cannot see a zero-height box behind display:none. Replaced with a DRIVEN oracle in `Spectr-browser-ux-polish` (mode 'modulation'): walks the ancestor chain for display:none, requires a rendered box, clicks the real target/ALL/NONE buttons and asserts the `modulation_targets_set` bridge payload delta plus the control's own restyle. Planted negative severs the onClick and requires a timeout, asserted on the exact message. Whole browser lane green. CORRECTION: this is a BROWSER-ONLY oracle -- it drives the DOM in Chromium and never builds or runs the SDK, so it cannot prove the native Skia surface Daniel asked to see. The native capture SKIPs on the shipping path and only renders through the diagnostic `-UNWEDGED` route until #8094 lands in an SDK | Pending visual/audible check in new PKG | Automated proof BROWSER-ONLY; native proof blocked on #8094 |
| MOD-2 | Whole-bank, Snapshot A/B, and Morph targets | Implemented; independent Bank/A/B/Morph toggles now preserve a mixed target mask, with ALL/NONE shortcuts | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): `internal modulation targets snapshots and host morph independently`, `internal modulation target mask composes selected destinations`, and `modulation target bridge accepts all and none target sets` all PASS | Pending audible check in new PKG | Waiting human confirmation only |
| MOD-3 | Host and internal modulation coexist | Implemented | CORRECTION: this row had NO oracle. Now covered by `Spectr keeps host band automation and internal modulation both audible` `[modulation][coexistence]`, which measures BOTH directions (internal modulation still audible while the host drives every band; the host's authored dB still audible while modulation runs) against a positive control. Both directions confirmed by planted negatives with the recompile observed: disabling modulation_settings.enabled reds one assertion, clearing host_field only while modulation is enabled reds the other. NOT covered: the snapshot A/B morph host axis, which needs the editor bridge rather than the parameter surface | Pending in new PKG and hosts | Waiting human confirmation |

### Additional correctness and delivery gates

| ID | Item | Implementation | Automated proof | Human confirmation | Overall |
| --- | --- | --- | --- | --- | --- |
| COR-1 | Right-side dBFS scale is semantically correct | Review pending | Pending | Pending if behavior changes | Open |
| COR-2 | Minimap edge drag cannot move opposite trim | Current interaction exists; endpoint clamp path audited | RERUN DONE on official SDK v0.834.0 (source 688a709b, contains the Pulp lifecycle contract f9bb36025): the minimap endpoint-invariant assertions inside the `[cursor]` case PASS | Pending in new PKG | Waiting human confirmation only |
| COR-3 | Fast band drawing and minimap interaction remain intact | Current interaction exists; pointer-owned hot path audited | BROWSER-ONLY PROOF (no native/Skia coverage; the SDK build is not exercised by this suite): `Spectr-browser-analyzer` PASSES including the React-budget drag assertions | Pending in new PKG | Waiting human confirmation only |
| DEL-1 | Exact-head architectural/adversarial review | Not applicable | Review receipt pending | Not applicable | Open |
| DEL-2 | Spectr PR merged with required checks green | Pulp modulation PR #8028 is merged at `d4ab59c883a99b23d456e56ef9b6b6a0eebcb3ba`; Spectr branch contains current UX fixes but is not yet landed | Pulp merge is complete; exact Spectr head still requires native/release proof before landing | Not applicable | Open — Spectr landing still required |
| DEL-3 | Logic AUv2 and REAPER VST3/CLAP acceptance | Formats implemented and the notarized PKG is installed into the system plugin locations | Installed AU `auval -v aufx Spec Pulp` passes; installed system CLAP validation passes 21 tests (16 passed, 0 failed, 5 skipped); REAPER smoke unit harness passes 39 tests. Real REAPER AU editor-open smoke was `INCONCLUSIVE` because the host scan cache did not publish the target (documented harness limitation); Logic automation and REAPER editor/automation receipts remain pending | Pending Logic/REAPER interaction and automation confirmation | Open |
| DEL-4 | Signed, notarized, installed, launch-verified M5 PKG | Exact-current-head (`1a6e3a7c87c23afc3f6ecc5d230a2f4b1b7df69c`) four-payload Release build completed against official Pulp v0.829.0 SDK (`cc75fa91cf6942a197b2fb00b38ac679de3cbcd1`) | `artifacts/Spectr-1.0.5.pkg` SHA-256 `45e1a95b4e1a0d56194fc411abb25b89a0f7f49061e1f123ea5edcbc08eb3c56`; Apple notarization submission `975db2fd-7719-4fe3-b13d-17f3ac319497` accepted; staple and `spctl` passed; fresh AU/CLAP/native receipts recorded below | Standalone launch and Logic/REAPER host confirmation pending | Waiting human/host confirmation |

## Protected source state

- The primary `/Users/danielraffel/Code/pulp` checkout is unrelated recovery
  state and is outside this workstream.
- Pulp typed dispatch PR #8012 merged its exact proven head
  `2c4d98c076e04cbcfd72fb538fb505a4dfd60972` to protected main as
  `aadb837854df498fd368e9fe28e55420b991241f`. The merge contains the dispatch
  API, implementation, focused repaint/rAF test, and Vellum watch event. The
  clean successor checkout is `pulp-8012-merged-sdk-20260902`; the prior local
  refresh worktree is superseded and remains unpushed. PR #8012 is merged, not
  waiting in the merge queue.
- Official SDK publication is now resolved by Pulp release `v0.829.0`: the
  Darwin ARM64 asset digest is
  `42294da6937280df758ed53c77c046aa68e5d99b71d0b97e3e3438b68fd78117`, with
  distribution-eligible source SHA
  `cc75fa91cf6942a197b2fb00b38ac679de3cbcd1`. Its SDK contains the merged
  #8012 `WidgetBridge::dispatch_native_message` API and is the immutable SDK
  used for the package proof below.
- Spectr integration worktree: `spectr-ux-burndown-20260901`; the integrated
  implementation head proven below is
  `8650a4d19703a2709d88f8510806397ea1be6c5c` (individual modulation target
  masks included).
- The preserved Pulp dropdown/modal worktree remains at
  `d6f5c37b1ea5973a7790721b976360b2b51f0b12`. Its six commits are already
  patch-equivalent on protected `origin/main` `421a5ee07b18429b6b252f0901b6f76b2936c3fe`;
  the cursor and React dismissal equivalents are `e5dc8d29c7` and
  `d3a4ce574a`, respectively, so they must not be republished.
- Spectr realtime modulation proof:
  `73c359b30735e725d5fa2da3e6071fba2e6d1be5`.

## Ordered landing ledger

### 1. Automation playback performance

- [x] Pulp exposes typed per-instance native message dispatch without generated
  JavaScript or forced animation-frame flushing.
- [x] The Pulp dispatch test proves typed payload delivery, deferred rAF, and
  exactly one repaint request; its redundant-repaint negative control fails.
- [x] Spectr uses typed dispatch for the compact host-automation projection and
  has a focused test that drives the real C++ frame-clock path.
- [x] An exact-Pulp-SHA Release Spectr build passes the native host-automation
  test and focused native suite.
- [x] The live AppKit to QuickJS to Skia/Graphite Perfetto gate passes for band,
  minimap, and automation workloads, with exact Spectr and Pulp SHA receipts.
- [x] Snapshot and typed dispatch costs, repaint counts, and frame-tail budgets
  have been inspected rather than inferred from the aggregate gate.
- [x] Scheduled audio automation remains sample-accurate and block-partition
  safe under the focused realtime tests.
- [ ] Logic shows the expected automation lanes and smooth band/viewport replay.

Clean-head evidence: the complete three-workload gate passed. The automation
receipt recorded 0.053 ms projection p95 and 1.46/2.07 ms frame p95/p99;
snapshot construction and typed dispatch were independently inspected, and
the rendered automation screenshot remained byte-identical after deferring the
redundant synchronous draw. The bands and minimap receipts also passed their
input, layout/paint-count, and frame-tail budgets. Any later branch-head change
requires regenerating the SHA-bound receipts.

### 2. Cursor feedback

- [x] Crosshair over band editing.
- [x] Open hand over the movable viewport.
- [x] Grabbing hand during viewport drag.
- [x] Left/right resize cursor over viewport trims.
- [ ] Standalone, AUv2, and REAPER acceptance recorded.

Clean-head evidence (2026-09-11), two layers that together cover the chain
from the runtime's `style.cursor` write to the `NSCursor` AppKit applies:

- **Spectr layer, exact clean head.** `Spectr-native-n1-test '[cursor]'`
  passed 54 assertions in 1 case, built Release from clean Spectr
  `e74bac75e218ea77fc80ebe7c10de91a41ee6b6a` (`SPECTR_SOURCE_GIT_DIRTY=FALSE`,
  recorded in the configure cache and embedded in the binary) against the
  official immutable Pulp SDK `v0.843.0` (`source_git_sha 95405ca6…`, ref
  `v0.843.0`, not dirty, `distribution_eligible`; it contains the AppKit
  gesture-phase cursor refresh `e5dc8d29c7`). Spectr `origin/main` is one
  commit behind that head (`30d9384`). The case drives the materialized runtime
  through `[data-spectr-filter-surface]` and asserts `surface->cursor()` in the
  order crosshair → horizontal-resize (left trim, right trim, on pointermove
  with no gesture in progress) → grabbing (pointerdown on the window) →
  grabbing (pointermove while held) → grab (pointerup) → crosshair (pointermove
  off the minimap). The runtime's `onPointerMove` chooses `col-resize` /
  `grab` / `grabbing` from `minimapHit` and the active gesture mode only — it
  never reads `e.buttons` — so the trim rows are hover-driven even though the
  test's `dispatch_minimap` sends `buttons: 1` on its pointermoves (a cosmetic
  inaccuracy in the fixture, not in the product).
- **Pulp host layer, screen readback.** Pulp PR #8230
  (`fix/css-empty-background-clears-20260911`, auto-merge armed) adds
  `test/test_mac_hover_cursor_live.mm` (ctest `pulp-test-mac-hover-cursor-live`,
  `validation`-labelled, `RUN_SERIAL`). Its `FilterRoot` fixture is the shape of
  the Spectr surface — ONE view whose hover handler picks crosshair (plot) /
  open hand (viewport window) / left-right resize (trims) and whose press
  handler closes the hand — hosted in a real `NSWindow` by the shipping
  `PulpView`, driven through the host's real `-mouseMoved:` / `-mouseDown:` /
  `-mouseDragged:` (+ coalesced flush) / `-mouseUp:`, and read back as the
  applied `+[NSCursor currentCursor]` with the view's own button-event count
  beside each reading. It asserts `crosshairCursor`, `openHandCursor`,
  `resizeLeftRightCursor` on pure hover with 0 button events; `closedHandCursor`
  only while a button is held (press and drag); `openHandCursor` back on release
  without pointer motion; crosshair again on the next hover. Every assertion was
  proven RED→GREEN with `tools/scripts/confirm_failure.sh` (observed recompile
  each time): crosshair→arrow, resizeLeftRight→arrow, grab→arrow and
  closedHand→openHand in `set_ns_cursor_for_style`; `simulate_hover` removed
  from `-mouseMoved:` (the cursor would then update only after a press — the
  reported bug); and the post-handler publish removed from `-mouseDown:`.

Still open, and who can close it:

- **Standalone acceptance** — the mechanism is proven above, but a
  person-visible standalone reading has not been taken. Instrument:
  `tools/testing/cursor-proof/hover_cursor_probe.m` (PR #8230; oracle
  `+[NSCursor currentSystemCursor]` read from outside the process), run as
  `hover_cursor_probe --pid $(pgrep -x Spectr) --cols 12 --rows 8 --compare-drag`
  against an installed clean-head standalone. Not run here: it dissociates the
  system pointer and this machine was carrying a concurrent perf measurement,
  and the standalone was not built in this session (only the test target was).
  Closable by any agent or human on an idle, Accessibility-trusted Mac.
- **AUv2 (Logic) and REAPER acceptance** — not run; needs a real host session.
  The Pulp live test's header states the DAW-hosted case is not exercised at
  all (a different window and tracking-area situation). Human validation, or an
  agent with Logic/REAPER on an idle machine, following the same probe recipe
  with the host's pid.
- **The in-app `SPECTR_CURSOR_PROBE` hover branch is dead.** `native_editor.cpp`
  guards it on `SPECTR_HAS_HOVER_DISPATCH`, which nothing defines, and the
  `deliver_hover_and_resolve_cursor` it would call exists in neither SDK
  `v0.843.0` nor current Pulp `main` (only `hover_cursor_at`). Every hover probe
  therefore reports `<no-hit>`, so `tools/cursor_invariants.py --expect` can
  never pass for a hover point; only the `x,y>x2,y2` drag branch measures
  anything. Closable by a Spectr change that either lands the Pulp hover
  dispatch helper and defines the macro, or rewires the hover branch to
  `simulate_hover` + `hover_cursor_at`.
- One measured-not-judged Pulp host property is recorded in the live test's
  header: AppKit's cursor pass (`-cursorUpdate:`) resolves from the hit view's
  cursor slot without delivering a hover sample, so on a surface like this a
  pass that runs where the pointer has not yet moved shows the slot's previous
  value until the next `-mouseMoved:` lands.

### 3. Dropdown and modal defaults

- [x] Escape closes every dropdown and modal.
- [x] Outside click closes and consumes the click, with no mutation behind the
  popup.
- [x] Up/down changes one visible highlight; Return selects and closes.
- [x] Hover feedback is distinct from selection.
- [ ] Pulp framework tests and inherited Spectr plugin-format behavior pass.

Exact-head browser evidence: `Spectr-browser-popups` passed in real Chromium at
`8650a4d19703a2709d88f8510806397ea1be6c5c`. It exercised all five footer
dropdowns plus Help, Settings, save, Pattern Manager, and band-context popups.
The outside activation sequence was consumed with the underlying editor and
processing state unchanged; a planted click-through control failed before the
fix. This does not replace the clean merged-SDK native and plugin-format pass.

### 4. Preset parity

- [x] Preset Manager matches the source layout without overlapping actions.
- [x] Long names truncate before Snapshot controls.
- [x] Selection updates both name and SVG with centered text/icons.
- [x] Flare preserves positive, negative, and exact-zero bands under the current
  sign-preserving scaling contract.
- [ ] Product sign-off decides whether Flare should remain sign-preserving or
  cross 0 dB during compression; the current implementation intentionally
  approaches zero without crossing it.

Exact-head evidence: the full real-Chromium editor matrix passed at
`8650a4d19703a2709d88f8510806397ea1be6c5c`, and the SDK-independent integrated
Flare oracle preserved negative, positive, and exact-zero band behavior. That
proves the current math, not that the sign-preserving interaction is the desired
product behavior.

### 5. Unified status overlay

- [x] Text is vertically centered below the graph's top ruler.
- [x] Drag feedback updates immediately and retains only the latest message.
- [x] The overlay dismisses after inactivity without an empty intermediate box.

Evidence: `tools/status_overlay_invariants.py`, registered as the
`Spectr-status-overlay` ctest, drives the shipping standalone and reads the
overlay's real geometry and lifetime rather than a source string. Place is a
pixel property and dismissal a time property, so both are gated separately;
the runner refuses a verdict unless its drag and probe drivers left liveness
markers, because an overlay that was never raised is pixel-identical to one
that dismissed correctly. Exit 3 (premise unproven) and 77 (no GPU editor)
never read as a pass. Passed at `26a16cc` in 13.45 s.

### 6. Settings

- [x] Modulation layout is stable, with fixed General/Modulation tabs and two LFO surfaces.
- [x] Header, close control, and settings tabs remain fixed while content scrolls.
- [x] Close hover/press, Escape, and outside-click behavior pass.
- [ ] Copy is centered and preserves Copied feedback.
- [ ] Status Info is not truncated and unnecessary scrollbars are absent.

Close/Escape/outside-click evidence: `test/test_native_state_parity.cpp:2006`
asserts Escape and outside-click dismissal against the shipping runtime, with
a negative control at `:2019`; `:2716-2725` drives `pointerenter` and
`pointerdown` on the close control and asserts the `data-spectr-close-state`
it publishes. That proves the behavior. The hover and press *styling* is
authored but no test reads its painted pixels, so a silent appearance
regression there would not be caught.

Copy stays open, and the remaining half is a core Pulp defect rather than a
Spectr one. The width half is fixed and proven: the button spanned the full
448-pixel panel and now measures 136 with a 114-wide label box inside it
(`docs/evidence/2026-09-11/COPY-WIDTH-{RED,GREEN}*`, captured from the
shipping materialized artifact, detector negative-controlled with `--plant`).
Centering is not fixed. A Label carrying text and no element children is
built as a Yoga leaf, so the text measure function lands on the Label's own
node and no anonymous flex item exists for `justify-content` to distribute;
the authored `justifyContent: center` provably does nothing, measured ink
centre 33.5 against an expected 136. Two positive controls confirm the
diagnosis rather than the measurement: `textAlign` reads 135.5, and wrapping
the string in a child Label also reads 135.5. `compat.json` lists
`css/justifyContent` as supported with `center` and no caveat, so this is an
unimplemented behavior, not a documented ceiling. A core Pulp fix is in
flight; this line closes when the fix lands and the ink centre measures 136.

Note on the contract markers: `test/test_import_fidelity.cpp:828` asserts the
`settings-centered-copy-feedback` string survives in the shipping artifact and
detects its removal. That gate passes today while the defect above persists,
because it proves the CSS is authored, not that it centers anything. Do not
read it as coverage for this line.

Status Info stays open because the native lane cannot see it:
`test/appearance_detectors.hpp:63-71,130-137` drops clipped boxes, which is
exactly the geometry a truncation or stray-scrollbar defect produces, and
`test/fixtures/settings-open-layout.json` is loaded by no test. Closing this
needs either a pixel check that can see a clipped box or an explicit
browser-only, human-verified line.
- [x] Loading build info resolves promptly and cannot remain stuck.

Exact-head browser evidence: the real-Chromium build-info harness reproduced
indefinite loading with its planted no-timeout control, then proved the shipping
component transitions an unresolved request to `BUILD INFO UNAVAILABLE` after
 the 1.5-second bound at `8650a4d19703a2709d88f8510806397ea1be6c5c`.

### 7. Internal modulation

- [x] Realtime audio ownership is proved for Bank, Snapshot A, Snapshot B, and
  Morph by commit `73c359b30735e725d5fa2da3e6071fba2e6d1be5`.
- [ ] LFO controls have a stable finished layout.
- [ ] All four targets compose with host automation and third-party modulation.
- [ ] Audible behavior and automation replay pass product acceptance.

Visual modulation is separately proven and is not what these three lines ask
for. With LFO 1 and 2 enabled, the assigned controls were observed animating
in the shipping editor under a full set of controls, which settles the
reported concern that an assigned LFO produced no visible motion. The three
lines above are layout stability, host-automation composition, and product
acceptance, and none of them follows from that observation.

Instrumentation trap worth recording: `SPECTR_STATE_TRACE` and
`SPECTR_STATE_OUT` read canonical state by design, so they will never show
LFO motion no matter how correct the modulation is. The phase only advances
inside `Spectr::process()`. Anyone reaching for those variables to check this
will measure a dead instrument and read it as a defect.

### 8. Remaining correctness

- [x] The right-side dBFS scale is semantically correct.
- [x] Minimap edge dragging cannot move the opposite trim.
- [ ] Fast band drawing and minimap interaction remain intact.

dBFS evidence: `788e25b` anchors the ruler to the plot the curve is actually
drawn in. `Analyzer bridge: post-DSP spectrum preserves peak-amplitude dBFS`
passes at `26a16cc` (8 assertions), and the fix was proven RED with a control
planted in `runtime.js` that failed 6 of 165 assertions and passed 165 on
restore.

Minimap trim evidence: `native minimap edge drag cannot move the opposite
trim` passes at `26a16cc` (10 assertions), and `minimap edge drag cannot
narrow past the viewport codec floor` passes alongside it (14 assertions).

Band drawing and minimap interaction stay open on a reproduced regression, so
this line is worse than unproven. Against a 17.5 ms no-drag control on the
same build, a minimap edge drag measures p95 41.6 and 35.2 ms with 24 and 23
inter-frame gaps at or past 25 ms.

**Correction (2026-09-11).** An earlier revision of this line ruled JavaScript
out as the cause and named `src/ui/native_editor.cpp`, after
`processing_state_set` returns, as where the cost is charged. Both claims are
experimentally refuted and must not be carried forward. The bridge-call total
they rested on never isolated the drag: only 59 of roughly 180 pointer samples
reach the minimap-drag branch at all, and about 118 of the 177 publishes come
from the gain lane that `SPECTR_BANDS_PERF_FIXTURE=1` drives. The 55 ms
attributed to the drag is therefore mostly fixture gain commits, which neither
rules JavaScript in nor rules it out. `replace_processing_state` is cleared by
the same measurement.

What survives is the effect itself, still unattributed: a pointer sweeping the
**minimap region** costs about 2x idle p95, while a pointer sweeping the
**bands region** costs about 1.0x. Attributing that gap needs native per-frame
accounting in the paint / dirty-rect path, bucketed by whether the pointer is
inside the minimap rect. It does not need another bridge-call total -- that is
the instrument that produced the wrong answer the first time.

A second, independent instrument reproduces it. `tools/frame_cadence_probe.py`
measures the gap *between* painted frames rather than the duration of frames
that painted, which matters because `tools/analyze_interaction_trace.py` scores
only `frame` slice durations and is structurally blind to a window in which no
frame was painted at all — the exact shape of both stalls here. On that probe
the minimap drag reads a sustained p95 ratio of 1.81 with 22 sustained gaps at
or past 25 ms against a control of 3. It is load-sensitive, reaching 2.9 under
contention on the same host, so the milliseconds recorded above are a floor and
not a ceiling. Reproduce with:

    python3 tools/frame_cadence_probe.py --app build/Spectr.app/Contents/MacOS/Spectr \
        --mode minimap --runs 2 --json /tmp/cad.json

The band stalls are single-frame events, not a smear, and the press is the
larger of the two. Across four runs the whole cost lands on exactly frame 45
(147.1 and 157.3 ms) and exactly frame 225 (105.5 and 108.1 ms), with both
neighbours at one vsync. That is the signature of one expensive synchronous
handler, and it corroborates the three-synchronous-React-commits root cause
from a completely independent instrument. Worth stating plainly because this
document had only ever named the release stall; the press one is worse.

Three gates landed with the probe at `8f5a6d8`. `Spectr-frame-cadence-selftest`
is the analyzer's own negative control: it plants a 220 ms gap in a synthetic
series and requires detection, requires a clean series to stay quiet, and
requires a press-window gap to be routed out of the sustained set; hardcoding
`ge100_ms` to 0 in a copy exits 1. `Spectr-band-drag-cadence` gates at a 1.35
sustained p95 ratio against an idle control captured in the same invocation,
and measures 0.938 and 0.94, so it has real headroom. The report gate
`Spectr-minimap-drag-cadence-report` deliberately carries **no threshold**:
registered as a 2.40 ratchet it failed at 2.9 ten minutes after measuring
1.81 on the same host, so a gate there would report the machine rather than
the code. It records the
figures and still exits 3 when its premise is unproven, which keeps it from
rotting into decoration.

The liveness gate is what makes a zero trustworthy here. Two idle captures
differ in 0 of 2554200 pixels, so the idle render is bit-deterministic and any
divergence proves the gesture actually moved something. Disabling the driver in
a copy of the probe produced a healthy-looking 1.054 ratio and still exited 3
with "the bands driver did not move anything" — without that check, a dead
`PULP_TEST_POINTER_DRAG` would have read as the fastest run of the day.

A second, separate stall is root-caused and belongs to core Pulp: `@pulp/react`
commits synchronously per update, so each `setState` inside a pointer handler
is its own full document commit. The band release handler's three calls
measure `setGains=27 setHover=25 onStatus=35`, 87 ms total, and account for
the whole of a 104-126 ms unpainted gap; no-oping the handler collapses that
gap to 16.0 ms. Deferring to a microtask does not batch, and the bundled
`ReactDOM` exposes no `unstable_batchedUpdates` even though the reconciler
exports `batchedUpdates`.

Instrumentation note: `tools/analyze_interaction_trace.py` scores `frame`
slice durations and is structurally blind to a gap in which no frame was
painted at all, which is the shape of both stalls above. Use
`PULP_PARTIAL_RENDERING_DEBUG=1`, which emits one line per painted frame, or
`tools/frame_cadence_probe.py`.

### 9. Landing and package

- [ ] Pulp dispatch, Pulp dropdown/modal, and Spectr slices have passed focused
  architectural and adversarial review.
- [ ] Exact-head Pulp and Spectr PRs are merged with required checks green.
- [ ] Focused Logic AUv2 and REAPER acceptance passes on landed dependencies.
- [ ] One clean M5-testable PKG records the exact merged Spectr and Pulp SDK
  SHAs and passes signing, notarization, installation, and launch checks.

The former package wake condition is superseded: official Pulp SDK `v0.829.0`
(`cc75fa91cf6942a197b2fb00b38ac679de3cbcd1`) is available and the signed,
notarized package below was built and installed against it. Remaining package
work is exact-current-HEAD provenance only; do not substitute a PR-head SDK.

### 2026-09-02 verification checkpoint

- Release `build-release-0829/Spectr-test '[modulation]'`: 1 case / 3,110
  assertions passed.
- Release `build-release-0829/Spectr-test '[automation]'`: 2 cases / 1,103
  assertions passed.
- Release `build-release-0829/Spectr-native-n1-test '[host-automation-live]'`:
  1 case / 13 assertions passed.
- Release `build-release-0829/Spectr-native-n1-test '[dismissal]'`: 2 cases /
  350 assertions passed. Runtime geometry warnings are expected from the
  dismissal fixture and did not fail the tests.
- These are fresh local evidence only; Logic AUv2 lanes, REAPER editor/
  automation receipts, COR-1/2/3 review, and PR #8028 landing remain open.

### 2026-09-02 Logic Record baseline blocker

- A reported host symptom remains unclassified: after editing bands or the
  viewport, pressing Logic's Record appears to snap Spectr back to the last
  recording instead of starting from the current live state.
- Headless/native coverage does not reproduce it. `replace_processing_state()`
  synchronously mirrors the live field and viewport into the host StateStore;
  `apply_surface_params()` only adopts a host value when it differs from its
  applied cache. Existing `test_param_surface` and native host-automation
  tests cover those invariants.
- Do not change reset/restore semantics speculatively. The next diagnostic
  receipt must capture Logic parameter writes around Record (or a state
  restore callback) to distinguish Logic reapplying an existing automation
  lane from a plug-in reset. AUT-1/AUT-2 remain blocked on that host trace.

### 2026-09-02 additional UX feedback

- Automation playback is materially improved in the installed build (human
  confirmation); Logic automation-lane visibility remains a separate AUT-3
  acceptance item.
- The installed package still lacks visible drag cursor transitions. Browser
  and native fixtures pass, but host-level cursor publication is not yet
  proven; keep cursor acceptance open until a React/native fix is rebuilt and
  observed in AU.
- Queued, tested Spectr fixes are not yet in a new package: settings content
  tabs (`ad227c0`), keyboard/ink tab styling (`41d017a`), readable default
  typography (`701ff6b`), preset Apply-close plus `P` shortcut (`8fd9f82`), and
  minimap endpoint/performance evidence (`e6588ae`).

### 2026-09-02 package checkpoint

- Exact current Spectr head `db99a489f34101f370acc26b82f2050d7149d5d4` was
  configured and built in Release against official Pulp SDK `v0.829.0`
  (`cc75fa91cf6942a197b2fb00b38ac679de3cbcd1`).
- `artifacts/Spectr-1.0.3.pkg` was signed, notarized, stapled, and validated;
  SHA-256 is
  `56c3a65adce9a7e695001eb82fb9d4bc9ef298c68d15028cecf1b2a93889b2ab`.
- The package includes the Settings tab/content, typography, and preset fixes
  above. It must not be treated as cursor acceptance proof until AU host
  cursor transitions are observed after installation.

### 2026-09-02 cursor bridge checkpoint

- Commit `d5631db` makes cursor state React-owned in both the authored editor
  and the materialized shipping document. Every crosshair, grab/grabbing, and
  trim-resize transition now publishes through `setCursor(...)` while retaining
  the DOM assignment as a browser fallback.
- The materialization recipe is replayable; authored and materialized script
  blocks parse with Node, and the emitted document contains six paired React
  cursor publications for six imperative writes.
- AU host cursor observation and a rebuilt package remain open; do not mark the
  cursor item human-tested until that install check passes.

### UX burn-down audit (current)

- Cursor feedback: implementation complete and script-validated; native AU
  observation pending.
- Preset parity/Flare: implementation and automated parity tests complete;
  human visual confirmation pending for negative-band intent.
- Dropdown/modal defaults: generic Pulp behavior complete and covered by the
  authoritative WidgetBridge regression; Spectr host smoke remains pending.
- Unified status overlay: implementation and native/browser proofs complete.
- Settings: General/Modulation tabs, sticky scrolling, typography, close,
  dismissal, and copy feedback implemented and fixture-tested; human install
  review pending.
- Automation: deterministic replay and native projection proofs pass;
  Logic Record/Stop snap-back trace and Logic lane visibility remain open.
- Modulation: two-LFO settings and host-composition plumbing implemented and
  Release-tested; host acceptance and final package rebuild remain open.
- Release/landing: current package predates `d5631db`; rebuild/notarize after
  cursor acceptance, then verify exact Pulp SDK provenance and installation.

### 2026-09-02 package 1.0.4 checkpoint

- `artifacts/Spectr-1.0.4.pkg` was rebuilt from exact clean Spectr head
  `48b27ba2d518a21d6bbd44bc3b33ae504d3d72e2` against official Pulp SDK
  `v0.829.0`, source SHA `cc75fa91cf6942a197b2fb00b38ac679de3cbcd1`.
- SHA-256:
  `2db0072ba303191e26398d758bab0664202f45657efa41fb0feb2ce147da9fec`.
- Notarization submission `e9194ed3-5843-4c12-a8b9-7a6ed5a702f4` was accepted;
  staple validation passed and `spctl` reports Notarized Developer ID.
- The package includes the React/native cursor bridge. It is ready for AU/VST3
  installation and cursor observation; Logic Record/Stop tracing and REAPER
  acceptance remain separate host gates.

### 2026-09-02 Pulp dispatch landing observation

- Pulp PR #8028 remains open at exact head
  `f2a04d1dcd5adf2ff9019bf750b74fb983456dca`; it has no merge timestamp.
- Current checks are not a merge proof: macOS Build/Test is failed, UBSan is
  failed, and ASan plus macOS coverage are pending; TSan and the Linux/static
  lanes are green. This is observation-only—no retry, cancel, rebase, or queue
  mutation was performed.
- Spectr package provenance is independent of this still-open PR because it is
  built against the official SDK release `v0.829.0`; do not claim Pulp #8028 is
  landed until its authoritative required checks and merge state change.

### 2026-09-03 Pulp #8028 failure receipts

- UBSan job `100444609436` completed `failure` at `2026-09-03T00:01:40Z`.
  Its CTest summary names GPU recipe/probe/trace/DPR self-tests `18849`,
  `18911`–`18914`, `18918`, `18920`–`18923`, install-layout `18977`, and
  sampler evidence `19083`/`19084`; this is a test-level implementation or
  baseline gate, not a dispatch transport failure.
- The macOS job `100444687696` failed before CTest because Homebrew could not
  download its API metadata (`curl 56`, HTTP status `000`, proxy CONNECT
  aborted). ASan job `100444609396` and coverage job `100444692985` are still
  `in_progress`.
- These receipts were fetched with `ghapp api .../actions/jobs/<id>/logs` and
  sent to the #8028 owner. No CI retry, cancellation, rebase, or queue mutation
  was performed.

### 2026-09-03 coverage terminal receipt

- Coverage job `100444692985` completed `failure` at
  `2026-09-03T00:18:12Z`. The log shows control-SDK consumer compilation
  failures (`std::jthread`/`std::stop_token` unavailable), repeated
  infrastructure-safety `test_real_plugin_records_accelerate_load_dependency`
  failures, and PulpSampler streaming-admission assertions at source line 537.
  This is not evidence of a Spectr cursor or dispatch regression.
- ASan `100444609396` remains in progress; PR #8028 remains open at the same
  exact head. The receipt was sent to the owning implementation session and
  CI remains observation-only.

### 2026-09-03 corrected cursor package checkpoint

- Commit `1a6e3a7c87c23afc3f6ecc5d230a2f4b1b7df69c` fixes the cursor
  materialization recipe so the hover-status patch retains the React cursor
  state and repeated materialization does not duplicate `setCursor` calls.
- `artifacts/Spectr-1.0.5.pkg` was rebuilt from that exact clean head against
  official SDK `cc75fa91cf6942a197b2fb00b38ac679de3cbcd1`.
- SHA-256:
  `45e1a95b4e1a0d56194fc411abb25b89a0f7f49061e1f123ea5edcbc08eb3c56`.
  Notarization submission `975db2fd-7719-4fe3-b13d-17f3ac319497` was
  accepted; staple validation and `spctl` passed.
- Headless browser fixtures currently fail to emit their oracle under this
  local Chrome invocation (the previous source-patch-missing error is gone),
  so this is not counted as fresh browser-pass evidence. Native/Release
  proofs from the prior checkpoint remain valid; installed-host cursor and
  Logic/REAPER acceptance are still required.
- Re-running `tools/patch_materialized_editor.py` is byte-for-byte idempotent
  on the corrected checkout; all nine materialized script blocks pass
  `node --check`.

### 2026-09-03 release/format verification receipt

- `build-release-0829/Spectr-test '[modulation]'`: 1 case / 3,110 assertions
  passed.
- `build-release-0829/Spectr-test '[automation]'`: 2 cases / 1,103 assertions
  passed.
- `build-release-0829/Spectr-native-n1-test '[dismissal]'`: 2 cases / 350
  assertions passed. Expected geometry warnings were emitted; no test failed.
- `build-release-0829/Spectr-native-n1-test '[cursor]'`: 1 case / 54 assertions
  passed; `Spectr-native-n1-test '[settings]'`: 1 case / 167 assertions passed.
- Installed AU `auval -v aufx Spec Pulp` succeeded (`AU VALIDATION SUCCEEDED`).
- `clap-validator validate build-release-0829/CLAP/Spectr.clap`: 20 tests,
  15 passed, 0 failed, 5 skipped (unsupported preset-discovery and note-input
  capabilities); one scan-time warning remains advisory.
- These receipts strengthen release/native evidence but do not replace
  Logic/REAPER host acceptance, cursor observation in AU, or the open Pulp
  modulation dependency in PR #8028.

### 2026-09-03 current package and browser-harness audit

- Current staged package: `artifacts/Spectr-1.0.8.pkg`, SHA-256
  `6ed118ee2e221c7606d102d5c1974fe8a55a2eebf1879f14696e2283f328c2a4`;
  `spctl --assess --type install` reports `accepted` and `source=Notarized
  Developer ID`. Its AU payload matches the current `build-release-current`
  binary, but that exact lane still fails the strict Settings active-overlay
  assertion; this package must not be presented as the Settings fix.
- The older `build-release-0829` lane passes Settings (167 assertions), but its
  embedded runtime predates the ScrollView upgrade and is stale. The current
  package therefore remains blocked on fixing and rerunning Settings in the
  exact-current lane.
- Browser tests 199 (popups), 201 (build-info timeout), 202 (UX polish), and
  203 (preset parity) were attempted but remain open: the local headless
  fixture reports `source patch point missing`/stale generated artifacts and a
  temporary-directory cleanup failure. These are not counted as product proof
  until the fixture is regenerated and rerun.

### 2026-09-03 Settings lifecycle repair checkpoint

- Root cause is confirmed: the ScrollView upgrade used `removeWidget(id)` with
  `preserve_js_dom_state=false`, retiring React/native bookkeeping and leaving
  the replacement outside the root interaction state.
- Pulp successor worktree `/Users/danielraffel/Code/pulp-spectr-settings-overlay-20260902`
  carries commit `2b870f4e1` adding the preserve-state argument to
  `removeWidget(id, preserve_js_dom_state)`.
- Spectr carries commit `ea148f7` passing that preserve flag during the Settings
  ScrollView replacement. The exact-current binary still uses the pre-fix SDK;
  rebuild against the successor SDK is required before this can be called green.
- No package was produced and no visible app launch is required; the strict
  headless Settings dismissal gate remains the wake condition.

### 2026-09-03 exact SDK-overlay retest (still blocked)

- Rebuilt `Spectr-native-n1-test` against the official SDK 0.829.0 overlay plus
  Pulp commit `2b870f4e1`; no visible standalone launch was performed.
- `ctest -R 'native settings modal'` still fails at
  `rig.root->interaction().active_overlay != nullptr` after Settings opens.
  The state-parity diagnostics switch to `settings`, but the live
  `[data-spectr-settings-panel]` portal has no native subtree/parent in the
  interaction tree. This is the remaining lifecycle bug, not a test relaxation.
- Experimental detached/root reattachment was not accepted as a fix and is
  uncommitted. `artifacts/Spectr-1.0.8.pkg` remains known-bad and no new PKG is
  authorized until the strict gate and screenshot proof pass.

### 2026-09-03 full UX validation audit

- Current exact SDK-overlay `Spectr-native-n1-test` sweep ran 11 native cases;
  7 failed: N1 mount, proportional resize, Settings command, Settings
  dismissal, Flare mixed-sign behavior, frozen state-atlas interaction, and
  whole-bound button hit testing. These are current failures, not completion
  evidence.
- The older `build-release-0829` binary passes cursor (54), dropdown (253),
  dismissal (350), Settings (167), modal dismissal (520), and host-automation
  (13), but is stale relative to the current runtime and cannot certify this
  branch. Its Flare case also fails (18/19 assertions).
- Current automation and modulation suites pass only on the existing
  `build-ux-dispatch-sdk` binary (automation 2/1,103; modulation 1/3,110).
  Browser UX groups remain unverified: current materialized source unpack fails
  at a missing patch point before feature assertions, with headless display and
  temp-directory cleanup errors as secondary harness issues.
- Therefore cursor, dropdown, preset, overlay, Settings, and correctness items
  remain open for exact-current proof; no package or human host acceptance is
  authorized from these stale/partial receipts.
- A fresh Release `build-settings-fix/Spectr-test` run confirms only the
  non-browser suites currently wired into that target: automation passes 2
  cases / 1,103 assertions and modulation passes 1 case / 3,110 assertions.
  UX/browser tags are not registered in that binary; their CTest wrappers still
  fail during materialized-source unpack before assertions.
- Commit `245d1f9` makes the source adapter's `replaceSpectrSource` helper
  idempotent when a durable replacement is already present. A manually emitted
  headless popup fixture now reaches `SPECTR_BROWSER_POPUP_OK`,
  `SPECTR_BROWSER_RESIZE_OK`, and `SPECTR_BROWSER_MUTE_MODES_OK`; CTest's
  direct Chrome wrapper remains flaky in this headless session (CVDisplayLink
  failures), so this is harness progress, not final UX proof.

### 2026-09-03 Settings lifecycle follow-up

- Ultra review confirms the required Griddy-like topology: one overlay owner,
  fixed header/tabs, and a dedicated scrolling body; post-mount retries remain
  suspect and must be removed before release.
- Current runtime changes release hidden Settings overlay claims, gate stale
  retry callbacks on the live marker, provide explicit ScrollView extents, and
  keep hidden Settings out of home metadata accounting.
- Exact-current focused results: Settings command and Escape/outside dismissal
  pass; frozen state-atlas still fails the overflow ellipsis paint-origin
  assertion; base N1 still fails a canvas tap publication assertion. No PKG.

## Evidence policy

- Performance evidence comes from Release binaries (`-O3 -DNDEBUG`) and exact
  source/SDK provenance.
- Live Perfetto proves UI-thread projection and rendering; it does not prove
  realtime audio timing. Deterministic audio tests own that claim.
- A skipped, unavailable, or dirty-provenance gate is not a pass.
- Update this ledger in the same commit as each coherent completed slice.

### 2026-09-03 exact-head browser/native recheck

- On Spectr head `c84ba9954169b45c951de647f504042515de8792`, the current
  browser-side release gates pass: `Spectr-browser-build-info-timeout`,
  `Spectr-browser-ux-polish`, and `Spectr-browser-preset-parity` (3/3).
- The focused native Settings command test passes 54 assertions, but the
  native N1 canvas tap still publishes no editor request (`requests=[]`) on
  the available SDK-linked binary. The frozen Settings replay likewise still
  fails to discover the body-only native `ScrollView` on that binary.
- Pulp successor commit `2de6d5efa20e4895530ac0a76b940eb02a2fe7e8` remains the
  authoritative dispatch candidate; its focused WidgetBridge tests 14402 and
  14450 pass. A fresh SDK rebuild was not started because the preserved
  successor checkout lacks its prepared external VST3 dependency, so no
  end-to-end dispatch or release claim is made from this observation.
# 2026-09-04 Settings body-scroll lifecycle follow-up

- The durable materialized document now contains the authored
  `data-spectr-settings-body` wrapper. Runtime rehydration resolves that body
  through the global materialized selector/registry when portal ancestry is
  detached, upgrades the body to the native `ScrollView`, and leaves the
  Settings panel as the fixed shell.
- The preserved Pulp successor SDK (`2de6d5efa20e4895530ac0a76b940eb02a2fe7e8`)
  was used for a focused rebuild. The native Settings test reached the full
  topology and passed 363/364 assertions after the body-only upgrade; the one
  remaining failure is a snapshot-capture button revision not advancing after
  the Settings state-atlas reparent. This is not a green acceptance result.
- Header/group geometry is now inside the panel bounds (`settings_body` at
  `427,211.5`, `466x529.184`; feedback group at `427,1095.5`, `466x108`).
- No package was produced. The exact-current native matrix, canvas dispatch,
  host acceptance, and release PKG gates remain open.

### 2026-09-04 Settings retained-subtree repair

- Runtime now reconstructs direct Settings-body children from retained DOM
  parent pointers when state-atlas replay drops the parent's `_children` list;
  this prevents an empty native ScrollView after reopening Settings.
- Retained event props are explicitly rebound after the native reparent, and a
  detached/closed Settings panel is now treated as hidden from its live marker,
  releasing its overlay claim so the next home click is not consumed.
- Focused native checks: Settings command/cursor `54` assertions passed and the
  dismissal matrix passed `350` assertions. The full frozen state-atlas case
  now reaches the post-Settings manager/snapshot phases; it still fails later
  in the native self-removing manager hit-target matrix, so this is not green.
- No package was produced; exact-current UX matrix, canvas dispatch, host
  acceptance, and release gates remain open.
- The same full test now gets past Settings and snapshot setup, but fails at
  the native self-removing Pattern Manager cycle: the retained menu label's
  logical click owner differs from the native hit target after the atlas
  transition. This is additional evidence that the reparent/replay path is
  not yet release-ready.
- Independent focused native checks against the rebuilt binary still pass:
  cursor `54` assertions and dropdown Escape/outside/arrow/Return coverage
  `253` assertions. These are useful slice receipts, not a release gate.
- The native frozen-state test now reaches Pattern Manager detail rendering
  (325 preview rects, 314 distributed bars) but its descendant selector call
  throws `not a function` in the replayed materialized wrapper. This remains a
  harness/runtime compatibility issue to resolve before claiming preset proof.
- The native materialized wrapper measured as `Element` with no
  `querySelectorAll`; the runtime fallback now supplies that method. The test
  consequently advances to the next real assertion: selected preset title/SVG
  identity is still incoherent after selecting `factory:tilt`. Preset parity
  remains open pending a live-state text/identity repair.

### 2026-09-04 Native preset identity repair

- Commit `11731c6` repairs the replayed Pattern Manager title so its live text
  follows the selected pattern identity, and replaces the unsupported DOM-SVG
  attribute check with a native `SvgRectWidget` geometry comparison.
- The rebuilt official-SDK frozen state-atlas test passes `886` assertions,
  including Settings body-only scrolling, modal lifecycle, preset selection,
  and native preview geometry changing between `factory:tilt` and
  `factory:flat`.
- The complete native N1 binary is `19/24` test cases green (`6978/7068`
  assertions). The five remaining failures are confined to canvas hit/input
  ownership and resize-grip overlap caused by the two full-surface materialized
  canvas siblings; this is the next shared Pulp dispatch blocker.
- The button/resize matrix was corrected to scope itself to actual click
  controls (canvas gesture ownership is covered by the dedicated N1 canvas
  test), and the Flare crossing-zero case now exercises both a negative and a
  positive band. Focused Settings/mode and Flare checks pass (`167` and `24`
  assertions respectively). The remaining canvas-dispatch work is therefore
  not hidden by button-test noise.
- The full rebuilt native N1 UX matrix now passes **24/24 test cases and
  6,657/6,657 assertions**. This includes the canvas-specific N1 gesture
  coverage, Settings body-only ScrollView lifecycle, all dropdown/modal
  dismissal/navigation cases, cursor receipts, preset identity/geometry,
  Flare mixed-sign behavior, button hit targets, and resize-grip checks.
- A forced official-SDK Release re-embed/rebuild was attempted against exact
  head `46820c4`; the Release binary still fails two Settings assertions
  (`scroll_view == nullptr`, `settings_body == nullptr`) while the same source
  passes in the development/native matrix. This indicates the current
  v0.829.0 release SDK/runtime does not yet carry the retained-body topology
needed by the repaired materialized surface. No PKG is claimed from that
binary; the SDK/release-lane compatibility must be resolved first.

### 2026-09-04 Pulp successor identity audit

- Pulp successor head `c910bd88b` is clean and rebased onto current `origin/main`.
- Focused WidgetBridge/ScrollView/responsive coverage passes 336/336; the
  retained-container lifetime test passes 11 assertions.
- The safer ScrollView-wrapper experiment preserves the original View and its
  callbacks, but currently gives the wrapper and retained content the same
  native ID. Subsequent DOM parent resolution or attribute writes can therefore
  address the wrong node. This is a framework identity/alias blocker, not a
  Spectr implementation failure.
- No Pulp PR, official SDK refresh, Spectr rebuild, screenshot sign-off, or PKG
  claim is authorized until the framework adds a distinct wrapper/content
  identity or an explicit alias-routing contract with adversarial tests.

Follow-up `5f564f3f3` separates the authored ID registry from scroll-wrapper
lookup and routes `setScrollContentSize` through the wrapper map; its focused
336-test suite remains green and the lifetime regression passes 15 assertions.
The alias layer still needs lifecycle/frame-clock ordering and transactional
rollback coverage before the Pulp landing gate can be reopened.

### 2026-09-06 Measuring a shift inside a scroll viewport

Removing `paddingTop: 50` from the Settings body was verified by Skia raster
capture, BEFORE vs AFTER. Recording the method because the obvious measurement
gives the wrong answer here and will be re-derived wrong otherwise.

**A rigid-shift cross-correlation over the whole panel is the wrong instrument.**
The Settings body is a scroll viewport, so stripping leading padding does not
translate the panel. Content above the fold moves up by the removed padding, but
new content is revealed at the bottom — the lower half is not a translation of
anything, it is different content. Searching for the single offset that best
aligns before/after therefore reports a weak, misleading optimum:

| band (device px) | best shift | score at best | score at s=0 |
|---|---|---|---|
| top group, 420–780 | 41.0 design px | 8.667 | 16.970 (+49%) |
| first block, 420–680 | 45.5 design px | 8.447 | 18.510 (+54%) |
| top half, 420–900 | 45.5 design px | 8.954 | 15.799 (+43%) |
| lower half, 900–1490 | 10.5 design px | 4.597 | 11.477 (+60%) |
| **whole panel, 420–1490** | **10.5 design px** | 8.935 | 13.416 (+33%) |

The whole-panel number (10.5) is dominated by the revealed region and is not the
shift. The per-band numbers drift (41.0 / 45.5) because each band mixes some
translated and some revealed content.

**The exact measurement is first ink: the first row whose luma exceeds the
background.** It needs no alignment model and lands on the answer with no
interpretation. Background here reads exactly 18.04 mean row luma, so a
threshold of 20.0 separates background from content cleanly:

```
before: first content row y_dev = 525  (design 262.5)
after : first content row y_dev = 425  (design 212.5)
     -> 100 device px = 50.0 design px at scale 2.0
```

Exactly the `paddingTop: 50` removed, and consistent with the 16-byte artifact
delta (`len('paddingTop: 50, ')`) in `materialized-document.runtime.json`.

Negative control held exactly: the Home capture is byte-identical before and
after (0 changed px of 4,540,800). The Settings capture changed 6.669% within
bbox `(854, 424, 1779, 1485)`.

Generalization: **when the region under test can scroll, reveal, reflow or clip,
measure a landmark (first ink, an edge, a known glyph row), not a global
alignment.** Cross-correlation assumes rigid translation; a viewport violates
that assumption silently and still returns a confident-looking number.

### 2026-09-06 Standalone bundle relink (build-artifact repair)

`build-review-109/Spectr.app` was left damaged by an interrupted build — at one
point the executable was present with `libwgpu_native.dylib` missing, later the
dylib was present and the executable gone. Launching it aborted in dyld before
`main` (`Library missing`, `@rpath/libwgpu_native.dylib`).

This was never a shipping defect. `Spectr_Standalone.dir/build.make:374` carries
the `copy_if_different` for the runtime, identical in shape to the known-good
`Spectr_CLAP.dir/build.make:373`, and `target_copy_webgpu_binaries()` sets the
`@loader_path` rpath and performs the copy in the same function — so the rpath's
presence on the binary proves the helper applied. The trap:

> **A POST_BUILD `copy_if_different` only fires when the target relinks.**
> Anything that removes a copied file from the bundle stays removed until the
> next relink, and the build reports itself up to date.

Repaired with a bounded `-j2` relink of `Spectr_Standalone` through
`tools/ci/governed-build.sh`. Verified by running the bundle, not by the link
exit code: `native:Built Spectr Standalone renders headlessly without opening
audio` (ctest #210) launches the bundle and requires exit 0 — it passed in
6.66 s, against the same oracle that had reproduced the user's exact dyld error.

**False-green trap hit while doing this.** The first oracle attempt filtered on
the CMake *target* name (`Spectr-native-standalone-artifact-test`) rather than
the registered *test* name, and ctest answered `No tests were found!!!` while
exiting 0 — a zero-match filter that reads as a pass. `ctest -N | grep` for the
finding plus a total-count control is what caught it. Related: an RC captured
after a pipe into `tail` reports the tail's status, never ctest's.
