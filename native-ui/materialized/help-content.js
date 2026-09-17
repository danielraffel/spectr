// The Spectr help overlay's copy. This file IS the copy: it is bundled as its
// own editor asset (CMakeLists `SPECTR_NATIVE_ASSET_SOURCES`, written into the
// editor package beside runtime.js and design.js, and evaluated by
// native_editor.cpp right after design.js), so editing the text below is the
// whole edit. Nothing here is compiled into
// materialized-document.runtime.json, which is a checked-in artifact neither
// generator on main can rebuild -- putting the copy there would have made
// every future wording change a surgical patch against a one-line blob.
//
// The markup the overlay understands is deliberately tiny, because the
// renderer lives inside the materialized document and every feature it grows
// is another thing that can only be changed by patching that artifact:
//
//   # heading      the document title, once
//   ## heading     a section heading
//   - item         a tight list row
//   blank line     a paragraph break
//   **bold**       a bold run, anywhere in a line
//
// Two characters are forbidden in the body and asserted at build-generation
// time by tools/spectr-detectors/help_overlay_contract.py: a backtick and a
// `${`, either of which would end this template literal early and take the
// rest of the copy with it.
//
// Both latency figures are the product's own, and the guide has to carry BOTH
// because there are two modes: Mixing is kSpectralFftSize +
// kSpectralAnalysisHop = 8192 + 2048 = 10240 samples, 213.33 ms at 48 kHz;
// Tracking is the zero-latency renderer's fixed 64-sample render block, 1.33 ms
// at 48 kHz. help_overlay_contract.py derives both from those sources rather
// than matching a typed string, and carries one plant per figure so neither
// check can go stale unwatched.
//
// The copy states the current default in exactly one sentence ("New instances
// start in Mixing") and nowhere argues FROM the default, so changing which mode
// ships as the default is a one-line edit here rather than a rewrite.
globalThis.SPECTR_HELP_TEXT = `# About Spectr

Spectr splits your sound into a row of frequency bands and lets you draw what happens to each one. Pull a band down to cut that frequency. Push it up to boost it. Mute it to remove it entirely.

Think of it as a precise way to reach into a sound and grab one part of it.

## How the bands work

The row runs from low frequencies on the left to high on the right, the way a piano runs from bass to treble. You choose 32, 40, 48, 56 or 64 bands from the toolbar. More bands means finer control and narrower cuts.

The bands are not evenly spaced in Hz. They are spaced more like how we hear pitch, so there is as much detail in the low end as the high end, even though the high end covers far more Hz.

## Zooming

Scroll to zoom into the frequency range. You always have the same number of bands, but they spread across whatever range you are viewing.

Zoomed out, each band covers a wide range of frequencies. Zoom into a smaller range and each band becomes much narrower, giving you finer control.

Two readouts tell you where you are. Bottom right shows the frequency range you are viewing. Top right shows the zoom amount.

The waveform behind the bands is your actual audio. Use it to see where the energy is, then draw on top of it.

## Drawing

Five ways to drag, in the edit menu:

- **Sculpt** sets each band to wherever your pointer is. The most direct one.
- **Level** makes every band you drag over the same height. Good for flat shelves.
- **Boost** makes your existing shape stronger or weaker without changing it. Drag up and the peaks and cuts get more extreme. Drag down and everything eases back toward flat.
- **Flare** pushes bands away from the middle, so peaks get sharper and cuts get deeper.
- **Glide** smooths neighbouring bands into each other.

Click a band to mute it. Shift and drag to mute a range.

## The analyzer

The colored line over the bands is your audio in real time. **Peak** shows instant level and catches transients. **Avg** is slower and shows sustained energy. **Both** shows them together so you can see the difference.

This is only a display. It does not change your sound.

## Snapshots and morph

Capture a shape into **A**, draw something different, capture it into **B**. The morph slider blends between them.

The slider stays greyed out until both slots are filled.

## Movement

Two LFOs can move things on their own, in time with your project tempo. Each one has a shape, a rate in beats, and a depth.

Point an LFO at:

- **Bank** to move all the bands up and down together.
- **A** or **B** to move toward that snapshot and back again.
- **Morph** to rock the morph slider back and forth on its own.

A and B do nothing if that snapshot is empty. Morph requires both snapshots. You can choose more than one destination at a time.

## Automation

Nearly every control here is a plug-in parameter, and your DAW can drive any of those. In Logic that means Learn Plug-in Parameter: open a Modulator or an automation lane, choose Learn, then touch the control in Spectr and the two are linked. Whatever you touched shows up by name, so moving band 31 offers you **Band 31 Gain**.

MIDI CC is a different mechanism, and Spectr does not listen to it. It is an audio effect with no MIDI input at all, so there are no CC numbers to look up and a list of them is not the thing to aim at here. Plug-in parameters are the whole surface.

There is no right-click path for this either. Right-clicking a band gives you band actions, mute, solo, reset to 0 dB, select. Assigning a modulator is something your host does, not something Spectr does.

Three exceptions worth knowing, and they are the only ones. An LFO's **Target** can be automated by your DAW; **Destinations**, which is how you pick more than one at a time, is set in the plugin only. **Morph moves the view** is likewise plugin-only. So is **Latency**, and for a reason worth stating: switching it rebuilds the processor and moves your DAW's delay compensation, which is not something a lane should be able to ask for once per block. It is saved with your project and recalled with it, but your DAW cannot sweep it.

## Modulating a range of bands

One band is easy. The whole bank at once is easy too, that is what **Bank** and the LFOs already do. A range in the middle is the interesting case, and the answer is the morph.

The morph blends every band from snapshot A to snapshot B independently, so a band holding the same value in both snapshots does not move at all. The range is simply wherever A and B disagree:

- Capture your current curve into **A**.
- Change only the bands you want to move, and capture that into **B**.
- Point your modulator at **A/B Morph**.

Only those bands travel. That is better than a plain range, because the shape across it is whatever you drew. The middle can move further than the edges, and part of it can go the other way.

Three things to know before you lean on it.

**Mute does not blend.** A band muted in one snapshot but not the other flips at the halfway point instead of fading, which is a click rather than a sweep. Keep the mutes matching in both snapshots and let gain do the work. Gain bottoms out at -24 dB, so a band fades down rather than away entirely.

**The zoom window travels too.** If A and B are looking at different frequency ranges, morphing slides the view as well as the bands. Turn that off in Settings, under Modulation, with the **Viewport** switch.

**The two end bands are not only themselves.** The leftmost visible band owns everything below the window and the rightmost owns everything above it, so a range reaching either end moves more than it appears to.

The morph is one parameter, so it gives you one range at a time. Two ranges moving independently means two instances of Spectr in series. And the path each band takes is fixed once you have drawn the two ends: you shape where it goes, and your modulator's own shape decides how it gets there.

The LFO **Target** control is not a second route to this. Its choices are Bank, A, B and Morph, and none of those is a range of bands. An LFO pointed at A or B is range-selective in the same way the morph is, but an LFO is always moving, so driving its **Depth** from your DAW scales an oscillation rather than placing the bands where you want them.

## What you can automate

The list your DAW shows is long, because every band is in it. The ones worth knowing by name:

- **A/B Morph** blends the whole bank between the two snapshots. The most musical single target in the plugin.
- **Viewport Center** and **Viewport Width** slide and widen the frequency range the bands cover. Automating the centre sweeps your whole shape up and down the spectrum.
- **LFO Rate**, **LFO Depth**, and the same pair on **LFO 2**, let you modulate the modulators from outside.
- **Band 01 Gain** through **Band 64 Gain**, and **Band 01 Mute** through **Band 64 Mute**, for one band at a time.
- **Mix** blends Spectr against the untouched signal. **Output** trims the level on the way out, by up to 24 dB either way.
- **Macro 1** through **Macro 4**, in a Macros group of their own, are four spare lanes each worth up to 24 dB either way. A macro is an offset: it rides on top of whatever its member bands are already drawn at, rather than replacing them. They are listed in every build so your host never has to rescan to find them, and they stay inert until bands are assigned to one, which this version has no way to do yet. Four lanes that currently move nothing, and worth recognising rather than hunting for.

Band numbers count from the left, so Band 01 is the lowest. Which frequency that actually is depends on where you are zoomed and how many bands you are showing, so the same lane means something different at 32 bands than at 64. Settle the band count before writing any band automation. **Band Count** is automatable itself, but changing it re-lays out the whole bank underneath your existing lanes, so treat it as a setup choice rather than a move.

## Presets

Eight to start: Flat, Harmonic Series, Alternating, Comb, Vocal Formants, Sub Only, Downward Tilt, Air Lift. Save your own, export them to a file or the clipboard, and import them back.

## Live and Precision

This changes how the display moves, not how it sounds. **Live** reacts fast. **Precision** settles slowly so values sit still while you aim at them.

## Latency

Spectr can realise the shape you draw in two ways, and they trade against each other. Which one suits you depends on what you are doing right now, not on which is better.

**Tracking** responds in about 1.3 ms, fast enough to play and record through Spectr and still hear yourself in time. The cost is depth: very narrow cuts come out shallower in this mode.

**Mixing** looks at a large slice of audio at once, and cuts far deeper for it. How much deeper depends on how strictly you judge where one band ends and the next begins, and on any reading it is a wide margin: a muted band lands somewhere between about 45 and 90 dB lower than the same band in Tracking. That margin holds steady wherever you are working, whether you are looking at the whole spectrum or zoomed right into a narrow span. Mixing costs about 213 ms of latency at 48 kHz, which your DAW lines up automatically so playback stays in sync.

Looking at that much audio at once has a second cost, and it is the reason Tracking is not just a lesser option. Mixing spreads a little of every sharp sound backwards in time, so a faint trace of a drum hit can arrive about a seventh of a second before the hit itself. That is not a fault waiting to be fixed. It is the price of the even, symmetrical way Mixing works, which is the same thing that makes it deep. Tracking spreads nothing backwards at all.

Most of the time you will never hear it. It only becomes audible when you cut a narrow low band on percussive material, and that happens to be the case where Mixing's extra depth buys you the least.

Neither one is an upgrade on the other. Use **Tracking** for drums and other percussive material when you are cutting a narrow low band, and whenever you are playing or recording through Spectr. Use **Mixing** everywhere else: it is dramatically deeper on mutes, and on most material that backward spread sits far too quiet to notice.

New instances start in Mixing. The chip at the bottom right, beside the gear, always shows which mode you are in and what it currently costs. Click it to switch. The same control is in Settings under Latency, with a line of guidance for each mode.

Switching rebuilds the processor and tells your DAW that its delay compensation has moved, so it is a setup choice rather than something to reach for in the middle of a take. A project always reopens in the mode it was saved in, so an older session keeps sounding and lining up exactly as it did.`;
