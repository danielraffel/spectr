# Spectr Sampler Phase Spec

Status: Draft (hardening pass)
Date: 2026-04-22
Owner: Daniel Raffel
Pairs with: `Spectr-V2-Product-Spec.md` (effect-first V1), `Spectr-V2-Pulp-Handoff.md`

This spec covers the sampler phases that come **after** V1 ships.
It replaces the thin seam-preservation language in the V2 product spec §13 with a
concrete product contract that the build team can plan against. It does not
change V1 scope.

## 1. Mental Model (Do Not Redesign)

The only conceptual step the effect → sampler transition adds is:

```
Capture  →  Freeze  →  Play
```

That is:

- **Capture** a source (live track input, file, or selection).
- **Freeze** the current spectral mask into a rendered playable buffer.
- **Play** that buffer chromatically across MIDI notes.

Not "sampler + EQ." Not a full multisample instrument. The sampler IS the
same Spectr editing surface plus a freeze point plus a playback path.

## 2. Source Design Inputs

Two design voices fed this spec:

- `/Users/danielraffel/Code/spectr-design/Spectr-2/sampler-ideas.txt` — the
  "minimal viable instrument" framing. Explicitly rejects full sampler UI,
  zones, envelopes, multisample editors.
- `/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr Sampler.html` —
  the richer prototype that adds root key, tune, direction, loop region,
  sample region, fades, and crossfade on top of the minimal model.

This spec honours both by phasing them: the minimal framing is Phase 4
**exit criteria**; the richer prototype controls are the Phase 4
**production target**. Multi-slice is Phase 5. Live spectral resynthesis
is Phase 6 (deferred).

## 3. Out Of Scope For Every Sampler Phase (Hard)

- Full multisample editor.
- Velocity-mapped zones.
- Round-robin slots.
- Per-zone modulation matrices.
- Streaming-from-disk sampler engines (Kontakt-style large sample sets).
- Integrated sample browser with tagging.

If a feature smells like a professional multisample instrument, it does not
belong here. Spectr's sampler is a frequency-slicing instrument, not a
general-purpose sample player.

## 4. Phase 4 — Sampler Bridge (The Commit)

### 4.1 Goal

Turn one Spectr-processed result into a playable buffer and play it through
MIDI. Prove the Capture → Freeze → Play chain works without redesigning
the effect.

### 4.2 Product Shape

Add exactly these new user-facing concepts to the Spectr surface:

- **Source picker** — dropdown selecting where audio comes from.
  Allowed sources:
  - plugin sidechain / track input (the default; matches prototype
    `input-12`)
  - dropped / loaded audio file
- **Freeze toggle** — single control (keybinding **F**, matching
  prototype) that captures the current processed output into a sample
  buffer.
  - `frozen = false` → live processing, effect behaviour unchanged
  - `frozen = true` → playback of the captured buffer, not live input
- **Effect / Instrument toggle** — plugin-level mode switch. Effect mode is
  the V1 surface unchanged. Instrument mode routes host MIDI note on/off
  into buffer playback.
- **Root key** — MIDI note that plays the buffer at its original pitch.
  Prototype default `60` (C4).
- **Fine tune** — cents offset.
- **Direction** — forward / reverse.
- **Sample region** — `sampleStart` / `sampleEnd` normalised scrub points
  into the frozen buffer.
- **Loop mode** — off / forward / ping-pong.
- **Loop region** — `loopStart` / `loopEnd` scrub points when loop is on.
- **Fades** — `fadeIn`, `fadeOut`, and `crossfade` (loop boundary).

Every one of those fields is present in the prototype settings blob
(`/Users/danielraffel/Code/spectr-design/Spectr-2/Spectr Sampler.html`
lines 185–201). This is the authoritative shape.

### 4.3 What Freeze Actually Captures

Freeze produces a **rendered sample** — the processed audio as heard at
the moment of capture, not a re-renderable mask + source pair.

Freeze payload:

- the rendered audio buffer (stereo, plugin sample rate)
- the Spectr state that produced it (`StateStore` blob + `StateTree`
  JSON) — stored for provenance only, not required for playback
- source metadata (track input ref or file path)
- an implicit default root note = note the user played when freezing, or
  `60` if captured outside a MIDI context

Decision confirmed: **rendered sample, not recompute-per-note.** Simpler,
predictable, sounds exactly like what the user heard. The
recompute-per-note option is deferred to Phase 6 as "live spectral
resynthesis," not built here.

### 4.4 Playback Contract

