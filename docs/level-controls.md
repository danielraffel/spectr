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

"Steady" here means steady K-weighted loudness (BS.1770), the measure AUTO
holds: a shape that boosts the lows can leave the RMS level well above the
input's while the loudness matches (the reviewer's pink / low +12 case: 0.0 LU,
+4.8 dB RMS).

- Nothing reads the output. A static shape over steady material is a constant
  target.
- `P` is an energy-weighted exponential average with a **3 s** time constant
  over 170 ms Hann frames (8192 points, hop 4096 at 48 kHz). A hit does not move
  it; a change of spectral balance that lasts for seconds does.
- **Silence gate:** a frame quieter than **-60 LUFS** (K-weighted, channels
  summed) is not material; the estimate holds through it instead of drifting
  toward the noise floor or resetting.
- **Slew:** material movement moves the target at **6 dB/s** near it, and
  proportionally faster far from it (10 x the distance, per second, at most
  120 dB/s). A one-step wobble of <= 0.5 dB rides a 0.3 s gain ramp; a real move
  rides a one-hop ramp. A shape edit (or switching AUTO on, or Mix) retargets at
  once, because that is the user asking for it.

## Following the material: changes, Freeze, locates

The first v2 followed the material only through its 3 s average and a fixed
6 dB/s, and forgot it at every locate. The independent review measured what
that cost: a bass line under +24 dB on the top octaves started 20.6 dB low after
every play-from-locate and took 3.7 s to recover; a Freeze release jumped to the
AUTO-off level and took ~14 s; a change to quieter material was ignored for
~3.5 s (the relative gate measured it against the louder past) and took ~16 s.
v2 now:

- **Keeps the material across a locate or host Reset.** Only the estimator's
  frame grid restarts (so the block size still cannot matter); the estimate and
  the target are kept, so playback from a locate starts at the level it had.
- **Saves the estimate with the session** (`auto_gain_estimate`: 160 log bands,
  10 Hz-24 kHz, float32, base64 -- 3.4 KB). A reopened project, an offline bounce
  and a play from the start all start warm and the same; a bounce from a saved
  state renders sample-identically however the host blocks it.
- **Detects a change of material.** Beside the energy-weighted estimates it
  keeps fast (0.4 s) and slow (3 s) averages of each frame's SHAPE -- every
  audible frame weighing the same, so a quieter new part shows within a few
  frames. When the make-up those two give the current shape differs by more than
  **4 dB for 3 frames**, the estimate restarts as a running mean of what comes
  next; the target holds for the first 0.5 s of it (one beat of a groove, so the
  first kick or hat cannot swing it), then follows fast.
- **Level-drop rule:** when no frame of the last 0.6 s comes within 10 dB of the
  estimate's level, the material got genuinely quieter and the estimate restarts
  from it. (The BS.1770 relative gate of the first v2 is gone: its own record
  showed it inside the noise, and it was what ignored quieter new material.)
- **Freeze edges.** The estimator keeps the live (dry) leg's spectrum warm
  through a hold. On release the wet estimate becomes the live one at once and
  the gain jumps with the release crossfade (20 ms ramp) instead of slewing after
  it; on engage the legs are treated as uncorrelated. The renderer's latency is
  added to every material event, so the gain moves with the audio the frame
  described.
- **Cold start** (a new instance, no saved estimate) begins at v1's estimate,
  whose weight falls linearly to zero over the first 0.5 s of audible material,
  and converges within about 0.9 s.

## Determinism

