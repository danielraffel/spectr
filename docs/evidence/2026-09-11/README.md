# COPY-WIDTH — settings copy build-info button

## What the defect actually was

Three prior commits (`612fa55`, `f10d913`, `a4332a8`) each touched this button
and each fixed **vertical centering**. None changed its width, which is what is
visible: the button spanned the full 448px About-panel.

The declared style was `alignSelf: 'flex-start', minWidth: 92`. `alignSelf` is
mapped in `core/view/js/web-compat.js` but is **not honoured by the native
lowering**, so the button stretched to its parent instead of shrinking to its
content. A positive control on the same surface proves an explicit width IS
honoured: the `VERSION` row's `width: 72` measures exactly 72.0.

Fix: an explicit `width: 136`, sized from the measured mono-9.5 advance
(5.70 px/char; `COPY UNAVAILABLE` is 16 characters ≈ 104–107 px of ink, plus
20 px padding and 2 px border).

## Artifacts

| file | what it shows |
|---|---|
| `COPY-WIDTH-RED-copy-unavailable.png` | pre-fix raster, longest label, button runs off the panel |
| `COPY-WIDTH-GREEN-copy-unavailable.png` | post-fix raster, longest label fits with symmetric padding |
| `COPY-WIDTH-RED.layout.json` + `.depths.json` | pre-fix geometry: button `__behavior_pr_6t` w=448, same as its parent |
| `COPY-WIDTH-GREEN.layout.json` + `.depths.json` | post-fix geometry: w=136, label box w=114 inside it |

Both rasters were captured from the **shipping materialized artifact** through
`Spectr-native-shot --backend=skia`, i.e. the same `Processor::create_view()` /
ScriptedUiSession path the plugin mounts — not a browser reference.

## Reproducing

```sh
SPECTR_COPY_SHOT=1 Spectr-native-shot --out=DIR --backend=skia
python3 tools/spectr-detectors/copy_button_width_invariance.py \
    DIR/02-settings-SHIPPING.layout.json DIR/05-copy-button.layout.json --expect 136
```

`SPECTR_COPY_SHOT` scrolls the About block into the viewport; without it the
button sits ~670 px below the fold, so a settings capture carries its geometry
but none of its pixels.

The detector's negative control is `--plant`, which forces the button to its
parent's width and must go red.

## The state-invariance measurement

`COPY UNAVAILABLE` is the longest of the four feedback states and is not
reachable by clicking in a headless run, so it was reached by seeding
`useState("COPY")` → `useState("COPY UNAVAILABLE")` in the artifact for the
capture only. Both seeds measure 136. The seed is **not** committed; the shipped
artifact carries `useState("COPY")`.

## Known limitation

The copy label is styled `width:"100%", height:"100%"` by the centering fix,
which makes the layout emitter treat it as a multi-line label and report
`intrinsic_width == 0`. It is therefore one of the runs
`painted_vs_measured_width.py` skips, and no dump-based check can measure its
glyph advance. Ink fit is proven by the rasters above instead.

## Settings slider hover thumb growth (2026-09-11)

`slider-hover-{RED,GREEN}-{idle,hover}.layout.json` are four
`visual-layout-snapshot-v1` dumps from `Spectr-native-shot --backend=skia`
under `SPECTR_HOVER_PROBE="878,132 753,374"`, which drives `simulate_hover`
over the settings "Hover, mute, and drag feedback" slider.

RED is the same source tree with the hover fan-out removed from
`native-ui/materialized/runtime.js`; the thumb `__behavior_pr_1u` stays
`14x14` in both frames. GREEN carries the fan-out and the thumb reads
`14x14` idle -> `18x18` hovered.

Read them with `tools/spectr-detectors/slider_thumb_hover_growth.py`; its
`--plant` flag compares the idle dump against itself and must fail.

---

# TEXT CONTRAST — a finding that needs a product decision, not a fix

`tools/text_contrast.py` reads the rendered PNG (not the authored colour) and
holds each text node to its WCAG floor: 4.5:1 normal, 3.0:1 for painted type at
or above 18.66 device px. It discriminates — planting a dim over the
highest-contrast label drops `'Theme'` from **18.76:1 to 1.70:1** and the run
goes red — so the readings below are the instrument working, not failing.

