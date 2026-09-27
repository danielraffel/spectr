# Reading shared GPU audio status

Spectr's existing About/build-information response and Copy action include GPU
audio delivery observations. This works without tracing or the private native
host probe. It is read-only; requesting status cannot switch modes, stop audio
or enable a provider.

`build_info_get` returns a `gpu_audio` object with `schema_version: 1`.
When available, its counters come from the shared renderer's terminal drain:

- `gpu_selected`: internal quanta selected from GPU output.
- `cpu_fallback`: internal quanta supplied by the continuously advanced CPU path.
- `cancelled`: terminal cancellations.
- `lost_terminal_records`: records the terminal ring could not retain.
- `current_epoch`: the latest service-observed logical epoch.
- `provider_state`: unprepared, shared_ready, cpu_only, fenced,
  release_unconfirmed, or unknown.

Counters and epoch use decimal strings to preserve all64 bits in JavaScript.
`sampling` is `independent_live_counters`; `count_scope` is
`renderer_preparation_before_mix_trim`. These are cumulative renderer
observations, not a coherent snapshot for the current epoch. They describe
selection before Spectr's final mix and trim, and their display can lag the
audio callback until the worker drains records. A ready provider or a requested
mode does not imply GPU-selected output.

When unavailable, `available` is false and `reason` is `not_built`,
`not_prepared`, `non_shared_renderer`, or `snapshot_unavailable`. Counter fields
are absent rather than zero. A normal build without the experimental shared
renderer reports not_built. An experimental build still defaults to
zero_latency; select linear_phase to exercise the shared renderer.

For a real-host check, capture the same loaded plugin's identity and status
before and after a stable, nontrivial wet run. Require a positive GPU-selected
delta, retain fallback and loss values, and independently compare the audio.
Preparation may prime DSP history, so a pre-existing positive count is not
proof that a later host interval used GPU output. Do not compare across
prepare, release or renderer replacement. A positive count does not establish
GPU execution time, deadline reliability, final lifecycle conservation or a
speed advantage. Mix0 alone does not prove the GPU result reached the audible
output.

The existing Copy action includes the same interpretation alongside product
and SDK provenance. Native and legacy editor bridges use the same projection;
no additional network or diagnostic ABI is exposed.
