# COR-1 · COR-2 · COR-3 · COR-4 — gesture and resize evidence

Everything here was produced by one installed build of `Spectr.app`, and every
number below is re-derivable from the files in this directory with the commands
given. `binary.sha256` is the binary that produced them.

## What the instrument is, and what it is not

Two independent drivers reach the app, and they answer different questions.
Neither alone closes a row.

| driver | how it enters | what it can see | what it is blind to |
|---|---|---|---|
| **stepped probe** (`SPECTR_GESTURES`) | `deliver_mouse_down` / `deliver_mouse_drag` / `deliver_mouse_up` with a `ViewCapture` — the same sequence `PulpMetalView` runs, in **root (design) coordinates** | every sample between press and release, because it reads processing state after each delivered move | the host's window→design pointer transform, which it bypasses; and the `PointerCoalescer`, because it delivers every sample |
| **AppKit fixture** (`PULP_TEST_POINTER_DRAG`, in the Pulp SDK) | real `[PulpMetalView mouseDown:/mouseDragged:/mouseUp:]` in **window coordinates**, through the coalescer | that a gesture lands correctly at a given real window size | anything mid-gesture — only the end state is dumped |

So: the stepped probe carries the *during-the-drag* claims, the AppKit fixture
carries the *does-it-work-at-this-window-size* claim, and they corroborate each
other at the endpoints.

## The controls that make the passes non-vacuous

A drag that misses its target produces a perfectly well-formed sample list in
which nothing changes — which reads exactly like a control that ignores the
drag. Every result below is therefore paired with a control that must come out
the other way.

| control | file | result |
|---|---|---|
| no gesture at all | `appkit-none.state.json` | viewport `20..20000`, 0 bands changed |
| an off-target drag (below the minimap, `y=830`) | `gestures.json`, gesture `offtarget-control` | hits a *different* view (`__behavior_pr_62`), changes neither viewport nor bands |
| a drag in the plot, not the minimap | `appkit-bands.state.json` | 30 bands changed, viewport **unmoved** at `20..20000` |
| a drag on the minimap, not the plot | `resize-*.state.json` | viewport moved to `100.551..13044.700`, **0 bands** changed |
| resize with no gesture | `nodrag-*.state.json` | viewport `20..20000` at every size — so the resize itself moves nothing |
| a request below the declared minimum | `clamp-400x260.state.json` | the host **refused** (`accepted=no`); the window stayed at 990×645 |

The plot drag and the minimap drag move disjoint state. That double
dissociation is what makes each positive result evidence about the control it
names, rather than evidence that any drag moves everything.

The two band checks are also deliberately separate, because they fail
independently. `no-skipped-bands` asks *were all the bands touched* — a hole in
the run is a fast drag outrunning its own painting. `profile-tracks-drag` asks
*were they touched with the right values* — a gesture that reaches the field and
paints every band a single flat value passes the first check and fails the
second.

## Re-running the detector

```sh
python3 tools/gesture_invariants.py \
  --probe        docs/evidence/2026-09-07/cor/gestures.json \
  --opposite-trim mini-left:min_hz \
  --opposite-trim mini-right:max_hz \
  --no-skipped-bands     bands-fast \
  --profile-tracks-drag  bands-fast \
  --span-preserved       mini-pan \
  --no-effect        offtarget-control \
  --resize docs/evidence/2026-09-07/cor/resize-792x516.state.json \
  --resize docs/evidence/2026-09-07/cor/resize-990x645.state.json \
  --resize docs/evidence/2026-09-07/cor/resize-1320x860.state.json \
  --resize docs/evidence/2026-09-07/cor/resize-2640x1720.state.json
```

Recorded output:

```
[opposite-trim] mini-left (dragging min_hz)
  min_hz: 20.0000 -> 80.7217 over 41 distinct values
  max_hz: held at 20000.0000 across all 42 delivered samples
[opposite-trim] mini-right (dragging max_hz)
  max_hz: 20000.0000 -> 4419.7800 over 41 distinct values
  min_hz: held at 80.7217 across all 42 delivered samples
[no-skipped-bands] bands-fast
  painted 29 contiguous bands 2..30 over 63 delivered samples, never skipping and never shrinking
[profile-tracks-drag] bands-fast
  29 painted bands hold 28 distinct values, monotone down from 10.229 dB to -12.856 dB, matching a pointer that moved +1050,+300
[span-preserved] mini-pan
  span held at 1.73841 decades (max drift 3.92e-06) while min_hz moved 80.72 -> 143.00
[no-effect control] offtarget-control
  off-target drag on '__behavior_pr_62' changed neither the viewport nor any band, so the on-target gestures are specific
[resize] 4 window size(s)
  4 host sizes (792x516, 990x645, 1320x860, 2640x1720) share one layout digest 85703ad98c9a, one root (1320, 860), and one gesture outcome (100.551, 13044.7)
GREEN  all requested gesture invariants hold
```

