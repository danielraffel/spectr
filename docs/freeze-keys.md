# Freeze Keys (prototype)

While Freeze holds a sound, MIDI notes play that sound chromatically up and
down the keyboard. Unfrozen, MIDI does nothing and audio passes through exactly
as it does without the feature.

Freeze Keys is built only as a development identity. The shipping identity
(Spectr, `com.pulp.spectr`, AU `aufx Spec Pulp`) does not read MIDI and its
identifiers and AU type are unchanged. `test/identity_check.cmake` pins this.

## Building it

```sh
cmake -S . -B build-keys -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR=<pulp-sdk>/lib/cmake/Pulp -DSPECTR_DEV_IDENTITY=Keys
cmake --build build-keys --target SpectrKeysDev_AU SpectrKeysDev_CLAP \
  SpectrKeysDev_VST3 SpectrKeysDev_Standalone
```

`-DSPECTR_DEV_IDENTITY=Keys` produces **Spectr Keys Dev**:
`com.pulp.spectr.keys-dev`, AU `aumf SpKz Pulp`, and a VST3 class id derived
from the bundle id. Its descriptor declares MIDI input (`accepts_midi`), CMake
passes `ACCEPTS_MIDI`, and the AU entry uses `PULP_AU_MIDI_PLUGIN`. All three are
required for an `aumf` to receive notes. `SPECTR_FREEZE_KEYS=ON|OFF` overrides the
default for any dev suffix. Configure refuses it on the shipping and preview
identities.

## Behaviour

- **Root key.** MIDI 60 (C3 in Logic's naming) plays the held sound at its own
  pitch. Every other key transposes it by its distance from the root in
  equal-tempered semitones. Keys more than 36 semitones from the root are
  ignored. The root can be changed with `Spectr::set_freeze_keys_root_note`;
  there is no Settings UI or saved state for it yet.
- **Freeze with no keys played** behaves exactly as before: the hold plays.
  The tests check this against the same render with Freeze Keys turned off.
- **The first key played into a hold** switches the frozen bus to keys mode.
  The hold fades out under the first note's attack (linearly when that note is
  the root of a loop, because it continues the same audio; equal-power
  otherwise). From then on only held notes sound, and with no key held the
  frozen bus is silent until Freeze is released.
- **Releasing Freeze** releases every voice and turns MIDI off. Live input
  returns on the source's own release fade. Once the voices' release and the
  mask's tail have passed, the output matches the never-frozen render.
- **A key pressed while Freeze is still arming** (waiting for signal or
  building its pre-roll) starts as soon as the hold becomes audible.
- **Voices.** Up to 8 notes sound at once; when a ninth arrives, the oldest
  held note releases to make room. There are 12 voice slots, so release tails
  have room to finish. Velocity sets level as `(velocity/127)^2`, so velocity
  127 plays at the hold's level. Attack is 10 ms and release is 80 ms. Notes
  are not normalised for polyphony, just as on a sampler: an 8-note chord is
  about +9 dB louder than the hold.
- **The mask, LFO and Mix** act on the voices exactly as they act on the hold,
  because the voices replace the hold ahead of the mask. Below 100% Mix the dry
  leg stays live.
- MIDI CC 120 and CC 123 release all notes. Pitch bend and sustain (CC 64) are
  not handled yet.

## How the pitch is shifted

`include/spectr/freeze_keys.hpp` is a wet-source stage that wraps
`FreezeSource`. It runs the source first and leaves it untouched, apart from
two read-only accessors that were added (`loop_position()` and `loop_sample()`).
After that, and only once a key has been played into a hold, it replaces the
held sound with the sum of the voices.

**Spectral hold** (Hold length below 0.25 s). The voice resynthesises the
held spectrum with every frequency multiplied by the note's ratio.

- Tonal peaks are found with the source's own lobe-locking prominence test.
  Each one becomes a partial and is redrawn at the scaled frequency, using the
  exact Hann main lobe of a sinusoid at that fractional bin. The pitch is
  therefore exact and not snapped to the 5.9 Hz bin grid. Measured error is
  under 0.01 cent for a 220 Hz tone transposed by ±12 semitones.
- Every other bin is a noise bin. Its magnitude is read from the held
  spectrum at bin `j / ratio`, which stretches the spectral envelope, and is
  scaled by `1/sqrt(ratio)` so power is kept the way varispeed keeps it. It
  plays at a random phase that runs at the scaled frequency with a small random
  walk, so a long note does not repeat.
- Each voice is its own overlap-add stream: 8192 points every 2048 samples,
  4× overlap. Because of this, a note starts and stops on a sample-accurate
  envelope.
- A voice's frame plan and pre-roll are built over the 512 samples after its
  note-on, a share per sample. That gives the spectral voices a 10.7 ms note
  latency at 48 kHz. Successive voices start at different points in their hop,
  so a chord never draws all its frames in one callback.

Built all at once, an 8-note chord cost 2.9 ms in the single 128-frame callback
it landed in, which is more than the 2.67 ms real-time budget. Spread out, the
worst callback in the AU probe costs about 1.0 ms. See "Measured" below.

**Loop hold** (Hold length 0.25 s or more). The voice plays the loop with
varispeed, like a sampler. It uses 4-point Hermite interpolation, crossfades
the seam the way the source does, and starts from wherever the loop is playing
at the note-on, so the root key continues the frozen loop exactly. Pitch and
loop duration change together. There is no note latency.

