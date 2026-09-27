# Spectr GPU-audio contention fixture

Status: opt-in staged GPU STFT baseline plus receipt contract. Spectr product
audio remains CPU-only. No shared-memory spectral provider is integrated here;
the baseline does not establish realtime or contention behavior.

Spectr already captures native Skia/Graphite/Dawn work through `PULP_TRACE_PATH`.
The existing `verify_interaction_perf.sh` workloads remain the graphics
baseline. A future shared GPU-audio fixture must run in that same traced host
process, so audio and graphics events share one Perfetto clock and one
evidence receipt. Launching a second `pulp gpu probe` process would create a
separate trace and cannot establish contention.

The lifecycle identity is `(stream_epoch, block_sequence)`. Providers should
emit these static event names using Pulp's
`PULP_TRACE_SCOPE_NAMED_ARGS("audio", name, ...)` macros:

* `gpu_audio_admission`
* `gpu_audio_submit`
* `gpu_audio_completion`
* `gpu_audio_terminal`

Every event carries `stream_epoch` and `block_sequence`; terminal events also
carry `terminal_disposition`. Allowed dispositions are `gpu_delivered`,
`cpu_fallback`, `silence`, `stale_rejected`, `late_rejected`, `device_lost`,
and `cancelled`. Every admitted block must have exactly one terminal event.
`tools/gpu_audio_contention_trace.py` validates an exported JSONL receipt
against that invariant. It is intentionally receipt-side and does not
synthesize a Perfetto trace.

Once Pulp exposes an authenticated shared provider, add an opt-in
`SPECTR_GPU_AUDIO_PERF_FIXTURE` to the existing `SPECTR_ENABLE_PERF_FIXTURES`
build. The fixture should prepare all resources before the callback, run
explicit lead values (1, 2, 4, and 8 blocks), keep a continuously prepared
CPU fallback, and emit the same identity through admission, submission,
completion, and terminal disposition. Extend the existing analyzer query to
report audio deadline misses and graphics frame tails from the same trace.
Keep the fixture disabled in shipping builds.

## Staged spectral baseline and the missing shared execution seam

`Spectr-gpu-stft-baseline` runs 16 GPU analysis/inverse-FFT frames at FFT 1024,
hop 256, then applies synthesis windows and overlap-add on the CPU. Its oracle
is independent of the GPU FFT: windowed input times the synthesis window,
accumulated with the same overlap normalization. Seeded noise exercises all
bins. Missing hardware returns CTest skip code 77, never a passing GPU receipt.
An unauthenticated or non-Metal adapter fails this Apple-specific baseline.

The current public `GpuStft` API blocks on CPU readback; `GpuAudioProgramKind::Spectral`
is descriptor metadata, not a shared-memory spectral executor. Accordingly the
receipt reports `staged_readback` and `shared_memory_proven: false`. The requested
lead matrix [1,2,4,8] is explicitly untested. The CPU identity oracle is available
as the reference substitute, but callback fallback is not exercised. Do not use
this baseline to claim a product WOLA implementation or scheduling improvement.

After configuring with `SPECTR_GPU_AUDIO_PERF_FIXTURE=ON` and the exact installed
Pulp prefix, build explicitly through the governed wrapper:

```sh
/Users/danielraffel/Code/pulp/tools/ci/governed-build.sh cmake --build BUILD --target Spectr-gpu-stft-baseline --parallel 2
ctest --test-dir BUILD -R '^Spectr-gpu-(stft-baseline|audio-contention-contract)$' --output-on-failure
```

Provisional build-tree run on 2026-09-27: authentic Metal adapter, all 16 frames
completed, maximum normalized absolute error `1.33617e-07` (limit `1e-4`). The
standalone build used PR #8918's existing archive; installed-SDK CMake validation
remains outstanding. Logs and exact compile command are in
`/tmp/spectr-stft-baseline-evidence-20260927`. This is correctness evidence only.

The next implementation must supply a persistent shared spectral executor and
connect Spectr's mask operation, CPU WOLA history/fallback, and fixed-hop timeline
to it before lead or contention tests can answer the product question.
