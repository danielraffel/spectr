# Shared renderer CLAP acceptance

This default-OFF diagnostic loads the actual Spectr CLAP binary and checks the
processor created by its factory. It does not instantiate a second source-linked
Spectr as a substitute. It uses ordinary paced threads and in-memory audio, with
no audio device, installation, editor, or DAW. A pass is functional host evidence,
not a realtime reliability or CPU-offload verdict.

Configure against the exact installed GPU-enabled SDK under investigation:

```sh
cmake -S . -B build-shared-host -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR="$SDK_PREFIX/lib/cmake/Pulp" \
  -DSPECTR_EXPERIMENTAL_SHARED_RENDERER=ON \
  -DSPECTR_SHARED_PRODUCT_ACCEPTANCE=ON \
  -DSPECTR_SHARED_NATIVE_HOST_PROBE=ON
cmake --build build-shared-host --target Spectr-shared-clap-host Spectr-shared-host-contract -j2
ctest --test-dir build-shared-host -R '^Spectr-shared-(clap-host|host-contract)$' --output-on-failure
```

Use the host governor when sharing a build machine. Record the SDK source and
installed manifest, consumer commit, executable and CLAP binary hashes, command,
and complete output together. The host executable accepts an optional capture
prefix after the CLAP binary path and writes separate normal/forced-CPU stereo
CSVs. Paths passed to the loader identify the executable inside the CLAP bundle;
CTest uses the CMake target file directly.

The test checks the loaded descriptor name and ID, resolves real parameter IDs,
and runs normal and forced-CPU epochs at 48 kHz / 512 frames. It changes band gain,
mix and output trim at sub-block offsets and crosses CLAP stop/reset/start.
Normal mode must select GPU output; forced CPU must select none. Both captures
must agree and contain audio. Separate fully dry impulse epochs measure each
channel's delay against CLAP's report and the intended `FFT + hop + 5*(hop/2)`.
A deliberately incorrect report must fail. Those unpaced dry epochs establish
PDC correctness; the paced epoch establishes actual GPU selection.

## Diagnostic lifetime contract

`spectr_shared_host_probe_v1` is an experimental test export, not a supported
production ABI. Only the opt-in CLAP entry defines it. Ordinary artifact tests
and their embedded CPU state authors keep their existing configuration.

Requests carry size, version, command, and an instance token. Discovery requires
exactly one live instance. Configure requires its token and an unprepared
processor. Replacement instances receive new tokens. Malformed, stale, ambiguous,
and inappropriate phase requests are rejected. The registry lock protects
instance lifetime, not callback/worker snapshot coherence.

The host must call CLAP `stop_processing` before Finalize. Finalize stops the
parameter publisher, joins the worker, checks provider release and drains final
terminal records. It returns a separate `release_confirmed` result. It destroys
the prepared render state: no DSP is permitted until deactivate and activate.
The diagnostic processor clears output and counts attempted process calls after
Finalize; the host tests that negative control. Duplicate Finalize is rejected.
A live Snapshot consists of independently loaded counters and must not be treated
as an atomic view. Only the stopped, successfully finalized snapshot establishes
completed aggregate accounting.

Aggregate GPU + CPU + cancelled totals and zero lost records do not establish
per-sequence uniqueness. The separate trace gate must still check each
`(stream_epoch, block_sequence)` for exactly one terminal outcome. The pure
registry executable uses a fake instance and cannot prove renderer teardown.

## Evidence boundary

Source and syntax checks are preparation only. Acceptance still requires the
exact installed SDK link, actual CLAP execution, captured output checks and
checked finalization. Binary absence of the diagnostic export in default builds
requires a linked-symbol check; preprocessing alone is narrower evidence.
Physical callback scheduling, DAW automation/PDC, editor contention, device loss,
package installation, and performance comparisons remain separate gates.

## Reset and comparison interpretation

Renderer delivery counters accumulate until the next prepare, across reset
boundaries. Reset first cancels pending bridge inputs in the old epoch, then
advances its epoch and clears DSP history. Finalize cancels remaining inputs in
the final epoch. Expected terminals are the sum of complete input quantums in
each epoch, not the floor of total frames divided by the quantum. An incomplete
quantum discarded at reset or shutdown was never admitted; the host prints those
partial frame counts separately. Default geometry has no partial input here.
The pure accounting regression uses quantum 1024 and reset at frame 4093 to
ensure the two partial inputs cannot be mistaken for one admitted quantum.

Normal mode may choose CPU fallback for any deadline. It needs at least one GPU
selection, not a 100% GPU delivery rate. The CPU shadow runs continuously, and
both selection paths use the same effective spectral controls and fixed delay.
The comparison checks the peak absolute residual below -90 dBFS across both
entire stereo captures, including startup, reset, automation and fallback. It
does not align, crop, omit late blocks or change the reference based on timing.
Scheduling can change selection counts; it must not change the audio contract.
