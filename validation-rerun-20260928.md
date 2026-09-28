# Spectr GPU-audio focused rerun

- Worktree: `/private/tmp/spectr-gpu-audio-chain-latest-20260928-refresh`
- Branch: `codex/gpu-audio-validation-latest-refresh`
- Spectr HEAD: `a1570e98de6665f81fa39a93c5e00ea24d136b55`
- `origin/main`: `d32c98548a4d2d3dafc345e232a48887c3313296`
- Relationship: branch is 2 commits ahead and 0 behind; no newer upstream Spectr changes were available at validation time.
- Pulp SDK source SHA: `c9c785f6cf81dba0f1eeae43f4a00037725523c0`
- SDK prefix: `/private/tmp/pulp-sdk-exact-host-gpu-prefix-20260928`
- SDK package: Pulp `0.877.2`, Release, development provenance (`distribution_eligible: false`)
- Build directory: `/private/tmp/spectr-gpu-audio-chain-build-host-exact-20260928`

Command:

```sh
ctest --test-dir /private/tmp/spectr-gpu-audio-chain-build-host-exact-20260928 \
  -R 'Spectr-(shared-spectral|gpu-stft)' --output-on-failure
```

Result: 11/11 passed in 15.95 seconds. This covers the staged GPU STFT baseline and rejection controls, shared spectral fixture, partitions, controls, renderer, mix, host inventory, trace validator/accounting, and reset.

This is a repeat of the exact-source SDK consumer correctness/capability check. It does not establish product realtime behavior, deadline reliability, speedup, or package readiness. Spectr remains CPU-default and GPU execution remains opt-in on this branch.
