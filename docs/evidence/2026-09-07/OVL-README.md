# OVL-1..5 — the status overlay, measured in the installed app

Every artifact here came from `build-8094/Spectr.app` (or an identical build of the
same source and the same pinned SDK), driven headlessly. The layout trees and the
PNG of a run come from the SAME process, so the picture and the measurement
describe one instant rather than two runs assumed to agree.

## The instrument

`SPECTR_DRAG="x0,y0,x1,y1,steps"` delivers the press, each move and the release
SEPARATELY through the same `pulp::view::deliver_mouse_{down,drag,up}` verbs a
window host calls, writing the laid-out tree between them under
`SPECTR_DRAG_DUMP_PREFIX`. `SPECTR_STATUS_PROBE_MS="a,b,c"` writes further trees
at wall-clock offsets after the release.

The tree is the instrument, not `textContent`. The editor writes the live drag
readout straight onto the DOM node, so a value read back from the shim proves
nothing about what the native Label paints; only the snapshot carries the
painted string.

```
SPECTR_DRAG="400,400,760,300,4" \
SPECTR_DRAG_DUMP_PREFIX=/tmp/d \
SPECTR_STATUS_PROBE_MS="1000,3000,5000,7000" \
  ./build-8094/Spectr.app/Contents/MacOS/Spectr \
  --screenshot=/tmp/shot.png --screenshot-frame-delay=25
```

The overlay shell is `__behavior_pr_z`; its text Label is `__behavior_pr_y`.
Both ids were resolved in-process from `[data-spectr-status-shell]` /
`[data-spectr-status-text]`, with `[data-spectr-settings-open]` as the control
that the selector path resolves at all.

## Verdicts

| row | verdict | evidence |
|---|---|---|
| OVL-1 text vertically centered | **RED — open** | glyph band centres 6.67px ABOVE the box centre |
| OVL-2 overlay below the top ruler line | **RED — open** | the overlay covers the ruler line |
| OVL-3 updates while dragging | **CLOSED** | red/green pair below |
| OVL-4 latest status held slightly longer | **CLOSED** | red/green pair below |
| OVL-5 disappears cleanly | **RED — open** | still fully painted 7s later |

### OVL-3 — CLOSED

The banner text changes at EVERY move, before the release:

```
press   '155Hz   0.0 dB   BAND 10/32'
move1   '239Hz   5.0 dB   BAND 12/32'
move2   '369Hz   7.0 dB   BAND 14/32'
move3   '705Hz   9.0 dB   BAND 17/32'
move4   '1.08kHz   11.0 dB   BAND 19/32'   <- still before the release
release '1.08kHz   11.0 dB   BAND 19/32'
```

The planted negative suppresses the update while a gesture is active
(`updateLiveHoverStatus` returns early when `pointerRef.current` is set). It
reproduces the exact defect the row forbids — frozen at the press readout through
every move, updating only at release:

```
press..move4  '155Hz   0.0 dB   BAND 10/32'     (frozen)
release       '1.08kHz   11.0 dB   BAND 19/32'  (only now)
```

```sh
S='1.08kHz   11.0 dB   BAND 19/32'
python3 tools/content_invariants.py docs/evidence/2026-09-07/OVL-3-GREEN-move4.layout.json        --present "$S"   # GREEN, exit 0
python3 tools/content_invariants.py docs/evidence/2026-09-07/OVL-3-RED-planted-move4.layout.json  --present "$S"   # RED,   exit 1
# third control: the plant froze the MID-DRAG update, not the readout itself
python3 tools/content_invariants.py docs/evidence/2026-09-07/OVL-3-RED-planted-release.layout.json --present "$S"  # GREEN, exit 0
```

### OVL-4 — CLOSED

Two messages are fired 800ms apart on the JS clock (SNAPSHOT A, then SNAPSHOT B).
The newer message is held for its own full duration and is NOT cut short:

```
t1000  'SNAPSHOT A CAPTURED'
t1900  'SNAPSHOT B CAPTURED'
t3400  'SNAPSHOT B CAPTURED'   <- ~1.8s after it appeared
t4600  []                      <- and it does clear
```

Planted negative: `holdMs = 200`.

```
t1900  'SNAPSHOT B CAPTURED'
t2500  []                      <- gone
```

```sh
S='SNAPSHOT B CAPTURED'
python3 tools/content_invariants.py docs/evidence/2026-09-07/OVL-4-GREEN-t3400.layout.json               --present "$S"  # GREEN
python3 tools/content_invariants.py docs/evidence/2026-09-07/OVL-4-RED-planted-hold200-t3400.layout.json --present "$S"  # RED
python3 tools/content_invariants.py docs/evidence/2026-09-07/OVL-4-GREEN-t4600.layout.json               --absent  "$S"  # GREEN
```

**A plant that did NOT redden, recorded so it is not retried.** Removing the
`if (generation !== generationRef.current) return;` stale-timer guard changed
nothing: the effect's own `clearTimeout` cleanup already cancels the previous
message's timer, so the guard is redundant belt-and-braces rather than the
mechanism that holds the newest message. The `holdMs` plant above is what
actually exercises the row.

### OVL-1 — RED, open

Measured from `OVL-1-2-RED-overlay-visible.png` (design space; the PNG is 1.5x):

```
overlay box       design y 60.0 .. 85.3    centre 73.0
painted glyph band design y 62.0 .. 70.67  centre 66.33
```

The text sits 6.67px above the box centre; the lower ~11px of a 26px banner is
empty. The layout tree agrees: the text Label is at y=61 h=24 with a 14px text
box at y=61, i.e. flush with the top of its own box.

The authored source already tries to fix this — the span carries
`paddingTop: 6px` inside a flex row with `alignItems: center` — but neither takes
effect, because the box is frozen (see OVL-2).

