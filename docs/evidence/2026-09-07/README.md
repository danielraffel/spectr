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

## What these files do NOT establish

The green capture is the shipping native editor path — the materialized runtime
package through `Processor::create_view()` with the real GPU compositor. It is
neither a browser fixture nor a DOM assertion, but it is also not the Settings
modal opened inside a host. SET-1's own wording asks for standalone and AUv2,
and the standalone screenshot here is the home surface, because the standalone
entry point exposes `--screenshot` but no way to drive the Settings control.

So the defect is proven fixed on the path that ships it. Closing SET-1 against
its own text still needs the modal captured in standalone and in Logic.

## Residual findings on the fixed build, still open

- `CLIP`: `⋯` measures 8.00px wide in a 7.03px box.
- `WRAP`: `ZOOMABLE FILTER BANK` needs 36.00px in a 14.00px box (2.57x).
- `OVERLAP` x2: a Settings row description meets the bottom transport bar.
