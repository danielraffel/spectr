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
