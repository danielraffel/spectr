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
- `P` is an exponential average with a **3 s** time constant over 170 ms Hann
  frames (8192 points, hop 4096 at 48 kHz), each frame weighted by its energy
  relative to the local level (below). A hit does not move it; a change of
  spectral balance that lasts does.
- **Silence gate:** a frame quieter than **-60 LUFS** (K-weighted, channels
  summed) is not material; the estimate holds through it instead of drifting
  toward the noise floor or resetting.
- **Slew:** material movement moves the target at **6 dB/s** near it, and
  proportionally faster far from it (10 x the distance, per second, at most
  120 dB/s). A one-step wobble of <= 0.5 dB rides a 0.3 s gain ramp; a real move
  rides a one-hop ramp. A shape edit (or switching AUTO on, or Mix) retargets at
  once, because that is the user asking for it.

## Following the material: changes, Freeze, locates, re-prepares

Three independent reviews shaped this. The first found the original v2 forgot the
material at every locate (a bass line under +24 dB on the top octaves restarted
20.6 dB low and took 3.7 s), took ~14 s after a Freeze release, and ignored a
quieter new part for ~3.5 s. The second found the fix for that too eager on
material that moves by itself (kick-only and full-drum bars alternating: 7
restarts and 0 <-> -15.5 dB swings; a decaying piano or drum tail restarting
the estimate every hit), a restored session's estimate slower than a cold start
on different material, and a host re-prepare forgetting everything. The third
found a quieter section after a loud one gliding for seconds under extreme
shapes (a verse after a 6 s chorus, top octaves +24: 4-6 dB too quiet for ~6 s;
chorus -> verse under a tilt: T1dB 3.8 s, settling 2.1 dB short), AUTO switched
on after the material changed while it was off taking ~6.5 s, and a 3 dB wobble
when a project reopens on the material it was saved on. v2 now:

- **Weighs frames by energy relative to the local level.** Each frame's power
  is divided by a 2 s average of frame power before it is averaged: inside a
  groove that level hardly moves, so a kick outweighs its tail exactly as
  integrated loudness weighs it; a swell or a quieter part no longer lets the
  loud past dominate for seconds.
- **Keeps the material across a locate, a host Reset and a re-prepare.** A
  locate restarts only the frame grid (so the block size still cannot matter).
  A re-prepare at the same geometry keeps the estimate whole; at a new rate or
  channel count it is carried across band-compressed and restored as the
  estimate (warm). The re-prepare the host does before a bounce therefore
  changes nothing.
