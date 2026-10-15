Spectr 1.0.8 makes modulation more precise: each LFO is its own switch, any selection of bands can be its own target, the Rate runs from 1/1024 to 4096 beats and opens in time with your session, and the Preset target steps through whole presets.

**Install:** download `Spectr-1.0.8.pkg` and open it. By default it installs the AU, VST3 and CLAP plug-ins, the standalone Spectr app, and Spectr Diagnostics. You can untick any of them in the installer's Customize pane. If you have Spectr 1.0.7, the standalone app offers this update itself.

## What's new since 1.0.7

### Modulation
- **Band groups.** Right-click a selection of bands and switch an LFO on for just those bands. A group is its own target with its own Depth per LFO, on top of whatever Bank is doing, so LFO 1 can move bands 30 to 32 while LFO 2 moves the rest. A session keeps up to four groups.
- **The LFO switch is its own control.** Turning an LFO on no longer switches Bank (or any target) on with it, and switching a target never flips the LFO. The captions say what is going on: "On, no target" for an LFO with nothing routed, "On, LFO off" for a target whose LFO is off.
- **Rate from 1/1024 to 4096 beats.** The Rate slider runs over musical stops from 1/1024 of a beat to 4096 beats, reads as one token ("1/16", "4"), and the LFO section head says the unit: RATE IN BEATS. A new LFO opens at 1 beat, one cycle per beat, so it is in time with your session before you touch it.
- **Preset steps through presets.** When an LFO drives the Preset target, the bands redraw for each neighbouring preset in menu order rather than blending between them.
- **Right-click menus everywhere.** The remaining controls, Snapshot A and Snapshot B included, have the same right-click menu as the header controls: reset, the LFO 1 / LFO 2 switches with Shape and Rate, and the target rows.
- **Steadier captions.** BANDS keeps its caption where it is while an LFO moves it, like LENGTH already did.

### Pro Tools
- The AAX format is part of this release cycle and is handed out as its own installer while its signing is done by hand; the AU, VST3 and CLAP plug-ins and the standalone app in this package are the same build.

Built with the Pulp v0.975.0 SDK.

**Having trouble?** Open **Spectr Diagnostics** from Applications. It saves a report you can email to support.
