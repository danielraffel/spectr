# Spectr Status — Live Handoff Dashboard

_Last updated: 2026-08-11. This is the concise state-of-the-world for
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

The active product branch is `recon/real-analyzer-integration-20260810` at
`e2b565e142e82b05ba2d1ab60d515cf3864b763d`, seven clean commits ahead of
Spectr `origin/main` at `36dfe1bc`. The Release 1 work is committed locally
but has not been pushed, PR'd, or merged. The ignored `native-react/`
experiment is not part of the shipping branch.

## Pulp SDK and fresh build evidence

The latest qualifying local integration SDK was built from Pulp integration
head `8c0d2cd6012e49b92e62427e40d47c6dce3fc443`, including:

- the transport-less VST3 host fix — hosting no longer fabricates a frozen
  playing/sample-zero process context;
- the inspector-off CLI fix — the development SDK links while preserving
  explicit live-control-unavailable behavior; and
- the realtime visualization bridge — audio callbacks perform only bounded,
  allocation-free capture while the UI thread owns FFT publication.

Exact immutable SDK identity:
`8c0d2cd6012e49b92e62427e40d47c6dce3fc443/3d9a0ae9f29c`, installed at
`~/.pulp/sdk-dev/forge-v1/darwin-arm64/8c0d2cd6012e49b92e62427e40d47c6dce3fc443/3d9a0ae9f29c`.

The fresh arm64 Release build proves `-O3 -DNDEBUG` and:

- 73,219 assertions across 124 native product cases;
- 128/128 complete CTest cases, including the deterministic Chromium editor
  oracle and real built CLAP/VST3 artifact hosting;
- exact-mute, upper/lower viewport rejection, and three nonadjacent stereo
  frequency-island projection checks;
- the exact packaged Standalone headless GPU/no-audio-device boundary;
- CLAP validation with zero applicable failures and `auval` success; and
- ad-hoc deep-signed AU, VST3, CLAP, and Standalone arm64 trial artifacts.

This is integration evidence, not a landed-Pulp or released-SDK claim. The
Pulp fixes must land and Spectr must be rebuilt against their authoritative
published development SDK before the product PR.

## Installed host matrix

| Surface | Recorded result | Remaining qualification |
|---|---|---|
| AUv2 | `auval` passes `aufx:Spec:Pulp`, including render, latency, state, custom UI, channel, and sample-rate checks. Logic Pro 12.3 discovers the current AU, opens the live editor, processes audio, accepts band painting, and displays the live post-effect analyzer. The initial musical trial is positive. | Repeat mute plus close/reopen twice with no non-finite banner; verify Logic Musical Typing does not trigger Spectr shortcuts; manual 800x521 sizing gate. |
| VST3 | The real built-bundle host test passes audio/state/exact-mute and the three-island stereo projection. | Current authoritative-SDK REAPER GUI/state/resize/bypass/PDC trial; pluginval is unavailable and is not claimed. |
| CLAP | The real built-bundle host test passes audio/state/exact-mute and the three-island stereo projection. CLAP validator has zero applicable failures. | Current authoritative-SDK REAPER GUI/state/resize/bypass/PDC trial; shared manual 800x521 gate. |
| Standalone | The packaged headless boundary test passes. The plot region is pixel-identical between the post-fix 120- and 600-frame captures; the settled capture restores the plot, rulers, spectrum, mask, minimap, and resolution disclosure. A visible launch opens stereo CoreAudio at 48 kHz / 256 samples and the 1320x860 native WebView editor. | Manual native edge dragging at the declared 800x521 minimum remains open because the app window is not exposed through the available accessibility control. |

The Logic result above is a current human trial, not inferred from metadata.
Prior REAPER discovery trials proved format visibility, but the final REAPER
matrix remains open until it is repeated with artifacts rebuilt from landed
Pulp dependencies.

## Remaining Release 1 gates

1. Land Pulp realtime visualization PR #7399, then the inspector-off CLI and
   transport-less VST3 host fixes; publish/select the resulting authoritative
   development SDK.
2. Rebuild all four Spectr formats against that SDK and rerun native,
   Chromium, artifact, validator, and CLI-first audio gates.
3. Complete the current Logic reopen/Musical Typing/minimum-size checks, the
   final REAPER VST3/CLAP matrix, real Standalone audio I/O, and the declared
   800x521 / 1320x860 / 2640x1720 size matrix.
4. Run the exact semantic audio gates before Audio Quality Lab regression
   comparisons; Quality Lab is advisory for perceptual change, not the oracle
   for exact mute, leakage, islands, or latency.
5. Push the reviewed Release 1 commits, open the product PR, and attach Whence
   provenance to the committed canonical goal:
   `pulp-planning-spectr/research/spectr-dsp-gap.md` on planning `main`.
6. Merge only after required checks and the final diff/provenance review pass.

## Release 2 product direction

Before expanding the product surface, Spectr has a renderer-modernization
phase. The completed WebView editor will first be landed as an immutable,
reproducible reference build. A separate worktree/branch will then implement a
parallel native editor through Pulp's DesignIR/View tree and Skia/Dawn. The two
lanes will share DSP, state, analyzer, preset, undo, automation, modulation, and
interaction contracts and will be compared using the same fixtures and host
matrix. Native becomes the default only after visual, behavioral,
accessibility, persistence, host, and performance gates pass; the WebView
baseline remains available for rollback and regression comparison. The
executable phases and cutover criteria live in
`planning/Spectr-Cutover-Gap-Tracker.md`.

The native migration is also a Pulp importer qualification project: gaps found
through Spectr should become reusable native-import/runtime capabilities rather
than accumulating as a Spectr-specific rewrite. The destination is a browser-
free editor rendered through Skia/Dawn, while preserving the source and
behavioral evidence supplied by the Claude Design prototype.

Release 2 remains effect-centric: add stereo, multi-layer freeze, sampling,
and polyphonic MIDI-keyboard playback to the same effect. An instrument
variant is a contingency only if the supported host matrix proves that a
single effect identity cannot reliably combine audio input, MIDI input,
freeze capture, recall, and keyboard playback. No Release 2 freeze or sampler
implementation is claimed by the current branch.

## References

- Canonical active goal:
  <https://github.com/danielraffel/pulp-planning/blob/main/research/spectr-dsp-gap.md>
- Product contract: `planning/Spectr-V2-Product-Spec.md`
- Pulp handoff: `planning/Spectr-V2-Pulp-Handoff.md`
- Future freeze/sampler scope: `planning/Spectr-Sampler-Phase-Spec.md`
