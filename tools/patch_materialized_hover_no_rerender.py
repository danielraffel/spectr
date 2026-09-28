#!/usr/bin/env python3
"""A pointer moving over the bands no longer re-renders the editor.

Measured with Perfetto on a standalone running LFO 1 over drawn bands: while
the mouse swept the plot for 15 seconds, 20 frames stalled for over 100 ms
(worst 2.5 s); the same animation with the mouse still had none. The stalls
were React work on the UI thread, in two layers:

  * Every pointer move stored a fresh hover object in the filter bank's React
    state, so every move re-rendered the 2,000-line filter bank and, because
    this document is a captured import, re-applied the whole document on
    commit (~42 ms per move, measured per event). With 64 bands nearly every
    move also crosses a band, so keeping only band changes in state was not
    enough. Hover is now not React state at all: the canvas and minimap read
    `hoverRef`, `renderAll` no longer depends on hover, and a move asks for a
    repaint.
  * The status pill (top centre) showed the hovered band's reading on plain
    hover, through an effect that published via the editor root's `onStatus`
    -- each publication re-applying the whole document (100-190 ms) -- and the
    pill then hid and re-showed through state every ~2.4 s while the pointer
    kept moving. The reading is now shown only while drawing (a band drag or
    the mute brush) and briefly after release; plain hover shows nothing and
    does no status work. While drawing, the live reading is written straight
    into the pill and restarts its hide countdown without a render.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const spectrHoverIdentity = "

EDITS = [
    (
        "moves inside one band only repaint",
        '''  const updatePointerHover = (next) => {
    const stamped = next ? { ...next, n: N } : next;
    hoverRef.current = stamped;
    if (!pointerRef.current || !pointerRef.current.mode) setHover(stamped);
  };''',
        '''  // Hover is paint-only: the canvas and minimap read hoverRef, so a move asks
  // for a repaint and never renders. See
  // tools/patch_materialized_hover_no_rerender.py.
  const updatePointerHover = (next) => {
    const stamped = next ? { ...next, n: N } : next;
    hoverRef.current = stamped;
    if (pointerRef.current && pointerRef.current.mode) return;
    wakeDraw();
  };''',
    ),
    (
        "minimap reads the hover ref",
        '''    const mmHover = hover && hover.mini ? hover.mini : null;''',
        '''    const mmHover = hoverRef.current && hoverRef.current.mini ? hoverRef.current.mini : null;''',
    ),
    (
        "renderAll no longer rebuilt per hover",
        '''  }, [view, N, bloom, spectrumIntensity, muteStyle, motionMode, metaphor, showMinimap, showRulers, theme, hover, selection, snapshots, morph, dspMode, visualizationMode]);''',
        '''  }, [view, N, bloom, spectrumIntensity, muteStyle, motionMode, metaphor, showMinimap, showRulers, theme, selection, snapshots, morph, dspMode, visualizationMode]);''',
    ),
    (
        "hover status is written, not rendered",
        '''  useEffect(() => {
    if (!onStatus) return;
    if (hoverBand < 0) {
      onStatus("");
      return;
    }
    const f = bandCenterFreq(hoverBand);
    const rendered = macroAdjustedGain(renderGainsRef.current[hoverBand], hoverBand);
    const gv = Number.isFinite(rendered) ? clamp(rendered, -1.02, 1.02) : 0;
    const db = isMuted(targetGainsRef.current[hoverBand]) ? "\\u2212\\u221E" : (gv * 24).toFixed(1);
    const label = `${window.SpectrFreq.fmt(f)}Hz   ${db}${db === "\\u2212\\u221E" ? "" : " dB"}   BAND ${hoverBand + 1}/${N}`;
    onStatus(label);
  }, [hoverBand, N, onStatus]);''',
        '''  // The status pill shows a band reading only while drawing (see
  // updateLiveHoverStatus); plain hover publishes nothing.''',
    ),
    (
        "the banner can be kept up without a render",
        '''  const timerRef = useRefChrome(null);
  useEffectChrome(() => {
    ++generationRef.current;''',
        '''  const timerRef = useRefChrome(null);
  // Restart the hide countdown of the banner that is showing, without a
  // render: hover readings written straight into the banner call this so it
  // stays up while the pointer moves across bands. Hiding is a render (and a
  // whole-document re-apply), and so is bringing it back.
  window.spectrStatusBannerKeepAlive = (holdMs = 2200) => {
    if (timerRef.current === null || !shownRef.current) return false;
    clearTimeout(timerRef.current);
    timerRef.current = setTimeout(() => {
      timerRef.current = null;
      setVisible(false);
      setText("");
      shownRef.current = "";
    }, holdMs);
    return true;
  };
  useEffectChrome(() => {
    ++generationRef.current;''',
    ),
    (
        "the live reading shows only while drawing",
        '''  const updateLiveHoverStatus = () => {
    const current = hoverRef.current;
    const pointer = pointerRef.current;
    if (hoverBandOf(current) < 0) return;''',
        '''  const updateLiveHoverStatus = () => {
    const current = hoverRef.current;
    const pointer = pointerRef.current;
    // The pill reads out a band only while it is being drawn; release shows
    // the final value (onPointerUp) and the pill fades on its own.
    if (!pointer || (pointer.mode !== "gain" && pointer.mode !== "mute-brush")) return;
    if (hoverBandOf(current) < 0) return;''',
    ),
    (
        "drawing keeps the pill up",
        '''    if (shown && text) {
      text.textContent = label;''',
        '''    if (shown && text) {
      if (typeof window.spectrStatusBannerKeepAlive === "function") window.spectrStatusBannerKeepAlive();
      text.textContent = label;''',
    ),
    (
        "a finished drag does not render hover",
        '''      setGains(targetGainsRef.current.slice());
      setHover(hoverRef.current);''',
        '''      setGains(targetGainsRef.current.slice());''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("hover no-rerender already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("hover no-rerender applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
