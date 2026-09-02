# Spectr UX and Automation Burn-down

Last updated: 2026-09-01

This is the durable landing ledger for the final Spectr native-editor UX,
automation, host-acceptance, and package work. A checked box requires committed
implementation plus the named evidence; a local visual impression is not
completion evidence.

## Protected source state

- The primary `/Users/danielraffel/Code/pulp` checkout is unrelated recovery
  state and is outside this workstream.
- Pulp typed dispatch worktree:
  `pulp-widget-bridge-realtime-dispatch-20260901`, commit
  `17998f387baff370284d112527375a5d702af88b`.
- Spectr integration worktree: `spectr-ux-burndown-20260901`, based on commit
  `639acec8ae016c4ac43652538290cd43579b5265` until the current coherent slice
  is committed.
- Pulp dropdown/modal worktree:
  `pulp-dropdown-modal-acceptance-20260831`, commit
  `d6f5c37b1ea5973a7790721b976360b2b51f0b12`.
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
- [ ] The live AppKit to QuickJS to Skia/Graphite Perfetto gate passes for band,
  minimap, and automation workloads, with exact Spectr and Pulp SHA receipts.
- [x] Snapshot and typed dispatch costs, repaint counts, and frame-tail budgets
  have been inspected rather than inferred from the aggregate gate.
- [x] Scheduled audio automation remains sample-accurate and block-partition
  safe under the focused realtime tests.
- [ ] Logic shows the expected automation lanes and smooth band/viewport replay.

Pre-commit functional evidence: the deferred-draw automation trace passed with
0.057 ms projection p95 and 1.56/1.97 ms frame p95/p99; snapshot construction
averaged 0.004 ms and typed dispatch averaged 0.083 ms. The minimap workload
also passed. The full gate remains open because two repeat bands captures
reported 1.145-1.168 layout slices per input against the 1.100 cap, despite
passing input and frame-tail budgets. Exact clean-Spectr-SHA receipts must
replace this functional evidence after the slice is committed.

### 2. Cursor feedback

- [ ] Crosshair over band editing.
- [ ] Open hand over the movable viewport.
- [ ] Grabbing hand during viewport drag.
- [ ] Left/right resize cursor over viewport trims.
- [ ] Standalone, AUv2, and REAPER acceptance recorded.

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
