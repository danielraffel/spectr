# HIT-TARGETS — the pointer must reach what the eye sees

Three reports from the shipped build, one property:

> **5.** "in settings tapping anywhere on the toggle should adjust it eg
> disable/enable — right now if i tap on the circle part it does nothing"
>
> **2.** "i can't seem to tap on snapshot a or b and get that to work (maybe
> it's just a small tap area and needs to be enlarged)"
>
> **1.** "in settings can the thumb be easier to grab once it's enlarged near
> hover"

Every number below is in the authored **1320x860** design space. The shipping
standalone pins that box into a **990x645** window, so a design number is
**0.75** of what the user's pointer sees — which is why controls that look
adequate in a layout dump feel small in the product.

## What was actually wrong

### 5 — the toggle knob. A real dispatch bug.

The switch is a 40x20 `<button>` with an absolutely positioned 16x16 `<span>`
knob. In a browser a press on the knob bubbles to the button. Under Pulp it did
not, and the reason is specific: `View::hit_test` returns the **deepest**
hit-testable view, and the bubble walk that follows stops at the first ancestor
carrying an `on_click` — but the knob carries **one of its own**.

Measured by `SPECTR_HIT_PROBE`, with the toggle ON so the knob sits at the
right-hand end:

| probe point | owner | press |
|---|---|---|
| x=844 — bare track | `spectr-status-info-toggle` | flipped `on` → `off` |
| x=855 — bare track | `spectr-status-info-toggle` | flipped `off` → `on` |
| **x=864 — the knob** | **`__behavior_pr_3v`** | **`on` → `on`, dead** |

```
[chain] 0 __behavior_pr_3v 16x16 handlers=click pointer dom-pointer
```

The two bare-track presses are the positive control: the instrument can see a
flip, so the third reading is the control and not the probe. The knob is 16 of
the toggle's 40px, and it travels to whichever end matches the value — so the
dead 40% was always the end the eye is drawn to.

**Bubbling cannot fix this**; the knob's own handler shadows the button's. The
fix is `pointerEvents: "box-only"` — this view receives events, its children do
not. The shipping document already relies on it: `SnapBtn` carries it, which is
why a press on a capture button's 6x6 status dot resolves to the **button**.
That is the in-document positive control for the mechanism.

### 2 — SNAPSHOT A/B. Not a dispatch bug.

A press at the centre of `spectr-snapshot-capture-a` resolves to the button and
**fires**: the status line went `(none)` → `SNAPSHOT A CAPTURED`. Hit rect
equalled painted rect exactly, with nothing swallowing it. What is true is the
size — 35x26 design px is **26.3 x 19.5 pt**.

The two RECALL buttons additionally refuse a press **by design** while their
slot is empty (`disabled`), which is enforced but signalled only by opacity.
That half is an affordance question and is deliberately not touched here.

### 1 — the settings slider thumb. One of the two possible complaints.

**Tap-to-move works** and was never broken: a press at 25% of the track moved
the thumb from x=824 to x=705, on both the click and the drag channel. So
"a plain click-to-move is unreliable" is **not** a defect.

What was real is that the grab target never followed the paint. The thumb is
`pointerEvents: none`, so the **track** owns every press — and the track is
exactly 156x16 whatever the thumb does:

| state | painted thumb | outside the pressable rect |
|---|---|---|
| idle | 14x14 at (824.0, 420.5) | 7px past the right end |
| hovered | 18x18 at (822.0, 418.5) | 9px right, 1px above, 1px below |

At either end of travel half of what the user aims at could not be pressed, and
the hover growth a sibling lane shipped made the overhang **worse**.

## The rule, stated once

> A control's hit rect must (a) contain every pixel it paints, and (b) be at
> least **38 design px** in the axis where it is short — 28.5 pt at 990x645,
> the HIG figure for a comfortable pointer target.

Nothing changes paint or layout. SET-9 is binding. `hitSlop` is React Native's
exact tool: `View::hit_bounds()` is `local_bounds()` grown by it, consulted by
`hit_test` and by nothing that draws.

Growth is capped by neighbours, measured on a dump taken with **both LFOs
enabled** (the densest the settings panel ever gets):

| constraint | measured | taken | left over |
|---|---|---|---|
| tightest toggle ↔ toggle gap | 26.0 | 9 + 9 | 8.0 |
| tightest slider ↔ slider gap | 30.0 | 11 + 11 | 8.0 |
| SNAPSHOT horizontal gap | 8.0 | 3 + 3 | 2.0 |
| SNAPSHOT row vertical clearance | 14.5 | 6 | 8.5 |

## Before and after

