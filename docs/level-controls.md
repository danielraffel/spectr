# Level controls: Auto Gain v2

Spectr's header has four level controls: **Mix**, **Intensity**, **Output** and
**AUTO** (Auto Gain, host parameter `5001`). This page is the design record for
Auto Gain v2, what AUTO runs from this version on. Intensity, Mix and Range are
described in [parameter-surface.md](parameter-surface.md) and
`include/spectr/level_controls.hpp`.

## What it computes

AUTO is a broadband gain after the mask and before Output trim. It answers one
question: *how much louder or quieter does the drawn shape make the sound going
through it?* and makes up the difference.

```
E        = sum_f P(f) |H(f)|^2  /  sum_f P(f)
make-up  = -10 log10(E), clamped to +-24 dB
```

- `H(f)` is the response the **active renderer realises** for the effective
  shape -- the drawn / morphed / macro field at the Intensity knob, blended
  with the dry signal at Mix (`m * g + (1 - m)`). Each renderer reports it
  through `MaskRenderer::realised_magnitude()`: the compiled band table, plus
  the band-edge shaping the zero-latency (Tracking) realisation adds before its
  minimum-phase design. LFOs are deliberately excluded, as in v1: a level LFO
  stays audible as level.
- `P(f)` is the long-term, BS.1770 K-weighted power spectrum of the material
  the mask is shaping: the live input, or -- while Freeze holds -- the held
  sound. It is taken from the wet-source stage the renderers already call
  (`AutoGainWetTap`), on the renderers' own 8192-point design grid, so `P` and
  `H` share bins.
- Below 100 % Mix the dry leg is the LIVE input, which while frozen is not
  the sound being shaped. So the estimator keeps both legs' spectra and their
  cross-spectrum, and the make-up is the ratio of
  `m^2 |H|^2 Pww + (1-m)^2 Pdd + 2m(1-m) Re(H Pwd)` to the same with `H = 1`.
  Live, the legs are one signal and this is exactly `|m H + 1 - m|^2` weighted
  by `P`; frozen, the cross term vanishes and the legs add as powers. The
  phase of `H` -- minimum phase in Tracking -- enters only through the cross
  term.

v1 (still in the code as `AutoGainReference`, selectable only through
`Spectr::set_auto_gain_model()` for measurement) used the same ratio with `P`
fixed to a K-weighted pink spectrum and `H` taken as the drawn band steps. That
is right for full-range material and badly wrong for narrow material: boosting
the top octaves of a bass line barely changes its loudness, yet v1 cut the whole
signal by ~20 dB.

## Why it does not pump

- Nothing reads the output. A static shape over steady material is a constant
  target.
- `P` is an energy-weighted exponential average with a **3 s** time constant
  over 170 ms Hann frames (8192 points, hop 4096 at 48 kHz). A hit does not move
  it; a change of spectral balance that lasts for seconds does.
- **Silence gate:** a frame quieter than **-60 LUFS** (K-weighted, channels
  summed) is not material; the estimate holds through it instead of drifting
  toward the noise floor or resetting.
- **Slew limit:** material movement moves the target by at most **6 dB/s**. A
  shape edit (or switching AUTO on, or Mix) retargets at once, because that is
  the user asking for it; the applied gain then ramps over 300 ms as in v1.

## Start-up and resets

Before any material is heard the estimate *is* v1's: the K-weighted pink
reference, tabulated per bin. Once audible frames arrive the prior's weight
decays with a 0.5 s constant (`exp(-t / 0.5 s)` of audible material) while the
material's own average builds up as a running mean, then as the 3 s average. A
transport jump or host Reset (`should_reset_stream_history`) and `prepare()`
restart the estimator from the prior, with the same frame grid -- so a bounce
and a playback that start from the same reset hear the same gain.

## Determinism

The estimator advances on a frame grid counted in samples from
`prepare()`/reset. When a frame completes inside a render block, it reports its
sample offset, and the processor applies the new target at exactly that sample
(`take_auto_events` in `Spectr::process`). The gain therefore does not depend
on the host's block size or on how fast it renders:

- `Auto Gain v2 does not depend on how the host chops the stream`
  (`test/test_auto_gain_v2.cpp`): 8 chunkings from 1 to 4096 samples, with the
  material changing mid-render and the gain moving 3.5 dB -- **0 mismatching
  samples** in Mixing (linear phase), and at most **8.4e-9** in Tracking, inside
  the renderer's own characterised 1-ULP bound (`test_render_mode.cpp`).
- `Spectr-au-offline-bounce-equivalence-auto-gain`: through the built AU, a
  paced render and an `kAudioUnitProperty_OfflineRender` bounce with AUTO on and
  a change of material at 3 s -- `max |paced - bounce| = 0`; its negative
  control (no offline flag) differs.

## Realtime safety

