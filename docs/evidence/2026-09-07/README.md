# Spectr UX re-verification evidence — 2026-09-07

Every file here is a measurement, not a narration. The detector that produced
the JSON verdicts is `tools/appearance_invariants.py`; re-run it against any
`*.layout.json` in this directory to reproduce the numbers.

## SET-1 / SET-2 — the Settings body laid out at zero height

The whole-image content floor scored the broken frame
`OK colors=3515 lum_sd=8.18 nonbg=0.692 opaque=1.000`, because unique colours,
luminance spread and non-background coverage are all preserved when a panel's
rows collapse onto each other. The appearance invariants read the laid-out tree
instead, and see it.

| file | what it is |
|---|---|
| `SET-1-RED-settings-empty-no8094.png` | Settings on SDK 0.835.0. Header, close control, empty body. |
| `SET-1-GREEN-settings-renders-with8094.png` | Same capture against an SDK built from `0f8ea171e`, the Pulp #8094 fix. |
| `SET-1-RED.layout.json` + `.depths.json` | The tree behind the red capture. |
| `SET-1-GREEN.layout.json` + `.depths.json` | The tree behind the green capture. |
| `SET-1-standalone-installed-app-with8094.png` | The installed `Spectr.app` standalone rendering against the fixed SDK. |

Verdicts, same detector and same surface, only the SDK differing:

```
                       OVERLAP  CLIP  WRAP  COLLAPSE  total
without #8094              359     1     1        13    374   RED
with    #8094                2     1     1         0      4
```

Root cause, measured rather than inferred: the Settings body
`__behavior_pr_4p` laid out `462x0` and the first group `__behavior_pr_26`
`458x0`. Every section header and description in the panel was `458x0` —
APPEARANCE, STRUCTURE, MOTION, FEEDBACK, MODULATION, ABOUT, COPY. Because each
group collapsed to zero height the rows stacked at the same y, which is what
produced the 359 overlapping text pairs. One defect, two symptoms.

## The installed-app pair — SET-1's own caveat, answered

SET-1's status column said **"Not visible in installed builds"** four times over
the row set, and that is the caveat that has to be contradicted with evidence
rather than left standing beside a pass. The standalone can screenshot but had
no way to drive a control first, so `SPECTR_OPEN_SETTINGS=1` opens the modal
through the same `[data-spectr-settings-open]` activation the shipping
open-settings command uses, alongside the existing `SPECTR_BANDS_PERF_FIXTURE`.

| file | what it is |
|---|---|
| `SET-1-RED-standalone-app-settings-empty.png` | Installed `Spectr.app`, Settings open, **empty body**, on an SDK without #8094. |
| `SET-1-GREEN-standalone-app-settings-renders.png` | The same app and the same affordance against the #8094 SDK, fully laid out. |

Same binary path, same frame delay, same activation. Only the SDK differs.

## What these files do NOT establish

AUv2 in Logic is untested here, so SET-1's Logic half is not covered. The
detector verdicts come from the headless harness running the same materialized
runtime against the same two SDKs; the screenshots come from the installed app.
Those are the same code path but not the same process, and that seam is stated
rather than papered over.

## Residual findings on the fixed build, still open

- `CLIP`: `⋯` measures 8.00px wide in a 7.03px box.
- `WRAP`: `ZOOMABLE FILTER BANK` needs 36.00px in a 14.00px box (2.57x).
- `OVERLAP` x2: a Settings row description meets the bottom transport bar.

---

## CUR-1..4 — the cursor over the band canvas, the viewport and its trims

These four rows were once closed on a test named *"native settings command and
minimap cursors reach the shipping runtime"* while their own status column said
**"Not visible in installed builds"** four times. A value reaching a
`View::CursorStyle` slot is not a person seeing a cursor, so they were reopened.

### The two standard instruments cannot tell these rows apart — measured

The red and green captures below are **byte-identical**, and so are their layout
trees:

```
3d671d5a85c063ec2f626fdc9d9d163e3cde95e50380218e60d107ec6404155a  CUR-RED-standalone-app.png
3d671d5a85c063ec2f626fdc9d9d163e3cde95e50380218e60d107ec6404155a  CUR-GREEN-standalone-app.png
6d9f92be641ee3ce220358629580e1e56a30d29c66e587c07cc6e069962dab0e  CUR-RED.layout.json
6d9f92be641ee3ce220358629580e1e56a30d29c66e587c07cc6e069962dab0e  CUR-GREEN.layout.json
```

A broken cursor moves no pixel and no box: a headless capture has no pointer on
screen to photograph, and the cursor is a per-view slot the layout snapshot does
not carry. So a screenshot pair and a layout diff both report "no change"
between a working build and a broken one. **That is why these rows could be
closed on a proxy with a failing caveat beside them, and it is why the
mandate's "a screenshot of the installed build" cannot be met in its literal
form here.** The honest substitute is `CUR-*-annotated.png`: the same installed
capture with every probe point marked and labelled with the cursor resolved
there, so each claim names the pixel it is about.

