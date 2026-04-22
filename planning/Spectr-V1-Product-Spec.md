# Spectr V1 Product Spec

Status: Draft
Date: 2026-04-22
Owner: Daniel Raffel
Implementation target: Pulp (`/Users/danielraffel/Code/pulp`)
Project: `/Users/danielraffel/Code/spectr/`

## 1. Product Thesis

Spectr is not an EQ and not a spectrum analyzer.

Spectr is a frequency slicer: a zoomable spectral effect that lets users isolate, remove, and recombine narrow frequency-defined parts of a sound with unusual precision.

The first release should ship as an effect plugin. It should feel complete on its own, while leaving a clean architectural seam for a later sampler mode where frozen spectral results become playable sources.

## Prototype Scope Rule

Prototype-visible effect features are in scope.

Multiple phases are fine, but the phases should sequence implementation and hardening, not silently remove capabilities already present in the prototype.

## 2. Product Identity

### What Spectr is

- A precision audio effect for frequency isolation and recombination
- A creative sound-design tool for pulling apart a sound into distinct spectral pieces
- A modern, performance-oriented reinterpretation of the zoomable filter-bank idea

### What Spectr is not

- Not a conventional graphic EQ
- Not a mastering EQ
- Not a spectrum analyzer with decorative controls
- Not a full sampler in V1
- Not a source-separation tool that claims stem-quality isolation

### Core promise

Users should be able to do all of the following quickly:

- zoom into a narrow part of the spectrum
- eliminate unwanted regions completely
- keep only a few non-adjacent regions
- reshape a sound into something new by spectral subtraction and recombination

## 3. Why This Should Exist

The strongest part of the reference idea is not "many bands." It is the combination of:

- many discrete bands
- dynamic zoomable frequency mapping
- analyzer-guided targeting
- cuts that can reach practical silence

That combination creates a "frequency microscope" rather than a normal EQ. That is the product category Spectr should own.

## 4. V1 Release Goal

Ship a musically convincing effect plugin that proves three things:

1. Spectr can isolate very specific frequency regions better than a typical EQ workflow.
2. Spectr can remove regions aggressively enough to feel like slicing, not trimming.
3. Spectr can be used as a creative reconstruction tool, not just a corrective tool.

If V1 nails those three truths, the later sampler mode has a strong foundation.

## 5. Primary Users

- Sound designers who want to extract or suppress very specific spectral regions
- Experimental producers who want to deconstruct and rebuild sounds
- Mixers who want more surgical and visual isolation than a standard EQ gives them
- Artists who want to turn one sound into multiple derivative textures by selective frequency removal

## 6. User Jobs

### Core jobs

- "Let me zoom into a small spectral area and keep only that."
- "Let me remove a region completely, not just turn it down."
- "Let me keep 300 Hz, 1.2 kHz, and 3.5 kHz and kill most of the rest."
- "Let me create a new sound by keeping only the interesting pieces."
- "Let me quickly try patterns and compare different spectral shapes."

### Future jobs the V1 architecture must not block

- "Freeze what I built and play it chromatically."
- "Turn separate frequency slices into separate playable layers."
- "Use Spectr as an instrument, not only an effect."

## 7. Product Principles

- `Isolation over enhancement`: the product is defined more by removal and selection than by broad boosting.
- `Zoom changes meaning`: the same control surface becomes a different instrument at different scales.
- `Discrete bands matter`: continuous EQ curves are not the point; selectable spectral pieces are.
- `Mute means removal`: the UX must make total removal feel fundamental, not like a hidden edge case.
- `The analyzer serves editing`: visuals should directly help users grab energy where it exists.
- `Sampler-forward, not sampler-now`: V1 must be cleanly extensible without dragging sampler complexity into the first release.

## 8. Shipping Scope For V1

### In scope

