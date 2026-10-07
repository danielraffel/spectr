#!/usr/bin/env python3
"""Suppress hover help during interaction and while a popup owns the editor."""
import json
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / 'native-ui/materialized/materialized-document.runtime.json'
MARKER = '__spectrTooltipInteraction'
EDITS = [
('    globalThis.spectrHeaderTipHide = () => { cancel(); setTip(null); };', '''    // __spectrTooltipInteraction: a commit can synthesize another enter on
    // the same control. A press invalidates hover intent until the pointer
    // actually moves; simply restarting the 500 ms timer would show help
    // again over LIVE/FROZEN or behind the dropdown the press just opened.
    let pointer = null;
    let pressedAt = null;
    let suppressed = false;
    const popupOpen = () => !!document.querySelector(
      '[data-spectr-overlay="true"], [aria-modal="true"], [aria-haspopup][aria-expanded="true"]');
    const hide = () => { cancel(); setTip(null); };
    const press = (event) => {
      suppressed = true;
      const x = Number(event && event.clientX), y = Number(event && event.clientY);
      pressedAt = Number.isFinite(x) && Number.isFinite(y) ? { x, y } : pointer;
      hide();
    };
    const move = (event) => {
      const x = Number(event && event.clientX), y = Number(event && event.clientY);
      if (!Number.isFinite(x) || !Number.isFinite(y)) return;
      pointer = { x, y };
      if (suppressed && pressedAt
          && Math.hypot(x - pressedAt.x, y - pressedAt.y) >= 4) {
        suppressed = false;
        pressedAt = null;
      }
    };
    document.addEventListener("pointerdown", press, true);
    document.addEventListener("click", press, true);
    document.addEventListener("contextmenu", press, true);
    document.addEventListener("pointermove", move, true);
    document.addEventListener("wheel", hide, true);
    document.addEventListener("keydown", press, true);
    globalThis.spectrHeaderTipHide = hide;'''),
('''      cancel();
      setTip(null);
      const cluster = document.querySelector("[data-spectr-output-cluster]");''', '''      cancel();
      setTip(null);
      if (suppressed || popupOpen()) return;
      const cluster = document.querySelector("[data-spectr-output-cluster]");'''),
('''        tipTimerRef.current = 0;
        if (globalThis.__spectrHeaderTipRequest) globalThis.__spectrHeaderTipRequest.shown = true;''', '''        tipTimerRef.current = 0;
        if (suppressed || popupOpen() || globalThis.__spectrShowTooltips === false) return;
        if (globalThis.__spectrHeaderTipRequest) globalThis.__spectrHeaderTipRequest.shown = true;'''),
('''      cancel();
      globalThis.spectrHeaderTip = void 0;''', '''      cancel();
      document.removeEventListener("pointerdown", press, true);
      document.removeEventListener("click", press, true);
      document.removeEventListener("contextmenu", press, true);
      document.removeEventListener("pointermove", move, true);
      document.removeEventListener("wheel", hide, true);
      document.removeEventListener("keydown", press, true);
      globalThis.spectrHeaderTip = void 0;'''),
]

def main():
    raw = PATH.read_text()
    html = json.loads(raw)['html']
    if MARKER in html:
        print('tooltip interaction already applied')
        return
    for old, new in EDITS:
        if html.count(old) != 1:
            raise SystemExit(f'FAIL: ambiguous tooltip anchor: {old[:65]}')
        encoded = lambda s: json.dumps(s, ensure_ascii=False)[1:-1]
        raw = raw.replace(encoded(old), encoded(new), 1)
        html = html.replace(old, new, 1)
    PATH.write_text(raw)
    print('tooltip interaction applied')

if __name__ == '__main__':
    main()
