# Spectr V1 Build Plan

Status: In progress
Date: 2026-04-22
State route: **§5.4 only.** `#625` is the plan of record. Do not build
under §5.5.

This plan sequences the V1 effect build so route-agnostic work can
proceed independently of `#625`. When Milestone 4 is reached, we pause
and wait for `#625` to land. The upstream-integration gate in
`Spectr-Upstream-Integration-Plan.md` §A is the gate.

## Order of Milestones

| # | Name | Depends on `#625`? | Ship gate |
|---|---|---|---|
| 1 | Foundation | No | scaffold works, tests pass |
| 2 | DSP truth spike | No | flat-state transparency + mute depth proven |
| 3 | Analyzer bridge wiring | No | `VisualizationBridge` publishes |
| **4** | **State registration** | **Yes — `#625` gate** | route decided + registered |
| 5 | UI skeleton | No (but bindings = yes) | band field + viewport render |
| 6 | Edit modes | Some bindings | S/L/B/F/G behaviours work |
| 7 | Pattern manager | Yes (for persistence) | §6.8 enumeration met |
| 8 | Snapshot / A-B / morph | Yes (for persistence) | roundtrip through preset |
| 9 | Preset file format | Yes | per handoff §7 |
| 10 | Format validation | No | auval + clap-validator green |
| 11 | Polish + CPU pass | No | CPU target met on reference hw |

## Milestone 1 — Foundation

### Scope

- Expand `spectr.hpp` from the stub into a proper multi-header layout
  under `include/spectr/`:
  - `spectr.hpp` — top-level `Spectr` class definition
  - `band_state.hpp` — canonical 64-slot band model + layout projection
  - `viewport.hpp` — log-Hz viewport math (min, max, band-to-Hz mapping)
  - `engine.hpp` — abstract `SpectralEngine` interface
  - `edit_modes.hpp` — enum + per-mode dispatch prototypes
- Split implementation out of headers where it already has non-trivial
  bodies.
- Reorganize CMake to find multiple sources under `src/`.
- Expand test harness: shared fixtures, test-signal helpers, tolerance
  macros.

### Exit criteria

- `pulp build && pulp test` still green.
- `spectr.hpp` compiles as the public entry point.
- Stub `Spectr` class still runs the 3 existing scaffold tests.

### Non-goals

- No DSP implementation yet — engines are interface-only.
- No `StateStore` parameter changes beyond the existing `Mix` param.
- No UI code.

## Milestone 2 — DSP Truth Spike

### Scope

Prove the core spectral-mask behaviour is viable before building product
surface.

- Implement `FftEngine` first (simplest mask path):
  - STFT via `pulp::signal::Stft`
  - apply canonical band mask to bin magnitudes
  - iSTFT back to time domain
  - overlap-add with a pulp-provided window
- Implement `IirEngine` second:
  - bank of band-pass filters per canonical band
  - sum filter outputs (or leave as parallel bands for later routing)
- Skip `HybridEngine` until M2 exit criteria met — this is a spike, not
  the final engine set.
- Test fixtures:
  - flat-state transparency test (all bands at 0 dB, mute off) — output
    ≈ input within engine tolerance
  - mute-depth test (bank of bands muted) — output within muted band
    region < -80 dB relative to input
  - layout projection test (switch between 32 / 40 / 48 / 56 / 64
    layouts) — deterministic remapping

### Exit criteria

- FftEngine passes flat-state + mute + projection tests.
- IirEngine passes flat-state + projection tests; mute-depth target is
  -60 dB in IIR (spectral will be deeper).
- CPU under 20% of one core on `pulp-demo` reference material at 48k /
  512.

### Non-goals

- No Live vs Precision tuning yet.
- No analyzer hookup.
- No UI.

## Milestone 3 — Analyzer Bridge Wiring

### Scope

- Hook `pulp::view::VisualizationBridge` to the DSP output.
- Publish STFT + meter snapshots to the UI thread via `TripleBuffer`.
- No UI rendering — just prove the publish path is lock-free and the
  numbers flow.

### Exit criteria

- Headless test reads published snapshots and verifies rough shape
  (monotonic decay on impulse, noise-floor on silence).
- No glitches or priority inversions under rapid parameter changes.

### Non-goals

- No visual output.
- No analyzer-mode switching.

## Milestone 4 — State Registration (GATE)

### Pre-gate check

Run `Spectr-Upstream-Integration-Plan.md` §A.2 before entering this
milestone.

- If `#625` merged to `origin/main` → proceed under §5.4.
- If `#625` still open → **park here.** Use the idle time on Milestone
  10 validation infrastructure or tighter Milestone 2 tests. Do not
  pre-register parameters under a §5.5 shape.

