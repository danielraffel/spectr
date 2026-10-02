# Internal modulation: routing, stacking and the viewport

Spectr has two tempo-synced LFOs. Each LFO drives **any set of destinations at
once**, and each destination has its own **Amount**. This page is the contract
for how routes combine, how the two viewport destinations behave, and how the
routing lanes relate to the single-target lane earlier versions shipped.
Parameter IDs are in [parameter-surface.md](parameter-surface.md); recording
and playback are in [automation.md](automation.md).

## Destinations

| Destination | What it moves | Excursion at Depth 100 %, Amount 100 % |
| --- | --- | --- |
| Bank | every band's level, together | +/-12 dB |
| Snapshot A | blends the field toward captured snapshot A and back (unipolar) | 0 ... 100 % of the way |
| Snapshot B | the same toward snapshot B | 0 ... 100 % |
| Morph | the A/B morph position, around the Morph slider | +/-0.5 of the morph range |
| Viewport position | slides the window across the spectrum, width kept | +/-1 decade (about 3.3 octaves) |
| Viewport zoom | widens / narrows the window about its centre | width x2 / x0.5 (log-frequency) |

Effective modulation of a destination = LFO **Depth x Amount**. Depth is one
per LFO; Amount is one per LFO per destination (default 100 %). A route that
is switched off contributes nothing whatever its Amount.

## How stacked routes combine

The combination is deterministic, independent of which LFO is evaluated first,
and level-safe. It is `compose_internal_modulation` in
`include/spectr/modulation.hpp` -- one pure function that the audio owner
renders and the editor draws.

1. **Sum per destination.** Every enabled (LFO, destination) route adds
   `wave x Depth x Amount` to that destination's coordinate. The snapshot
   destinations take the unipolar wave `(wave + 1) / 2`. Two LFOs on the same
   destination therefore add; opposite phases cancel exactly.
2. **Apply each destination once, in a fixed order, clamping once.**
   - Morph: the morph position moves by the coordinate x 0.5 (clamped to
     0...1), and the field follows by the *difference* that move makes. It is
     the identity at coordinate 0 and keeps whatever else is in the field
     (macros, band edits made after a morph).
   - Snapshot A, then Snapshot B: a blend from the current field toward the
     captured shape (coordinate clamped to 0...1).
   - Bank: the summed offset x 12 dB is added to every band and the result is
     clamped into the band range once.
   - Viewport zoom, then viewport position, on the window (see below).

   Morph and the snapshots *reshape* (every band stays between the values it
   is blended from); Bank *offsets* that shape. So Bank + Morph is a morphing
   shape that also breathes -- before this change Morph replaced the field and
   a Bank selected with it was silently lost.

Every destination is the identity at coordinate 0, which is what lets a route
fade in and out continuously (below).

Authored mutes always survive: a muted band is excluded from modulation and no
destination toggles a mute (`preserve_authored_mutes`).

### Changes against 1.0.6

With one LFO on one destination the sound is unchanged. Differences only
appear where 1.0.6 composed sequentially:

- Bank together with Morph (or Snapshot A/B together with Morph): 1.0.6 let
  Morph overwrite the others; now all apply.
- Both LFOs on Morph: 1.0.6 discarded LFO 1's morph; now the two sum.
- Both LFOs on Bank near the band limits: 1.0.6 clamped after each LFO; now the
  sum is clamped once.
- The Morph destination with no derived morph (the Morph slider never moved):
  1.0.6 replaced the drawn field with the morph of A and B; now it moves the
  field by the change in the morph, around the field as drawn.

## Smoothness

Every routing lane is something a host can automate at any sample, so none of
them may step the sound:

- A route's audible level (`enabled ? Amount : 0`) is slewed like the LFO's
  own level (`slew_lfo_level`): full scale per 60 ms for the four field
  destinations, per 250 ms for the two viewport destinations
  (`kViewportRouteSlewSeconds`). Switching a destination on at an LFO crest,
  switching it off at a trough, or jumping its Amount fades the contribution
  instead of stepping it. The viewport ramp is longer because fading a
  viewport route moves the whole bank by up to a decade; over 60 ms that is
  several times faster than the fastest free-running sweep (measured through
  the AU host as a 1.7-2.2 dB/ms envelope step on a tone the bank passes over,
  against 0.3-0.4 dB/ms for the running LFO).
