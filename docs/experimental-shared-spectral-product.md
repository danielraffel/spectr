# Opt-in Spectr shared renderer

`SPECTR_EXPERIMENTAL_SHARED_RENDERER=ON` selects the shared spectral adapter for
native Spectr's existing linear-phase mode. It is OFF by default. The saved mode
values and normal CPU factory are unchanged; the zero-latency mode stays CPU.
This is a development build option, not a released backend preference. Use a
separate preview identity when installing it beside normal Spectr.

The wrapper implements `MaskRenderer`, so existing band publication, audio-rate
staging, mix automation and reset reach the same control authority and mixer
validated by the fixture. The CPU renderer continues computing full DSP. The
GPU result is accepted only through the existing epoch/history bridge; missing
or fenced results select the continuously prepared CPU output. There is no
claimed CPU offload yet.

## Latency is deliberately explicit

The internal quantum is half the analysis hop; additional latency is five
quantums. This depends only on configured spectral geometry, never callback
length, machine speed or provider availability. The dry path and automation
use exactly the same added delay even if GPU preparation fails.

| FFT / hop | CPU latency | Added latency | Experimental total at 48 kHz |
| --- | --- | --- | --- |
| 256 / 64 | 320 samples | 160 samples | 480 samples, 10 ms |
| 8192 / 2048 (product default) | 10240 samples | 5120 samples | 15360 samples, 320 ms |

At product geometry the extra delay is about 106.7 ms. This is a substantial
tradeoff to evaluate, not negligible overhead. It may be unsuitable for live
monitoring even if it enables useful shared GPU delivery. Before prepare the
product reports the same total PDC that the prepared wrapper reports. In this
experimental build the descriptor reports a fixed conservative tail covering
both modes, including the additional delay. It reads only compile-time geometry,
so it does not dereference a renderer during concurrent mode replacement. Default builds retain their existing behavior.

## Ownership and fallback

The product's stopped/control lane constructs, prepares and destroys wrappers.
An owned non-realtime worker services Dawn and drains delivery records; destruction
requests stop and joins it before releasing resources. The callback only stages
controls, mixes/processes prepared buffers and requests logical reset. The existing
parameter-sync lane is stopped before prepare/release, preserving the single
control publisher contract. Mode replacement retains the product's existing
reader epoch protection.

The worker currently sleeps for 50 microseconds between service calls. That is
an experimental polling policy, not event-driven completion or realtime priority.
The SDK's separately demonstrated timed wait must not be confused with removing
this delay or the transport worker's own inter-pump wait. Scheduling experiments
and authentic host validation remain necessary.

A failed GPU prepare may leave the adapter ready for CPU-only output, with the
same delay. `Snapshot::state` distinguishes unprepared, shared-ready, CPU-only
and fenced states. It does not invent a device failure cause from absence.
Forced CPU-only construction is an explicit test control; it is not persisted
as a plugin mode. Allocation/invalid-configuration failure still fails prepare.

The worker drains terminal records, and stopped teardown drains its final
cancellations. With Pulp tracing enabled, `spectr.shared_audio.delivery` events
in the `gpu` category carry `stream_epoch`, `block_sequence`, `ingress_admitted`
and the numeric `GpuAudioTerminalDisposition`. These describe logical Spectr
delivery, not GPU execution timestamps. The logical epoch is not the physical
provider epoch. A snapshot also exposes GPU/fallback/cancelled counts and lost
records; missing trace data must never be interpreted as no fallback.

## Acceptance boundaries

`Spectr-shared-spectral-renderer` runs small and product geometry through an
independent service thread, normal and forced-CPU modes, irregular callbacks,
mask changes, mix automation and reset. It compares every emitted sample with
the existing CPU renderer delayed by the declared extra latency. Its 2 ms test
sleeps deliberately allow the service thread to run and are not audio deadlines.
The product translation unit is separately compiled with the opt-in branch.

The source-linked probe reuses validated provider objects and older SDK archives.
It is not a packaged plugin, installed-SDK or host acceptance receipt. CMake
wiring, complete product linking, real CLAP/VST3/AU host behavior, PDC/tail reports,
Perfetto capture and paced contention are separate remaining gates. Keep the
normal product default until those results justify a policy change.

A refused physical release is not treated as successful shutdown. The wrapper
stops/joins its worker, reports `ReleaseUnconfirmed` and refuses reprepare while
retaining the adapter graph for a later retry. If destruction still cannot
confirm release, it intentionally retains the whole stopped graph for process
lifetime rather than destroying potentially GPU-owned buffers. This exceptional
path trades a resource leak for avoiding use-after-free; it is not ordinary
cleanup. Source-level refusal injection checks refusal, reprepare rejection and
the destructor's quarantine branch separately from provider lifecycle tests.

Fence diagnostics carry logical epoch and typed callback/worker causes. A burst
case may reach fallback after a fence and still produce correct audio; that
proves failure continuity only. A separate bounded normal segment requires the
worker to remain shared-ready and emit GPU results. Neither proves sustained
realtime reliability, and the forced-CPU wrapper still services a polling worker,
so it is not an uncontaminated CPU-performance baseline.

The provisional five-case run produced:

| Case | GPU / CPU deliveries | Ending state | Max CPU-reference residual |
| --- | --- | --- | --- |
| FFT 256, bounded 31-frame callbacks | 224 / 23 | shared-ready | 2.98023e-08 |
| FFT 256, forced CPU | 0 / 247 | CPU-only | 0 |
| FFT 8192, irregular callbacks up to 512 | 55 / 0 | shared-ready | 5.96046e-08 |
| FFT 8192, forced CPU | 0 / 55 | CPU-only | 0 |
| FFT 256, burst callbacks up to 512 | 57 / 190 | fenced: input journal unavailable | 2.98023e-08 |

