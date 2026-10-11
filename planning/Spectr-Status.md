# Spectr Status — Live Handoff Dashboard

_Last updated: 2026-10-10. This is the concise state-of-the-world for
Spectr. Refresh it whenever the product branch or its landing gates change._

## Release 1 product state

Release 1 is a zoomable spectral-isolation effect:

- 32, 40, 48, 56, or 64 logarithmic bands over a continuously adjustable
  frequency viewport;
- nonadjacent frequency islands and categorical mute, with muted mask bins
  written as exact zero;
- Pulp's shared `SpectralMaskProcessor` WOLA engine, using the Balanced
  8192-sample FFT / 2048-sample hop profile by default and reporting 10,240
  samples of latency;
- the reviewed Claude Design HTML embedded source-preservingly in a native
  Pulp WebView, with C++ owning audio, state, and host integration;
- AUv2, VST3, CLAP, and Standalone arm64 artifacts. No PKG, signing,
  notarization, or public distribution claim is part of this local M5 trial.

The active product branch is `recon/real-analyzer-integration-20260810` at
`e2b565e142e82b05ba2d1ab60d515cf3864b763d`, seven clean commits ahead of
Spectr `origin/main` at `36dfe1bc`. The Release 1 work is committed locally
but has not been pushed, PR'd, or merged. The ignored `native-react/`
experiment is not part of the shipping branch.

## Pulp SDK and fresh build evidence

The latest qualifying local integration SDK was built from Pulp integration
head `8c0d2cd6012e49b92e62427e40d47c6dce3fc443`, including:

- the transport-less VST3 host fix — hosting no longer fabricates a frozen
  playing/sample-zero process context;
- the inspector-off CLI fix — the development SDK links while preserving
  explicit live-control-unavailable behavior; and
- the realtime visualization bridge — audio callbacks perform only bounded,
  allocation-free capture while the UI thread owns FFT publication.

Exact immutable SDK identity:
`8c0d2cd6012e49b92e62427e40d47c6dce3fc443/3d9a0ae9f29c`, installed at
`~/.pulp/sdk-dev/forge-v1/darwin-arm64/8c0d2cd6012e49b92e62427e40d47c6dce3fc443/3d9a0ae9f29c`.

The fresh arm64 Release build proves `-O3 -DNDEBUG` and:

- 73,219 assertions across 124 native product cases;
- 128/128 complete CTest cases, including the deterministic Chromium editor
  oracle and real built CLAP/VST3 artifact hosting;
- exact-mute, upper/lower viewport rejection, and three nonadjacent stereo
  frequency-island projection checks;
- the exact packaged Standalone headless GPU/no-audio-device boundary;
- CLAP validation with zero applicable failures and `auval` success; and
- ad-hoc deep-signed AU, VST3, CLAP, and Standalone arm64 trial artifacts.

This is integration evidence, not a landed-Pulp or released-SDK claim. The
Pulp fixes must land and Spectr must be rebuilt against their authoritative
published development SDK before the product PR.

## Installed host matrix

| Surface | Recorded result | Remaining qualification |
|---|---|---|
| AUv2 | `auval` passes `aufx:Spec:Pulp`, including render, latency, state, custom UI, channel, and sample-rate checks. Logic Pro 12.3 discovers the current AU, opens the live editor, processes audio, accepts band painting, and displays the live post-effect analyzer. The initial musical trial is positive. | Repeat mute plus close/reopen twice with no non-finite banner; verify Logic Musical Typing does not trigger Spectr shortcuts; manual 800x521 sizing gate. |
| VST3 | The real built-bundle host test passes audio/state/exact-mute and the three-island stereo projection. | Current authoritative-SDK REAPER GUI/state/resize/bypass/PDC trial; pluginval is unavailable and is not claimed. |
| CLAP | The real built-bundle host test passes audio/state/exact-mute and the three-island stereo projection. CLAP validator has zero applicable failures. | Current authoritative-SDK REAPER GUI/state/resize/bypass/PDC trial; shared manual 800x521 gate. |
| Standalone | The packaged headless boundary test passes. The plot region is pixel-identical between the post-fix 120- and 600-frame captures; the settled capture restores the plot, rulers, spectrum, mask, minimap, and resolution disclosure. A visible launch opens stereo CoreAudio at 48 kHz / 256 samples and the 1320x860 native WebView editor. | Manual native edge dragging at the declared 800x521 minimum remains open because the app window is not exposed through the available accessibility control. |

The Logic result above is a current human trial, not inferred from metadata.
Prior REAPER discovery trials proved format visibility, but the final REAPER
matrix remains open until it is repeated with artifacts rebuilt from landed
Pulp dependencies.

## Remaining Release 1 gates

1. Land Pulp realtime visualization PR #7399, then the inspector-off CLI and
   transport-less VST3 host fixes; publish/select the resulting authoritative
   development SDK.
2. Rebuild all four Spectr formats against that SDK and rerun native,
   Chromium, artifact, validator, and CLI-first audio gates.