### What is measured instead

`SPECTR_CURSOR_PROBE` runs inside the installed `Spectr.app` and calls
`pulp::view::deliver_hover_and_resolve_cursor` — **the same function the macOS
window host's `mouseMoved:` calls**, not a copy of its steps. The host and the
probe therefore cannot drift apart; re-implementing the host's sequence in a
test is how "reaches the runtime" came to stand beside "not visible in
installed builds" in the first place.

| file | what it is |
|---|---|
| `CUR-RED.cursor.json` | Every probe point on the installed app with the buttonless-move dispatch removed. |
| `CUR-GREEN.cursor.json` | The same points, same source, dispatch restored. |
| `CUR-RED-annotated.png` / `CUR-GREEN-annotated.png` | The installed capture with each point marked and its resolved cursor named. |
| `CUR-{RED,GREEN}-standalone-app.png` | The unannotated captures (identical, see above). |
| `CUR-{RED,GREEN}.layout.json` + `.depths.json` | The tree behind each capture (identical, see above). |

### Root cause, with the control that makes it a finding

`dispatch_dom_pointer_event` is the only path that fires a JS
`pointermove`/`mousemove`. Before this change it was reached from exactly two
places in the repo — the Android GPU surface and the web event translator.
**Neither macOS host called it.** `mouseMoved:` ran `simulate_hover` (which
flips `hovered_` and calls `on_hover_move`, and runs no JavaScript) and then
read `hit_test(pt)->cursor()`. So the app's `onPointerMove` handler — where
Spectr decides `grab` / `grabbing` / `col-resize` / `crosshair` — never ran on a
hover, and the filter surface reported the cursor it mounted with (`crosshair`)
wherever the pointer went.

Measured directly, with a listener installed on the surface from inside the app:

```
                            JS pointermove events on [data-spectr-filter-surface]
4 hovers, 0 drags   before      0        <- the finding
4 hovers, 0 drags   after       4
0 hovers, 2 drags   before      2        <- the CONTROL: drags always reached JS,
0 hovers, 2 drags   after       2           so a zero above is an absence, not a
                                            dead listener
```

The drag arm is what makes the hover zero mean something. `pointerdown`,
`pointerup` and drag-driven `pointermove` all fired throughout, so the listener,
the element lookup and the dispatch harness were all live while hovers produced
nothing.

### RED then GREEN, one variable

The pair below differs **only** by whether
`deliver_hover_and_resolve_cursor` performs its DOM dispatch. Same Spectr
source, same probe, same points, same SDK build tree; the object file was
deleted before each rebuild and the recompile line confirmed in the build log
rather than trusting `Built target`.

```
$ python3 tools/cursor_invariants.py docs/evidence/2026-09-07/CUR-RED.cursor.json \
    --expect CUR-1-canvas=crosshair --expect CUR-2-viewport=grab \
    --expect CUR-3-viewport-drag=grabbing \
    --expect CUR-4-trim-left=ew-resize --expect CUR-4-trim-right=ew-resize \
    --require-responsive CUR-1-canvas,CUR-2-viewport \
    --require-responsive CUR-2-viewport,CUR-4-trim-left

  OK   CUR-1-canvas          want=crosshair          got=crosshair
  RED  CUR-2-viewport        want=grab               got=crosshair
  OK   CUR-3-viewport-drag   want=grabbing           got=grabbing
  RED  CUR-4-trim-left       want=horizontal-resize  got=crosshair
  RED  CUR-4-trim-right      want=horizontal-resize  got=crosshair
  RED  CUR-1-canvas and CUR-2-viewport both report 'crosshair'
  RED  CUR-2-viewport and CUR-4-trim-left both report 'crosshair'
  exit 1

  (same command, CUR-GREEN.cursor.json)
  OK   CUR-1-canvas          want=crosshair          got=crosshair    ns=crosshairCursor
  OK   CUR-2-viewport        want=grab               got=grab         ns=openHandCursor
  OK   CUR-3-viewport-drag   want=grabbing           got=grabbing     ns=closedHandCursor
  OK   CUR-4-trim-left       want=horizontal-resize  got=…            ns=resizeLeftRightCursor
  OK   CUR-4-trim-right      want=horizontal-resize  got=…            ns=resizeLeftRightCursor
  OK   CUR-1-canvas=crosshair differs from CUR-2-viewport=grab
  OK   CUR-2-viewport=grab differs from CUR-4-trim-left=horizontal-resize
  exit 0
```

### Why CUR-1 needed the responsiveness rule

