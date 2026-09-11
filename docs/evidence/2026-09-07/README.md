# Spectr UX re-verification evidence — 2026-09-07

Every file here is a measurement, not a narration. The detector that produced
the JSON verdicts is `tools/appearance_invariants.py`; re-run it against any
`*.layout.json` in this directory to reproduce the numbers.

> **The commands below were written against an earlier CLI and will not run
> as printed.** `appearance_invariants.py` was rewritten to adjudicate painted
> glyph geometry instead of layout boxes, and the squash that landed that
> rewrite took the new module with the old callers. `--subtree`, `--only` and
> `--plant <name>` are gone; the flags are now:
>
> | printed here | today |
> |---|---|
> | `--subtree <node-id>` | `--region x,y,w,h` (a box, not an id — look the node's rect up in the dump) |
> | `--only clip` | the `painted-vs-measured` detector, which always runs |
> | `--plant clip` | `--plant-overflow` |
> | `--plant overlap` | `--plant-overlap` |
> | `--only collapse`, `--only wrap` | **no successor — see below** |
>
> The verdicts recorded in this file still reproduce. Worked example, checked:
> the OVL-3 CLIP at `OVL-README.md` is `--region 540,60,240,26` today and still
> reports `225.0px in a 210.0px box` RED on `move4` and GREEN on `press`.
>
> **A real coverage gap, not just a rename:** the old WRAP and COLLAPSE
> detectors have no replacement. Today's module adjudicates overlap and fit
> only. Collapse survives only where a check names the string it wants
> (`content_invariants.py` counts a string present only if its box has area)
> or the control it wants (`control_invariants.py` on zero-height tracks) —
> there is no sweep for a collapsed box anywhere on the surface.

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

## CLIP now measures the box a string actually gets

`detect_clip` compared a string's measured width against **its own node's**
rect. A label with a generous box that hangs out of an `overflow: hidden`
ancestor is truncated on screen while its own numbers look fine, and that
comparison cannot see it — the primitive to do it right (`inherited_clip` /
`visible_overlap_box`) already existed in this file and CLIP simply never
consulted it. The box a string gets is its rect intersected with every
`overflow: hidden` ancestor's clip, and that is what it now compares against.

`overflow: scroll` ancestors stay excluded, for the reason already recorded
under `visible_overlap_box`: snapshot coordinates are pre-scroll, so
intersecting a below-the-fold row against its container says "off screen" for
content the viewer scrolls to.

RED/GREEN, on a plant where the string fits its own box and not the ancestor's
clip (text 400px, own box 462px, parent clip 300px):

```
pre-fix   no finding for __behavior_pr_14 — 400 < 462, so the check passes
post-fix  RED CLIP: "APPEARANCE" measures 400.00px wide but paints in a
          300.00px box (overflows by 100.00px, clipped to 300.00px by an
          overflow:hidden ancestor)
```

**This changes no verdict on any real snapshot.** Across all 31 committed
layout snapshots the pre-fix and post-fix runs are byte-identical, so the gap
was latent on this surface rather than hiding a defect. Same positive control
as above (10 of 31 differ under a deliberate alteration), so that null is a
real null. The fix is a guard against a false negative, not a discovery.

### The one real CLIP on this surface, and why it is probably not a defect

`__behavior_pr_4x__text` at `(96, 825.24)` renders `⋯` measuring `8.00px` in a
`7.03px` box — 0.97px over, on the main-window bottom bar, outside the settings
panel. `measured_text_boxes` carries the **advance** width, which includes the
trailing side bearing, so the last glyph's ink stops short of the reported
extent; a sub-pixel overflow is not proof of visible truncation. The snapshot
carries no ink extents, so this is stated as a limit of the instrument rather
than thresholded away. It is recorded as a candidate row, not as a SET-6
finding.

## CLIP is a candidate-finder, not a detector (2026-09-07)

`appearance_invariants.py --only clip` compares a string's *measured advance*
(`measured_text_boxes[].rect.w`) against the box it paints in. Measured against
the render, that advance disagrees with painted ink by **-33% to +21%**:

| node | text | measured | painted ink | box |
|---|---|---|---|---|
| `__behavior_pr_y` | `1.08kHz   11.0 dB   BAND 19/32` | 225.0px | 186.7px | 210.0px |
| `__behavior_pr_p` | `32 bands` | 68.0px | 90.7px | 92.0px |

The error is an order of magnitude larger than the trailing-side-bearing effect
previously recorded here as "sub-pixel rounding", so no width tolerance can
separate a false 15px overflow from a true one.

Both of CLIP's standing violations are now proven false positives, by pixels:

* `__behavior_pr_y` — CLIP reports a 15px overflow. The ink spans 186.7px
  **inset 11.7px on both sides** of its 210px box. Nothing is truncated.
* `⋯` (`__behavior_pr_4x__text`) — CLIP reports 0.97px. Ink is 6.0px in a 7.0px
  box, clear [0.7, 0.4], and the crop shows three complete dots with padding.
  **This resolves the standing caveat on SET-6.**

CLIP has **zero confirmed true positives** across the evidence set, and cannot
have any: sweeping all 31 snapshots, **0 of 756** visible text nodes have a rect
that reaches past its clip — the only geometry in which a glyph can be cut.
(Control: 525 of those 756 do sit under a tighter-than-viewport clip, so the
query can see clips.) No closed row rests on a CLIP true positive, so nothing is
invalidated; the finding resolves a caveat rather than voiding a row.

### `tools/ink_extents.py`

Adjudicates CLIP candidates against painted pixels. Asymmetric on purpose: only
EXONERATED is a conclusion. `--plant` self-tests both claims the tool makes —
that it can see ink (planting a spill must flip a verdict) and that it can see a
clip edge. The clip arm reports **NOT ARMED / UNPROVEN** on all 13 adjudicable
snapshots, because no node's rect reaches its clip; an unarmed control is never
allowed to read as a pass.

Two incidental findings, neither a defect:

* **NO_INK is usually a disabled control, not a missing one.** The
  snapshot-recall buttons paint `▸ A` / `▸ B` at luminance 55 on a 12 ground
  (**1.64:1**) while `CLEAR` in the same frame paints at 219 (13.74:1). Control
  rules out the overlay scrim: the dimness is identical in the no-overlay
  frames. These are the *recall* buttons with nothing stored — WCAG 1.4.3
  exempts inactive components.
* **SPILL is routine and benign.** `32 bands ▾` and `CLEAR` paint 1.8px / 0.7px
  past their own rects; nothing clips them, so the ink is drawn, not cut. 0.7px
  is one image pixel at scale 1.5 — the quantization floor.

Adjudicated: 13 snapshots, all RESOLVED, 0 CANDIDATE. 14 snapshots (OVL-3,
OVL-4, OVL-5) have no PNG and 4 (CUR, SET-1) pair ambiguously with two PNGs
each; those are **not adjudicated**, since a wrong pairing measures the wrong
pixels silently.

---

## COR-4 — resize preserves layout and all controls remain reachable

**Verdict: OPEN.** The editor-side half is proven and no defect was found. The
half a user actually performs — dragging a live host window — is unproven and
blocked by the same locked display as SET-1/SET-2/SET-6 and AUT-3.

Files: `COR-4-{660x430,792x516,990x645,1320x860,1650x1075,1320x500}.{png,layout.json,depths.json}`,
`COR-4-sweep.log`, `COR-4-RED-plant-1320x860.png`, `COR-4-RED-plant.log`.

### The control comes first, because the receipt cannot self-report

Under a pinned design viewport the layout receipt is byte-identical at every
host size *by design* — `src/spectr.cpp:334-366` lays the root out at the
authored 1320x860 box and publishes those dimensions deliberately, and
`publish_native_layout_` early-returns when the box is unchanged. So an
identical receipt is equally consistent with "correct" and with "my resize never
arrived", and the second reading is what voided CUR-1..4.

`prove_resize_reaches_runtime()` therefore reads an effect only the resize path
produces: break the root bounds and require `on_view_resized` to repair them.
Two arms, since a check that cannot fail proves nothing.

```
[control] resize-reaches-runtime host=990x645 broke=640x400
          -> after_break=640x400  (armed=yes)
          -> after_resize=1320x860 (repaired=yes)
```

### The census population is the runtime's own focus_order

My first census guessed selectors (`button`, `[role=button]`) and returned
`total=0` on a surface with 41 focusable controls — a guessed population cannot
report a control it was never told about. It now reads
`__spectrResponsiveLayoutReceipt__.focus_order`, and every id resolves:
`population=41 resolved(byId=41,byAttr=0,bySel=0)`.

The second error was subtler: rects come back in **design space** and are
identical at every host size by construction, so testing them against the *host*
box manufactures a false OFFSCREEN for every control on a small host (my first
counts of 26/23/18). The reachability question is whether a control leaves the
design viewport, which is what the host scales onto the surface.

### Result: invariant across six host sizes

0.5x to 1.25x plus a deliberately wrong aspect (1320x500):

```
cor4-660x430   38/41 | OFFSCREEN __behavior_pr_4i ; OFFSCREEN __behavior_pr_6t ; ZERO __behavior_pr_12
cor4-792x516   38/41 | (identical)
cor4-990x645   38/41 | (identical)
cor4-1320x860  38/41 | (identical)
cor4-1650x1075 38/41 | (identical)
cor4-1320x500  38/41 | (identical)
```

Delta across sizes: zero. All six captures pass the content floor (colors
2584-2651, nonbg 0.848-0.849).

The three residuals are **not** resize failures: all size-invariant, all inside
the closed Settings scroll subtree. `__behavior_pr_4i` and `__behavior_pr_6t`
descend from `__behavior_pr_4t` (h=1227, `overflow: hidden` — the node the
receipt names as `scroll_upgrade.nodeId`); `__behavior_pr_12` sits under
`__behavior_pr_4u`, which is 0x0 while the panel is closed. The receipt reports
`scroll_reachable: true`, and SET-6's scrolled capture shows that content in
full.

### RED arm

A census that only ever names the same three rows is indistinguishable from one
that cannot name anything. `plant_offscreen()` displaces a control the census
just called reachable:

```
[plant] __behavior_pr_q [1059.5,10.5] -> [5059.5,4010.5] moved
PLANT   37/41 | ... ; OFFSCREEN __behavior_pr_q [5059.5,4010.5 92.0x22.0] ; ...
```

38/41 -> 37/41, naming the displaced control. OFFSCREEN armed. HIDDEN fired
during probing (`display:none` -> `HIDDEN __behavior_pr_q`). ZERO fires on
`__behavior_pr_12`. **MISSING has never fired and is unproven** — cite the
detector with that caveat attached.

### Two runtime findings, both surfaced by the plant failing

Neither is a Spectr bug; both are Pulp materialized-runtime behaviours that
silently mislead a detector.

1. **`setLeft`/`setTop` silently drop a unit suffix.** The bridge accepts a bare
   number (px) or a percent string (`runtime.js:6187-6215`); `'4000px'` is
   neither and is discarded with no error. My first plant "displaced" a control
   without moving it and would have certified a dead arm as armed.
2. **A style write's layout does not commit until a layout-affecting write
   follows.** 24 settled frames after `left=4000`, `getBoundingClientRect` still
   returned the pre-write rect. `zIndex` did not flush it; `marginLeft` did, and
   the full displacement appeared at once. A detector that writes and reads in
   one breath sees no change and concludes, wrongly, that nothing moved.

### Why this does not close the row

All of the above runs in the offscreen GPU harness — the shipping runtime and
real pixels, not a browser fixture and not a DOM assertion, but not an installed
build in a resized host window either. The host's `set_design_viewport` scale
and letterbox math never execute here, and the display has been locked
(`CGSSessionScreenIsLocked: True`) all session, so no live-window fixture can
fire. Closing on the editor half would repeat the CUR-1..4 error exactly:
closing a row on the strongest evidence available rather than on evidence that
covers what the row claims. Remaining work is one specific thing — a live
host-window drag.

### COR-4 addendum — the 41-control census was too narrow; a wider one agrees

`__spectrResponsiveLayoutReceipt__.focus_order` is not a focus ring. Its
construction is `values.filter(node => materializedNodeTag(node) === "button"
&& node.style?.display !== "none")` (`runtime.js:9573,9589`) — **visible
`<button>` tags only**. Anything authored as a `div` with a pointer handler —
sliders, knobs, the graph canvas, the minimap trims — is invisible to it. So
"38/41 reachable" is true but covers a narrower population than COR-4 claims,
and a displaced slider would not have been reported.

The wider population is the runtime's own hit-testing accounting rather than
another guess of mine. In the layout snapshot, 151 of 300 nodes carry a
`hit_regions` entry, and every entry is exactly the node's own rect — so a node
carries one when it accepts input. Containers carry one too (the root does), so
the *controls* are the hit-test **leaves**: a node with a hit region and no
descendant that has one. That is **100 nodes**, and it includes both
`Browser_canvas_*` nodes, which the button census could never see.

`tools/reachability_census.py` implements it. Result across the same six host
sizes (`COR-4-reachability.log`):

```
660x430 .. 1650x1075, 1320x500:  population=100  reachable=88  excused=12  findings=0
```

Invariant, and **zero findings** — the same conclusion the narrow census
reached, now over 2.4x the population and covering non-button controls.

The 12 excused rows are proven, not asserted: an ancestry walk shows all 12 pass
through `__behavior_pr_4t` / `__behavior_pr_4u`, the closed Settings scroll
nodes (`overflow: hidden`, the node the receipt names in `scroll_upgrade`).
Zero residuals fall outside that subtree. The exclusion is not a way to make the
number look good — with no allowlist the same run reports `findings=12`, and a
bogus allowlist (`--allow-under __no_such_node__`) excuses nothing, so the
allowlist does real work and matches only what it names.

RED/GREEN, on the post-plant snapshot at the same size:

```
baseline    population=100 reachable=88 excused=12 findings=0   exit 0
post-plant  population=100 reachable=87 excused=12 findings=1   exit 1
            FINDING  OFFSCREEN __behavior_pr_p [5072.5,4013.0 92.0x22.0]
```

The detector also refuses to run rather than pass vacuously: zero hit-test
leaves exits 2 with "the snapshot carries no hit_regions, so nothing could ever
be reported" — the failure mode that let my first census report `total=0` as
though it were a clean sweep.

Arms still unproven on this wider detector: **HIDDEN and ZERO** never fire here,
because no hit-test leaf is currently invisible or degenerate. Only OFFSCREEN is
armed. Cite it with that caveat.

This does not change the verdict. COR-4 stays **OPEN** on the live host-window
half; the editor-side half is now proven over a materially better population.
