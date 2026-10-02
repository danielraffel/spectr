#!/usr/bin/env python3
"""Range: the plot's vertical scale and how far a full-height edit reaches.

"Gain scale ranges like 3, 6, 12 dB for mix/master" -- a tester. Bands are
+-24 dB host parameters and stay that way (a host stores normalised values,
so changing the parameter range would move every saved automation lane).
Range is an EDITING scale instead, kept with the session as editor state
(Spectr::editor_range_db, `range_get` / `range_set`, hydration `range_db`):

  * Settings > STRUCTURE > Range: +-3 / +-6 / +-12 / +-24 dB, default +-24.
  * The plot's geometry carries two half-heights: `plotHalfH`, the pixels
    from 0 dB to the plot edge, and `halfH`, the pixels for the normalised
    +-24 dB unit the editor stores gains in, which is plotHalfH x 24/Range.
    Every gain->y and y->gain mapping already goes through `halfH`, so the
    bars, the response curve, the hover readout and every drag scale with
    it; a full-height drag at Range 6 writes exactly +-6 dB.
  * The edit clamp is +-Range (`pxToGain`), so drawing never overshoots.
  * A band already outside the range keeps its value (and its sound) and is
    drawn pinned to the edge with an amber overflow marker; the next edit of
    that band brings it inside.
  * The dB rulers and grid label +-Range in quarters. The analyzer keeps its
    own dBFS scale against the plot's real height (`plotHalfH`).

Switching Range never touches audio: it is a view, and the processor's
render is bit-identical across it (test/test_level_controls.cpp).

Idempotent: exact substitutions, each asserted unique.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def escaped(value):
    # ensure_ascii=False: the artifact stores non-ASCII literally (see
    # tools/git/merge_materialized_runtime.py), so must every edit.
    return json.dumps(value, ensure_ascii=False)[1:-1]


RANGE_STORE = r'''  // Range (tools/patch_materialized_range.py): the editor's vertical
  // scale in dB. Editor state the processor persists; set on hydration,
  // from Settings, and read by every gain<->pixel mapping.
  globalThis.__spectrRange = globalThis.__spectrRange || { db: 24, listeners: [] };
  globalThis.spectrRangeChoices = [3, 6, 12, 24];
  globalThis.spectrRangeDb = () => {
    const db = globalThis.__spectrRange.db;
    return globalThis.spectrRangeChoices.indexOf(db) >= 0 ? db : 24;
  };
  // Pixels per normalised (24 dB) unit, relative to the plot's half-height.
  globalThis.spectrRangeScale = () => 24 / globalThis.spectrRangeDb();
  // The furthest a normalised gain is DRAWN or EDITED at this Range.
  globalThis.spectrRangeLimit = () => globalThis.spectrRangeDb() / 24;
  globalThis.spectrSetRangeDb = (db, publish) => {
    if (globalThis.spectrRangeChoices.indexOf(db) < 0) return false;
    const store = globalThis.__spectrRange;
    const moved = store.db !== db;
    store.db = db;
    if (publish && typeof window !== 'undefined' && window.pulp
        && typeof window.pulp.postMessage === 'function') {
      try {
        Promise.resolve(window.pulp.postMessage("range_set", { range_db: db },
          "spectr-range")).catch((error) =>
            console.error("[Spectr] range write failed", error));
      } catch (error) {
        console.error("[Spectr] range write failed", error);
      }
    }
    if (moved) store.listeners.slice().forEach((fn) => {
      try { fn(db); } catch (error) {
        console.error("[Spectr] range listener failed", error);
      }
    });
    return true;
  };
  globalThis.spectrOnRange = (fn) => {
    const store = globalThis.__spectrRange;
    store.listeners.push(fn);
    return () => {
      const at = store.listeners.indexOf(fn);
      if (at >= 0) store.listeners.splice(at, 1);
    };
  };
'''

SETTINGS_ROW = r'''function SpectrRangeSetting() {
  const [db, setDb] = React.useState(() => typeof globalThis.spectrRangeDb === "function"
    ? globalThis.spectrRangeDb() : 24);
  React.useEffect(() => typeof globalThis.spectrOnRange === "function"
    ? globalThis.spectrOnRange(setDb) : void 0, []);
  return /* @__PURE__ */ React.createElement("div", { "data-spectr-range-setting": String(db) },
    /* @__PURE__ */ React.createElement(SpectrSettingsChips, {
      value: String(db),
      onChange: (v) => {
        const next = parseInt(v, 10);
        if (typeof globalThis.spectrSetRangeDb === "function")
          globalThis.spectrSetRangeDb(next, true);
        else setDb(next);
      },
      opts: [["3", "\xB13"], ["6", "\xB16"], ["12", "\xB112"], ["24", "\xB124 dB"]]
    }));
}
'''

OVERFLOW_PASS = r'''  // Bands past the Range keep their value; they draw pinned to the edge,
  // and this marks which edge they overflow (tools/patch_materialized_range.py).
  function drawRangeOverflow(ctx, g) {
    const limit = typeof globalThis.spectrRangeLimit === "function"
      ? globalThis.spectrRangeLimit() : 1;
    if (limit >= 1) return;
    const tg = targetGainsRef.current;
    ctx.save();
    ctx.fillStyle = "rgba(255,190,90,0.92)";
    for (let i = 0; i < N; ++i) {
      if (isMuted(tg[i]) || !Number.isFinite(tg[i])) continue;
      const v = macroAdjustedGain(tg[i], i);
      if (Math.abs(v) <= limit + 1e-6) continue;
      const cx = bandCenterX(i, g);
      const up = v > 0;
      const edgeY = up ? g.zeroY - g.plotHalfH : g.zeroY + g.plotHalfH;
      const s = Math.max(2.5, Math.min(5, g.bandW * 0.3));
      ctx.beginPath();
      ctx.moveTo(cx - s, edgeY + (up ? s + 2 : -s - 2));
      ctx.lineTo(cx + s, edgeY + (up ? s + 2 : -s - 2));
      ctx.lineTo(cx, edgeY + (up ? 2 : -2));
      ctx.closePath();
      ctx.fill();
    }
    ctx.restore();
  }
'''

EDITS = [
    ("the range store is global and set on hydration",
     "  const parseNativeState = payload => {\n",
     RANGE_STORE + "  const parseNativeState = payload => {\n"),
    ("hydration carries the session's Range",
     "    if (payload && payload.keyboard) applyKeyboardPolicy(payload.keyboard);\n",
     "    if (payload && payload.keyboard) applyKeyboardPolicy(payload.keyboard);\n"
     "    // Range: hydration-only editor state, so update on presence.\n"
     "    if (payload && typeof payload.range_db === 'number')\n"
     "      globalThis.spectrSetRangeDb(payload.range_db, false);\n"),
    ("geometry carries the plot and the Range half-heights",
     "    const halfH = Math.min(zeroY - inner.y, inner.y + inner.h - zeroY);\n"
     "    const bandGap = 2;\n"
     "    const bandW = (inner.w - bandGap * (N - 1)) / N;\n"
     "    return { w, h, pad, inner, zeroY, halfH, bandW, bandGap };\n"
     "  }, [N]);\n",
     "    // plotHalfH: pixels from 0 dB to the plot edge. halfH: pixels per\n"
     "    // normalised 24 dB unit at this Range, so every gain<->y mapping\n"
     "    // scales with it (tools/patch_materialized_range.py).\n"
     "    const plotHalfH = Math.min(zeroY - inner.y, inner.y + inner.h - zeroY);\n"
     "    const halfH = plotHalfH * (24 / (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24));\n"
     "    const bandGap = 2;\n"
     "    const bandW = (inner.w - bandGap * (N - 1)) / N;\n"
     "    return { w, h, pad, inner, zeroY, halfH, plotHalfH, bandW, bandGap };\n"
     "  }, [N, globalThis.__spectrRange && globalThis.__spectrRange.db]);\n"),
    ("FilterBank follows the Range",
     "function FilterBank({ settings, onStateChange, sharedState, onStatus, dspMode, editMode, analyzerMode, visualizationMode, onEditModeChange, nativeHydrated, onNativeState }) {\n",
     "function FilterBank({ settings, onStateChange, sharedState, onStatus, dspMode, editMode, analyzerMode, visualizationMode, onEditModeChange, nativeHydrated, onNativeState }) {\n"
     "  const [, setRangeDb] = React.useState(0);\n"
     "  React.useEffect(() => typeof globalThis.spectrOnRange === 'function'\n"
     "    ? globalThis.spectrOnRange((db) => setRangeDb(db)) : void 0, []);\n"),
    ("the frame redraws on a Range change",
     "  }, [view, N, bloom, spectrumIntensity, muteStyle, motionMode, metaphor, showMinimap, showRulers, theme, selection, snapshots, morph, dspMode, visualizationMode]);\n",
     "  }, [view, N, bloom, spectrumIntensity, muteStyle, motionMode, metaphor, showMinimap, showRulers, theme, selection, snapshots, morph, dspMode, visualizationMode, globalThis.__spectrRange && globalThis.__spectrRange.db]);\n"),
    ("overflow markers draw last",
     '    if (visualizationMode !== "bars") drawMaskResponse(ctx, g);\n    octx.clearRect(0, 0, w, h);\n',
     '    if (visualizationMode !== "bars") drawMaskResponse(ctx, g);\n    drawRangeOverflow(ctx, g);\n    octx.clearRect(0, 0, w, h);\n'),
    ("the overflow pass exists",
     "  function drawGrid(ctx, g) {\n",
     OVERFLOW_PASS + "  function drawGrid(ctx, g) {\n"),
    ("grid lines in quarters of the Range",
     "    for (let db = -24; db <= 24; db += 6) {\n"
     "      const y = g.zeroY - db / 24 * g.halfH + 0.5;\n",
     "    const gridRange = (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24);\n"
     "    for (let db = -gridRange; db <= gridRange; db += gridRange / 4) {\n"
     "      const y = g.zeroY - db / gridRange * (g.plotHalfH ?? g.halfH) + 0.5;\n"),
    ("ruler labels in quarters of the Range",
     "    for (let db = -24; db <= 24; db += 6) {\n"
     "      const y = g.zeroY - db / 24 * g.halfH;\n"
     "      ctx.fillStyle = db === 0 ? \"rgba(255,255,255,0.65)\" : \"rgba(255,255,255,0.30)\";\n"
     "      ctx.fillText((db > 0 ? \"+\" : \"\") + db, inner.x - 8, y);\n",
     "    const rulerRange = (typeof globalThis.spectrRangeDb === 'function' ? globalThis.spectrRangeDb() : 24);\n"
     "    for (let db = -rulerRange; db <= rulerRange; db += rulerRange / 4) {\n"
     "      const y = g.zeroY - db / rulerRange * (g.plotHalfH ?? g.halfH);\n"
     "      ctx.fillStyle = db === 0 ? \"rgba(255,255,255,0.65)\" : \"rgba(255,255,255,0.30)\";\n"
     "      ctx.fillText((db > 0 ? \"+\" : \"\") + (Number.isInteger(db) ? db : db.toFixed(1)), inner.x - 8, y);\n"),
    ("the analyzer ticks keep the plot's own height",
     "      const y = window.SpectrAnalyzer.project(amount, g.zeroY, g.halfH);\n",
     "      const y = window.SpectrAnalyzer.project(amount, g.zeroY, (g.plotHalfH ?? g.halfH));\n"),
    ("the -inf label stays at the plot floor",
     '    ctx.fillText("\\u2212\\u221E", inner.x - 8, g.zeroY + g.halfH + 10);\n',
     '    ctx.fillText("\\u2212\\u221E", inner.x - 8, g.zeroY + (g.plotHalfH ?? g.halfH) + 10);\n'),
    ("the analyzer trace keeps the plot's own height",
     "    const { inner, zeroY, halfH } = g;\n    const t = timeRef.current;\n",
     "    const { inner, zeroY } = g; const halfH = g.plotHalfH ?? g.halfH;\n    const t = timeRef.current;\n"),
    ("the response curve pins at the edge",
     "      const rendered = Number.isFinite(rg[i]) ? clamp(macroAdjustedGain(rg[i], i), -1, 1) : 0;\n",
     "      const rendered = Number.isFinite(rg[i]) ? clamp(macroAdjustedGain(rg[i], i), -(typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1), (typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1)) : 0;\n"),
    ("bars pin at the edge",
     "      const gval = effectiveGains[i];\n      const targetMuted = isMuted(tg[i]);\n",
     "      const gval = clamp(effectiveGains[i], -(typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1), (typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1));\n      const targetMuted = isMuted(tg[i]);\n"),
    ("the stair-step pins at the edge",
     "        y: zeroY - effectiveGains[i] * halfH\n",
     "        y: zeroY - clamp(effectiveGains[i], -(typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1), (typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1)) * halfH\n"),
    ("modulation looks pin at the edge",
     "    const yOf = (v, i) => zeroY - clamp(macroAdjustedGain(v, i), -1.02, 1.02) * halfH;\n",
     "    const yOf = (v, i) => zeroY - clamp(macroAdjustedGain(v, i), -1.02 * (typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1), 1.02 * (typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1)) * halfH;\n"),
    ("muted bars sit at the plot floor",
     "function gainToY(g, zeroY, halfH) {\n  if (isMuted(g)) return zeroY + halfH;\n  return zeroY - g * halfH;\n",
     "function gainToY(g, zeroY, halfH) {\n"
     "  const limit = typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1;\n"
     "  if (isMuted(g)) return zeroY + halfH * limit;\n"
     "  return zeroY - Math.max(-limit, Math.min(limit, g)) * halfH;\n"),
    ("an edit never reaches past the Range",
     "  const pxToGain = (y, g) => clamp((g.zeroY - y) / g.halfH, -1, 1);\n",
     "  const pxToGain = (y, g) => clamp((g.zeroY - y) / g.halfH, -(typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1), (typeof globalThis.spectrRangeLimit === 'function' ? globalThis.spectrRangeLimit() : 1));\n"),
    ("Settings has a Range row component",
     "function SettingsModal(",
     SETTINGS_ROW + "function SettingsModal("),
    ("STRUCTURE gains the Range row",
     '/* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: "general", title: "STRUCTURE", subtitle: "Band count, mute behavior, chrome." }, ',
     '/* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: "general", title: "STRUCTURE", subtitle: "Band count, mute behavior, chrome." }, '
     '/* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Range", hint: "How far a full-height drag reaches. Doesn\'t change the sound." }, '
     '/* @__PURE__ */ React.createElement(SpectrRangeSetting, null)), '),
]


def main():
    raw = open(PATH, encoding="utf-8").read()
    changed = False
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1:
            print("already applied ", label)
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1" % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        print("applied         ", label)
    html = json.loads(raw)["html"]
    for token in ("function SpectrRangeSetting(", "function drawRangeOverflow(",
                  "globalThis.spectrSetRangeDb = ", 'label: "Range"'):
        if html.count(token) != 1:
            sys.exit("FAIL: %r appears %d times, expected 1" % (token, html.count(token)))
    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