## The headline number is scoped wrong, and the scoping changes the answer

An UNSCOPED whole-surface run on `SET-837-settings-unscrolled` reports **19 of
57 measured nodes below floor**, `'SPECTR'` among them at 1.29:1. That run is
measuring the toolbar **through the open Settings panel's scrim**, which is
dimmed on purpose. The tool's own `--within` help says exactly this. Controls:

| label | settings OPEN (scrimmed) | settings panel not over it |
|---|---|---|
| `SNAPSHOT` | 1.08:1 | **3.53:1** |
| `Theme` (scoped SET-9 run) | **18.76:1** | — |

A single label moving 15x between two captures of the same surface means the
reading is dominated by the capture's overlay state, not by the design tokens.
So an unscoped contrast run is not a sound product signal and must not drive a
design change on its own.

The run recorded as valid — `--scale 1.5 --within 400,90.5,520,679`, scoped to
the Settings panel — is **GREEN: 38 nodes, 0 violations**, and its plant
reddens it. That is the one wired into `tools/ci/detector_selftest.py`.

## What survives the scoping, and is therefore the real question

Measured on captures where the label is NOT behind a scrim, and identical
across three independent captures (`DDM-1-GREEN-escape-dismissed`,
`OVL-1-2-GREEN-overlay-below-ruler`, `COR-4-1320x860`):

| label | measured | floor | shortfall |
|---|---|---|---|
| `SNAPSHOT` | 3.53:1 | 4.5:1 | −0.97 |
| `ZOOMABLE FILTER BANK` | 3.94:1 | 4.5:1 | −0.56 |
| `1.00x zoom` | 4.49:1 | 4.5:1 | −0.01 |

These are toolbar chrome at small caps sizes. They are genuinely under the
normal-text floor, consistently, with no scrim to explain them away.

## Why this is not being fixed here

1. It is **not one of the user's six named issues**. Changing chrome colour to
   clear a floor is a design change, and belongs to whoever owns the palette.
2. **SET-9 is binding** — keep 990x645, raise type tiers, 9pt AppKit mini — and
   `a36797a` must not be reverted. Any remedy has to work inside that, which
   means a *colour/weight* change, not a size change.
3. `1.00x zoom` at 4.49:1 against a 4.5:1 floor is inside the noise of the
   measurement; it should not be treated as a defect on its own.

## The decision to take

Either (a) lift the toolbar chrome's ink a step so `SNAPSHOT` and
`ZOOMABLE FILTER BANK` clear 4.5:1, or (b) record deliberately that small-caps
toolbar chrome is held to the 3.0:1 large-text floor and state why. Option (b)
is a real answer, but it has to be written down, because the detector will keep
reporting it otherwise.
# CUR-PIN846 — hover cursor probe revived, and what it measured

## The instrument was dead, and every green run was meaningless

`src/ui/native_editor.cpp` guarded its hover branch on `SPECTR_HAS_HOVER_DISPATCH`,
which nothing defines, and the `deliver_hover_and_resolve_cursor()` it would have
called does not exist in the pinned SDK (v0.846.0, source sha `2616a6e9`). Every
hover probe therefore reported `<no-hit>`, so `tools/cursor_invariants.py --expect`
could never pass for a hover point. Only the `x,y>x2,y2` DRAG branch measured
anything.

The fix rewires the branch to the pinned SDK's own hover path — `simulate_hover`
then `hover_cursor_at`, the same function `window_host_mac.mm`'s
`resolveHoverCursorAt:` and `pulp_plugin_apply_hover_cursor` call. No Pulp change,
no SDK re-pin.

## What it measures now (home screen, 1320x860)

