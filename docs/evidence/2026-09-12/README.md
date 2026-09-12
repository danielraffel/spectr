# Evidence — 2026-09-12

## Slider thumb: pill shape and inset travel

Spectr's design source drew every slider — the four settings sliders
(`SSlider`) and the home morph slider (`MorphSlider`) — as a native
`<input type="range">` tinted with `accentColor: hsl(200,80%,60%)`. The native
editor cannot host a range input, so both were reimplemented as custom
`div[role=slider]` nodes with a hand-drawn thumb, and that reimplementation
drew a circle. Nothing chose the circle as a design; it was incidental to the
port. The thumb is now a pill: `22x14` idle, `26x16` hovered, fully rounded.

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
