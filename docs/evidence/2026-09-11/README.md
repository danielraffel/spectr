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
