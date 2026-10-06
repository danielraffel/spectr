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

## Workload and UI direction

Spectr has two separate resolutions. The processing resolution is the FFT/bin
grid; the editing resolution is the number of control bands drawn in the
editor. The current product profile is an 8192-point FFT with a 2048-sample
hop and 32--64 editable bands. More editable bands must not be presented as
an FFT quality setting, and increasing the FFT size does not require exposing
more hit targets in the editor.

The shared renderer is currently selected for the `linear_phase` (Mixing)
path. Tracking remains the low-latency CPU path. This is intentional: the
first useful GPU opportunity is more parallel work with explicit latency
budget, not replacing a small CPU operation with a GPU submission.

The preferred product policy is automatic selection. A future policy can keep
Tracking on the CPU and select the shared GPU path for Mixing, larger FFT
profiles, multiple spectral layers, or long Freeze work when the provider is
ready. CPU processing remains continuously prepared and owns a block whenever
GPU output is late or unavailable. The UI should report the selected engine
and those outcomes; it should not imply that a GPU label guarantees that every
part of the plugin ran on the GPU.

Forced CPU/GPU selection remains useful for developer A/B runs, but belongs
behind an experimental control until a real backend mode-setting seam exists.
The compact status surface may be hidden independently through the GPU stats
setting. Build information remains the place for provenance and detailed
diagnostics.

Native package note: the installed editor executes
`native-ui/materialized/materialized-document.runtime.json`, not
`resources/editor.html`. The GPU controls in the native package are maintained
by `tools/patch_materialized_gpu_audio_ui.py`; the corresponding
`Spectr-gpu-audio-materialized-ui` test scans the embedded runtime directly.
GPU-enabled package builds must also configure
`SPECTR_EXPERIMENTAL_SHARED_RENDERER=ON`. A green browser-source test alone is
not evidence that the native package contains the controls.

Before adding a high-band-count control, measure the existing 64-band editor
with progressively heavier workloads. The first matrix is 8192-point CPU,
8192-point GPU, and 16384-point GPU, followed by multiple 16384-point layers
and longer Freeze holds. A 32768-point profile is a separate future build
experiment because the current supported profile list ends at 16384. Each
step needs both an acoustic reason to exist and realtime evidence: output
parity, CPU/GPU work time, p99.9 deadline behavior, fallback rate, and the
effect of simultaneous rendering load.

## GPU processing: a Mixing choice, off by default

GPU processing is a saved, non-automatable session setting that selects
Mixing's renderer: on, Mixing renders through the shared GPU renderer; off
(the default), through the CPU linear-phase renderer. Tracking always runs on
the CPU minimum-phase renderer, and the header chip there is a disabled
"CPU" readout. In Mixing the chip is the CPU/GPU toggle; Settings > Latency
carries the same switch with both latency figures. Switching it rebuilds the
renderer through the same path as a Tracking/Mixing switch: one
latency-changed notification, the new figure reported before the host reads
it, bounded output, no crossfade.

Reported latency, measured (impulse onset dry, noise cross-correlation peak
wet, in-process and through the built AU, VST3 and CLAP, at 44.1 and 48 kHz,
blocks 128 and 512): Tracking 64 samples, Mixing on the CPU 10240 samples
(213 ms at 48 kHz), Mixing on the GPU 15360 samples (320 ms at 48 kHz). The
GPU figure is constant: a block the GPU does not deliver in time is rendered
by the CPU path at the same alignment, so a fallback never changes the
latency. The latency chip, Settings, the hydration payload and build
information all show the figure the host is told.

What GPU processing does and does not buy today:

- Sound: none. GPU output equals the CPU linear-phase output to within
  1.2e-7 (-138 dBFS), and a fallback block is that CPU output.
- CPU: none, today. The CPU path runs every block so it can stand in for a
  late GPU result, so the host thread does the CPU renderer's work either way,
  and the GPU service worker adds roughly 8 to 12 percent of a core for the
  process. It is an architecture for later work (larger FFT profiles, more
  spectral layers, long Freeze holds) once the CPU stand-in no longer has to
  run in full every block.
- Latency: 5120 samples more than Mixing on the CPU.

Fallbacks: in a paced session (steady, transport reset, all cores loaded, a
late host, a band edited every block) none were observed. They come from
callbacks arriving faster than the GPU round trip: an offline bounce or a
host that renders ahead (148 of 188 quanta fell back in a 4-second
as-fast-as-possible run), and the first quantum or two after the renderer is
rebuilt. They are not audible: the substituted block is the same-latency CPU
rendering of the same input. `Spectr-shared-spectral-fallback-trace` prints
every fallback with its phase and fence reasons.
