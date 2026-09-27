# Proposed realtime reset for the experimental spectral adapter

Read-only design against Spectr `42d0f1f` and composed Pulp `09c4b82186`.
Historical design, now implemented experimentally by the callback reset successor.
See `experimental-shared-spectral.md` for provisional test results and remaining
gates. The original design below is retained to preserve its reasoning.

## What can already reset safely

Spectr `LinearPhaseMaskRenderer::reset()` in `src/mask_renderer.cpp:449` calls
`SpectralMaskProcessor::reset()`. Pulp's processor clears engine and mixer state
and settles the adopted immutable mask. `SpectralFrameEngine::reset()` fills
already allocated input/output/normalization/frame arrays and resets indices.
This is the CPU reset to reuse, not a second DSP implementation. Its cost is
bounded by prepared geometry but must still be measured at maximum geometry.

The bridge's current `reset()` calls prepare/release and recreates the GPU
session. It is not suitable for the audio thread. Nor is
`GpuAudioTransport::release()`, which joins its worker. `GpuSpectralMaskSession`
provides a process-unique physical session epoch, contiguous submission,
service/receive and physical-release confirmation. Those remain worker-owned.
The generic `SharedIoProgramSession` retains physical retirement/quarantine
ownership; do not replace that machinery with callback-side frees.

## Identity and ownership changes required

Keep one callback producer and one serialized service owner. Reset may run on
the callback owner between process calls, never concurrently with that same
owner's processing. Host reset/prepare/release still need an adapter-level
lifetime contract; a third thread may not clear CPU arrays under processing.

Add a logical audio epoch distinct from the GPU session's physical epoch. The
callback owns the logical epoch, block sequence, CPU renderer, fallback ring,
adapter assembly/output buffers and terminal producer cursor. The worker owns
GPU objects, hop assembly/submission and physical completion. Every ingress and
output slot gains a logical epoch alongside its sequence. The worker records
which logical epoch its current physical session represents.

A strict bounded callback must not call the current non-RT epoch allocator's
retrying CAS loop. Reserve a disjoint range of logical epoch values during
prepare, then increment locally on reset. Range exhaustion fails closed without
identity wrap and requires non-RT reprepare. Static-assert the atomic widths used
for handshakes are always lock-free on supported builds. Physical and logical
epoch fields are separate; do not compare their numeric values as if identical.

## Callback reset protocol

1. Emit `Cancelled` for input sequences still awaiting terminal disposition in
   the old logical epoch. In this bridge that set is bounded by prepared lead,
   not by stream duration. The callback remains the sole terminal producer.
   Preserve queued records/read cursor and cumulative dropped-record count.
2. Increment the logical epoch and reset local sequence/accounting. Clear CPU
   DSP state, fallback delay and the adapter's two quantum buffers/cursor using
   existing storage. The immutable mask and sample-based latency stay unchanged.
3. Publish `requested_epoch` with release ordering. Do not call service, create,
   release, wait, join, a GPU API, or change worker-owned pointers. Do not reset
   all slot states: a worker may currently own one.
4. Continue CPU rendering immediately. Queue each new complete input quantum,
   starting at sequence zero, tagged with the new epoch. Accept GPU output only
   when its epoch/sequence match the callback's exact target and the worker has
   advertised readiness for that same epoch. Otherwise use aligned CPU fallback.

No stale output is audible after reset. Clearing both adapter buffers preserves
its one-quantum assembly delay; clearing the bridge fallback preserves its L
quantums. Total remains FFT+hop+(L+1)Q for every host partition.

## Worker transition and slot races

On observing a new requested epoch with acquire ordering, stop admitting old
logical work, retire the old physical session using its existing lifecycle, and
create the replacement off the callback. If physical release fails, retain its
ownership, publish unavailable status and continue CPU fallback. Do not create
unbounded replacements or free live imported memory.

Do not globally clear ingress/output slot states. A slot's owner transitions
Empty -> Busy -> Ready and Ready -> Busy -> Empty with the existing acquire/
release publication. Epoch and sample metadata are accessed only while owned.
The callback may reclaim a Ready ingress slot from an older epoch by first
winning its ownership CAS; it must never overwrite a Busy slot. The worker
likewise discards obsolete slots only after claiming ownership. This permits
reset without touching memory concurrently held by the other thread.

The worker checks requested epoch before submission and before publishing
output. A reset racing the final check can still leave an obsolete GPU dispatch
in flight; that is safe because its physical resources remain owned and the
callback independently rejects its logical epoch. Epoch checks are a delivery
barrier, not a promise to cancel already submitted Metal work.

## Preserve history during rebuild

Do not simply disable input capture while rebuilding, then submit the current
block. The CPU has advanced and spectral history would diverge. Reuse the
existing fixed ingress slots as a bounded journal of *all* post-reset quantums.
The worker replays them contiguously from sequence zero, preserving hop phase.
Expired results may be discarded, but their DSP history must still advance.

Readiness means the replacement has consumed the contiguous input prefix and
has valid current-epoch results, not merely that pipeline creation succeeded.
Publish readiness as an epoch-tagged atomic. A late readiness write for an older
epoch cannot authorize a newer one. Callback acceptance still requires an exact
output identity and deadline/target check.

If a Busy-slot collision, journal capacity, submission failure or input gap
loses any new-epoch quantum, fence GPU delivery for that epoch. CPU rendering
continues with unchanged latency. Report `history_gap`/`reset_recovery_failed`;
do not silently start at a later sequence. The minimal implementation stays CPU
until a new explicit reset/reprepare. Automatic history priming after overflow
would be a separate, proven extension. Rebuild duration and journal capacity
therefore affect recovery availability, not audio correctness.

## Reset storms and telemetry

Coalesce requests to the latest epoch while retaining at most one owned physical
session. Do not begin a replacement until the preceding session confirms safe
retirement. If preparation finishes
for a superseded epoch, retire it on the worker and re-check the latest request.
Callback reset never waits for acknowledgment. Each reset cancels only that
epoch's outstanding inputs; physical completion never emits a second terminal
for them. Use epoch-tagged ready/failure reports so an old worker cannot clear a
newer fence or authorize stale output.

Terminal records describe internal bridge quantums. Some were already accepted
into the partition adapter's output buffer before reset; do not rewrite their
terminal outcome when reset intentionally discards buffered playback. Keep this
endpoint explicit. Queue overflow increments persistent lost telemetry and makes
trace completeness unavailable; reset must not erase that evidence.

## Minimum tests before claiming this gap closed

- Deterministic barriers pause the worker while it owns input/output slots, has
  an old dispatch in flight, is retiring, and is preparing the replacement.
  Reset at each point; plant a stale-ready publication and prove rejection.
- Multiple resets before worker acknowledgment and during replacement creation:
  latest epoch wins, old resources retire, no duplicate/orphan terminal records
  when telemetry is complete, bounded allocation/ownership count.
- CPU oracle resets at the identical sample position; cover nonzero history,
  partial adapter quantum, irregular callbacks, silent startup and nonzero tail.
  Reported latency remains constant. Allocation guard surrounds reset and
  processing; assert neither invokes worker/GPU methods or waits.
- Adequate journal recovery proves CPU/GPU parity after catch-up. A deliberately
  undersized/full journal proves persistent CPU fallback and explicit failure,
  not acceptance of a newly started but historically wrong GPU stream.
- Device loss or failed physical retirement during reset, trace overflow across
  reset, epoch exhaustion refusal and shutdown after a reset storm.

This closes reset independently of live automation. Do not add control-table
publication or ramps in the same concurrency change.
