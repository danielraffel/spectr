Spectr 1.0.7 brings musical Freeze lengths, multi-target modulation, new level controls, and the first standalone app that updates itself.

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
- **Modulated controls show what they play.** LENGTH, BANDS and the preset name show the value the LFO is playing, in violet.
- **Ask before overriding.** Touching a control an LFO is driving asks whether to stop the modulation first. **Keep modulating** leaves the LFO in charge (for LIVE / FROZEN your click is set aside); **Turn off** stops it and applies your change. You can turn this off in Settings.

### Level controls
- **MIX, INTENSITY and OUTPUT knobs** in the header, with **AUTO** gain.
- **AUTO listens to your sound.** Auto Gain now keeps the level steady by weighing your shape against the long-term balance of the sound going through Spectr, so boosting the highs of a bass line no longer turns it down. It holds through silence and does not pump, catches up in about a second when the sound changes or Freeze is released, remembers the sound across stops, locates and reopened projects, and while Freeze holds it listens to the held sound. AUTO is still off by default. A project that saved AUTO on with an earlier version keeps that version's AUTO, so its level does not change, until you switch AUTO off and on again.
- **Range and Display settings** for the editor.

### Editor
- **Tooltips** on the header controls, with a Settings switch to hide them.
- **Right-click menus** on LIVE / FROZEN, MIX, INTENSITY, OUTPUT, LENGTH, BANDS, MORPH and the preset button: reset, the LFO 1 / LFO 2 switches with Shape and Rate, then just that control's own target ("LFO 1 → Freeze") with its Depth (and Hold for Length) nested under it, and **All targets…** for the full list scrolled to and highlighting that target.
- **Faster opening.** The plug-in window opens sooner and with no flash of colour.

### In your DAW
- **Accurate offline bounces.** An offline bounce or freeze now matches real-time playback.
- **Automation records and plays back** your edits to Mix, Output, both LFOs, Morph and the plot.
- **More reliable with every host.** Setting many parameters at once (for example when a host restores a session) always gives the same result.

### Updates
- **The standalone app updates itself.** Spectr.app checks for updates and installs them with **Check for Updates**. This is the first version that can update itself; earlier versions need this installer once.

Built with the Pulp v0.901.0 SDK.

**Having trouble?** Open **Spectr Diagnostics** from Applications. It saves a report you can email to support.
