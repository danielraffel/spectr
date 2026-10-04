# Host parameter surface

Spectr registers a static host parameter list. Hosts cache parameter identities,
so an ID is permanent after release and the list does not change when the visible
band count changes. Band slots above the current 32, 40, 48, 56, or 64-band
layout remain stored but do not enter the active spectral mask.

## ID allocation

| Range | Purpose |
| --- | --- |
| `1` | Mix |
| `2` | Output trim |
| `3` | Freeze (Live/Frozen) |
| `4` | Freeze Length (1 bar/2 bars/4 bars/8 bars/Custom) |
| `5...999` | Reserved global controls |
| `1000...1063` | Band 01...64 gain |
| `1064...1999` | Reserved band-gain growth |
| `2000...2063` | Band 01...64 mute |
| `2064...2999` | Reserved band-mute growth |
| `3000` | A/B snapshot morph |
| `3001` | Viewport center in log10 Hz |
| `3002` | Viewport width in log10 decades |
| `3003` | Visible band count |
| `3004...3099` | Reserved viewport and snapshot controls |
| `3100` | Motion mode (Live/Precision) |
| `3101` | Analyzer mode |
| `3102` | Edit mode |
| `3103` | Visualization mode |
| `3104...3999` | Reserved mode and global growth |
| `4000` | Internal LFO enabled |
| `4001` | Internal LFO shape (sine/triangle/square/saw) |
| `4002` | Internal LFO rate (beats per cycle) |
| `4003` | Internal LFO depth (legacy command lane: sets every enabled target's Depth) |
| `4004` | Internal LFO target (whole bank/snapshot A/snapshot B/morph): legacy command lane, see [modulation.md](modulation.md) |
| `4005...4009` | Reserved modulation growth |
| `4010` | Internal LFO 2 enabled |
| `4011` | Internal LFO 2 shape (sine/triangle/square/saw) |
| `4012` | Internal LFO 2 rate (beats per cycle) |
| `4013` | Internal LFO 2 depth (legacy command lane, as `4003`) |
| `4014...4019` | Reserved modulation growth |
| `4020...4027` | LFO 1 targets on/off: Bank, Snapshot A, Snapshot B, Morph, Band shift, Band spread, Freeze, Length |
| `4028...4029` | Reserved LFO 1 target growth |
| `4030...4037` | LFO 1 target Depths (0-1, shown "50%"), same order |
| `4038...4039` | Reserved LFO 1 target growth |
| `4040...4047` | LFO 2 targets on/off, same order |
| `4048...4049` | Reserved LFO 2 target growth |
| `4050...4057` | LFO 2 target Depths, same order |
| `4058...4059` | Reserved modulation growth |
| `4060...4062` | LFO 1 level targets on/off: Intensity, Mix, Output |
| `4063...4069` | Reserved LFO 1 level-target growth |
| `4070...4072` | LFO 1 level target Depths, same order |
| `4073...4079` | Reserved LFO 1 level-target growth |
| `4080...4082` | LFO 2 level targets on/off, same order |
| `4083...4089` | Reserved LFO 2 level-target growth |
| `4090...4092` | LFO 2 level target Depths, same order |
| `4093...4099` | Reserved modulation growth |
| `4100...4101` | LFO 1 targets on/off: Bands, Preset |
| `4102...4109` | Reserved |
| `4110...4111` | LFO 1 Bands / Preset Depths |
| `4112...4119` | Reserved |
| `4120...4121` | LFO 2 targets on/off: Bands, Preset |
| `4122...4129` | Reserved |
| `4130...4131` | LFO 2 Bands / Preset Depths |
| `4132...4139` | Reserved |
| `4140` | Freeze Hold for Length, Off/On (default Off) |
| `4141...4199` | Reserved modulation growth |
| `4200...4203` | Macro 1...4 |
| `4204...4299` | Reserved macro growth |
| `5000` | Intensity, 0...100 % (scales the composed shape toward flat) |
| `5001` | Auto Gain, Off/On |
| `5002...5009` | Reserved level controls |

The gain and mute names are zero-padded (`Band 01 Gain` through
`Band 64 Gain`) so hosts that flatten groups still sort them correctly.

The level targets (ModulationTarget 8...10) came after the first block had
shipped, so they take a block of their own rather than its two-ID headroom:
for LFO `l` (0 or 1) and target `t`, on/off is `4020 + 20 l + t` for
`t < 8` and `4060 + 20 l + (t - 8)` from Intensity on, and a target's Depth
is always its on/off ID + 10 (`lfo_route_enabled_param_id` /
`lfo_route_amount_param_id` in `param_surface.hpp`). Host names follow the
first block: `LFO 1 Intensity`, `LFO 1 Intensity Depth`, ..., `LFO 2 Output
Depth`.

Bands and Preset (ModulationTarget 11, 12) take a third block, clear of
every ID the first two use: on/off `4100 + 20 l + (t - 11)`, Depth +10
(`LFO 1 Bands`, `LFO 1 Preset Depth`, ..., `LFO 2 Preset Depth`). Freeze
**Hold for Length** is `4140`, a toggle: on, each engage the Freeze target
makes latches for exactly the effective Length (see
[modulation.md](modulation.md#hold-for-length)).

## Display and recording

LFO rate reads in a host's lane as beats ("4 beats", "1 beat"), and depth as a
percentage ("50%"); typed values accept the same forms. LFO on/off, shape and
target are discrete, labelled lanes, as are the per-LFO route switches
("LFO 1 Band spread"); target Depths read as percentages ("LFO 2 Morph
Depth"). Defaults reproduce a fresh 1.0.x instance: both LFOs on Bank at 50 %,
every other target off, every Depth 50 %. The ID order (Bank, Snapshot A,
Snapshot B, Morph, Band shift, Band spread, Freeze, Length) is the order the
lanes were added; the editor lists them most-modulated first. How each control records an edit gesture
and follows playback is in [automation.md](automation.md).

## Freeze

`3` holds the input spectrum. It is a boolean, automatable like any other
lane: freezing changes neither the reported latency nor the topology. The
capture is taken ahead of the mask, so bands, mutes, morph, macros and both
LFOs keep acting on the held sound, and the dry leg of Mix stays live. While a
freeze is requested or its hold is still audible, Spectr reports an infinite
tail.

## Freeze Length

`4` is how much input the next freeze takes in, as a musical length: an enum
of the header dropdown's lengths -- the sixteen fractions of a bar alone
(1/32 ... 15/16), then 1, 2, 4 and 8 bars, ascending -- plus `Custom`, which
selects the custom length the editor's Custom length… popover last committed.
A length is exact: whole bars `0...128` plus one bar fraction from a fixed set
(`include/spectr/freeze_length.hpp` is the only definition). The custom length
persists in the supplemental plugin-state blob as
`freeze_length: {bars: <int>, fraction: "<n/d>"}`, never as a float, so
`1 1/12` round-trips exactly.

Why an enum and not every length: there are 129 x 17 - 1 valid lengths, and a
lane of 2,192 steps is unusable to draw automation on. The common lengths are
one step apart, and anything else is still reachable by automating to Custom.
The enum may grow only by appending before `Custom` would move, so it is
frozen at five values; a new common length would need a new parameter.

Seconds come from the host transport: bars x quarter notes per bar
(`numerator x 4 / denominator`) x 60 / tempo. With no transport (the
standalone, or a host that reports none) it is 120 BPM 4/4. A tempo or meter
change applies to the next freeze; a hold that is playing keeps the loop it
took. Below a quarter of a second the hold is spectral (a steady tone), from
there up it loops the audio, exactly that many samples per pass. Loops are
capped at 60 s (lower where 256 MB of loop memory would not cover 60 s at the
instance's channel count and rate); the editor says so only when the cap
bites.

A session written before Freeze Length stored a seconds value,
`freeze_hold_seconds`. Untouched (the old default) it opens at 1 bar;
otherwise at the musical length nearest those seconds at the transport the
instance has seen (120 BPM 4/4 if none, as a session usually loads before
playback), selecting its common length or Custom.

The held sound itself is not saved: a session that stored Freeze on re-arms
and holds the first stretch of input it plays.

## Macros

A macro is one host-automatable offset addressed to a user-chosen subset of the
64 canonical band slots, so a scattered group of bands can be driven from a
single lane a host modulator can reach.

- Range is the full band range, ±24 dB, default 0. A macro is an OFFSET, so
  +24 dB only reaches the ceiling for a member already at 0 dB.
- A macro ADDS to each member's gain, shape-preserving: the contour drawn
  inside the group survives. Offsets from overlapping macros SUM, and the
  result is clamped ONCE into the band range, so it never depends on the order
  the macros are visited.
- A muted member receives no offset, and a macro never toggles a mute.
- Macros compose BEFORE the internal LFOs, so an LFO wobbles around the
  macro-driven level rather than around the drawn one.
- A macro is never written back to its members' gain lanes. Like the LFOs it is
  a non-destructive overlay, and the drawn curve stays editable underneath.

Membership is NOT a parameter. A set of slots is not a value a host can
automate, and exposing one lane per member would reproduce the 64-lane echo
macros exist to remove. It is editor state, persisted in the supplemental
plugin-state blob as an optional `macro_members` array of index arrays. Absence
means no macros are assigned, which is what a writer predating the feature
meant, so the member was added without a schema-version bump.

Membership survives a band-count change: the layout is a projection onto the
first N canonical slots, so a member above the visible count is inert rather
than forgotten, and returns when the count rises again.

## Groups

The StateStore schema assigns parameters to Global, Band Gain, Band Mute,
Snapshots, Viewport, Modes, Modulation, and Macros groups. Format adapters
project those groups through their native host grouping mechanism where the
format supports one; the AU v2 adapter maps them to parameter clumps. The
stable names remain the fallback presentation.

## Viewport encoding

The viewport is encoded as center plus width in the same log-frequency domain
used by the display. This makes pan and zoom independent automation lanes and
prevents independently automated edges from crossing. Decoding clamps the
window to 20 Hz...20 kHz and enforces a one-octave minimum width.

## Level controls (5000...5009)

Intensity and Auto Gain are appended in their own reserved block so they never
collide with modulation growth (`4005...4199`). Like Mix and Output they are not
in the surface slot cache: the audio owner reads them from the store or the
block's parameter cursor. See `include/spectr/level_controls.hpp`.

- **Intensity** (`5000`, default 100 %): `effective_db = intensity x composed_db`,
  applied once after morph, macros and LFOs; a muted band's linear gain becomes
  `1 - intensity`. Slewed at 200 ms full scale. 100 % is an exact identity.
- **Auto Gain** (`5001`): a post gain before Output trim,
  `-10 log10(sum P(f) |H(f)|^2 / sum P(f))`, where `H` is the response the
  active renderer realises for the effective (pre-LFO) shape, blended with Mix,
  and `P` is the long-term K-weighted spectrum of the material the mask shapes
  (the live input, or the held sound while Freeze holds). Clamped to +-24 dB;
  300 ms ramp; exactly 1.0 when off. This is Auto Gain v2; v1 (the same ratio
  weighted by a fixed K-weighted pink reference, clamped to -24...+12 dB) stays
  in the code for comparison only. Design, tuning and measurements:
  [level-controls.md](level-controls.md). New instances default **Off**
  (`kAutoGainDefaultForNewInstances`); a session saved without the
  `level_controls` marker opens with it Off too; a session that saved it On
  keeps it On -- and now runs v2, so its make-up follows the material rather
  than the pink reference.
- **Range** is not a parameter: it is editor state (`editor_range_db` in the
  supplemental blob, default 24) and never changes the sound.
- **Show tooltips** (Settings > FEEDBACK) is not a parameter either: editor
  state (`show_tooltips` in the supplemental blob, default on), saved with the
  session like Keyboard shortcuts in DAW, so each project keeps its own
  choice and a new instance shows them. Per session rather than a global app
  preference because a plug-in has no other storage it can count on in every
  host, and a project-level choice survives being opened on another machine.
- **Ask before overriding modulation** (Settings > MODULATION and the header
  context menus) is the same kind of editor state: `ask_before_override` in
  the supplemental blob, default on, absent on an older session means on.
  "Don't ask again" in the override dialog turns it off and that is saved too.
- The **Preset** target's neighbourhood (the names and band gains of the
  presets around the current one) rides the supplemental blob as
  `preset_modulation`, so the target keeps playing when a session reopens.
