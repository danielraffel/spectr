#!/usr/bin/env python3
"""Pressing and releasing a band no longer re-renders the editor.

Measured with Perfetto on the shipping standalone, with and without audio:
the press and the release of a band-drawing gesture cost 88-124 ms each -- a
dom_event_dispatch of 52-88 ms plus a ~36 ms microtask pump, all React. This
document is a captured import, so every commit re-applies the whole document
(~44 ms). The commits were:

  * the status pill. Press, release and a click's MUTED/UNMUTED reading all
    went through the bank's `onStatus`, which is the App's state: a root
    commit, then the banner's own commit to show the text. The banner now
    exposes `spectrStatusBannerShow`, which writes the message, its geometry
    and the shown styling straight into the pill and arms the same hide
    countdown; a message shown that way is hidden the same way. React's own
    path is untouched for every other status message.
  * the release's `setGains(targetGainsRef.current.slice())`. React's `gains`
    is only a mirror -- the painters and the native publication read
    `targetGainsRef` -- and the release re-rendered the whole bank to refresh
    it. The mirror went stale anyway (a live projection writes the refs with
    no setter), and the two functional setters that built on it could then
    overwrite the drawn gains with the stale copy. Every setter now writes the
    mirror FROM the ref, the band-count resize reads the ref, and the release
    (and a click's mute toggle, which is now deferred like a drawn gain)
    settles the publication flags its render used to settle.

Undo grouping (undo_gesture_start/end), macro drag brackets and every native
publication are unchanged.

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "window.spectrStatusBannerShow = "

EDITS = [
    (
        "commitGain writes the mirror from the ref",
        '''    if (!deferReact) setGains((prev) => {
      const nxt = prev.slice();
      nxt[idx] = value;
      return nxt;
    });
''',
        '''    // The mirror is written FROM the ref. Building on `prev` would bring a
    // stale mirror's values back through the [gains] effect and overwrite
    // what was drawn since. See tools/patch_materialized_band_press_cost.py.
    if (!deferReact) setGains(targetGainsRef.current.slice());
''',
    ),
    (
        "commitMany writes the mirror from the ref",
        '''    if (!deferReact) setGains((prev) => {
      const nextState = prev.slice();
      for (const [k, v] of map) nextState[k] = v;
      return nextState;
    });
''',
        '''    if (!deferReact) setGains(nextTarget.slice());
''',
    ),
    (
        "the band-count resize reads what is drawn",
        '''    setGains((prev) => {
      if (prev.length === N) return prev;
      const next = new Array(N).fill(0);
      const nextMutedGainDb = new Array(N).fill(0);
      const previousMutedGainDb = mutedGainDbRef.current;
      for (let i = 0; i < N; i++) {
        const j = Math.floor(i / N * prev.length);
        next[i] = prev[j] ?? 0;
        nextMutedGainDb[i] = previousMutedGainDb[j] ?? 0;
      }
''',
        '''    setGains((prev) => {
      if (prev.length === N) return prev;
      // Resampled from the ref: React's copy is a mirror a deferred edit
      // leaves behind, and the ref is what is drawn and published.
      const base = targetGainsRef.current;
      const next = new Array(N).fill(0);
      const nextMutedGainDb = new Array(N).fill(0);
      const previousMutedGainDb = mutedGainDbRef.current;
      for (let i = 0; i < N; i++) {
        const j = Math.floor(i / N * base.length);
        next[i] = base[j] ?? 0;
        nextMutedGainDb[i] = previousMutedGainDb[j] ?? 0;
      }
''',
    ),
    (
        "the bank shows status straight into the pill",
        '''  const updateLiveHoverStatus = () => {''',
        '''  // Press and release write their reading straight into the status pill.
  // Through `onStatus` each was an App commit plus the banner's own -- in
  // this captured import ~44 ms apiece, on every press and every release.
  // Falls back to `onStatus` where no banner is mounted.
  const showStatus = (message) => {
    if (typeof window.spectrStatusBannerShow === "function"
        && window.spectrStatusBannerShow(message)) return;
    if (onStatus) onStatus(message);
  };
  const updateLiveHoverStatus = () => {''',
    ),
    (
        "a release settles without a render",
        '''    if (p && (p.mode === "gain" || p.mode === "mute-brush")) {
      setGains(targetGainsRef.current.slice());
      if (onStatus) onStatus(liveHoverLabel(hoverRef.current));
    }
''',
        '''    if (p && (p.mode === "gain" || p.mode === "mute-brush")) {
      // No render to refresh React's `gains`: the stroke already published
      // every sample to native, nothing on screen reads the mirror, and every
      // setter now writes it from the ref. What the release's render used to
      // do through the publication effect is done here: the edit is settled.
      nativeProjectionRef.current = false;
      nativeEditPendingRef.current = false;
      showStatus(liveHoverLabel(hoverRef.current));
    }
''',
    ),
    (
        "a click's mute toggle is deferred like a drawn gain",
        '''      commitGain(p.band, isMuted(cur) ? restored : -Infinity);
      if (onStatus) onStatus(isMuted(cur) ? `BAND ${p.band + 1} UNMUTED` : `BAND ${p.band + 1} MUTED`);
''',
        '''      // Deferred, like the mute brush: the painters read the refs and the
      // toggle is published by commitGain itself.
      commitGain(p.band, isMuted(cur) ? restored : -Infinity, true);
      nativeProjectionRef.current = false;
      nativeEditPendingRef.current = false;
      wakeDraw();
      showStatus(isMuted(cur) ? `BAND ${p.band + 1} UNMUTED` : `BAND ${p.band + 1} MUTED`);
''',
    ),
    (
        "a mute-brush press shows its reading directly",
        '''      brush.paint(band);
      pointerRef.current = brush;
      hoverRef.current = { band, x, y, n: N };
      if (onStatus) onStatus(liveHoverLabel(hoverRef.current));
''',
        '''      brush.paint(band);
      pointerRef.current = brush;
      hoverRef.current = { band, x, y, n: N };
      showStatus(liveHoverLabel(hoverRef.current));
''',
    ),
    (
        "a drawing press shows its reading directly",
        '''      postNative("macro_drag_start", {});
    hoverRef.current = { band, x, y, n: N };
    if (onStatus) onStatus(liveHoverLabel(hoverRef.current));
''',
        '''      postNative("macro_drag_start", {});
    hoverRef.current = { band, x, y, n: N };
    showStatus(liveHoverLabel(hoverRef.current));
''',
    ),
    (
        "the banner can show and hide a message without a render",
        '''  const [visible, setVisible] = useStateChrome(false);
  const [text, setText] = useStateChrome("");
  const shownRef = useRefChrome("");
''',
        '''  const [visible, setVisible] = useStateChrome(false);
  const [text, setText] = useStateChrome("");
  const shownRef = useRefChrome("");
  // True while the pill carries a message written by spectrStatusBannerShow
  // rather than rendered, so hiding it knows to undo those writes.
  const directRef = useRefChrome(false);
  const disabledRef = useRefChrome(disabled);
  disabledRef.current = disabled;
  const statusNodes = () => ({
    shell: document.querySelector("[data-spectr-status-shell]"),
    textNode: document.querySelector("[data-spectr-status-text]"),
  });
  // The shown and hidden looks, written directly. The same values the render
  // below derives from `text`, so either writer leaves the pill identical.
  const paintStatus = (shell, textNode, display) => {
    const on = !!display;
    textNode.textContent = display;
    if (on) shell.setAttribute("data-spectr-status-banner", "true");
    else if (typeof shell.removeAttribute === "function")
      shell.removeAttribute("data-spectr-status-banner");
    const style = shell.style;
    style.background = on ? "rgba(12,16,22,0.92)" : "transparent";
    style.border = "1px solid " + (on ? "rgba(180,210,255,0.3)" : "transparent");
    style.color = on ? "rgba(200,220,255,0.95)" : "transparent";
    style.opacity = on ? 1 : 0;
    style.backdropFilter = on ? "blur(8px)" : "none";
    spectrPlaceStatusBanner(shell, spectrStatusBannerWidth(display));
  };
  // Hide whatever is up. React's copy goes through React -- a setter that
  // resolves to the value already held costs nothing -- and a direct message
  // is taken down directly, so neither path renders on the other's behalf.
  const hideStatus = () => {
    timerRef.current = null;
    shownRef.current = "";
    if (directRef.current) {
      directRef.current = false;
      const { shell, textNode } = statusNodes();
      if (shell && textNode) paintStatus(shell, textNode, "");
    }
    setVisible(false);
    setText("");
  };
  // Show a message without a render: the bank's press and release readings.
  // Answers false where there is no pill to write into, so the caller can
  // publish through React instead. With status info off there is nothing to
  // show, which is handled, not refused.
  window.spectrStatusBannerShow = (display) => {
    if (disabledRef.current) return true;
    const { shell, textNode } = statusNodes();
    if (!shell || !textNode || typeof shell.setAttribute !== "function")
      return false;
    if (timerRef.current !== null) clearTimeout(timerRef.current);
    if (!display) {
      timerRef.current = shownRef.current ? setTimeout(hideStatus, 160) : null;
      return true;
    }
    paintStatus(shell, textNode, display);
    directRef.current = true;
    shownRef.current = display;
    const hold = /\\b(?:MUTED|UNMUTED)\\b/.test(display) ? 2800 : 2200;
    timerRef.current = setTimeout(hideStatus, hold);
    return true;
  };
''',
    ),
    (
        "a rendered message over a direct one is written through",
        '''    setText(display);
    setVisible(true);
    shownRef.current = display;
''',
        '''    setText(display);
    setVisible(true);
    shownRef.current = display;
    // A direct message may be on screen with React's copy still holding an
    // older one, and a setter that resolves to that older value renders
    // nothing. Write this one through so the pill cannot keep the direct text.
    if (directRef.current) {
      const { shell, textNode } = statusNodes();
      if (shell && textNode) paintStatus(shell, textNode, display);
    }
''',
    ),
    (
        "keep-alive hides through the one path",
        '''    timerRef.current = setTimeout(() => {
      timerRef.current = null;
      setVisible(false);
      setText("");
      shownRef.current = "";
    }, holdMs);
    return true;
  };''',
        '''    timerRef.current = setTimeout(hideStatus, holdMs);
    return true;
  };''',
    ),
    (
        "the countdown hides through the one path",
        '''    const arm = (delay) => {
      cancel();
      timerRef.current = setTimeout(() => {
        timerRef.current = null;
        setVisible(false);
        setText("");
        shownRef.current = "";
      }, delay);
    };''',
        '''    const arm = (delay) => {
      cancel();
      timerRef.current = setTimeout(hideStatus, delay);
    };''',
    ),
    (
        "turning status info off takes a direct message down too",
        '''    if (disabled) {
      cancel();
      setVisible(false);
      setText("");
      shownRef.current = "";
      return;
    }''',
        '''    if (disabled) {
      cancel();
      hideStatus();
      return;
    }''',
    ),
    (
        "the placement watch sizes the pill for what it shows",
        '''  useEffectChrome(() => {
    const node = document.querySelector("[data-spectr-status-shell]");
    spectrPlaceStatusBanner(node, bannerWidth);''',
        '''  useEffectChrome(() => {
    const node = document.querySelector("[data-spectr-status-shell]");
    // A directly shown message is not in React's `text`; size for it.
    const placedWidth = directRef.current
      ? spectrStatusBannerWidth(shownRef.current) : bannerWidth;
    spectrPlaceStatusBanner(node, placedWidth);''',
    ),
    (
        "the watch re-places at that width",
        '''      if (!rect || Math.abs(rect.top - spectrStatusBannerTop()) > 0.5)
        spectrPlaceStatusBanner(node, bannerWidth);
      else if (rect.width > 0
          && Math.abs(rect.width - bannerWidth) > 0.5)''',
        '''      if (!rect || Math.abs(rect.top - spectrStatusBannerTop()) > 0.5)
        spectrPlaceStatusBanner(node, placedWidth);
      else if (rect.width > 0
          && Math.abs(rect.width - placedWidth) > 0.5)''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("band press cost already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("band press cost applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
