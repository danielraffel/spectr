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

## Pulp implementation now in progress

The prerequisite has an isolated implementation at
`/tmp/pulp-shared-spectral-sdk-20260927`, branch
`feat/shared-spectral-sdk-20260927`, initial commit `8fc7c916c8`.
It supplies `GpuSpectralMaskSession` with persistent GPU history/OLA and imported
slots. Provisional new-source CPU-oracle parity passed on real Metal; production
SDK rebuild/install and expanded lifecycle acceptance are still open. The
installed c17fc3 prefix used by this Spectr branch does not yet include that API.
Do not switch Spectr to shared mode until the new installed consumer is proved.

## Active callback/worker consumer slice

Owner sdk_buildtree_probe now implements a fixture-only bridge with the real
Spectr LinearPhaseMaskRenderer as continuously advanced CPU fallback. Public
GpuSpectralMaskSession worker uses fixed immutable masks; live control changes
are refused. Host-block lead is explicit (1/2/4/8), distinct from hop assembly
and intrinsic FFT+hop latency. Fixed ingress/output storage, bounded callback,
late-output rejection and GPU fencing on missing input are required. Trace
records carry stream epoch/block sequence/admission and one terminal outcome.
Quiescent reset must rebuild both CPU/GPU state and allocate a new epoch.

Provisional builds may link new Pulp source at 1ad61b59b0 plus existing c17fc3
archives, clearly labeled. Parent owns combined production SDK build; installed
consumer is still a separate gate.

## Shared consumer correctness result

First provisional callback/worker run passed on M5. FFT1024/hop256 stereo,
64-frame callbacks, 160 admitted inputs at each lead1/2/4/8. Maximum absolute
CPU-reference error5.96046e-08. GPU delivery counts36/72/144/149; fallback
counts124/88/16/11. Worker-stopped overflow run produced160 CPU fallback blocks,
zero error and64 ingress admissions. All normal drained terminal sequences were
unique/complete; reset changed epoch. Runtime WebGPU transfer-call counts were
zero. Driver waits make these **correctness**, not realtime/performance results.
Logs `/tmp/spectr-shared-consumer-provisional-20260927/{runtime.log,terminal.jsonl}`.

Implementation is now reusable `spectr::experimental::SharedSpectralBridge` in
source-owned include/src paths, exposed through opt-in `SpectrSharedSpectralBridge`
library. The test remains a thin consumer. Usage/lifecycle limits are in
`docs/experimental-shared-spectral.md`. Shipping selection remains unchanged.
Installed SDK and product adapter are open. Abrupt-stop cancellation and trace
overflow controls are implemented; the expanded provisional run is pending.

Expanded reusable-bridge build/run passed (`build-v3.log`, `runtime-v3.log`,
`terminal-v3.jsonl` in the same evidence directory). Abrupt release and reset each
preserved8 unique outcomes including4 cancellations. Full trace-ring control
produced4096 retained records plus4 explicitly lost records; loss survives reset
and invalidates trace completeness. Repeated release produced no duplicates.
Lead1/2/4/8 parity remained5.96046e-08; delivery counts36/72/144/148 are not timing
measurements. Overflow path remained exact CPU parity. Runtime exit0.