- Upward transposition applies no anti-alias filter. Content above
  `fs / (2 * ratio)` folds back. This has not been measured. If it is audible
  on bright material, the fix is a band-limited (polyphase) resampler.
- The alternative is a time- and formant-preserving pitch shift, so the loop
  keeps its length at any pitch, for example the stretch tools in Pulp (`pulp::signal`
  phase vocoder / `RealtimePitchTimeProcessor`). That costs a pitch shifter per
  voice and adds latency, and it changes the character from "sampler" to
  "harmoniser". Varispeed is the simpler choice and it sounds right for a
  frozen loop played from a keyboard.

## Real-time

`prepare()` allocates every buffer, including 12 voice slots of OLA,
spectrum plans and partial tables (about 4 MB in stereo). The audio thread does
not allocate, lock or read a clock. `tools/ci/check_render_path_clock.py` scans
`freeze_keys.hpp` as ctest `Spectr-render-path-clock-freeze-keys`. Notes are
queued with their sample offset (512-event ring per block) and applied at that
sample.

## Measured

| What | Result |
|---|---|
| Pitch, root and −12…+12 semitones, 220 Hz tone, both hold kinds | under 0.01 cent off (test tolerance 3 cents); e.g. +7: 329.6276 Hz against 329.6276 expected |
| 3-note chord | three pitches, each within 3 cents and within 6 dB of the strongest |
| Saw chord and pad, +7 | the strongest partial moves by a fifth within 3 cents, both hold kinds |
| Level, root at velocity 127 against the hold | within 1.5 dB; velocity 64 sits at `(64/127)^2` within 0.5 dB |
| Note-off | below −100 dBFS 0.4 s after the release |
| Unfreeze | matches the never-frozen render within 1e-5 once the release and mask tail pass; notes after it change nothing |
| No MIDI while frozen | matches Freeze Keys off; the stage alone matches a bare `FreezeSource` bit for bit at uneven block sizes |
| Clicks (sine, note on/off, chord, first key, unfreeze) | worst second difference 0.0012 (0.30 with the envelopes removed) |
| AU host, 8-note chord per tap (`Spectr-au-freeze-keys-host*`) | MIDI accepted (no −4); worst note-on call about 1.0 ms at 128 frames / 48 kHz, 3.6× the costliest call without freeze (gate 6×); loop voices 57 µs |
| CLAP, VST3, AU through `pulp::host::PluginSlot` | 220.000 Hz held, then 329.628 Hz after note 67 |

Renders to listen to (`Spectr-test "[.render]"` with
`FREEZE_KEYS_WAV_DIR=/tmp/freeze-keys`): a melody and a chord played over a
frozen pad in both hold modes, the same freeze without keys, and the input.

## Product decision: the AU type

Logic never sends MIDI to an `aufx`. Freeze Keys in an AU therefore needs one
of these options:

1. **Make Spectr an `aumf` (music effect).** This is what Logic expects for an
   effect that takes notes. It is listed as an Audio FX and under AU
   MIDI-controlled Effects, and its header offers a MIDI side-chain menu. This
   prototype has not been tried in Logic. The AU type is part of the saved identity,
   though, so changing it orphans every session saved against `aufx Spec Pulp`.
   It would have to ship as a new product identity (for example a separate
   "Spectr Keys" component next to the shipping one), not as an update.
2. **Ship a separate MIDI companion.** An `aumi` MIDI FX plugin forwards notes
   to Spectr through a shared in-process channel. This keeps `aufx`, but it is
   fragile: there is no supported cross-plugin channel, and Logic hosts AUs
   out of process.
3. **Keep `aufx` and have no keys in Logic AU.** CLAP and VST3 already take
   notes as effects (shown here through `PluginSlot`), so Freeze Keys should work in
   hosts that route MIDI to effects, such as REAPER, Bitwig and Cubase, with
   no identity change. It has not been tried in a DAW yet. Logic users would get it
   only if option 1 ships.

Recommendation: option 3 for the next release (CLAP and VST3 effects declare
MIDI input, AU unchanged), with option 1 as a separate `aumf` identity if
Logic users ask for it.

## Pulp SDK notes

- `aumf` support is complete. `ACCEPTS_MIDI`, `accepts_midi` and
  `PULP_AU_MIDI_PLUGIN` produce a component that takes `MusicDeviceMIDIEvent`.
  The probe proves this in-process.
- A VST3 effect with `accepts_midi` also registers 2080 hidden MIDI-controller
  proxy parameters (`IMidiMapping`). Hosts and parameter-surface checks see
  2233 parameters instead of 153, and `test_built_clap.cpp` filters them out.
  A descriptor opt-out ("notes only, no CC mapping") would keep an effect's
  automation list clean.
- Pulp has no voice or pitch primitive for this. Several pieces are generic
  and could move into `pulp::signal`: per-voice overlap-add resynthesis from a
  held magnitude/frequency spectrum, an exact fractional-bin Hann lobe, and a
  polyphonic voice allocator with a spread-over-samples preparation budget. A
  band-limited varispeed reader for loops would also fit there.
  `FreezeHold` exposes the held spectrum but has no "play it at ratio r".
