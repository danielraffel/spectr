# Experimental shared spectral bridge

`SpectrSharedSpectralBridge` is an opt-in library built with
`SPECTR_SHARED_SPECTRAL_FIXTURE=ON`. Its implementation lives in
`src/experimental/shared_spectral_bridge.cpp`; the test driver uses that same
implementation. The shipping renderer factory is unchanged.

Prepare an immutable layout while stopped, then call `process()` from one audio
callback producer and `service()` from one non-realtime worker. All vectors,
slots, CPU history and GPU resources are prepared ahead of processing. Stop and
join both callers before `reset()`, `release()`, destruction or inspecting
provider diagnostics from another thread. Reset is not audio-thread safe.

The supported initial slice is linear-phase, fully wet processing without mix
ramps or layout transitions. `publish_layout()` and `set_mix()` explicitly refuse
live changes. The host block divides the spectral hop. Pipeline lead is 1 through
8 **host blocks**, added to the renderer's intrinsic FFT-plus-hop latency.
Adapters must report that total to the host before activating the path.

The CPU renderer advances on every callback, even when the GPU output is used.
Missing or expired GPU outputs select the exact latency-aligned CPU output.
This guarantees reference continuity within the validated envelope, but saves
no CPU DSP work. An ingress gap or overflow fences GPU delivery until quiescent
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
owns submission. Prepare/reset/release still require joined callers; live masks
and RT reset remain outside this slice.

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