| control | painted | hit BEFORE | hit AFTER |
|---|---|---|---|
| settings toggle | 40 x 20 | 40 x 20 (30.0 x 15.0 pt) | **40 x 38** (30.0 x 28.5 pt) |
| settings slider track | 156 x 16 | 156 x 16 (117.0 x 12.0 pt) | **174 x 38** (130.5 x 28.5 pt) |
| SNAPSHOT capture A/B | 35 x 26 | 35 x 26 (26.3 x 19.5 pt) | **41 x 38** (30.8 x 28.5 pt) |
| SNAPSHOT recall ▸A/▸B | 39 x 26 | 39 x 26 (29.3 x 19.5 pt) | **45 x 38** (33.8 x 28.5 pt) |

The slider's horizontal 9 is not an ergonomics number: it is the hover thumb's
overhang exactly, and it is what makes rule (a) true at both ends of travel.
After the change the hovered thumb's right edge sits at overhang `R = 0.0` —
contained, with nothing to spare, by construction.

**Paint did not move.** Across all three dumps, 410 nodes each: *0* painted
rects and *0* measured text boxes changed. One node's rect differs — the toggle
knob at x=857 → x=837 — and that is the knob travelling to the `off` position
because the third press now works.

## Known gap, named rather than hidden

`spectr-snapshot-morph` (90x16, 67.5 x 12.0 pt, thumb 7px outside its track) is
**not** fixed here. Its track style is the exact literal that
`tools/patch_materialized_morph_affordance.py` matches on; a second writer there
would make that script's replay exit 1 at a patch point it can no longer find,
and those scripts are what let concurrent branches merge. `hit_target_reach.py`
prints its measured numbers on **every** run as a standing statement.

## Artifacts

| file | what it shows |
|---|---|
| `RED-hit-transport.layout.json` + `.depths.json` | pre-fix transport row: four SNAPSHOT buttons, hit rect == painted rect |
| `GREEN-hit-transport.layout.json` + `.depths.json` | post-fix: 41x38 and 45x38, 2.0px neighbour gaps |
| `RED-hit-settings-modulation.layout.json` + `.depths.json` | pre-fix settings with both LFOs open: 6 toggles + 7 slider tracks at 20 and 16px |
| `GREEN-hit-settings-modulation.layout.json` + `.depths.json` | post-fix: every one at 38px, thumbs contained, no overlap |

All four come from the **shipping materialized artifact** through
`Spectr-native-shot --backend=skia`, i.e. the same `Processor::create_view()` /
`ScriptedUiSession` path the plugin mounts — not a browser reference.

## Reproducing

```sh
SPECTR_HIT_PROBE=1 ./build/Spectr-native-shot --out=/tmp/shots --backend=skia --scale=1
python3 tools/spectr-detectors/hit_target_reach.py /tmp/shots/hit-transport.layout.json
python3 tools/spectr-detectors/hit_target_reach.py /tmp/shots/hit-settings-modulation.layout.json
```

The probe opens Settings, expands both LFO disclosures, reports painted vs hit
rect per control, and — decisively — resolves **which view owns each probe
point** before pressing it. "Nothing happened" and "a child swallowed the press"
are otherwise indistinguishable.

`SPECTR_HIT_SLOP_DIAG=1` adds the discriminator that found the second half of
this bug: it writes `hitSlop` twice on one control, once through the style
object and once through the bridge function, and prints the rect after each.

## Negative controls

| arm | result |
|---|---|
| `hit_target_reach.py --plant` (hit rects shrunk onto paint) | exit 1 on all three dumps |
| the detector on the real **pre-fix** dumps | exit 1 — 4, 14 and 19 findings |
| remove `pointerEvents: "box-only"`, rebuild | knob owner reverts to `__behavior_pr_3v`, third press dead again |
| neuter `call("setHitSlop", …)` in `runtime.js`, rebuild | every hit rect collapses to `grown=+0.0x+0.0`; knob still works |

The last two are what prove the two fixes are **orthogonal and each necessary**:
breaking either leaves the other intact.

## A pre-existing flake found on the way, and NOT caused here