3. Complete the current Logic reopen/Musical Typing/minimum-size checks, the
   final REAPER VST3/CLAP matrix, real Standalone audio I/O, and the declared
   800x521 / 1320x860 / 2640x1720 size matrix.
4. Run the exact semantic audio gates before Audio Quality Lab regression
   comparisons; Quality Lab is advisory for perceptual change, not the oracle
   for exact mute, leakage, islands, or latency.
5. Push the reviewed Release 1 commits, open the product PR, and attach Whence
   provenance to the committed canonical goal:
   `pulp-planning-spectr/research/spectr-dsp-gap.md` on planning `main`.
6. Merge only after required checks and the final diff/provenance review pass.

## Release 2 product direction

Before expanding the product surface, Spectr has a renderer-modernization
phase. The completed WebView editor will first be landed as an immutable,
reproducible reference build. A separate worktree/branch will then implement a
parallel native editor through Pulp's DesignIR/View tree and Skia/Dawn. The two
lanes will share DSP, state, analyzer, preset, undo, automation, modulation, and
interaction contracts and will be compared using the same fixtures and host
matrix. Native becomes the default only after visual, behavioral,
accessibility, persistence, host, and performance gates pass; the WebView
baseline remains available for rollback and regression comparison. The
executable phases and cutover criteria live in
`planning/Spectr-Cutover-Gap-Tracker.md`.

The native migration is also a Pulp importer qualification project: gaps found
through Spectr should become reusable native-import/runtime capabilities rather
than accumulating as a Spectr-specific rewrite. The destination is a browser-
free editor rendered through Skia/Dawn, while preserving the source and
behavioral evidence supplied by the Claude Design prototype.

Release 2 remains effect-centric: add stereo, multi-layer freeze, sampling,
and polyphonic MIDI-keyboard playback to the same effect. An instrument
variant is a contingency only if the supported host matrix proves that a
single effect identity cannot reliably combine audio input, MIDI input,
freeze capture, recall, and keyboard playback. No Release 2 freeze or sampler
implementation is claimed by the current branch.

## References

- Canonical active goal:
  <https://github.com/danielraffel/pulp-planning/blob/main/research/spectr-dsp-gap.md>
- Product contract: `planning/Spectr-V2-Product-Spec.md`
- Pulp handoff: `planning/Spectr-V2-Pulp-Handoff.md`
- Future freeze/sampler scope: `planning/Spectr-Sampler-Phase-Spec.md`
- Ordered native-then-sampler execution:
  `planning/Spectr-Native-Then-Sampler-Plan.md`

## 2026-10-09 bounded authored FilterBank re-import browser gate

The next importer slice is now exercised by a fresh Chromium run from the
current materialized artifact. The harness
`tools/authored_reimport_filter_bank_app_mount.mjs` regenerates the dependency
manifest and authored `FilterBank.tsx`, compiles it with the pinned TypeScript
toolchain, injects it into a served copy of `resources/editor.html`, and runs
the real ReactDOM/App mount with deterministic native state and analyzer frames.
It captures before/after screenshots, checks changing analyzer traces, samples
canvas ink, and verifies every 32- and 64-band hit-test position. A planted
no-ink mutation must fail the central-canvas ink gate.

Run-5 receipt and screenshots are retained at
`/Volumes/Workshop/Code/agent-artifacts/spectr-authored-filter-bank-reimport-20261009-run5/receipt.json`.
The exact source identities are:

- materialized artifact SHA-256
  `05e56e1f56baf6f8324f88083db2816fcf482fe0d0c7e363991499428ea20c8c`;
- generated authored `FilterBank.tsx` SHA-256
  `3f5ef95e055ce64c3259433c16cc89a5788ffee739c9e84aeb16cba6d1f5568f`;
- editor template SHA-256
  `a5ab76e4b841a74f5749c49a9370db9daf3cfd37577cb46e3a313ac0e215a65a`.

The positive lanes mounted with zero runtime errors, two analyzer emissions,
changed canvas hashes, central-canvas colorful-pixel counts above 150,000, and
complete 32/64-band geometry. The no-ink control was rejected with zero canvas
ink. CMake registers this as optional
`Spectr-browser-authored-filter-bank-reimport` when Node and Chrome are
available, and the acceptance pattern includes it through `^Spectr-browser-`.
The run-5 receipt SHA-256 is
`121070ec9f75ecb303e5918d08a6e5140fb777ac5a7ce5d21543de0f5e555c1f`.

The follow-on full-App closure probe now runs against the current materialized
runtime, which contains the complete helper/component closure missing from the
older Claude template. `tools/authored_reimport_full_app_materialized_browser.mjs`
regenerates and provenance-checks the authored `App`, injects it into a copy of
the materialized runtime, and compares baseline/re-imported ReactDOM trees in
Chromium. Run-8 evidence is retained at
`/Volumes/Workshop/Code/agent-artifacts/spectr-authored-full-app-materialized-reimport-20261009-run8/receipt.json`
(SHA-256
`ddc8f4b675d3ef9a7c5411b989f5fcd35996d9fb9aef0805a05a5f671704e0e0`). Both
baseline and re-imported captures mounted three canvas layers, received
sequence 1 then sequence 2 through the native analyzer publication surface,
had zero runtime/browser errors, showed positive first-canvas ink, proved the
authored App invocation count was 0 for baseline and 5 for re-import, and
produced the identical settled screenshot SHA-256
`f91e4a193087f1719415e49267bd20c1595ae5b40f4514d7df8047dd0f6ffff7`.
The planted hidden-node App mutation was rejected by the DOM parity gate.
The CMake browser lane registers this probe alongside the FilterBank gate.