- Effect plugin, not instrument
- Stereo in / stereo out
- User-selectable band layouts shown in the prototype: 32, 40, 48, 56, and 64
- Edge bands functioning as low-cut and high-cut boundaries
- Zoomable and pannable frequency viewport
- Always-available overview strip / minimap for full-range navigation
- Per-band gain control with true mute state
- Analyzer overlay with at least `Peak`, `Average`, `Both`, and `Off`
- Core prototype edit gestures for direct sculpting and fast repeatable shaping
- Pattern presets
- A/B snapshots with morph slider
- `Live` and `Precision` response modes
- `IIR`, `FFT`, and `Hybrid` engine modes
- Prototype action controls including `Reset`, `Clear`, `Invert`, `Mute All`, and `Fit View`
- Selection and group-edit gestures shown in the prototype
- Preset/state recall
- Standalone build for development and validation

### Out of scope for V1

- Instrument mode
- File loading in the plugin UI
- Freeze-to-sampler playback
- Per-band multi-output routing
- Stem export or built-in bounce manager
- Mid/side processing
- Surround or immersive workflows
- AI-assisted suggestions

## 9. Band Layout Strategy

The prototypes already establish variable band density as part of the effect's language. That feature should remain in the V1 effect plan.

The right constraint is not "remove it." The right constraint is "implement it without making recall or automation fragile."

Requirements:

- 32 bands remains the default identity layout
- 40, 48, 56, and 64 remain selectable, matching the prototype
- patterns and snapshots survive layout changes predictably
- implementation details can evolve, but the visible selector stays in scope

## 10. Core Experience

### 10.1 Main canvas

The center of the product is a discrete vertical band field:

- 32, 40, 48, 56, or 64 columns across the current viewport
- each band has a gain state and a mute state
- middle bands act as spectral slices
- the leftmost and rightmost bands define boundary behavior and feel like low/high cuts

### 10.2 Frequency viewport

The viewport is not just a visual zoom. It changes what the bands mean.

- frequency mapping is logarithmic
- users can zoom in and out smoothly
- users can pan the visible range
- the visible range is part of the sound-defining state, not just a camera setting

### 10.3 Overview strip

The overview strip is mandatory in V1.

It should:

- show the full 20 Hz to 20 kHz range
- show the active window
- allow dragging the active window
- make zoom state legible at a glance

This is a primary control surface, not optional chrome.

### 10.4 Analyzer

The analyzer should sit directly behind or beneath the bands and support precise targeting.

Modes:

- `Peak`
- `Average`
- `Both`
- `Off`

The analyzer is informational, but it must feel tightly coupled to the editing gesture.

### 10.5 Editing

V1 should support these interaction patterns:

- drag a band vertically to change gain
- click or double-click to toggle mute
- drag across multiple bands to paint a curve
- scroll to zoom
- alt-drag or equivalent to pan the viewport
- hover readout for frequency and gain

### 10.6 Edit modes

Shipping V1 should preserve the prototype's visible edit modes:

- `Sculpt`: direct draw
- `Level`: flatten selected/painted bands to one level
- `Boost`: intensify or flatten an existing shape
- `Flare`: exaggerate positive and negative contours away from 0 dB
- `Smooth`: reduce band-to-band jaggedness

### 10.7 Patterns and snapshots

Patterns and snapshots are different concepts and must stay separate.

Patterns:

- store only relative band shape information
- are lightweight and reusable
- should include a small factory library

Snapshots:

- capture the full working state
- enable A/B comparison
- support morphing

Suggested factory patterns for V1:

- Flat
- Harmonic series
- Alternating
- Comb
- Vocal formants
- Sub only
- Downward tilt
- Air lift

## 11. DSP Contract

The implementation may use spectral, filter-bank, or hybrid techniques, but the product should be evaluated against behavior, not implementation ideology.

### 11.1 Identity path

`Precision` is the reference mode for judging product truth.

It must prioritize:

- narrow-band targeting
- deep cuts / practical silence
- stable reconstruction
- predictable behavior under extreme settings

### 11.2 Live path

`Live` is the lower-latency path.

It may relax some precision, but it must preserve:

