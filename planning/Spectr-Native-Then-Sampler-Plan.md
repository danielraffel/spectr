# Spectr — Native Then Sampler Execution Plan

Status: Ready for execution
Date: 2026-08-11

## Objective

First replace Spectr's shipping WebView editor with a qualified native Pulp
editor rendered through Skia/Dawn. Then add the Spectral Sampler to that native
product, including freeze capture and chromatic keyboard playback.

Do not mix the two projects. Native parity is accepted before sampler product
work begins.

## Phase 0 — Preserve the reference build (complete)

- Immutable WebView implementation baseline:
  `5486b009d7431ef3a28257500613f5d3c9371d25`.
- Keep its Release tests, screenshots, Pulp provenance, and AUv2/VST3/CLAP/
  Standalone artifact hashes as rollback and comparison evidence.
- Never rewrite or force-update this baseline while implementing native UI.

## Phase 1 — Native Skia/Dawn editor

Follow `Spectr-Cutover-Gap-Tracker.md` phases N0–N5:

1. Run an Ultra architecture/import audit of the preserved Claude Design source.
2. Create a separate worktree and native-renderer branch.
3. Use DesignIR/browser capture only as structural and visual audit evidence.
   Canonicalize frozen original-plus-adapter behavior into stable source, then
   run it live in QuickJS through `@pulp/react` native Views and CanvasWidgets.
4. Render through Skia Graphite/Dawn without WebView, WKWebView, an embedded
   browser, screenshot-backed controls, or a hidden fallback. Require the GPU
   backend and fail closed if it is unavailable.
5. Reuse the same DSP, state, analyzer, preset, undo, automation, modulation,
   A/B, morph, and interaction contracts as the WebView baseline.
6. Generalize missing widgets/import/runtime behavior in Pulp rather than adding
   one-off Spectr importer patches.
7. Keep WebView and native builds runnable and drive both with the same visual,
   interaction, host, state, and performance fixtures.
8. Make native the default only after the complete N5 gate passes. Retain the
   WebView baseline for rollback and regression comparison.

The first accepted native slice must already cross the real product boundary:
finite C++ hydration, a real analyzer frame, a real 32-band canvas, tap-to-mute,
and sculpt-drag must round-trip through revisioned authoritative state. An inert
native shell or static screenshot does not satisfy Phase 1.

## Phase 2 — Spectral Sampler on the native editor

After native cutover acceptance, follow `Spectr-Sampler-Phase-Spec.md`:

1. Keep the primary Spectr editor and identity; add the compact source selector
   and `LIVE` / `FROZEN` state control shown by the sampler prototype.
2. Let the user choose only sources the runtime can truthfully expose: main
   input, configured sidechain/aux buses, loaded files, Standalone device
   inputs, or explicitly host-provided descriptors. The plugin must not invent
   an arbitrary DAW track list.
3. `LIVE` processes the incoming source through Spectr. MIDI notes do not
   trigger sample playback in this state.
4. `FROZEN` captures the processed result into a persistent sample buffer and
   is the only state in which MIDI notes can trigger sampler voices.
5. Preserve the frozen buffer, source metadata, Spectr state provenance, sampler
   controls, and keyboard mapping across project save, close, and reopen.
6. Make the frozen sample playable from a MIDI keyboard using chromatic mapping:
   - keyboard playback is hard-gated by `FROZEN`; returning to `LIVE` releases
     active sampler voices click-free and disables new sample-note triggers;
   - one configurable root note plays the sample at its original pitch;
   - each MIDI note above the root raises playback by one semitone per key;
   - each MIDI note below the root lowers playback by one semitone per key;
   - playback-rate ratio is `2^((note - root_note) / 12)` before any optional
     fine-tune offset;
   - note-on starts a voice, note-off releases/stops it according to the sampler
     envelope contract, velocity controls gain, and sustain pedal is honored;
   - initial delivery is monophonic if necessary for correctness, followed by a
     separately tested polyphonic voice allocator.
7. Add root key, fine tune, forward/reverse, sample start/end, loop mode,
   loop start/end, fades, and loop crossfade as specified by the prototype.
8. Switching between live and frozen audio must be click-free and must preserve
   source-time integrity—no dropped, duplicated, reordered, or stale samples.

## Phase 3 — Sampler acceptance

Sampler work is complete only when:

- a captured reference tone plays at its original pitch on the root key and at
  measured equal-tempered semitone ratios across the supported keyboard range;
- octave checks are exact within tolerance: root + 12 plays at 2x rate and root
  - 12 at 0.5x rate;
- note-on, note-off, velocity, sustain, voice stealing, reverse, regions, loops,
  fades, and crossfades pass deterministic audio tests;
- freeze → save project → close → reopen → play is audio-equivalent;
- 44.1, 48, 88.2, 96, and 192 kHz sessions pass capture, playback, pitch, loop,
  and persistence tests;
- AUv2, VST3, CLAP, and Standalone pass the applicable MIDI/audio/UI/state host
  matrix; and
- the sampler remains native Skia/Dawn with no reintroduced WebView dependency.

## Execution rule

Complete and review each phase before opening the next. Framework gaps land in
Pulp with independent tests; product behavior lands in Spectr. Do not declare
native cutover or sampler completion from screenshots alone—require audio,
state, interaction, host, and performance evidence.
