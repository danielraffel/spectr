# Spectr UX and Automation Burn-down

Last updated: 2026-09-03

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
| CUR-1 | Crosshair over band-editing canvas | Done | Prior native cursor suite passed; official-SDK rerun pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| CUR-2 | Open hand over movable viewport | Done (`grab`) | Prior native cursor suite passed; official-SDK rerun pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| CUR-3 | Closed/grabbing hand while moving viewport | Done (`grabbing`) | Prior native cursor suite passed; official-SDK rerun pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| CUR-4 | Left/right resize cursor over viewport trims | Done (horizontal resize) | Prior native cursor suite passed; official-SDK rerun pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| PRE-1 | Preset Manager matches original source HTML | Mostly implemented | Registered shipping-surface Chromium test passes 8 factory + saved-user states; source A/B comparison remains report-only and needs review | Pending in new PKG | Open |
| PRE-2 | Selected-preset detail layout and action overlap | Implemented for factory and saved-user states | Registered real-Chromium matrix passed all 8 factory + saved-user selection and rename/edit states; planted overlap control fails | Pending in new PKG | Waiting human confirmation |
| PRE-3 | Long names truncate before Snapshot controls | Done | Exact-head real-Chromium matrix passed | Pending in new PKG | Waiting human confirmation |
| PRE-4 | Icons/text vertically centered for every selection | Implemented | Registered real-Chromium matrix passed every factory + saved-user selection and rename/edit state alignment | Pending in new PKG | Waiting human confirmation |
| PRE-5 | Selected preset name and SVG both update | Done | Exact-head real-Chromium matrix passed | Pending in new PKG | Waiting human confirmation |
| PRE-6 | Flare correctly handles negative, positive, and zero bands | Current sign-preserving behavior implemented | Unit and native oracles pass current contract | Required: decide whether stopping at 0 dB is desired | Open product decision |
| DDM-1 | Escape closes every dropdown and modal | Done | Every Spectr surface passed in real Chromium; official-SDK native/plugin replay pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| DDM-2 | Outside click closes and consumes without mute/draw | Done | No-underlying-mutation negative-control coverage passed; official-SDK native/plugin replay pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| DDM-3 | Up/down changes highlighted item | Done | Real-Chromium coverage passed; official-SDK native/plugin replay pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| DDM-4 | Return selects and closes | Done | Real-Chromium coverage passed; official-SDK native/plugin replay pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| DDM-5 | Hover feedback differs from selection | Done | Real-Chromium coverage passed; official-SDK native/plugin replay pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| OVL-1 | Status text vertically centered | Done | Real-Chromium geometry coverage passed; official-SDK native rerun pending | Pending in new PKG | Waiting automated proof + human confirmation |
| OVL-2 | Status overlay below graph top ruler | Done | Real-Chromium geometry coverage passed; official-SDK native rerun pending | Pending in new PKG | Waiting automated proof + human confirmation |
| OVL-3 | Status updates immediately while dragging | Done | Live-drag browser and native coverage exists; official-SDK rerun pending | Pending in new PKG | Waiting automated proof + human confirmation |
| OVL-4 | Latest status remains visible longer | Done (2.2 s normal; 2.8 s mute/unmute) | Browser schedule oracle passes authored 2200/2800 ms values; native stale-clear replacement path and materialized inactivity-clear contracts pass; wall-clock expiry still needs clean exact-SDK replay | Pending in new PKG | Waiting automated proof + human confirmation |
| OVL-5 | Status disappears without an empty box | Done | Real-Chromium expiration coverage passed; official-SDK native rerun pending | Pending in new PKG | Waiting automated proof + human confirmation |
| SET-1 | Fixed header with content scrolling beneath | Done | Native geometry coverage exists; official-SDK rerun pending | Pending in new PKG | Waiting automated proof + human confirmation |
| SET-2 | Fixed close button with hover/press feedback | Done | Native interaction coverage exists; official-SDK rerun pending | Pending in new PKG | Waiting automated proof + human confirmation |
| SET-3 | Escape/outside click closes Settings | Done | Real-Chromium coverage passed; official-SDK native/plugin replay pending | Pending in new PKG and hosts | Waiting automated proof + human confirmation |
| SET-4 | Copy is centered and retains Copied feedback | Implemented | Registered Chromium runtime proof passes COPY/COPYING/COPIED and centering | Pending in new PKG | Waiting human confirmation |
| SET-5 | Status Info description is not truncated | Implemented | Registered Chromium rendered geometry proof passes | Pending in new PKG | Waiting human confirmation |
| SET-6 | No unnecessary scrollbar when content fits | Implemented | Registered Chromium fit/overflow proof passes | Pending in new PKG | Waiting human confirmation |
| SET-7 | Loading build info cannot remain stuck | Done | Real-Chromium timeout test and planted negative control passed | Pending in new PKG | Waiting human confirmation |
| AUT-1 | Recorded automation/playback is sample-accurate | Processor playback implemented | Exact-sample/block-partition proof plus released-SDK `Spectr-native-n1-test "*automation*"` (1 case / 12 assertions); DAW recording/AUv2 evidence still absent | Logic test required | Open |
| AUT-2 | Bands and viewport animate during host playback | Done | Native frame-lane and Perfetto proof passed | Pending in Logic with new PKG | Waiting human/host confirmation |
| AUT-3 | Logic automation lanes are visible and behave correctly | Parameter surface implemented | Real-host automated receipt unavailable | Pending in Logic with new PKG | Waiting human/host confirmation |
| MOD-1 | Internal modulation/LFO Settings UI | Done | Materialized Chromium proof passes; both LFOs expose on/off, Sin/Tri/Square/Saw, tempo rate, depth, and bridge-backed hydration/write paths; official SDK 0.829.0 `Spectr-test '*modulation*'` passes 5 cases / 3,123 assertions | Pending visual/audible check in new PKG | Waiting human confirmation |
| MOD-2 | Whole-bank, Snapshot A/B, and Morph targets | Implemented; independent Bank/A/B/Morph toggles now preserve a mixed target mask, with ALL/NONE shortcuts | Native target-mask bridge composes selected destinations; materialized Chromium proof asserts all four target controls and mask state; official SDK 0.829.0 focused modulation suite passes 5/3,123; exact package/audio proof remains pending | Pending audible check in new PKG | Waiting human confirmation |
| MOD-3 | Third-party host modulation works alongside internal modulation | Pulp successor foundation committed (`35364ef25f`, test extension `c757b68096`); Spectr now composes two native LFO overlays after host field/morph | Spectr released-SDK suite passes 169 cases / 162,522 assertions; Pulp CLAP host-validation passes 6 cases / 3,661 assertions; PR #8028 remains open with macOS Build + Test job `100444687696` failing before tests on Homebrew proxy CONNECT (curl 56 / HTTP 000) and other checks still running | Logic/REAPER after PR/API landing | Open |

