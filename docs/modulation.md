# Internal modulation: targets, depths, stacking

Spectr has two tempo-synced LFOs. An LFO defines a **movement** -- its Shape
and its Rate in beats -- and nothing else. What it moves, and how far, is set
per **target**: each LFO drives any set of targets at once, and each target
has its own **Depth**. This page is the contract for how targets combine, what
each one does, how the controls record and play back, and how sessions and
automation from before per-target routing behave. Parameter IDs are in
[parameter-surface.md](parameter-surface.md); recording and playback in
[automation.md](automation.md).

## Targets

Shown in this order -- most-modulated first -- in the band menu (right-click a
band > Modulation > LFO n TARGETS) and in Settings > MODULATION, which list
the same targets from one source (`spectrModulationRouteList`) and edit the
same lanes, so either follows the other live.

| Target | What it moves | At Depth 100 % |
| --- | --- | --- |
| Bank | every band's level, together | +/-12 dB |
| Band shift | slides the whole band set up/down in frequency, width kept | +/-1 decade (about 3.3 octaves) |
| Band spread | spreads the bands wider/narrower about their centre | width x2 / x0.5 (log-frequency) |
| Intensity | pulls the Intensity amount toward flat and back (unipolar) | all the way to flat at the wave's top |
| Mix | pulls Mix toward dry and back (unipolar): the freeze blend | all the way to dry at the wave's top |
| Morph | the A/B morph position, around the Morph slider | +/-0.5 of the morph range |
| Freeze | LIVE / FROZEN, gated by the LFO | frozen the whole cycle (Depth = frozen duty) |
| Length | the next freeze's loop length, around the user's LENGTH | +/-8 steps of the LENGTH list |
| Bands | the band count the mask is built with, around the user's BANDS | +/-4 steps (the whole 32...64 list) |
| Preset | morphs toward the neighbouring presets' band gains (menu order) | +/-4 presets |
| Output | the Output trim, after Auto Gain | +/-6 dB (clamped with the trim to +/-24 dB) |
| Snapshot A / B | blends toward that captured snapshot and back (unipolar) | all the way there |

Parameter IDs: the first eight targets at 4020... (on/off) and 4030...
(Depth), the three level targets in their own block at 4060... / 4070...,
Bands and Preset in a third at 4100... / 4110... (LFO 2 +20 throughout);
Freeze's Hold for Length is 4140. See [parameter-surface.md](parameter-surface.md).

Effective modulation of a target is **wave x that target's Depth**. There is no
LFO-level depth. A target that is switched off contributes nothing whatever its
Depth, and shows no Depth row: each target is one compact row (name, switch),
and its Depth row appears, indented under it, only while it is on (progressive
disclosure, in the band menu and in Settings alike). Several enabled targets
show their Depth rows together; a switched-off target keeps its stored Depth
for when it is switched on again. Switching a target never moves the row under
the pointer: the Depth row opens below it and the list's scroll offset is
kept. Depths default to 50 % (the LFO depth a fresh 1.0.x instance opened
with); a fresh instance drives Bank from both LFOs, everything else off.

## How stacked targets combine

Deterministic, independent of which LFO is evaluated first, and level-safe:
`compose_internal_modulation` in `include/spectr/modulation.hpp`, one pure
function that the audio owner renders and the editor draws.

1. **Sum per target.** Every enabled (LFO, target) route adds
   `wave x Depth` to that target's coordinate (Snapshot A/B take the unipolar
   `(wave + 1) / 2`). Two LFOs on one target add; opposite phases cancel.
2. **Apply each target once, in a fixed order, clamping once:**
   Morph (the field follows the *difference* the morph move makes, so it is
   the identity at zero and keeps macros and later band edits), then Snapshot
   A, then Snapshot B (blends: every band stays between its inputs), then Bank
   (an offset, clamped into the band range once), then Band spread and Band
   shift on the window.

Morph and the snapshots reshape; Bank offsets the shape, so Bank + Morph is a
morphing shape that also breathes. Authored mutes always survive: a muted band
is excluded from modulation and no target toggles a mute.

Freeze, Length and Bands do not touch the field (below), and neither do the
level targets. Preset reshapes the field after all of the above (its own
section).

## Intensity, Mix and Output

These move a level control around the user's setting and never write it: the
knob keeps showing its own value, its host lane keeps its automation.