This closes a staging proof for the complete authored App closure only. It
still does not replace `editor.html`, establish native Skia/Dawn parity, or
qualify production cutover.

This is staging/browser evidence only. `resources/editor.html` and the
materialized runtime were unchanged. The complete authored `App` closure is
verified against the current materialized runtime; the older Claude
`editor.html` template still lacks newer helpers such as `SpectrControlMenu`,
so replacing that template, native Skia/Dawn parity, and production cutover
remain open.

## 2026-10-08 authored App-mount re-import staging proof

The authored `MBtn.tsx` slice now has a bounded App-level re-import proof. Commit
`ac40c536865def0a0938997b3de1bc4561b53b2c` adds the fixture and harness on
branch `codex/spectr-authored-app-mount-20261008`; it is not pushed or merged.
The run14 receipt is
`/Volumes/Workshop/Code/agent-artifacts/spectr-authored-app-mount-20261008-run14/receipt.json`
(SHA-256
`59c00eb90f5cfbf1356c5048e0eaee61f6d68c9f88647e0e3c5b5f95d0fbdcdd`).

The proof compiled the authored TSX with the pinned TypeScript toolchain,
matched the template render tree, rejected a planted `height: 26 -> 27`
mutation before browser execution, mounted the real ReactDOM App, opened
`PRESETS` and `MANAGE`, invoked the re-imported `MBtn` six times, and found
baseline/patched manager DOM parity. Chromium captured both states; the
manager screenshot visibly contains the factory band preview rows and the
mounted App reports two canvas layers. The baseline and patched screenshot
artifacts are respectively
`/Volumes/Workshop/Code/agent-artifacts/spectr-authored-app-mount-20261008-run14/baseline.png`
(SHA-256
`2a9c47669024d8a9327fce7414dba62a4fa2640e7b7e3471035bdf95eafe5a42`) and
`/Volumes/Workshop/Code/agent-artifacts/spectr-authored-app-mount-20261008-run14/patched.png`
(SHA-256
`aae1e4337213f362c5cecc9ddd601a7320b88ddec8ea321be0cc5795a64ff784`).

This remains staging evidence only: `editor.html` and the materialized runtime
were unchanged, `production_cutover` is false, and `full_native_parity` is
false. A full authored editor cutover, native parity, and release qualification
remain open.

## 2026-10-08 authored PatternRow band-preview staging proof

A second bounded authored-source slice now exercises the manager's visible band
previews through the real App mount. Commit
`2904e0176e930f0810c80dcb6684b976c0c18750` on branch
`codex/spectr-pattern-row-app-mount-20261008` adds authored `PatternRow` and
`MiniPreview` fixtures plus their App-mount harness. The run3 receipt is
`/Volumes/Workshop/Code/agent-artifacts/spectr-pattern-row-app-mount-20261008-run3/receipt.json`
(SHA-256
`f02b6713bcceffeaf6e045dae02d96c1e69e36c3942e4edd0442b09de47a42b2`).

The proof compiled both TSX modules, matched the adapted template render tree,
rejected a planted `gap: 10 -> gap: 11` mutation before browser execution,
mounted the real ReactDOM App, opened `PRESETS` and `MANAGE`, invoked authored
`PatternRow` eight times, found manager DOM parity, and verified eight pattern
rows with 261 SVG band bars in Chromium. Baseline and patched screenshots are
captured in the receipt. The harness reports the exact fresh-worktree preflight:
`npm ci --ignore-scripts --prefix tools/wp1-parser`.

This remains staging evidence only: `editor.html` and the materialized runtime
were unchanged, `production_cutover` is false, and `full_native_parity` is
false. A production component cutover and native parity remain open.

## 2026-10-08 source-import regression gate

