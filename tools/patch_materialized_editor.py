#!/usr/bin/env python3
"""Mirror the resources/editor.html source patches into the shipping document.

INTERIM. native-ui/materialized/materialized-document.runtime.json is a
generated artifact and no recipe in this repo reproduces the checked-in pair
(danielraffel/spectr#48), so editor-behaviour fixes have to be applied to it by
hand. Every edit below is a mechanical, one-for-one image of a
replaceSpectrSource entry in resources/editor.html -- the source stays the
durable record, and a future re-materialization should make this script a no-op.

DELIBERATE DIVERGENCE, and the only one: resources/editor.html carries a
"REDRAW UNMUTES" toggle in the overflow menu that is NOT mirrored here. The
native materialized runtime paints any child ADDED to a container at that
container's FIRST child position, on top of what is already there. Reproduced
by screenshot in three unrelated containers -- the settings STRUCTURE group,
the settings MOTION group, and the overflow menu -- so this is a property of
the runtime, not of one placement. Mirroring the toggle would ship an
unreadable overlap. The setting itself IS mirrored and works; in the shipping
native editor it is reachable only at its default (on). Re-materializing the
document is what surfaces the control, and at that point this note goes away.

Idempotent: re-running it after a successful pass reports "already applied".
"""
import json
import os
import shutil
import sys

PATH = 'native-ui/materialized/materialized-document.runtime.json'
SOURCE_PATH = 'resources/editor.html'

