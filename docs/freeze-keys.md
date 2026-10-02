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

## Playing it in Logic Pro

In Logic, an AU MIDI-controlled effect is loaded in a software instrument
track's **Instrument** slot, not in an Audio FX slot. It plays from that
track's own MIDI. The header's **Side Chain** pop-up chooses the *audio* the
effect processes, not the MIDI source (Apple, Waves and Native Instruments
document this setup; it is the same pattern as Logic's EVOC 20 vocoder).

1. Create a Software Instrument track (File > New Tracks > Software
   Instrument).
2. In its **Instrument** slot choose **AU MIDI-controlled Effects > Pulp >
   Spectr Keys Dev**.
3. In the plug-in window header, open the **Side Chain** pop-up (top right)
   and choose the track whose sound you want to freeze. Optionally set that
   track's output to No Output so it isn't heard twice.
4. Select the Spectr track so your keyboard, or Window > Show Musical Typing
   (Cmd-K), plays it.
5. Start playback, press **Freeze** in Spectr while the sound plays, then
   play keys: C3 (MIDI 60) plays the hold at its own pitch, G3 a fifth up,
   C2 an octave down. Releasing Freeze returns the live input.

Not yet confirmed in Logic: whether the Side Chain pop-up appears for this
component. Most shipping `aumf` plug-ins expose a single input bus, as Spectr
does, and Logic reportedly feeds the side-chain audio to that bus in the
Instrument slot. If the pop-up is missing, Pulp's AU v2 effect adapter needs a
second input element (it exposes one today), plus a Plug-in Manager rescan or
version bump so Logic notices.

### Is MIDI arriving?

Spectr Keys Dev writes what it receives to the system log, from inside
Logic's out-of-process AU host too:

```sh
/usr/bin/log show --last 10m --predicate 'eventMessage CONTAINS "spectr-keys"' --style compact
/usr/bin/log stream --predicate 'eventMessage CONTAINS "spectr-keys"' --style compact
```

- `[spectr-keys] ready: ...` is printed each time the host prepares the
  plugin. No `ready` line: the loaded build is not a Freeze Keys build.
- `[spectr-keys] note-on 67 (G3) vel 100 ...: frozen=1 phase=held hold=loop
  mode=keys voices=1 ... -> playing` is printed for the first note-on, and for
  the first after Freeze is switched on or off (at most 32 a session). The
  end of the line says what the note did: `playing`, `ignored: Freeze is off`
  or `waiting: the hold is not audible yet`.
- A `ready` line and no `note-on` line while you play: the host is not
  sending MIDI to the plugin. In Logic, check that Spectr is in the
  Instrument slot of the track you are playing (steps 1-2).

The audio thread only records the note; a worker formats and logs it.

## Behaviour

- **Root key: MIDI 60, named C3.** The root key plays the held sound at its
  own pitch. Every other key transposes it by its distance from the root in
  equal-tempered semitones (`2^((note - root) / 12)`). Notes are named the way
  Logic Pro names them, octave = `note / 12 - 2`, so MIDI 60 is **C3**, MIDI 0
  is **C-2** and MIDI 127 is **G8** (`FreezeKeys::note_name`). This is the
  same convention PlunderTube uses for the key that plays a sample at its
  original pitch (see "Root key convention" below). Yamaha also calls MIDI 60
  C3; Roland and scientific pitch notation call it C4. The number, 60, is what
  matters; only the label differs between conventions.
  The root can be changed with `Spectr::set_freeze_keys_root_note`; there is
  no Settings control or saved state for it yet. The Settings FREEZE KEYS
  subtitle names it ("...; C3 plays it at its own pitch."), from `root_name`
  in the editor's `freeze_keys` payload, which also carries `root_note`.
- **Range: every MIDI note, 0 to 127 (C-2 to G8).** From the default root
  that is 60 semitones down (1/32×) to 67 up (47.9×), so Musical Typing can
  shift across the whole MIDI range. Nothing is ignored. Notes far below the
  root may be mostly sub-audible (a 220 Hz hold played at C-2 sounds at
  6.9 Hz); see "The ends of the range" for what each voice does there.
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
two read-only accessors that were added (`loop_position()` and `loop_sample()`)
and `set_external_loop_readers()`: while a loop voice sounds (a release tail
can outlast the hold), the source does not adopt the bigger loop rings a long
Length asks the storage worker for, so a voice never reads rings swapped (and
freed) under it.
After that, and only once a key has been played into a hold, it replaces the
held sound with the sum of the voices.

**Spectral hold** (the freeze Length, in seconds at the host tempo, below
0.25 s: 1/16 bar at 120 BPM, say). The voice resynthesises the
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

**Loop hold** (the Length 0.25 s or more at the host tempo; the default,
1 bar). The voice plays the loop with varispeed, like a sampler, and
crossfades the seam the way the source does. Pitch and loop duration change
together. There is no note latency.

- **At or below the root** it reads the loop with 4-point Hermite
  interpolation. Slowing a loop down folds nothing back, so no filter is
  needed; at the root the read is the loop sample for sample.
- **Above the root** the read is band-limited. Reading a loop `ratio` times
  faster pushes anything above `fs / (2 * ratio)` past Nyquist, where it
  would fold back as inharmonic tones (measured below: at full level without
  the filter). The voice streams every loop sample it passes over through a
  cascade of half-band decimators, one per octave of the ratio, until the
  ratio left is in [1, 2). It then reads the last stage with a
  Kaiser-windowed sinc (11 zero crossings each side, β 6.76) whose cutoff is
  scaled to `0.5 / residual`. Each half-band stage's length is what its place
  in the cascade needs to keep the final pass band (0.4 fs at the output,
  19.2 kHz at 48 kHz) clear of its aliases: 47 taps for the last, then 19, 15,
  and 11 for the rest. The cascade runs in batches of up to 2048 loop
  samples. Its cost grows with the ratio, because it consumes every loop
  sample the voice skips over: at 47.9× that is 48 loop samples per output
  sample per channel. Where a note starts is a setting,
**Restart loop on note** (Settings > FREEZE KEYS):

- **On (the default).** Each note-on starts its own playhead at the top of the
  captured loop. Its first pass plays the plain captured audio, then it loops
  for as long as the key is held, with the source's seam crossfade at each
  wrap. A chord, or notes played at different times, each start from the top.
  This is sampler behaviour: hold a key over a 2-bar loop and you hear those
  2 bars from their downbeat, repeating. The loop is exactly the musical
  Length (a Length loop does not move its end to a better-matched seam), so
  a root note's period is the Length in seconds at the host tempo: 2 bars at
  120 BPM 4/4 is 4.000 s, 1 1/8 bars at 90 BPM is 3.000 s.
- **Off.** A note starts wherever the frozen loop is playing at the note-on,
  so the root key continues the running loop seamlessly. That makes the hold
  fade out under the first root note linearly, since it is the same audio;
  any other first note fades the hold out equal-power.

The setting does not apply to a spectral hold, which has no start. It is
saved in the plugin state as `freeze_keys_restart_loop`. A session saved
before the setting existed has no such member and loads as **On**, the same
as a new instance. It is not a host parameter: it adds nothing to the
append-only automation surface, and it can be exposed later as an appended
parameter if hosts need to automate it. The Settings group is its own
(`marker: "freeze-keys"`), placed before APPEARANCE and independent of the
FREEZE group (whose Hold length row the header LENGTH control replaced). It renders only where Freeze Keys plays
(the hydration carries `freeze_keys` only then). The UI is applied by
`tools/patch_materialized_freeze_keys_restart.py`.

- The alternative is a time- and formant-preserving pitch shift, so the loop
  keeps its length at any pitch, for example the stretch tools in Pulp (`pulp::signal`
  phase vocoder / `RealtimePitchTimeProcessor`). That costs a pitch shifter per
  voice and adds latency, and it changes the character from "sampler" to
  "harmoniser". Varispeed is the simpler choice and it sounds right for a
  frozen loop played from a keyboard.

### The ends of the range

- **Loop voices, far up (to 47.9×).** Band-limited as above. A tone the
  transposition carries past Nyquist leaves about −80 dB relative to an
  in-band tone of the same level; with the plain Hermite read it folded back
  at full level (0 dB). The playhead wraps however many times a step crosses
  the loop's end.
- **Loop voices, far down (to 1/32×).** Hermite, unchanged. A step of 1/32 of
  a sample is exact in the read position's double precision. The output is the
  loop slowed down: finite, no DC, at the hold's level.
- **Spectral voices, far up.** Each tonal partial is redrawn at its scaled
  frequency only while its whole lobe is below Nyquist; a partial scaled past
  it is dropped, and a noise bin whose scaled frequency reaches Nyquist is
  silent. Nothing wraps: a tone carried past Nyquist leaves less than
  −110 dBFS.
- **Spectral voices, far down.** Noise bins read the held spectrum at
  `j / ratio`, so at 1/32× only the lowest 1/32 of the bins sound, with power
  kept as varispeed keeps it. Partials that land below 4 bins (23 Hz at
  48 kHz) are dropped, as is DC. Notes whose fundamental falls there are
  mostly sub-audible, by design.

## Real-time

`prepare()` allocates every buffer, including 12 voice slots of OLA,
spectrum plans and partial tables (about 4 MB in stereo), plus each voice's
band-limiting histories and two shared batch buffers for the cascade (about
90 KB in stereo). The audio thread does
not allocate, lock or read a clock. `tools/ci/check_render_path_clock.py` scans
`freeze_keys.hpp` as ctest `Spectr-render-path-clock-freeze-keys`. Notes are
queued with their sample offset (512-event ring per block) and applied at that
sample.

## Measured

| What | Result |
|---|---|
| Pitch, root and −12…+12 semitones, 220 Hz tone, both hold kinds | under 0.01 cent off (test tolerance 3 cents); e.g. +7: 329.6276 Hz against 329.6276 expected |
| Range, notes 0, 12, 24, 48, 60, 72, 96, 120, 127, 220 Hz and 1 kHz tones, every hold kind | every output finite, peak under 1.0; all 13 readings whose pitch is in band (30 Hz to 0.4 fs; note 0 on 1 kHz is 31.25 Hz, note 127 on 220 Hz is 10.55 kHz) within 3 cents and within 2 dB of the hold's level, mean under 0.01 |
| Fold-back, a tone carried to 0.7 fs, notes 67 to 127 (1.5× to 47.9×) | loop voices: about −80 dB under an in-band tone of the same level (−90.6 dBFS against −10.5); plain Hermite (before band-limiting): 0 dB. Spectral voices: −113 dBFS or less in total. Test limit: 60 dB under |
| Cost, 8-note chord at the top (120–127) and bottom (0–7), 128 frames / 48 kHz, least of three runs (two sessions) | loop top: costliest call 0.45–0.69 ms, mean 0.40–0.60 ms (budget 2.67 ms; the unfiltered Hermite read cost 0.04 ms); loop bottom 0.03–0.05 ms; spectral top and bottom 0.50–0.82 ms costliest, against 0.46–0.73 ms for the freeze alone. Gate: at most 6× the freeze alone's costliest call (loop top: 2.6–3.2×) |
| AU host, 8-note chord 48–72 (`Spectr-au-freeze-keys-host`), after band-limiting | worst note-on call 663 µs, 4.5× the costliest call without freeze (gate 6×) |
| 3-note chord | three pitches, each within 3 cents and within 6 dB of the strongest |
| Saw chord and pad, +7 | the strongest partial moves by a fifth within 3 cents, both hold kinds |
| Level, root at velocity 127 against the hold | within 1.5 dB; velocity 64 sits at `(64/127)^2` within 0.5 dB |
| Note-off | below −100 dBFS 0.4 s after the release |
| Unfreeze | matches the never-frozen render within 1e-5 once the release and mask tail pass; notes after it change nothing |
| No MIDI while frozen | matches Freeze Keys off; the stage alone matches a bare `FreezeSource` bit for bit at uneven block sizes |
| Clicks (sine, note on/off, chord, first key, unfreeze) | worst second difference 0.0012 (0.30 with the envelopes removed) |
| AU host, 8-note chord per tap (`Spectr-au-freeze-keys-host*`) | MIDI accepted (no −4); worst note-on call about 1.0 ms at 128 frames / 48 kHz, 3.6× the costliest call without freeze (gate 6×); loop voices 57 µs |
| CLAP, VST3, AU through `pulp::host::PluginSlot` | 220.000 Hz held, then 329.628 Hz after note 67 |
| AU hosted as Logic hosts an `aumf` (`Spectr-au-freeze-keys-pitch-*`, `tools/au_keys_host_probe.cpp`): found by its `aumf SpKz Pulp` description, host beat/tempo and transport callbacks, the key sent through `MusicDeviceMIDIEvent` and through `MusicDeviceMIDIEventList` (UMP), transport playing and stopped; also `--out-of-process` (an AUHostingService process, as Logic loads AUs) against the installed component | 220.000 Hz held, 329.628 Hz while note 67 is held, every case. Negative control: the `aufx` Spectr Freeze Dev answers the note with −4 and stays at 220 Hz |
| REAPER 7, offline render (`tools/reaper_freeze_keys.py`): sine on track 1 through Spectr Keys Dev, Freeze automated on at 2 s, a MIDI item on track 2 sent MIDI-only to track 1 | VST3, CLAP and the installed AU: hold 220.000 Hz; notes 67, 72, 55 at 329.628, 440.000, 164.814 Hz (within 0.03 cent); silent between notes |
| Host log | each reported note-on prints one `[spectr-keys]` line from a worker; a build with Freeze Keys off reads, counts and logs nothing |
| Restart loop on note, on, under a host transport | 2 bars at 120 BPM 4/4: the root's period, measured from the product render, 4.000 s (error < 1 ns); 1 1/8 bars at 90 BPM: 3.000 s; its first pass correlates 1.0000 with the loop's top at lag 0 (the loop 10 ms on: 0.0007) |
| Restart loop on note, on | a held root note's first loop length (after its 10 ms attack) correlates 1.0000 with the loop's top; a second note 0.37 s later does too |
| ...held for 2.5 loop lengths | equals the loop sample for sample (worst 1.7e-8): the plain top, then each later pass with the source's seam; sharpest step equals the plain hold's over the same span |
| ...off (the control) | the same note correlates −0.008 with the top; the root equals the plain hold within 1e-5 |
| ...saved state | round-trips; a blob without the member restores On |

Renders to listen to (`Spectr-test "[.render]"` with
`FREEZE_KEYS_WAV_DIR=/tmp/freeze-keys`): a melody and a chord played over a
frozen pad in both hold modes, the same freeze without keys, and the input.

## Root key convention

Spectr follows PlunderTube, which assigns the key that plays a sample at its
original pitch (its Root Key) like this:

- The root is **MIDI 60** by default:
  `Source/WaveformState.h:211` `int keyRangeRootKey = 60;    // Default to C3 (MIDI note 60) - the original pitch`,
  and its `rootKey_N` parameter defaults to 60 (`Source/PluginProcessor.cpp:4297-4300`).
- Other keys shift by `2^((note - root) / 12)`:
  `Source/PluginProcessor.cpp:657-659`.
- Its UI names notes in Logic Pro's convention, `octave = note / 12 - 2`, so
  60 is "C3" and the range is C-2 to G8:
  `Source/Visage/VisageWaveformEditor.cpp:3820-3823` (the Root Key menu,
  "full MIDI note range from C-2 to G8") and `:12953-12957`
  ("Logic Pro octave naming: MIDI note 60 = C3"); the "Root Key: X" label in
  `Source/Visage/VisagePluginEditor.cpp:882-886`.

One inconsistency in PlunderTube is deliberately not copied. Its host-facing
parameter text uses JUCE's octave-4 naming
(`getMidiNoteName(value, true, true, 4)`, `Source/PluginProcessor.cpp:4305`),
so a DAW would show the same 60 as "C4". Spectr uses the Logic naming
everywhere it names a note.

## Product decision: the AU type

Logic never sends MIDI to an `aufx`. Freeze Keys in an AU therefore needs one
of these options:

1. **Make Spectr an `aumf` (music effect).** This is what Logic expects for an
   effect that takes notes. It is listed under AU MIDI-controlled
   Effects in a software instrument track's Instrument slot, takes MIDI from
   that track, and takes its audio through the Side Chain pop-up. This
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
   no identity change. REAPER plays it in all three formats (see "Measured"). Logic users would get it
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
  polyphonic voice allocator with a spread-over-samples preparation budget.
  The band-limited varispeed loop reader (half-band cascade plus scaled
  windowed sinc) would also fit there.
  `FreezeHold` exposes the held spectrum but has no "play it at ratio r".
