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
// start in Tracking") and nowhere argues FROM the default, so changing which mode
// ships as the default is a one-line edit here rather than a rewrite.
// The "Live and Precision" section is withheld while the header's LIVE /
// PRECISION control is hidden (the editor always eases at the Live rate).
// Restoring that control means restoring this section, after "## Presets":
//
//   ## Live and Precision
//
//   This changes how the display moves, not how it sounds. **Live** reacts
//   fast. **Precision** settles slowly so values sit still while you aim at
//   them.
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

## Keyboard shortcuts

In the standalone app, letter keys switch modes: S, L, B, F and G pick the edit mode, M mutes the selected bands, T switches latency and Q freezes the sound. In a DAW they are off by default, so the DAW's own keys, such as Logic's Musical Typing, keep working. Turn on **Keyboard shortcuts in DAW** in Settings to use them there.

## The analyzer

The colored line over the bands is your audio in real time. **Peak** shows instant level and catches transients. **Avg** is slower and shows sustained energy. **Both** shows them together so you can see the difference.

This is only a display. It does not change your sound.

## Snapshots and morph

Capture a shape into **A**, draw something different, capture it into **B**. The morph slider blends between them.

The slider stays greyed out until both slots are filled.

## Freeze

Press **LIVE** in the header, or Q, to freeze the sound coming in. Spectr holds it and keeps playing it, and everything you draw keeps working on what it holds: the bands, mutes, the morph and both LFOs. Press **FROZEN** to go back to the live sound.

Below 100% Mix only the processed part is frozen, so the untouched part stays live and you can play over the held sound.

**LENGTH**, beside the button, sets how much of the incoming sound a freeze takes in, in bars of your DAW's tempo and time signature: a fraction of a bar from 1/32 to 15/16 (dotted and triplet values such as 3/16 and 1/3 included), 1, 2, 4 or 8 bars, or **Custom length…** for whole bars plus a fraction of a bar, such as 1 1/8 bars. Spectr loops exactly that long, so what you freeze stays on the beat and maps onto your loops. A length shorter than a quarter of a second holds the moment you press as a steady tone instead. Without a DAW tempo, a bar is two seconds (120 BPM in 4/4). A change of tempo applies to the next freeze, never to one that is playing. Very long lengths at slow tempos loop at most the last minute.

A freeze pressed over silence waits for sound before it holds anything, so it never holds silence.

## Level: Mix, Intensity, Output and AUTO

Three knobs in the header set how much you hear and how loud it is. Drag up or right to turn one up, hold Shift or Option for fine steps, scroll over it, or use the arrow keys once it has focus. Double-click a knob to reset it.

- **Intensity** is how strong the effect is. At 100% you hear the shape exactly as drawn; at 50% every boost and cut is half as deep in dB; at 0% Spectr is flat. A muted band fades back in as Intensity comes down. It is the cleanest way to make the filtering subtler.
- **Mix** blends Spectr's sound with the original input. It is most useful with Freeze: below 100% you hear the frozen sound layered over the live input. To make the filtering itself gentler, reach for Intensity instead: a part-way Mix adds the untouched signal back rather than softening the shape.
- **Output** is the final volume, in dB, up to 24 either way.
- **AUTO** keeps the level steady as you boost or cut. It listens to the sound going through Spectr, its long-term balance of lows, mids and highs, works out how much louder or quieter your shape makes that sound, and makes up the difference, before Output, so Output still has the last word. Boosting the highs of a bass line barely changes its level, so AUTO barely moves. It listens over several seconds and holds its setting through silence, so it does not pump; when you change the shape it follows within a fraction of a second. When the sound itself changes, a new part, a much quieter passage, releasing Freeze, it catches up in about a second, and it remembers the sound when you stop, locate or reopen the project, so playback starts at the right level. While Freeze holds, it listens to the held sound. It does not follow the LFOs: a level LFO stays audible as level. AUTO starts off: turn it on when you want it. A project that saved AUTO on keeps it on. A project that saved AUTO on with an earlier version keeps that version's AUTO, so its level does not change, until you switch AUTO off and on again.

## Range and Display

**Range** in Settings, under Structure, sets how far a full-height drag reaches: plus or minus 3, 6, 12 or 24 dB. At 6 dB the whole height of the plot is 6 dB either way, which gives the fine control mixing and mastering moves need. It does not change the sound. A band already beyond the range keeps its value and is drawn pinned to the edge with an amber marker; your next edit to it brings it inside. Range is saved with your project.

**Display**, under Appearance, chooses whether the plot draws the bands, the response curve, or both. It is the same setting the header used to carry, and your DAW can still automate it as **Visualization**.

**Show tooltips**, under Feedback, turns off the short descriptions that appear when you rest the pointer on a header control. It is on by default and saved with your project.

## Movement

Two LFOs can move things on their own, in time with your project tempo. An LFO only sets the movement: its **Shape** and its **Rate** in beats. What it moves, and how far, you choose per target.