### Additional correctness and delivery gates

| ID | Item | Implementation | Automated proof | Human confirmation | Overall |
| --- | --- | --- | --- | --- | --- |
| COR-1 | Right-side dBFS scale is semantically correct | Review pending | Pending | Pending if behavior changes | Open |
| COR-2 | Minimap edge drag cannot move opposite trim | Current interaction exists; endpoint clamp path audited | Native endpoint-invariant and horizontal-endpoint regression proofs present; exact-SDK rerun pending | Pending in new PKG | Open final acceptance |
| COR-3 | Fast band drawing and minimap interaction remain intact | Current interaction exists; pointer-owned hot path audited | Native minimap React-budget and browser interaction proofs present; exact-SDK/performance rerun pending | Pending in new PKG | Open final acceptance |
| DEL-1 | Exact-head architectural/adversarial review | Not applicable | Review receipt pending | Not applicable | Open |
| DEL-2 | Spectr PR merged with required checks green | Spectr branch remains clean; Pulp modulation successor is pushed as PR #8028 at exact head `f2a04d1dcd5adf2ff9019bf750b74fb983456dca` | Build, baseline-diff, macOS-universal, linux-arm64, linux-x64, examples, and TSan are green; macOS Build + Test job `100444687696` failed before tests on Homebrew proxy CONNECT abort (curl 56 / HTTP 000), while ASan, UBSan, and macOS coverage remain pending | Not applicable | Open |
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

- [ ] Crosshair over band editing.
- [ ] Open hand over the movable viewport.
- [ ] Grabbing hand during viewport drag.
- [ ] Left/right resize cursor over viewport trims.
- [ ] Standalone, AUv2, and REAPER acceptance recorded.

Implementation/proof state: protected Pulp main contains the AppKit
gesture-phase cursor refresh at `e5dc8d29c7`, and current committed Spectr source
drives the shipping materialized runtime through crosshair, left/right
horizontal resize, grabbing, and restored grab states. The available Release
`Spectr-native-n1-test '[cursor]'` passed 54 assertions, but its cache records
dirty Spectr `639acec` and development Pulp SDK `17998f…`; under this ledger's
provenance policy it is prior evidence, not a clean-head pass. Rebuild and rerun
on the merged immutable SDK before checking these rows or beginning host-format
visual acceptance.

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

- [ ] Text is vertically centered below the graph's top ruler.
- [ ] Drag feedback updates immediately and retains only the latest message.
- [ ] The overlay dismisses after inactivity without an empty intermediate box.

### 6. Settings

- [x] Modulation layout is stable, with fixed General/Modulation tabs and two LFO surfaces.
- [x] Header, close control, and settings tabs remain fixed while content scrolls.
- [ ] Close hover/press, Escape, and outside-click behavior pass.
- [ ] Copy is centered and preserves Copied feedback.
- [ ] Status Info is not truncated and unnecessary scrollbars are absent.
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

### 8. Remaining correctness

- [ ] The right-side dBFS scale is semantically correct.
- [ ] Minimap edge dragging cannot move the opposite trim.
- [ ] Fast band drawing and minimap interaction remain intact.

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
