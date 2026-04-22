# Spectr Pulp Handoff

Status: Draft
Date: 2026-04-22

This document translates the product spec into practical build guidance for Pulp. Pulp-side claims in this doc were verified against the current Pulp code
at `/Users/danielraffel/Code/pulp` while it was being written. Where Pulp docs lag behind the code, the code is treated as source of truth and the drift is called out explicitly.

## 1. Source Artifacts

Primary prototype references:

- `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr (standalone).html`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr Sampler.html`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/src/`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/effect-ideas.txt`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/sampler-ideas.txt`

Scaffolded Spectr project:

- `/Users/danielraffel/Code/spectr/spectr.hpp`
- `/Users/danielraffel/Code/spectr/CMakeLists.txt`
- `/Users/danielraffel/Code/spectr/pulp.toml`
- `/Users/danielraffel/Code/spectr/test_spectr.cpp`

Relevant Pulp references:

- `/Users/danielraffel/Code/pulp/README.md`
- `/Users/danielraffel/Code/pulp/docs/reference/capabilities.md`
- `/Users/danielraffel/Code/pulp/docs/guides/formats.md`
- `/Users/danielraffel/Code/pulp/docs/guides/signal-processing.md`
- `/Users/danielraffel/Code/pulp/docs/guides/packages/`
- `/Users/danielraffel/Code/pulp/core/state/`
- `/Users/danielraffel/Code/pulp/core/signal/`
- `/Users/danielraffel/Code/pulp/core/view/include/pulp/view/visualization_bridge.hpp`
- `/Users/danielraffel/Code/pulp/core/format/src/{vst3,au_v2,clap}_adapter.cpp`
- `/Users/danielraffel/Code/pulp/examples/PulpSampler/`
- `/Users/danielraffel/Code/pulp/examples/PulpSynth/`

## 2. Recommended Build Order

1. Build Spectr as an effect first.
2. Keep a standalone target for rapid DSP and UI validation.
3. Defer sampler playback until the effect has a trusted sound contract.

Do not start with a combined effect/instrument product.

Important scope rule:

- prototype-visible effect features stay in scope
- phases sequence implementation and hardening
- phases do not remove prototype functionality

## 3. Recommended First Shipping Targets

Primary target:

- macOS effect plugin

Recommended formats for the effect:

- VST3
- AU v2
- CLAP
- Standalone for development

All four are already scaffolded by `pulp create` and were built+tested at
project creation time in `/Users/danielraffel/Code/spectr/build/`.

Recommended targets for the later sampler phase:

- CLAP instrument (fastest lane; reference: `examples/PulpSynth/`)
- VST3 instrument
- Standalone

Rationale:

- Pulp demonstrates usable effect and instrument lanes
- note on/off support is enough for the first freeze-play instrument step
- standalone is the fastest place to validate source capture and freeze semantics

## 4. Product Model

Keep these concepts separate in code and serialization:

### 4.1 Spectr state

The live working effect state.

Suggested fields (flat, automatable — lives in `StateStore`):

- `mix`
- `output_trim_db`
- `response_mode`
- `engine_mode`
- per-band gain values for the active layout

Suggested fields (dynamic, hierarchical — lives in `StateTree`):

- `band_count`
- `band_states` (gain + mute per band, for the active layout)
- `view_min_hz`
- `view_max_hz`
- `analyzer_mode`
- snapshot bank
- pattern library

### 4.2 Band state

Suggested representation:

```cpp
struct BandState {
    float gain_db;
    bool  muted;
};
```

Do not rely only on a very low gain value to mean mute in internal state. Keep mute explicit.

The internal model must be ready for the prototype's selectable layouts up to 64 visible bands.

### 4.3 Pattern

Relative, reusable shape only.

Suggested fields:

- `name`
- `relative_band_values` in a canonical internal representation or layout-aware array
- optional tags / metadata

Patterns should not own viewport bounds. Patterns live in `StateTree`.

### 4.4 Snapshot

Full working state for A/B comparison and morph.

Suggested payload:

- `StateStore.serialize()` binary blob
- `StateTree` JSON subtree for non-parameter state

Snapshot banks themselves are a `StateTree` structure pointing at those payloads.

### 4.5 Frozen asset

Reserved for the sampler phase.

Suggested fields:

- rendered audio buffer
- source metadata
- spectral state metadata used at freeze time (`StateStore` blob + `StateTree` JSON at capture time)
- optional root note metadata