The source comparator on branch `codex/spectr-source-comparator-20261008`
(PR <https://github.com/danielraffel/spectr/pull/237>) now records strict
Chromium evidence for both the Claude standalone source and the current
`resources/editor.html`, including root/canvas readiness, console/network
failures, screenshot hashes, and a planted no-ink failure control. The latest
same-browser comparison passed for both old and new HTML: each mounted one
root with two canvases, zero console/network failures, and nonzero central
canvas ink. The old standalone source is SHA-256
`7ae6f1d807f2f356b8473d9e672a95535adbe7affea58af360f9ac5c211daf9e`; the
current editor is SHA-256
`402ee225bdab0983863fd0e526cc2f760d84c34fa58aeb67f9462e143eb2a42f`.

The comparator also accepts an importer browser capture and checks its
provenance source SHA plus a higher-contrast graph-region ink threshold. The
current standalone re-import capture fails this gate with zero pixels above
the threshold (its graph-region maximum RGB sum is 43), even though the source
capture itself passes. This is intentional evidence of an importer fidelity
gap: importer self-consistency and `Similarity: 100%` do not prove that the
nonempty analyzer content survived. Evidence is retained at
`/Volumes/Workshop/Code/agent-artifacts/spectr-source-comparator-pr-20261008/import-standalone-20261008T1315/`.

This remains browser/import evidence only. Native Skia/Dawn parity, production
cutover, and a repaired nonempty importer capture remain open.

## 2026-10-08 PR queue and Shipyard stewardship snapshot

This is a point-in-time read-only snapshot, not a claim that the backlog is
static. At `2026-10-08T21:57:51Z`, `ghapp` listed 25 open Pulp pull requests
and 9 open Spectr pull requests. The live lists are:

- Pulp: <https://github.com/Generous-Corp/pulp/pulls?q=is%3Apr+is%3Aopen>
- Spectr: <https://github.com/danielraffel/spectr/pulls?q=is%3Apr+is%3Aopen>

The Spectr provenance gate is PR
<https://github.com/danielraffel/spectr/pull/250> at commit
`86e6ddaec314e78d08f237ca04e90db2ee74fe61`. Its 36-test WP-1 regression
suite, four provenance controls, and direct Node verifier pass; the M5 product
acceptance check is still running. The PR records staging identity only and
does not claim production cutover or native parity.

Pulp uses a GitHub merge queue. During this observation window PR
<https://github.com/Generous-Corp/pulp/pull/9930> advanced from queue position
1 and merged at `2026-10-08T21:54:34Z`; the queue was empty on the following
read while auto-merge remained armed on other PRs. Direct merging around that
queue is unsafe. Spectr has no configured GitHub merge queue.

Shipyard `0.283.0` currently provides useful read-only landing, queue, and
`pr-watch` primitives, but it does not own the complete open-PR lifecycle:

- `shipyard ship-state list --json` contains three durable records (two failed
  and one passed), rather than the complete Pulp/Spectr backlog;
- `pr-watch wakes` reports zero raised/sent/seen/open wakes, while its digest
  would send one flag for Pulp PR
  <https://github.com/Generous-Corp/pulp/pull/9825> (required `macos` red for
  more than 234 minutes while auto-merge is armed but not queued);
- the actionable wake producer is disabled because no repository policy is
  configured, and queue-observe snapshots are stale/manual rather than an
  always-on follow process.

The safe ownership boundary is therefore: Shipyard should collect bounded
GitHub state continuously, persist exact head/base/check/queue transitions,
bucket open PRs by deterministic blocker, and deliver one acknowledged wake to
the responsible coordinator. Agents should own code fixes and runner repair.
Mutation should remain opt-in and exact-head/green-gated after a read-only soak;
automatic rebases, force-pushes, direct merges, and blind red-check reruns stay
disabled. A queue-steward health heartbeat and a daily immutable digest should
make stale transport, missing ownership, and unresolved PRs visible without
requiring an agent to poll manually.

## 2026-10-08 generated runtime-client staging contract

The renderer-neutral `pulp.postMessage` seam now has a deterministic staging
client contract on Spectr branch
`codex/spectr-runtime-client-contract-20261008` at final implementation commit
`57533422a7605785fbcff712f4b4bbcf6cd47ef3`. The reviewed branch head is
`d934e761e165edb7b47a886058ef8b115fe9b6ea`. The generator reads the C++
`add_handler` registrations from `src/editor_bridge.cpp` and
`src/ui/editor_view.cpp`, checks literal command calls in
`native-ui/materialized/spectr-native-services.js`, and emits 54 handler
methods plus a TypeScript declaration and source-hash manifest under
`native-ui/materialized/generated/`. Six focused controls pass: deterministic
repeat output and Node dispatch, TypeScript declaration compilation, checked-in
manifest verification, missing C++ handler rejection (including the lifecycle
`editor_ready` handler), and unknown service command rejection. TypeScript
validation fails closed when the pinned compiler is unavailable. The CMake acceptance test is
`Spectr-wp1-runtime-client-contract`.

This is staging evidence only. `resources/editor.html`,
`native-ui/materialized/runtime.js`, and
`native-ui/materialized/materialized-document.runtime.json` are unchanged;
the generated client is not wired into the shipping runtime and does not claim
browser/native parity or production cutover. The SDK materialized-runtime
producer remains a separate Pulp follow-up.

## 2026-10-08 runtime-client bridge-source correction

The staging generator was corrected after adversarial review to scan the active
native bridge (`src/editor_bridge.cpp` and `src/ui/native_editor.cpp`). The
browser adapter lifecycle command `editor_ready` is now represented explicitly
as `adapter_only`, while native strict service references remain limited to the
active bridge. The checked-in manifest records strict, optional, and adapter-only
service references. The rebased implementation head is
`00fa31a9b4a193740a2f0dc0f86f5c6218355f2e`; `node tools/generate_spectr_runtime_client.mjs --verify`
and all six focused contract tests pass. This remains staging-only: no shipping
runtime, authored `editor.html`, or native/browser parity claim changed.