`crosshair` is the value the filter surface **mounts** with, so a rule that only
asserts "crosshair over the plot" passes on a build where the cursor never
changes at all — CUR-1 reads OK in the RED column above for exactly that reason.
`--require-responsive` names two points that must NOT agree, and it is what
separates "the app is answering" from "the app is showing one constant". In the
green run the same view `__behavior_pr_3` reports **five** different cursors
depending on where the pointer is (`crosshair`, `grab`, `grabbing`,
`horizontal-resize`, and `default` off the plot), which no static value can do.

### The last hop is observed, not transcribed

The in-app probe stops at `View::CursorStyle`. `tools/cursor-proof/` links the
shipping `window_host_mac_geometry.mm.o` out of the SDK archive, calls
`set_ns_cursor_for_style`, and reads `[NSCursor currentCursor]` back:

```
$ tools/cursor-proof/build.sh <sdk-prefix>
OK    CUR-1  style -> crosshairCursor          current=crosshairCursor
OK    CUR-2  style -> openHandCursor           current=openHandCursor
OK    CUR-3  style -> closedHandCursor         current=closedHandCursor
OK    CUR-4  style -> resizeLeftRightCursor    current=resizeLeftRightCursor
GREEN

$ NS_CURSOR_PROOF_PLANT=1 …            # the same check, one expectation swapped
RED   CUR-1  style -> IBeamCursor(PLANTED)     current=<NSCursor: 0x…>
RED   1 of 4 cursor styles mapped to the wrong NSCursor   (exit 1)
```

### Every plant, shown reddening a reading that passes unplanted

```
tools/cursor_invariants.py --plant wrong-cursor   crosshair -> zoom-out          exit 1
tools/cursor_invariants.py --plant no-hit         hit=false                      exit 3 INCONCLUSIVE
tools/cursor_invariants.py --plant freeze         two points forced to agree     exit 1
tools/cursor-proof  NS_CURSOR_PROOF_PLANT=1       one NSCursor expectation swap  exit 1
```

### What this does NOT establish

- **No human has seen these cursors on screen.** The chain is proven to the
  `[[NSCursor …] set]` call and read back through `[NSCursor currentCursor]`;
  nobody has moved a physical mouse over the built app and looked.
- **Standalone only.** `plugin_view_host_mac.mm` (the AU/VST3/CLAP editor host)
  and the iOS host still never dispatch a buttonless move, so a Spectr **plugin**
  editor in a DAW is expected to have the same defect. Measured — both files
  contain zero calls to `dispatch_dom_pointer_event` — but not fixed and not
  verified here.
- **The fix is in an unreleased development SDK.** It is not in any published
  release, so a Spectr built against a shipped SDK still has the defect.
- **One probe run mutates the app it measures.** The probe is latched to run
  once and releases its drag, because a press left un-released poisoned every
  later hover in the same run — the first run reported `grabbing` at *every*
  point, including the plot, which looked exactly like a uniform app defect and
  was an instrument artefact.

## SET-2 / SET-6 / SET-9 re-measured on the SHIPPING SDK

The SET-1 and SET-2 captures above were taken against a pre-release SDK that
carried #8094 but was not a published release. Their own in-frame About block
reads `PULP SDK 0.835.0`. That is not the surface anyone ships, so the three
rows were re-measured against `v0.837.0`.

`SET-837-settings-unscrolled.png` and `SET-837-settings-scrolled.png` print
their provenance inside the frame, which is what makes them auditable rather
than asserted:

| field | value |
|---|---|
| `PULP SDK` | `0.837.0` |
| `SDK SHA` | `6914d57d4d400e93bc85b8a0d7b9567943735769` (matches `sdk-provenance.json`) |
| `SDK SOURCE` | `CLEAN` |
| `SPECTR SHA` | `0a32e524` |
| `BUILD` | `Release` |

### The mechanism the images alone cannot show

A scroll check that compares two PNGs cannot distinguish "the body did not
scroll" from "the body had nothing to scroll", and `scroll_invariants.py` says
so in its own failure text. The layout trees supply what the pixels cannot:

| build | scroll container | body content | `max_y = content - viewport` |
|---|---|---|---|
| no #8094 | `466 x 531` | `462 x 0` | `<= 0` — **cannot scroll at all** |
| `v0.837.0` | `466 x 531` | `466 x 1246` | `715` — scrolled to `y=715.0` |

So the red verdict on the pre-#8094 build is correct *for that build*, and the
reason is a zero-height body rather than a fixture that failed to fire. On the
red build the `SPECTR_SETTINGS_SCROLL` fixture's own `max_y <= 0` guard skips
`set_scroll` silently, which is why the red pair is byte-identical to the
unscrolled capture — that pair is the plant input, not independent evidence.

### What the shipping capture establishes

- `scroll_invariants.py` is **GREEN**: header fixed, body scrolled. Both
  controls redden it (`--plant header-moves`, `--plant body-frozen`).