- pitch → linear interpolation or Hermite; SDF/sinc deferred
- velocity → optional gain scale (default on, reasonable curve)
- sustain pedal (MIDI CC 64) → standard hold behaviour
- voice allocation → monophonic by default, polyphonic toggle later
- loop behaviour → sample-accurate at loop boundary when crossfade is 0

### 4.5 Phase 4 Exit Criteria

- User can pick a source, sculpt it in Spectr, press **F**, and play the
  result across the MIDI keyboard from a DAW.
- The frozen buffer survives DAW session reload (requires `#625` or the
  fallback recall contract in V2 handoff §5.5).
- A/B snapshots continue to work in effect mode; freezing from either A
  or B is supported.
- Toggling `frozen = false` returns to live processing without state
  corruption.
- Round-trip test: freeze → save project → reload → playback sounds
  identical.

### 4.6 Phase 4 Non-Goals (Sharp)

- No zone editor.
- No modulation matrix.
- No sample browser.
- No per-note spectral mask recomputation (that is Phase 6).
- No multi-layer playback (that is Phase 5).
- No MIDI learn UI.
- No user-authored envelopes beyond the fade in / fade out / crossfade
  already in the prototype settings.

## 5. Phase 5 — Multi-Slice Instrument

### 5.1 Goal

Turn Spectr's spectral pieces into separately playable layers. This is the
payoff for the effect's "non-contiguous regions are the point" identity.

### 5.2 Product Shape

- Promote each active band (or band group) into its own freeze channel.
- Each layer gets:
  - its own rendered buffer (the band-masked output)
  - its own root key (defaults inherit from parent freeze)
  - its own MIDI routing rule (all, channel, key range)
- Group editing in the V1 effect already provides the band grouping
  primitive — Phase 5 leans on that directly.

### 5.3 Authoring Flow

- User shapes the spectral mask.
- User selects or groups bands.
- Freeze-to-layer turns that selection into one layer.
- Repeat for other groups.
- Play → each layer responds to its own routing rule.

### 5.4 Phase 5 Exit Criteria

- At least three layers can be authored from a single capture.
- Each layer plays back independently on its routing rule.
- Project save/load round-trips all layers including routing.
- No layer audibly degrades the effect-mode sound of the same selection.

### 5.5 Phase 5 Non-Goals

- No per-layer effect chain beyond what V1 effect mode already provides.
- No mid/side layers.
- No automatic sparsity detection or "auto-layer the N loudest bands" ML
  feature.

## 6. Phase 6 — Live Spectral Instrument (Deferred)

### 6.1 Goal

MIDI note-on recomputes the spectral mask from live incoming audio
instead of playing a frozen buffer. This is a research-quality feature,
not a shipping target.

### 6.2 Why This Is Phase 6 And Not Earlier

- Requires a recompute-per-note model — rejected in Phase 4 as too
  complex for a first instrument step.
- Latency and CPU characteristics are not yet characterised.
- The freeze-and-play model already delivers the "sampler identity" —
  Phase 6 is an enrichment, not a product.

### 6.3 Gating

Open Phase 6 only if all of:

- Phase 4 and Phase 5 shipped cleanly and users still ask for live
  spectral resynthesis.
- A DSP spike proves per-note recomputation stays under the
  product-wide latency/CPU budget on representative hardware.
- There is a concrete differentiator against existing spectral
  resynthesis tools. "We also have one" is not enough.

## 7. State Architecture (Sampler-Aware)

The sampler phases do not invent a new state system. They extend the V1
contract:

- **`StateStore` (flat, automatable host-facing):**
  - effect-side params unchanged from V1
  - `sampler_mode` (effect / instrument)
  - `sampler_root_key`
  - `sampler_tune_cents`
  - `sampler_mix` (carried through from effect mode if in instrument mode)
- **`StateTree` (hierarchical, dynamic):**
  - effect-side state unchanged from V1 (layouts, snapshots, patterns)
  - `sampler.source` reference
  - `sampler.direction` / `sampler.loopMode`
  - `sampler.sampleStart` / `sampleEnd` / `loopStart` / `loopEnd` / fades
  - frozen-asset records (buffer IDs, capture metadata)
  - Phase 5 layer table (per-layer root key, routing, buffer ID)
- **Spectr-owned binary assets (side-channel, per project):**
  - rendered buffers for frozen assets
  - persisted via the `#625` supplemental plugin-state path if it lands,
    or via Spectr-owned per-project asset files if it does not

The sampler phases **absolutely depend on `#625`** more than the effect
phases do. Freeze assets are not representable as flat parameter floats.
If `#625` has not landed by Phase 4 kickoff, plan on the Spectr-owned
side-channel file route instead; call that out explicitly in the Phase 4
kickoff decision.