## 2026-10-08 runtime-client contract PR

The corrected staging contract is open for hosted validation at
https://github.com/danielraffel/spectr/pull/254 with exact head
`5b598ddc541001f8f96dcdd7ddedae194f4bb35a`. The Spectr M5 product-acceptance
gate is queued at
https://github.com/danielraffel/spectr/actions/runs/37863531996/job/113604703004.
Local generation verification and six focused tests pass. The branch remains
staging-only and does not establish authored-source replacement, native/browser
parity, or production cutover.

## 2026-10-09 hosted acceptance for runtime-client contract

The exact PR 254 head `473ff1dcd5dab1c33835cb03244cc1187b9e97e6` passed the
Spectr M5 product-acceptance run:
https://github.com/danielraffel/spectr/actions/runs/37863575839. Static contract,
provenance, focused behavior, native capture, control-reachability, packaging,
and AUv2 validation completed successfully. The release configure log explicitly
reports the browser oracle disabled because Node/Chrome were unavailable on that
runner; independent Chromium evidence remains the authoritative browser receipt.
The app-driven detector failures are non-blocking and remain known acceptance
gaps. This PR still stages the runtime-client contract only and does not claim
production runtime adoption or native/browser pixel parity.

## 2026-10-09 runtime-client facade adoption (in progress)

PR 254 is now merged at
<https://github.com/danielraffel/spectr/commit/a06987e9f141c69d33673f6f66f6936a15e088e1>.
The follow-up adoption is being developed in a fresh worktree on branch
`codex/spectr-runtime-facade-adoption-20261009`. The maintained native service
bridge embeds the generated inline client and routes valid `pulp.postMessage`
requests through its allowlisted command set before invoking the existing
processor dispatch core. Unknown commands are rejected before the processor;
the old direct dispatcher remains available only as the parity baseline in the
test harness. `resources/editor.html` and
`native-ui/materialized/materialized-document.runtime.json` are unchanged.

Local evidence currently passing:

- six generated-contract tests, including deterministic equality of the new
  inline artifact;
- materialized paint micro test and generated-service synchronization check;
- Node syntax checks for the maintained service and materialized runtime; and
- VM plus real headless Chromium parity. Seven valid commands produce equal
  envelopes, traces, and responses with and without the facade. The baseline
  forwards `unknown_command` (eight processor calls), while the facade rejects
  it before dispatch (seven calls). Both browser captures are identical at
  SHA-256
  `3418c31839ea0e96655f5f586a6f749ca90e7b887cb934e679e9f16522571f21`
  (4,428 bytes).

The CMake browser lane now registers this parity harness when Node and Chrome
are available. Commit, governed gates, and merge remain open; this is scoped
adapter parity evidence and does not claim full editor/native pixel parity or
production cutover.

Adversarial review found no functional issue. Its independent checks passed:
six runtime-client contract tests, generated/client replay and materialized
runtime synchronization, JS syntax, paint micro and commit-scope tests, eight
full-App runtime-surface tests, five runtime-facade contract tests, and the
same VM/Chromium parity harness. A deterministic exhaustive probe exercised
all 54 generated handlers plus adapter-only `editor_ready`: baseline and
facade each made 56 processor calls with identical JSON results and traces
(`firstDiff = -1`). It used a fake JSON echo dispatcher, so this establishes
command-adapter behavior rather than full `editor.html` or native pixel parity.
The review's one documentation mismatch in the generated ESM header was fixed
and the generated module and manifest were refreshed.

The review also found that hosted acceptance did not verify that the generated
inline client matched the block embedded in the maintained service. The CMake
suite now registers `Spectr-runtime-client-inline-sync`, and the contract suite
includes a planted mismatch that changes embedded `ab_toggle` to `ab_togglx`.
The clean copy passes while the planted copy exits nonzero with the stale-facade
diagnostic. All seven contract tests and the direct inline sync check pass.

## 2026-10-10 parity closeout fixture wave

The parity closeout implementation is pushed at
https://github.com/Generous-Corp/spectr/commit/24d97a8. The current work proves
shared deterministic analyzer fixture provenance across Chromium, native CPU
Skia, and native GPU Dawn/Metal. The fixture is
`test/fixtures/spectr-parity-analyzer-v1.json`; its SHA is
`68987ed2f756a919ef7e7476f538c53a9702ab141bcfa2790341e4e2ec5c33f8`.

Completed evidence:

- production `Spectr-native-parity-deterministic` CTest passes;
- exact-head native CPU and GPU captures pass analyzer response and plot-region
  gates, with Dawn/Metal initialized on Apple M3 Ultra;
- strict Chromium capture proves source readiness, fixture acceptance, two
  canvases, exact 1320x860 logical / 2640x1720 device dimensions, and zero
  console/network errors;