Right-click a band, open **Modulation**, and under **LFO 1 Targets** (or LFO 2) switch on as many targets as you like. The same list is in Settings, under Modulation; change it in either place and the other follows. Each target you switch on shows its own **Depth** right under it, so one LFO can push the bands hard and nudge the frequencies gently at the same time. Both LFOs can drive the same target; their movements add.

You can also right-click LIVE / FROZEN, MIX, INTENSITY, OUTPUT, LENGTH or BANDS for a small menu of just that control: reset it, switch LFO 1 or LFO 2 on for it and set the Depth, and (for LIVE / FROZEN) Hold for Length.

- **Bank** moves all the band levels up and down together.
- **Band shift** slides the whole set of bands up and down in frequency, keeping its width. You hear the sweep; the display stays where you set it, so you can keep drawing.
- **Band spread** spreads the bands wider or narrower around their centre.
- **Intensity** pulls the shape toward flat and back, so the whole effect breathes. At 100% Depth it goes all the way to flat at the top of each cycle, wherever the Intensity knob sits.
- **Mix** pulls toward the original input and back. Over a frozen sound this blends frozen and live in time: try a slow sine for a swell, or a square for a rhythmic gate between the two.
- **Morph** rocks the A/B morph back and forth around where the slider sits.
- **Freeze** switches between LIVE and FROZEN. Depth is how much of each cycle is frozen, and every freeze captures fresh sound. Try a square wave on a short rate for rhythmic stutters. **Hold for Length** (under Freeze, off by default) keeps Freeze enabled for the length of the modulated Length: each time the LFO swings up, Freeze plays to the end of the LENGTH in effect at that moment, then lets go, whatever the LFO does meanwhile; the next swing up freezes again for whatever the LENGTH is then. Off, Freeze simply follows the LFO. With one LFO on both Freeze and Length, each freeze reads the Length a step further along the LFO, so the lengths follow its shape. To vary the length of each freeze independently, modulate Length with the other LFO.
- **Length** picks each new freeze's loop length around your LENGTH setting. Depth is how many steps either way it can go. A loop that is already playing is never resized. While it is modulated, LENGTH shows in violet the length the freeze is using (or the next one will use); its menu still shows your own setting.
- **Bands** steps the band count around your BANDS setting (32 to 64). Depth is how many steps either way. Each change fades through flat for a moment, so it never clicks. BANDS shows the count playing, in violet.
- **Preset** morphs toward the presets next to the one you chose, in the order of the preset menu. Depth is how many presets either way (up to four). Your own edits are the centre: at the middle of each swing you hear exactly what you drew. The preset label shows the preset it is nearest, in violet. Pick a preset first; it remembers the neighbourhood with the session.
- **Output** moves the volume up and down around your Output setting, up to 6 dB either way at 100% Depth. AUTO never cancels it.
- **Snapshot A / B** blend toward that snapshot and back again.

A and B do nothing if that snapshot is empty, and Morph needs both. The list scrolls: the most-used targets are at the top, and the heading stays put while you scroll.

The Intensity, Mix and Output knobs keep showing your setting while an LFO moves them; their ring and rim turn violet to show that one is.

While a menu is open, scrolling moves only that menu; scrolling anywhere else does nothing until it closes.

**Touching something an LFO is driving.** The LFO keeps running. Press LIVE / FROZEN while Freeze is modulated and your press holds until the LFO next switches (with Hold for Length, until that freeze ends); pick a LENGTH, a band count or a preset and it becomes the new centre; turn the Intensity, Mix or Output knob and your setting becomes the new centre. With **Ask before overriding modulation** on (Settings, under Modulation, on by default) Spectr asks first: **Keep modulating** leaves the LFO in charge -- a new LENGTH, band count, preset or knob setting becomes the centre it moves around, and a LIVE / FROZEN press is set aside so Freeze keeps following the LFO -- while **Turn off** switches that LFO's target off and then does what you asked. Return turns off, Escape keeps modulating, and **Don't ask again** turns the question off. For a knob, Keep modulating lets you turn it from then on without asking again.

## Automation

Nearly every control here is a plug-in parameter, and your DAW can drive any of those. In Logic that means Learn Plug-in Parameter: open a Modulator or an automation lane, choose Learn, then touch the control in Spectr and the two are linked. Whatever you touched shows up by name, so moving band 31 offers you **Band 31 Gain**.

MIDI CC is a different mechanism, and Spectr does not listen to it. It is an audio effect with no MIDI input at all, so there are no CC numbers to look up and a list of them is not the thing to aim at here. Plug-in parameters are the whole surface.

There is no right-click path for this either. Right-clicking a band gives you band actions, mute, solo, reset to 0 dB, select. Assigning a modulator is something your host does, not something Spectr does.

Every LFO target's on/off and its Depth are plug-in parameters too, so you can record them and play them back like anything else: **LFO 1 Band shift**, **LFO 1 Band shift Depth**, and so on for each target and both LFOs. Sessions from earlier versions sound as they did: an old LFO Depth is carried into the Depth of each target that LFO was driving, and old automation of **LFO Target** or **LFO Depth** still works, applying to the targets that LFO drives.

