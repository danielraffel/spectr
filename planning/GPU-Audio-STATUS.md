# GPU audio execution status

Owner: sdk_buildtree_probe; worktree `/tmp/spectr-gpu-audio-validation-20260927`.

Current slice: harden staged STFT baseline. Preparation failure is checked; all
frame/output/window/oracle/weight values must be finite; only fully overlapped
interior samples participate in numerical comparison. Receipt gives exact
comparison range. Negative controls inject startup/interior/tail NaNs into
output, oracle and weights; overflow residual, zero weight and wrong finite
output must also fail. These controls do not require a GPU.

Shared spectral execution is **blocked on a Pulp SDK implementation**, not on
Spectr build tooling. Installed SDK source `c17fc3c` exposes blocking/readback
`GpuStft` and a metadata-only Spectral program kind. It has no public shared
spectral session. `GpuWaveNetSession` is neural-specific, not an interchangeable
FFT executor. Private provider headers are not installed and must not become a
Spectr dependency.

Next owner: Pulp GPU-audio provider/session implementation. Concrete design:
`/tmp/spectr-stft-baseline-evidence-20260927/shared-spectral-next-step.md` and
pulp-planning `research/2026-09-27-shared-spectral-next-step.md` (PR #279).
Required seam: persistent input history, FFT/mask/inverse/OLA/normalization and
final shared output slots, with contiguous hops, physical retirement, CPU
history/fallback parity and immutable per-hop control snapshots.

After SDK seam lands, connect opt-in `LinearPhaseMaskRenderer` sibling to a
non-RT submission worker and bounded callback bridge. Retain CPU WOLA default,
oracle and continuously advanced fallback. Explicitly distinguish host-block
lead from spectral-hop lead. No shared-memory or realtime benefit claimed now.

Verification: governed explicit build and focused CTest results are recorded
under `/tmp/spectr-stft-baseline-evidence-20260927/hardening-*`.