### 4.6 Recommended Spectr state architecture in Pulp

Confirmed present:

- `pulp::state::StateStore` (`core/state/include/pulp/state/store.hpp`) —
  canonical flat parameter registry with raw / normalized / modulated access,
  listeners, gestures, and binary save/load. Already used by VST3 / AU v2 /
  CLAP adapters for host automation and state save/load.
- `pulp::state::StateTree` (`core/state/include/pulp/state/state_tree.hpp`) —
  hierarchical reactive state with JSON to/from.

Use them as:

- `StateStore` for flat automatable / host-facing state
- `StateTree` for hierarchical / dynamic state

Why:

- `StateStore` already matches host automation and plugin save/load well
- `StateTree` is a better fit for variable band layouts, snapshot banks, pattern libraries, and future frozen-asset metadata

### 4.7 Preset and recall contract

Spectr V1 should define its own preset payload:

- `StateStore.serialize()` binary blob for canonical parameter state
- `StateTree` JSON for dynamic / hierarchical state
- wrapped in a single JSON file with a `version` field and metadata

This is safer than assuming flat parameter state alone will be enough.

### 4.8 PresetManager caveat — READ BEFORE IMPLEMENTING PRESETS

`pulp::state::PresetManager` exists
(`core/state/include/pulp/state/preset_manager.hpp`), but the current
implementation in `core/state/src/preset_manager.cpp` writes and reads a JSON
map of `parameter_name -> float_value` only. It computes the
`StateStore.serialize()` binary blob but does not persist it, and it does not
persist any `StateTree` payload. Factory preset discovery
(`factory_dir_`) is not wired to a default path.

Implications for Spectr:

- Do NOT treat `PresetManager` as "the Spectr preset format."
- Spectr should define its own preset payload (see 4.7) and write/read it through its own code.
- Spectr MAY still use `PresetManager` as a metadata scanner / bank manager for listing files, but the on-disk content must be Spectr's contract, not PresetManager's default shape.
- If Spectr needs factory presets that ship inside the plugin bundle, Spectr must load them itself — do not rely on PresetManager's factory path yet.

## 5. Architecture Recommendations

### 5.1 Separate the concerns early

Treat Spectr as four systems:

- DSP engine
- analyzer tap
- state/serialization layer
- UI/editor layer

The UI prototype is already rich. Do not let UI decisions drive DSP coupling.

### 5.2 Precision and Live should share state, not implementation

Recommended contract:

- one shared state model
- two processing modes behind the same parameter/state surface

This keeps recall, presets, and future freeze capture consistent.

### 5.3 Preserve both visible mode layers from the prototype

The prototypes expose two distinct mode concepts:

- response mode: `Live` / `Precision`
- engine mode: `IIR` / `FFT` / `Hybrid`

Do not collapse that distinction away in the build plan. Pulp can refine semantics, but the visible product should preserve both layers.

### 5.4 Analyzer must not destabilize DSP

Use Pulp's existing `VisualizationBridge`
(`core/view/include/pulp/view/visualization_bridge.hpp`) to feed the analyzer.
It composes `signal::Stft` + `signal::MultiChannelMeter` and publishes
snapshots through `runtime::TripleBuffer` for a single-writer / single-reader
lock-free audio→UI transport.

Treat the analyzer as read-only visualization data. It must never become the reason the product crackles or stalls under UI load.

### 5.5 The viewport is sound state

Do not model viewport as editor-only camera state.

For Spectr, viewport bounds change the meaning of the visible bands, so they are part of preset recall and snapshot recall. They live in `StateTree`, not in ephemeral UI state.

## 6. Parameter Exposure Strategy

Prefer a smaller set of strong host parameters over trying to automate every UI concept in V1.

Recommended public host parameters (registered via `StateStore`):

- `mix`
- `output_trim`
- `response_mode`
- `engine_mode`
- band controls sufficient to preserve the prototype's selectable layouts without changing project meaning between sessions

Recommended internal/`StateTree`-only state in V1:

- analyzer mode
- edit mode
- theme / visual settings
- pattern library contents
- snapshot store
- viewport bounds

Why:

- parameter count stays manageable
- host automation remains understandable
- presets can still recall more than hosts automate

If hosts require explicit mute for usability, add explicit per-band mute parameters only after validating the UX cost against the chosen canonical parameter strategy.