- the same control model
- the same state model
- the same general sound intent

### 11.3 Flat-state behavior

With all bands at neutral and no mutes engaged, Spectr should behave as a transparent pass-through aside from any unavoidable reported latency in the chosen engine.

### 11.4 Null behavior

Muted bands must behave like true removals, not mild attenuation.

User expectation:

- if a band is muted, that region is effectively gone
- muted regions should be silent enough to feel categorical, not approximate

### 11.5 Stereo behavior

V1 should apply the same mask/shape across left and right by default to preserve stereo image.

Stereo-independent editing is not a V1 goal.

### 11.6 Latency

- Precision mode may introduce meaningful latency if it improves the core result
- Live mode should be substantially more responsive
- any latency must be reported to the host correctly

### 11.7 Failure modes to avoid

- broad smeary behavior when the user expects narrow targeting
- unstable gain jumps while dragging
- harsh zipper noise during state changes
- audible crackle when toggling mute
- a "pretty analyzer, weak audio result" mismatch

## 12. Pulp State Foundation

Pulp already ships two complementary state systems. Spectr should use both, not reinvent.

Both were verified against current Pulp code while writing this spec
(`core/state/src/store.cpp`, `core/state/src/state_tree.cpp`).

### 12.1 Canonical flat plugin state — `StateStore`

Use `pulp::state::StateStore` as the canonical host/plugin state for:

- stable automatable parameters
- host recall
- project save/load
- A/B and snapshot capture of public parameter state
- gestures and undo grouping

`StateStore` is the foundation Pulp's format adapters (VST3, AU v2, CLAP)
already route host automation and save/load through. That is the right anchor
for Spectr's automatable surface.

### 12.2 Hierarchical companion state — `StateTree`

Use `pulp::state::StateTree` as the companion model for dynamic and nested Spectr structures such as:

- variable band-layout metadata
- snapshot banks and snapshot metadata
- editor-only layout state
- pattern library entries (structured, not just flat key/value)
- future frozen assets and richer spectral objects

`StateTree` is a hierarchical reactive system with JSON in/out. It is the right
place for structures that are awkward to force into a flat parameter array.

### 12.3 Recommended Spectr state split

Recommended split:

- `StateStore` for automatable/public state
- `StateTree` for dynamic hierarchical state

That gives Spectr both:

- a stable host-facing state surface
- a flexible internal model for variable layouts and later sampler work

### 12.4 Preset and snapshot contract — IMPORTANT CAVEAT

Pulp ships `pulp::state::PresetManager`, but the current implementation writes
a JSON map of `parameter_name -> float_value` only. It does **not** currently
persist the `StateStore.serialize()` binary blob or any `StateTree` payload.
Factory preset discovery is not wired to a default path.

That means `PresetManager` out of the box is insufficient for Spectr, which
needs to persist more than a flat parameter snapshot (variable band layouts,
snapshot banks, patterns, future freeze metadata).

Spectr's preset contract should therefore be defined explicitly by Spectr:

- canonical payload = `StateStore.serialize()` binary blob
- structured payload = `StateTree` JSON for dynamic state
- file format = a single JSON wrapper containing both, with a version field
- factory presets = shipped as files inside the project, loaded by Spectr itself

Spectr may still use `PresetManager` as a file/metadata scanner, but it should
treat the preset *payload* as its own contract, not lean on PresetManager's
current built-in format.

### 12.5 Implication for variable band layouts

Variable band layouts are still in scope, but they should not be allowed to destabilize the product contract.

That means:

- stable public parameter semantics
- deterministic mapping between layout and sound state
- predictable remapping for patterns and snapshots
- layout changes treated as structured state, not ad hoc UI mutation

## 13. Dependency Strategy

Pulp already provides a meaningful built-in DSP baseline. Spectr should start there and only add third-party libraries where the built-ins stop being the best answer.

These built-ins and package lanes were verified against current Pulp code and
the `docs/guides/packages/` catalog while writing this spec.

