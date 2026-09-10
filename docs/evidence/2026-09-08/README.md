# 2026-09-08 — PRE-5 and PRE-7 closed on the shipping surface

Both rows were already worked to `NO DEFECT` / `FIXED` offscreen. The only
outstanding closure bar was #1, a capture of the shipping surface. The earlier
note said they "close when the display unlocks" — that was wrong. The display is
locked (`Window Server / Display 1 Shield`; `screencapture` returns a 1-colour
black frame and `-R`/`-l` fail outright), but nothing here needs the display:
the shipping app bundle renders through its own GPU path under `--screenshot`,
which is the same surface that closed SET-2 and SET-6.

Surface: `build-gate/Spectr.app/Contents/MacOS/Spectr` — the app bundle the PKG
installs. Not a browser fixture, not `native_shot`, not a headless DOM assertion.

## PRE-7 — Apply applies the preset and closes the manager in one action

Driven through the runtime's own activation path, every stage probed in one run.
`[data-spectr-manager-action]` is the open/closed signal.

| stage | actions | rows |
|---|---|---|
| home | 0 | 0 |
| pattern menu open | 0 | 0 |
| manager open | 1 | 8 |
| `factory:harmonic` selected | **6** | 10 |
| after APPLY | **0** | 0 |

Both poles appear in the same run (0 at home, 6 when open), so `0` after APPLY
means *closed* rather than *not found*.

**Control — the close is caused by APPLY, not by any click.** Clicking a second
preset row instead of APPLY, from the identical selected state:

| | actions |
|---|---|
| `factory:harmonic` selected | 6 |
| after clicking `factory:comb` | **6** (manager still open) |

`PRE7-shipping-after-apply.png` — manager gone, `APPLIED "HARMONIC SERIES"` toast
up, harmonic curve on the bands.
`PRE5-shipping-comb-selected.png` — the control: manager still open with COMB
selected. That file is byte-identical to the capture the PRE-5 run produced of
the same state (sha256 `c1d8fac3a87c…`), reached by two independent runs, so it
is stored once rather than twice. The collision is itself a determinism control:
the surface renders the same state identically across runs.

(The bottom bar in the after-apply capture reads `HARMON… ▾`, truncated and
wrapped. That is the separately tracked truncation finding, not a PRE-7 defect.)

## PRE-5 — selecting a preset updates its displayed name and artwork

Detector: `tools/preset_detail_update.py`, pixel-based.

**A textContent probe is a broken instrument on this surface and was discarded.**
Querying `document.body.textContent` for `SELECT A PATTERN` returns false on a
capture that visibly contains that exact string — the materialized runtime does
not expose rendered text through DOM text nodes. Every negative it produced was
therefore meaningless and none is reported here.

What the DOM *can* see, with its control:

| stage | `svg` count |
|---|---|
| manager open, nothing selected | 13 |
| control: click empty detail-pane space | 13 (unchanged) |
| `factory:harmonic` selected | 14 |

Pixels carry the rest — that the pane *changes between presets*, not merely that
it populates:

```
python3 tools/preset_detail_update.py \
  docs/evidence/2026-09-08/PRE5-shipping-harmonic-selected.png \
  docs/evidence/2026-09-08/PRE5-shipping-comb-selected.png --scale 1.5
```

| region | changed | verdict |
|---|---|---|
| control: manager title strip | 0.00% (max delta 0) | same aligned surface — the diff is not frame noise |
| detail pane | 14.66% (max delta 216) | name + artwork both updated |

Planted negative (`--plant`, compares a capture against itself, standing in for a
selection that changed nothing): detail pane 0.00% → **RED**, exit 1.
Real pair → **GREEN**, exit 0.