Two exceptions worth knowing, and they are the only ones. **Morph moves the view** is plugin-only. So is **Latency**, and for a reason worth stating: switching it rebuilds the processor and moves your DAW's delay compensation, which is not something a lane should be able to ask for once per block. It is saved with your project and recalled with it, but your DAW cannot sweep it.

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

An LFO is not a second route to this. Its targets move the whole bank, the frequencies, or the morph, and none of those is a range of bands. An LFO pointed at A or B is range-selective in the same way the morph is, but an LFO is always moving, so driving a target's **Depth** from your DAW scales an oscillation rather than placing the bands where you want them.

## What you can automate

The list your DAW shows is long, because every band is in it. The ones worth knowing by name:

- **A/B Morph** blends the whole bank between the two snapshots. The most musical single target in the plugin.
- **Viewport Center** and **Viewport Width** slide and widen the frequency range the bands cover. Automating the centre sweeps your whole shape up and down the spectrum.
- **LFO Rate** and **LFO 2 Rate**, and each target's on/off and **Depth** (**LFO 1 Bank Depth** and so on), let you modulate the modulators from outside.
- **Band 01 Gain** through **Band 64 Gain**, and **Band 01 Mute** through **Band 64 Mute**, for one band at a time.
- **Freeze** is the LIVE / FROZEN button, so your DAW can freeze and release the sound on the beat. **Freeze Hold for Length** is the switch of that name under the Freeze target. **Freeze Length** is the LENGTH control: each fraction of a bar from 1/32 to 15/16, 1, 2, 4 or 8 bars, or Custom, the custom length you last set.
- **Mix** blends Spectr with the original input. **Intensity** scales the whole shape toward flat. **Output** trims the level on the way out, by up to 24 dB either way. **Auto Gain** is the AUTO switch.
- **Macro 1** through **Macro 4**, in a Macros group of their own, are four spare lanes each worth up to 24 dB either way. A macro is an offset: it rides on top of whatever its member bands are already drawn at, rather than replacing them. They are listed in every build so your host never has to rescan to find them, and they stay inert until bands are assigned to one, which this version has no way to do yet. Four lanes that currently move nothing, and worth recognising rather than hunting for.

Band numbers count from the left, so Band 01 is the lowest. Which frequency that actually is depends on where you are zoomed and how many bands you are showing, so the same lane means something different at 32 bands than at 64. Settle the band count before writing any band automation. **Band Count** is automatable itself, but changing it re-lays out the whole bank underneath your existing lanes, so treat it as a setup choice rather than a move.

## Presets

Eight to start: Flat, Harmonic Series, Alternating, Comb, Vocal Formants, Sub Only, Downward Tilt, Air Lift. Save your own, export them to a file or the clipboard, and import them back.

## Latency

Spectr can realise the shape you draw in two ways, and they trade against each other. Which one suits you depends on what you are doing right now, not on which is better.

**Tracking** responds in about 1.3 ms, fast enough to play and record through Spectr and still hear yourself in time. The cost is depth: very narrow cuts come out shallower in this mode.

**Mixing** looks at a large slice of audio at once, and cuts far deeper for it. How much deeper depends on how strictly you judge where one band ends and the next begins, and on any reading it is a wide margin: a muted band lands somewhere between about 45 and 90 dB lower than the same band in Tracking. That margin holds steady wherever you are working, whether you are looking at the whole spectrum or zoomed right into a narrow span. Mixing costs about 213 ms of latency at 48 kHz, which your DAW lines up automatically so playback stays in sync.

**GPU processing** (Settings, under Latency, or the CPU/GPU chip at the top right) renders Mixing on the graphics processor instead. It sounds exactly the same and adds latency, which Settings shows before you switch and your DAW again lines up; it is off unless you turn it on. Tracking always runs on the CPU, so there the chip reads CPU and does nothing.

Looking at that much audio at once has a second cost, and it is the reason Tracking is not just a lesser option. Mixing spreads a little of every sharp sound backwards in time, so a faint trace of a drum hit can arrive about a seventh of a second before the hit itself. That is not a fault waiting to be fixed. It is the price of the even, symmetrical way Mixing works, which is the same thing that makes it deep. Tracking spreads nothing backwards at all.

Most of the time you will never hear it. It only becomes audible when you cut a narrow low band on percussive material, and that happens to be the case where Mixing's extra depth buys you the least.

Neither one is an upgrade on the other. Use **Tracking** for drums and other percussive material when you are cutting a narrow low band, and whenever you are playing or recording through Spectr. Use **Mixing** everywhere else: it is dramatically deeper on mutes, and on most material that backward spread sits far too quiet to notice.

New instances start in Tracking. The chip at the bottom right, beside the gear, always shows which mode you are in and what it currently costs. Click it to switch. The same control is in Settings under Latency, with a line of guidance for each mode.

Switching rebuilds the processor and tells your DAW that its delay compensation has moved, so it is a setup choice rather than something to reach for in the middle of a take. A project always reopens in the mode it was saved in, so an older session keeps sounding and lining up exactly as it did.`;