- The whole-image diff bbox is `(641, 317, 1334, 1088)`. The scroll container at
  scale 1.5 is `(640.5, 317.25)-(1339.5, 1113.75)`. The diff matches the
  container to within a pixel and does **not** touch the header box
  `(602, 138)-(1378, 245)`, so nothing outside the scrolling region moved.
- SET-6: `appearance_invariants.py --only clip --only collapse` scoped to the
  settings panel `__behavior_pr_4u` is **GREEN** over 190 nodes / 75 text nodes.
- SET-9: `text_contrast.py --scale 1.5 --within 400,90.5,520,679` measures 38
  text nodes with **0 contrast violations**; `--plant` reddens it.

### Controls, including one that was invalid

`appearance_invariants.py --subtree` plants on the first measurable node in the
*whole* snapshot, which for this surface is the `SPECTR` wordmark — outside the
settings panel. Scoped runs therefore reported `GREEN` while the tool printed
`BROKEN: the planted negative did not redden any detector` and exited 4. The
valid control plants **inside** the subtree, and both detectors redden:

```
CLIP     __behavior_pr_14(Label) "APPEARANCE" measures 502.00px in a 462.00px box
COLLAPSE __behavior_pr_14(Label) carries text "APPEARANCE" but its box is 462.00x0.00
```

Note that a hand-built plant file needs its `.depths.json` sidecar copied
beside it, or the tool falls back to inferred ancestry and sees 2 nodes.

## What these files still do NOT establish

- **No live window.** These are offscreen Dawn/Skia captures through
  `Processor::create_view()` / `ScriptedUiSession`. The standalone's live-window
  fixtures (`SPECTR_LIVE_CAPTURE`, `SPECTR_OPEN_SETTINGS`, `SPECTR_CURSOR_PROBE`)
  do not run while the macOS session is locked: the editor window opens and
  draws a first frame, but the frame clock never ticks, so
  `tick_native_analyzer_` — which hosts every fixture — is never called. Proven
  with a positive control: the same log contains the `[gpu-host] first frame`
  line, and two independent fixtures emit nothing.
- **No AUv2 in a host.** SET-1's wording covers standalone *and* AUv2. Logic
  needs an unlocked display too.
- **`MOTION` is UNMEASURED for contrast** (0 ink px — it sits below the fold in
  the unscrolled capture). The scrolled capture cannot substitute: its layout
  rects are recorded without the scroll offset, so 18 nodes read as UNMEASURED
  against it. SET-9 therefore covers 38 of 39 settings text nodes.
- **None of these Python detectors is a CI gate.** `add_test` in `CMakeLists.txt`
  registers `Spectr-native-shot` and the browser suites only; the invariants
  scripts are run by hand and their output committed here.
- **One real CLIP violation exists outside the settings panel** and is not
  SET-6: `__behavior_pr_4x__text` at `(96, 825.24)` renders `⋯` measuring
  `8.00px` in a `7.03px` box (0.97px overflow). Recorded, not fixed.

## The `--subtree` control is no longer vacuous

`appearance_invariants.py --subtree X --plant Y` used to plant on the first
text-bearing node in the **whole** snapshot and *then* filter to the subtree, so
the planted defect was discarded before any detector could see it. On this
surface it landed on the `SPECTR` wordmark, outside the settings panel.

The tool caught its own failure and said so — `BROKEN: the planted negative did
not redden any detector`, exit 4 — which is why the SET-6 control above had to
be hand-built as a separate planted snapshot. What hid it from me for a while
was piping the run through `tail -3`: the BROKEN line prints *first*, so the
truncation showed a clean `GREEN` and dropped the verdict. **Do not pipe a
detector through `tail`.**

Fixed by scoping first and planting second. Discriminating control, same
command and same snapshot:

```
pre-fix   CONTROL: planted CLIP: widened __behavior_pr_a  text to box+40px  -> BROKEN, exit 4
post-fix  CONTROL: planted CLIP: widened __behavior_pr_14 text to box+40px  -> RED,    exit 1
```

`__behavior_pr_14` is the "APPEARANCE" label inside the panel. All four plants
now redden under `--subtree`:

```
overlap  RED [OVERLAP=1]    moved __behavior_pr_14 onto __behavior_pr_15
clip     RED [CLIP=1]       widened __behavior_pr_14 text to box+40px
wrap     RED [WRAP=1]       doubled __behavior_pr_19__text text height
collapse RED [COLLAPSE=1]   zeroed height of __behavior_pr_14
```

Behaviour on the no-plant path is unchanged: across all 31 committed layout
snapshots the pre-fix and post-fix runs are byte-identical, stdout+stderr+exit
code. That null has a positive control — re-running the same sweep with the
post-fix side deliberately altered (`--only clip`) reports 10 of 31 differing,
so the comparison can in fact detect a difference.