### 13.1 Built-ins first (confirmed present)

Confirmed in `core/signal/include/pulp/signal/` and friends:

- FFT (`fft.hpp` — real/complex, Apple vDSP fast path)
- STFT (`stft.hpp` — ring-buffered, frame API)
- Spectrogram helpers (`spectrogram.hpp` — frequency axis, color mapper, buffer)
- Windowing (`windowing.hpp` — rectangular, Hann, Hamming, Blackman, flat-top, Kaiser)
- Convolution (`fft.hpp` overlap-add `Convolver` + `convolver.hpp` `PartitionedConvolver`)
- Interpolation (`interpolator.hpp` — linear, Hermite, Lagrange, sinc6)
- Multi-channel metering (`multi_channel_meter.hpp`)
- Visualization bridge (`core/view/include/pulp/view/visualization_bridge.hpp`)
  composing STFT + meter + waveform snapshots through `runtime::TripleBuffer`
  for lock-free audio-to-UI transport
- Sampler and synth examples: `examples/PulpSampler/`, `examples/PulpSynth/`,
  `examples/PulpDrums/`, `examples/mpe-synth/` (note: PulpSynth/PulpDrums
  CMake targets are currently CLAP-only — useful as a reference regardless)

Spectr should prove itself on these primitives before reaching for third-party
libraries.

### 13.2 Preferred package lanes

Best-fit documented package lanes for Spectr (all under
`docs/guides/packages/`):

- `signalsmith-dsp` — MIT
- `signalsmith-stretch` — MIT
- `dr-libs` — MIT-0
- `r8brain-free-src` — MIT
- `pffft` — BSD-3-Clause
- `libsamplerate` — BSD-2-Clause
- `cycfi-q` — MIT

Other documented lanes that may or may not matter for Spectr:

- `daisysp`, `rtneural`, `fontaudio`

### 13.3 Recommended usage

Recommended order of use:

- start with Pulp built-ins for core spectral processing
- add `signalsmith-stretch` if freeze/play or later sampler pitch/time behavior needs higher-quality stretching
- add `dr-libs` if Spectr needs broader import support beyond basic WAV paths
- add `r8brain-free-src` or `libsamplerate` for non-audio-thread sample-rate conversion workflows
- add `pffft` only if profiling proves FFT throughput is a real bottleneck
- add `cycfi-q` only if pitch-aware or note-aware features become part of the product

### 13.4 Package-manager note

Pulp ships a `pulp add` / `pulp search` / `pulp remove` CLI in `tools/cli/`,
but `docs/guides/packages/README.md` still frames the feature as evolving.

For Spectr, that means:

- the curated package docs are immediately useful for choosing candidates
- `pulp add` can be used when convenient
- manual CMake integration remains a safe fallback for production builds

## 14. Global Controls

V1 should include these global controls even if the exact placement changes:

- response mode: `Live` / `Precision`
- engine mode: `IIR` / `FFT` / `Hybrid`
- analyzer mode
- mix
- output trim
- reset / clear / invert / mute all
- fit view
- pattern library
- A/B snapshots and morph

`Mix` and `Output trim` are practical product controls even though they are not emphasized in the prototype.

## 15. State Model

The product should treat the following as recallable state:

- active band layout
- band values for the active layout
- mute states
- viewport min/max
- response mode
- engine mode
- analyzer mode
- mix
- output trim
- snapshot data

The following are not host-critical parameters and can remain UI/local state in V1:

- help overlays
- visual theme choices
- panel open/closed state

## 16. Host Automation Strategy

V1 should prefer stable, understandable automation over maximal automation.

Recommended host-exposed parameters (registered via `StateStore`):

- `Mix`
- `Output`
- `Response Mode`
- `Engine Mode`
- band controls sufficient to preserve the prototype's selectable layouts

Recommended V1 non-goals for host automation:

- viewport bounds (still saved in preset state, but not host-automated)
- analyzer mode
- pattern recall
- snapshot morph