- **Saves the estimate with the session** (`auto_gain_estimate`: 160 log bands,
  10 Hz-24 kHz, float32, base64 -- 3.4 KB). A reopened project, an offline bounce
  and a play from the start all start at the saved level; two renders from one
  saved state are sample-identical at any block size. A RESTORED estimate is a
  prior, not the estimate: the reopened project may play other material, so the
  material's own estimate takes over exactly as from a cold start (the same
  0.5 s fade), never slower. While the material AGREES with the restored prior
  (the make-up the fast shape gives is within the detector's 4 dB of the
  prior's), the prior fades three times as slowly (1.5 s), so the first frames
  of the same song -- one kick, one hat -- do not swing the gain.
- **Detects a change of material, and only a lasting one.** Beside the
  estimates it keeps fast (0.25 s) and slow (3 s) averages of each frame's
  SHAPE (level-independent, so a quieter new part shows at once). The gap
  between the make-up those two give the current shape must PERSIST: a leaky
  count -- up one per audible frame above 4 dB, down two per frame below -- must
  reach 0.7 s (0.4 s while the gap is above 12 dB). A fill, a hit, a decay
  drains away and is forgotten. A confirmed change restarts the estimate from
  the CANDIDATE -- the new material's own average since the gap was first seen,
  minus the frames whose windows straddle the change -- so it is right at once.
- **Quieter after louder.** When the frames' level (0.25 s average) has fallen
  at least 3 dB below its slow (3 s) average, after louder material that lasted
  at least 3 s, a gap of 2 dB counts instead of 4. That is exactly where the
  energy-weighted estimate is slowest: the louder past outweighs the quieter new
  part. A decay also falls in level, so this path confirms only material whose
  level is steady over the run (a least-squares slope no steeper than -6 dB/s:
  a piano or drum tail falls 13-17 dB/s), and the 3 s minimum leaves a fast
  alternation of short loud and quiet parts to the alternation memory.
- **Listens while AUTO is off.** The estimator always ran; the change detector
  now runs too (on the drawn shape at the current Intensity and Mix), so AUTO
  switched on after the material changed starts from the new material: about
  1.2 s to within 1 dB instead of ~6.5 s. CPU is unchanged: the response is
  derived only when the shape changes, as with AUTO on.
- **Recognises alternation.** It remembers the material before the last change
  and the material the last change went to (12 s). Material that goes back to
  either is not new: the halves are merged, and the slow estimate averages over
  12 s from then on, so kick-only / full bars stop being chased after one cycle.
- **Level-drop rule rescales level only, never the spectrum** (no frame within
  10 dB of the estimate's level for 4 s; 0.6 s for the live leg under a hold or
  a Mix, which has no change detector). A decay is not a change of material.
- **Freeze edges.** The live (dry) leg's spectrum is kept warm through a hold.
  On release the wet estimate becomes the live one at once and the gain jumps
  with the release crossfade (20 ms) instead of slewing after it; on engage the
  legs are treated as uncorrelated. The renderer's latency is added to every
  material event, so the gain moves with the audio the frame described.
- **Cold start** (a new instance, no saved estimate) begins at v1's estimate,
  whose weight falls linearly to zero over the first 0.5 s of audible material,
  and converges within about 0.75 s.

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
Mix. "Steady" means steady K-weighted loudness, not RMS. Evidence for every
number below is under `spectr-specs/auto-gain-v2/v2c/` (sweep report, raw,
transient and dynamic rows, ctest logs, and both independent reviews' own probes
rebuilt and re-run on this code).

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
| v2 | 492 | 0.00 | **0.18** | **2.21** | 0.35 | 2.49 |

(Earlier rounds, steady / from 0 s worst: the first v2 0.28 / 2.62; the second
0.21 / 2.46 and 3.14.) Target: p95 <= 1.5 LU and worst <= 3 LU -- met, from 0 s
as well. On narrow material v2 is better than v1 in **225 of 225** cases where
v1 is off by more than 0.5 LU (narrow p95: v1 14.94 LU, v2 0.03 LU).

| material | v1 p95 | v1 worst | v2 p95 | v2 worst | v2 worst from 0 s |
|---|---|---|---|---|---|
| pink | 0.21 | 0.42 | 0.02 | 0.07 | 0.10 |
| bass_line | 13.83 | 20.60 | 0.05 | 0.17 | 0.76 |
| vocal_buzz | 15.62 | 20.60 | 0.01 | 0.01 | 0.32 |
| hats | 14.56 | 18.18 | 0.01 | 0.02 | 0.15 |
| synth_pad | 14.54 | 18.97 | 0.05 | 0.16 | 0.16 |
| drum_loop_synth | 8.80 | 14.21 | 1.06 | 1.11 | 2.49 |
| sine_1k | 16.95 | 22.22 | 0.04 | 0.48 | 1.05 |
| pink_with_gaps | 0.24 | 0.44 | 0.10 | 0.16 | 0.14 |
| freeze_bass_then_hats | 13.78 | 20.60 | 0.04 | 0.14 | 0.56 |
| ql_drum_break | 6.63 | 10.87 | 0.61 | 2.21 | 2.47 |
| ql_tonal | 14.57 | 20.60 | 0.01 | 0.02 | 0.19 |
| ql_stereo_pad | 5.13 | 14.24 | 0.02 | 0.06 | 0.14 |

What is left is transient material under large low boosts (the drum break,
+24 dB below 170 Hz: +2.21 LU, as v1). Integrated loudness gates 400 ms blocks
against their own mean, and boosting the kick by 24 dB changes which blocks pass
that gate, which no estimate of the input's spectrum can see.

### Transients

Measured from the event: T1dB is the time for the applied gain to come within
1 dB of where it settles (its mean over the last second of the window); max
momentary is the largest error of the output's momentary (400 ms) loudness
against a flat AUTO-off render of the same scenario -- a perfect Auto Gain --
and "s > 6 LU" how long it stays above 6 LU. "first v2" re-creates the first
v2's transient behaviour in this build (`SPECTR_LEVEL_PLANT=autogain-v2a`).

| scenario | event | first v2 (v2a plant) T1dB / max mom / s>6 LU | v2 T1dB / max mom / s>6 LU |
|---|---|---|---|
| cold start: bass line, high +24 | start | 3.44 s / 20.1 LU / 2.40 s | **0.74 s** / 19.8 LU / 0.40 s |
| cold start: vocal, high +24 | start | 3.44 s / 20.2 LU / 2.40 s | **0.74 s** / 19.9 LU / 0.40 s |
| cold start: hats, high -24 | start | 3.03 s / 17.8 LU / 2.00 s | **0.70 s** / 17.3 LU / 0.40 s |
| cold start: sine 1k, mid narrow -24 | start | 2.74 s / 16.1 LU / 1.80 s | **0.70 s** / 15.3 LU / 0.40 s |
| cold start: drum loop, low +24 | start | 1.46 s / 9.3 LU / 0.70 s | **0.52 s** / 9.2 LU / 0.20 s |
| cold start: pink, low +12 | start | 0.00 s / 0.5 LU / 0.00 s | **0.00 s** / 0.5 LU / 0.00 s |
| locate every 3 s: bass line, high +24 | each locate (worst) | 2.91 s / 19.8 LU / 2.70 s | **0.00 s** / 0.4 LU / 0.00 s |
| locate every 3 s: drum loop, low +24 | each locate (worst) | 1.47 s / 9.3 LU / 1.00 s | **0.00 s** / 0.7 LU / 0.00 s |
| locate every 3 s: vocal, mid narrow -12 | each locate (worst) | 0.00 s / 0.8 LU / 0.00 s | **0.00 s** / 0.0 LU / 0.00 s |
| host reset mid-hold: bass held, hats live, high +24 | reset at 7 s | 3.44 s / 19.8 LU / 2.50 s | **0.00 s** / 0.0 LU / 0.00 s |
| Freeze: bass held at 4.5 s, hats live from 5 s, high +24 | engage | 0.00 s / 0.0 LU / 0.00 s | **0.00 s** / 0.0 LU / 0.00 s |
| Freeze: bass held at 4.5 s, hats live from 5 s, high +24 | release at 9 s | 7.68 s / 23.5 LU / 4.80 s | **0.01 s** / 5.5 LU / 0.00 s |
| change at 8 s: bass -> hats, high +24 | change | 6.49 s / 23.1 LU / 4.80 s | **0.98 s** / 22.7 LU / 1.00 s |
| change at 8 s: bass -> hats -15 dB (quieter after loud) | change | 7.90 s / 24.0 LU / 7.59 s | **1.11 s** / 24.0 LU / 1.00 s |
| change at 8 s: hats -> bass -15 dB | change | 6.68 s / 23.9 LU / 7.69 s | **1.17 s** / 23.9 LU / 0.99 s |
| change at 8 s: hats -15 dB -> bass (loud after quiet) | change | 6.44 s / 23.5 LU / 7.89 s | **1.15 s** / 23.1 LU / 1.20 s |
| change at 8 s: pink -> vocal -15 dB | change | 7.90 s / 20.8 LU / 7.59 s | **1.16 s** / 20.8 LU / 0.90 s |
| switch every 4 s, high +24: bass -> hats | switch | 3.90 s / 23.0 LU / 3.70 s | **0.97 s** / 22.7 LU / 1.00 s |
| switch every 4 s, high +24: hats -> vocal | switch | 2.52 s / 15.8 LU / 3.81 s | **1.08 s** / 19.5 LU / 1.10 s |
| switch every 4 s, high +24: vocal -> drums | switch | 2.73 s / 10.5 LU / 1.72 s | **1.04 s** / 16.1 LU / 1.00 s |
| switch every 4 s, high +24: drums -> bass | switch | 2.62 s / 12.3 LU / 3.70 s | **1.10 s** / 14.7 LU / 1.10 s |

Targets: T1dB <= 1.0 s after a locate (warm) -- 0.00 s; <= 1.5 s after a change
of material or a Freeze release -- worst 1.17 s and 0.01 s; max momentary error
<= 6 LU through locates, host resets, re-prepares and Freeze engage / release --
worst 5.5 LU. An ABRUPT change of material cannot meet 6 LU momentary with any
causal Auto Gain: when the needed gain jumps by 24 dB, even a perfect
compensator that reacted 10 ms after the switch would leave the 400 ms window
10 log10((0.01 x 251 + 0.39) / 0.4) = 8.6 LU over, and one that needs a single
170 ms analysis window 20.3 LU. Those cases are gated on T1dB and on the time
above 6 LU (<= 1.6 s; worst 1.20 s).

The third review's section probes (`review4 sections`, rebuilt on this code),
second section alone as the target, settled error over 3-5 s after the change:

| change at 10 s | shape | third review (v2c): T1dB, settled error | now |
|---|---|---|---|
| chorus -> verse (6 dB quieter) | tilt -12..+12 | 3.80 s, -2.11 dB | **1.17 s, -0.18 dB** |
| chorus -> verse | high broad +24 | 1.26 s, -0.03 dB | **1.09 s, -0.04 dB** |
| verse 0-10, chorus 10-16, verse from 16 s | high broad +24 | -6.6 dB at 16.5 s, -3.2 at 22 s (needs -0.5) | **1.15 s, -0.03 dB** |
| AUTO off, bass 0-3 s then hats, AUTO on at 6 s | high broad +24 | -15.2 dB at 7 s, -20.7 at 9 s (needs -24) | **1.23 s** (-21.7 at 7 s, -23.9 at 8 s) |
| reopened on the same drum loop (worst deviation in 4 s) | high broad +24 | 2.98 dB | **1.79 dB** |
| reopened on the same dense mix | high broad +24 | 1.96 dB | **1.00 dB** |

Every other row of the section, alternation, startup, restart, materials, rates,
Freeze, transition, Mixing, zipper and state probes is unchanged or better. Two
quieter-after-louder cases are still slower than 2 s: verse after chorus under
the low octaves +12 (a 1.5 dB difference, under the 2 dB threshold: about 1.1 dB
short for ~4 s, where AUTO off is 11 dB out), and DJ song A -> song B under the
top octaves +24 (louder AND brighter: T1dB 3.75 s, settled +1.24 dB; unchanged
from v2c).

### Material that moves by itself

The second review's probes, and the same cases in the gates and the sweep: a
constant gain leaves the spread (sd) of momentary error against the flat AUTO-off
render exactly where AUTO off has it, so "excess" is how much v2's movement adds
(from 3 s). The review's own `dynamic` probe, rebuilt on this code (60 material x
shape rows):