## RED — every check was shown failing

Re-run any row by adding its `--plant` to the command above.

| plant | exit | what it forced |
|---|---|---|
| `opposite-moves` | 1 | a 1% excursion on `max_hz` mid-drag → *"the opposite trim max_hz moved during the drag: 1 of 42 samples differ from 20000 (first at sample 21 = 20200.0)"* |
| `frozen-drag` | 1 | the dragged edge held still → *"took only 1 distinct value… so 'the opposite trim never moved' is vacuous, not a pass"* |
| `band-hole` | 1 | one band removed from the painted set → *"sample 61 touched bands 2..30 but skipped [16] — a fast drag left 1 band(s) behind"* |
| `band-shrink` | 1 | the painted set shrank → *"sample 61 touches 14 bands after sample 60 touched 28"* |
| `profile-flat` | 1 | every painted band given one value → *"only 1 distinct values across 29 painted bands — the drag touched every band but stopped tracking the pointer, which 'no band was skipped' alone would have passed"* |
| `profile-kink` | 1 | one band's value reversed → *"the painted profile is not monotone over bands 2..30: it reverses at band(s) [16]"* |
| `span-drift` | 1 | a 10% span excursion mid-pan → *"the viewport span drifted by 0.04139 decades"* |
| `control-moves` | 1 | the off-target control made to move → *"any drag moves it"* |
| `layout-drift` | 1 | one size's layout digest changed |
| `one-size` | **3** | only one window size given → INCONCLUSIVE, not a pass |

Two further reds use no plant at all, only real data pointed the wrong way:

* `--no-skipped-bands offtarget-control` → RED, *"no band changed over the whole
  gesture — the drag did not reach the band field, so 'no band was skipped' is
  vacuous"*.
* `--resize nodrag-792x516.state.json --resize resize-792x516.state.json` → RED,
  the gesture outcome differs, proving that clause is live rather than
  trivially satisfied.
* `--profile-tracks-drag mini-left` → RED, *"no band was painted, so there is no
  profile to judge"* — the check refuses a gesture it cannot judge instead of
  passing it.

`profile-tracks-drag` also caught a defect in **itself** before it was believed.
Written with a strict `>`, it reddened the real capture at band 30. The cause was
the instrument, not the app: a sculpt move repaints the whole span from the
previously painted band to the current one at the current value, so the final
band and the one before it necessarily share a value. The check is non-strict
now — a **reversal** is the defect, and a curve that stopped tracking is caught
by the distinct-value clause instead.

## Not established here

* **Timing — and the reason is mechanical, not an omission.** "Responsive" is a
  latency claim, and `tools/verify_interaction_perf.sh` is the right instrument:
  it drives real AppKit drags, Perfetto-traces them, and enforces a 120 Hz
  budget bound to exact Spectr and SDK SHAs. It **cannot run against the pinned
  SDK**, because that SDK is built with tracing OFF, so `PULP_TRACE_SCOPE_NAMED`
  compiles to `((void)0)` and no trace file is produced. Measured, with the
  control that makes the zero mean something:

  | probe on the built binary | count |
  |---|---|
  | trace slice name `spectr_processing_state_set` (from `editor_bridge.cpp`) | **0** |
  | control — an ordinary literal from *that same file* | 1 |
  | trace slice name `native_drag_dispatch` (from the SDK) | **0** |
  | control — an ordinary SDK log literal | 1 |

  The literals survive and only the trace names are missing, so this is tracing
  compiled out, not a broken `strings` probe. Turning it on means rebuilding the
  GPU forge-dev SDK with `PULP_TRACING_ENABLED`, which also changes the binary
  under test.

  Separately, and independently disqualifying: this machine was at load average
  **35.67 / 25.22** during the attempt (42 login sessions). A p95/p99 frame-time
  gate taken there measures the host, not Spectr. Both reasons have to be
  cleared before a timing number from this machine means anything.
* **The stepped probe does not exercise the pointer coalescer.** It delivers
  every sample. Coalescing can only remove samples, so the accuracy invariants
  proven here are the stronger statement — but a defect that exists *only* under
  coalescing is outside this probe, and is why the AppKit fixture runs alongside
  it.
* **COR-1's dBFS-semantics half.** The row's other half is a product decision.
  What is proven here is only the drag-correctness half: an edge drag moves its
  own edge and no other.
