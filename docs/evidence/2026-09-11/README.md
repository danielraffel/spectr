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