- three deterministic browser captures and repeated native CPU/GPU captures
  are byte-identical;
- malformed fixture, stale fixture hash, planted image mutation, and optional
  generic HTML compatibility controls fail or pass through their intended
  paths;
- autoreview and adversarial review have been run repeatedly during the wave.

Current honest result:

- strict browser/native pixel parity remains open. The latest CPU join has
  4,331,167 differing pixels and the GPU join has 4,366,704. Mean errors are
  2.604556 and 2.608550. These are renderer/layout composition differences,
  not stale state or fixture drift.
- generic live Claude standalone HTML renders successfully in Chromium but is
  intentionally not treated as deterministic parity evidence because its live
  analyzer changes between captures;
- CTest receipt-wrapper rows may skip when the configured Python lacks Pillow;
  direct `/usr/local/bin/python3` harness runs are the authoritative local
  receipts until hosted CI supplies the dependency;
- Shipyard/PR merge proof and final thermonuclear review remain open.

## 2026-10-10 exact-head parity closeout plan and review update

The closeout plan is now explicitly staged so pixel parity and release work
remain separate, testable gates:

1. **Contract hardening:** keep one checked-in analyzer fixture and state
   manifest; authenticate source, fixture, native binary, readiness, and
   screenshot provenance in every browser/native receipt.
2. **Production capture:** rebuild the native shot from the exact branch head,
   exercise CPU Skia and GPU Dawn/Metal, run the settled Chromium capture, and
   repeat each deterministic lane for byte identity.
3. **Parity verdict:** run strict CPU and GPU joins without weakening thresholds.
   A failed join stays open and is reported as renderer/layout composition work
   until a measured implementation change improves it.
4. **Adversarial closure:** require the plot-region content floor to inspect the
   authored plot bounds, and require the CTest row to pin the fixture SHA before
   accepting a native semantic pass.
5. **Landing:** refresh the durable Pulp planning ledger, rerun autoreview and
   thermonuclear review on the final exact head, then use Shipyard/required CI
   and verify the merged SHA and ancestry before closing the goal.

Current exact-head evidence after the P2 fixes:

- branch head is now `66cb13c`; the explicit plot bounds, authenticated CTest
  wrapper, browser/native-equivalent fixture validation, and receipt-embedded
  state/binary authentication are committed and
  pushed;
- CPU Skia and GPU Dawn/Metal deterministic captures pass response readiness;
- the direct authenticated CTest wrapper passes and a planted fixture mutation
  fails on SHA mismatch;
- strict joins still honestly fail at 4,331,167 CPU pixels / MAE 2.604556 and
  4,366,704 GPU pixels / MAE 2.608550, preserving the renderer/layout gap;
- Chromium remains settled with two canvases, accepted analyzer sequence 2,
  zero console/network errors, and byte-identical repeated screenshots.

The final exact-head state manifest is `state-v12` with state SHA
`065afa1bc753c17899b7e633cbaa2d9a868280d992ab96480297291968a20d41`; the
rebuilt native binary SHA is
`9ee49779cd2ab9916b61dab82268241cfb9ec453b9d517a236c5dc9da40103d9`.

The next action is to commit this hardening, rerun both independent reviews,
refresh the exact-head receipts, and then drive PR
<https://github.com/Generous-Corp/spectr/pull/270> through merge proof.

## 2026-10-11 artifact-authority parity finding

Ultra’s source audit found that `resources/editor.html` and the shipping
`native-ui/materialized/materialized-document.runtime.json` are intentionally
different artifacts. The native surface contains mature LENGTH, MIX, INTENSITY,
OUTPUT, AUTO, PEAK, and tracking controls that the browser source does not.
Replacing the materialized artifact with `editor.html` would discard existing
product behavior, so it is not an acceptable pixel-parity fix.

The same-artifact experiment extracted both materialized document forms for
Chromium. The plain document references external materialized assets and fails
with file-not-found loads. The runtime document is self-contained enough to
load, but still requires the native host bridge and does not mount under the
generic browser oracle. This proves the next parity slice must build a
materialized-runtime Chromium oracle with the same host bridge and state
contract. The current strict `editor.html` versus native-materialized join is
retained as a diagnostic and is not treated as evidence that the importer
itself can reach pixel parity.

## 2026-10-11 Ultra plan: same-artifact parity and remaining opens

Ultra reviewed the current receipts, native source path, materialized runtime,
and comparator. The next wave is deliberately phased:

1. **Authority contract.** Record browser and native artifact identities in
   every parity state. The existing `editor.html` versus materialized-runtime
   join remains a diagnostic and must be labeled as an authority mismatch.
   Comparators must fail closed when a receipt claims strict parity without
   matching authority identities.
2. **Same-runtime browser oracle.** Serve the checked-in
   `materialized-document.runtime.json` through Chromium with the same
   `pulp.on` and `pulp.postMessage` host contract used by native capture.
   Inject the shared analyzer/state fixture, prove readiness and canvas
   ownership, exercise one interaction, and require two byte-stable settled
   captures with zero console/network failures.
