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
| Morph | the A/B morph position, around the Morph slider | +/-0.5 of the morph range |
| Freeze | LIVE / FROZEN, gated by the LFO | frozen the whole cycle (Depth = frozen duty) |
| Length | the next freeze's loop length, around the user's LENGTH | +/-8 steps of the LENGTH list |
| Snapshot A / B | blends toward that captured snapshot and back (unipolar) | all the way there |

The order also has slots for Intensity, Mix and Output, which become targets
when the gain controls land; a build without them skips them.

Effective modulation of a target is **wave x that target's Depth**. There is no
LFO-level depth. A target that is switched off contributes nothing whatever its
Depth; its Depth row stays in place, dimmed and inert, so switching never moves
the rows. Depths default to 50 % (the LFO depth a fresh 1.0.x instance opened
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

Freeze and Length do not touch the field (below).

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

## Length

The user's LENGTH is the centre. At each freeze **engage** -- from the Freeze
target, the button, a key or automation -- the Length coordinate at that moment
(`wave x Depth`, summed over LFOs) picks the loop length:
`round(coordinate x 8)` steps through the LENGTH list (16 fractions of a bar,
then 1, 2, 4 and 8 bars), clamped to its ends (`modulated_length_index`). A
custom LENGTH is centred on the list entry nearest it. A loop that is already
playing is never resized. The LENGTH control keeps showing the user's value.

## Touching a modulated control

Modulation keeps running. A Freeze press holds until the gate's next change; a
LENGTH pick becomes the new centre. With **Ask before overriding modulation**
on (Settings > MODULATION, default on) Spectr asks first, in the preset
dialogs' style: "Freeze is being modulated by LFO 1. Turn off its Freeze
target?" -- **Keep modulating** applies the action and leaves the LFO running;
**Turn off** writes that LFO's target lane off (a recorded host gesture) and
then applies it. Return = Turn off, Escape = Keep modulating, and **Don't ask
again** turns the Setting off. The dialog is generic
(`window.spectrOverrideModulated(control, target, lfos, action)`), so a knob
that becomes a target later asks the same question in its own name.

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

Captured headless (`SPECTR_MODULATION_ROUTE_SHOTS=1 Spectr-native-shot
--backend=skia`), in [`evidence/2026-10-02-lfo-routing/`](evidence/2026-10-02-lfo-routing/):
the band-menu panel with all eight targets is 742 design px of the 780 the
menu may use, so it neither scrolls nor clips, at the default 990 x 645 and the
minimum 792 x 516 alike (the editor is pinned to its 1320 x 860 design box and
scaled uniformly). Hiding off targets' Depth rows was therefore not needed.
Three more targets (Intensity, Mix, Output: six rows) will take it past the
limit; the runtime cannot scroll an overflow container, so that merge needs
the menu to scroll by hand the way the help guide does.
