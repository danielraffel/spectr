# Spectr UX and Automation Burn-down

Last updated: 2026-09-02

This is the durable landing ledger for the final Spectr native-editor UX,
automation, host-acceptance, and package work. A checked box requires committed
implementation plus the named evidence; a local visual impression is not
completion evidence.

## Protected source state

- The primary `/Users/danielraffel/Code/pulp` checkout is unrelated recovery
  state and is outside this workstream.
- Pulp typed dispatch worktree:
  `pulp-widget-bridge-realtime-dispatch-20260901`, commit
  `2c4d98c076e04cbcfd72fb538fb505a4dfd60972`; PR #8012's required
  Build and Test run `33583683746` has expanded from its zero-job concurrency
  wedge and is active, but no merge SHA exists yet.
- Spectr integration worktree: `spectr-ux-burndown-20260901`; the committed
  branch head is `ea4bf5668bf8eb3df0f73137e63cb893cf7860be`.
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

- [ ] Escape closes every dropdown and modal.
- [ ] Outside click closes and consumes the click, with no mutation behind the
  popup.
- [ ] Up/down changes one visible highlight; Return selects and closes.
- [ ] Hover feedback is distinct from selection.
- [ ] Pulp framework tests and inherited Spectr plugin-format behavior pass.

### 4. Preset parity

- [ ] Preset Manager matches the source layout without overlapping actions.
- [ ] Long names truncate before Snapshot controls.
- [ ] Selection updates both name and SVG with centered text/icons.
- [ ] Flare behavior is correct for negative bands and crossings through 0 dB.

### 5. Unified status overlay

- [ ] Text is vertically centered below the graph's top ruler.
- [ ] Drag feedback updates immediately and retains only the latest message.
- [ ] The overlay dismisses after inactivity without an empty intermediate box.

### 6. Settings

- [ ] Modulation layout is stable.
- [ ] Header and close control remain fixed while content scrolls.
- [ ] Close hover/press, Escape, and outside-click behavior pass.
- [ ] Copy is centered and preserves Copied feedback.
- [ ] Status Info is not truncated and unnecessary scrollbars are absent.
- [ ] Loading build info resolves promptly and cannot remain stuck.

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

## Evidence policy

- Performance evidence comes from Release binaries (`-O3 -DNDEBUG`) and exact
  source/SDK provenance.
- Live Perfetto proves UI-thread projection and rendering; it does not prove
  realtime audio timing. Deterministic audio tests own that claim.
- A skipped, unavailable, or dirty-provenance gate is not a pass.
- Update this ledger in the same commit as each coherent completed slice.