3. **Measured join.** Compare that browser capture with CPU Skia and GPU
   Dawn/Metal using existing strict thresholds and provenance receipts. Keep
   plot, chrome, and footer regions separate so a renderer defect is
   attributable instead of hidden by a whole-frame score.
4. **Renderer repair only after authority alignment.** If the same-runtime
   join still differs, use regional diffs and interaction traces to fix the
   smallest shared Pulp/runtime cause. Preserve mature Spectr controls and do
   not replace native materialized authority with the older Claude source.
5. **Closeout.** Re-run adversarial review, thermonuclear autoreview, governed
   native CPU/GPU captures, Chromium repeatability, and Shipyard/required CI;
   merge only after exact-head and ancestry proof.

Current status: contract hardening and deterministic native/browser receipts
are complete; the authority-aligned Chromium oracle, same-artifact join,
renderer repair (if still needed), and merge proof remain open. Kill criteria:
do not add a generalized frontend graph or rewrite the materialized runtime if
the oracle cannot establish a measurable reduction in ambiguity or diff size.

## 2026-10-11 authority guard implementation

Commit `680ebd5dec46dc59f18071f4343ba5774a4819cd` adds the explicit authority
declaration to `native-ui/capture-states/manifest.json` and registers a
positive CTest contract plus a planted missing-authority negative control.
The direct positive and negative checks pass. This prevents the current
cross-artifact comparison from being described as importer pixel parity, but
it does not claim that the same-runtime Chromium oracle exists yet.

The existing authored full-App/materialized browser harness is the scaffold
for that oracle. It now runs after `npm ci --ignore-scripts --prefix
tools/wp1-parser`; its current mock state is intentionally insufficient for
parity because it omits mature native fields. The next implementation must
serve the materialized envelope assets, inject React vendors without replacing
the retained native services, and derive the bridge payload from
`make_editor_state_payload` before comparing screenshots.

## 2026-10-11 authority-aligned Chromium oracle implementation

`tools/materialized_runtime_chromium_oracle.mjs` now provides the first
repeatable browser capture of the shipping materialized runtime itself. It
serves the runtime HTML and all 13 embedded assets over HTTP, injects the
shared deterministic analyzer fixture through the retained native-message
seam, uses the native 1320x860 / DPR 2 viewport, waits for fonts and analyzer
sequence 2, and requires three canvas layers, zero application console or
network failures, a real band-dropdown interaction, and byte-identical
settled screenshots. The runtime and source editor remain unchanged.

CTest registers the positive lane as
`Spectr-browser-materialized-runtime-authority-oracle` and a self-validating
missing-font negative control as
`Spectr-browser-materialized-runtime-authority-oracle-negative-control`. Both
focused tests pass on the M5 checkout. The receipt schema records the runtime
artifact SHA, HTML SHA, analyzer fixture SHA, canonical state digest, viewport,
asset count, interaction evidence, and the explicit
`same-materialized-runtime-diagnostic` authority mode.

This closes the browser evidence gap but does not claim native pixel parity or
production cutover. A same-authority native join still reports a large measured
renderer/composition difference and remains the next repair gate; the existing
`editor.html` versus materialized-runtime comparison remains diagnostic only.

## 2026-10-10 corrected state hydration and authenticated same-artifact join

The authority-aligned browser bridge now mirrors the native
`make_editor_state_payload` contract for the mature controls that were missing
from the first oracle pass: nested freeze state and canonical length values,
standalone keyboard policy, Tracking latency (`64` samples / `1.333` ms with
the `10240`-sample mixing option), default modulation routes and depth, native
level fields, synchronous `output_levels_get`, and the initial output-meter
publication. The source remains
`/Volumes/Workshop/Code/spectr-parity-closeout-20261010/tools/materialized_runtime_chromium_oracle.mjs`;
`node --check` passes.

The corrected Chromium receipt is
`/Volumes/Workshop/Code/agent-artifacts/spectr-materialized-runtime-oracle-state-corrected-20261010-r2/receipt.json`
(SHA-256 `ba9ded8c21268a2a0f9f9932efceafa21e6a074cd7d474d0cfc02901b3007263`).
It proves three canvas layers, analyzer sequences 1 and 2, a real dropdown
interaction, zero application console/network errors, and byte-identical
settled captures at 2640x1720. The settled image is
`/Volumes/Workshop/Code/agent-artifacts/spectr-materialized-runtime-oracle-state-corrected-20261010-r2/settled.png`
(SHA-256 `ffe13738d0a2db300cb9dfb10377699f23351b7aa5424ee5f9de377d956f4335`).

`tools/join_materialized_runtime_parity.py` now authenticates both receipts,
the shared materialized artifact, analyzer fixture, native binary, viewport,
and PNG dimensions. It normalizes the browser receipt's `dpr` spelling to the
state manifest's `deviceScaleFactor` and rejects conflicting aliases. The
focused boundary test is registered as
`Spectr-materialized-parity-join-boundary` and passes; the one-pixel planted
negative also passes.