## 7. Band Layout Preservation

Do not cut the prototype's band-count selector from the V1 effect plan.

Instead, solve it architecturally.

Recommended constraints:

- 32 remains the default identity layout
- 40 / 48 / 56 / 64 remain selectable
- layout switching must not corrupt recall
- snapshot and pattern remapping between layouts must be validated early
- `band_count` lives in `StateTree`, not as a host-automatable parameter

## 8. DSP Acceptance Tests

Before polishing UI, build automated or semi-automated checks for these. All
should live alongside `test_spectr.cpp` in the project root.

### 8.1 Identity / flat state

- neutral state passes audio transparently within the expected tolerance of the chosen engine

### 8.2 Mute depth

- a muted targeted region is suppressed enough to feel categorical (e.g. ≤ -90 dB within the muted band)

### 8.3 View remap determinism

- recalling the same viewport and state produces the same band-to-frequency meaning

### 8.4 State recall

- presets and snapshots restore exact working state (roundtrip `StateStore` blob + `StateTree` JSON)

### 8.5 Interaction stability

- no clicks when muting
- no zippering during common edits
- no runaway CPU spikes during zoom or drag

## 9. UI Build Guidance

The prototype is already pointing in the right direction.

Carry forward:

- wide central band field
- analyzer beneath / behind bands
- slim instrument-style chrome
- overview strip
- mode pills
- preset / pattern affordances
- A/B morphing

Do not let the chrome overpower the band field. The band field is the product.

## 10. Prototype Features To Preserve

From the current prototypes, these feel core and should survive into the implemented product:

- band-count selector
- band painting
- hover readout
- minimap / overview navigation
- analyzer modes
- full edit-mode set
- response mode selector
- engine selector
- pattern library
- A/B snapshots and morph
- clear distinction between live and precision behavior
- explicit mute state
- reset / clear / invert / mute all / fit view
- selection and group move

These are lower priority than the above if implementation pressure rises, but they should be treated as polish rather than scope cuts:

- many visual themes
- many visual metaphors
- overflow polish
- advanced settings chrome

## 11. Sampler-Forward Implementation Seam

Build V1 so a later sampler phase can add:

- `SourceSelector`
- `FreezeService`
- `FrozenAssetStore`
- `PlaybackEngine`

without rewriting the effect.

Recommended future flow:

```text
Source -> SpectrState -> DSP Render -> FreezeService -> FrozenAsset -> PlaybackEngine
```

V1 only needs the first half of that chain to be clean.

Reference implementations already in Pulp:

- `examples/PulpSampler/pulp_sampler.hpp` — sample playback, ADSR, MIDI, params
- `examples/PulpSynth/pulp_synth.hpp` — synth reference processor
- `examples/mpe-synth/pulp_mpe_synth.hpp` — MPE reference

Note: `PulpSynth` and `PulpDrums` currently ship as CLAP-only CMake targets.
They are still useful references for the instrument side.

## 12. Pulp-Specific Notes

### 12.1 Current Pulp strengths relevant to Spectr

- effect and instrument plugin formats already exist (VST3 / AU v2 / CLAP / Standalone)
- standalone builds are straightforward (`pulp create` scaffolded one already)
- GPU UI stack matches the prototype direction
- FFT and audio file support already exist
- MIDI note input in CLAP + AU v2 instrument is enough for a first freeze-play instrument step

### 12.2 Current Pulp caveats to respect

Code-verified (current main):

- CLAP event translation today covers note on/off and sysex, but not the full MIDI controller surface. This is fine for effect Spectr; it matters only if Spectr later wants rich CC automation into the instrument.
- AU v2 **effect** adapter does not currently wire MIDI input — Render always creates an empty `MidiBuffer`. Do not design effect features that need MIDI input. If Spectr ever needs "follow the host's MIDI" on the effect, this needs to be fixed in Pulp first.
- AU v2 **effect** parameter feedback to host **does work** today (docs lag behind code). The old "AU effect param feedback has caveats" claim from the v1 handoff is stale — the adapter snapshots params before `ProcessBufferLists`, diffs afterward, and calls `SetParameter` + `AUEventListenerNotify`.
- Sidechain routing **is** wired in VST3 and CLAP adapters (bus 1 → `Processor::set_sidechain()`). Older docs that say it is only declared are stale.
- Multi-bus behavior beyond sidechain is not the best place to start.
- Shipping targets should stay narrow until the macOS effect is solid.

