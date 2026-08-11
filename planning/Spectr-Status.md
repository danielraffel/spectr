# Spectr Status — Live Handoff Dashboard

_Last updated: 2026-08-10. This is the concise state-of-the-world for
Spectr. Refresh it whenever the product branch or its landing gates change._

## Release 1 product state

Release 1 is a zoomable spectral-isolation effect:

- 32, 40, 48, 56, or 64 logarithmic bands over a continuously adjustable
  frequency viewport;
- nonadjacent frequency islands and categorical mute, with muted mask bins
  written as exact zero;
- Pulp's shared `SpectralMaskProcessor` WOLA engine, using the Balanced
  8192-sample FFT / 2048-sample hop profile by default and reporting 10,240
  samples of latency;
- the reviewed Claude Design HTML embedded source-preservingly in a native
  Pulp WebView, with C++ owning audio, state, and host integration;
- AUv2, VST3, CLAP, and Standalone arm64 artifacts. No PKG, signing,
  notarization, or public distribution claim is part of this local M5 trial.

The active product branch is
`feature/release1-isolation-clean-20260809`, based on Spectr
`origin/main` at `36dfe1bc`. The Release 1 changes are not yet committed,
PR'd, or merged. The ignored `native-react/` experiment is not part of the
shipping branch.

## Pulp SDK and fresh build evidence

The qualifying development SDK was built from Pulp fixes:

- `2da347e8b` — transport-less VST3 hosting no longer fabricates a frozen
  playing/sample-zero process context;
- `30fe16c63` — the inspector-off development SDK links the CLI while
  preserving explicit live-control-unavailable behavior.

Exact immutable SDK identity:
`30fe16c630039ea9bd3c5762e951be73328759f8/70a1a7ef1a30`, installed at
`~/.pulp/sdk-dev/forge-v1/darwin-arm64/30fe16c630039ea9bd3c5762e951be73328759f8/70a1a7ef1a30`.

The fresh arm64 Release build proves `-O3 -DNDEBUG` and:

- 72,345 assertions across 119 in-process product cases;
- 2,084 assertions across the real built CLAP and VST3 artifact cases,
  covering load, processing, 10,240-sample latency, state restore, wet audio,
  and sample-exact stereo silence for an authored all-muted state;
- 9 assertions for the exact packaged Standalone headless GPU/no-audio-device
  boundary;
- 122/122 complete CTest cases;
- self-contained AU, plus VST3, CLAP, and Standalone artifacts.

## Installed host matrix

| Surface | Recorded result | Remaining qualification |
|---|---|---|
| AUv2 | `auval` passes `aufx:Spec:Pulp`, including render, latency, state, custom UI, channel, and sample-rate checks. Pulp's CLI harness rendered four seconds of stereo 48 kHz audio. Logic Pro 12.3 discovers the AU, opens the live editor, processes audio, accepts band painting, and preserves the curve across editor close/reopen. | Shared manual 800x521 native sizing gate. |
| VST3 | The real built-bundle host test passes audio/state/exact-mute. REAPER 7.78 discovers it, opens the full live editor, and reports 10,240 samples of latency. Its initially white embedded frame repaints after WebView/GPU attachment settles. | Pluginval is unavailable on this machine and is not claimed; shared manual 800x521 gate remains. |
| CLAP | The real built-bundle host test passes audio/state/exact-mute. CLAP validator passes all 15 applicable tests with zero failures; five unsupported preset/note-port cases are skipped. REAPER 7.78 discovers it, opens the full live editor, and reports 10,240 samples of latency. | One non-fatal 797 ms scan warning; shared manual 800x521 gate remains. |
| Standalone | The packaged headless boundary test passes. The plot region is pixel-identical between the post-fix 120- and 600-frame captures; the settled capture restores the plot, rulers, spectrum, mask, minimap, and resolution disclosure. A visible launch opens stereo CoreAudio at 48 kHz / 256 samples and the 1320x860 Metal/Skia editor. | Manual native edge dragging at the declared 800x521 minimum remains open because the custom GPU window is not exposed through the available accessibility control. |

Logic and REAPER are real recorded trials, not inferred from metadata. REAPER
also loaded VST3 and CLAP together, reporting 20,480 samples for the two-plugin
chain while retaining independent band states.

## Remaining Release 1 gates

1. Manually resize the visible native editor to 800x521 and confirm
   proportional layout, edge dragging, menus, and band painting.
2. Land the two qualifying Pulp fixes and rebuild against their landed SDK
   identity if the merge SHA changes the installed surface.
3. Stage only the reviewed Release 1 files, commit the Spectr branch, open the
   product PR, and attach Whence provenance to the committed canonical goal:
   `pulp-planning-spectr/research/spectr-dsp-gap.md` on planning `main`.
4. Merge only after required checks and the final diff/provenance review pass.

## Release 2 product direction

Release 2 remains effect-centric: add stereo, multi-layer freeze, sampling,
and polyphonic MIDI-keyboard playback to the same effect. An instrument
variant is a contingency only if the supported host matrix proves that a
single effect identity cannot reliably combine audio input, MIDI input,
freeze capture, recall, and keyboard playback. No Release 2 freeze or sampler
implementation is claimed by the current branch.

## References

- Canonical active goal:
  `pulp-planning-spectr/research/spectr-dsp-gap.md`
- Product contract: `planning/Spectr-V2-Product-Spec.md`
- Pulp handoff: `planning/Spectr-V2-Pulp-Handoff.md`
- Future freeze/sampler scope: `planning/Spectr-Sampler-Phase-Spec.md`