EDITS = [
    ('animation loop paints the latest canvas renderer',
     '      renderAll();\n'
     '      rafRef.current = requestAnimationFrame(draw);\n'
     '    };',
     '      (renderAllRef.current || renderAll)();\n'
     '      rafRef.current = requestAnimationFrame(draw);\n'
     '    };'),

    # The animation loop paints through renderAllRef (see the entry above), so it
    # does NOT need re-creating when a render input changes. Listing render
    # inputs here made every `setView` during a minimap drag tear the loop down
    # (cancelAnimationFrame) and rebuild it -- once per pointer event, cancelling
    # the frame already in flight. `view` and `N` were the per-event offenders.
    #
    # `motionMode` MUST STAY. `draw` reads it directly out of its closure, not
    # through a ref: `const k = motionMode === "precision" ? 6 : 22`, the
    # smoothing rate. With empty deps the closure pins the mount-time value and
    # toggling PRECISION silently stops changing the smoothing -- i.e. the
    # toggle appears dead. It changes on a click, not per pointer event, so
    # keeping it costs nothing. To reach empty deps, add a motionModeRef (the
    # file already uses editModeRef / dspModeRef / analyzerModeRef for exactly
    # this) rather than dropping the dependency.
    ('animation loop is not rebuilt on every render-input change',
     '}, [N, bloom, spectrumIntensity, muteStyle, motionMode, metaphor, '
     'showMinimap, showRulers, theme, view]);',
     '}, [motionMode]);'),

    # SAVE CURRENT... and MANAGE... shared one row because `menuItem` sets no
    # `display`, so the buttons defaulted to inline. MANAGE read as a modifier
    # on SAVE rather than its own action, and users did not find it. Two rows.
    ('preset dropdown footer actions get their own rows',
     'style: { ...menuItem, color: "hsl(200,85%,70%)" }',
     'style: { ...menuItem, color: "hsl(200,85%,70%)", '
     'display: "block", width: "100%" }'),

    ('hover readout clears the status banner slot',
     '    const ty = clamp(y - 30, g.inner.y + 2, g.inner.y + g.inner.h);',
     '    const ty = clamp(y - 30, g.inner.y + 20, g.inner.y + g.inner.h);'),

    ('status banner replaces one message at a time',
     '  const [text, setText] = useStateChrome("");\n'
     '  useEffectChrome(() => {\n'
     '    if (!message) {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      return;\n'
     '    }\n'
     '    const display = message.split("|")[0].trim();\n'
     '    if (!display) {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      return;\n'
     '    }\n'
     '    setText(display);\n'
     '    setVisible(true);\n'
     '    const t = setTimeout(() => {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '    }, 1400);\n'
     '    return () => clearTimeout(t);\n'
     '  }, [message]);\n',

     '  const [text, setText] = useStateChrome("");\n'
     '  const shownRef = useRefChrome("");\n'
     '  useEffectChrome(() => {\n'
     '    const display = message ? message.split("|")[0].trim() : "";\n'
     '    if (!display) {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '      return;\n'
     '    }\n'
     '    const replacing = !!shownRef.current && shownRef.current !== display;\n'
     '    const timers = [];\n'
     '    if (replacing) {\n'
     '      setVisible(false);\n'
     '      timers.push(setTimeout(() => {\n'
     '        setText(display);\n'
     '        setVisible(true);\n'
     '      }, 150));\n'
     '    } else {\n'
     '      setText(display);\n'
     '      setVisible(true);\n'
     '    }\n'
     '    shownRef.current = display;\n'
     '    timers.push(setTimeout(() => {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '    }, replacing ? 1550 : 1400));\n'
     '    return () => timers.forEach(clearTimeout);\n'
     '  }, [message]);\n'),

    ('status banner chrome survives the fade-out',
     '        background: visible && text ? "rgba(12,16,22,0.92)" : "transparent",\n'
     '        border: "1px solid " + (visible && text ? "rgba(180,210,255,0.3)" : "transparent"),\n'
     '        borderRadius: 3,\n'
     '        fontFamily: "var(--mono)",\n'
     '        fontSize: 10,\n'
     '        letterSpacing: 1.5,\n'
     '        color: visible && text ? "rgba(200,220,255,0.95)" : "transparent",\n'
     '        zIndex: 6,\n'
     '        pointerEvents: "none",\n'
     '        opacity: visible && text ? 1 : 0,\n'
     '        transition: "opacity 0.2s",\n'
     '        backdropFilter: visible && text ? "blur(8px)" : "none"\n'
     '      }\n'
     '    },\n'
     '    visible && text ? text : ""\n'
     '  );',

     '        background: text ? "rgba(12,16,22,0.92)" : "transparent",\n'
     '        border: "1px solid " + (text ? "rgba(180,210,255,0.3)" : "transparent"),\n'
     '        borderRadius: 3,\n'
     '        fontFamily: "var(--mono)",\n'
     '        fontSize: 10,\n'
     '        letterSpacing: 1.5,\n'
     '        color: text ? "rgba(200,220,255,0.95)" : "transparent",\n'
     '        zIndex: 6,\n'
     '        pointerEvents: "none",\n'
     '        opacity: visible && text ? 1 : 0,\n'
     '        transition: "opacity 0.15s ease",\n'
     '        backdropFilter: text ? "blur(8px)" : "none"\n'
     '      }\n'
     '    },\n'
     '    text\n'
     '  );'),

    # ----------------------------- task 4: mute consistency across edit modes
    ('redraw-unmutes setting is read by the bank',
     '  const { bandCount, metaphor, bloom, spectrumIntensity, muteStyle, '
     'motionMode, showMinimap, showRulers, theme } = settings;',
     '  const { bandCount, metaphor, bloom, spectrumIntensity, muteStyle, '
     'motionMode, showMinimap, showRulers, theme, unmuteOnDraw } = settings;'),

    ('redraw-unmutes pointer-stable ref',
     '  const N = bandCount;\n'
     '  const editModeRef = useRef(editMode || "sculpt");\n',
     '  const N = bandCount;\n'
     '  const unmuteOnDrawRef = useRef(unmuteOnDraw !== false);\n'
     '  useEffect(() => {\n'
     '    unmuteOnDrawRef.current = unmuteOnDraw !== false;\n'
     '  }, [unmuteOnDraw]);\n'
     '  const editModeRef = useRef(editMode || "sculpt");\n'),

    ('drawn gain edits share one mute decision',
     '    queueNativeProcessingStatePublication();\n'
     '  };\n'
     '  const minimapHit = (x, y, g) => {',
     '    queueNativeProcessingStatePublication();\n'
     '  };\n'
     '  const commitDrawnGains = (map) => {\n'
     '    if (unmuteOnDrawRef.current) {\n'
     '      commitMany(map);\n'
     '      return;\n'
     '    }\n'
     '    const held = /* @__PURE__ */ new Map();\n'
     '    for (const [index, value] of map)\n'
     '      held.set(index, isMuted(targetGainsRef.current[index]) ? -Infinity : value);\n'
     '    commitMany(held);\n'
     '  };\n'
     '  const editBaseGain = (value, index) => {\n'
     '    if (!isMuted(value)) return value;\n'
     '    const db = mutedGainDbRef.current[index];\n'
     '    return Number.isFinite(db) ? clamp(db / 24, -1, 1) : 0;\n'
     '  };\n'
     '  const minimapHit = (x, y, g) => {'),

    ('group drag defers the mute decision',
     '        for (const [i, v0] of p.groupStart.entries()) {\n'
     '          if (isMuted(v0)) {\n'
     '            map.set(i, -Infinity);\n'
     '            continue;\n'
     '          }\n'
     '          map.set(i, clamp(v0 + delta, -1, 1));\n'
     '        }\n'
     '        commitMany(map);',
     '        for (const [i, v0] of p.groupStart.entries())\n'
     '          map.set(i, clamp(editBaseGain(v0, i) + delta, -1, 1));\n'
     '        commitDrawnGains(map);'),

    ('sculpt defers the mute decision',
     '        map.set(curBand, newG);\n'
     '        commitMany(map);',
     '        map.set(curBand, newG);\n'
     '        commitDrawnGains(map);'),

    ('level defers the mute decision',
     '        for (const b of p.paintedBands) map.set(b, newG);\n'
     '        commitMany(map);',
     '        for (const b of p.paintedBands) map.set(b, newG);\n'
     '        commitDrawnGains(map);'),

    ('boost defers the mute decision',
     '          const v0 = p.startSnap[b];\n'
     '          if (isMuted(v0)) {\n'
     '            map.set(b, -Infinity);\n'
     '            continue;\n'
     '          }\n'
     '          map.set(b, clamp(v0 * k, -1, 1));\n'
     '        }\n'
     '        commitMany(map);',
     '          const v0 = editBaseGain(p.startSnap[b], b);\n'
     '          map.set(b, clamp(v0 * k, -1, 1));\n'
     '        }\n'
     '        commitDrawnGains(map);'),

    ('flare defers the mute decision',
     '          const v0 = p.startSnap[b];\n'
     '          if (isMuted(v0)) {\n'
     '            map.set(b, -Infinity);\n'
     '            continue;\n'
     '          }\n'
     '          const sign = v0 >= 0 ? 1 : -1;',
     '          const v0 = editBaseGain(p.startSnap[b], b);\n'
     '          const sign = v0 >= 0 ? 1 : -1;'),

    ('flare commits through the shared path',
     '          map.set(b, clamp(out, -1, 1));\n'
     '        }\n'
     '        commitMany(map);',
     '          map.set(b, clamp(out, -1, 1));\n'
     '        }\n'
     '        commitDrawnGains(map);'),

    ('glide defers the mute decision',
     '          const v0 = p.startSnap[i];\n'
     '          if (isMuted(v0)) {\n'
     '            map.set(i, -Infinity);\n'
     '            continue;\n'
     '          }\n'
     '          map.set(i, clamp(v0 + (newG - v0) * w, -1, 1));\n'
     '        }\n'
     '        commitMany(map);',
     '          const v0 = editBaseGain(p.startSnap[i], i);\n'
     '          map.set(i, clamp(v0 + (newG - v0) * w, -1, 1));\n'
     '        }\n'
     '        commitDrawnGains(map);'),

    ('hover readout uses unified status banner',
     'const [hover, setHover] = useState(null);\n'
     '  const hoverBand = hover && !hover.mini ? hover.band : -1;\n'
     '  useEffect(() => {\n'
     '    if (!onStatus) return;\n'
     '    if (hoverBand < 0) {\n'
     '      onStatus("");\n'
     '      return;\n'
     '    }\n'
     '    const f = bandCenterFreq(hoverBand);\n'
     '    const rendered = renderGainsRef.current[hoverBand];\n'
     '    const gv = Number.isFinite(rendered) ? clamp(rendered, -1.02, 1.02) : 0;\n'
     '    const db = isMuted(targetGainsRef.current[hoverBand]) ? "\\u2212\\u221E" : (gv * 24).toFixed(1);\n'
     '    onStatus(`${window.SpectrFreq.fmt(f)}Hz   ${db}${db === "\\u2212\\u221E" ? "" : " dB"}   BAND ${hoverBand + 1}/${N}`);\n'
     '  }, [hoverBand, N, onStatus]);\n'
     '  const [ctxMenu, setCtxMenu] = useState(null);',
     'const [hover, setHover] = useState(null);\n'
     '  const hoverBand = hover && !hover.mini ? hover.band : -1;\n'
     '  useEffect(() => {\n'
     '    if (!onStatus) return;\n'
     '    if (hoverBand < 0) {\n'
     '      onStatus("");\n'
     '      return;\n'
     '    }\n'
     '    const f = bandCenterFreq(hoverBand);\n'
     '    const rendered = renderGainsRef.current[hoverBand];\n'
     '    const gv = Number.isFinite(rendered) ? clamp(rendered, -1.02, 1.02) : 0;\n'
     '    const db = isMuted(targetGainsRef.current[hoverBand]) ? "\\u2212\\u221E" : (gv * 24).toFixed(1);\n'
     '    const label = `${window.SpectrFreq.fmt(f)}Hz   ${db}${db === "\\u2212\\u221E" ? "" : " dB"}   BAND ${hoverBand + 1}/${N}`;\n'
     '    onStatus(label);\n'
     '    const keepAlive = setInterval(() => onStatus(label), 1e3);\n'
     '    return () => clearInterval(keepAlive);\n'
     '  }, [hoverBand, N, onStatus]);\n'
     '  const [ctxMenu, setCtxMenu] = useState(null);'),

    ('hover canvas keeps guide but not floating tooltip',
     '  function drawHover(ctx, g) {\n'
     '    if (!hover) return;\n'
     '    const { x, y, band } = hover;\n'
     '    const f = bandCenterFreq(band);\n'
     '    const rendered = renderGainsRef.current[band];\n'
     '    const gv = Number.isFinite(rendered) ? clamp(rendered, -1.02, 1.02) : 0;\n'
     '    const db = isMuted(targetGainsRef.current[band]) ? "\\u2212\\u221E" : (gv * 24).toFixed(1);\n'
     '    const label = `${window.SpectrFreq.fmt(f)}Hz   ${db}${db === "\\u2212\\u221E" ? "" : " dB"}   band ${band + 1}/${N}`;\n'
     '    ctx.save();\n'
     '    ctx.font = "11px JetBrains Mono, monospace";\n'
     '    const tw = ctx.measureText(label).width + 18;\n'
     '    const tx = clamp(x - tw / 2, g.inner.x, g.inner.x + g.inner.w - tw);\n'
     '    const ty = clamp(y - 30, g.inner.y + 20, g.inner.y + g.inner.h);\n'
     '    ctx.fillStyle = "rgba(10,14,20,0.88)";\n'
     '    ctx.strokeStyle = "rgba(255,255,255,0.18)";\n'
     '    ctx.lineWidth = 1;\n'
     '    roundRect(ctx, tx, ty, tw, 22, 4);\n'
     '    ctx.fill();\n'
     '    ctx.stroke();\n'
     '    ctx.fillStyle = "rgba(255,255,255,0.9)";\n'
     '    ctx.textAlign = "left";\n'
     '    ctx.textBaseline = "middle";\n'
     '    ctx.fillText(label, tx + 9, ty + 11);\n'
     '    ctx.strokeStyle = "rgba(255,255,255,0.15)";\n'
     '    ctx.setLineDash([2, 3]);\n'
     '    ctx.beginPath();\n'
     '    ctx.moveTo(x, g.inner.y);\n'
     '    ctx.lineTo(x, g.inner.y + g.inner.h);\n'
     '    ctx.stroke();\n'
     '    ctx.setLineDash([]);\n'
     '    ctx.restore();\n'
     '  }',
     '  function drawHover(ctx, g) {\n'
     '    if (!hover || hover.mini) return;\n'
     '    const { x } = hover;\n'
     '    ctx.save();\n'
     '    ctx.strokeStyle = "rgba(255,255,255,0.15)";\n'
     '    ctx.setLineDash([2, 3]);\n'
     '    ctx.beginPath();\n'
     '    ctx.moveTo(x, g.inner.y);\n'
     '    ctx.lineTo(x, g.inner.y + g.inner.h);\n'
     '    ctx.stroke();\n'
     '    ctx.setLineDash([]);\n'
     '    ctx.restore();\n'
     '  }'),

    ('edge labels have a stable baseline without dashed boxes',
     '      if (G.edge) {\n'
     '        ctx.strokeStyle = "rgba(255,255,255,0.38)";\n'
     '        ctx.lineWidth = 1;\n'
     '        ctx.setLineDash([2, 2]);\n'
     '        ctx.strokeRect(\n'
     '          Math.round(G.cx - G.innerW / 2) - 1.5,\n'
     '          Math.round(Math.min(G.topY, G.botY)) - 1.5,\n'
     '          Math.round(G.innerW) + 3,\n'
     '          Math.round(Math.abs(G.botY - G.topY)) + 3\n'
     '        );\n'
     '        ctx.setLineDash([]);\n'
     '        ctx.fillStyle = "rgba(255,255,255,0.55)";\n'
     '        ctx.font = "9px JetBrains Mono, monospace";\n'
     '        ctx.textAlign = "center";\n'
     '        ctx.fillText(i === 0 ? "HPF" : "LPF", G.cx, g.inner.y + g.inner.h + 14);\n'
     '      }',
     '      if (G.edge) {\n'
     '        ctx.fillStyle = "rgba(255,255,255,0.55)";\n'
     '        ctx.font = "10px JetBrains Mono, monospace";\n'
     '        ctx.textAlign = "center";\n'
     '        ctx.textBaseline = "middle";\n'
     '        ctx.fillText(i === 0 ? "HPF" : "LPF", G.cx, g.inner.y + g.inner.h + 14);\n'
     '      }'),

    ('band count label shares one vertical center',
     '    letterSpacing: 0.5,\n'
     '    cursor: "pointer",\n'
     '    display: "inline-flex",\n'
     '    alignItems: "center",\n'
     '    gap: 4,\n'
     '    lineHeight: 1\n'
     '  }, title: "Click to change band count" }, /* @__PURE__ */ React.createElement("span", { className: "tnum", style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, info.N), /* @__PURE__ */ React.createElement("span", { style: { lineHeight: 1 } }, "bands \\u25BE"))',
     '    letterSpacing: 0.5,\n'
     '    cursor: "pointer",\n'
     '    display: "inline-flex",\n'
     '    alignItems: "center",\n'
     '    gap: 4,\n'
     '    lineHeight: 1\n'
     '  }, title: "Click to change band count" }, /* @__PURE__ */ React.createElement("span", { className: "tnum", style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, info.N), " bands \\u25BE")'),

    ('dropdown items use a consistent native-friendly surface',
     'const menuItem = {\n'
     '  background: "transparent",\n'
     '  border: "none",\n'
     '  color: "rgba(255,255,255,0.85)",\n'
     '  fontFamily: "var(--mono)",\n'
     '  fontSize: 10.5,\n'
     '  letterSpacing: 0.8,\n'
     '  padding: "7px 10px",\n'
     '  textAlign: "left",\n'
     '  cursor: "pointer",\n'
     '  borderRadius: 2\n'
     '};',
     'const menuItem = {\n'
     '  background: "rgba(255,255,255,0.025)",\n'
     '  border: "1px solid transparent",\n'
     '  color: "rgba(255,255,255,0.85)",\n'
     '  fontFamily: "var(--mono)",\n'
     '  fontSize: 10.5,\n'
     '  letterSpacing: 0.8,\n'
     '  padding: "7px 10px",\n'
     '  minHeight: 30,\n'
     '  width: "100%",\n'
     '  boxSizing: "border-box",\n'
     '  display: "flex",\n'
     '  alignItems: "center",\n'
     '  gap: 6,\n'
     '  textAlign: "left",\n'
     '  cursor: "pointer",\n'
     '  borderRadius: 3\n'
     '};'),

    ('status banner is content-sized with symmetric padding',
     '        width: Math.max(96, Math.min(520, text.length * 7 + 28)),\n'
     '        height: 26,\n'
     '        padding: "0 14px",\n'
     '        boxSizing: "border-box",\n'
     '        display: "flex",\n'
     '        alignItems: "center",\n'
     '        justifyContent: "center",\n'
     '        whiteSpace: "nowrap",',
     '        width: Math.max(96, Math.min(520, text.length * 8 + 28)),\n'
     '        height: 26,\n'
     '        padding: "0 14px",\n'
     '        boxSizing: "border-box",\n'
     '        display: "flex",\n'
     '        alignItems: "center",\n'
     '        justifyContent: "center",\n'
     '        whiteSpace: "nowrap",'),

    ('status banner resizes smoothly',
     '        transition: "opacity 0.15s ease",',
     '        transition: "width 0.18s ease, opacity 0.15s ease",'),

    ('status banner sits below the plot top line',
     '        top: 60,',
     '        top: 76,'),

    ('bottom rail controls center glyphs text and chevrons',
     '        borderRadius: 3,\n'
     '        cursor: "pointer",\n'
     '        height: 26,\n'
     '        transition: "background 0.15s, border-color 0.15s"',
     '        borderRadius: 3,\n'
     '        cursor: "pointer",\n'
     '        height: 26,\n'
     '        display: "inline-flex",\n'
     '        alignItems: "center",\n'
     '        justifyContent: "center",\n'
     '        lineHeight: 1,\n'
     '        transition: "background 0.15s, border-color 0.15s"'),

    ('edit rail label shares one chevron baseline',
     'React.createElement("span", { style: { marginLeft: 6 } }, editMode.toUpperCase(), " \\u25BE")',
     'React.createElement("span", { style: { marginLeft: 6, display: "inline-flex", alignItems: "center", lineHeight: 1 } }, editMode.toUpperCase(), " \\u25BE")'),

    ('analyzer rail label shares one chevron baseline',
     'React.createElement("span", { style: { marginLeft: 6 } }, analyzerMode.toUpperCase(), " \\u25BE")',
     'React.createElement("span", { style: { marginLeft: 6, display: "inline-flex", alignItems: "center", lineHeight: 1 } }, analyzerMode.toUpperCase(), " \\u25BE")'),

    ('preset rail label shares one chevron baseline',
     'React.createElement("span", { style: { marginLeft: 6 } }, "PRESETS \\u25BE")',
     'React.createElement("span", { style: { marginLeft: 6, display: "inline-flex", alignItems: "center", lineHeight: 1 } }, "PRESETS \\u25BE")'),

    ('band dropdown inactive items retain a surface',
     'background: info.N === n ? "rgba(120,180,255,0.18)" : "transparent",',
     'background: info.N === n ? "rgba(120,180,255,0.18)" : "rgba(255,255,255,0.025)",'),

    ('edit dropdown inactive items retain a surface',
     'background: active ? "rgba(120,180,255,0.14)" : "transparent",',
     'background: active ? "rgba(120,180,255,0.14)" : "rgba(255,255,255,0.025)",'),

    ('analyzer dropdown inactive items retain a surface',
     'background: active ? "rgba(255,255,255,0.08)" : "transparent",',
     'background: active ? "rgba(255,255,255,0.08)" : "rgba(255,255,255,0.025)",'),

    ('settings dropdown inactive items retain a surface',
     'background: active ? "rgba(120,180,255,0.16)" : "transparent",',
     'background: active ? "rgba(120,180,255,0.16)" : "rgba(255,255,255,0.025)",'),

]

