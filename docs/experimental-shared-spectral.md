# Experimental shared spectral bridge

`SpectrSharedSpectralBridge` is an opt-in library built with
`SPECTR_SHARED_SPECTRAL_FIXTURE=ON`. Its implementation lives in
`src/experimental/shared_spectral_bridge.cpp`; the test driver uses that same
implementation. The shipping renderer factory is unchanged.

Prepare an immutable layout while stopped, then call `process()` from one audio
callback producer and `service()` from one non-realtime worker. All vectors,
slots, CPU history and GPU resources are prepared ahead of processing. Stop and
join both callers before `reset()`, `release()`, destruction or inspecting
provider diagnostics from another thread. That quiescent reset is not audio-thread safe; use the separate experimental
`reset_realtime()` protocol described below from the callback.

The supported slice is linear-phase, fully wet processing without mix
ramps. Live layout publication and audio-owner staging use the existing CPU
authority; `set_mix()` still refuses changes. The host block divides the spectral hop. Pipeline lead is 1 through
8 **host blocks**, added to the renderer's intrinsic FFT-plus-hop latency.
Adapters must report that total to the host before activating the path.

The CPU renderer advances on every callback, even when the GPU output is used.
Missing or expired GPU outputs select the exact latency-aligned CPU output.
This guarantees reference continuity within the validated envelope, but saves
no CPU DSP work. An ingress gap or overflow fences GPU delivery until a new logical epoch through
reset. The worker continues retiring physical GPU work.

A callback only accepts output tagged with its target block sequence and epoch.
Terminal records are bounded and consumed with `pop_terminal()`. `ingress_admitted`
means admission to the bridge ingress, not proof of a submitted GPU dispatch.
Normal shutdown should call `process(..., false)` for the configured number of
lead blocks to drain terminal outcomes, then stop and join. Do not resume new
input after this drain without reset. Abrupt reset/release cancels remaining input sequences from the quiescent owner.
Reset preserves queued terminal records and loss counts across epochs. The tests
exercise both abrupt operations; a real product lifecycle adapter remains open. Trace overflow is observable and must
invalidate any exactly-once trace claim.

The fixture pauses the worker, exercises all four lead settings, forces ingress
overflow and checks a new epoch after reset. It compares every output sample,
including startup and zero-fed tails, with the actual independent Spectr CPU
renderer. Its driver waits outside the callback for work to finish. GPU-delivery
counts are correctness evidence, not deadline success rates or benchmarks.

The first provisional M5 run matched within 5.96046e-08 at FFT1024/hop256,
stereo/64-frame host blocks. Runtime WriteBuffer, CopyBuffer and MapAsync counts
were zero. CPU copies into and out of shared slots remain. A canonical installed
SDK consumer, paced performance, rendering contention, host integration and
control publication are still open gates. Do not change the default on this
result.

## Variable host callback partitions

`SharedSpectralPartitionAdapter` wraps the same bridge with one input and one
output quantum buffer. `internal_quantum` Q is fixed at prepare time; it is not
selected from the incoming callback length. `additional_latency_samples` A is a
persisted sample count, supported initially as multiples of Q from 2Q through
9Q. The adapter computes the bridge lead as L=A/Q-1.

The adapter emits the previous quantum while assembling the next one. The first
quantum's bridge output therefore begins at host sample Q, independent of how
many host callbacks assembled it. Total latency is:

    FFT size + analysis hop + L*Q + Q = FFT size + analysis hop + A

This is one full Q, not Q-1: copying the quantum's last input sample does not
replace the output sample already emitted at that position. The bridge is called
after the copy, and its first output sample is emitted at the next host sample.

Callback sizes from zero through `max_callback_frames` are accepted. Zero is a
no-op. Per-channel in-place processing works; different channels must not alias.
Input/output pointers are validated before any state change. Process uses no
resizing, allocation, wait or GPU API. The existing non-RT service owner still
owns submission. Prepare/quiescent reset/release still require joined callers.
Live masks use the capture protocol described below; callback reset is described below.

Tests compare partitions 1,31,32,63,64,127,128 and irregular splits against the
actual CPU renderer at the same explicit latency, with zero-fed tails, a paused
worker, all-CPU fallback and in-place buffers. The identity impulse uses Pulp's
latency measurement helper; a report deliberately wrong by one sample must
fail. A C++ allocation-operator guard covers the complete adapter/CPU fallback path;
it does not interpose arbitrary C allocator calls.

Terminal identities remain **internal bridge quantums**, not host callbacks.
`GpuDelivered` means the bridge accepted that quantum into the adapter's output
buffer. It must not be relabeled as a hardware-played host block, particularly
when a stop discards a partly consumed output quantum. Physical-device playback
is a separate endpoint. Release cancels outstanding bridge inputs; an unfinished
input quantum was never admitted into that bridge.

## Callback reset successor (provisional correctness validated)

