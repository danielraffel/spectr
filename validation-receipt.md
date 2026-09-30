# Spectr GPU-audio exact-SDK validation

- Worktree: `/private/tmp/spectr-gpu-audio-chain-latest-20260928-refresh`
- Spectr branch: `codex/gpu-audio-validation-latest-refresh`
- Spectr HEAD: `a1570e9`
- SDK prefix: `/private/tmp/pulp-sdk-exact-m3-prefix-20260928`
- Pulp SDK source SHA: `c9c785f6cf81dba0f1eeae43f4a00037725523c0`
- SDK package: Pulp `0.877.2`
- Build: Release; `SPECTR_SHARED_SPECTRAL_FIXTURE=ON`; `SPECTR_GPU_AUDIO_PERF_FIXTURE=ON`

Configure, focused build, and eleven focused CTest cases all passed:

```
ctest --test-dir /private/tmp/spectr-gpu-audio-chain-build-20260928 \
  -R 'Spectr-(shared-spectral|gpu-stft)' --output-on-failure
```

Passed: staged GPU STFT baseline and rejection controls; shared spectral fixture,
partitions, controls, renderer, mix, host inventory, trace validator, trace
accounting, and reset. Total: 11/11, 18.49 s.

This validates the exact SDK consumer compile/runtime correctness and trace/control
contracts. It does not establish Spectr product audio integration, realtime
scheduling, deadline reliability, p50/p99 audio timing, or a GPU speedup. The
shipping product remains CPU-default; the shared spectral path is opt-in.

## Public Pulp 0.878.0 compatibility smoke (September 28, 2026)

A separate Release build used the freshly installed public Pulp `0.878.0`
prefix from source `ed878f2a5cce38111f29df53b39b639aa72c84af`:

- Pulp prefix: `/private/tmp/pulp-sdk-public-0.878.0-20260928`
- `Spectr_Standalone`: configured and linked successfully
- GPU-enabled Pulp/Skia/Dawn targets: resolved successfully
- Relocatability checks: completed successfully

This is a source/SDK compatibility smoke, not a replacement for the exact
provenance-marked `0.877.2` acceptance receipt above. The public prefix was a
local Release install and is not distribution-eligible until its provenance
marker and package gates are produced.
