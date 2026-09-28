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
