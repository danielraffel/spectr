# Spectr V2 Pulp Handoff

Status: Draft  
Date: 2026-04-22

This document translates the v2 product spec into practical implementation guidance for Pulp. Code is treated as source of truth over docs. Where the docs and code drift, this handoff follows the code and records the drift.

## 1. Working Assumption

The planning package is ready for V1 effect implementation.

Phase 0 still has to choose the active state route before parameters are
registered:

- prefer §5.4 if `danielraffel/pulp#625` lands in time
- otherwise use §5.5 fallback

The nearby format/MIDI branches are worth tracking, but they are not V1 effect
blockers:

- `feature/clap-midi-cc-coverage` matters for later sampler/instrument phases
- `feature/au-v2-effect-midi-input` is orthogonal to the V1 effect and to the
  sampler's AU-instrument lane
- `feature/format-skills-clap-vst3-auv3` is docs-only

## 2. Recommended First Shipping Targets

### First-wave Spectr targets

- `VST3`
- `AU v2`
- `CLAP`
- `Standalone`

### Recommendation on AU v2 vs AU v3

Pulp can handle both.

Use `AU v2` first for Spectr V1 because:

- the current Spectr scaffold already targets it
- Spectr is desktop-first
- AU v2 is in-host and non-sandboxed on macOS
- AU v3 is still the more complex deployment lane for a desktop effect

Keep `AU v3` as a later lane if:

- iOS/iPad matters
- Apple extension deployment matters
- the team explicitly wants to validate sandboxed AUv3 packaging

### Practical AU v3 caution

Pulp has a real AU v3 adapter and build path, but desktop AU v3 still carries:

- app-extension packaging
- sandbox rules
- a rougher install/distribution workflow
- less reason to take the complexity hit for an effect-first macOS V1

## 3. Source Inputs

Primary Spectr references:

- `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr (standalone).html`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr Sampler.html`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/src/`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/effect-ideas.txt`
- `/Users/danielraffel/Code/spectr-design/Spectr-2/sampler-ideas.txt`

Current Spectr scaffold:

- `/Users/danielraffel/Code/spectr/spectr.hpp`
- `/Users/danielraffel/Code/spectr/CMakeLists.txt`
- `/Users/danielraffel/Code/spectr/pulp.toml`
- `/Users/danielraffel/Code/spectr/test_spectr.cpp`

Pulp references:

- `/Users/danielraffel/Code/pulp/core/state/`
- `/Users/danielraffel/Code/pulp/core/signal/`
- `/Users/danielraffel/Code/pulp/core/view/include/pulp/view/visualization_bridge.hpp`
- `/Users/danielraffel/Code/pulp/core/view/include/pulp/view/ab_compare.hpp`
- `/Users/danielraffel/Code/pulp/core/format/src/{clap,vst3,au_v2,au}_adapter*`
- `/Users/danielraffel/Code/pulp/docs/guides/packages/`
- `/Users/danielraffel/Code/pulp/examples/PulpSampler/`
- `/Users/danielraffel/Code/pulp/examples/PulpSynth/`

## 4. Core Implementation Principle

Treat Spectr as four systems from day one:

- DSP engine
- analyzer bridge
- state and serialization layer
- editor and workflow layer

Do not let the current prototype UI determine the persistence contract by accident.

## 5. State Architecture

The main v2 correction is here.

### 5.1 What Pulp gives us today

- `StateStore`: stable flat parameter registry, host-facing, automatable, adapter-persisted
- `StateTree`: dynamic hierarchical state, JSON-capable, not adapter-persisted by default
- `ABCompare`: two-slot `StateStore` snapshot helper
- `PresetManager`: insufficient as Spectr's real preset format

### 5.2 What Spectr needs

Split Spectr state into three rings:

1. Host/session state  
This must survive DAW session save/load. On current Pulp this means `StateStore`, unless Pulp gains a supplemental plugin-state path.

2. Working/editor state  
This can use `StateTree` in memory, but if it must survive host session recall it cannot live only there.

3. Library/archive state  
Patterns, user banks, frozen assets, imported metadata, and other richer content should use Spectr-owned files or a Spectr-owned preset contract.

### 5.3 Sound-defining fields

These are not allowed to be editor-only:

- active band state
- active layout
- viewport bounds
- response mode
- engine mode
- mix
- output trim

If any of those fields only live in `StateTree`, current Pulp host save/load will lose them.

### 5.4 Preferred route

Preferred Spectr architecture if Pulp adds a supplemental plugin-state path:

- `StateStore` for host-visible automatable controls
- `StateTree` for richer structured state
- Spectr-owned supplemental state blob for host/session persistence of non-flat structured state
- Spectr-owned preset JSON wrapping:
  - `StateStore.serialize()` blob
  - structured state payload
  - metadata and versioning

This is the cleanest route.

If `#625` lands substantially as currently scoped, the preferred route maps to:

- `Processor::serialize_plugin_state() const -> std::vector<uint8_t>`
- `Processor::deserialize_plugin_state(std::span<const uint8_t>) -> bool`
- adapter-side `pulp::format::plugin_state_io::{serialize,deserialize}` helpers
  instead of direct hook calls
- backward-compatible blob handling: legacy `StateStore`-only payloads still
  round-trip unchanged
- an envelope format when plugin-owned state is non-empty:
  `[PLST magic][version][store_size][plugin_size][store_bytes][plugin_bytes][CRC32]`
- restore semantics where an empty plugin-state span means "legacy blob or reset
  plugin-owned state to defaults," and `false` means reject malformed payload

Builder implication: once `#625` lands, Spectr should treat §5.4 as the default
route, not merely a theoretical option.

### 5.5 Fallback route if Pulp does not add supplemental state

If `danielraffel/pulp#625` has not landed by Phase 0 kickoff, Spectr V1
ships under this explicit fallback contract. This is workable but
pushes more into host-visible parameters than is ideal.

#### 5.5.1 Recall guarantee — what survives DAW session reload

The following fields are recalled on every DAW session reload. They
live in `StateStore` and are serialized through each adapter's host
state path (`core/format/src/{vst3,au_v2,clap}_adapter.cpp`).

Canonical sound state (all registered as `StateStore` parameters):

- `mix` (0–100%)
- `output_trim_db` (-24..+24 dB)
- `response_mode` (Live / Precision — enum-as-float)
- `engine_mode` (IIR / FFT / Hybrid — enum-as-float)
- `band_count` (discrete: 32 / 40 / 48 / 56 / 64 — enum-as-float)
- `band_gain[0..63]` — 64 per-band gain slots in a canonical layout;
  the current layout projects its N visible bands onto the first N of
  the 64 canonical slots. Slots above N are held at 0 dB.
- `band_mute[0..63]` — 64 explicit mute flags (bool-as-float), same
  canonical-layout projection as `band_gain`
- `view_min_hz` (log Hz, 20..20000)
- `view_max_hz` (log Hz, 20..20000)
- `morph_ab` (0..1, between active A and B snapshots)

These must survive a DAW session reload under all format adapters.

Canonical layout rule: the 64-slot canonical representation is the
contract regardless of which visible layout (`32/40/48/56/64`) is
active. The build does not re-register params on layout change.
Switching layouts is a projection, not a parameter-set mutation.

#### 5.5.2 Runtime-only — does NOT survive DAW session reload

These are intentionally not recalled. Users who want them restored
must explicitly save a Spectr preset.

- A and B snapshot payloads (held in `ABCompare` slots only)
- user pattern library (session-ephemeral)
- active pattern name / default-pattern selection
- analyzer mode (`Peak` / `Average` / `Both` / `Off`) — UI-only in
  fallback; not recalled
- active edit mode (`Sculpt / Level / Boost / Flare / Glide`) — UI-only
- UI panel open/closed state, help popover state

Rationale for runtime-only: these are large, nested, or UX-shaped and
cannot be forced into flat `StateStore` parameters without bloating the
host automation lane past what is reasonable for a mix engineer to
scroll through.

#### 5.5.3 Preset-only — survives via Spectr-owned preset files

Spectr owns its own preset file format (see §7). A Spectr preset file
carries:

- the `StateStore.serialize()` blob (everything in §5.5.1)
- a `StateTree` JSON payload containing:
  - snapshot banks (A/B payloads beyond the two runtime slots)
  - user pattern library (names, shapes, metadata, `updatedAt`)
  - default-pattern id
  - analyzer mode
  - active edit mode
  - per-preset notes / author / tags
- a schema version and plugin version

Users who want their pattern library or snapshot banks to survive a
host session reload in the fallback route **must save a Spectr preset
and load it on session reopen**. This is the documented limitation of
the fallback. It is not ideal; it is workable.

#### 5.5.4 Host automation lane size

Under this fallback, the host sees **~136 parameters** registered by
Spectr:

- 4 global enums / continuous (mix, output trim, response mode, engine mode)
- 1 layout (`band_count`)
- 64 canonical band gains
- 64 canonical band mute flags
- 2 viewport bounds
- 1 morph slider

That is larger than the "small surface" §6 recommends, and it is the
price of not having `#625`. If `#625` lands before Phase 0, Spectr
drops to something closer to §6's recommended public surface
(mix / output_trim / response_mode / engine_mode plus a smaller band
surface) and moves everything else into the supplemental payload.

#### 5.5.5 Decision point

