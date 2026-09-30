# Spectr GPU-audio host-GPU SDK validation

- Worktree: `/private/tmp/spectr-gpu-audio-chain-latest-20260928-refresh`
- Spectr branch: `codex/gpu-audio-validation-latest-refresh`
- Spectr HEAD: `a1570e9`
- SDK prefix: `/private/tmp/pulp-sdk-exact-host-gpu-prefix-20260928`
- Pulp SDK source SHA: `c9c785f6cf81dba0f1eeae43f4a00037725523c0`
- SDK package: Pulp `0.877.2`, Release build, development provenance
- `distribution_eligible`: `false`
- SDK provenance SHA256: `a1e22e9fd1b55a898892231eed44e9224c8664f8880ede0d41e9ea9939f20ed6`
- Forge catalog SHA256: `5cfb95e456da36d601ba7a78fedb2779bfedb3e9bcfd2dd03e5450716ddab577`
- Build-info SHA256: `6fcd3cef5c3ccc7b59368654662476991016c6f0014537a43277511877cf7091`

Configured with exact SHA validation, `SPECTR_SHARED_SPECTRAL_FIXTURE=ON`, and
`SPECTR_GPU_AUDIO_PERF_FIXTURE=ON`. The SDK exposes the GPU-audio transport,
spectral-mask, audio-program, and STFT headers, plus `Pulp::gpu-audio`; the
Forge catalog is installed.

Focused build targets passed. CTest command:

```
ctest --test-dir /private/tmp/spectr-gpu-audio-chain-build-host-exact-20260928 \
  -R 'Spectr-(shared-spectral|gpu-stft)' --output-on-failure
```

Result: 11/11 passed in 18.54 s: staged GPU STFT baseline and rejection controls;
shared spectral fixture, partitions, controls, renderer, mix, host inventory,
trace validator/accounting, and reset.

This is exact-source SDK consumer correctness and capability evidence. It does
not establish Spectr product audio integration, realtime scheduling, deadline
reliability, p50/p99 audio timing, or GPU speedup. Spectr shipping audio remains
CPU-default; shared spectral execution remains opt-in.