No allocation, no lock, no clock on the audio thread. `prepare()` sizes every
buffer (including the scratch table the renderer computes `H` into); the frame
FFT is Pulp's `FftT::forward_real`. Covered by `The realtime calls do not
allocate` (a counting `operator new` around the estimator, target and pure
functions, with a positive control) and by `Spectr-render-path-clock-*` scans of
the unit and the adapter.

## Measured

All loudness is BS.1770 integrated loudness (Pulp's `MultiChannelMeter`) of
the output with AUTO on minus the input's, at 48 kHz, through HeadlessHost
renders of the real plug-in in its default (Tracking) mode. Freeze rows
measure against the same frozen render with a flat shape at the same Mix.

### The corpus sweep (advisory)

`tools/autogain_corpus_report.py` + `Spectr-autogain-sweep`: 12 license-clean
materials (Spectr's generated pink, bass line, vocal-range buzz, hats, pad,
drum loop, 1 kHz sine, pink with silent gaps, and a Freeze case -- a bass line
held while the live input turns to hats -- plus the Audio Quality Lab's drum
break, tonal and stereo-pad generators, each registered with its SHA-256
through `quality_lab.corpus`) x 41 shapes (low / mid / high, broad / narrow,
+-6 / 12 / 24 dB; Intensity 50 % and Mix 50 % variants; a tilt), steady
window from 4 s:

| model | renders | median abs LU | p95 abs LU | worst abs LU | worst incl. start-up |
|---|---|---|---|---|---|
| v1 | 492 | 1.15 | 14.00 | 22.22 | 22.22 |
| v2 | 492 | 0.01 | **0.28** | **2.62** | 3.36 |

Target: v2 p95 <= 1.5 LU and worst <= 3 LU -- met. On narrow material
(bass, vocal, hats, pads, sine, the Freeze case) v2 is better than v1 in
**225 of 225** cases where v1 is off by more than 0.5 LU (narrow-material p95:
v1 14.83 LU, v2 0.03 LU).

| material | v1 p95 | v1 worst | v2 p95 | v2 worst |
|---|---|---|---|---|
| pink | 0.21 | 0.42 | 0.02 | 0.07 |
| bass_line | 13.83 | 20.60 | 0.06 | 0.19 |
| vocal_buzz | 15.62 | 20.60 | 0.01 | 0.02 |
| hats | 14.56 | 18.18 | 0.01 | 0.02 |
| synth_pad | 14.54 | 18.97 | 0.04 | 0.13 |
| drum_loop_synth | 8.80 | 14.21 | 0.98 | 1.09 |
| sine_1k | 16.95 | 22.22 | 0.04 | 0.49 |
| pink_with_gaps | 0.24 | 0.44 | 0.11 | 0.17 |
| freeze_bass_then_hats | 13.78 | 20.60 | 0.09 | 1.70 |
| ql_drum_break | 6.63 | 10.87 | 0.56 | 2.62 |
| ql_tonal | 14.57 | 20.60 | 0.01 | 0.02 |
| ql_stereo_pad | 5.13 | 14.24 | 0.01 | 0.05 |

| material | shape | AUTO off change | v1 error | v2 error |
|---|---|---|---|---|
| bass_line | high broad +24 | +0.00 | -20.60 | -0.02 |
| bass_line | high narrow +12 | +0.00 | -2.66 | -0.00 |
| bass_line | low broad +12 | +11.64 | +7.43 | +0.01 |
| hats | low broad +12 | -0.00 | -4.21 | -0.00 |
| hats | high broad -24 | -20.81 | -18.18 | -0.02 |
| sine_1k | mid narrow -12 | -8.53 | -8.42 | -0.14 |
| vocal_buzz | high broad +24 | +0.00 | -20.60 | -0.02 |
| synth_pad | low broad -24 @mix50 | -1.04 | -0.69 | -0.02 |
| drum_loop_synth | low broad +24 | +23.36 | +8.80 | -0.03 |
| pink | tilt -12..+12 | +7.46 | +0.42 | +0.01 |
| freeze_bass_then_hats | high broad +24 | +0.00 | -20.60 | -0.00 |
| freeze_bass_then_hats | low broad -24 @mix50 | -9.40 | -9.05 | -1.70 |
| ql_drum_break | low broad +24 | +16.77 | +2.21 | +2.62 |

What is left is transient material under large low boosts (the drum break,
+24 dB below 170 Hz: +2.62 LU, where v1 happened to land at +2.21). The
estimator weighs energy; integrated loudness gates 400 ms blocks against their
own mean, and boosting the kick by 24 dB changes which blocks pass that gate,
which no estimate of the input's spectrum can see. "Incl. start-up" measures
from 0.5 s, while v2 is still moving from v1's estimate to the material's.

### The ctest gates (fast, deterministic)

`test/test_auto_gain_v2.cpp`, all in the default ctest:

| gate | result | negative control |
|---|---|---|
| accuracy: 6 materials x 6 shapes, v2 p95 <= 1.5, worst <= 3, better than v1 on every narrow case | p95 0.18 LU, worst 0.57 LU, 22/22 | `SPECTR_LEVEL_PLANT=autogain-v2-unweighted` (every bin weighs the same): p95 23.3 LU, fails; v1 on bass + top octaves +24: -20.6 LU (in-test control) |
| pumping: steady drum loop / pink / bass under a static shape; applied-gain spread <= 0.5 dB, extra momentary-loudness sd vs AUTO off <= 0.05 LU | 0.144 dB, 0.013 LU | `autogain-v2-no-smoothing` (each frame replaces the estimate, no slew): 1.51 dB and 0.079 LU, fails |
| chunking: 8 host chunkings (1 ... 4096), material changing mid-render | Mixing 0 mismatching samples; Tracking <= 8.4e-9 | a different shape differs by > 1e-4; the gain really moved 3.5 dB |
| start-up and silence | first block equals v1's gain within 0.005 dB; across 2 s of silence the gain moves <= 0.007 dB | the gate saw 64 silent frames |
| Freeze: bass held, hats live, top octaves +24 | v2 -0.00 LU | v1 -20.6 LU (the shape-only answer) |
| shape edit: pink, top octaves +12 dB in one edit | peak excursion 0.28 LU, back within 1 LU at once | AUTO off jumps +9.16 LU |

`test/test_loudness_compensation.cpp` holds the unit's own gates: pure-function
vectors (single-leg, two-leg coherent / uncorrelated / phase-flip, band form),
a minimum-phase reconstruction of a known one-pole, chunking determinism of the
estimator (bit-identical spectra and frame positions over 7 chunkings),
gating, prior start-up and reset, the target slew, and zero allocations on the
realtime calls (with a positive control).

### Pumping on the full corpus

Extra momentary-loudness standard deviation of v2 over AUTO off, same static
shape (low +12 / high -12), steady window: at most **+0.023 LU** (pink, low
+12), +0.015 LU on the drum break, 0.000 on tonal material. Applied-gain
standard deviation at most 0.16 dB (drum break).

### Decisions

| decision | value | evidence (quick subset: drums, pink, bass, vocal, hats, drum break x 4 shapes) |
|---|---|---|
| time constant | 3 s | 1 s: p95 0.54 LU, worst applied spread 1.60 dB; **3 s: 0.38 LU, 0.59 dB**; 6 s: 0.34 LU, 0.54 dB -- 6 s buys 0.04 LU and 0.05 dB for twice the time to follow a change of material |
| response | realised (renderer's own magnitude, minimum phase where it designs one) | p95 0.38 vs 0.41 LU for the drawn steps; and the Mix 50 % cases with phase interference fall from ~1.0 LU (v1 0.7) to 0.02 LU |
| two legs (wet vs live, cross-spectrum) | on | Freeze + Mix 50 %, low -24: -4.82 LU with one leg, -1.70 LU with two |
| relative gate | 10 LU (BS.1770) | p95 0.38 / worst 0.56 LU with it, 0.31 / 0.58 without: inside the noise; kept for agreement with how integrated loudness counts |
| silence gate | -60 LUFS per frame | holds the gain through gaps within 0.007 dB |
| slew | 6 dB/s on material movement; edits immediate | start-up from v1 to the material in under a second or two; no step a listener reads as pumping |
| range | +-24 dB | v1's +12 dB boost cap would leave a -24 dB cut of what is playing 12 dB short |
| prior | v1's reference, decaying with 0.5 s of audible material | at 8 frames' weight (harmonic decay) the prior dominated boosts where the material is empty: bass + top octaves +24 read -6.9 LU at 4 s |

### Recommendation

The default-on bar is met: v2 p95 0.28 LU and worst 2.62 LU on the sweep,
better than v1 on every narrow case, pumping indistinguishable from AUTO off.
**Recommend AUTO on for new instances** (`kAutoGainDefaultForNewInstances`);
not flipped here -- that is a product call.

## Old sessions and the default

A session that saved AUTO **on** keeps it on and now runs v2: its make-up
follows the material instead of the pink reference, so on narrow material its
level changes (towards the input's loudness). Sessions saved before AUTO
existed still open with it off. New instances still default **off**
(`kAutoGainDefaultForNewInstances`).

## Upstream

The estimator is a product-independent unit staged for the Pulp SDK:
`include/spectr/upstream/loudness_compensation.hpp` (namespace
`pulp_candidate::signal`, depends on Pulp and the standard library only) with
its own tests, `test/test_loudness_compensation.cpp` (links `Pulp::signal` and
Catch2 only). Spectr keeps the adapter (`include/spectr/auto_gain_material.hpp`):
the layout-to-response mapping, the v1 prior, the negative-control seams, the
per-slice event list and the wet-source tap.

## Re-running the measurements

```
ctest --test-dir build -R "autogain|loudness-compensation|Auto Gain"
cmake --build build --target Spectr-autogain-sweep
~/.pulp/tools/python-envs/audio-quality-lab/.venv/bin/python \
    tools/autogain_corpus_report.py --build build --out <dir>
```