`reset_realtime()` resets preallocated CPU fallback and adapter buffers, cancels
pending logical outcomes, and publishes a new logical epoch. It never invokes
the GPU service observer, provider creation/release or worker join. Quiescent
`reset()` remains available separately. Logical identities are reserved in
ranges during prepare; exhausting a range refuses callback reset and requires
non-RT reprepare, without wrapping or aliasing an earlier stream.

The worker retires/recreates its physical session and replays every captured
new-epoch quantum from sequence zero. Ready output requires the new logical
identity and exact target sequence. Input-slot collision or capacity loss fences
GPU delivery for the entire epoch; CPU fallback remains aligned. A later reset
can retry. The current API exposes this as `fenced()`; detailed product-facing
failure reasons remain future diagnostic work.

A service observer provides deterministic worker-side test barriers. It is
optional, installed only while callers are stopped, and is never called by
callback reset/process. Callback counters and terminal records use logical
epochs; physical GPU epochs remain separate. Trace records and loss counters
survive reset. Reset cost scales with prepared buffer geometry; allocation-free
is not a hard realtime scheduling guarantee.

The provisional reset suite passes history replay, reset storms, journal overflow,
and six deterministic worker barriers. Successful recovery matched the CPU
reference within 4.47035e-08 with 103 GPU and 21 fallback outcomes. Overflow
remained entirely on CPU with exact parity. The callback C++ allocation guard
observed zero calls. All 18 partition controls and original bridge lifecycle
controls still pass.

Three isolated faults were injected to check the tests themselves: omitting
history replay failed with residual 0.149576; allowing old-epoch output delivered
a stale sample and failed; discarding newly claimed current-epoch input failed
recovery. The initial sample-only stale test missed its defect because the
partition adapter delays accepted output another quantum. The final test checks
both terminal acceptance and the subsequent emitted sample.

These are source-linked correctness tests using reused provisional provider
objects, not installed-SDK or paced host tests. Device-loss/physical-retirement
failure injection, epoch exhaustion, maximum-geometry reset timing, live control
publication, and authentic SDK/host integration remain open.


## Capturing live effective masks

The experimental build enables the linear renderer's effective-frame observer.
Other renderers return unsupported, and the shipping renderer factory selection
is unchanged. The bridge's `publish_layout` and `set_layout_rt` delegate to the
existing CPU renderer. Its publication adoption and transition interpolation are
the only control authority; the GPU worker never recomputes a ramp or compiles
an independently selected latest layout. The configuration field
`immutable_layout` is retained for source compatibility and now means initial
layout. Only the experimental target defines the observer compile flag, so
ordinary renderer builds do not require this new SDK method yet.

Each applied coherent spectral frame copies its exact effective gains into a
prepared64-slot control journal. Entries carry logical epoch and frame ordinal.
The existing ingress quantum journal carries the matching samples. A control
entry is published before the corresponding input quantum becomes Ready. Worker
hop `q` requires frame `q - (FFT/hop - 1)` after initial FFT fill; it checks exact
epoch and ordinal, copies that table into prepared worker scratch, and submits
it through the SDK's per-hop shared-slot path. Retry retains the same table.
Missing or colliding entries fence GPU acceptance for the epoch. All ordered
contributing frames must be present, which is stronger than checking only a
current-generation label on WOLA output.

Reset preserves the observer, restarts frame order and tags subsequent tables
with the new logical epoch. The worker replays the new input/control history
from the beginning. Prepared initial gains are only used during initial FFT fill,
when no analysis frame exists. Thus reset after automation follows the CPU's
settled adopted mask even though the worker recreates its physical session.
Input/output/control slots obey exclusive ownership before metadata reads.

The provisional control suite passes17 cases: host partitions1/31/32/63/64/127/128,
with and without a partial-quantum reset, two irregular-partition cases and one
forced ingress overflow. It interrupts mask transitions, mixes control-thread
publication with audio-owner staging, stalls the worker and verifies all output
against the existing CPU renderer at the declared480-sample total latency.
Maximum residual is2.08616e-07; overflow falls back with exact parity. Callback
C++ allocation count and runtime WebGPU transfer calls are zero. Existing reset,
18-partition and bridge lifecycle regressions pass against this implementation.

Dependencies are exact Pulp gain transport `116ee043a7` and frame observer
`6adbdbcb57`; the focused build reuses previously validated provider objects and
older SDK archives. It is not an installed-SDK or plugin-host proof. Fully wet
mode remains mandatory. Dry/wet automation timing, real worker pacing, rendering
contention, maximum-size capture/reset cost and product factory integration remain
open. This validation path still computes the full CPU fallback continuously.


The separate stale-table implementation control deliberately keeps the worker's
previous gains while still consuming every control slot. It fails actual sample
parity with residual0.496724 and exit83 while reporting115 GPU outcomes. This
shows that GPU-delivery counts alone would not catch the control-history defect.
One control publisher may publish concurrently with the callback; preparation,
release and destruction require that publisher, callback and worker to stop.
The callback's `set_layout_rt` remains separate from the control publisher.
