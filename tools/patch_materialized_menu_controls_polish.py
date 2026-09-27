#!/usr/bin/env python3
"""Menu controls read as controls, and the guide reads as prose.

  * Shortcut chips are centred boxes. The chip was a bare text span with
    padding, so the glyph sat where the text line box put it -- measured low
    and right in the one-letter Edit-mode chips, high in `Cmd+Shift+P`. It
    is now a fixed-height flex box that centres its text on both axes.
  * RESET ALL's description is a second line under the label, not a run
    beside it that wrapped mid-phrase in the overflow menu.
  * The band menu's slider thumb is the Settings pill (22x14), not a disc.
  * LFO 1 / LFO 2 are switches: the row keeps its On/Off word and ends in
    the same track-and-knob the Settings toggles draw.
  * Shape shows all four waveforms as glyphs; one press picks one. The row
    is still ONE keyboard stop (`role="slider"` over the shape index), so
    Left/Right cycle shapes exactly as they adjust Rate and Depth. No
    dropdown opens inside the menu.
  * The About guide breaks its own lines. Pulp has no inline flow, so a
    paragraph led by a `**bold**` term used to become two flex boxes: the
    term, and the rest of the sentence wrapping beside it with a hanging
    indent. Lines are now measured with the canvas text metrics (the Skia
    measurer the renderer draws with) and emitted one per box, the first
    carrying the bold term, so continuation lines start at the margin. The
    scroll range is the sum of those lines, which also removes the blank
    screenful the old over-estimating height left below the last paragraph.

Requires patch_materialized_band_menu_slider_rows.py.
Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "function spectrHelpLines("

EDITS = [
    (
        "shortcut chip is a centred box",
        '''function spectrShortcutChipStyle() {
  return {
    fontFamily: "var(--mono)",
    fontSize: 9.5,
    letterSpacing: 0.8,
    opacity: 0.78,
    padding: "2px 7px",
    border: "1px solid rgba(255,255,255,0.14)",
    borderRadius: 2,
    whiteSpace: "nowrap",
    flexShrink: 0
  };
}''',
        '''function spectrShortcutChipStyle() {
  // A box that centres its text, not a text span with padding: the padded
  // span put the glyph wherever its line box did, which measured low and
  // right in a one-letter chip and high in a long one.
  return {
    fontFamily: "var(--mono)",
    fontSize: 9.5,
    letterSpacing: 0.8,
    lineHeight: "14px",
    textAlign: "center",
    opacity: 0.78,
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    boxSizing: "border-box",
    height: 16,
    minWidth: 18,
    padding: "0 6px",
    border: "1px solid rgba(255,255,255,0.14)",
    borderRadius: 2,
    whiteSpace: "nowrap",
    flexShrink: 0
  };
}''',
    ),
    (
        "reset all description on its own line",
        '''  }, style: menuItem }, "RESET ALL ", /* @__PURE__ */ React.createElement("span", { style: { opacity: 0.4, marginLeft: 8 } }, "gains \\xB7 view \\xB7 snapshots")),''',
        '''  }, style: { ...menuItem, flexDirection: "column", alignItems: "flex-start", gap: 3 } }, /* @__PURE__ */ React.createElement("span", null, "RESET ALL"), /* @__PURE__ */ React.createElement("span", { style: { opacity: 0.4, fontSize: 9.5, whiteSpace: "nowrap" } }, "gains \\xB7 view \\xB7 snapshots")),''',
    ),
    (
        "menu slider thumb is the settings pill",
        '''React.createElement("div", { "data-spectr-menu-slider-thumb": action, style: { position: "absolute", top: 2, width: 12, height: 12, borderRadius: 6, pointerEvents: "none", background: disabled ? "rgba(255,255,255,0.35)" : "#fff", border: "1px solid rgba(0,0,0,0.35)", left: (100 * fraction) + "%", marginLeft: -12 * fraction } })''',
        '''React.createElement("div", { "data-spectr-menu-slider-thumb": action, style: { position: "absolute", top: 1, width: 22, height: 14, borderRadius: 7, pointerEvents: "none", background: disabled ? "rgba(255,255,255,0.35)" : "#fff", border: "1px solid rgba(0,0,0,0.35)", left: (100 * fraction) + "%", marginLeft: -22 * fraction } })''',
    ),
    (
        "item takes a switch",
        '''const Item = ({ key, action, label, hint, onClick, onKeyDown, onHover, disabled, danger, sub, keepOpen, checked, expanded, hasPopup }) =>''',
        '''const Item = ({ key, action, label, hint, onClick, onKeyDown, onHover, disabled, danger, sub, keepOpen, checked, expanded, hasPopup, toggle }) =>''',
    ),
    (
        "switch renders after the sub text",
        '''    sub && /* @__PURE__ */ React.createElement("span", { style: { opacity: 0.45, fontSize: 9.5 } }, sub),
    hint && /* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": "band", style: spectrShortcutChipStyle() }, hint)
  );''',
        '''    sub && /* @__PURE__ */ React.createElement("span", { style: { opacity: 0.45, fontSize: 9.5 } }, sub),
    // The Settings switch, drawn: the whole row is still the one button, so
    // a press anywhere on it toggles and the keyboard cursor treats it like
    // any other row.
    toggle !== undefined && /* @__PURE__ */ React.createElement("span", {
      "data-spectr-menu-switch": toggle ? "on" : "off",
      style: {
        position: "relative", width: 30, height: 16, borderRadius: 8, flexShrink: 0,
        pointerEvents: "none",
        background: disabled ? "rgba(255,255,255,0.06)" : toggle ? "hsl(200,70%,45%)" : "rgba(255,255,255,0.1)",
        border: "1px solid rgba(255,255,255,0.1)"
      }
    }, /* @__PURE__ */ React.createElement("span", { style: {
      position: "absolute", top: 1, left: toggle ? 15 : 1, width: 12, height: 12, borderRadius: 6,
      pointerEvents: "none", background: disabled ? "rgba(255,255,255,0.3)" : "#fff"
    } })),
    hint && /* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": "band", style: spectrShortcutChipStyle() }, hint)
  );
  // A choice among waveforms, shown rather than cycled: every shape is a
  // glyph and one press picks it. It is a slider over the shape index for
  // the keyboard, so Left/Right step through the shapes on the one stop.
  const spectrShapeGlyphs = [
    "M1 6 C4 -1.5 7 -1.5 10 6 C13 13.5 16 13.5 19 6",
    "M1 6 L5.5 1.5 L14.5 10.5 L19 6",
    "M1 10 L1 2 L10 2 L10 10 L19 10 L19 2",
    "M1 10 L10 2 L10 10 L19 2 L19 10"
  ];
  const ShapeRow = ({ key, action, value, names, disabled, onChange }) => React.createElement("div", {
    key,
    role: "slider",
    "aria-label": "Shape",
    "aria-valuemin": 0,
    "aria-valuemax": names.length - 1,
    "aria-valuenow": value,
    "aria-valuetext": names[value],
    "aria-disabled": disabled ? "true" : "false",
    "data-spectr-band-action": action,
    ref: (node) => {
      if (!node) return;
      node.__spectrSliderStep = (direction) => {
        if (disabled) return;
        onChange((value + direction + names.length) % names.length);
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
      padding: "5px 12px",
      color: disabled ? "rgba(255,255,255,0.25)" : "rgba(255,255,255,0.88)",
      fontFamily: "var(--mono)", fontSize: 10.5, letterSpacing: 0.3
    }
  },
    React.createElement("span", { style: { width: 40, flexShrink: 0 } }, "Shape"),
    React.createElement("div", { style: { display: "flex", gap: 4, flex: 1 } },
      names.map((name, index) => {
        const picked = index === value;
        return React.createElement("div", {
          key: name,
          "data-spectr-shape-option": index,
          "aria-label": name,
          "aria-pressed": picked ? "true" : "false",
          onPointerDown: () => { if (!disabled && !picked) onChange(index); },
          style: {
            flex: 1, height: 20, borderRadius: 3, boxSizing: "border-box",
            display: "flex", alignItems: "center", justifyContent: "center",
            cursor: disabled ? "default" : "pointer",
            border: "1px solid " + (picked ? "rgba(180,210,255,0.5)" : "rgba(255,255,255,0.1)"),
            background: picked ? "rgba(120,180,255,0.2)" : "rgba(255,255,255,0.03)"
          }
        }, React.createElement("svg", { width: 20, height: 12, viewBox: "0 0 20 12", style: { pointerEvents: "none" } },
          React.createElement("path", {
            d: spectrShapeGlyphs[index] || spectrShapeGlyphs[0],
            stroke: disabled ? "rgba(255,255,255,0.25)" : picked ? "hsl(200,85%,75%)" : "rgba(255,255,255,0.55)",
            strokeWidth: "1.4", fill: "none", strokeLinecap: "round", strokeLinejoin: "round"
          })));
      })
    ),
    React.createElement("span", { className: "tnum", style: { width: 44, flexShrink: 0, textAlign: "right", opacity: 0.55, fontSize: 9.5 } }, names[value])
  );''',
    ),
    (
        "lfo 1 is a switch",
        '''Item({ action: "lfo1-enable", label: "LFO 1", sub: modulation.enabled ? "On" : "Off", checked: modulation.enabled,''',
        '''Item({ action: "lfo1-enable", label: "LFO 1", sub: modulation.enabled ? "On" : "Off", toggle: !!modulation.enabled, checked: modulation.enabled,''',
    ),
    (
        "lfo 2 is a switch",
        '''Item({ action: "lfo2-enable", label: "LFO 2", sub: modulation.lfo2Enabled ? "On" : "Off", checked: modulation.lfo2Enabled,''',
        '''Item({ action: "lfo2-enable", label: "LFO 2", sub: modulation.lfo2Enabled ? "On" : "Off", toggle: !!modulation.lfo2Enabled, checked: modulation.lfo2Enabled,''',
    ),
    (
        "shape row shows every waveform",
        '''        Item({
          action: modulationSource === 1 ? "lfo1-shape" : "lfo2-shape", label: "Shape", keepOpen: true,
          sub: spectrModulationShapes[modulationSource === 1 ? modulation.shape : modulation.lfo2Shape],
          disabled: !modulationReady,
          onClick: () => {
            const key = modulationSource === 1 ? "shape" : "lfo2Shape";
            publishModulation(key, modulationSource === 1 ? 4001 : 4011,
              spectrCycleModulationValue([0, 1, 2, 3], modulation[key]));
          }
        }),''',
        '''        ShapeRow({
          action: modulationSource === 1 ? "lfo1-shape" : "lfo2-shape",
          value: Math.max(0, Math.min(spectrModulationShapes.length - 1,
            Math.round(modulationSource === 1 ? modulation.shape : modulation.lfo2Shape) || 0)),
          names: spectrModulationShapes,
          disabled: !modulationReady,
          onChange: (index) => publishModulation(modulationSource === 1 ? "shape" : "lfo2Shape",
            modulationSource === 1 ? 4001 : 4011, index)
        }),''',
    ),
    (
        "guide measures its own lines",
        '''function spectrHelpContentHeight(blocks, textW) {''',
        '''// Width of a run in the guide's body face, from the same Skia measurer the
// renderer draws with. The estimate is only a fallback for a host without it.
function spectrHelpMeasure(text, bold) {
  if (typeof canvasMeasureText === "function") {
    var metrics = canvasMeasureText("", text, "Inter", 11.5);
    if (metrics && metrics.width > 0)
      return metrics.width * (bold ? 1.06 : 1) + text.length * 0.1;
  }
  return text.length * 6.6;
}
// Break a paragraph into the lines it will occupy, the first carrying its
// bold lead term. Pulp has no inline flow, so a bold term and the sentence it
// starts cannot share a wrapping line box: left to layout, the term becomes
// one flex box and the rest wraps BESIDE it with a hanging indent. Breaking
// here lets every continuation line start at the margin.
function spectrHelpLines(text, width) {
  var lead = null;
  var rest = text;
  if (text.slice(0, 2) === "**") {
    var close = text.indexOf("**", 2);
    if (close > 2) { lead = text.slice(2, close); rest = text.slice(close + 2); }
  }
  rest = rest.split("**").join("");
  var words = rest.split(" ");
  var lines = [];
  var current = "";
  var used = lead ? spectrHelpMeasure(lead, true) : 0;
  var limit = Math.max(40, width - 4);
  for (var i = 0; i < words.length; i += 1) {
    var word = words[i];
    if (word === "" && current === "" && !(lead && lines.length === 0)) continue;
    var candidate = current ? current + " " + word : word;
    if (current && used + spectrHelpMeasure(candidate, false) > limit) {
      lines.push(current);
      current = word;
      used = 0;
    } else if (!current && lines.length === 0 && lead
               && used + spectrHelpMeasure(word, false) > limit) {
      lines.push("");
      current = word;
      used = 0;
    } else {
      current = candidate;
    }
  }
  if (current || lines.length === 0) lines.push(current);
  return { lead: lead, lines: lines };
}
function spectrHelpContentHeight(blocks, textW) {''',
    ),
    (
        "height is the sum of the measured lines",
        '''    var w = block.t === "item" ? textW - 12 : textW;
    var perLine = Math.max(20, Math.floor(w / 6.6));
    var lines = Math.max(1, Math.ceil(block.text.length / perLine));
    total += lines * 19 + (block.t === "item" ? 5 : 11);''',
        '''    var w = block.t === "item" ? textW - 12 : textW;
    var lines = spectrHelpLines(block.text, w).lines.length;
    total += lines * 19 + (block.t === "item" ? 5 : 11);''',
    ),
    (
        "paragraph renders its measured lines",
        '''        marginBottom: block.t === "item" ? 5 : 11,
        marginLeft: block.t === "item" ? 12 : 0
      }
    }, spectrHelpRuns(block.text));''',
        '''        marginBottom: block.t === "item" ? 5 : 11,
        marginLeft: block.t === "item" ? 12 : 0,
        flexDirection: "column"
      }
    }, (function () {
      var laid = spectrHelpLines(block.text, block.t === "item" ? textW - 12 : textW);
      return laid.lines.map(function (line, n) {
        var lineStyle = { display: "flex", flexDirection: "row", height: 19, lineHeight: "19px",
                          whiteSpace: "nowrap", flexShrink: 0 };
        if (n === 0 && laid.lead) {
          return /* @__PURE__ */ React.createElement("span", { key: n, style: lineStyle },
            /* @__PURE__ */ React.createElement("span", {
              style: { color: "rgba(255,255,255,0.96)", fontWeight: 600, flexShrink: 0, whiteSpace: "pre" }
            }, laid.lead),
            /* @__PURE__ */ React.createElement("span", { style: { whiteSpace: "pre" } }, line ? (line.charAt(0) === " " || /^[.,;:!?)]/.test(line) ? line : " " + line) : ""));
        }
        return /* @__PURE__ */ React.createElement("span", { key: n, style: lineStyle }, line);
      });
    })());''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("menu controls polish already applied")
        return 0
    for name, old, new in EDITS:
        count = raw.count(encode(old))
        if count != 1:
            sys.exit("FAIL: %s anchor occurs %d times, expected 1" % (name, count))
        raw = raw.replace(encode(old), encode(new), 1)
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("menu controls polish applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
