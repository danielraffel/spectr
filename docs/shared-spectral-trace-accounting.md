# Account for every Spectr spectral input

The experimental shared renderer emits `spectr.shared_audio.delivery` and one
`spectr.shared_audio.final` for each prepared renderer run. Capture belongs to
the plugin's existing tracing attachment. Do not create another tracing runtime
in the host. The attachment must outlive the processor's final stopped drain.
These records explain delivery and fallback; their event time is diagnostic
drain time, not GPU completion time or an audio deadline measurement.

A run is identified by `(Perfetto upid, renderer_run_id)`. Its ID is the initial
logical epoch reserved at prepare, remains constant across callback resets, and
changes on the next preparation. The epoch allocator is unique within a loaded
plugin module's lifetime. A complete unload/reload in the same process can restart
it; the validator rejects colliding run IDs as duplicate finals. Use an isolated
host process per capture until a host-bound module-incarnation identity exists. Delivery identity adds `(stream_epoch,
block_sequence)`. Epochs with no complete input quantum have no delivery rows.
A partial host block discarded by reset is not an accepted quantum.

The bridge counts complete processed input quanta and successful ingress journal
publications independently of the trace reader. Ingress publication is not GPU
submission. Immediately after reset, input may enter the journal before the
worker reestablishes a ForcedCpu fence, even in a CPU-only control. Its stopped snapshot also exposes actual terminal enqueue, pop and
loss counts. Counts survive logical reset and prepare; the renderer reports
per-run deltas. Saturation or invalid subtraction sets `counter_overflow`, which
invalidates the accounting instead of wrapping silently. The stopped snapshot
must never be read from a live UI or while callback, worker or reader is active.

Stop joins the worker, releases the adapter, drains final cancellations, then
emits the final summary only when physical release succeeded. Failed release
keeps the summary pending for retry. Repeated release and destruction cannot
emit duplicate finals. Destruction following unconfirmed release has no successful
final and remains incomplete; existing retained ownership behavior is unchanged.
A thread-start failure after adapter preparation can produce a zero-input final
once release succeeds, but cannot qualify as positive GPU-use evidence.

Use `tools/shared_spectral_trace.sql` with the SDK-pinned Perfetto processor.
Preserve both complete result sets as one JSON object with `events` and `stats`
arrays. Bind the process and expected run IDs to host receipts, rather than
assuming every surviving trace row represents the entire test. Then run:

```sh
python3 tools/validate_shared_spectral_trace.py exported.json \
  --expected-pid 12345 --expected-run-id 4294967297
```

The validator requires exactly one final, unique delivery identities, contiguous
sequences within each nonempty epoch, conserved input/terminal counts, matching
ingress and disposition totals, confirmed release, and zero terminal-ring and
Perfetto transport loss. Missing fields stay missing. No delivery may follow its
final. `--allow-cpu-only` admits a lifecycle control with no GPU output; the
default requires positive GPU delivery for each expected run.

An empty trace cannot pass. Independent host receipts are still needed to detect
an entirely missing process or run. Aggregate conservation does not independently
reconstruct every epoch's admission set. These checks do not establish realtime
scheduling reliability, explain a GPU scheduling delay, or demonstrate a speedup.

Focused controls cover 4100 inputs overflowing a 4096-record ring, complete and
partial quanta across resets, multiple prepares and instances, repeated stop,
callback allocation checks, and failed-release retry in the final-summary state
machine. That retry control does not simulate a physical GPU failure. The Python
controls exercise the actual export SQL plus missing/duplicate/malformed records,
loss, wrong process/run identities and misleading GPU-use claims. A real installed
plugin capture remains a separate gate.