| material | shape | second v2: restarts, sd v2 / off | now: restarts, sd v2 / off |
|---|---|---|---|
| kick-only / full drums alternating | high broad +24 | 7, 9.22 / 6.82 | 2, 7.80 / 6.82 |
| kick-only / full drums alternating | tilt -12..+12 | 5, 6.12 / 4.67 | 2, 5.34 / 4.67 |
| piano hits every 3 s | high broad +24 | 6, 1.80 / 1.19 (range 6.8 dB) | 0, 1.42 / 1.19 (3.2 dB) |
| drum hit + darkening tail / 3 s | high broad +24 | 6, 2.11 / 1.85 (range 4.2 dB) | 0, 1.83 / 1.85 (0.1 dB) |
| pink fade-out 45 dB / 8 s | low broad +12 | 2, 0.28 / 0.23 | 0, 0.23 / 0.23 |
| dense -> pad breakdown -> dense | low broad +12 | 2, 2.70 / 3.65 | 1, 2.43 / 3.65 |
| dense -> pad breakdown -> dense | high broad +24 | 0, 2.62 / 2.66 | 1, 2.45 / 2.66 |

Over all 60 rows the worst excess is now +0.23 LU except the kick-only /
full-drum alternation under the two most extreme shapes (+0.98 and +0.67 LU from
3 s). That is the cost of the alternation's first cycle: until a pattern has
come back once it cannot be told from a real change of material, which must be
followed within 1.5 s. From the point it is recognised (10 s on) the excess is
at most +0.22 LU. Gates (`Auto Gain v2 does not pump on sparse or alternating or
decaying material`): excess <= 0.25 LU (the alternation from 10 s), at most two
restarts on the alternation, the swell's applied gain within 1.5 dB.

