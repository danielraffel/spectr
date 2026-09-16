# Spectr

A zoomable frequency-slicer audio effect built on [Pulp](https://github.com/danielraffel/pulp).

Spectr is not an EQ and not a spectrum analyzer. It is a **precision tool
for isolating, removing, and recombining narrow frequency-defined parts
of a sound** with unusual depth and targeting.

## Status

Release 1 is an effect with 32–64 authored logarithmic band controls, a
continuously zoomable frequency viewport, nonadjacent frequency islands, and
exact mute. The `RES represented/active` disclosure reports how many controls
the live renderer's design grid can represent independently in the current
viewport; it is computed and served over the editor bridge, and is not
currently drawn in the shipping editor. The default Balanced 8192 profile represents all 32 controls
across 20 Hz–20 kHz; a narrow zoom or a higher control count may represent fewer
controls independently. Its reviewed HTML design is embedded source-preservingly
through Pulp's native WebView bridge, with a narrow runtime adapter connecting
the design's live band/zoom state to native C++ DSP and state.

The production path builds AU, VST3, CLAP, and Standalone artifacts. The test
suite covers the shared spectral-mask DSP, exact latency and mute behavior,
state round-trip, actual CLAP/VST3 artifact hosting, and a headless Standalone
launch. Visible host validation is tracked in the canonical goal document.

Hosts see the full surface: `Mix` and `Output`, all 64 band gains and mutes,
the A/B snapshot morph, the viewport and band count, the mode toggles, the two
internal LFOs, and four macros — each macro a single automatable lane that
drives a user-chosen group of bands. See
[`docs/parameter-surface.md`](docs/parameter-surface.md) for the ID scheme and
the compatibility contract. Macro MEMBERSHIP and the snapshot bank itself stay
editor-local working state, preserved in Spectr's supplemental plugin state:
neither is a value a host can automate.

See [`planning/`](planning/) for the full design package:

- [`planning/Spectr-V2-Product-Spec.md`](planning/Spectr-V2-Product-Spec.md) — product contract
- [`planning/Spectr-V2-Pulp-Handoff.md`](planning/Spectr-V2-Pulp-Handoff.md) — build guidance
- [`planning/Spectr-V1-Build-Plan.md`](planning/Spectr-V1-Build-Plan.md) — implementation sequence
- [`planning/Spectr-Sampler-Phase-Spec.md`](planning/Spectr-Sampler-Phase-Spec.md) — Phase 4+ sampler spec
- [`planning/Spectr-Upstream-Integration-Plan.md`](planning/Spectr-Upstream-Integration-Plan.md) — Pulp pickup playbook
- [`planning/Spectr-Build-Signoff.md`](planning/Spectr-Build-Signoff.md) — current build clearance state

Release candidates use the manually dispatched, local-first
[`Spectr M5 Product Acceptance`](docs/local-first-product-acceptance.md) gate.
It consumes an immutable official Pulp SDK, runs in a clean transient Tart macOS
ARM64 guest, and publishes an exact-head PKG for hands-on M5 testing.

## Building

Requires a Pulp SDK with the dedicated native scripted Skia/Dawn view target
and AU/VST3/CLAP/Standalone support. A local Pulp checkout can produce the
immutable development SDK used by Forge-style consumers:

```bash
Pulp_DIR="$(pulp sdk install --local --profile forge-dev --print-path)/lib/cmake/Pulp"
Pulp_SHA="$(git -C /path/to/exact/pulp-worktree rev-parse HEAD)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR="$Pulp_DIR" \
  -DSPECTR_EXPECTED_PULP_SDK_SHA="$Pulp_SHA"
cmake --build build
ctest --test-dir build --output-on-failure
```

The forge profile validates WebView provenance and normalizes every installed
static archive to arm64. The expected-SHA gate rejects a compatible but older
or substituted SDK before Spectr compiles. Release/distribution builds should
use a provenance-marked, distribution-eligible release SDK from the same exact
accepted Pulp commit, not the development profile.

For the exact-head M5 interaction gate, install a tracing-enabled development
SDK from the same merged Pulp commit, configure Spectr in Release with that
exact SDK and SHA, and run the two isolated live-host workloads:

```bash
TracePrefix="$(tools/install_trace_sdk.sh /path/to/exact-clean-pulp-worktree)"
Pulp_SHA="$(git -C /path/to/exact-clean-pulp-worktree rev-parse HEAD)"
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR="$TracePrefix/lib/cmake/Pulp" \
  -DSPECTR_EXPECTED_PULP_SDK_SHA="$Pulp_SHA" \
  -DSPECTR_ENABLE_PERF_FIXTURES=ON
cmake --build build-native --target Spectr_Standalone
tools/verify_interaction_perf.sh \
  build-native <exact-spectr-sha> <exact-pulp-sdk-sha> artifacts/perf
```

The command captures separate band-edit, minimap, and host-automation
Perfetto traces, GPU screenshots, and JSON receipts. The `automation` workload
drives `SPECTR_AUTOMATION_PERF_FIXTURE=1`, which only has an effect when the
binary under test was configured with `-DSPECTR_ENABLE_PERF_FIXTURES=ON` (OFF
by default, including for every shipping/distribution build) — that fixture
hook does not exist in an ordinary build, by design, so the `automation`
workload silently measures unforced host-automation cadence instead of the
stressed path unless this flag is set. It fails closed when provenance
differs, a required AppKit/QuickJS/Skia stage is absent, layout or paint
repeats more than once per delivered input, or the M5 trace misses the 120 Hz
p95 / 60 Hz p99 frame budgets. These are development artifacts; traced
binaries must never be packaged for distribution.

### Spectral build profiles

New build directories use the **Balanced** product default: an 8192-sample FFT
with a 2048-sample analysis hop. At 48 kHz it represents all 32 bands across
the full 20 Hz–20 kHz viewport and reports 10,240 samples (213.33 ms) of
latency to the host in the Mixing latency mode.

Latency is a function of the render mode as well as this geometry: the
Tracking mode realises the same drawn magnitude through a minimum-phase FIR
and reports 64 samples (1.33 ms) whatever the build profile below says. The
figures in that table are the Mixing mode's.

Two alternate fixed build profiles are available for explicit trials:

| Profile | CMake configuration | 48 kHz latency | Intended tradeoff |
|---|---|---:|---|
| Live | `-DSPECTR_FFT_SIZE=1024 -DSPECTR_ANALYSIS_HOP=256` | 1,280 samples / 26.67 ms | Lower latency, substantially coarser narrow-view isolation |
| Balanced (default) | `-DSPECTR_FFT_SIZE=8192 -DSPECTR_ANALYSIS_HOP=2048` | 10,240 samples / 213.33 ms | Full normal-range representation with useful zoom detail |
| Maximum | `-DSPECTR_FFT_SIZE=16384 -DSPECTR_ANALYSIS_HOP=4096` | 20,480 samples / 426.67 ms | Highest available narrow-view detail, highest latency |

These select one compile-time WOLA geometry for an artifact. They are not
runtime response modes and cannot be switched dynamically in a loaded plugin.
Use a fresh build directory when comparing profiles so an older CMake cache
does not retain its previous geometry.

## Merging the materialized editor document

`native-ui/materialized/materialized-document.runtime.json` is a checked-in,
minified, single-line JSON artifact (~830 KB) carrying the whole editor
document. Because it is one physical line, git's default content merge treats
*any* two changes to it as a conflict -- including changes thousands of lines
apart once its `html` payload is decoded. On 2026-09-15 that cost four PRs
repeated rebases for changes with nothing to do with each other: a menu fix, a
header label and a marquee perf fix.

Enable the merge driver once per clone:

```sh
tools/git/install-merge-driver.sh
```

It parses both sides plus the ancestor, three-way merges the *decoded* `html`
payload (8000+ ordinary lines), and splices the result back into the existing
bytes, so the artifact's formatting never drifts. Two branches editing
different parts of the editor stop conflicting.

It deliberately refuses rather than guess. The document's `text_bindings`,
`layout_bindings` and `paint_bindings` address DOM nodes by *positional path*,
and are derived from `html`: a binding set computed against one side's html is
not valid once the other side's insertions also land. So the driver merges only
when `html` is the single key either side changed, and falls back to an
ordinary conflict whenever a binding list differs, the decoded payload itself
conflicts, or its own post-merge check finds the result is not both sides'
changes composed. Of the 76 changes in this file's history whose parent is
comparable, 73 touched `html` alone and would be permitted.

A counter both sides advance to the same value also conflicts rather than being
taken once -- the case where "both sides agree" is exactly what makes the result
wrong, and where git's own merge of the decoded content accepts it silently. It
happened twice in one night elsewhere in this organisation: a header census
(212 + 212 -> 213, truth 214) and a `receipt_binding_count` (9 + 9 -> 10, truth
11), the second forcing two PRs to land strictly sequentially.

And nothing may appear in the merged document that was in none of the three
inputs. That is a stronger guarantee than "both sides' changes survive",
because it catches the merge *inventing* material rather than losing it -- a
three-way merge elsewhere produced 204 placeholder entries in a file that had
zero on both the PR head and the base.

Two lanes inserting a child at the same point in the same parent also conflict
rather than composing -- the case where both children would survive and every
binding indexing a later sibling would be wrong. Two insertions at *different*
points do merge, and the driver does not pretend otherwise: a single insertion
renumbers later siblings by the same mechanism, so that staleness is a property
of editing this document, not of merging it.

The driver is optional. `.gitattributes` names it, but git will not run a
command a repository supplies, so it lives in local config. Until you install
it nothing breaks -- you just keep conflicting. Coverage:
`ctest -R Spectr-materialized-merge-driver`.

Git reads `.gitattributes` from the branch you have checked out, so a branch
created before this landed keeps conflicting until it merges `main` once. That
first merge is the old behaviour; every merge after it uses the driver. This is
deliberate rather than worked around -- see the note in
`tools/git/install-merge-driver.sh` for why making the rule branch-independent
would risk silently dropping one side.

When a conflict *is* reported here, resolve it by re-running the relevant
`tools/patch_materialized_*.py` script against the merged document. Never
`git checkout --theirs`: it discards the other side's edits wholesale, and this
artifact is one line, so that is the entire document.

## License

TBD.