All cases include a reset and retain the declared delay. Observed callback C++
allocations and lost logical delivery records were zero. The burst case reached
an input-journal admission fence, not a reported provider result failure. That
identifies the failure boundary, not its scheduling cause. Correct fallback after
that fence does not establish sustained GPU use. The two separately required
shared-ready segments demonstrate bounded live-worker integration only.

The final run also checks teardown accounting: GPU delivery plus CPU fallback
plus cancellation equals every complete admitted-or-fallback quantum across both
epochs, including final release. The totals are 255 for FFT 256 and 63 for FFT
8192, with 8 cancellations in each case and no lost records. This complements
the bridge's existing per-sequence uniqueness tests; Perfetto capture itself is
still an independent gate.

Mode replacement in the opt-in product branch also serializes renderer ownership
transfer with the existing control-publication mutex. The new renderer receives
the latest field before audio publication; the existing audio epoch still protects
an old callback reader. Constructing a private replacement no longer rewrites the
live publication cache. Full plugin/host stress coverage of this control handoff
remains required before changing defaults.

The actual product's construction loop only pumps silence when generation is
zero. The bridge's initial CPU publication already advances generation, so this
wrapper skips that loop and performs the product's immediate reset. The probe
asserts nonzero generation and repeats that reset before first audio, as well as
its later midstream reset. This checks startup chronology without claiming a
full plugin-host invocation of the construction path.

Repeating the probe with the immediate product-style startup reset kept both
bounded normal cases shared-ready (FFT 256: 203 GPU / 44 CPU; FFT 8192: 55 GPU /
0 CPU). The small-FFT burst case fenced before any GPU delivery and returned
247 exact CPU outputs. This is an actual startup/burst limitation while the
worker rebuilds its epoch, not a successful sustained GPU run. The burst test
checks continuity and records the outcome; only the separate healthy segments
require nonzero GPU delivery and shared-ready state. The preserved raw runs show
the difference rather than hiding it behind aggregate correctness.

The renderer oracle does not yet cover Spectr's post-render output-trim smoother
or the complete host parameter-event path. Trim still runs on the product's
output timeline. A full-processor comparison must establish the intended
chronology before describing the entire plugin output as the CPU product output
shifted by the additional delay. No second trim ramp or hidden timing rewrite
is introduced here.

## Installed-SDK processor acceptance readiness

`SPECTR_SHARED_PRODUCT_ACCEPTANCE=ON` adds a dedicated
`Spectr-shared-product-acceptance` executable and stopped-lane diagnostics. The
option is OFF by default and requires the experimental renderer. It does not
change saved state or normal backend selection. Its forced-CPU control must be
set before preparation and cannot change a prepared instance.

The test constructs the real Spectr processor twice, with normal and forced-CPU
shared wrappers. Both have identical declared latency. It compares emitted
samples through mix, output-trim and band-gain events, requires nonzero normal
GPU selections and zero forced-CPU GPU selections, checks lost records and PDC,
and repeats prepare/process/release before switching to zero-latency CPU mode.
It uses ordinary-thread pacing and establishes no realtime deadline guarantee.
A separate fully dry impulse case measures actual input-to-output delay in both
normal and forced-CPU modes. It uses asymmetric stereo markers at frames13 and29,
compares every output sample to the independently delayed input, and applies the
audio-harness marker policy with a pinned expected delay. A deliberately wrong
reported delay must fail. Mix, trim and band events in the main test also occur
at offsets17,29 and31 inside callbacks.

The snapshot describes selected spectral quantums before product mix and trim,
not submissions, completions, callback counts or proof of audible GPU contribution
at zero wet mix. It is separate from Pulp's transport `DeliverySnapshot`, since
Spectr uses its stamped spectral bridge directly. Reads require the stopped
control lane, with no concurrent prepare/release/mode replacement. Worker
counters may still advance independently.

Build against an exact, complete installed SDK, with no source-overlay libraries:

```sh
cmake -S . -B ../spectr-shared-product-installed-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$SPECTR_ACCEPTANCE_SDK" \
  -DSPECTR_EXPECTED_PULP_SDK_SHA="$SPECTR_ACCEPTANCE_SDK_SHA" \
  -DSPECTR_EXPECTED_PRODUCT_GIT_SHA="$SPECTR_ACCEPTANCE_SOURCE_SHA" \
  -DSPECTR_NATIVE_PREVIEW_IDENTITY=ON \
  -DSPECTR_EXPERIMENTAL_SHARED_RENDERER=ON \
  -DSPECTR_SHARED_PRODUCT_ACCEPTANCE=ON
cmake --build ../spectr-shared-product-installed-build \
  --target Spectr-shared-product-acceptance -j 2
ctest --test-dir ../spectr-shared-product-installed-build \
  -R '^Spectr-shared-product-acceptance$' --output-on-failure
```

This processor executable does not cross a plugin binary boundary. The existing
`Spectr-artifact-test` target loads built CLAP/VST3/AU binaries via `Pulp::host`;
run its format-specific cases separately against the same preview build. Existing
CPU-centric expectations must be audited for the experimental extra PDC before
calling that suite an acceptance gate. On a tracing-enabled SDK, preserve the
actual plugin's `spectr.shared_audio.delivery` records with output/PDC/control
receipts; a host that merely produces correct fallback audio does not prove GPU
selection. AU registration/install and interactive DAW lifecycle remain separate
from both executable tests.

Readiness is not acceptance: source syntax is checked, but complete installed-SDK
linking, this processor runtime, native-plugin host execution, default-binary
symbol absence, and actual-plugin trace capture are still open until their
receipts exist. The earlier source-linked renderer results remain separate.
