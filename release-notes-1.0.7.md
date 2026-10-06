Spectr 1.0.7 brings musical Freeze lengths, multi-target modulation, new level controls, optional GPU processing for Mixing, and the first standalone app that updates itself.

**Install:** download `Spectr-1.0.7.pkg` and open it. By default it installs the AU, VST3 and CLAP plug-ins, the standalone Spectr app, and Spectr Diagnostics. You can untick any of them under Customize. The installer is signed and notarized by Apple. Requires a Mac with Apple silicon running macOS 13.4 or later.

## What's new since 1.0.5

### Freeze
- **Length dropdown.** A LENGTH menu sits next to LIVE / FROZEN. Pick a musical length (bars and beats, synced to your session's tempo) or choose **Custom** for your own.
- **Hold length loops.** Freeze loops exactly the length you chose.
- **Freeze fixes.** Engaging and releasing Freeze is cleaner and more reliable.

### Modulation
- **Multiple targets per LFO.** Each LFO can drive several destinations at once, and every target has its own **Depth**.
- **13 targets:** Bank, Snapshot A, Snapshot B, Morph, Band shift, Band spread, Freeze, Length, Intensity, Mix, Output, Bands and Preset.
- **Hold for Length.** When an LFO drives Freeze, turn on Hold for Length to keep Freeze enabled for the length of the modulated Length: each trigger freezes fresh sound and plays to the end of the Length in effect at that moment, then lets go. With Length modulated, every freeze can have its own length. Off (the default), Freeze follows the LFO as a gate.
- **Modulated controls show what they play.** A modulated knob or MORPH shows a single indicator at the value the LFO is playing, and moves with it; LENGTH, BANDS and the preset name show the value the LFO is playing, in violet. Modulation never writes automation.
- **Ask before overriding.** Touching a control an LFO is driving asks whether to stop the modulation first. **Keep modulating** leaves the LFO in charge (for LIVE / FROZEN your click is set aside); **Turn off** stops it and applies your change. You can turn this off in Settings.

### Level controls
- **MIX, INTENSITY and OUTPUT knobs** in the header, with **AUTO** gain.
- **AUTO listens to your sound.** Auto Gain now keeps the level steady by weighing your shape against the long-term balance of the sound going through Spectr, so boosting the highs of a bass line no longer turns it down. It holds through silence and does not pump, catches up in about a second when the sound changes or Freeze is released, remembers the sound across stops, locates and reopened projects, and while Freeze holds it listens to the held sound. It keeps listening while it is off, so switching it on starts from the sound that is playing. **AUTO is now on by default** in a new instance; projects keep the AUTO they saved, and one saved before AUTO existed opens with it off. A project that saved AUTO on with an earlier version keeps that version's AUTO, so its level does not change, until you switch AUTO off and on again.
- **Range and Display settings** for the editor.

### GPU processing (Mixing, experimental)
- **Optional, and off by default.** Mixing can run its spectral processing on the Mac's GPU. Turn it on with the CPU / GPU chip in the header while in Mixing, or under **Settings > GPU processing**. Tracking always runs on the CPU for the lowest latency; in Tracking the chip says so and changes nothing.
- **Same sound, more latency.** The output is identical to the CPU path. GPU processing adds about 107 ms of latency at 48 kHz, which your DAW compensates: Mixing reports about 320 ms instead of 213 ms. Settings shows the figure for your session's sample rate.
- **No CPU saving yet.** In this version GPU processing does not lower Spectr's CPU use.
- **Never drops out.** When the GPU is busy or the Mac is heavily loaded, the affected blocks render on the CPU at the same latency, so playback continues. On Macs with fewer GPU cores, or while other apps use the GPU heavily, more blocks may fall back (the audio is unaffected); if the GPU stats show frequent fallback, turn GPU processing off.
- **GPU stats.** A Settings switch shows what the GPU is doing; it is off by default.

### Audio
- **Click-free Latency switch.** Switching between Tracking and Mixing crossfades from one to the other instead of dropping out, also when you switch before playback starts or the host resets during the switch.
- **Small buffers.** Mixing and Auto Gain spread their work across callbacks, so Spectr keeps up at 32-sample buffers at 48 kHz and 96 kHz.
- **Mixing starts at full level.** The first moments of playback in Mixing come through at full level instead of fading in.

### Editor
- **Tooltips** on the header controls, with a Settings switch to hide them.
- **Right-click menus** on LIVE / FROZEN, MIX, INTENSITY, OUTPUT, LENGTH, BANDS, MORPH and the preset button: reset, the LFO 1 / LFO 2 switches with Shape and Rate, then just that control's own target ("LFO 1 → Freeze") with its Depth (and Hold for Length) nested under it, and **All targets…** for the full list scrolled to and highlighting that target.
- **Faster opening.** The plug-in window opens sooner and with no flash of colour.

### In your DAW
- **Accurate offline bounces.** An offline bounce or freeze now matches real-time playback.
- **Automation records and plays back** your edits to Mix, Output, both LFOs, Morph and the plot.
- **More reliable with every host.** Setting many parameters at once (for example when a host restores a session) always gives the same result.

### Updates
- **The standalone app updates itself.** Spectr.app checks for updates on its own and offers each one; it installs only when you choose **Install Update**. Check any time with **Check for Updates…**, right under **About Spectr** in the app menu, or in **Settings → Updates**, where you can also turn automatic checks off. This is the first version that can update itself; earlier versions need this installer once.

Built with the Pulp v0.912.0 SDK.

**Having trouble?** Open **Spectr Diagnostics** from Applications. It saves a report you can email to support.