- A viewport destination restages the filter bank every audio block; the mask
  renderer's swap crossfade spans the gap between restages
  (`kIrCrossfadeSamples` in `src/mask_renderer.cpp`), the same path a user
  zoom or pan drag takes.

Measured (`test/test_modulation_routing.cpp`, 512-sample blocks at 48 kHz, sine
LFO at 1 beat, -12 dB bank; gate = 15 % of the 24 dB swing):

| Control | Max step, route ramp (shipping) | Max step, route-step plant (fail-before) |
| --- | --- | --- |
| free-running LFO (yardstick) | 1.61 dB/block | 1.61 dB/block |
| Bank switched on at the crest | 2.13 dB/block | 11.99 dB/block |
| Bank switched off at the trough | 2.25 dB/block | 11.86 dB/block |
| Amount 10 % -> 100 % at the crest | 2.14 dB/block | 10.80 dB/block |
| Amount ramp 0 -> 100 % over 2 s, toggled off/on mid-render | 2.20 dB/block | 9.11 dB/block |
| Viewport position switched on at the crest (decades/block, gate 0.30; free-running 0.134) | 0.072 | 0.999 |

`Spectr-route-smoothness-negative-control` re-runs those tests with
`SPECTR_MODULATION_PLANT=route-step` and must fail.

### Through a real host

`Spectr-au-routes-host` (`tools/au_routes_probe.cpp`) loads the built
`.component` in-process (offline: no install, no device, no window) and plays
the routing lanes the way a DAW does -- `AudioUnitScheduleParameters`, an
Amount ramp written as one event per 128-frame render call, switches at LFO
crests and troughs -- through a steady 2 kHz tone. Each edge is scored against
a reference render with the same destination running steadily. Three runs:

| Edge | whitened spike, dB (reference) | largest 1 ms envelope step, dB (reference) | costliest call near it |
| --- | --- | --- | --- |
| Bank off at crest | 10.3-10.5 (10.2-10.5) | 0.42-0.47 (0.45-1.41) | 244-389 us |
| Viewport position on at crest | 8.1-11.4 (7.7-8.8) | 0.68-1.78 (1.69-2.28) | 216-253 us |
| Viewport position off at trough | 7.7-9.1 (8.6-8.7) | 0.67-1.02 (1.69-2.28) | 250-292 us |
| Viewport zoom on at crest | 9.8-11.1 (7.3-11.3) | 0.14-0.16 (0.26-0.50) | 159-283 us |
| Zoom Amount 100 % -> 20 % | 11.4-11.5 (10.7-11.3) | 0.07-0.10 (0.26-0.50) | 251-312 us |
| Viewport zoom off at trough | 11.2-11.4 (11.2-11.7) | 0.08 (0.26-0.50) | 186-302 us |
| Bank on at crest | 9.9-11.1 (10.2-11.0) | 0.59-0.76 (0.45-1.41) | 125-321 us |
| Amount ramp 0 -> 100 % over 2 s | -- | 0.41-0.80 (0.45-1.41) | -- |

Budget per call 2 667 us; reference p99 272-370 us. No edge reads as a click
(gate: reference + 6 dB) or moves faster than its reference + 0.5 dB/ms.

What it cannot see: the mask renderer spreads every swap over a crossfade of up
to 18 ms, so even an un-ramped switch reaches the audio at about 1 dB/ms --
inside the running LFO's own range. The probe still passes with
`SPECTR_MODULATION_PLANT=route-step`, so it is a host-path gate (lanes arrive
sample-accurately through a real AU, no click, no slow call), not a detector
for a lost route ramp; the ramp is proven at the field level above. A REAPER
pass was not run on this machine (no focus-stealing GUI permitted here).

## The viewport destinations and the editor

In Spectr the viewport is a DSP input: it sets the frequency span the bands
cover. The viewport destinations modulate the **audible** window around the
window the user set (after any morph derivation). The stored viewport lanes
(`3001`, `3002`) and their automation are never written by the LFO.

**The editable plot stays where the user put it.** The bands are drawn, and
every pointer hit-test runs, in the user's window; the audible window is drawn
as an overlay:

- on the minimap, a dashed amber bracket around the audible window, beside the
  user's solid window;
- on the plot, a thin amber bracket across the top spanning the part of the
  audible window that falls inside the view, with a faint tint, and an arrow at
  an edge the audible window runs past.

