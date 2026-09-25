# Spectr GPU-audio contention fixture

Status: receipt contract only. No GPU-audio provider is integrated here and
this fixture does not claim realtime or contention evidence.

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