The estimator advances on a frame grid counted in samples from `prepare()` and
from every locate. When a frame completes inside a render block, its new target
is queued at its absolute stream sample (plus the renderer's latency) and the
processor applies it at exactly that sample (`take_auto_events` in
`Spectr::process`); the wet-source tap splits the freeze source's block at frame
boundaries, so a Freeze edge reaches the estimator at the same sample too. The
gain therefore does not depend on the host's block size or on how fast it
renders:

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

All loudness is BS.1770 (Pulp's `MultiChannelMeter`), at 48 kHz, through
HeadlessHost renders of the real plug-in in its default (Tracking) mode.
"Error" is the output's integrated loudness with AUTO on minus the input's;
Freeze rows measure against the same frozen render with a flat shape at the same
Mix. Evidence for every number below is under
`spectr-specs/auto-gain-v2/v2b/` (sweep report, raw rows, transient rows, ctest
logs, and the independent review's own probes re-run on this code).

### The corpus sweep (advisory)

`tools/autogain_corpus_report.py` + `Spectr-autogain-sweep`: 12 license-clean
materials (Spectr's generated pink, bass line, vocal-range buzz, hats, pad,
drum loop, 1 kHz sine, pink with silent gaps, and a Freeze case -- a bass line
held while the live input turns to hats -- plus the Audio Quality Lab's drum
break, tonal and stereo-pad generators, each registered with its SHA-256
through `quality_lab.corpus`) x 41 shapes (low / mid / high, broad / narrow,
+-6 / 12 / 24 dB; Intensity 50 % and Mix 50 % variants; a tilt). "Steady" is
from 4 s; "from 0 s" is the whole render, start-up included.

| model | renders | median abs LU | p95 abs LU | worst abs LU | p95 from 0 s | worst from 0 s |
|---|---|---|---|---|---|---|
| v1 | 492 | 1.15 | 14.00 | 22.22 | 14.02 | 22.19 |
| v2 | 492 | 0.00 | **0.21** | **2.46** | 0.37 | 3.14 |

(The first v2, steady: p95 0.28, worst 2.62.) Target: p95 <= 1.5 LU and worst
<= 3 LU steady -- met. On narrow material (bass, vocal, hats, pads, sine, the
Freeze case) v2 is better than v1 in **225 of 225** cases where v1 is off by
more than 0.5 LU (narrow-material p95: v1 14.94 LU, v2 0.03 LU).

| material | v1 p95 | v1 worst | v2 p95 | v2 worst | v2 worst from 0 s |
|---|---|---|---|---|---|
| pink | 0.21 | 0.42 | 0.02 | 0.07 | 0.09 |
| bass_line | 13.83 | 20.60 | 0.05 | 0.16 | 0.75 |
| vocal_buzz | 15.62 | 20.60 | 0.01 | 0.01 | 0.32 |
| hats | 14.56 | 18.18 | 0.01 | 0.01 | 0.15 |
| synth_pad | 14.54 | 18.97 | 0.04 | 0.13 | 0.15 |
| drum_loop_synth | 8.80 | 14.21 | 1.05 | 1.10 | 2.48 |
| sine_1k | 16.95 | 22.22 | 0.04 | 0.48 | 1.05 |
| pink_with_gaps | 0.24 | 0.44 | 0.11 | 0.16 | 0.15 |
| freeze_bass_then_hats | 13.78 | 20.60 | 0.05 | 0.14 | 0.57 |
| ql_drum_break | 6.63 | 10.87 | 0.58 | 2.46 | 3.14 |
| ql_tonal | 14.57 | 20.60 | 0.01 | 0.02 | 0.19 |
| ql_stereo_pad | 5.13 | 14.24 | 0.01 | 0.05 | 0.14 |

| material | shape | AUTO off change | v1 error | v2 error |
|---|---|---|---|---|
| bass_line | high broad +24 | +0.00 | -20.60 | -0.00 |
| bass_line | low broad +12 | +11.64 | +7.43 | +0.01 |
| hats | high broad -24 | -20.81 | -18.18 | -0.00 |
| sine_1k | mid narrow -12 | -8.53 | -8.42 | -0.14 |
| vocal_buzz | high broad +24 | +0.00 | -20.60 | -0.00 |
| drum_loop_synth | low broad +24 | +23.36 | +8.80 | -0.02 |
| freeze_bass_then_hats | high broad +24 | +0.00 | -20.60 | -0.00 |
| freeze_bass_then_hats | low broad -24 @mix50 | -9.40 | -9.05 | -0.05 |
| ql_drum_break | low broad +24 | +16.77 | +2.21 | +2.46 |

What is left is transient material under large low boosts (the drum break,
+24 dB below 170 Hz: +2.46 LU, where v1 lands at +2.21). The estimator weighs
energy; integrated loudness gates 400 ms blocks against their own mean, and
boosting the kick by 24 dB changes which blocks pass that gate, which no
estimate of the input's spectrum can see. From 0 s the worst is 3.14 LU on the
same case: its first 0.5 s start from v1's estimate.

### Transients

Measured from the event: T1dB is the time for the applied gain to come within
1 dB of where it settles (its mean over the last second of the window); max
momentary is the largest error of the output's momentary (400 ms) loudness
against a flat AUTO-off render of the same scenario -- what a perfect Auto Gain
would sound like -- and "s > 6 LU" how long it stays above 6 LU. "v2a" re-creates
the first v2's transient behaviour in this build (`SPECTR_LEVEL_PLANT=autogain-v2a`:
reset at every locate, no change detector, no level-drop rule, no Freeze leg
switch, fixed 6 dB/s); the independent review's own probes, re-run on this code,
agree (bass line + top octaves +24 after a cold start: T1dB 3.71 s -> 0.74 s;
reset every 3 s: -10.98 LU integrated -> -0.01 LU; Freeze release: 13.95 s ->
0.01 s; bass -> hats 15 dB quieter: 15.93 s -> 1.04 s).

| scenario | event | v2a (first v2, plant) T1dB / max mom / s>6 LU | v2 T1dB / max mom / s>6 LU |
|---|---|---|---|
| cold start: bass line, high +24 | start | 3.44 s / 20.1 LU / 2.40 s | **0.74 s** / 19.8 LU / 0.40 s |
| cold start: vocal, high +24 | start | 3.44 s / 20.2 LU / 2.40 s | **0.74 s** / 19.9 LU / 0.40 s |
| cold start: hats, high -24 | start | 3.03 s / 17.8 LU / 2.00 s | **0.70 s** / 17.3 LU / 0.40 s |
| cold start: sine 1k, mid narrow -24 | start | 2.74 s / 16.1 LU / 1.80 s | **0.70 s** / 15.3 LU / 0.40 s |
| cold start: drum loop, low +24 | start | 1.46 s / 9.3 LU / 0.70 s | **0.52 s** / 9.2 LU / 0.20 s |
| cold start: pink, low +12 | start | 0.00 s / 0.5 LU / 0.00 s | **0.00 s** / 0.5 LU / 0.00 s |
| locate every 3 s: bass line, high +24 | each locate (worst) | 2.91 s / 19.8 LU / 2.70 s | **0.00 s** / 0.4 LU / 0.00 s |
| locate every 3 s: drum loop, low +24 | each locate (worst) | 1.47 s / 9.3 LU / 1.00 s | **0.00 s** / 0.6 LU / 0.00 s |
| locate every 3 s: vocal, mid narrow -12 | each locate (worst) | 0.00 s / 0.8 LU / 0.00 s | **0.00 s** / 0.0 LU / 0.00 s |
| host reset mid-hold: bass held, hats live, high +24 | reset at 7 s | 3.44 s / 19.8 LU / 2.50 s | **0.00 s** / 0.0 LU / 0.00 s |
| Freeze: bass held at 4.5 s, hats live from 5 s, high +24 | engage | 0.00 s / 0.0 LU / 0.00 s | **0.00 s** / 0.0 LU / 0.00 s |
| Freeze: bass held at 4.5 s, hats live from 5 s, high +24 | release at 9 s | 9.35 s / 23.5 LU / 8.50 s | **0.01 s** / 5.5 LU / 0.00 s |
| change at 8 s: bass -> hats, high +24 | change | 6.64 s / 23.1 LU / 7.80 s | **1.00 s** / 22.9 LU / 1.10 s |
| change at 8 s: bass -> hats -15 dB (quieter after loud) | change | 6.37 s / 24.0 LU / 7.59 s | **1.11 s** / 24.0 LU / 1.00 s |
| change at 8 s: hats -> bass -15 dB | change | 6.72 s / 23.8 LU / 7.69 s | **1.44 s** / 23.8 LU / 1.29 s |
| change at 8 s: hats -15 dB -> bass (loud after quiet) | change | 3.90 s / 23.5 LU / 3.20 s | **1.20 s** / 22.3 LU / 0.50 s |
| change at 8 s: pink -> vocal -15 dB | change | 3.88 s / 20.8 LU / 7.59 s | **1.42 s** / 20.8 LU / 1.10 s |
| switch every 4 s, high +24: bass -> hats | switch | 2.91 s / 23.0 LU / 3.70 s | **0.99 s** / 22.9 LU / 1.10 s |
| switch every 4 s, high +24: hats -> vocal | switch | 2.67 s / 12.8 LU / 2.10 s | **1.35 s** / 20.2 LU / 1.30 s |
| switch every 4 s, high +24: vocal -> drums | switch | 2.73 s / 13.4 LU / 1.91 s | **1.47 s** / 15.9 LU / 1.10 s |
| switch every 4 s, high +24: drums -> bass | switch | 2.70 s / 11.5 LU / 3.70 s | **1.38 s** / 14.6 LU / 1.40 s |

Targets: T1dB <= 1.0 s after a locate (warm) -- 0.00 s; <= 1.5 s after a change
of material or a Freeze release -- worst 1.47 s and 0.01 s; max momentary error
<= 6 LU through locates, host resets and Freeze engage / release -- worst 5.5 LU.
An ABRUPT change of material cannot meet 6 LU momentary with any causal Auto
Gain: when the needed gain jumps by 24 dB, even a perfect compensator that
reacted 10 ms after the switch would leave the 400 ms window
10 log10((0.01 x 251 + 0.39) / 0.4) = 8.6 LU over, and one that needs a single
170 ms analysis window 20.3 LU. Those cases are gated instead on T1dB and on the
time above 6 LU (<= 1.5 s; worst 1.40 s, against 3.2-8.5 s for the first v2 and
for ever with AUTO off). A cold start (no saved estimate) begins at v1's answer:
up to ~20 LU for its first 0.4-0.5 s, T1dB <= 0.74 s.

### The ctest gates (fast, deterministic)

`test/test_auto_gain_v2.cpp` and `test/test_auto_gain_v2_transients.cpp`, all in
the default ctest:

| gate | result | negative control |
|---|---|---|
| accuracy: 6 materials x 6 shapes, v2 p95 <= 1.5, worst <= 3, better than v1 on every narrow case | p95 0.16 LU, worst 0.55 LU, 22/22 | `autogain-v2-unweighted`: p95 23.3 LU, fails; v1 on bass + top octaves +24: -20.6 LU (in-test control) |
| pumping: steady drum loop / pink / bass under a static shape; applied-gain spread <= 0.5 dB, extra momentary sd vs AUTO off <= 0.05 LU, no change-detector restart | 0.144 dB, 0.013 LU, 0 restarts | `autogain-v2-no-smoothing`: 4.0 dB and 0.057 LU, fails |
| chunking: 8 host chunkings (1 ... 4096), material changing mid-render | Mixing 0 mismatching samples; Tracking <= 8.4e-9 | a different shape differs by > 1e-4; the gain really moved 4.2 dB |
| locate every 3 s (bass, drums, vocal) | T1dB 0.00 s, max momentary 0.65 LU | `autogain-v2-reset-on-seek`: 19.8 LU, fails |
| host reset mid-hold | T1dB 0.00 s, 0.0 LU | |
| Freeze engage and release | engage 0.00 s / 0.0 LU; release 0.01 s / 5.5 LU | `autogain-v2-stale-on-change`: release 9.35 s / 23.5 LU, fails |
| change of material (5 cases, +-15 dB) | T1dB <= 1.44 s, <= 1.29 s over 6 LU | `autogain-v2-stale-on-change`: 6.7 s, 7.8 s, fails |
| switches every 4 s | T1dB <= 1.47 s, <= 1.40 s over 6 LU | |
| cold start | T1dB <= 0.74 s | |
| warm restore from a saved session | first block within 0.01 dB of where the saved session ended; two renders from one saved state (different block sizes) identical | a cold start begins 20.6 dB away |
| pre-v2 session with AUTO on keeps v1 until AUTO is toggled | v1 (-20.6 dB) until off/on, then v2 (0.0 dB); a re-save keeps v1 | |
| silence, start-up, Freeze weighting, shape edit (unchanged from the first v2) | pass | |

Through the built AU, `Spectr-au-offline-bounce-equivalence-auto-gain`: paced
render vs OfflineRender bounce with AUTO on and a change of material,
`max |paced - bounce| = 0` (MATCH); without the offline flag it differs.

### Pumping on the full corpus

Extra momentary-loudness standard deviation of v2 over AUTO off, same static
shape, steady window, over all 41 shapes: at most +0.128 LU (the first v2:
+0.147). For the two pumping shapes (low +12 / high -12): at most +0.017 LU
(pink). Applied-gain spread is at most 1.54 dB (drum break, low narrow +24;
the first v2: 1.02 dB there) -- the gain follows the 3 s estimate of a sparse
break more closely now, and the loudness it produces fluctuates no more than
with AUTO off (+0.045 LU).

### Cost

Rendering 48 s of stereo audio (quick shape set) took 1.94 s with AUTO off and
2.06 s with v2 (user CPU, HeadlessHost, Tracking): about 0.25 % of real time
for the estimator, the change detector and the tap.

### Decisions

| decision | value | evidence |
|---|---|---|
| time constant | 3 s | quick subset (drums, pink, bass, vocal, hats, drum break x 4 shapes): 1 s: p95 0.59 LU, worst applied spread 2.58 dB; **3 s: 0.29 LU, 0.77 dB**; 6 s: 0.26 LU, 0.51 dB -- 6 s buys 0.03 LU for twice the time to follow the material |
| response | realised (renderer's own magnitude, minimum phase where it designs one) | p95 0.29 vs 0.32 LU for the drawn steps; the Mix 50 % phase-interference cases fall from ~1.0 LU (v1 0.7) to 0.02 LU |
| two legs (wet vs live, cross-spectrum) | on | Freeze + Mix 50 %, low -24: -4.82 LU with one leg, -0.05 LU now |
| relative gate | removed | it measured quieter new material against the louder past and ignored it for ~3.5 s; its effect on steady accuracy was inside the noise (0.38 vs 0.31 LU p95) |
| locate / host reset | keep the estimate; restart only the frame grid | T1dB 2.9 s -> 0.00 s; integrated error with a reset every 3 s -10.98 LU -> -0.01 LU |
| session state | save the estimate (160 bands, 3.4 KB) | warm restore: first block within 0.01 dB of the saved session's end; a cold start is 20.6 dB away |
| change detector | fast 0.4 s vs slow 3 s SHAPE averages, 4 dB for 3 frames; restart; hold 0.5 s | level-independent, so quieter new material is seen within frames; 0 restarts on steady material; the 0.5 s hold keeps the first frames of a groove (one kick) from swinging the gain (without it: vocal -> drums swung +/-4 dB, T1dB 2.6 s) |
| level-drop rule | no frame within 10 dB of the estimate's level for 0.6 s -> restart | bass -> hats 15 dB quieter: T1dB 1.11 s (first v2: 6.4 s) |
| Freeze release | wet := live (dry) estimate; jump over 20 ms; skip the frames straddling the release | T1dB 9.35 s -> 0.01 s, max momentary 23.5 -> 5.5 LU |
| events | at their own stream sample plus the renderer's latency | otherwise the gain moves before the audio it describes (Mixing: 213 ms early) |
| slew | 6 dB/s near, + 10/s x distance far (<= 120 dB/s); 0.3 s ramp for <= 0.5 dB steps, one hop otherwise | cold start T1dB 3.44 s -> 0.74 s with no change in steady pumping |
| prior | v1's estimate, weight falling linearly to 0 over 0.5 s of audible material | an exponential fade left a +24 boost where the material has nothing 20 dB off for seconds |
| silence gate | -60 LUFS per frame | holds the gain through 2 s gaps within 0.007 dB |
| range | +-24 dB | v1's +12 dB boost cap would leave a -24 dB cut of what is playing 12 dB short |

### Recommendation

Steady accuracy, pumping and every transient target are met (abrupt material
switches on their physically achievable gate, as above). **Recommend AUTO on for
new instances** (`kAutoGainDefaultForNewInstances`); not flipped here -- that is
a product call, pending the independent re-review.

## Old sessions and the default

A session that saved AUTO **on** before v2 (no `auto_gain_model` member, or
`auto_gain_model: 1`) keeps running v1, so its level does not change on reload;
saving it again keeps v1. The first time the user switches AUTO off and on, that
instance runs v2. Sessions saved before AUTO existed still open with it off. New
instances run v2 and still default **off** (`kAutoGainDefaultForNewInstances`).
Covered by `A session that saved AUTO on before v2 keeps v1 until AUTO is
toggled` (test/test_auto_gain_v2_transients.cpp).

## Upstream

The estimator is a product-independent unit staged for the Pulp SDK:
`include/spectr/upstream/loudness_compensation.hpp` (namespace
`pulp_candidate::signal`, depends on Pulp and the standard library only) with
its own tests, `test/test_loudness_compensation.cpp` (links `Pulp::signal` and
Catch2 only). Spectr keeps the adapter (`include/spectr/auto_gain_material.hpp`):
the renderer's realised response, the v1 prior, the negative-control seams, the
latency-aligned event queue, the session-state handoff and the wet-source tap.

## Re-running the measurements

```
ctest --test-dir build -R "autogain|loudness-compensation|Auto Gain"
cmake --build build --target Spectr-autogain-sweep
~/.pulp/tools/python-envs/audio-quality-lab/.venv/bin/python \
    tools/autogain_corpus_report.py --build build --out <dir>
```