Phase 0 kickoff must write down which of §5.4 (preferred route via
`#625`) or §5.5 (this fallback) is in effect. The build team must not
start implementation until that decision is recorded, because the
parameter registration shape is different in each route.

## 6. Canonical Spectr State Model

### 6.1 Recommendation

Use a canonical internal spectral mask model, not a per-layout one-off model.

Recommended shape:

- one canonical band-state representation
- visible layouts `32/40/48/56/64` project from that canonical state into the current viewport
- layout changes remap deterministically through the canonical representation

### 6.2 Required stored concepts

- `band_count`
- `view_min_hz`
- `view_max_hz`
- canonical band gains
- canonical band mute states
- current response mode
- current engine mode
- mix
- output trim

### 6.3 Snapshot strategy

Treat snapshots separately from the live working state.

Recommended rule:

- current sound state is one thing
- A and B captures are another
- morph value is another

Under §5.5 fallback, this is already decided: A/B payloads are preset-only and
do not survive DAW session recall. Under §5.4 preferred route, they may move
into the supplemental payload if the build team wants host/session recall for
them.

### 6.4 Pattern strategy

Patterns should store relative spectral shape, not full working state.

Patterns should not own:

- viewport
- response mode
- engine mode
- analyzer mode

Patterns should own:

- reusable band-shape information
- metadata like name and tags

## 7. Preset Contract

Do not use `PresetManager` as the Spectr preset format.

Use a Spectr-owned preset contract with:

- schema version
- plugin version
- `StateStore` serialized blob
- structured payload for any additional Spectr state
- optional metadata such as name, author, notes, tags

### 7.1 Factory presets

Do not rely on `PresetManager` factory-path defaults for shipping factory content.
Spectr should own factory preset discovery and loading if factory content is part of the product.

## 8. UI And Analyzer Plumbing

### 8.1 Analyzer bridge

Use `VisualizationBridge` as the primary analyzer lane for Spectr because it already combines:

- STFT
- spectrum publication
- metering
- waveform capture

This is a good fit for the analyzer-over-bands design.

### 8.2 Analyzer rules

- analyzer data is read-only
- analyzer publication must stay lock-free
- analyzer frame rate and smoothing must not destabilize DSP
- live editing must remain smooth under UI load

## 9. DSP Strategy

### 9.1 Use Pulp built-ins first

Start with Pulp built-ins for:

- FFT
- STFT
- spectrogram helpers
- windowing
- interpolation
- convolution if needed

### 9.2 Third-party libraries only where they clearly help

Best candidates already reflected in Pulp package docs:

- `signalsmith-stretch` for future freeze/play pitch-time work
- `dr_libs` for broader import handling where needed
- `libsamplerate` or `r8brain-free-src` for offline resampling
- `pffft` only if profiling says FFT throughput is the bottleneck
- `cycfi-q` only if pitch-aware features become a real product need

### 9.3 Do not overfit to one DSP ideology too early

The product contract is:

- deep cuts
- useful isolation
- stable reconstruction
- trustworthy zoom-dependent targeting

Use whichever internal path wins those tests.

## 10. Code Spikes To Run Early

1. State-contract spike  
Decide whether Spectr can ship cleanly on current Pulp state facilities or whether Pulp needs supplemental plugin state first.

2. Band-remap spike  
Prove that layout changes preserve meaning across `32/40/48/56/64`.

3. DSP truth spike  
Compare `IIR`, `FFT`, and `Hybrid` paths against the product's actual isolation and reconstruction goals.

4. Snapshot spike  
Decide what A/B persistence guarantee Spectr makes on current Pulp and test it.

5. Format validation spike  
Confirm first-wave format behavior for `VST3`, `AU v2`, `CLAP`, and `Standalone` before UI polish.

## 11. Tests Spectr Should Add Early

- neutral-state pass-through test
- muted-band depth test
- viewport/layout remap determinism test
- `StateStore` roundtrip test
- Spectr preset roundtrip test
- A/B snapshot behavior test
- CPU and latency characterization for `Live` vs `Precision`

## 12. Remaining Pulp Work To Consider

If the next reviewer agrees, these should become GitHub issues instead of staying vague:

1. High priority: supplemental processor-owned plugin state in format adapters
This is the cleanest fix for Spectr's host/session recall problem without bloating host-visible parameters.

2. Optional: parameter visibility or automation metadata
Useful only if Spectr still needs to encode more internal state through `StateStore`.

3. Optional: AU v3 install/distribution helper improvements
Only matters if AU v3 becomes a committed Spectr target.

## 13. Direct Build Recommendation

Start Spectr only after the format/MIDI Pulp work is finished.

Then build in this order:

1. finalize the Spectr state contract
2. prove the DSP truth path
3. build full prototype-parity effect behavior
4. harden recall, presets, and shipping formats
5. only then start the sampler bridge
