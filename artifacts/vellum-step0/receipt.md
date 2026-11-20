# Spectr on Vellum step 0 receipt

Built on `spectr#363` (`lane/b1b-spectr` fast-forward `77061a4`) with the read-only SDK prefixes supplied for this lane. The CLAP was configured with `SPECTR_VELLUM_EDITOR_PACKAGE=ON`.

## Offscreen editor

`editor-probe.log` shows the real CLAP lifecycle, embedded Cocoa parent assignment to an `NSView` with `window == nil`, two identical control paints, and the same-value negative control with 0 changed pixels. The requested Mix change was accepted by `clap.params.flush`, but this runtime painted 0 changed pixels, so the changed-control and control-bounding-box assertions are FAIL and editor parameter repaint is not proven.

## Offline audio

The SDK `pulp audio render` path produced dry, wet, automation, restore, and planted/unplanted renders. The planted `Output Trim` change is 0.1 dB and has a distinct WAV hash from the unplanted render.

## Not proven

- Visible editor response to the changed Mix value, because the offscreen frame diff was 0 pixels.
- Product UX in a DAW, on-screen windows, live audio, installation, and system plug-in discovery. Those were intentionally not exercised.
