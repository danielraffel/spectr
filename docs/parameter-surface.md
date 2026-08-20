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
| `4000...4199` | Reserved LFO and modulation controls |

The gain and mute names are zero-padded (`Band 01 Gain` through
`Band 64 Gain`) so hosts that flatten groups still sort them correctly.

## Groups

The StateStore schema assigns parameters to Global, Band Gain, Band Mute,
Snapshots, Viewport, and Modes groups. Format adapters should project those
groups through their native host grouping mechanism where the format supports
one; the stable names remain the fallback presentation.

## Viewport encoding

The viewport is encoded as center plus width in the same log-frequency domain
used by the display. This makes pan and zoom independent automation lanes and
prevents independently automated edges from crossing. Decoding clamps the
window to 20 Hz...20 kHz and enforces a one-octave minimum width.
