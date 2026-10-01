# Host automation coverage

Spectr is meant to be played, so every control a performance reaches for has to
do three things in a DAW:

1. **Be a host parameter**, so it has a lane at all (`define_parameters`; see
   [parameter-surface.md](parameter-surface.md) for the IDs).
2. **Record**: an edit in the editor reaches the host inside an edit gesture
   (begin, value, end). Hosts recording in Touch, Latch or Write key on that
   bracket; a bare value change moves the lane but leaves them nothing to
   record. A drag must be **one** bracket for the whole drag. If each move is
   its own bracket, a host in Touch sees the control released between every
   two moves and snaps back to the existing lane in each gap.
3. **Play back**: host automation moves the DSP (sample-accurately, through the
   audio owner's parameter cursor) and the editor's control follows it (the
   compact live projection, `processing_state_live`).

## Coverage

| Control | Host parameter | Records (editor → host) | Plays back: DSP | Plays back: editor |
| --- | --- | --- | --- | --- |
| LIVE / FROZEN toggle, Q, freeze chord | `3` Freeze (toggle) | One bracket per press (`freeze_set`) | Per sub-block | Toggle face follows |
| Band gain (paint, Level/Boost/Flare/Glide, group drags) | `1000-1063` | One bracket per touched band per drag (plot drag epoch) | Sample-accurate | Bars follow |
| Band mute (click, menu, selection) | `2000-2063` (toggle) | One bracket per change | Sample-accurate | Mute badge follows |
| Band count | `3003` (stepped) | One bracket per change | Yes | Yes |
| Viewport pan / zoom (minimap, wheel, zoom keys) | `3001` center, `3002` width | Minimap drag: one bracket per drag. Wheel and keys: one bracket per step | Yes | Yes |
| A/B Morph slider | `3000` | One bracket per drag (drag epoch) | Re-derives bands (and viewport, if enabled) | Slider and the bands it derives follow |
| Snapshot capture / recall / clear | none (editor state) | Recall writes the band lanes it changes, gestured | Via band lanes | Yes |
| Motion / Analyzer / Edit / Visualization modes | `3100-3103` (enum) | One bracket per change (`mode_set`) | Editor-only effect | Yes |
| **LFO 1 on/off** | `4000` (toggle) | One bracket per press (`param_edit`) | 60 ms level ramp | Switch follows |
| **LFO 1 shape** (Sin / Tri / Square / Saw) | `4001` (enum, labelled) | One bracket per pick | 150 ms crossfade | Shape row follows |
| **LFO 1 rate** | `4002`, 0.25-16 beats, shown "4 beats" | One bracket per drag | Phase-continuous | Rate row follows |
| **LFO 1 depth** | `4003`, 0-1, shown "50%" | One bracket per drag | 60 ms level ramp | Depth row follows, including after a hand edit |
| **Shared target** (Bank / Snapshot A / Snapshot B / Morph) | `4004` (enum) | One bracket per pick | Yes; takes authority back from a Destinations selection | Yes |
| **LFO 2 on/off, shape, rate, depth** | `4010-4013` | As for LFO 1 | As for LFO 1 | As for LFO 1 |
| Destinations multi-select (Settings) | none (editor state, `modulation_targets_set`) | Not automatable | n/a | n/a |
| "Edit LFO 1 / 2" source switch | none (view state) | n/a | n/a | n/a |
| Macro 1-4 value | `4200-4203` | One bracket per drag (`macro_drag_*`) | Yes | Yes |
| Macro membership | none (supplemental state) | Not automatable, by design | n/a | n/a |
| Output trim | `2` (dB) | One bracket per change; one per drag where the runtime delivers the input's pointer events | Sample-accurate, smoothed | Readout follows |
| Mix | `1` (%) | No editor control | Yes | n/a |
| Freeze hold length, latency mode, morph-moves-viewport, keyboard shortcuts in DAW, appearance | none (Settings, plugin state) | Not automatable, by design | n/a | n/a |

## The modulation lanes as an instrument

An LFO edit is a performance gesture, so the DSP treats every lane as something
that can change at any sample:

- **On/off and depth** drive one slewed level per LFO (`slew_lfo_level`,
  full scale per 60 ms). The waveform fades in where it already is rather than
  restarting. Without the ramp, an LFO switched on at its crest moved every
  band by 12 dB in one block. The editor draws the same ramp, because the
  slewed level is what the audio owner publishes.
- **Shape** crossfades the old waveform into the new one at the same phase
  over 150 ms (`LfoShapeFade`).
- **Rate** is a phase accumulator, so a new rate changes how fast the phase
  moves and never where it is.

## Editor protocol (for anyone adding a control)

A control whose parameter is its whole state (Mix, Output trim, the LFO lanes)
writes through the bridge, never through a bare `param_set`:

- A press that is one complete act (a toggle, a shape, a target, a keyboard
  step) posts `param_edit {id, value}`, which the processor applies as a
  complete begin/value/end bracket.
- A drag posts `param_gesture_begin {id}` on press, `param_edit` for each value,
  and `param_gesture_end {id}` on release. The editor helper
  `globalThis.spectrParamGesture(id, open)` closes a bracket once, however many
  release events the pointer fires.
- A drag on something the processor writes as a derived value (Morph, or the
  bands and viewport a plot drag republishes) posts `param_drag_start` /
  `param_drag_end` (`globalThis.spectrParamDrag(open)`). That opens the
  processor's gesture epoch: each lane the drag writes opens once and closes
  on release.

`param_edit` refuses parameters that own a different route (bands, morph,
viewport, modes, Freeze, macros), so a new control cannot bypass the
applied-value cache by accident. An editor that goes away mid-drag closes every
bracket it opened.

## Tests

- `test_native_state_parity.cpp`: the real editor document, driven headless.
  "every LFO edit in the band menu / in Settings records as a host gesture",
  "host playback of the LFO lanes moves the band menu, even after an edit",
  "an Output trim or Morph edit records as a host gesture", "a band paint drag
  is one host gesture per band it touches", "host playback of Morph moves the
  Morph slider", and "a freeze press goes to the host as one edit gesture".
- `test_mode_bridge.cpp`: the bridge protocol (`param_edit`, drag brackets,
  refusals) with a host-side recorder.
- `test_spectr.cpp`: the LFO level ramp in the modulated field and in the
  audio, for both renderers.
- `test_param_surface.cpp`: display strings, steps and labels of the LFO
  lanes.
