# Spectr UX and Automation Burn-down

Last updated: 2026-09-02

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
| COR-2 | Minimap edge drag cannot move opposite trim | Current interaction exists | Final exact-SDK regression pending | Pending in new PKG | Open final acceptance |
| COR-3 | Fast band drawing and minimap interaction remain intact | Current interaction exists | Final performance/regression pass pending | Pending in new PKG | Open final acceptance |
| DEL-1 | Exact-head architectural/adversarial review | Not applicable | Review receipt pending | Not applicable | Open |
| DEL-2 | Spectr PR merged with required checks green | Spectr branch remains clean; Pulp modulation successor is pushed as PR #8028 at exact head `f2a04d1dcd5adf2ff9019bf750b74fb983456dca` | Build, baseline-diff, macOS-universal, linux-arm64, linux-x64, examples, and TSan are green; macOS Build + Test job `100444687696` failed before tests on Homebrew proxy CONNECT abort (curl 56 / HTTP 000), while ASan, UBSan, and macOS coverage remain pending | Not applicable | Open |
| DEL-3 | Logic AUv2 and REAPER VST3/CLAP acceptance | Formats implemented and the notarized PKG is installed into the system plugin locations | Installed AU `auval -v aufx Spec Pulp` passes; installed system CLAP validation passes 21 tests (16 passed, 0 failed, 5 skipped); REAPER smoke unit harness passes 39 tests. Real REAPER AU editor-open smoke was `INCONCLUSIVE` because the host scan cache did not publish the target (documented harness limitation); Logic automation and REAPER editor/automation receipts remain pending | Pending Logic/REAPER interaction and automation confirmation | Open |
| DEL-4 | Signed, notarized, installed, launch-verified M5 PKG | Exact-current-head (`302b265`) four-payload Release build completed against official Pulp v0.829.0 SDK (`cc75fa91cf6942a197b2fb00b38ac679de3cbcd1`) and installed | `artifacts/Spectr-1.0.2.pkg` SHA-256 `97becdb35be6787c12f2bbdcae919ebf0781bddd56182f3f275ea556928cfe94`; Apple notarization submission `ebdc0763-75dd-415f-b30b-307121e2911a` accepted; staple, `spctl`, installer signature, AU, and CLAP checks pass | Standalone launch and DAW host confirmation pending | Waiting human/host confirmation |

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

Current package wake condition: Root supplies an authoritative official Darwin
ARM64 SDK release asset containing the merged #8012 dispatch implementation.
Only then may this branch record its immutable tag, source SHA, and asset
SHA-256; configure the exact-SHA Release build; rerun native and host acceptance;
land Spectr; and package. Until then, the release-lane queue is an external
dependency, not permission to use a PR-head or locally built SDK.

## Evidence policy

- Performance evidence comes from Release binaries (`-O3 -DNDEBUG`) and exact
  source/SDK provenance.
- Live Perfetto proves UI-thread projection and rendering; it does not prove
  realtime audio timing. Deterministic audio tests own that claim.
- A skipped, unavailable, or dirty-provenance gate is not a pass.
- Update this ledger in the same commit as each coherent completed slice.
