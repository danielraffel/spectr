# 2026-09-13 — the morph slider's "unavailable" state

> "there is a little Call To Action text on the morph to tap B … it seems like
> a bug the way it's displayed I like the intent."

The morph slider interpolates the band field between snapshot A and snapshot B,
so with either slot empty it has no endpoints. That has always been enforced,
and since the affordance lane it has also been *explained* — but the
explanation was painted **inside the slider's own 90×16 groove**, so a
half-configured control read as

```
A ———— SET B ———— B
```

Nothing was broken. The text rendered exactly where it was told to. A line of
instructional type running through a slider track simply reads as a rendering
fault rather than as guidance, which is what the report says: the intent was
right, the placement was not.

## What the design system says

The Pulp Design System (`guidelines/motion-states.html`) is unambiguous on
both halves:

* disabled is **`disabled · .42` — opacity only**, with *no instructional text
  on the control*;
* captions are `font-family: var(--font-mono); font-size: 10px; color:
  var(--text-faint)`.

Spelled in Spectr's own vocabulary rather than by importing variable names the
shipping document does not use: `var(--mono)` (already used 35 times in the
document) at `fontSize: 10`, faint via `opacity: 0.55` over the transport bar's
inherited `rgba(255,255,255,0.7)`. That is *exactly* the treatment the
`SNAPSHOT` label four items to its left already uses, and it resolves to about
`#636568` — within a hair of Spectr's own `--dim: #6b7380` token.

So the groove now carries nothing but the slider, the whole row carries one
`opacity: hasBoth ? 1 : 0.42`, and the sentence is a caption under the row:
`SET A + B TO MORPH` / `SET B TO MORPH` / `SET A TO MORPH`.

## `MORPH-CAPTION-{RED-text-in-groove,GREEN-caption-below}.layout.json`

Two real `SPECTR_LAYOUT_DUMP` captures of the shipping standalone home screen,
before and after, at 1320×860 authored space.

| node | RED (shipped defect) | GREEN (this change) |
|---|---|---|
| morph group box | `x=758.188 y=822.500 w=116.750 h=20.000` | `x=758.188 y=822.500 w=116.750 h=20.000` |
| track | `x=770.188 y=824.500 w=90.000 h=16.000` | `x=770.188 y=824.500 w=90.000 h=16.000` |
| clearance left / right | `6.000` / `6.000` | `6.000` / `6.000` |
| text painted inside the track | **`"SET A + B"`** | none |
| caption box | — (inside the groove) | `x=758.188 y=842.500 w=108.000 h=13.000` |
| caption ink | — | `x=758.188 y=842.500 w=108.000 h=13.000` |

**The control did not move.** The group box, the track and both flanking
clearances are identical to the digit in both captures — the caption is
`position: absolute`, so it costs the transport row neither width nor height.

That is not belt-and-braces on the vertical axis, it is the point. An in-flow
caption grows the control's box from 20 to ~35px, and the 56px transport bar
centres its items, so the track would ride **7.5px up** — off the `y=832.500`
centreline its 26px button neighbours sit on — and then **jump back down** the
moment the second slot was captured and the caption unmounted. A control that
moves when it becomes usable is worse than the defect being fixed.

One measured subtlety: nesting the row inside an outer box was not free.
The outer box resolves to 20px while the row inside it is 16, and a Yoga column
defaults to `flex-start`, so the first nested build put the track at
`y=822.500` — 2px high. `justifyContent: "center"` on the outer box puts it
back at `824.500`, which is what the table above records.

## Why this needed a detector of its own

`morph_row_clearance.py` states its rule on the track's **horizontal**
neighbours and says nothing about what is drawn under the row.
`box_intersection.py` compares text-bearing nodes and *did* report the
caption-on-track pair once, as a 5.4×12px finding that was waved through as
pre-existing decoration.

And **no pixel comparison can adjudicate this at all**: the defect rendered
perfectly, so a diff of the two screenshots above scores the RED state as a
faithful render of itself.

`tools/spectr-detectors/morph_caption_below_track.py` states five claims, two
of them about the same defect seen from opposite sides:

* **NO TEXT** — no descendant of the track paints any text. This is the design
  system's rule read straight off the control, and it needs no idea which node
  the caption is. It is what the RED fixture above trips.
* **BELOW** — the caption's box starts at or under the track's bottom edge and
  does not overlap it. A caption re-parented out of the track but still
  positioned over it is still on the control.
* **BOXED** — the caption has a real layout box, non-zero on both axes, that
  contains its own ink. `whiteSpace: "nowrap"` has already made ink measurable
  **on this very row** while the layout box stayed its old size; ink alone is
  not evidence that an element occupies anything.
* **INKED** — it actually paints non-empty text.
* **ALIGNED** — it starts on the control group's own leading edge.

Four plants, one per claim, each touching one thing so no claim can pass on
another's strength: `--plant text-in-groove`, `--plant in-groove`,
`--plant nowrap-only`, `--plant drift`. All six rows (green fixture, red
fixture, four plants) are wired into `tools/ci/detector_selftest.py`.

The caption is resolved **structurally** — "the child of the morph group that
is not the row holding the track" — because the layout dump carries `id`,
`kind`, `rect` and measured text and **no React props**, so
`data-spectr-morph-hint` is invisible to it. That is a stronger handle than the
attribute anyway: it proves the caption is the track's uncle rather than
trusting a `data-` attribute that merely says so.

## The artifact side

`test/test_materialized_morph_affordance.mjs` executes the shipped component
out of `materialized-document.runtime.json` in all four slot combinations,
which a capture cannot do — the app only ever boots into one of them. It gained
the placement claim (`not painted inside the groove`), the one-dim claim, and
two new plants: `--plant-hint-in-track` restores the reported defect verbatim,
and `--plant-caption-into-groove` re-parents *today's* caption into the groove
with its wording and treatment untouched, so the first cannot be passing on the
strength of a changed string.
