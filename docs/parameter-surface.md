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
| `3...999` | Reserved global controls |
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
| `4003` | Internal LFO depth |
| `4004` | Internal LFO target (whole bank/snapshot A/snapshot B/morph) |
| `4005...4009` | Reserved modulation growth |
| `4010` | Internal LFO 2 enabled |
| `4011` | Internal LFO 2 shape (sine/triangle/square/saw) |
| `4012` | Internal LFO 2 rate (beats per cycle) |
| `4013` | Internal LFO 2 depth |
| `4014...4199` | Reserved modulation growth |
| `4200...4203` | Macro 1...4 |
| `4204...4299` | Reserved macro growth |

The gain and mute names are zero-padded (`Band 01 Gain` through
`Band 64 Gain`) so hosts that flatten groups still sort them correctly.

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
