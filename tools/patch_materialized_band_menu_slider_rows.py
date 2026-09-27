#!/usr/bin/env python3
"""Value ranges in the band menu are sliders, not tap-to-cycle rows.

The Modulation submenu's Rate and Depth rows stepped through a fixed list on
every tap (Depth 0 -> 25 -> 50 -> 75 -> 100%), so reaching a value meant
tapping past everything before it, and nothing between the stops could be
set. They are now slider rows: drag the track (a press anywhere on it jumps
there), and the live value is printed beside it.

  * Depth is continuous, 0-100% in 1% steps.
  * Rate snaps to the musical rates the LFO offers (1/4 .. 16 beats); the
    slider moves over that list rather than over raw beats.
  * Shape is a choice between four waveforms, not a range, so it stays a
    tap-to-cycle row.

The whole row is ONE keyboard stop (`role="slider"` with ARIA values): with
the cursor on it, Left/Right adjust the value instead of leaving the submenu,
and a hover moves the cursor there like any other row. It uses the same
drag mechanics as the Settings slider (pointer capture on the track).

Requires patch_materialized_band_menu_cursor_order.py.
Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const SliderRow = "

EDITS = [
    (
        "slider row component",
        '''  const Divider = ({ label, rule = true }) =>''',
        '''  // A value row: drag the track, or put the keyboard cursor on the row and
  // use Left/Right. Called as a plain function, like Item, so the row keeps
  // its DOM node across renders. See
  // tools/patch_materialized_band_menu_slider_rows.py.
  const sliderDragRef = React.useRef(null);
  const SliderRow = ({ key, action, label, value, min, max, step, fmt, disabled, onChange }) => {
    const span = (max - min) || 1;
    const fraction = Math.max(0, Math.min(1, (value - min) / span));
    const settle = (raw) => {
      const snapped = step ? Math.round((raw - min) / step) * step + min : raw;
      return Math.max(min, Math.min(max, snapped));
    };
    const commit = (event, track) => {
      if (disabled || !track || typeof track.getBoundingClientRect !== "function") return;
      const box = track.getBoundingClientRect();
      if (!box || !(box.width > 0)) return;
      const x = (event.clientX === undefined ? box.left : event.clientX) - box.left;
      const next = settle(min + Math.max(0, Math.min(1, x / box.width)) * span);
      if (Number.isFinite(next) && next !== value) onChange(next);
    };
    return React.createElement("div", {
      key,
      role: "slider",
      "aria-label": label,
      "aria-valuemin": min,
      "aria-valuemax": max,
      "aria-valuenow": value,
      "aria-valuetext": fmt(value),
      "aria-disabled": disabled ? "true" : "false",
      "data-spectr-band-action": action,
      ref: (node) => {
        if (!node) return;
        node.__spectrSliderStep = (direction) => {
          if (disabled) return;
          const next = settle(value + direction * (step || span / 100));
          if (next !== value) onChange(next);
        };
      },
      onMouseEnter: (e) => {
        if (disabled) return;
        const node = e.currentTarget;
        hoverIntent(syncHover(node) || levelOf(node), action, null);
      },
      onMouseLeave: (e) => {
        e.currentTarget.style.background = "transparent";
      },
      style: {
        display: "flex", alignItems: "center", gap: 10, width: "100%",
        padding: "6px 12px",
        color: disabled ? "rgba(255,255,255,0.25)" : "rgba(255,255,255,0.88)",
        fontFamily: "var(--mono)", fontSize: 10.5, letterSpacing: 0.3
      }
    },
      React.createElement("span", { style: { width: 40, flexShrink: 0 } }, label),
      React.createElement("div", {
        "data-spectr-menu-slider-track": action,
        onPointerDown: (event) => {
          sliderDragRef.current = event && event.currentTarget ? event.currentTarget : null;
          const track = sliderDragRef.current;
          if (track && track.setPointerCapture && event.pointerId !== undefined) {
            try { track.setPointerCapture(event.pointerId); } catch (err) {}
          }
          commit(event, track);
        },
        onPointerMove: (event) => {
          if (sliderDragRef.current === event.currentTarget) commit(event, event.currentTarget);
        },
        onPointerUp: () => { sliderDragRef.current = null; },
        onPointerCancel: () => { sliderDragRef.current = null; },
        onLostPointerCapture: () => { sliderDragRef.current = null; },
        style: { position: "relative", flex: 1, height: 16, cursor: disabled ? "default" : "pointer", hitSlop: "8 6" }
      },
        React.createElement("div", { style: { position: "absolute", left: 0, top: 6, width: "100%", height: 4, borderRadius: 2, pointerEvents: "none", background: "rgba(255,255,255,0.14)" } }),
        React.createElement("div", { style: { position: "absolute", left: 0, top: 6, height: 4, borderRadius: 2, pointerEvents: "none", background: disabled ? "rgba(255,255,255,0.2)" : "hsl(200,80%,60%)", width: (100 * fraction) + "%" } }),
        React.createElement("div", { "data-spectr-menu-slider-thumb": action, style: { position: "absolute", top: 2, width: 12, height: 12, borderRadius: 6, pointerEvents: "none", background: disabled ? "rgba(255,255,255,0.35)" : "#fff", border: "1px solid rgba(0,0,0,0.35)", left: (100 * fraction) + "%", marginLeft: -12 * fraction } })
      ),
      React.createElement("span", { className: "tnum", style: { width: 58, flexShrink: 0, textAlign: "right", opacity: 0.55, fontSize: 9.5 } }, fmt(value))
    );
  };
  const Divider = ({ label, rule = true }) =>''',
    ),
    (
        "rate row is a slider",
        '''        Item({
          action: modulationSource === 1 ? "lfo1-rate" : "lfo2-rate", label: "Rate", keepOpen: true,
          sub: (modulationSource === 1 ? modulation.rate : modulation.lfo2Rate) + " beats",
          disabled: !modulationReady,
          onClick: () => {
            const key = modulationSource === 1 ? "rate" : "lfo2Rate";
            publishModulation(key, modulationSource === 1 ? 4002 : 4012,
              spectrCycleModulationValue(spectrModulationRates, modulation[key]));
          }
        }),''',
        '''        (() => {
          // Rate moves over the musical rates, not raw beats: the slider's
          // value is an index into that list.
          const key = modulationSource === 1 ? "rate" : "lfo2Rate";
          const current = modulation[key];
          let index = 0;
          spectrModulationRates.forEach((rate, i) => {
            if (Math.abs(rate - current) < Math.abs(spectrModulationRates[index] - current)) index = i;
          });
          return SliderRow({
            action: modulationSource === 1 ? "lfo1-rate" : "lfo2-rate", label: "Rate",
            value: index, min: 0, max: spectrModulationRates.length - 1, step: 1,
            fmt: (i) => spectrModulationRates[i] + " beats",
            disabled: !modulationReady,
            onChange: (i) => publishModulation(key, modulationSource === 1 ? 4002 : 4012,
              spectrModulationRates[i])
          });
        })(),''',
    ),
    (
        "depth row is a slider",
        '''        Item({
          action: modulationSource === 1 ? "lfo1-depth" : "lfo2-depth", label: "Depth", keepOpen: true,
          sub: Math.round((modulationSource === 1 ? modulation.depth : modulation.lfo2Depth) * 100) + "%",
          disabled: !modulationReady,
          onClick: () => {
            const key = modulationSource === 1 ? "depth" : "lfo2Depth";
            publishModulation(key, modulationSource === 1 ? 4003 : 4013,
              spectrCycleModulationValue(spectrModulationDepths, modulation[key]));
          }
        }),''',
        '''        SliderRow({
          action: modulationSource === 1 ? "lfo1-depth" : "lfo2-depth", label: "Depth",
          value: modulationSource === 1 ? modulation.depth : modulation.lfo2Depth,
          min: 0, max: 1, step: 0.01,
          fmt: (v) => Math.round(v * 100) + "%",
          disabled: !modulationReady,
          onChange: (v) => publishModulation(modulationSource === 1 ? "depth" : "lfo2Depth",
            modulationSource === 1 ? 4003 : 4013, Math.round(v * 100) / 100)
        }),''',
    ),
    (
        "slider rows are cursor stops",
        '''    const buttons = new Set(document.querySelectorAll("button"));''',
        '''    const buttons = new Set([...document.querySelectorAll("button"),
                             ...document.querySelectorAll('[role="slider"]')]);''',
    ),
    (
        "slider rows light like menu rows",
        '''        const menuRow = role === "menuitem" || role === "menuitemcheckbox";''',
        '''        const menuRow = role === "menuitem" || role === "menuitemcheckbox" || role === "slider";''',
    ),
    (
        "left and right adjust a slider row",
        '''      if (key === "ArrowRight") {
        consume();
        if (opensLevel) enterSubmenu(opensLevel);
        return;
      }''',
        '''      // On a slider row Left/Right adjust its value; everywhere else they
      // enter and leave submenus.
      if ((key === "ArrowRight" || key === "ArrowLeft") && row
          && row.getAttribute && row.getAttribute("role") === "slider") {
        consume();
        if (typeof row.__spectrSliderStep === "function")
          row.__spectrSliderStep(key === "ArrowRight" ? 1 : -1);
        return;
      }
      if (key === "ArrowRight") {
        consume();
        if (opensLevel) enterSubmenu(opensLevel);
        return;
      }''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("band menu slider rows already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("band menu slider rows applied (Rate, Depth)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