| Point | Coord | Hit view | Cursor | NSCursor |
|---|---|---|---|---|
| toolbar button | (712,21) | `__behavior_pr_f` | pointer | pointingHandCursor |
| footer capture button | (630,826) | `spectr-snapshot-capture-b` | pointer | pointingHandCursor |
| inert header label | (90,28) | `__behavior_pr_a` | default | arrowCursor |
| canvas body | (660,300) | `__behavior_pr_3` | crosshair | crosshairCursor |
| viewport strip | (660,779) | `__behavior_pr_3` | crosshair | crosshairCursor |
| trim left / right | (56,779) / (1264,779) | `__behavior_pr_3` | crosshair | crosshairCursor |
| viewport under DRAG | (660,779)->(700,779) | `__behavior_pr_3` | grabbing | closedHandCursor |

Hover cursors change only between chrome regions styled by **static** CSS. Inside
the body, where the JS assigns `grab` / `grabbing` / `col-resize`, hover changes
nothing: viewport and both trims all resolve `crosshair`, while the same point
under a drag resolves `grabbing`.

Root cause, verified at the pinned source ref: `dispatch_dom_pointer_event` has
call sites only in `gpu_surface_android.cpp`, `web_event_translate.hpp`, and three
internal callers in `pointer_dispatch.cpp` — `deliver_mouse_down`,
`deliver_mouse_drag`, `deliver_pointer_terminal`, all button paths. Neither mac
host's `mouseMoved:` delivers a DOM pointermove, so a buttonless move never runs
JS. Tracked in a separate core-Pulp lane.

`--require-responsive CUR-2-viewport,CUR-4-trim-left` is the live regression row.
It is RED today and should flip GREEN when the core fix lands.

## Ordering hazard — drag points must come LAST

A **completed** drag mutates the View cursor slot (the JS sets `cursor` on
pointerup), and a later hover over the same view reports that stale value rather
than re-resolving, because a buttonless move delivers no JS pointermove. Measured
at (660,60), same hit view both times:

- `CUR-PIN846-ordering-cold` — probed cold: **crosshair**
- `CUR-PIN846-ordering-after-drag` — probed right after a drag: **grab**

Order drag points last in `SPECTR_CURSOR_POINTS`, or treat a hover that follows
one as unsound. Documented at the point of use in `native_editor.cpp`.

## Artifacts

| File | What it proves |
|---|---|
| `CUR-PIN846-GREEN.cursor.json` | Live capture, drag ordered last. Detector exits 0. |
| `CUR-PIN846-RED-negative-control.cursor.json` | `hover_style` forced to `default_` in source, object deleted, real recompile (`Building CXX object .../native_editor.cpp.o`). All four points collapse to `default`; detector exits 1. |
| `CUR-PIN846-RESTORED.cursor.json` | Source restored (sha256 verified), recompiled, GREEN returns. Closes RED->GREEN. |
| `CUR-PIN846-home-grid-330pt.cursor.json` | 330-point grid: 330 hits, 0 misses, `{crosshair: 286, default: 30, pointer: 14}`. The probe is alive and discriminates. |
| `CUR-PIN846-ordering-*.cursor.json` | The drag-ordering hazard above. |

Detector plant modes also verified: `--plant wrong-cursor` -> exit 1,
`--plant no-hit` -> exit 3 (INCONCLUSIVE, distinct from pass), `--plant freeze`
-> exit 1. An unrunnable probe never reads as a pass.

---

# STATUS-PILL — the hover readout after CLEAR

## The reported symptom

> "hover text appears correct UNLESS YOU PRESS CLEAR — then after clear it's no
> longer center aligned with some padding, it gets messed up"

## It is a WIDTH defect, and the centring measurement is how that was decided

The pill has two writers for the string it paints. React commits a message;
`updateLiveHoverStatus` runs from the draw loop and writes the live reading
straight onto the DOM node, deliberately, so a per-frame reading does not cost a
whole-document React commit. Only the React path re-derived the width
(`Math.max(96, Math.min(520, text.length * 8 + 28))`), and its publish is
throttled to 700ms and skipped entirely while the reading is unchanged — so the
pill kept the size of whatever string React last saw.

CLEAR publishes `CLEARED GAINS`; clicking a band publishes `BAND n MUTED`. Both
are 13 characters, so both size the pill to 132px. The next live reading is a
full hover label needing 212–252px, painted into that 132px box.

Measured on the shipping standalone (990x645 host, 1320x860 design space), band
click at (400,300) driven by `SPECTR_DRAG="400,300,400,300,1"`:

