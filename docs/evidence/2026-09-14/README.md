# Press reachability — 2026-09-14

A press at the rect a control PAINTS must reach that control.

## Why these fixtures exist

`hit_target_reach.py` compares a control's own hit rect against its own painted
rect. That sees only a defect a control commits against ITSELF, and its
population is the shapes `classify()` recognises — settings toggles, settings
sliders, and four named transport buttons.

The help guide's close `×` shipped as a correctly sized 32×32 button, correctly
wired, inside a wrapper whose box had collapsed to 0×0. `Rect::contains` is
half-open, so that wrapper admitted no point at all. Every rect-vs-rect check in
this repo was green. The only reason the guide was dismissable was the symmetric
~500px slack `View::hit_test` grants an `overflow: visible` child, measured from
the wrapper's in-flow position at y=804 while the `×` paints at y=73.

`press_target_reach.py` therefore reads the verdicts of a real `View::hit_test`,
run natively at each named control's painted centre.

## The fixtures

| file | what it is |
|---|---|
| `GREEN-required.press-reach.json` | this build: the `×` paints (893,73 32×32) and a press at (909,89) reaches it |
| `RED-required-0x0-anchor.press-reach.json` | the guide anchor collapsed to 0×0 at its shipped in-flow y=804; the `×` then paints at y=877, below an 860-tall root, and a press reaches nothing |
| `EMPTY-required.press-reach.json` | an empty population, which is exit 3 rather than a pass |

## Reproducing

```sh
PULP_HEADLESS=1 PULP_TEST_MODE=1 PULP_DISABLE_PLUGIN_EDITOR=1 \
  SPECTR_PRESS_REACH=1 "$BUILD/Spectr-native-shot" --out=/tmp/pr --backend=skia --scale=1
python3 tools/spectr-detectors/press_target_reach.py /tmp/pr/required.press-reach.json

# the RED fixture, regenerated: collapse the anchor natively mid-run
SPECTR_PRESS_REACH=1 SPECTR_PRESS_REACH_PLANT=1 "$BUILD/Spectr-native-shot" --out=/tmp/pr-red ...
```

The probe also runs the `×` as a TRIAL rather than a measurement: a press away
from it must leave the guide standing, and a press at its painted centre must
dismiss it. Both halves are required — a dismissal verb that fired on every
press would pass the first half alone.