### Scope (assuming `#625` merged)

- Register flat automatable surface via `StateStore`:
  - `mix`, `output_trim_db`
  - `response_mode`, `engine_mode`
  - band_count (enum)
  - plus a small set of continuous automation handles chosen per product
    spec §16 (to be finalized at this milestone)
- Implement `serialize_plugin_state` / `deserialize_plugin_state` hooks
  on `Spectr` class:
  - payload = `StateTree` JSON containing
    - canonical 64-slot band gains + mutes
    - viewport bounds
    - snapshot banks
    - pattern library
    - analyzer mode, edit mode
  - empty span on restore = reset to defaults
  - return false on version mismatch or malformed JSON
- Add tests:
  - `StateStore` round-trip (existing test, verify still passes)
  - supplemental blob round-trip via
    `pulp::format::plugin_state_io::{serialize,deserialize}` in the
    headless host
  - legacy-blob handling — pass a `PULP`-only blob, confirm
    `deserialize_plugin_state` is called with empty span

### Exit criteria

- All existing tests pass.
- New round-trip tests pass.
- Format adapters compile and the scaffold-test suite runs against each.

## Milestone 5 — UI Skeleton

### Scope

- Band field renderer (canvas-based or view-based, per the Pulp UI
  stack).
- Viewport (log-Hz mapping, zoom, pan).
- Overview strip / minimap.
- Hover readout.

### Exit criteria

- Standalone app shows the band field.
- Zoom / pan work.
- Analyzer overlay renders published data from M3.

## Milestone 6 — Edit Modes

### Scope

- All five modes: `Sculpt(S)`, `Level(L)`, `Boost(B)`, `Flare(F)`,
  `Glide(G)`.
- Snapshot-at-drag-start for Boost / Flare / Glide.
- Selection + group move.

### Exit criteria

- Each mode has a headless test asserting the gain delta matches the
  prototype's per-mode contract.

## Milestone 7 — Pattern Manager

### Scope

Per product spec §6.8 full enumeration:
save, rename, duplicate, delete, update-from-current, set-as-default,
apply, search, factory vs user badges, import JSON (file / clipboard /
paste), export (file / clipboard / all), chrome rail dropdown.

### Exit criteria

- Factory patterns load on open (default pattern setting respected).
- User pattern JSON round-trips through import / export.
- Default pattern survives host session reload via the supplemental
  blob.

## Milestone 8 — Snapshot / A-B / Morph

### Scope

- Use `pulp::view::ABCompare` for the two-slot StateStore snapshots.
- Snapshot payloads store through the supplemental blob (A / B band
  shapes + viewport + mode state).
- Morph slider interpolates A↔B on the canonical 64-slot band model.

### Exit criteria

- A/B toggle is click-level in the UI.
- Morph is artifact-free at any position.
- Both survive host session reload.

## Milestone 9 — Preset File Format

### Scope

Per handoff §7: a Spectr-owned JSON wrapper containing `StateStore`
blob + `StateTree` payload + schema version + plugin version + metadata.
Not `PresetManager`'s default format.

### Exit criteria

- Save / load preset round-trip matches the current working state
  exactly.
- Older preset schema versions report a clear migration error.

## Milestone 10 — Format Validation

### Scope

- `auval` passes for AU v2 effect.
- `clap-validator` passes for CLAP.
- `pluginval` passes for VST3 at the highest practical strictness level.
- Standalone boots and passes the same headless tests.

### Exit criteria

- All four validators green.
- `pulp validate` passes as a one-liner.

## Milestone 11 — Polish + CPU Pass

### Scope

- CPU profile under `pulp-demo` and representative material.
- Tune engine FFT sizes, window lengths, smoothing constants.
- Click / zipper audit under rapid edits.
- Final DAW smoke: Logic, Ableton Live, Reaper, Bitwig.

### Exit criteria

- CPU budget met on reference hardware (target: under 15% of one core at
  48k / 512 with 64 bands active, Precision mode).
- No audible clicks during mute toggle or layout switch.
- Passes DAW smoke without crashes or scan issues.

## Build Risks Tracked

- `#625` API might change between the peek and the merged PR — the
  integration gate is explicitly designed to catch this.
- DSP truth spike in M2 might fail one of the exit criteria (mute depth
  or IIR stability). If so, pause and decide whether to pick a different
  engine topology before proceeding.
- Pulp SDK upgrade lands breaking `pulp_add_plugin()` changes — per the
  integration plan §B.1, read migration notes before running `pulp
  upgrade`.

## What's Out Of Scope For V1

See `Spectr-V2-Product-Spec.md` §12 for the full non-goals list. Sampler
work lives in `Spectr-Sampler-Phase-Spec.md` and is Phase 4+.
