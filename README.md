# Spectr

A zoomable frequency-slicer audio effect built on [Pulp](https://github.com/danielraffel/pulp).

Spectr is not an EQ and not a spectrum analyzer. It is a **precision tool
for isolating, removing, and recombining narrow frequency-defined parts
of a sound** with unusual depth and targeting.

## Status

Release 1 is an effect with 32–64 authored logarithmic band controls, a
continuously zoomable frequency viewport, nonadjacent frequency islands, and
exact mute. The visible `RES represented/active` disclosure reports how many
controls the selected fixed FFT geometry can represent independently in the
current viewport. The default Balanced 8192 profile represents all 32 controls
across 20 Hz–20 kHz; a narrow zoom or a higher control count may represent fewer
controls independently. Its reviewed HTML design is embedded source-preservingly
through Pulp's native WebView bridge, with a narrow runtime adapter connecting
the design's live band/zoom state to native C++ DSP and state.

The production path builds AU, VST3, CLAP, and Standalone artifacts. The test
suite covers the shared spectral-mask DSP, exact latency and mute behavior,
state round-trip, actual CLAP/VST3 artifact hosting, and a headless Standalone
launch. Visible host validation is tracked in the canonical goal document.

For the M5 trial, hosts see only the meaningful continuous audio controls:
`Mix` and `Output`. Snapshot A/B selection and per-band morph remain editor-local
working state and are preserved with Spectr's supplemental plugin state; they are
not advertised as host automation until their realtime publication contract is
ready.

See [`planning/`](planning/) for the full design package:

- [`planning/Spectr-V2-Product-Spec.md`](planning/Spectr-V2-Product-Spec.md) — product contract
- [`planning/Spectr-V2-Pulp-Handoff.md`](planning/Spectr-V2-Pulp-Handoff.md) — build guidance
- [`planning/Spectr-V1-Build-Plan.md`](planning/Spectr-V1-Build-Plan.md) — implementation sequence
- [`planning/Spectr-Sampler-Phase-Spec.md`](planning/Spectr-Sampler-Phase-Spec.md) — Phase 4+ sampler spec
- [`planning/Spectr-Upstream-Integration-Plan.md`](planning/Spectr-Upstream-Integration-Plan.md) — Pulp pickup playbook
- [`planning/Spectr-Build-Signoff.md`](planning/Spectr-Build-Signoff.md) — current build clearance state

## Building

Requires a Pulp SDK with the dedicated native scripted Skia/Dawn view target
and AU/VST3/CLAP/Standalone support. A local Pulp checkout can produce the
immutable development SDK used by Forge-style consumers:

```bash
Pulp_DIR="$(pulp sdk install --local --profile forge-dev --print-path)/lib/cmake/Pulp"
Pulp_SHA="$(git -C /path/to/exact/pulp-worktree rev-parse HEAD)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR="$Pulp_DIR" \
  -DSPECTR_EXPECTED_PULP_SDK_SHA="$Pulp_SHA"
cmake --build build
ctest --test-dir build --output-on-failure
```

The forge profile validates WebView provenance and normalizes every installed
static archive to arm64. The expected-SHA gate rejects a compatible but older
or substituted SDK before Spectr compiles. Release/distribution builds should
use a provenance-marked, distribution-eligible release SDK from the same exact
accepted Pulp commit, not the development profile.

### Spectral build profiles

New build directories use the **Balanced** product default: an 8192-sample FFT
with a 2048-sample analysis hop. At 48 kHz it represents all 32 bands across
the full 20 Hz–20 kHz viewport and reports 8,191 samples (170.65 ms) of
latency to the host.

Two alternate fixed build profiles are available for explicit trials:

| Profile | CMake configuration | 48 kHz latency | Intended tradeoff |
|---|---|---:|---|
| Live | `-DSPECTR_FFT_SIZE=1024 -DSPECTR_ANALYSIS_HOP=256` | 1,023 samples / 21.31 ms | Lower latency, substantially coarser narrow-view isolation |
| Balanced (default) | `-DSPECTR_FFT_SIZE=8192 -DSPECTR_ANALYSIS_HOP=2048` | 8,191 samples / 170.65 ms | Full normal-range representation with useful zoom detail |
| Maximum | `-DSPECTR_FFT_SIZE=16384 -DSPECTR_ANALYSIS_HOP=4096` | 16,383 samples / 341.31 ms | Highest available narrow-view detail, highest latency |

These select one compile-time WOLA geometry for an artifact. They are not
runtime response modes and cannot be switched dynamically in a loaded plugin.
Use a fresh build directory when comparing profiles so an older CMake cache
does not retain its previous geometry.

## License

TBD.
