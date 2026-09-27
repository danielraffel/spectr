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