- **Intensity** and **Mix** are unipolar pulls, in proportion to the knob:
  `effective = knob x (1 - c)`, `c = (wave + 1) / 2 x Depth` (summed over
  LFOs, clamped to 0..1). Proportional rather than an offset so the full
  Depth is always usable without clipping -- the knobs default to 100 %, where
  an offset could only ever move one way -- and a knob set lower is scaled
  rather than pinned at zero. At Depth 100 % Intensity reaches flat and Mix
  reaches dry once per cycle. Over a frozen sound the Mix target is the freeze
  blend: frozen and live alternate at the LFO rate.
- **Output** is bipolar: `wave x Depth x 6 dB` added to the trim, the sum
  clamped into the trim's +/-24 dB range. 6 dB each way (12 dB peak to peak)
  is a clear tremolo without the level jumps a +/-12 dB swing on the final
  gain would invite; two LFOs on Output add.

**Auto Gain never cancels them.** It is computed from the unmodulated
Intensity and Mix (the knobs), and the Output target is applied after it, on
top of the trim, the same rule that keeps a Bank LFO audible: a level LFO stays
audible as level.

**Smoothing.** A route's level is slewed like every other target's; the
Intensity route over 200 ms full scale, the Intensity knob's own slew, because
a full-depth Intensity route switched on at a crest moves every band at once.
The Output gain is ramped per sample from the previous slice's value to the
LFO value at the END of the slice, so a running Output LFO is a smooth gain
and slices of different lengths meet without a kink (evaluating at the slice
start lagged a slice and steepened a short slice 2.7x). Mix rides the mixer's
own ramp; Intensity restages the mask per block like the knob.

Measured (`test/test_level_controls.cpp`, 48 kHz, 512-sample blocks):

| Case | Result |
| --- | --- |
| Output, Depth 100 % / 50 %, square at its top, Auto Gain on (+12.000 dB make-up) | +6.000 / +3.000 dB, Auto Gain unchanged |
| Intensity on a +12 dB shape, Depth 100 % / 50 % | 0.000 / +6.000 dB; Auto Gain -10.084 dB with and without the route |
| Mix on a -24 dB shape, Depth 100 % / 50 % | 0.000 dB (dry) / -5.49 dB |
| Output LFO running (sine, 1 beat, Depth 100 %): largest 1 ms envelope step | 0.075 dB (gate 0.2; per-block plant 0.80) |
| Output switched on at a crest | 0.100 dB / ms |
| Intensity switched on at a crest, +12 dB shape | 1.05 dB per block (Bank-LFO yardstick 2.3) |

`Spectr-level-target-step-negative-control` re-runs the smoothness case with
`SPECTR_MODULATION_PLANT=level-target-step` (the Output gain lands once per
block) and must fail on its gate.

**The knobs.** MIX, INTENSITY and OUTPUT show the base value, never the
modulated one: the value under the pointer is the value a drag starts from,
and a needle that moved on its own would make every grab look like a jump.
While an LFO drives one, its track ring and rim are tinted violet (existing
nodes recoloured; no animation, so it costs nothing per frame; the rim because
at 100 % the value arc covers the whole track). Grabbing a driven
knob -- a drag, a wheel notch or an arrow key -- asks the override question
(below) on the release of the press; **Keep modulating** lets the knob turn
from then on without asking until the set of LFOs driving it changes, and the
user's new value is the new centre.

### Changes against 1.0.6