The alternative -- pausing viewport modulation while a band is dragged -- was
rejected: it changes what is *heard* the moment the user touches a band (the
sweep stops, the filter bank jumps back to the base window), which is the
opposite of a non-destructive overlay, and on release it would jump again. The
overlay keeps the sound continuous and the pointer honest. It is paint-only:
the modulation frame writes a ref and wakes the draw loop; nothing re-renders
React per frame.

## The legacy single-target lane (4004)

`4004` (LFO Target: Whole Bank / Snapshot A / Snapshot B / Morph) and the
Settings "Destinations" selection both predate routing and were shared by both
LFOs. They are kept, never renumbered, and behave as follows:

- **Old sessions.** A session written without the `lfo_routing` marker maps
  its single selection (the Destinations mask when one was saved, else the
  `4004` value) to "that one on" for **both** LFOs, Amount 100 %, viewport
  routes off -- it sounds as it did.
- **Going forward `4004` is a command lane.** A change of its value (host
  automation written before routing existed, or a host edit) selects that one
  field destination for both LFOs, leaving the viewport routes and every Amount
  as they are, and writes the routing lanes so the editor and host agree. The
  audio owner applies it at the event's own block.
- **It is not written back.** Switching the new toggles does not move `4004`:
  a one-hot enum cannot represent several destinations, and a processor-side
  write would itself be recorded by a host in Write/Latch and would replay as a
  command that collapses the selection. Its displayed value is therefore the
  last command given, not necessarily what is routed.
- Settings "Target" and "Destinations" remain as both-LFO shortcuts onto the
  same routing lanes (each changed lane is written as its own host gesture).

`modulation_target_mask` keeps being saved (LFO 1's field destinations) so an
older Spectr opening a new session plays the nearest thing it can express.

## Cost

Audio-thread cost per 512-sample callback (Tracking renderer, M-series laptop,
median / p99, 3 x 280 blocks; budget 10 667 us):

| LFO 1 on | median | p99 |
| --- | --- | --- |
| Bank | 449 us | 944 us |
| Viewport position | 460 us | 1 200 us |
| Viewport zoom | 469 us | 1 033 us |

Both kinds restage the mask every block, so they cost the same order; the gate
in `test_modulation_routing.cpp` holds the viewport median within 1.5x of Bank
and its p99 under half the budget. The redesign itself runs on the renderer's
worker, as for every other mask change, and the audio path stays
allocation-free (the route levels are a fixed array; `compose_internal_modulation`
works on stack values).

## The menu, measured

Captured headless from the shipping editor
(`SPECTR_MODULATION_ROUTE_SHOTS=1 Spectr-native-shot --backend=skia`), in
[`evidence/2026-10-02-lfo-routing/`](evidence/2026-10-02-lfo-routing/):

- With every destination on, the Modulation panel is 655 design px tall
  (18 rows; each Amount row 29 px, the same as Rate and Depth) against the
  780 px the menu may use (860 - 64 - 16), so it never needs to scroll and
  never clips -- which matters, because the runtime cannot scroll an overflow
  container. The editor is pinned to its 1320 x 860 design box and scaled
  uniformly, so the panel is the same at the default 990 x 645 and the minimum
  792 x 516 host sizes (the two captures differ only in the live spectrum).
  The fallback of hiding the Amount rows of switched-off destinations was
  therefore not needed: Amount rows always stay in place, dimmed while off.
- `viewport-overlay.png`: a full-depth viewport-position LFO, the plot on the
  user's 200 Hz - 2 kHz window, the audible window bracketed on the minimap and
  across the top of the plot.

Editor frame cost (`SPECTR_ROUTE_FRAME_COST=1 Spectr-native-shot`, 64 bands,
one 800-sample audio block then one display tick, 400 frames; the delivery
control confirms 460 of 460 modulation frames reached the document and the
viewport runs carried 58-59 distinct audible windows): LFO on Bank p50 0.057 /
p95 0.305 ms, on Viewport position 0.077 / 0.332 ms, on both 0.075 / 0.352 ms,
Bank again 0.079 / 0.362 ms. No p95 regression beyond the run-to-run spread of
the Bank baseline. This times the editor's own work (publication, the
paint-only overlay apply, the draw loop); the raster of the two extra strokes
the overlay adds happens in the compositor and is not in this number.