Those values should still be saved in preset/state recall, but do not need to be host-automated in the first release.

If mute cannot be represented cleanly through a band's minimum endpoint, add explicit mute parameters only after validating host UX and parameter count.

The exact host-automation strategy can remain flexible, but the visible prototype features it supports should not be cut from the product plan.

## 17. Sampler-Forward Constraints

The V1 effect must make later sampler work easier.

### V1 must preserve these seams

- a clear separation between `live input`, `effect state`, and `rendered result`
- a serializable representation of the current spectral mask/state
- a capture point where processed audio can later be frozen to a buffer
- a difference between `pattern`, `snapshot`, and future `frozen asset`

### Definitions

- `Pattern`: reusable relative band shape (stored in `StateTree`)
- `Snapshot`: full working state for recall/morph (StateStore blob + StateTree JSON)
- `Frozen asset`: rendered audio plus the spectral state metadata that created it (sampler phase)

### Future sampler concept

The correct mental model is:

`Capture -> Freeze -> Play`

Not:

`Sampler + EQ`

## 18. Phase Plan

### Phase 0: DSP truth test

Goal:

- prove the core isolation behavior before polishing the plugin

Deliverables:

- engine comparison or spike (prototype-faithful filter-bank + FFT mask paths)
- flat-state transparency validation
- deep-cut validation (muted bands measured at -90 dB or lower)
- viewport remapping validation

Exit condition:

- the team trusts the core sound enough to build product around it

### Phase 1: Effect alpha

Goal:

- land the full prototype-visible effect experience in a usable plugin shell

Scope:

- selectable 32 / 40 / 48 / 56 / 64 band layouts
- viewport + overview strip
- analyzer (driven by `VisualizationBridge`)
- sculpt / level / boost / flare / smooth editing
- true mute
- live + precision response modes
- IIR / FFT / Hybrid engine modes
- mix / output
- reset / clear / invert / mute all / fit view
- patterns / presets
- snapshots / morph
- selection and group editing

Exit condition:

- the implemented effect reaches practical parity with the visible prototype

### Phase 2: Shipping V1 effect

Goal:

- turn the parity build into a distinct, polished product

Scope:

- smooth interaction polish
- stable recall (Spectr's own preset contract landed)
- CPU tuning
- host validation (auval, pluginval, clap-validator)
- preset content
- documentation and demos

Exit condition:

- Spectr feels complete as an effect, not like a sampler placeholder

### Phase 3: Decomposition workflows

Goal:

- strengthen the "frequency slicing" identity

Scope:

- deeper group editing and audition workflows
- audition and solo workflows
- grouped region handling
- improved render/export paths where justified

### Phase 4: Sampler bridge

Goal:

- add the smallest meaningful instrument step without redesigning the product

Scope:

- source selection
- freeze processed output to a playable buffer
- instrument playback across MIDI notes
- minimal effect/instrument mode switch

### Phase 5: Multi-slice instrument

Goal:

- turn spectral pieces into layered playable sources

Scope:

- per-band or grouped freeze
- mapped layers
- selective triggering

## 19. Success Criteria For V1

V1 is successful if users describe it as:

- precise
- unusual
- fast to understand
- more like slicing than EQ
- useful both for correction and sound design

The strongest outcome is that users start bouncing multiple versions of the same sound because Spectr makes spectral decomposition creatively obvious.

## 20. Open Questions Reserved For Implementation

These can be decided during build work as long as the product contract is preserved:

- exact engine design for Precision and Live modes
- exact dB range above 0 dB
- exact mute representation in host parameters
- exact latency tradeoff in Precision mode
- how `Flare` is implemented and tuned within V1
- final visual metaphor details

## 21. Final Product Statement

Spectr V1 should ship as a focused effect plugin built around one strong idea:

`a zoomable frequency slicer for isolating, removing, and recombining sound`

If the effect nails that identity, the sampler becomes a natural next step instead of a distracting first step.