The authenticated diagnostic join is retained at
`/Volumes/Workshop/Code/agent-artifacts/spectr-materialized-runtime-parity-join-20261010.json`
(SHA-256 `38eb365bd76a2afefe138bbf06375c12493f514f82c848d2bc10e6c1daf326c5`).
It reports `4,328,800 / 4,540,800` differing pixels and MAE
`2.5303022595` against the CPU Skia capture, a small improvement over the
synthetic bridge but still far from parity. The negative-control receipt is
`/Volumes/Workshop/Code/agent-artifacts/spectr-materialized-runtime-parity-negative-20261010.json`
(SHA-256 `17c83d18a665dd1dbd55f971546aad3806fb5785720ce5740e250020a4e5ea55`).
The remaining gap is therefore renderer/layout composition and text fitting,
including the visible `AUTO` and Tracking footer clipping; no threshold was
weakened and native parity remains open.

## 2026-10-10 adversarial old/new source regression rerun

The old Claude Design source and the current `resources/editor.html` were
captured again in the same Chrome 155 process with the strict root/canvas,
canvas-ink, and zero console/network-error checks. Both mounted one root and
two canvases at 1320x860 / DPR 1. The old source is
`/Users/danielraffel/Downloads/Claude/Spectr/Spectr (standalone).html` with
SHA-256 `7ae6f1d807f2f356b8473d9e672a95535adbe7affea58af360f9ac5c211daf9e`;
the current editor is `resources/editor.html` with SHA-256
`5425add49d538713ef24b25f8a50b15d5f981b2db47fe1c0e880d749a931f558`.

The old source is animated and is not screenshot-byte-stable: three clean
runs produced screenshot hashes `db710f0a8965a08cad8becf4599fd569afdbce00e601a79ac0be11678e448b54`,
`f938ec5a5d42e386a6b91e8d209dd3b85c7f2b651ee004cefd2d4ecfc67b7e7b`, and
`7ffdf17dfbf0b661a9039441b24c92d4320143ba3f2b9ec6e8cd12a1bdc0cb7d`.
The new editor is stable across reruns at screenshot SHA-256
`f95b6dc204a847f54996c47144bcb3100715988ff4d5e78a43940beae949ec0e`.
The old/new image difference is therefore diagnostic rather than an exact
parity verdict; it also includes intentional state differences (old IIR/FFT/
HYBRID and 64 bands versus new BARS/RESPONSE/BOTH, 32 bands, and CPU
disclosure). The planted no-ink mutation still fails the old-source gate, so
the regression harness detects a completely blank render while preserving
this animation caveat.

The comparator now records per-canvas ink and supports
`--require-primary-canvas-ink`. An adversarial copy with all drawing methods
disabled on the first canvas still passed the aggregate ink check through the
overlay canvas; the new primary-layer check rejects that mutation. The
standards/quirks fixture suite and fresh old/new captures pass with the
primary-layer check enabled.

The captured receipts and screenshots are retained under
`/Volumes/Workshop/Code/agent-artifacts/spectr-old-new-regression-20261010/old-claude/`,
`/Volumes/Workshop/Code/agent-artifacts/spectr-old-new-regression-20261010/new-editor/`,
and the primary-layer rerun under
`/Volumes/Workshop/Code/agent-artifacts/spectr-old-new-regression-20261010/primary-rerun/`.

## 2026-10-11 oracle hardening and fresh native join

The authority-aligned Chromium oracle now authenticates the selected React and
ReactDOM vendor IDs and SHA-256 values, classifies only the known Chrome GCM
authentication diagnostics as non-fatal, and keeps the missing-asset control
as a normal expected-failure assertion. The authority manifest negative control
now mutates a copy and proves the validator rejects the missing field; it no
longer relies on CTest `WILL_FAIL`.

The materialized runtime's focused typography migration was applied through
`tools/patch_materialized_editor.py --readable-typography-only` to remove the
viewport-wide font-size override that clipped `AUTO` and the Tracking footer.
The checked-in artifact provenance manifest was refreshed and its four focused
source/artifact tests pass.

The native target was rebuilt through the governed build wrapper. The current
binary SHA is `76c76e84681305781e2a5ba0da127cb0eb9bd4496e7a60a8854e38ecd2db146e`;
the materialized runtime SHA is
`00a8ca66cd628f3d960ddcf1486ee89e33e432df4691f7f7e409ba4c75ac5212`.
The fresh browser oracle and native CPU Skia capture share the same artifact,
fixture, viewport, state digest, and binary identity. The authenticated join
passes all provenance checks and remains a measured diagnostic failure:
`4,327,187 / 4,540,800` differing pixels, MAE `2.4101314746`. No parity
threshold was weakened; the remaining work is renderer/composition repair.

The current-head receipts supersede earlier pre-typography-refresh artifacts.
They are retained in
`/Volumes/Workshop/Code/spectr-parity-closeout-20261010/build-native/materialized-runtime-parity/`;
the matching GPU join reports `4,361,926` differing pixels with MAE
`2.4109404180`. The older receipts must not be used for closeout claims.
