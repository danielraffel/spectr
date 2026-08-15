# Spectr N4 Performance and Operational Receipt

Date: 2026-08-15
Status: Operational comparison complete; timing judgment requires an idle-M5 rerun

## Method

`tools/benchmark_native_cutover.py` launches the separately built frozen
WebView and native Standalone products with identical one-frame and 120-frame
headless budgets. It records cold and warm wall/user/system time, root-process
maximum RSS, sampled whole-process-tree RSS and CPU, attributed WebKit helper
processes, analyzer/audio operation, three simultaneous editors, valid PNG
production, and clean teardown. The complete per-sample JSON receipt is
`build-native-final-release/native-cutover-benchmark-20260815.json`.

The script deliberately reports measurements rather than applying a
machine-independent performance threshold. Its GPU counter is unavailable;
whole-tree CPU includes the WebView lane's WebContent/GPU helpers.

## Recorded comparison

Warm medians (first launch excluded):

| Scenario | Native | Frozen WebView |
|---|---:|---:|
| One-frame wall time | 2.680 s | 0.675 s |
| One-frame aggregate peak RSS | 181.4 MiB | 232.5 MiB |
| 120-frame wall time | 12.030 s | 2.605 s |
| 120-frame aggregate peak RSS | 295.5 MiB | 795.5 MiB |
| 120-frame aggregate peak CPU | 103.6% | 237.0% |
| Analyzer/audio 120-frame wall time | 15.275 s | 2.845 s |
| Analyzer/audio aggregate peak RSS | 395.7 MiB | 821.5 MiB |
| Analyzer/audio aggregate peak CPU | 106.7% | 232.7% |

Three simultaneous 120-frame editors recorded 863.8 MiB aggregate peak RSS
(287.9 MiB/editor), 299.7% peak CPU, and three editor processes for native.
The WebView lane recorded 2451.8 MiB aggregate peak RSS (817.3 MiB/editor),
622.3% peak CPU, and twelve processes: three editor processes plus nine
attributed WebKit helpers. Both lanes left zero editor or attributed helper
processes after teardown. Native linked and launched with zero WebKit helpers.

## Interpretation and remaining timing gate

The operational result is positive: native removes three helper processes per
editor, materially lowers whole-tree steady/analyzer/multi-editor memory and
CPU, opens no audio device in the silent cases, produces valid captures, and
tears down cleanly. Analyzer-enabled runs opened audio as requested.

The wall-time result is not accepted from this run. During measurement the
BlackBook had two unrelated virtualization processes consuming roughly 400%
CPU each, an unrelated coverage test at roughly 100%, additional compiles,
and load averages above 20. That environment particularly penalizes native's
single UI/render thread relative to WebKit's multi-process work distribution.
The 120-frame native result therefore remains a provisional regression, not a
cutover rationale. N5 requires the same checked-in benchmark to be rerun on an
idle M5 before native timing/frame-pacing readiness is accepted.

Renderer-independent DSP automation, modulation, analyzer calibration, and
state publication remain covered by deterministic Release tests. Native input,
resize, menu, modal, persistence, and state-transition behavior is covered by
the 19-state native atlas and the shared browser/native interaction oracles;
those are correctness receipts, not substitutes for the pending idle timing
measurement.
