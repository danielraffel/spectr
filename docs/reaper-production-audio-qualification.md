# Production REAPER audio qualification

`tools/reaper_production_audio.py` is an opt-in macOS VST3/CLAP harness for a
shipping Spectr bundle with test seams absent. Its oracle tests run without
REAPER. Implementing the harness and passing those tests does **not** qualify a
release; qualification requires successful retained runtime evidence for each
exact artifact and host architecture being advertised.

The measured executable is a temporary signature-preserving copy of the
provided bundle. Actual installed-path plugin discovery/loading remains a
separate product proof; `installed_path_verified` is always false.

The harness does not open or drive the plugin editor. Visible controls, menus,
modulation markers, repaint and real mouse gestures remain separate GUI checks.
It does not test LFO modulation, wet spectral processing, freeze loops, AU,
Logic, or the installer. In particular, passing this check cannot replace those
release requirements. It proves host Output automation and host-project parameter
persistence through a cold reopen, with dry audio as a precise reference.

Run only in an authorized host session. Supply the executable digest extracted
from the exact final release artifact, independently of the bundle under test.
The digest binds executable bytes; package provenance and signature verification
remain separate release evidence. Use the exact REAPER executable and a current
Pulp checkout containing the canonical daw-smoke session helper. Python needs
NumPy for the reused scan/audio-device helpers; the oracle unit tests themselves
use only the standard library.

```sh
python3 -B tools/reaper_production_audio.py \
  --plugin "$QUALIFIED_BUNDLE" \
  --expected-sha256 "$RELEASE_EXECUTABLE_SHA256" \
  --reaper "$REAPER_EXECUTABLE" \
  --pulp-repo "$PULP_CHECKOUT" \
  --out "$NEW_EVIDENCE_DIRECTORY"
```

The output directory must not already exist. Every result, including failure,
is retained. The default timeout is 120 seconds per scan/handshake/render and
can be bounded explicitly with `--timeout` (1–600 seconds). The harness stops
only its owned REAPER children. It stages a signature-preserving bundle copy
in a temporary scan folder using Pulp's session helper and never installs into
real plugin directories. AU is deliberately unsupported. The temporary REAPER
configuration points input and output at a nonexistent audio device, and host
facts require that no output device opened. Production fixture variables from
the calling environment are removed for the scan child and both rendering children
using per-child environment dictionaries; the parent environment is not changed.
No plugin fixture flags are set. The
existing release test-seam checker must accept the supplied bundle before any
REAPER launch.

Each format run performs two host lifetimes:

1. Create one Spectr track, find required parameters by their exact unique host
   names, set Mix to dry and retain a nondefault Intensity value, disable Auto
   Gain and LFO 1, and create a real host Output envelope: 0 dB for seconds
   0–3, −12 dB for seconds 3–6, then 0 dB for seconds 6–9. The lane uses the
   host's reported parameter range and envelope scaling mode.
2. Before permitting the synchronous render action, inspect the owned live PID
   using `lsof`, resolve the intended `CFBundleExecutable`, and require exactly
   one mapped plugin executable with the externally supplied SHA256. After the
   action returns, repeat the mapped-file check in that same live PID. Save the
   host project, then stop the child.
3. Launch a fresh owned REAPER with the saved project. Do **not** reapply the
   parameter values or recreate its envelope. Require the restored host values
   and exact points, then repeat the two-receipt render protocol and save again.

Both renders use the same owned PID as their before/after receipts; no separate
`-renderproject` process is launched. Receipt evidence consists of mapped paths
and their on-disk file hashes at those two points, not a hash of process memory
or continuous mapping observation. The reusable receipt's
`offline_render_processes_verified` field stays false because it does not prove
separate render processes. This harness's aggregate instead states
`separate_render_processes_used: false` and retains both receipts per stage.

The deterministic stereo input contains 431 Hz and 997 Hz tones. Audio must be
48 kHz stereo, exactly nine seconds, finite and unclipped. Stable one-second
windows must reproduce the expected absolute gain within 0.25 dB on both
channels. Each tone must independently obey the same gain, and unexpected
non-probe energy must stay below 0.1 percent of output power. Additionally,
contiguous 50 ms windows check absolute gain within 0.25 dB on both channels
throughout 0.1–2.9, 3.1–5.9 and 6.1–9.0 seconds: 8.5 of the nine seconds.
Only 0–0.1 seconds of startup and ±0.1 seconds around the two automation
transitions are excluded, allowing initial host/DSP settling and ten times the
Output control's 10 ms smoother. Those bounded margins are explicitly unproven;
no other gaps are permitted. The result retains every short-window gain and
its exact coverage/exclusions. A cold-reopened
render must additionally match the first render within a peak sample error of
0.0003. These tolerances allow PCM quantization and ordinary render noise while
rejecting silence, bypass, an ignored lane, a wrong uniform gain, changed
spectrum with equal RMS, missing audio between the original spectral windows,
brief one-channel dropouts, and materially different reopened audio.

The retained `result.json` carries terminal PASS/FAIL, exact expected executable
hash, host executable path/hash, parameter/envelope facts before and after each
render, all four mapped-file receipts, project and WAV hashes, measured audio
metrics, and explicit `gui_verified: false` and
`package_provenance_verified: false` and `installed_path_verified: false`. A missing status, timeout, absent module,
stale same-name module, ambiguous mapping, changed hash, failed render, wrong
state, or bad audio never becomes a passing result. Every artifact must obtain
its own complete evidence directory. No runtime result is checked in with this
source change.

Run the source/oracle controls without opening REAPER:

```sh
python3 -B tools/test_reaper_production_audio.py -v
python3 -B tools/test_reaper_loaded_binary.py -v
```

Required CI registers `Spectr-reaper-production-audio-oracles` and selects it in
the product acceptance pattern list. This is a harness regression check, not a
real-host release qualification receipt.