The drum loop swelling +-12 dB under +24 dB on the top octaves moves the applied
gain 1.44 dB (0.65 dB without the swell; the first v2's behaviour on this same
case, 1.68 dB).

### The ctest gates (fast, deterministic)

`test/test_auto_gain_v2.cpp` and `test/test_auto_gain_v2_transients.cpp` (all in
the default ctest), with a re-creation of each fixed defect registered as a
negative control that must fail its gate:

| gate | result | negative control (must fail) |
|---|---|---|
| accuracy: 6 materials x 6 shapes | p95 0.16 LU, worst 0.63 LU, 22/22 better than v1 | `autogain-v2-unweighted` |
| pumping: steady drums / pink / bass | spread 0.144 dB, +0.013 LU, 0 restarts | `autogain-v2-no-smoothing` |
| chunking (8 chunkings, gain moving) | Mixing 0 samples differ; Tracking <= 8.4e-9 | -- |
| locate every 3 s | T1dB 0.00 s, 0.66 LU | `autogain-v2-reset-on-seek` |
| Freeze engage / release | 0.00 s / 0.01 s, <= 5.5 LU | `autogain-v2-stale-on-change` |
| change of material (5 cases) / switches every 4 s | T1dB <= 1.25 s / <= 1.10 s | `autogain-v2-stale-on-change` |
| sparse / alternating / decaying / breakdown / swell | excess <= 0.23 LU; swell 1.44 dB; 2 restarts | `autogain-v2-short-persistence` (the second v2's 3-frame detector, no memory): the alternation's excess +1.58 LU from 10 s (sweep) |
| host re-prepare (same, other block size, 44.1 kHz; after a restore) | T1dB 0.00 s, <= 0.08 LU | `autogain-v2-reprepare-reset` |
| restored estimate on other material | T1dB <= max(cold, 0.75 s) | `autogain-v2-restore-as-warm` |
| quieter section after a loud one (3 cases) | T1dB <= 1.17 s, settled <= 0.18 dB (gates 2 s, 1 dB) | `autogain-v2-no-drop-path`: 7.09 s, 4.34 dB |
| AUTO switched on after a change while off (2 cases) | T1dB 1.23 s (gate 1.5 s), settles where AUTO-on-throughout does | `autogain-v2-detect-only-when-on`: 4.04 s |
| reopened on the same material, high +24 | worst deviation 1.79 dB (gate 2.0 dB) | `autogain-v2-restore-fixed-fade`: 2.98 dB |
| warm restore / two renders from one state | first block within 0.01 dB; 0 samples differ | -- |
| pre-v2 session keeps v1 until toggled, also with a flat shape | v1 -> v2 on off/on | `autogain-v2-legacy-composed-only` |

The loudness-compensation unit's own tests (`test/test_loudness_compensation.cpp`)
cover the detector's persistence, the candidate restart and the merge, the
rescale-only level-drop rule, prior vs warm import -- including that a warm
import's slow shape is armed on the carried material (the second review's
ordering bug; re-introducing it fails that test: slow shape 1.00 above 2 kHz
after 0.5 s against a bound of 0.4) -- and the detector's quieter-after-louder
path: a steady 7 dB drop with a 3 dB gap confirms (9 frames), a 1 dB/frame decay
never does, a loud burst shorter than the minimum does not arm it, and with the
path off the same drop is never confirmed.