One LFO on one target sounds as before. Where 1.0.6 composed sequentially:
Bank (or Snapshot A/B) with Morph now both apply (Morph used to overwrite);
both LFOs on Morph now sum (LFO 1's morph used to be discarded); both LFOs on
Bank clamp once instead of per LFO; and Morph with the slider never moved now
moves the drawn field relative to itself instead of jumping to the A/B morph.

## Band shift and Band spread

In Spectr the viewport is a DSP input -- it sets the frequency span the bands
cover -- so these two sweep the filter bank. They modulate the **audible**
window around the window the user set (after any morph derivation), clamped
into 20 Hz..20 kHz with a one-octave minimum; at an edge Band shift holds the
width rather than squeezing it. The viewport lanes (3001, 3002) and their
automation are never written by an LFO.

**Audio only.** The plot and the minimap keep showing the user's window, and
every pointer hit-test runs in it, so the band under the pointer is always the
one edited (proved in `test_native_state_parity.cpp`, "viewport modulation
never moves the band under the pointer"). An overlay of the audible window was
built and removed by decision; pausing the sweep while a band is dragged was
rejected because it changes what is heard the moment a band is touched.

## Freeze

The LFO gates LIVE / FROZEN: frozen while the wave is above a threshold placed
so that **Depth is the frozen duty** exactly -- 0 % never, 50 % half the cycle
(a centred threshold), 100 % always. Sine uses `cos(pi x duty)`; triangle and
saw (uniform over a cycle) `1 - 2 x duty`; a square, which has only two values,
freezes for a window of `duty` of the cycle centred on its high half. Both LFOs
on Freeze: frozen while either gate is (`lfo_freeze_gate`).

Every rise of the gate is an ordinary engage through the freeze source --
capturing fresh audio, with the usual equal-power crossfade, amortised engage
and stationary spectral hold -- and every fall an ordinary release. The gate is
evaluated per audio block (a 256-sample block is 5 ms at 48 kHz).

The LIVE/FROZEN button shows the state being played. The processor sends
`freeze_display` only when it changes, and the button paints its own nodes for
it; a gate transition renders nothing in React.

Pressing the button (or Q, or the chord) while an LFO drives Freeze flips what
the button shows, and that holds until the gate next changes; then the LFO
carries on. Host automation of the Freeze lane behaves the same way.

### Hold for Length

**Hold for Length** (lane 4140, off by default; under the Freeze target in the
band menu, in Settings > MODULATION and in the LIVE / FROZEN context menu)
changes what a gate means: each **rising edge** latches the freeze for exactly
the effective Length -- the length that engage takes, Length target included --
then releases it, ignoring the LFO's off-phase meanwhile; the next rising edge
after the release latches again. A rising edge during a hold is ignored. The
release lands on the audio slice at or after the Length (at most one block
late; the gate is evaluated per block). Off, the gate follows the LFO as
above. Measured (`test/test_modulation_freeze.cpp`, "Hold for Length latches
each Freeze-target engage for exactly the Length"): 1 bar at 120 and 90 BPM and
a fraction of a bar at 150 BPM, every hold within one 256-sample block of the
Length, re-latched one LFO cycle later; the same render with the switch off
holds only the gate's own 10 % window. The editor's switch follows a host
playing this lane at the next live projection.

## Length

The user's LENGTH is the centre. At each freeze **engage** -- from the Freeze
target, the button, a key or automation -- the Length coordinate at that moment
(`wave x Depth`, summed over LFOs) picks the loop length:
`round(coordinate x 8)` steps through the LENGTH list (16 fractions of a bar,
then 1, 2, 4 and 8 bars), clamped to its ends (`modulated_length_index`). A
custom LENGTH is centred on the list entry nearest it. A loop that is already
playing is never resized.

While an LFO drives Length, the closed LENGTH control shows, in the violet of
a modulated knob (text and rim), the length the freeze uses: while frozen, the
length the playing loop took at its engage; while live, the length the next
engage takes, moving as the LFO moves. Its menu keeps showing -- and editing --
the user's own LENGTH. The label is the processor's answer
(`freeze_shown_length_index`, carried by `freeze_display`, which is published
only when it changes), so whenever Freeze engages it takes exactly the length
the label showed (`test_native_state_parity.cpp`, "LENGTH, BANDS and the
preset label show what their LFO plays").

## Bands

The user's BANDS is the centre. `round(coordinate x 4)` steps through the
band-count list (32, 40, 48, 56, 64), clamped (`modulated_band_count`); at
Depth 100 % a full swing covers the whole list from 48. The plot keeps drawing
the user's band count (like Band shift, the target is audio only); the closed
BANDS control shows the count playing, in violet.

A band-count change is structural: it re-lays the drawn slots over the
spectrum. A switch straight across -- what a host's Band Count lane does --
sprays broadband energy at each switch, so the target **crossfades through
flat**: the shape fades to flat over 80 ms at the old count
(`kBandsFadeSeconds`), the count switches while nothing is shaped (silently),
and the shape fades back in at the new count, every step a gain restage the
renderer carries like an Intensity move. No allocation: the layout holds all
64 slots, and slots past the user's count are neutral. A count change while a
fade is under way retargets it.

Measured (`test_modulation_freeze.cpp`, "the Bands destination steps the band
count click-free inside the cost gate": a sine at 1 s sweeping 32...64 over a
+/-12 dB comb, a steady 2 kHz tone, 256-sample blocks at 48 kHz):

| | largest 1 ms step | broadband splatter (worst 5 ms, vs the tone) |
| --- | --- | --- |
| Bands target | 0.55-0.70 dB | -49...-52 dB |
| host Band Count lane switching straight (plant) | 10-11 dB | -15...-16 dB |
| route at Depth 0 (control) | 0.00 dB | -148...-152 dB |

Cost per 256-sample callback, each block's cheapest of three renders: median
155-171 us, max 338-368 us (budget 5 333 us). `kBandsTargetDisabled` is the
escape hatch (plays the user's count, lanes kept); it is off.

## Preset

The Preset target morphs the composed field (after Morph, the snapshots, Bank
and the macros) toward the presets next to the current one in the preset
menu's order (factory, then user): the coordinate x 4 is a continuous position
in presets, and between two presets each band's gain interpolates, the
morph's own rule. Position 0 is **the field as it stands, the user's edits
included**: the target never discards an edit, it moves away from the current
sound toward its neighbours and back. Mutes never move
(`preserve_authored_mutes`); a band a preset floors goes to -24 dB. At the
ends of the list it stops at the last neighbour that exists.

The neighbourhood is the editor's: when a preset is applied, the band count
changes or the user library changes, it resolves the presets four either side
at the current band count -- exactly what applying each would write -- and
sends their names and gains (`preset_modulation_set`); the processor publishes
them to the audio owner with the rest of the modulation state and saves them
with the session, so the target keeps playing when the project reopens. Until
a preset has been chosen once the target has nothing to move toward and is
silent. The preset label shows, in violet, the preset the target is nearest.

Measured (`test_modulation_freeze.cpp`, "the Preset destination morphs toward
neighbouring presets and back"): neighbours at +12 and -12 dB around a flat
current preset, Depth 25 % (one preset each way), a square LFO: the halves
measure 24 +/- 3 dB apart and the shown step is +1 / -1 by half.

## Header controls: context menus

Right-clicking LIVE / FROZEN, MIX, INTENSITY, OUTPUT, LENGTH or BANDS opens a
small menu in the band menu's style: the control's name, **Reset to** its
default (Live, 100 %, 0.0 dB, 1 bar, 32 bands), then **MODULATION** scoped to
that control's target -- LFO 1 and LFO 2, each a switch with its Depth slider
under it while on (a target on for an LFO that is itself off reads "On, LFO
off"; the menu does not switch the LFO on behind the user's back), plus Hold
for Length under LIVE / FROZEN -- and **Ask before overriding modulation**.
One overlay: an outside press, Escape or a press on its control closes it.

## Touching a modulated control

Modulation keeps running. A Freeze press holds until the gate's next change; a
LENGTH, BANDS or preset pick becomes the new centre. With **Ask before overriding modulation**
on (Settings > MODULATION, default on) Spectr asks first, in the preset
dialogs' style: "Freeze is being modulated by LFO 1. Turn off its Freeze
target?" -- **Keep modulating** applies the action and leaves the LFO running;
**Turn off** writes that LFO's target lane off (a recorded host gesture) and
then applies it. Return = Turn off, Escape = Keep modulating, and **Don't ask
again** turns the Setting off. The dialog is generic
(`window.spectrOverrideModulated(control, target, lfos, action)`); the Mix,
Intensity and Output knobs ask it in their own names.

## Smoothness

Every routing lane can be automated at any sample, so none may step the sound.
A route's audible level (`enabled ? Depth : 0`) is slewed like an LFO's own
on/off (`slew_lfo_level`): full scale per 60 ms for the level targets, per
250 ms for Band shift and Band spread (`kViewportRouteSlewSeconds`) -- fading
a viewport route moves the whole bank by up to a decade, which over 60 ms
outran the running sweep. Band shift/spread restage the mask every block, and
the renderer's swap crossfade spans the gap between restages, the path a user
zoom drag takes. Freeze and Length need no slew: the freeze source has its own
engage and release crossfades.

Measured (`test/test_modulation_routing.cpp`, 512-sample blocks at 48 kHz, sine
LFO at 1 beat, -12 dB bank; gate = 15 % of the 24 dB swing):

| Control | Max step, shipping | Max step, route-step plant (fail-before) |
| --- | --- | --- |
| free-running LFO (yardstick) | 1.61 dB/block | 1.61 dB/block |
| Bank switched on at the crest | 2.13 dB/block | 11.99 dB/block |
| Bank switched off at the trough | 2.25 dB/block | 11.86 dB/block |
| Depth 10 % -> 100 % at the crest | 2.14 dB/block | 10.80 dB/block |
| Depth ramp 0 -> 100 % over 2 s, switched off/on mid-render | 2.20 dB/block | 9.11 dB/block |
| Band shift switched on at the crest (decades/block, gate 0.30; free-running 0.134) | 0.072 | 0.999 |

`Spectr-route-smoothness-negative-control` re-runs those with
`SPECTR_MODULATION_PLANT=route-step` and must fail.

The Freeze target over a steady tone at a 1/16-note square (8 engages and 8
releases a second): largest 1 ms envelope step 0.01 dB; per-callback cost
(256-sample blocks, cheapest of 3, budget 5 333 us) median 250 us / max
1 210 us against 265 / 308 us with the target off
(`test/test_modulation_freeze.cpp`).

### Through a real host

`Spectr-au-routes-host` (`tools/au_routes_probe.cpp`) loads the built
`.component` in-process (offline: no install, no device, no window) and plays
the routing lanes the way a DAW does -- `AudioUnitScheduleParameters`, a Depth
ramp written as one event per 128-frame render call, switches at LFO crests and
troughs -- through a steady 2 kHz tone, scoring each edge against a render with
that target running steadily:

| Edge | whitened spike, dB (reference) | largest 1 ms step, dB (reference) | costliest call |
| --- | --- | --- | --- |
| Bank off at crest | 10.2 (10.2) | 0.42 (1.28) | 274 us |
| Band shift on at crest | 8.9 (8.6) | 0.75 (2.33) | 340 us |
| Band shift off at trough | 7.7 (8.6) | 0.67 (2.33) | 273 us |
| Band spread on at crest | 11.2 (11.1) | 0.14 (0.48) | 329 us |
| Band spread Depth 100 -> 20 % | 11.0 (11.0) | 0.09 (0.48) | 397 us |
| Band spread off at trough | 11.5 (11.7) | 0.08 (0.48) | 314 us |
| Bank on at crest | 10.1 (10.3) | 0.63 (1.28) | 266 us |
| Depth ramp 0 -> 100 % over 2 s | -- | 0.63 (1.28) | -- |

Budget 2 667 us per call; reference p99 344 us. It is a host-path gate (lanes
arrive sample-accurately through a real AU, no click, no slow call), not a
detector for a lost route ramp: the renderer's swap crossfade spreads even an
un-ramped switch to about 1 dB/ms, so it passes with the plant too. The ramp is
proven at the field level above. No REAPER pass was run (not installed on the
machine allowed to run one).

## Compatibility: old sessions and old automation

- **Pre-routing sessions** (no `lfo_routing` marker) map their single
  selection -- the saved Destinations mask, else the `4004` value -- to "that
  one on" for **both** LFOs, carry each LFO's Depth into each enabled target's
  Depth (other targets take the default), and leave Freeze/Length off. They
  sound as they did. (`lfo_routing: 1` blobs, from development builds where a
  target's amount was relative to an LFO depth, are folded the same way.)
- **`4004` (LFO Target) is a command lane.** A change of its value selects that
  one of Bank / A / B / Morph for both LFOs, leaving the other targets and every
  Depth alone, and writes the routing lanes so editor and host agree. Applied
  at the event's block. Never written back: an enum cannot represent several
  targets, and a processor-side write would be recorded in Write/Latch and
  replay as a command that collapses the selection.
- **`4003` / `4013` (LFO / LFO 2 Depth) are command lanes** the same way: a
  change sets the Depth of every target that LFO currently drives. Never
  written back, so a host shows the last command, not the per-target Depths.
- **A command only applies when it moves alone.** If the host writes any of an
  LFO's routing lanes (a target's on/off or Depth) in the same pass as `4003`,
  `4004` or `4013` -- a session restore, a CLAP `params.flush()`, a host
  setting every parameter at once -- the routing lanes are the explicit
  statement and the command is not applied to that LFO. Old automation only
  ever moves the legacy lanes, so it still replays as a command. This makes the
  result independent of how the host's writes and the parameter-sync pass
  interleave (clap-validator's `state-reproducibility-flush`). The lanes stay
  automatable rather than read-only so pre-1.0.7 automation keeps working.
- `modulation_target_mask` is saved from the routing lanes themselves (not
  from a reconciled copy), so identical parameter values always save an
  identical blob.
- `modulation_target_mask` keeps being saved (LFO 1's level targets) so an
  older Spectr opening a new session plays the nearest thing it can.

## Cost

Audio thread, per 512-sample callback (Tracking renderer, each block's
cheapest of 3 identical renders; budget 10 667 us): LFO 1 on Bank 376-431 us
median / 462-501 us p99, on Band shift 363-432 / 464-471 us, on Band spread
370-432 / 458-510 us. Both kinds restage the mask every block; the redesign
runs on the renderer's worker and the audio path stays allocation-free.

Editor frame (`SPECTR_ROUTE_FRAME_COST=1 Spectr-native-shot`, 64 bands, one
800-sample audio block then one display tick, 400 frames, all 460 modulation
frames delivered): Bank p50 0.055 / p95 0.276 ms, Band shift 0.068 / 0.298 ms,
both 0.066 / 0.288 ms, Bank again 0.067 / 0.294 ms -- no p95 regression beyond
the Bank baseline's own spread. Band shift adds no editor work: it is audio
only.

## The menu, measured

### Progressive Depth rows, a capped panel, and a wheel that stays put

Each target is one row and its Depth row is disclosed only while it is on, so
the default list (Bank on) is 13 rows plus one Depth row. The submenu is
capped at 560 design px (it used to take the editor's whole height whenever it
could, 736 px), so wherever the band menu opens -- near the top of the window
included -- the panel sits beside it and the target list scrolls inside it.
Captured by `SPECTR_POLISH_SHOTS=1 Spectr-native-shot --backend=skia`, in
[`evidence/2026-10-03-polish/`](evidence/2026-10-03-polish/) (with the
before shots of the submenu and of the tooltip).

While any menu, submenu, dropdown or popover is open the wheel belongs to it:
over the menu it scrolls only the menu and stops at its ends without chaining
(the head, either end of the list, a sideways trackpad delta), and anywhere
else it is swallowed and the menu stays open -- the macOS menu behaviour.
Before, a wheel over the Modulation submenu's head or past the end of its list
zoomed the viewport behind it. The rule belongs to Pulp's overlay routing
(`route_passive_pointer` / `deliver_mouse_wheel`, Generous-Corp/pulp#9307,
Pulp SDK 0.901.0 and later): a wheel outside an open menu is dropped, and one
inside it never bubbles past the menu's root.
`test_native_state_parity.cpp`: "while a band menu is open no wheel reaches the
plot behind it".

### With thirteen targets: the scrolling list

Thirteen targets are up to 26 rows -- a switch each, and a Depth row under
each one that is on; about 754 design px at the 29 px a row measured with
eleven -- plus Hold for Length under the Freeze target while it is on, under
a 238 px head, well past the 780 px the menu may use. Pulp does not scroll an overflow container, so the
submenu is the help guide's scroller: a fixed head -- Back, the LFO switches,
EDIT LFO, Shape, Rate and the **LFO n TARGETS** heading, which therefore stays
put -- over a viewport that clips the target rows at a numeric height (502 px
at 990 x 645) and moves them by a negative margin. A clipping viewport also
keeps a scrolled-away row from taking a press. The wheel (and a trackpad's
small deltas) move the rows directly, without a re-render; a 4 pt Spectr
scrollbar shows the position; toggling a target or switching EDIT LFO keeps
the offset; a keyboard move (arrows, Home, End) scrolls its row fully into
view; each opening starts at the top. Bank, Band shift, Band spread and
Intensity with their Depth rows show without scrolling at 990 x 645.

`test_native_state_parity.cpp`: "the Modulation submenu scrolls its thirteen
targets under a sticky heading" (order, fit, wheel, offset kept on toggle,
keyboard reveal), and the all-controls first-press sweep covers the head and
every target row at every wheel position. That sweep found menu slider tracks
reaching 8 pt up into the switch row above; they reach 6 pt now.

### Eight targets (1.0.7 development)


Captured headless (`SPECTR_MODULATION_ROUTE_SHOTS=1 Spectr-native-shot
--backend=skia`), in [`evidence/2026-10-02-lfo-routing/`](evidence/2026-10-02-lfo-routing/):
the band-menu panel with all eight targets is 742 design px of the 780 the
menu may use, so it neither scrolls nor clips, at the default 990 x 645 and the
minimum 792 x 516 alike (the editor is pinned to its 1320 x 860 design box and
scaled uniformly). Hiding off targets' Depth rows was therefore not needed.
Three more targets (Intensity, Mix, Output: six rows) took it past the limit;
see above.