## 8. DSP Dependencies Specific To The Sampler Phases

### 8.1 Pulp built-ins sufficient for Phase 4

- existing FFT / STFT / spectrogram for effect processing
- linear / Hermite interpolation in `core/signal/include/pulp/signal/interpolator.hpp` for initial pitch playback

### 8.2 Third-party lanes likely needed

- `signalsmith-stretch` (MIT) — the moment pitch/time quality matters
  (e.g. playing a buffer two octaves up without chipmunking), this is
  the recommended lane. Already documented in
  `/Users/danielraffel/Code/pulp/docs/guides/packages/signalsmith-stretch.md`.
- `dr_libs` (MIT-0) — for file-source import beyond WAV. Only needed if
  the source picker exposes dropped files. Documented in
  `docs/guides/packages/dr-libs.md`.
- `libsamplerate` or `r8brain-free-src` — only for offline sample-rate
  conversion if captured buffers need to live at a different SR than the
  host.

### 8.3 Third-party lanes not needed until Phase 6

- `pffft` — FFT throughput on a per-note basis.
- `cycfi-q` — pitch-aware triggering.

Do not pull these in before Phase 6.

## 9. Format Considerations

### 9.1 Instrument format targets

When the sampler ships, the same format set expands to include instrument
variants:

- CLAP instrument (fastest lane — reference `examples/PulpSynth/`)
- VST3 instrument
- AU v2 instrument
- Standalone instrument

### 9.2 AU v2 effect MIDI input

The AU v2 effect adapter currently drops host MIDI
(`core/format/src/au_v2_adapter.cpp:252` — `midi_in, midi_out` empty).
**This does not affect effect V1.** It matters for Phase 4 only if the
sampler operates as an AU effect rather than as an AU instrument — which
it should not. The sampler runs as an **instrument**
(`aumu` / `aumf`), not as an `aufx` effect, so the AU v2 effect MIDI gap
is orthogonal.

### 9.3 CLAP controller coverage

Phase 4 needs at least: note on/off, pitch bend, sustain (CC 64), mod
wheel (CC 1, optional). The current CLAP adapter handles note on/off and
sysex only (`core/format/src/clap_adapter.cpp:176`). The in-flight
`feature/clap-midi-cc-coverage` branch is designed to close this — Phase
4 kickoff should confirm that branch landed before starting instrument
work.

## 10. UX Rules (Kept Tight On Purpose)

- The effect surface does not change in instrument mode except for the
  source picker and freeze toggle in the top bar. Users who never freeze
  never see sampler UI.
- The prototype's sampler HTML runs the **exact same** band field,
  viewport, analyzer, and edit modes as the standalone HTML. Preserve that
  visual equivalence — the sampler is not a different product.
- Keybindings: **F** freezes / unfreezes. Same mnemonic as the prototype.

## 11. Success Criteria For The Sampler (Cumulative)

### Phase 4 success

- A user drops a source in, sculpts it, hits F, and plays it. It works.
  Nothing feels like a sampler bolted onto an EQ.
- Frozen sound survives project save / reload.

### Phase 5 success

- A user authors three layers from one source in under a minute.
- The three-layer playback is musically useful on its own — users keep
  the result, they don't re-render it in effect mode.

### Phase 6 success (if ever pursued)

- Per-note spectral resynthesis sounds subjectively different from
  Phase 4 freeze playback on real material, and the CPU budget holds on
  modest hardware.

## 12. Risks Specific To The Sampler Phases

- **Overbuilding toward a general sampler.** Every new control that is
  not in §4.2 or §5.2 should be rejected by default.
- **Freeze UX ambiguity during live playback.** Pressing F while a note
  is held must not click or zipper. Crossfade into the frozen buffer.
- **State-persistence blast radius.** Frozen audio buffers are big.
  Project save/load performance and size must be characterised before
  Phase 4 ships, not after.
- **MIDI voice allocation.** Starting monophonic keeps Phase 4 honest.
  Adding polyphony without a voice-stealing policy is a trap.

## 13. What Phase 4 Kickoff Must Confirm

Before starting Phase 4 work:

1. V1 effect has shipped and is stable.
2. `feature/clap-midi-cc-coverage` has landed (needed for sustain / pitch
   bend).
3. `#625` status is resolved: either landed (preferred route) or
   explicitly deferred to a Spectr-owned side-channel file format for
   frozen assets.
4. A short Phase 4 scoping doc pins which subset of §4.2 is "MVP" and
   which is "production target," because the prototype has richer
   controls than the sampler-ideas "minimal" framing — the build team
   needs a clear line, not two competing design voices.