| | pill x | pill w | text box w | ink w | pill centre | text centre |
|---|---|---|---|---|---|---|
| before the short message | 538.0 | 244.0 | 214.0 | 203.0 | 660.0 | 660.0 |
| after, pre-fix | 594.0 | **132.0** | 102.0 | **173.0** | 660.0 | 660.0 |
| after, post-fix | 554.0 | 212.0 | 182.0 | 173.0 | 660.0 | 660.0 |

The ink overhangs its content box by **71.0px** pre-fix — 35.5px past each edge
of a pill that is 80px narrower than its text needs. **Both centres read
exactly 660.0 in every state**, which is what rules out a centring defect: the
box is the wrong size, not in the wrong place. This is the same distinction the
Settings copy button needed after three centring "fixes" (COPY-WIDTH above).

It is persistent, not a flash: identical geometry at +55, +158, +415, +906 and
+1600ms after the release, because the live writer only re-runs when the reading
changes and the React publisher is throttled.

## The fix

`tools/patch_materialized_status_pill_width.py`. The rule that turns a string
into a width becomes one function, `spectrStatusBannerWidth`, and the per-frame
writer sets the width and margin alongside the text — whoever writes the string
owns the box. No extra React commit. The fast path is NOT removed; routing every
frame through React is the ~22ms whole-document commit the zoom-readout work
removed.

## Artifacts

| file | what it shows |
|---|---|
| `STATUS-PILL-RED-pill.png` | pre-fix raster: `155Hz  −∞  BAND 10/32` overhanging both ends of the pill |
| `STATUS-PILL-GREEN-pill.png` | post-fix raster: the same reading inside the pill with symmetric padding |
| `STATUS-PILL-RED-after-short-message.layout.json` + `.depths.json` | pre-fix geometry: pill w=132, box w=102, ink w=173 |
| `STATUS-PILL-GREEN-hover.layout.json` + `.depths.json` | healthy reference state: pill w=244, box w=214, ink w=203 |
| `STATUS-PILL-GREEN-after-short-message.layout.json` + `.depths.json` | post-fix geometry: pill w=212, box w=182, ink w=173 |
| `STATUS-PILL-RED-length-collision-{a,b}.layout.json` + `.depths.json` | the small version of the same defect: two 28-character readings measuring 244.0 and 252.0 |

## Reproducing

```sh
env PULP_HEADLESS=1 PULP_FRAMES=100 PULP_SCREENSHOT=OUT/shot.png \
    SPECTR_LAYOUT_DUMP=OUT/final.layout.json \
    SPECTR_DRAG="400,300,400,300,1" SPECTR_DRAG_DUMP_PREFIX=OUT/run \
    build/Spectr.app/Contents/MacOS/Spectr
python3 tools/spectr-detectors/status_pill_width_invariance.py \
    OUT/run.move1.layout.json OUT/final.layout.json
```

A press-and-release on a band is a mute, which is what publishes the 13-character
message; `run.move1` is the healthy reading captured before it. A buttonless
`simulate_hover` cannot be used here — neither mac host delivers a DOM
pointermove, so it never reaches the JS hover path (see CUR-PIN846 above).

`--plant shrink` forces the pill to a 13-character width and `--plant offset`
pushes it off centre; both must go red.

## What the detector cannot see

Whether the pill is a *sensible* size — only whether it agrees with its own text.
A rule that sized every pill to 40px would satisfy the width-is-a-function rule
and fail only the ink-fit rule. A dismissed pill carries no text child and is
skipped, so a run of only-dismissed dumps reports UNMEASURED (exit 2) rather than
passing.

## Residual, stated rather than hidden

If React commits a *different* message of *identical* length while a live reading
is on screen (mute band 10, then band 11 — both 13 characters), React's prop diff
sees no width change and writes only the text, leaving the pill at the width the
fast path set for the previous reading: a pill too WIDE for its text, cosmetic
rather than broken, corrected on the next frame by the reading the mute itself
produces. Closing it outright costs a `querySelector` pair on every frame of the
draw loop, in the hot path the previous lane worked to shrink.