**Why this is not closed:** no built detector expresses "vertically centred".
`appearance_invariants.py` covered OVERLAP / CLIP / WRAP / COLLAPSE when this was
written; today it covers overlap and fit only, and WRAP / COLLAPSE have no
successor. Either way the defect is measured here but not asserted by a
re-runnable check, and there is no GREEN half.

(`tools/centering_invariant.py` now asserts HORIZONTAL centring of a text run
inside its owner, and is negative-controlled by `--plant`. It does not answer
the vertical question above.)

### OVL-2 — RED, open

The graph's top ruler line is the brightest row left of the overlay, at design
**y = 62.0**. The overlay occupies design **y 60.0 .. 85.3**, so it does not sit
below the line — it starts 2px above it and paints over it. `OVL-1-2-RED-overlay-visible.png`
shows the ruler running in from the left and terminating at the overlay's border.

**Root cause, with a positive control.** The authored style says `top: 104`
(`materialized-document.runtime.json`), which would clear the ruler. It is inert:

| change | reached the binary | moved the overlay |
|---|---|---|
| `layout_bindings[34].box.top` 60 -> 104 | yes (`grep -a`) | no |
| authored style `top: 104` -> `top: 333` | yes | no |
| authored `background` -> `rgb(255,0,0)` | yes | **YES — banner painted red** |

The third row is the positive control: document edits DO reach the render, so the
first two are not a broken build. The overlay's box comes from
`var capturedLayoutBindings` inside **`native-ui/materialized/runtime.js`** —
`{"anchor":"#root","path":[{"tag":"div","index":0},{"tag":"div","index":2}],`
`"box":{"left":540,"top":60,"width":240,"height":26}}`, 12 occurrences, one per
captured state. `applyMaterializedImportMetadata` freezes the node to that box
with `setLeft/setTop/setFlex`, so `top`, `left`, `width` and `height` are all
inert while `padding`, `background` and `color` stay live. That capture predates
the `top: 96` / `top: 104` and dynamic-width edits.

The same frozen box causes a **CLIP** the appearance detector already sees — a long
drag readout measures 225px in a 210px box:

```sh
# `--subtree <node-id>` no longer exists; scope by the node's box instead.
# __behavior_pr_z is [540,60 240x26] in both dumps. Re-checked 2026-09-11:
# still 225.0px of glyphs in a 210.0px box on move4, still fits on press.
python3 tools/appearance_invariants.py docs/evidence/2026-09-07/OVL-3-GREEN-move4.layout.json --region 540,60,240,26   # RED  (exit 1)
python3 tools/appearance_invariants.py docs/evidence/2026-09-07/OVL-3-GREEN-press.layout.json --region 540,60,240,26   # GREEN (exit 0, shorter label fits)
```

**The fix, not applied here:** runtime.js already has the idiom — `activeLayoutBindings`
is filtered by `isSettingsDescendantBinding` and `belongsToAuthoredManagerDetail`
so authored layout wins for those subtrees. A matching `isStatusOverlayBinding`
filter, added through `tools/patch_materialized_editor.py`'s `RUNTIME_EDITS`,
would hand the overlay back to the authored style and close OVL-1, OVL-2 and the
CLIP in one edit. It was not applied because another session was editing this
worktree concurrently.

### OVL-5 — RED, open

After a DRAG the overlay never disappears. Seven seconds after the last update it
is still fully painted: 859 glyph-ink pixels and both border rows.

```
release '1.08kHz   11.0 dB   BAND 19/32'
t1000   '1.08kHz   11.0 dB   BAND 19/32'
t3000   '1.08kHz   11.0 dB   BAND 19/32'
t5000   '1.08kHz   11.0 dB   BAND 19/32'
t7000   '1.08kHz   11.0 dB   BAND 19/32'
```

**Two controls, because "nothing happened" is exactly the reading that lies.**

1. *Timers fire.* `setTimeout` at 500 / 1500 / 3000 / 6000 ms all fired on time
   in the same run (`+773ms`, `+1711ms`, `+3004ms`, `+6003ms`). The overlay's own
   2200ms hold is not sitting in a dead queue.
2. *The clear path works on the OTHER route.* A status fired by a CLICK
   (`EDIT -> LEVEL`, React-driven, no direct DOM write) clears between 1.15s and
   1.6s. So the failure is specific to the drag route, where the readout is
   written straight onto the node with `text.textContent = label` and React's
   `message` never carries the final label.

Pixel verdict, via the existing region tool:

```sh
# banner region must NOT change; a dragged band region MUST change (the control)
python3 tools/scroll_invariants.py \
  docs/evidence/2026-09-07/OVL-5-baseline-overlay-never-shown.png \
  docs/evidence/2026-09-07/OVL-5-RED-still-painted-7s-after.png \
  --header 808,86,1174,132 --body 600,400,1200,700
#   header_fixed=False  body_scrolled=True   -> RED, exit 1

# instrument control: two runs that never show the overlay
python3 tools/scroll_invariants.py \
  docs/evidence/2026-09-07/OVL-5-baseline-overlay-never-shown.png \
  docs/evidence/2026-09-07/OVL-5-baseline-control-second-run.png \
  --header 808,86,1174,132 --body 600,400,1200,700
#   header_fixed=True    -> the region is pixel-identical run to run,
#                           so the RED above is a real difference, not noise
```

(The tool's wording is scroll-specific; the mechanics — one region that must not
change, one that must — are exactly the OVL-5 question.)

**Note on the wrong instrument.** The layout tree alone CANNOT settle OVL-5: it
records the string regardless of `opacity`, so a correctly hidden overlay would
still appear in the tree. The pixels are the oracle here, and the tree is
supporting evidence only.