# A later edit may deliberately consume the exact replacement image of an
# earlier one. These named sentinels keep reruns strict without pretending the
# superseded intermediate text must remain in the final shipping document.
SUPERSEDED_SENTINELS = {
    'hover readout clears the status banner slot':
        'if (!hover || hover.mini) return;',
    'status banner chrome survives the fade-out':
        '"data-spectr-status-text": "true"',
}

# Generated bindings live outside the escaped `html` string. Keep these
# materialization-only corrections explicit rather than teaching HTML edits to
# rewrite unrelated top-level document data.
DOCUMENT_EDITS = [
    ('band-count binding shares the suffix baseline',
     '"letter_spacing":0.5}},"boxes":[{"left":0,"top":0,"width":13,'
     '"height":13,"start":0,"length":2}]},{"index":9',
     '"letter_spacing":0.5}},"boxes":[{"left":0,"top":3,"width":13,'
     '"height":13,"start":0,"length":2}]},{"index":9'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def check_script_blocks(label, blocks):
    """Parse JavaScript blocks with the same Node parser as the browser oracle."""
    import subprocess
    import tempfile

    node = shutil.which('node')
    if not node:
        print(f'warn: node not found; skipped the {label} script parse check')
        return
    if not blocks:
        sys.exit(f'FAIL: no {label} script blocks found')
    for index, block in enumerate(blocks):
        with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as handle:
            handle.write(block)
            path = handle.name
        result = subprocess.run([node, '--check', path],
                                capture_output=True, text=True)
        os.unlink(path)
        if result.returncode != 0:
            sys.exit(f'FAIL: {label} script block {index} does not parse\n'
                     + result.stderr)
    print(f'parsed {len(blocks)} {label} script blocks')


def check_bootstrap_scripts():
    """Parse executable scripts in the authored bootstrap document."""
    import re

    source = open(SOURCE_PATH, encoding='utf-8').read()
    blocks = re.findall(
        r'<script(?: type="text/javascript")?>(.*?)</script>', source, re.S)
    check_script_blocks('bootstrap', blocks)


def check_emitted_scripts(html):
    """Parse every emitted application script block.

    A replacement that is one parenthesis out still substitutes cleanly and
    still passes a substring post-check, but QuickJS then rejects the whole
    document and the native editor quietly falls back to the generic auto-UI
    panel. Node is the same parser the browser oracle uses.
    """
    import re
    blocks = re.findall(
        r'<script(?: type="text/javascript")?>(.*?)</script>', html, re.S)
    check_script_blocks('emitted application', blocks)


def main():
    check_bootstrap_scripts()

    # An edit whose replacement still contains its own patch point would apply
    # again on every run, silently stacking duplicates. Make every edit
    # self-consuming, and refuse to run if one is not.
    for label, old, new in EDITS:
        if old in new:
            sys.exit(f'FAIL {label}: patch point survives its own replacement')

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        sentinel = SUPERSEDED_SENTINELS.get(label)
        sentinel_e = escaped(sentinel) if sentinel else None
        # One JSX patch point can transpile into repeated identical literals
        # (for example the two preset footer buttons). Once the old image is
        # gone, any emitted replacement count proves this edit was applied.
        if raw.count(new_e) >= 1 and raw.count(old_e) == 0:
            print('already applied ', label)
            continue
        if raw.count(old_e) == 0 and sentinel_e and sentinel_e in raw:
            print('superseded     ', label)
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit(f'FAIL {label}: patch point occurs {count} times')
        raw = raw.replace(old_e, new_e)
        changed = True
        print('applied         ', label)

    for label, old, new in DOCUMENT_EDITS:
        if raw.count(new) == 1 and raw.count(old) == 0:
            print('already applied ', label)
            continue
        count = raw.count(old)
        if count != 1:
            sys.exit(f'FAIL {label}: document patch point occurs {count} times')
        raw = raw.replace(old, new)
        changed = True
        print('applied         ', label)

    document = json.loads(raw)
    html = document['html']
    for label, _old, new in EDITS:
        sentinel = SUPERSEDED_SENTINELS.get(label)
        if new not in html and (not sentinel or sentinel not in html):
            sys.exit(f'FAIL {label}: post-check did not find the replacement')
    for label, _old, new in DOCUMENT_EDITS:
        if new not in raw:
            sys.exit(f'FAIL {label}: document post-check did not find the replacement')
    check_emitted_scripts(html)
    if changed:
        open(PATH, 'w', encoding='utf-8').write(raw)
        print('written', PATH)
    else:
        print('no change needed')


if __name__ == '__main__':
    main()