`Spectr-browser-ux-polish` (test #279) fails intermittently on `origin/main`
with the artifacts of this branch reverted:

| artifacts | runs | failures |
|---|---|---|
| pristine `origin/main` | 4 | **3** |
| this branch | 3 | 2 |

The oracle `<pre>` comes back **empty** — not `SPECTR_POLISH_ORACLE_ERROR` — and
the label alternates between `overflowing` (height 860) and `fitting`
(height 1800) from run to run. A layout assertion that fails at *both* heights
alternately is a timing signature, not a geometry one; the runner gives Chrome
`--virtual-time-budget=15000` and the settings scenario's waits sit close to it.

This is recorded because the first pristine run **passed**, which briefly read as
"my change broke it". One reading of a flaky test is a snapshot, not a baseline.
Fixing it is not this lane's work; it is left named rather than silently
absorbed.

The other failing test, `the settings copy button centres its feedback and
answers a press` (`26.0f < 25.0f`), is the already-known pre-existing failure and
is likewise untouched here. Everything else is green: **280 of 282**.
# Evidence — 2026-09-12

## Slider thumb: pill shape and inset travel

Spectr's design source drew every slider — the four settings sliders
(`SSlider`) and the home morph slider (`MorphSlider`) — as a native
`<input type="range">` tinted with `accentColor: hsl(200,80%,60%)`. The native
editor cannot host a range input, so both were reimplemented as custom
`div[role=slider]` nodes with a hand-drawn thumb, and that reimplementation
drew a circle. Nothing chose the circle as a design; it was incidental to the
port. The thumb is now a pill: `22x14` idle, `26x16` hovered, fully rounded.

`slider-thumb-BEFORE-circle.png` and `slider-thumb-AFTER-pill.png` are the
same 460x120 design-px region of `05-MODULATION-lfo1-expanded-UNWEDGED.png`
(the settings Rate slider at 4.00), at 4x nearest-neighbour, from the two
captures below.

### Measured, `Spectr-native-shot --backend=skia`, same worktree and SDK pin

| surface | painted track | thumb before | thumb after |
|---|---|---|---|
| home `spectr-snapshot-morph` | `90x16` | `14x14`, **7px outside** the left edge | `22x14`, inside |
| settings Rate | `156x16` | `14x14`, **7px outside** the right edge | `22x14`, inside |
| settings Depth | `156x16` | `14x14`, **7px outside** the right edge | `22x14`, inside |
| settings LFO 2 rate | `156x16` | `14x14`, inside (mid-travel) | `22x14`, inside |
| settings LFO 2 depth | `156x16` | `14x14`, inside (mid-travel) | `22x14`, inside |

The overhang was not a rounding artifact: the circle was positioned
`left: ratio%` with a FIXED `marginLeft` of half its width, so it hung half
outside the track at both ends of the travel and only sat inside in between.
The pill insets its own travel (`marginLeft: -(width * ratio)`), which is what
a real range input does, so its left edge runs `0 .. trackWidth - width`.

Painted track boxes and hit rects are byte-identical before and after — this
changes the thumb, not the control's footprint. Nearest-neighbour clearance on
the transport row improved with it: the gap from the morph thumb to
`spectr-snapshot-recall-b` went `7.0px -> 14.0px`.

One overlap on that row is unchanged by this work and predates it: the flanking
`A` label's node box sits at the morph track's left edge and intersects the
`SET A + B` caption by `5.4 x 12.0px`. `box_intersection.py` reports it
identically on the before and after dumps, with `evidence=box` (widget boxes,
not proven glyph occlusion), and exits 0 in both.

### `slider-hover-PILL-{idle,hover}.layout.json`

Two `visual-layout-snapshot-v1` dumps from one
`SPECTR_HOVER_PROBE="878,132 753,374"` run — the same probe spec the
2026-09-11 pair used, over the settings Rate slider. Thumb
`__behavior_pr_1u` reads `22x14` idle and `26x16` hovered, inside its
`156x16` track in both frames.

They exist because the 2026-09-11 GREEN/RED pair is a **circle-era** capture.
`slider_thumb_hover_growth.py` located its thumb by requiring `w == h`, so
against a pill it would have found nothing at all and failed for the wrong
reason. The finder is now shape-agnostic (thumb-shaped, not square) and prints
each frame's aspect ratio; this pair is what proves it still resolves and
adjudicates the shape that actually ships. The 2026-09-11 pair is kept and
still runs — a growth detector should read dumps from both eras.

Read them with `tools/spectr-detectors/slider_thumb_hover_growth.py`; its
`--plant` flag compares the idle dump against itself and must fail.

### Where the shape itself is adjudicated

`tools/spectr-detectors/slider_thumb_pill_shape.py`, against the CHECKED-IN
`materialized-document.runtime.json` rather than a capture. Every state
`Spectr-native-shot` captures has the morph slider disabled, where its thumb is
`opacity: 0` and the `SET A + B` caption shows instead — so no capture can
adjudicate the morph half, and a fixture would only ever record one build. Two
independent negative controls, because it makes two independent claims:
`--plant circle` reverts the shape on both components, `--plant overhang`
keeps the pill and reverts only the travel.
