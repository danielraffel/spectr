# M11 Slice 2 — Engine Default Swap Design

> Decision doc for the remaining M11 work. Captures the two viable
> approaches to routing `EngineKind::Fft` through the windowed STFT
> engine landed in M11 slice 1. No code changes in this PR — just the
> decision record so execution is mechanical.

## Context

**M11 slice 1** (merged in Spectr PR #6) shipped
`spectr::WindowedStftEngine` alongside `BlockFftEngine`. Both satisfy
the `SpectralEngine` contract; only `BlockFftEngine` is reachable
from `make_engine(EngineKind::Fft)` today.

**Slice 2's charter** per the build plan: swap the default so
`EngineKind::Fft` routes to the windowed engine, closing out M11's
"non-aligned tone mute depth ≥ 80 dB" product claim.

**The wrinkle:** `test_block_fft_engine.cpp` has 7 test cases asserting
block-FFT-specific semantics (0 latency, sample-exact flat-gain
passthrough, bin-aligned mute depth against a single 2048-sample
FFT). A direct swap breaks those — not because the windowed engine
is wrong, but because the assertions are block-FFT-specific.

Two approaches are viable, each with distinct downstream implications.

## Option A — Add `EngineKind::WindowedFft` as a 4th kind

Keep `Fft` → `BlockFftEngine`, add a new enum value that routes to
the windowed engine.

### Changes

- `include/spectr/engine.hpp`: add `EngineKind::WindowedFft = 3`
- `src/block_fft_engine.cpp::make_engine`: route the new case to
  `make_windowed_stft_engine()`
- `include/spectr/spectr.hpp`: bump `kEngineMode` param range from
  `[0, 2]` to `[0, 3]` so the UI can reach the 4th kind
- `test_block_fft_engine.cpp`: unchanged (still targets `EngineKind::Fft`)
- `test_windowed_stft_engine.cpp`: unchanged (already targets
  `make_windowed_stft_engine()` directly)

### Pros

- **Zero test regressions.** Existing block-FFT tests keep working
  because they still address the same engine.
- **Non-destructive.** Users on existing presets can't accidentally
  flip to the new engine; it's opt-in.
- **Simple mental model.** Kind maps 1:1 to implementation.

### Cons

- **Semantically wrong.** The spec calls for the windowed engine as
  the default at M11. A 4th enum value parallel to the old one
  codifies the tech-debt rather than closing it.
- **ParamID range bump** is a StateStore-visible change. Any V1
  preset saved with `kEngineMode = 2.0` (Hybrid) still loads fine,
  but adding a 4th option means `kBandCount`-style `clamp` logic
  needs the new max everywhere it appears.
- **UI surface cost.** The editor's engine-mode picker would need
  "WindowedFft" as a fourth option. Hybrid currently reads as "the
  better Fft"; adding a separate WindowedFft next to Fft is
  confusing.
- **Doesn't actually ship the M11 product claim.** Flat-mask
  passthrough with block-FFT continues to leak on non-aligned tones;
  the only way a user gets the -80 dB mute is to know to flip the
  kind.

### When A makes sense

If the long-term plan is to ship **both** engines with different
latency/precision trade-offs (e.g., block-FFT is "Live" mode for
low-latency work, windowed-STFT is "Precision" mode for offline /
mix-down), Option A is the right modeling shape — but that pattern
is better served by Option B using the existing `ResponseMode` enum
rather than adding a new `EngineKind`.

## Option B — Route `Fft` via `ResponseMode`

`ResponseMode::Live` → `BlockFftEngine` (low latency, some leakage).
`ResponseMode::Precision` → `WindowedStftEngine` (higher latency,
clean -80 dB mute).

This reuses the existing `ResponseMode` knob which was designed for
exactly this trade-off (see `include/spectr/engine.hpp` comments).

### Changes

- `src/block_fft_engine.cpp::make_engine`: `EngineKind::Fft` case
  inspects… wait, `ResponseMode` isn't available at engine-
  construction time. It's a per-process-block arg. Either:
  - **B1:** `make_engine` takes `ResponseMode` as a second arg and
    returns the right impl. `Spectr::rebuild_engine_()` rebuilds on
    response-mode switch same way it does on engine-kind switch.
  - **B2:** a "HybridFft" class that owns BOTH engines internally
    and dispatches per-process based on `ResponseMode`. No rebuild
    on mode switch; a ~3 ms click at the switch boundary instead.
- `test_block_fft_engine.cpp`: update the rig to construct with
  `ResponseMode::Live` explicitly. Remaining cases still target
  block-FFT semantics; they just have to declare that intent.
- `test_windowed_stft_engine.cpp`: unchanged.
- New test: `test_response_mode_switch.cpp` — verifies that flipping
  mode between Live and Precision is click-free in the B2 variant,
  or rebuild-triggered in the B1 variant.

### Pros

- **Semantically correct.** Matches the engine.hpp comments and the
  build plan's "Live vs Precision" framing. The windowed engine is
  the Precision path; block-FFT remains useful for Live.
- **Default ships the product claim.** Users on default (`Precision`)
  get -80 dB mute automatically. The lower-precision block-FFT is
  opt-in via the existing Response knob.
- **No new enum.** `EngineKind` stays at 3 values (Iir, Fft,
  Hybrid); the mode selector doesn't need a new UI row.
- **Leverages existing state model.** `ResponseMode` is already a
  top-level param (`kResponseMode` in StateStore); existing presets
  carry the selection.

### Cons

- **`make_engine` signature change** (B1) or engine-internal
  dispatch (B2). Both are invasive to the engine construction path.
- **Latency is now mode-dependent**, not just kind-dependent. Any
  code that reports latency (e.g., `latency_samples()`) has to
  honor the mode.
- **Test refactor.** Existing block-FFT tests have to explicitly
  pass `ResponseMode::Live` to set up the rig.
- **B2 memory cost.** Holds both engines in memory simultaneously;
  ~2× the state vs B1. Negligible at 1024 samples × 64 bins × 2
  channels but a real allocation.

### When B makes sense

Default choice if the Live/Precision distinction is semantically
real (which the spec implies it is). B2 is preferable over B1 if
click-free mode switching matters at runtime; B1 is preferable if
rebuild overhead during mode switch is acceptable and simpler code
wins.

## Recommendation: **B2**

B2 matches the build plan's framing, ships the product claim on the
default path, respects existing StateStore semantics, and avoids a
click at mode-switch boundaries (which users WILL hit — Response is
a prominent UI control). The ~2× memory cost is negligible.

Trade: a small refactor of the construction path and updates to ~5
test cases in `test_block_fft_engine.cpp` to be explicit about
`ResponseMode::Live`. Both are straightforward.

## Rejected options

- **Silent swap:** just flip `Fft → WindowedStft` with no test
  update. Breaks 7 tests. Obvious no.
- **Delete `BlockFftEngine`:** might be tempting, but loses the
  Live/low-latency path the ResponseMode model is built around.

## Execution checklist (when Option B2 is chosen)

- [ ] Add new class `HybridFftEngine` that owns both
      `BlockFftEngine` and `WindowedStftEngine` internally
- [ ] `make_engine(EngineKind::Fft)` returns `HybridFftEngine`
- [ ] `HybridFftEngine::process` dispatches to the sub-engine
      matching the `ResponseMode` arg
- [ ] `HybridFftEngine::latency_samples()` returns max of the two
      (so host latency reporting is safe regardless of mode)
- [ ] `test_block_fft_engine.cpp`: prep rig with
      `ResponseMode::Live`, rename or retarget cases to make the
      block-FFT focus explicit
- [ ] New `test_response_mode_switch.cpp`: click-free switch test
- [ ] Full suite passes (110 + new cases)
- [ ] Product-claim test: non-aligned 997 Hz tone with
      `ResponseMode::Precision` hits -80 dB mute

## Related

- [Spectr PR #6](https://github.com/danielraffel/spectr/pull/6) —
  M11 slice 1 (windowed STFT alongside block-FFT)
- `include/spectr/engine.hpp` — `EngineKind` + `ResponseMode` contract
- `planning/Spectr-V1-Build-Plan.md` §M11 — product-claim target
  (-80 dB mute on non-aligned tones, Precision mode)