### 12.3 Built-in DSP and UI primitives that already help Spectr

Confirmed in current Pulp code:

- FFT (`core/signal/include/pulp/signal/fft.hpp`)
- STFT (`core/signal/include/pulp/signal/stft.hpp`)
- Spectrogram utilities (`core/signal/include/pulp/signal/spectrogram.hpp`)
- Windowing (`core/signal/include/pulp/signal/windowing.hpp` — rectangular, Hann, Hamming, Blackman, flat-top, Kaiser)
- Convolution (`fft.hpp` overlap-add `Convolver`, `convolver.hpp` `PartitionedConvolver`)
- Interpolation (`core/signal/include/pulp/signal/interpolator.hpp` — linear, Hermite, Lagrange, sinc6)
- Multi-channel metering (`core/signal/include/pulp/signal/multi_channel_meter.hpp`)
- Visualization bridge (`core/view/include/pulp/view/visualization_bridge.hpp`)
- Sampler and synth examples (`examples/PulpSampler`, `examples/PulpSynth`, `examples/PulpDrums`, `examples/mpe-synth`)

This means Spectr should begin by proving itself on built-ins before adding external DSP dependencies.

### 12.4 Best-fit documented third-party package lanes

Most relevant documented package candidates (all under `docs/guides/packages/`):

- `signalsmith-dsp` — MIT
- `signalsmith-stretch` — MIT
- `dr-libs` — MIT-0
- `r8brain-free-src` — MIT
- `pffft` — BSD-3-Clause
- `libsamplerate` — BSD-2-Clause
- `cycfi-q` — MIT

Also documented (not likely Spectr-first, but available):

- `daisysp`, `rtneural`, `fontaudio`

### 12.5 Recommended package usage for Spectr

Practical recommendation:

- use Pulp built-ins first for core spectral processing
- add `signalsmith-stretch` when freeze/play or later sampler pitch/time behavior needs it
- add `dr-libs` if Spectr needs FLAC/MP3 import paths
- add `r8brain-free-src` or `libsamplerate` for prepare-time or offline sample-rate conversion
- add `pffft` only if profiling shows built-in FFT is the bottleneck
- add `cycfi-q` only if pitch-aware features become product scope

### 12.6 Package-manager maturity note

Pulp ships a curated package CLI in `tools/cli/package_commands.cpp`
(`pulp add`, `pulp remove`, `pulp search`, `pulp suggest`, `pulp target`,
etc.), but `docs/guides/packages/README.md` still frames the feature as
evolving.

So for Spectr:

- package docs are immediately useful for choosing candidates
- `pulp add` works for experimentation
- manual CMake integration remains a safe fallback for production builds

## 13. Recommended Milestones

### Milestone A: Truth spike

- minimal processor
- canonical band-state model that can support the prototype's layouts
- viewport mapping
- proof of narrow isolation
- baseline against Pulp built-in FFT + STFT + windowing

### Milestone B: Usable effect shell

- Pulp plugin project scaffold (done — `/Users/danielraffel/Code/spectr/`)
- core UI field
- analyzer fed by `VisualizationBridge`
- live / precision switching
- IIR / FFT / Hybrid switching
- band layout selector
- full visible edit-mode set
- state recall (Spectr-defined preset payload)

### Milestone C: Productized V1 / prototype hardening

- patterns
- snapshots
- mix / output
- presets (Spectr-defined contract, NOT raw `PresetManager` default)
- testing
- performance pass

### Milestone D: Sampler bridge

- source selection
- freeze capture
- MIDI playback from frozen result

## 14. Build Risks

Main risks:

- overcommitting to a DSP architecture before validating the audible result
- turning Spectr into a complicated EQ instead of a slicer
- exposing too many host parameters too early
- trying to solve sampler UX before the effect feels finished
- preserving variable band layouts without a stable canonical state model
- **adopting `PresetManager`'s default on-disk format without realizing it only persists flat parameter name/value JSON** — Spectr must ship its own contract
- designing AU v2 effect features that assume MIDI input into the effect lane (not wired today)

## 15. Bottom Line

Pulp should build Spectr as a prototype-faithful effect whose core truths are:

- zoomable frequency slicing
- true removal
- analyzer-guided targeting
- reconstruction through selective recombination

If that lands cleanly, the sampler phase becomes an additive extension instead of a reset.