Through the built AU, `Spectr-au-offline-bounce-equivalence-auto-gain`: paced
render vs OfflineRender bounce with AUTO on and a change of material,
`max |paced - bounce| = 0`.

### Decisions

| decision | value | evidence |
|---|---|---|
| frame weighting | energy relative to a 2 s local level | equal-frame or sqrt weighting calmed the swell (0.90 dB) but cost drum accuracy (drum loop p95 1.69 vs 1.06 LU); without the local level a quieter part waits for energy to decay |
| change detector | shape gap > 4 dB, leaky count to 0.7 s (0.4 s above 12 dB) | 3 frames: 7 restarts on kick/full bars and every piano or drum-tail decay; 1 s: switches T1dB up to 1.7 s |
| restart seed | the candidate (since the gap was first seen, minus straddling frames) | from empty: a 0.5 s hold was needed and T1dB rose; with straddling frames: a 1 dB tail on bass -> hats |
| alternation memory | the material before the last change and the one it went to, 12 s; merged halves average over 12 s | kick/full: 7 -> 2 restarts, excess from 10 s +0.17 LU |
| level-drop rule | rescale only; 4 s wet, 0.6 s live leg | restarting on a level drop restarted every decay; the live leg needs the short window (Freeze + Mix 50 %, low -24: -2.28 LU with 4 s, -0.04 LU with 0.6 s) |
| restored estimate | a prior (same 0.5 s fade as a cold start; 1.5 s while the material agrees) | as the estimate itself, other material took 1.2-3 s (the review: pink still -18.8 dB at 3 s); seeding the estimate with 1 s of weight cut the reopen wobble to 0.46 dB but slowed other material to 1.0-6.3 s |
| quieter after louder | 2 dB gap after a >= 3 dB level drop that follows >= 3 s of louder material, steady level (slope >= -6 dB/s) | chorus -> verse tilt 3.80 -> 1.17 s; without the 3 s minimum a 2 s verse/chorus alternation restarted (+0.17 LU sd); a drop of 4.5 dB instead of 3 missed the 16 s alternation |
| AUTO off | detector runs | 6.5 s -> 1.2 s after switching on; CPU within run-to-run noise (12 s of pink, 512-sample blocks: Tracking 184.7 vs 179.4 ms static, band wiggle 2080 vs 2090 ms) |
| re-prepare | keep the estimate (same geometry) or carry it band-compressed (new rate) | otherwise a -19.9 dB dip on every host re-prepare |
| slew | 6 dB/s near, + 10/s x distance far (<= 120 dB/s) | unchanged from the second v2 |
| time constant | 3 s | quick subset: 1 s p95 0.58 LU, spread 2.47 dB; 3 s 0.22 LU, 0.65 dB; 6 s 0.16 LU, 0.42 dB, twice as slow to follow |

### The default

AUTO is **on for new instances** (`kAutoGainDefaultForNewInstances`), decided
after the third independent review: steady accuracy p95 0.18 LU (v1 14.00 LU),
transients within their targets, and on material that moves by itself at most
+0.23 LU of momentary spread over a constant gain -- except the first cycle of an
alternation of very different halves under the most extreme shapes.

## Old sessions and the default

A session that saved AUTO **on** before v2 (no `auto_gain_model` member, or
`auto_gain_model: 1`) keeps running v1, so its level does not change on reload;
saving it again keeps v1. The first time the user switches AUTO off and on, that
instance runs v2. Sessions saved before AUTO existed still open with it off, and
a session that saved AUTO off keeps it off. New instances run v2 with AUTO
**on** (`kAutoGainDefaultForNewInstances`); covered by `A session saved before
Auto Gain opens with it off; new ones keep it` (test/test_level_controls.cpp).
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
