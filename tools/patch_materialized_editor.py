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
RUNTIME_PATH = 'native-ui/materialized/runtime.js'
SOURCE_PATH = 'resources/editor.html'

EDITS = [
    ('native menu lookup uses the document selector surface',
     '    const root = document.querySelector(\'[data-spectr-menu-root="\' + key + \'"]\');\n'
     '    if (!root) return;\n'
     '    const triggerHost = root.querySelector("[data-spectr-menu-trigger]");\n'
     '    const trigger = triggerHost && (triggerHost.matches("button") ? triggerHost : triggerHost.querySelector("button"));',
     '    const rootSelector = \'[data-spectr-menu-root="\' + key + \'"]\';\n'
     '    const root = document.querySelector(rootSelector);\n'
     '    if (!root) return;\n'
     '    // The native materialized DOM intentionally exposes selectors on document,\n'
     '    // not on every element wrapper. Keep these scoped through the root selector\n'
     '    // so the same menu contract works in both native and browser hosts.\n'
     '    const triggerHost = document.querySelector(rootSelector + " [data-spectr-menu-trigger]");\n'
     '    const trigger = triggerHost && (triggerHost.tagName === "BUTTON" ? triggerHost : document.querySelector(rootSelector + " [data-spectr-menu-trigger] button"));'),

    ('native menu option lookup uses the document selector surface',
     '      const options = Array.from(root.querySelectorAll("[data-spectr-menu-options] button:not([disabled])"));',
     '      const options = Array.from(document.querySelectorAll(\n'
     '        rootSelector + " [data-spectr-menu-options] button:not([disabled])"));'),

    ('native mode publisher',
     '  const [analyzerMode, setAnalyzerMode] = useAppS("peak");\n'
     '  const [status, setStatus] = useAppS("");',
     '  const [analyzerMode, setAnalyzerMode] = useAppS("peak");\n'
     '  window.spectrPublishMode = (kind, value) => {\n'
     '    if (!nativeBridgeAvailable) return;\n'
     '    try {\n'
     '      Promise.resolve(window.pulp.postMessage("mode_set", { kind, value }, "spectr-mode-" + kind)).catch((error) => console.error("[Spectr] native mode write failed", error));\n'
     '    } catch (error) {\n'
     '      console.error("[Spectr] native mode write failed", error);\n'
     '    }\n'
     '  };\n'
     '  const [status, setStatus] = useAppS("");'),

    ('top motion mode publication',
     '      onChange: (v) => setSettings((s) => ({ ...s, motionMode: v })),',
     '      onChange: (v) => {\n'
     '        setSettings((s) => ({ ...s, motionMode: v }));\n'
     '        window.spectrPublishMode("motion", v);\n'
     '      },'),

    ('settings modal motion mode publication',
     'function SettingsModal({ settings, setSettings, onClose }) {\n'
     '  React.useEffect(() => {',
     'function SettingsModal({ settings, setSettings, onClose }) {\n'
     '  const publishMotionMode = (patch) => {\n'
     '    if (patch.motionMode) window.spectrPublishMode("motion", patch.motionMode);\n'
     '  };\n'
     '  React.useEffect(() => {'),

    ('settings and tweaks invoke motion publication',
     '  const persist = (patch) => {\n'
     '    setSettings((s) => ({ ...s, ...patch }));\n'
     '    try {\n'
     '      window.parent.postMessage({ type: "__edit_mode_set_keys", edits: patch }, "*");',
     '  const persist = (patch) => {\n'
     '    setSettings((s) => ({ ...s, ...patch }));\n'
     '    publishMotionMode(patch);\n'
     '    try {\n'
     '      window.parent.postMessage({ type: "__edit_mode_set_keys", edits: patch }, "*");',
     2),

    ('tweaks motion mode publication',
     'function TweaksPanel({ settings, setSettings }) {\n'
     '  const [open, setOpen] = useTS(false);',
     'function TweaksPanel({ settings, setSettings }) {\n'
     '  const publishMotionMode = (patch) => {\n'
     '    if (patch.motionMode) window.spectrPublishMode("motion", patch.motionMode);\n'
     '  };\n'
     '  const [open, setOpen] = useTS(false);'),

    ('keyboard edit mode publication',
     '        setEditMode(modeKeys[k]);\n'
     '        fireStatus("EDIT \\u2192 " + modeKeys[k].toUpperCase());',
     '        setEditMode(modeKeys[k]);\n'
     '        window.spectrPublishMode("edit", modeKeys[k]);\n'
     '        fireStatus("EDIT \\u2192 " + modeKeys[k].toUpperCase());'),

    ('keyboard analyzer mode publication',
     '          const next = m === "peak" ? "avg" : m === "avg" ? "both" : m === "both" ? "off" : "peak";\n'
     '          fireStatus("ANALYZER \\u2192 " + next.toUpperCase());\n'
     '          return next;',
     '          const next = m === "peak" ? "avg" : m === "avg" ? "both" : m === "both" ? "off" : "peak";\n'
     '          window.spectrPublishMode("analyzer", next);\n'
     '          fireStatus("ANALYZER \\u2192 " + next.toUpperCase());\n'
     '          return next;'),

    ('canvas edit mode publication',
     '    setEditMode(v);\n'
     '    fireStatus(`EDIT \\u2192 ${v.toUpperCase()}`);\n'
     '  } }),',
     '    setEditMode(v);\n'
     '    window.spectrPublishMode("edit", v);\n'
     '    fireStatus(`EDIT \\u2192 ${v.toUpperCase()}`);\n'
     '  } }),'),

    ('chrome visualization mode publication',
     '        setVisualizationMode(v);\n'
     '        fireStatus(`VIEW \\u2192 ${v.toUpperCase()}`);',
     '        setVisualizationMode(v);\n'
     '        window.spectrPublishMode("visualization", v);\n'
     '        fireStatus(`VIEW \\u2192 ${v.toUpperCase()}`);'),

    ('chrome edit mode publication',
     '        setEditMode(v);\n'
     '        fireStatus(`EDIT \\u2192 ${v.toUpperCase()}`);',
     '        setEditMode(v);\n'
     '        window.spectrPublishMode("edit", v);\n'
     '        fireStatus(`EDIT \\u2192 ${v.toUpperCase()}`);'),

    ('chrome analyzer mode publication',
     '        setAnalyzerMode(v);\n'
     '        fireStatus(`ANALYZER \\u2192 ${v.toUpperCase()}`);',
     '        setAnalyzerMode(v);\n'
     '        window.spectrPublishMode("analyzer", v);\n'
     '        fireStatus(`ANALYZER \\u2192 ${v.toUpperCase()}`);'),

    ('animation loop paints the latest canvas renderer',
     '      renderAll();\n'
     '      rafRef.current = requestAnimationFrame(draw);\n'
     '    };',
     '      (renderAllRef.current || renderAll)();\n'
     '      updateLiveHoverStatus();\n'
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

    ('empty status clears the unified banner',
     '  const fireStatus = useAppC((msg) => {\n'
     '    setStatus(msg + "|" + Date.now());',
     '  const fireStatus = useAppC((msg) => {\n'
     '    if (!msg) {\n'
     '      setStatus("");\n'
     '      return;\n'
     '    }\n'
     '    setStatus(msg + "|" + Date.now());'),

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
     '      commitMany(map, true);\n'
     '      return;\n'
     '    }\n'
     '    const held = /* @__PURE__ */ new Map();\n'
     '    for (const [index, value] of map)\n'
     '      held.set(index, isMuted(targetGainsRef.current[index]) ? -Infinity : value);\n'
     '    commitMany(held, true);\n'
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
     '  }, title: "Click to change band count" }, /* @__PURE__ */ React.createElement("span", { className: "tnum", style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, settings.bandCount), " bands \\u25BE")'),

    ('band count trigger reflects selection immediately',
     'React.createElement("span", { className: "tnum", style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, info.N), " bands \\u25BE"',
     'React.createElement("span", { className: "tnum", style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, settings.bandCount), " bands \\u25BE"'),

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

    ('filter surface keeps semantic identity without remapping its native owner',
     '      ref: wrapRef,\n'
     '      id: "spectr-filter-surface",\n'
     '      "data-spectr-filter-surface": true,',
     '      ref: wrapRef,\n'
     '      "data-spectr-filter-surface": true,'),

    ('minimap press uses grabbing cursor',
     '    const mm = minimapHit(x, y, g);\n'
     '    if (mm) {\n'
     '      const fullMin = Math.log10(20), fullMax = Math.log10(2e4);',
     '    const mm = minimapHit(x, y, g);\n'
     '    if (mm) {\n'
     '      wrapRef.current.style.cursor = "grabbing";\n'
     '      const fullMin = Math.log10(20), fullMax = Math.log10(2e4);'),

    ('minimap hover and drag cursors',
     '    if (mm) {\n'
     '      setHover({ mini: mm, x, y, band: -1 });\n'
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right" ? "ew-resize" : mm === "window" ? "grab" : "pointer";',
     '    if (mm) {\n'
     '      setHover({ mini: mm, x, y, band: -1 });\n'
     '      const activeMini = pointerRef.current\n'
     '        && (pointerRef.current.mode === "minimap-drag"\n'
     '          || pointerRef.current.mode === "minimap-resize");\n'
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right"\n'
     '        ? "ew-resize"\n'
     '        : activeMini ? "grabbing" : mm === "window" ? "grab" : "pointer";'),

    ('minimap release restores grab cursor',
     '  const onPointerUp = (e) => {\n'
     '    const p = pointerRef.current;\n'
     '    pointerRef.current = { mode: null };\n'
     '    if (!p || !p.mode) {',
     '  const onPointerUp = (e) => {\n'
     '    const p = pointerRef.current;\n'
     '    pointerRef.current = { mode: null };\n'
     '    if (p && p.mode === "minimap-drag") wrapRef.current.style.cursor = "grab";\n'
     '    if (p && p.mode === "minimap-resize") wrapRef.current.style.cursor = "ew-resize";\n'
     '    if (!p || !p.mode) {'),

    ('response ticks omit first and last edges',
     '      for (let i = 0; i <= N; i++) {\n'
     '        const x = inner.x + i * (bandW + bandGap) + 0.5;',
     '      for (let i = 1; i < N; i++) {\n'
     '        const x = inner.x + i * (bandW + bandGap) + 0.5;'),

    ('minimap edges retain horizontal resize cursor',
     '      wrapRef.current.style.cursor = activeMini ? "grabbing" : "grab";',
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right"\n'
     '        ? "ew-resize"\n'
     '        : activeMini ? "grabbing" : mm === "window" ? "grab" : "pointer";'),

    ('minimap release retains physical cursor',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize"))\n'
     '      wrapRef.current.style.cursor = "grab";',
     '    if (p && p.mode === "minimap-drag") wrapRef.current.style.cursor = "grab";\n'
     '    if (p && p.mode === "minimap-resize") wrapRef.current.style.cursor = "ew-resize";'),

    ('minimap deferred release retains physical cursor',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize")) {\n'
     '      setView({ ...viewRef.current });\n'
     '      wrapRef.current.style.cursor = "grab";\n'
     '    }',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize")) {\n'
     '      setView({ ...viewRef.current });\n'
     '      wrapRef.current.style.cursor = p.mode === "minimap-resize" ? "ew-resize" : "grab";\n'
     '    }'),

    ('status info defaults on',
     '  "showRulers": true,\n  "scheme": "midnight",',
     '  "showRulers": true,\n  "statusInfo": true,\n  "scheme": "midnight",'),

    ('disabled status info suppresses all messages',
     '  const fireStatus = useAppC((msg) => {\n'
     '    if (!msg) {',
     '  const fireStatus = useAppC((msg) => {\n'
     '    if (settings.statusInfo === false) {\n'
     '      setStatus("");\n'
     '      return;\n'
     '    }\n'
     '    if (!msg) {'),

    ('status disable clears immediately and selected preset is retained',
     '  }, []);\n'
     '  const applyPattern = useAppC((p) => {',
     '  }, [settings.statusInfo]);\n'
     '  useAppE(() => {\n'
     '    if (settings.statusInfo === false) setStatus("");\n'
     '  }, [settings.statusInfo]);\n'
     '  const [selectedPatternName, setSelectedPatternName] = useAppS("PRESETS");\n'
     '  const applyPattern = useAppC((p) => {'),

    ('selected preset name updates with applied state',
     '    b.setGains(gains);\n'
     '    fireStatus(`APPLIED "${p.name}"`);',
     '    b.setGains(gains);\n'
     '    setSelectedPatternName(p.name);\n'
     '    fireStatus(`APPLIED "${p.name}"`);'),

    ('chrome receives selected preset name',
     'function Chrome({ settings, setSettings, bankRef, info, status, dspMode,',
     'function Chrome({ settings, setSettings, bankRef, info, status, selectedPatternName, dspMode,'),

    ('preset trigger shows selected name',
     'React.createElement("span", { style: { marginLeft: 6, display: "inline-flex", alignItems: "center", lineHeight: 1 } }, "PRESETS \\u25BE")',
     'React.createElement("span", { "data-spectr-selected-preset": true, style: { marginLeft: 6, display: "inline-flex", alignItems: "center", lineHeight: 1 } }, selectedPatternName, " \\u25BE")'),

    ('selected preset name reaches chrome',
     '      status,\n'
     '      dspMode,',
     '      status,\n'
     '      selectedPatternName,\n'
     '      dspMode,'),

    ('rail buttons accept semantic popup kind',
     'function RailBtn({ children, onClick, active }) {\n'
     '  const [flash, setFlash] = useStateChrome(false);',
     'function RailBtn({ children, onClick, active, popupKind }) {\n'
     '  const [flash, setFlash] = useStateChrome(false);'),

    ('rail popup trigger semantics',
     '  return /* @__PURE__ */ React.createElement("button", { onClick: handle, style: {',
     '  return /* @__PURE__ */ React.createElement("button", { "data-spectr-menu-trigger": popupKind ? true : void 0, "aria-haspopup": popupKind || void 0, "aria-expanded": popupKind ? active : void 0, onClick: handle, style: {'),

    ('overflow popup semantics',
     'React.createElement(RailBtn, { onClick: () => setOverflowMenu((v) => !v), active: overflowMenu },',
     'React.createElement(RailBtn, { popupKind: "menu", onClick: () => setOverflowMenu((v) => !v), active: overflowMenu },'),

    ('edit popup semantics',
     'React.createElement(RailBtn, { onClick: () => toggleMenu("edit"), active: editMenu },',
     'React.createElement(RailBtn, { popupKind: "listbox", onClick: () => toggleMenu("edit"), active: editMenu },'),

    ('analyzer popup semantics',
     'React.createElement(RailBtn, { onClick: () => toggleMenu("analyzer"), active: analyzerMenu },',
     'React.createElement(RailBtn, { popupKind: "listbox", onClick: () => toggleMenu("analyzer"), active: analyzerMenu },'),

    ('preset popup semantics',
     'React.createElement(RailBtn, { onClick: () => setPatternMenu((v) => !v), active: patternMenu },',
     'React.createElement(RailBtn, { popupKind: "menu", onClick: () => setPatternMenu((v) => !v), active: patternMenu },'),

    ('rail popup semantics survive source ordering',
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "button",\n'
     '    {\n'
     '      onClick: handle,',
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "button",\n'
     '    {\n'
     '      "data-spectr-menu-trigger": popupKind ? true : void 0,\n'
     '      "aria-haspopup": popupKind || void 0,\n'
     '      "aria-expanded": popupKind ? active : void 0,\n'
     '      onClick: handle,'),

    ('rail popup trigger is the interactive button',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn',
     'React.createElement("span", null, /* @__PURE__ */ React.createElement(RailBtn',
     4),

    ('generic Pulp popup owns keyboard and outside dismissal',
     '  useEffectChrome(() => {\n'
     '    const key = openMenu || (helpOpen ? "help" : null);\n'
     '    if (!key) return;\n'
     '    const rootSelector = \'[data-spectr-menu-root="\' + key + \'"]\';\n'
     '    const root = document.querySelector(rootSelector);\n'
     '    if (!root) return;\n'
     '    // The native materialized DOM intentionally exposes selectors on document,\n'
     '    // not on every element wrapper. Keep these scoped through the root selector\n'
     '    // so the same menu contract works in both native and browser hosts.\n'
     '    const triggerHost = document.querySelector(rootSelector + " [data-spectr-menu-trigger]");\n'
     '    const trigger = triggerHost && (triggerHost.tagName === "BUTTON" ? triggerHost : document.querySelector(rootSelector + " [data-spectr-menu-trigger] button"));\n'
     '    const close = (returnFocus) => {\n'
     '      restoreMenuFocus.current = !!returnFocus;\n'
     '      if (key === "help") setHelpOpen(false);\n'
     '      else setOpenMenu(null);\n'
     '    };\n'
     '    const onPointer = (event) => {\n'
     '      if (!root.contains(event.target)) close(false);\n'
     '    };\n'
     '    const onKey = (event) => {\n'
     '      if (event.key === "Escape") {\n'
     '        event.preventDefault();\n'
     '        event.stopPropagation();\n'
     '        close(true);\n'
     '        return;\n'
     '      }\n'
     '      const options = Array.from(document.querySelectorAll(\n'
     '        rootSelector + " [data-spectr-menu-options] button:not([disabled])"));\n'
     '      if (!options.length) return;\n'
     '      if (event.key === "ArrowDown" || event.key === "ArrowUp" || event.key === "Home" || event.key === "End") {\n'
     '        event.preventDefault();\n'
     '        event.stopPropagation();\n'
     '        const current = options.indexOf(document.activeElement);\n'
     '        const next = event.key === "Home" ? 0 : event.key === "End" ? options.length - 1 : event.key === "ArrowDown" ? (current + 1 + options.length) % options.length : (current - 1 + options.length) % options.length;\n'
     '        options[next].focus();\n'
     '      } else if ((event.key === "Enter" || event.key === " ") && options.includes(document.activeElement)) {\n'
     '        event.preventDefault();\n'
     '        event.stopPropagation();\n'
     '        restoreMenuFocus.current = true;\n'
     '        document.activeElement.click();\n'
     '      }\n'
     '    };\n'
     '    document.addEventListener("mousedown", onPointer);\n'
     '    document.addEventListener("keydown", onKey, true);\n'
     '    return () => {\n'
     '      document.removeEventListener("mousedown", onPointer);\n'
     '      document.removeEventListener("keydown", onKey, true);\n'
     '      if (restoreMenuFocus.current) {\n'
     '        restoreMenuFocus.current = false;\n'
     '        requestAnimationFrame(() => trigger && trigger.focus());\n'
     '      }\n'
     '    };\n'
     '  }, [openMenu, helpOpen]);\n',
     ''),

    ('generic Pulp popup owns focus restoration',
     '  const restoreMenuFocus = useRefChrome(false);\n',
     ''),

    ('generic Pulp popup owns settings picker navigation',
     '  React.useEffect(() => {\n'
     '    if (!open) return;\n'
     '    const selected = Math.max(0, options.findIndex((o) => o.k === value));\n'
     '    setActiveIndex(selected);\n'
     '    requestAnimationFrame(() => {\n'
     '      const buttons = listRef.current ? Array.from(listRef.current.querySelectorAll("button:not([disabled])")) : [];\n'
     '      if (buttons[selected]) buttons[selected].focus();\n'
     '    });\n'
     '    const close = (returnFocus) => {\n'
     '      setOpen(false);\n'
     '      if (returnFocus) requestAnimationFrame(() => triggerRef.current && triggerRef.current.focus());\n'
     '    };\n'
     '    const onDoc = (e) => {\n'
     '      if (ref.current && !ref.current.contains(e.target)) close(false);\n'
     '    };\n'
     '    const onKey = (e) => {\n'
     '      if (e.key === "Escape") {\n'
     '        e.preventDefault();\n'
     '        e.stopPropagation();\n'
     '        close(true);\n'
     '        return;\n'
     '      }\n'
     '      if (e.key !== "ArrowDown" && e.key !== "ArrowUp" && e.key !== "Home" && e.key !== "End") return;\n'
     '      e.preventDefault();\n'
     '      e.stopPropagation();\n'
     '      const next = e.key === "Home" ? 0 : e.key === "End" ? options.length - 1 : e.key === "ArrowDown" ? (activeIndex + 1) % options.length : (activeIndex - 1 + options.length) % options.length;\n'
     '      setActiveIndex(next);\n'
     '      const buttons = listRef.current ? Array.from(listRef.current.querySelectorAll("button:not([disabled])")) : [];\n'
     '      if (buttons[next]) buttons[next].focus();\n'
     '    };\n'
     '    document.addEventListener("mousedown", onDoc);\n'
     '    document.addEventListener("keydown", onKey, true);\n'
     '    return () => {\n'
     '      document.removeEventListener("mousedown", onDoc);\n'
     '      document.removeEventListener("keydown", onKey, true);\n'
     '    };\n'
     '  }, [open, activeIndex, options, value]);\n',
     ''),

    ('generic Pulp popup restores settings trigger focus',
     '          setOpen(false);\n'
     '          requestAnimationFrame(() => triggerRef.current && triggerRef.current.focus());',
     '          setOpen(false);'),

    ('surface leave resets idle cursor',
     '      onPointerLeave: () => setHover(null),',
     '      onPointerLeave: () => {\n'
     '        setHover(null);\n'
     '        if (!pointerRef.current || !pointerRef.current.mode)\n'
     '          wrapRef.current.style.cursor = "default";\n'
     '      },'),

    ('draw commits defer React state until release',
     '  const commitGain = (idx, value) => {',
     '  const commitGain = (idx, value, deferReact = false) => {'),

    ('single gain commit can skip React reconciliation',
     '    setGains((prev) => {\n'
     '      const nxt = prev.slice();\n'
     '      nxt[idx] = value;\n'
     '      return nxt;\n'
     '    });\n'
     '    queueNativeProcessingStatePublication();',
     '    if (!deferReact) setGains((prev) => {\n'
     '      const nxt = prev.slice();\n'
     '      nxt[idx] = value;\n'
     '      return nxt;\n'
     '    });\n'
     '    queueNativeProcessingStatePublication();'),

    ('multi gain commits defer React state until release',
     '  const commitMany = (map) => {',
     '  const commitMany = (map, deferReact = false) => {'),

    ('multi gain commit can skip React reconciliation',
     '    targetGainsRef.current = nextTarget;\n'
     '    setGains((prev) => {',
     '    targetGainsRef.current = nextTarget;\n'
     '    if (!deferReact) setGains((prev) => {'),

    ('unmuted drawing defers React reconciliation',
     '      commitMany(map);\n'
     '      return;',
     '      commitMany(map, true);\n'
     '      return;'),

    ('held-muted drawing defers React reconciliation',
     '    commitMany(held);\n'
     '  };\n'
     '  const editBaseGain',
     '    commitMany(held, true);\n'
     '  };\n'
     '  const editBaseGain'),

    ('mute brush defers React reconciliation',
     '        commitGain(index, shouldMute ? -Infinity : isMuted(current) ? restored : current);',
     '        commitGain(index, shouldMute ? -Infinity : isMuted(current) ? restored : current, true);'),

    ('drawing keeps live hover outside React reconciliation',
     '  const [hover, setHover] = useState(null);\n'
     '  const hoverBand = hover && !hover.mini ? hover.band : -1;',
     '  const [hover, setHover] = useState(null);\n'
     '  const hoverRef = useRef(null);\n'
     '  const hoverBand = hover && !hover.mini ? hover.band : -1;\n'
     '  const updatePointerHover = (next) => {\n'
     '    hoverRef.current = next;\n'
     '    if (!pointerRef.current || !pointerRef.current.mode) setHover(next);\n'
     '  };\n'
     '    if (!pointerRef.current || !pointerRef.current.mode) setHover(next);\n'
     '  };\n'
     '  const updateLiveHoverStatus = () => {\n'
     '    const current = hoverRef.current;\n'
     '    const pointer = pointerRef.current;\n'
     '    if (!current || current.mini || !pointer || pointer.mode !== "gain" && pointer.mode !== "mute-brush") return;\n'
     '    const band = current.band;\n'
     '    const rendered = renderGainsRef.current[band];\n'
     '    const gv = Number.isFinite(rendered) ? clamp(rendered, -1.02, 1.02) : 0;\n'
     '    const db = isMuted(targetGainsRef.current[band]) ? "\\u2212\\u221E" : (gv * 24).toFixed(1);\n'
     '    const label = `${window.SpectrFreq.fmt(bandCenterFreq(band))}Hz   ${db}${db === "\\u2212\\u221E" ? "" : " dB"}   BAND ${band + 1}/${N}`;\n'
     '    const shell = document.querySelector("[data-spectr-status-shell]");\n'
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!shell || !text) return;\n'
     '    text.textContent = label;\n'
     '    shell.dataset.spectrStatusBanner = "true";\n'
     '    shell.style.width = Math.max(96, Math.min(520, label.length * 8 + 28)) + "px";\n'
     '    shell.style.opacity = "1";\n'
     '    shell.style.background = "rgba(12,16,22,0.92)";\n'
     '    shell.style.borderColor = "rgba(180,210,255,0.3)";\n'
     '    shell.style.color = "rgba(200,220,255,0.95)";\n'
     '  };'),

    ('live drawing status follows the paint clock',
     '      (renderAllRef.current || renderAll)();\n'
     '      rafRef.current = requestAnimationFrame(draw);',
     '      (renderAllRef.current || renderAll)();\n'
     '      updateLiveHoverStatus();\n'
     '      rafRef.current = requestAnimationFrame(draw);'),

    ('hover guide reads the pointer-owned ref',
     '    if (!hover || hover.mini) return;\n'
     '    const { x } = hover;',
     '    const currentHover = hoverRef.current || hover;\n'
     '    if (!currentHover || currentHover.mini) return;\n'
     '    const { x } = currentHover;'),

    ('minimap hover uses the pointer-owned ref',
     '    if (mm) {\n'
     '      setHover({ mini: mm, x, y, band: -1 });',
     '    if (mm) {\n'
     '      updatePointerHover({ mini: mm, x, y, band: -1 });'),

    ('band hover uses the pointer-owned ref',
     '      setHover({ band: bandH, x, y });\n'
     '      wrapRef.current.style.cursor = "crosshair";',
     '      updatePointerHover({ band: bandH, x, y });\n'
     '      wrapRef.current.style.cursor = "crosshair";'),

    ('idle hover clearing uses the pointer-owned ref',
     '      setHover(null);\n'
     '      wrapRef.current.style.cursor = "default";',
     '      updatePointerHover(null);\n'
     '      wrapRef.current.style.cursor = "default";'),

    ('pointer release publishes one final React snapshot',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize"))\n'
     '      wrapRef.current.style.cursor = "grab";\n'
     '    if (!p || !p.mode) {\n'
     '      setMarquee(null);',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize"))\n'
     '      wrapRef.current.style.cursor = "grab";\n'
     '    if (p && (p.mode === "gain" || p.mode === "mute-brush")) {\n'
     '      setGains(targetGainsRef.current.slice());\n'
     '      setHover(hoverRef.current);\n'
     '    }\n'
     '    if (!p || !p.mode) {\n'
     '      setMarquee(null);'),

    ('browser oracle exposes the semantic React snapshot',
     '      gains: Array.from(renderGainsRef.current),\n'
     '      targetGains: Array.from(targetGainsRef.current),',
     '      gains: Array.from(renderGainsRef.current),\n'
     '      reactGains: Array.from(gains),\n'
     '      targetGains: Array.from(targetGainsRef.current),'),

    ('viewport keeps a live ref beside its React snapshot',
     '  const [view, setView] = useState({ lmin: Math.log10(20), lmax: Math.log10(2e4) });',
     '  const initialView = { lmin: Math.log10(20), lmax: Math.log10(2e4) };\n'
     '  const [reactView, setReactView] = useState(initialView);\n'
     '  const viewRef = useRef({ ...initialView });\n'
     '  const view = viewRef.current;\n'
     '  const setView = (next) => {\n'
     '    const resolved = typeof next === "function" ? next(viewRef.current) : next;\n'
     '    viewRef.current = { ...resolved };\n'
     '    setReactView({ ...resolved });\n'
     '  };'),

    ('minimap viewport publishes without reconciling React',
     '  };\n'
     '  const commitGain = (idx, value, deferReact = false) => {',
     '  };\n'
     '  const commitLiveViewport = (next) => {\n'
     '    viewRef.current.lmin = next.lmin;\n'
     '    viewRef.current.lmax = next.lmax;\n'
     '    queueNativeProcessingStatePublication();\n'
     '  };\n'
     '  // Gain commits are independent of the live viewport publication lane.\n'
     '  const commitGain = (idx, value, deferReact = false) => {'),

    ('minimap handle resize stays off the React hot path',
     '      if (p.edge === "left") lmin = clamp(f, fullMin, lmax - 0.1);\n'
     '      else lmax = clamp(f, lmin + 0.1, fullMax);\n'
     '      setView({ lmin, lmax });\n'
     '      return;',
     '      if (p.edge === "left") lmin = clamp(f, fullMin, lmax - 0.1);\n'
     '      else lmax = clamp(f, lmin + 0.1, fullMax);\n'
     '      commitLiveViewport({ lmin, lmax });\n'
     '      return;'),

    ('minimap center pan stays off the React hot path',
     '      let lmin = clamp(p.viewStart.lmin + shift, fullMin, fullMax - span);\n'
     '      setView({ lmin, lmax: lmin + span });\n'
     '      return;',
     '      let lmin = clamp(p.viewStart.lmin + shift, fullMin, fullMax - span);\n'
     '      commitLiveViewport({ lmin, lmax: lmin + span });\n'
     '      return;'),

    ('minimap release publishes one final React viewport',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize"))\n'
     '      wrapRef.current.style.cursor = "grab";\n'
     '    if (p && (p.mode === "gain" || p.mode === "mute-brush")) {',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize")) {\n'
     '      setView({ ...viewRef.current });\n'
     '      wrapRef.current.style.cursor = "grab";\n'
     '    }\n'
     '    if (p && (p.mode === "gain" || p.mode === "mute-brush")) {'),

    ('viewport oracle exposes live and React snapshots',
     '      view: { ...view },\n'
     '      nVisible: N,',
     '      view: { ...viewRef.current },\n'
     '      reactView: { ...reactView },\n'
     '      nVisible: N,'),

    ('live hover label has one immediate source',
     '    if (!pointerRef.current || !pointerRef.current.mode) setHover(next);\n'
     '  };\n'
     '  const updateLiveHoverStatus = () => {\n'
     '    const current = hoverRef.current;\n'
     '    const pointer = pointerRef.current;',
     '    if (!pointerRef.current || !pointerRef.current.mode) setHover(next);\n'
     '  };\n'
     '  const liveHoverLabel = (current) => {\n'
     '    if (!current || current.mini) return "";\n'
     '    const band = current.band;\n'
     '    const rendered = renderGainsRef.current[band];\n'
     '    const gv = Number.isFinite(rendered) ? clamp(rendered, -1.02, 1.02) : 0;\n'
     '    const db = isMuted(targetGainsRef.current[band]) ? "\\u2212\\u221E" : (gv * 24).toFixed(1);\n'
     '    return window.SpectrFreq.fmt(bandCenterFreq(band)) + "Hz   " + db + (db === "\\u2212\\u221E" ? "" : " dB") + "   BAND " + (band + 1) + "/" + N;\n'
     '  };\n'
     '  const updateLiveHoverStatus = () => {\n'
     '    const current = hoverRef.current;\n'
     '    const pointer = pointerRef.current;'),

    ('live hover status reuses the current label',
     '    const band = current.band;\n'
     '    const rendered = renderGainsRef.current[band];\n'
     '    const gv = Number.isFinite(rendered) ? clamp(rendered, -1.02, 1.02) : 0;\n'
     '    const db = isMuted(targetGainsRef.current[band]) ? "\\u2212\\u221E" : (gv * 24).toFixed(1);\n'
     '    const label = `${window.SpectrFreq.fmt(bandCenterFreq(band))}Hz   ${db}${db === "\\u2212\\u221E" ? "" : " dB"}   BAND ${band + 1}/${N}`;',
     '    const label = liveHoverLabel(current);'),

    ('live status leaves visibility chrome React-owned',
     '    text.textContent = label;\n'
     '    shell.dataset.spectrStatusBanner = "true";\n'
     '    shell.style.width = Math.max(96, Math.min(520, label.length * 8 + 28)) + "px";\n'
     '    shell.style.opacity = "1";\n'
     '    shell.style.background = "rgba(12,16,22,0.92)";\n'
     '    shell.style.borderColor = "rgba(180,210,255,0.3)";\n'
     '    shell.style.color = "rgba(200,220,255,0.95)";',
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!text) return;\n'
     '    text.textContent = label;'),

    ('release returns live status ownership to React',
     '      setGains(targetGainsRef.current.slice());\n'
     '      setHover(hoverRef.current);\n'
     '    }\n'
     '    if (!p || !p.mode) {',
     '      setGains(targetGainsRef.current.slice());\n'
     '      setHover(hoverRef.current);\n'
     '      if (onStatus) onStatus(liveHoverLabel(hoverRef.current));\n'
     '    }\n'
     '    if (!p || !p.mode) {'),

    ('live status uses the materialized text surface only',
     '    const shell = document.querySelector("[data-spectr-status-shell]");\n'
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!shell || !text) return;\n'
     '    text.textContent = label;\n'
     '    shell.style.width = Math.max(96, Math.min(520, label.length * 8 + 28)) + "px";',
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!text) return;\n'
     '    text.textContent = label;'),

    ('live status avoids descendant queries in the emitted runtime',
     '    const shell = document.querySelector("[data-spectr-status-shell]");\n'
     '    const text = shell && shell.querySelector("[data-spectr-status-text]");\n'
     '    if (!shell || !text) return;\n'
     '    text.textContent = label;\n'
     '    shell.style.width = Math.max(96, Math.min(520, label.length * 8 + 28)) + "px";',
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!text) return;\n'
     '    text.textContent = label;'),

    ('performance fixture remains outside the shipping authored state',
     '  const [settings, setSettings] = useAppS(() => globalThis.__spectrBandsPerfFixture\n'
     '    ? { ...defaults, bandCount: 64 } : defaults);',
     '  const [settings, setSettings] = useAppS(defaults);'),

    ('hover guide treats cleared ref as authoritative',
     '    const currentHover = hoverRef.current || hover;',
     '    const currentHover = hoverRef.current;'),

    ('surface leave clears the pointer hover ref',
     '      onPointerLeave: () => {\n'
     '        setHover(null);',
     '      onPointerLeave: () => {\n'
     '        updatePointerHover(null);'),

    ('status clear is cancellable and repaints its old bounds',
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
     '  }, [message]);',
     '  const shownRef = useRefChrome("");\n'
     '  const generationRef = useRefChrome(0);\n'
     '  useEffectChrome(() => {\n'
     '    const generation = ++generationRef.current;\n'
     '    const display = message ? message.split("|")[0].trim() : "";\n'
     '    const hide = (delay) => setTimeout(() => {\n'
     '      if (generation !== generationRef.current) return;\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '      requestAnimationFrame(() => window.dispatchEvent(new Event("resize")));\n'
     '    }, delay);\n'
     '    if (!display) {\n'
     '      const timer2 = hide(120);\n'
     '      return () => clearTimeout(timer2);\n'
     '    }\n'
     '    setText(display);\n'
     '    setVisible(true);\n'
     '    shownRef.current = display;\n'
     '    const timer = hide(1400);\n'
     '    return () => clearTimeout(timer);\n'
     '  }, [message]);'),

    ('status banner accepts the persistent disable state',
     'function StatusBanner({ message }) {',
     'function StatusBanner({ message, disabled }) {'),

    ('disabled status banner clears synchronously',
     '    const generation = ++generationRef.current;\n'
     '    const display = message ? message.split("|")[0].trim() : "";',
     '    const generation = ++generationRef.current;\n'
     '    if (disabled) {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '      requestAnimationFrame(() => window.dispatchEvent(new Event("resize")));\n'
     '      return;\n'
     '    }\n'
     '    const display = message ? message.split("|")[0].trim() : "";'),

    ('status disable invalidates pending banner effects',
     '  }, [message]);\n'
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "div",\n'
     '    {\n'
     '      "data-spectr-status-shell": "true",',
     '  }, [message, disabled]);\n'
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "div",\n'
     '    {\n'
     '      "data-spectr-status-shell": "true",'),

    ('status banner receives the persistent setting',
     'React.createElement(StatusBanner, { message: status })',
     'React.createElement(StatusBanner, { message: status, disabled: settings.statusInfo === false })'),

    ('settings groups fit the authored viewport without clipping rows',
     'function SpectrSettingsGroup({ title, subtitle, children }) {\n'
     '  return /* @__PURE__ */ React.createElement("div", { style: { marginBottom: 22 } },',
     'function SpectrSettingsGroup({ title, subtitle, children, marker }) {\n'
     '  return /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-group": marker, style: { marginBottom: 18 } },'),

    ('status info no longer adds an uncaptured settings row',
     ')), /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Rulers", hint: "Frequency labels along the bottom axis" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: settings.showRulers, onChange: (v) => persist({ showRulers: v }) })), /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Status info", hint: "Show hover, mute, and drag feedback" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { statusInfo: true, value: settings.statusInfo !== false, onChange: (v) => persist({ statusInfo: v }) }))), /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { title: "MOTION",',
     ')), /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Rulers", hint: "Frequency labels along the bottom axis" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: settings.showRulers, onChange: (v) => persist({ showRulers: v }) }))), /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { title: "MOTION",'),

    ('settings status control preserves the captured child count',
     '    position: "relative",\n'
     '    width: 520,',
     '    width: 520,'),

    ('settings status control reuses the captured title node',
     '  } }, /* @__PURE__ */ React.createElement("button", {\n'
     '    "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on",\n'
     '    role: "switch",\n'
     '    "aria-checked": settings.statusInfo !== false,\n'
     '    onClick: () => persist({ statusInfo: settings.statusInfo === false }),\n'
     '    style: {\n'
     '      position: "absolute", top: 26, right: 68, height: 32, padding: "0 10px",\n'
     '      borderRadius: 3, cursor: "pointer",\n'
     '      background: settings.statusInfo === false ? "rgba(255,255,255,0.03)" : "rgba(120,180,255,0.18)",\n'
     '      border: "1px solid " + (settings.statusInfo === false ? "rgba(255,255,255,0.1)" : "rgba(180,210,255,0.4)"),\n'
     '      color: settings.statusInfo === false ? "rgba(255,255,255,0.65)" : "#fff",\n'
     '      fontFamily: "var(--mono)", fontSize: 9, letterSpacing: 1\n'
     '    }\n'
     '  }, "STATUS INFO · ", settings.statusInfo === false ? "OFF" : "ON"), /* @__PURE__ */ React.createElement("div", { style: { display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 22 } },',
     '  } }, /* @__PURE__ */ React.createElement("div", { style: { display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 22 } },'),

    ('settings title exposes the persistent status control',
     '/* @__PURE__ */ React.createElement("div", null, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 14, letterSpacing: 2, fontWeight: 600 } }, "SETTINGS"))',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 12, letterSpacing: 1.2, fontWeight: 600, color: settings.statusInfo === false ? "rgba(255,255,255,0.45)" : "rgba(200,220,255,0.95)" } }, "SETTINGS · STATUS INFO"))'),

    ('settings status label stays stable across toggle repaint',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 11, letterSpacing: 1.2, fontWeight: 600, padding: "7px 10px", borderRadius: 3, background: settings.statusInfo === false ? "rgba(255,255,255,0.03)" : "rgba(120,180,255,0.18)", border: "1px solid " + (settings.statusInfo === false ? "rgba(255,255,255,0.1)" : "rgba(180,210,255,0.4)") } }, "SETTINGS · STATUS INFO ", settings.statusInfo === false ? "OFF" : "ON"))',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 12, letterSpacing: 1.2, fontWeight: 600, color: settings.statusInfo === false ? "rgba(255,255,255,0.45)" : "rgba(200,220,255,0.95)" } }, "SETTINGS · STATUS INFO"))'),

    ('settings status title uses color without a frozen-width box',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 11, letterSpacing: 1.2, fontWeight: 600, padding: "7px 10px", borderRadius: 3, background: settings.statusInfo === false ? "rgba(255,255,255,0.03)" : "rgba(120,180,255,0.18)", border: "1px solid " + (settings.statusInfo === false ? "rgba(255,255,255,0.1)" : "rgba(180,210,255,0.4)") } }, "SETTINGS · STATUS INFO"))',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 12, letterSpacing: 1.2, fontWeight: 600, color: settings.statusInfo === false ? "rgba(255,255,255,0.45)" : "rgba(200,220,255,0.95)" } }, "SETTINGS · STATUS INFO"))'),

    ('settings header separates title and status control in one captured line',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 12, letterSpacing: 1.2, fontWeight: 600, color: settings.statusInfo === false ? "rgba(255,255,255,0.45)" : "rgba(200,220,255,0.95)" } }, "SETTINGS · STATUS INFO"))',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 12, letterSpacing: 1.2, fontWeight: 600, whiteSpace: "pre", color: settings.statusInfo === false ? "rgba(255,255,255,0.45)" : "rgba(200,220,255,0.95)" } }, "SETTINGS            STATUS INFO ON/OFF"))'),

    ('settings toggle does not reshape text during materialized updates',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 12, letterSpacing: 1.2, fontWeight: 600, whiteSpace: "pre", color: settings.statusInfo === false ? "rgba(255,255,255,0.45)" : "rgba(200,220,255,0.95)" } }, "SETTINGS            STATUS INFO: " + (settings.statusInfo === false ? "OFF" : "ON ")))',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 12, letterSpacing: 1.2, fontWeight: 600, whiteSpace: "pre", color: settings.statusInfo === false ? "rgba(255,255,255,0.45)" : "rgba(200,220,255,0.95)" } }, "SETTINGS            STATUS INFO ON/OFF"))'),

    ('settings title is distinct from the status control',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 12, letterSpacing: 1.2, fontWeight: 600, whiteSpace: "pre", color: settings.statusInfo === false ? "rgba(255,255,255,0.45)" : "rgba(200,220,255,0.95)" } }, "SETTINGS            STATUS INFO ON/OFF"))',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-settings-close": true, role: "button", "aria-label": "Close settings", onClick: (event) => { event.stopPropagation(); onClose(); }, style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 14, letterSpacing: 2, fontWeight: 600 } }, "SETTINGS ×"))'),

    ('settings action slot is an intentional status switch',
     '/* @__PURE__ */ React.createElement("button", { "data-spectr-settings-close": true, "aria-label": "Close settings", onClick: (event) => {\n'
     '    event.stopPropagation();\n'
     '    onClose();\n'
     '  }, style: {\n'
     '    background: "transparent",\n'
     '    border: "none",\n'
     '    color: "rgba(255,255,255,0.6)",\n'
     '    cursor: "pointer",\n'
     '    fontSize: 20,\n'
     '    padding: 0,\n'
     '    lineHeight: 1,\n'
     '    width: 32,\n'
     '    height: 32\n'
     '  } }, "\\xD7")',
     '/* @__PURE__ */ React.createElement("button", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: {\n'
     '    background: settings.statusInfo === false ? "rgba(255,255,255,0.03)" : "rgba(120,180,255,0.18)",\n'
     '    border: "1px solid " + (settings.statusInfo === false ? "rgba(255,255,255,0.1)" : "rgba(180,210,255,0.4)"),\n'
     '    color: settings.statusInfo === false ? "rgba(255,255,255,0.55)" : "rgba(220,235,255,0.95)",\n'
     '    cursor: "pointer", fontFamily: "var(--mono)", fontSize: 9, fontWeight: 600,\n'
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32,\n'
     '    borderRadius: 3, display: "flex", alignItems: "center", justifyContent: "center"\n'
     '  } }, settings.statusInfo === false ? "STATUS INFO OFF" : "STATUS INFO ON")'),

    ('settings status label extends inward from the captured action slot',
     '    cursor: "pointer", fontFamily: "var(--mono)", fontSize: 9, fontWeight: 600,\n'
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32, borderRadius: 3\n',
     '    cursor: "pointer", fontFamily: "var(--mono)", fontSize: 9, fontWeight: 600,\n'
     '    letterSpacing: 0.8, padding: 0, lineHeight: 1, width: 32, height: 32, borderRadius: 3,\n'
     '    textIndent: "-122px", textAlign: "left", whiteSpace: "nowrap", overflow: "visible"\n'),

    ('settings switch occupies an intentional position in the header',
     '    textIndent: "-122px", textAlign: "left", whiteSpace: "nowrap", overflow: "visible"\n',
     '    transform: "translateX(-122px)", textIndent: "40px", textAlign: "left",\n'
     '    whiteSpace: "nowrap", overflow: "visible"\n'),

    ('settings switch label fits the captured action slot',
     '    transform: "translateX(-122px)", textIndent: "40px", textAlign: "left",\n'
     '    whiteSpace: "nowrap", overflow: "visible"\n'
     '  } }, "STATUS INFO ON/OFF")',
     '    textAlign: "center", whiteSpace: "pre-line", overflow: "hidden"\n'
     '  } }, "STATUS", String.fromCharCode(10), "INFO ON/OFF")'),

    ('settings switch type fits without clipping',
     '    cursor: "pointer", fontFamily: "var(--mono)", fontSize: 9, fontWeight: 600,\n'
     '    letterSpacing: 0.8, padding: 0, lineHeight: 1, width: 32, height: 32, borderRadius: 3,\n',
     '    cursor: "pointer", fontFamily: "var(--mono)", fontSize: 5.5, fontWeight: 700,\n'
     '    letterSpacing: 0.1, padding: 0, lineHeight: 1.3, width: 32, height: 32, borderRadius: 3,\n'),

    ('settings switch uses authored width after stale binding removal',
     '    cursor: "pointer", fontFamily: "var(--mono)", fontSize: 5.5, fontWeight: 700,\n'
     '    letterSpacing: 0.1, padding: 0, lineHeight: 1.3, width: 32, height: 32, borderRadius: 3,\n'
     '    textAlign: "center", whiteSpace: "pre-line", overflow: "hidden"\n'
     '  } }, "STATUS", String.fromCharCode(10), "INFO ON/OFF")',
     '    cursor: "pointer", fontFamily: "var(--mono)", fontSize: 9, fontWeight: 600,\n'
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32, borderRadius: 3\n'
     '  } }, "STATUS INFO ON/OFF")'),

    ('settings switch aligns to the header action edge',
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32, borderRadius: 3\n'
     '  } }, "STATUS INFO ON/OFF")',
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32,\n'
     '    borderRadius: 3, transform: "translateX(316px)"\n'
     '  } }, "STATUS INFO ON/OFF")'),

    ('settings switch position comes from the corrected capture binding',
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32,\n'
     '    borderRadius: 3, transform: "translateX(316px)"\n'
     '  } }, "STATUS INFO ON/OFF")',
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32, borderRadius: 3\n'
     '  } }, "STATUS INFO ON/OFF")'),

    ('settings switch label is centered within its control',
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32, borderRadius: 3\n'
     '  } }, "STATUS INFO ON/OFF")',
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32,\n'
     '    borderRadius: 3, display: "flex", alignItems: "center", justifyContent: "center"\n'
     '  } }, "STATUS INFO ON/OFF")'),

    ('settings switch names its actual state',
     '  } }, "STATUS INFO ON/OFF")',
     '  } }, settings.statusInfo === false ? "STATUS INFO OFF" : "STATUS INFO ON")'),

    ('settings title has a direct materialized font target',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-settings-close": true, role: "button", "aria-label": "Close settings", onClick: (event) => { event.stopPropagation(); onClose(); }, style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { style: { fontSize: 14, letterSpacing: 2, fontWeight: 600 } }, "SETTINGS ×"))',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-settings-close": true, role: "button", "aria-label": "Close settings", onClick: (event) => { event.stopPropagation(); onClose(); }, style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-title": true, style: { fontSize: 14, letterSpacing: 2, fontWeight: 600 } }, "SETTINGS ×"))'),

    ('settings header exposes stable sticky identity',
     '/* @__PURE__ */ React.createElement("div", { style: { display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 22 } }, /* @__PURE__ */ React.createElement("div", null, /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-title": true,',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-settings-header": true, style: { display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 22 } }, /* @__PURE__ */ React.createElement("div", null, /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-title": true,'),

    ('ordinary settings toggles stay generic',
     'function SpectrSettingsToggle({ value, onChange, statusInfo }) {\n'
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "button",\n'
     '    {\n'
     '      id: statusInfo ? "spectr-status-info-toggle" : void 0,\n'
     '      "data-spectr-setting-toggle": true,\n'
     '      "data-spectr-status-info-toggle": statusInfo ? "true" : void 0,\n'
     '      "data-spectr-status-info-state": statusInfo ? value ? "on" : "off" : void 0,\n'
     '      role: "switch",',
     'function SpectrSettingsToggle({ value, onChange }) {\n'
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "button",\n'
     '    {\n'
     '      "data-spectr-setting-toggle": true,\n'
     '      role: "switch",'),

    ('settings close interaction state is local to the modal',
     '  }, [onClose]);\n'
     '  const persist = (patch) => {',
     '  }, [onClose]);\n'
     '  const [closeState, setCloseState] = React.useState("idle");\n'
     '  const persist = (patch) => {'),

    ('settings status semantics follow persisted state',
     '  const [closeState, setCloseState] = React.useState("idle");\n'
     '  const persist = (patch) => {',
     '  const [closeState, setCloseState] = React.useState("idle");\n'
     '  React.useEffect(() => {\n'
     '    const toggle = document.getElementById("spectr-status-info-toggle");\n'
     '    if (!toggle) return;\n'
     '    const enabled = settings.statusInfo !== false;\n'
     '    toggle.setAttribute("aria-checked", enabled ? "true" : "false");\n'
     '    toggle.setAttribute("data-spectr-status-info-state", enabled ? "on" : "off");\n'
     '  }, [settings.statusInfo]);\n'
     '  const persist = (patch) => {'),

    ('settings header restores a separate title and close action',
     '/* @__PURE__ */ React.createElement("div", { "data-spectr-settings-close": true, role: "button", "aria-label": "Close settings", onClick: (event) => { event.stopPropagation(); onClose(); }, style: { cursor: "pointer" } }, /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-title": true, style: { fontSize: 14, letterSpacing: 2, fontWeight: 600 } }, "SETTINGS ×"))',
     '/* @__PURE__ */ React.createElement("div", null, /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-title": true, style: { fontSize: 14, letterSpacing: 2, fontWeight: 600 } }, "SETTINGS"))'),

    ('settings close action restores hover and pressed feedback',
     '/* @__PURE__ */ React.createElement("button", { "data-spectr-status-info-toggle": settings.statusInfo === false ? "off" : "on", role: "switch", "aria-checked": settings.statusInfo !== false, onClick: () => persist({ statusInfo: settings.statusInfo === false }), style: {\n'
     '    background: settings.statusInfo === false ? "rgba(255,255,255,0.03)" : "rgba(120,180,255,0.18)",\n'
     '    border: "1px solid " + (settings.statusInfo === false ? "rgba(255,255,255,0.1)" : "rgba(180,210,255,0.4)"),\n'
     '    color: settings.statusInfo === false ? "rgba(255,255,255,0.55)" : "rgba(220,235,255,0.95)",\n'
     '    cursor: "pointer", fontFamily: "var(--mono)", fontSize: 9, fontWeight: 600,\n'
     '    letterSpacing: 0.8, padding: "0 10px", lineHeight: 1, width: 150, height: 32,\n'
     '    borderRadius: 3, display: "flex", alignItems: "center", justifyContent: "center"\n'
     '  } }, settings.statusInfo === false ? "STATUS INFO OFF" : "STATUS INFO ON")',
     '/* @__PURE__ */ React.createElement("button", { "data-spectr-settings-close": true, "data-spectr-close-state": closeState, "aria-label": "Close settings", onPointerEnter: () => setCloseState("hover"), onPointerLeave: () => setCloseState("idle"), onPointerDown: (event) => { event.stopPropagation(); setCloseState("pressed"); }, onPointerUp: () => setCloseState("hover"), onClick: (event) => { event.stopPropagation(); onClose(); }, style: {\n'
     '    background: closeState === "pressed" ? "rgba(180,220,255,0.22)" : closeState === "hover" ? "rgba(255,255,255,0.10)" : "transparent",\n'
     '    border: "1px solid " + (closeState === "pressed" ? "rgba(200,230,255,0.55)" : closeState === "hover" ? "rgba(255,255,255,0.18)" : "transparent"),\n'
     '    color: closeState === "idle" ? "rgba(255,255,255,0.6)" : "#fff",\n'
     '    cursor: "pointer", fontSize: 20, padding: 0, lineHeight: 1, width: 32, height: 32,\n'
     '    borderRadius: 3, display: "flex", alignItems: "center", justifyContent: "center"\n'
     '  } }, "\\xD7")'),

    ('status info is appended as a standard scrollable field',
     '  )))));\n'
     '}',
     '  ))), /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: "feedback", title: "FEEDBACK", subtitle: "Choose which interaction details Spectr shows." }, /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Status info", hint: "Hover, mute, and drag details in the top banner" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { statusInfo: true, value: settings.statusInfo !== false, onChange: (v) => persist({ statusInfo: v }) })))));\n'
     '}'),

    ('status info toggle exposes its semantic state',
     'function SpectrSettingsToggle({ value, onChange }) {\n'
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "button",\n'
     '    {\n'
     '      "data-spectr-setting-toggle": true,\n'
     '      role: "switch",',
     'function SpectrSettingsToggle({ value, onChange, statusInfo = false }) {\n'
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "button",\n'
     '    {\n'
     '      "data-spectr-setting-toggle": true,\n'
     '      "data-spectr-status-info-toggle": statusInfo ? value ? "on" : "off" : void 0,\n'
     '      role: "switch",'),

    ('feedback field marks its status info toggle',
     'React.createElement(SpectrSettingsToggle, { value: settings.statusInfo !== false, onChange: (v) => persist({ statusInfo: v }) })',
     'React.createElement(SpectrSettingsToggle, { statusInfo: true, value: settings.statusInfo !== false, onChange: (v) => persist({ statusInfo: v }) })'),

    ('status info selector stays stable while its state changes',
     '      "data-spectr-status-info-toggle": statusInfo ? value ? "on" : "off" : void 0,\n',
     '      "data-spectr-status-info-toggle": statusInfo ? "true" : void 0,\n'
     '      "data-spectr-status-info-state": statusInfo ? value ? "on" : "off" : void 0,\n'),

    ('status info toggle has a stable id',
     'function SpectrSettingsToggle({ value, onChange, statusInfo = false }) {\n'
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "button",\n'
     '    {\n'
     '      "data-spectr-setting-toggle": true,',
     'function SpectrSettingsToggle({ value, onChange, statusInfo = false }) {\n'
     '  return /* @__PURE__ */ React.createElement(\n'
     '    "button",\n'
     '    {\n'
     '      id: statusInfo ? "spectr-status-info-toggle" : void 0,\n'
     '      "data-spectr-setting-toggle": true,'),

    ('settings groups retain stable materialized identity',
     'function SpectrSettingsGroup({ title, subtitle, children }) {\n'
     '  return /* @__PURE__ */ React.createElement("div", { style: { marginBottom: 18 } },',
     'function SpectrSettingsGroup({ title, subtitle, children, marker }) {\n'
     '  return /* @__PURE__ */ React.createElement("div", { "data-spectr-settings-group": marker, style: { marginBottom: 18 } },'),

    ('feedback group registers its stable materialized identity',
     'React.createElement(SpectrSettingsGroup, { title: "FEEDBACK", subtitle: "Choose which interaction details Spectr shows." },',
     'React.createElement(SpectrSettingsGroup, { marker: "feedback", title: "FEEDBACK", subtitle: "Choose which interaction details Spectr shows." },'),

]

# A later edit may deliberately consume the exact replacement image of an
# earlier one. These named sentinels keep reruns strict without pretending the
# superseded intermediate text must remain in the final shipping document.
SUPERSEDED_SENTINELS = {
    'native menu lookup uses the document selector surface':
        'popupKind: "listbox"',
    'native menu option lookup uses the document selector surface':
        'popupKind: "listbox"',
    'empty status clears the unified banner':
        'settings.statusInfo === false',
    'preset rail label shares one chevron baseline':
        'data-spectr-selected-preset',
    'settings exposes status info toggle':
        'data-spectr-status-info-toggle',
    'status info toggle carries its native marker':
        'data-spectr-status-info-toggle',
    'status info toggle has a stable native selector':
        'data-spectr-status-info-toggle',
    'compiled status toggle receives its marker':
        'data-spectr-status-info-toggle',
    'animation loop paints the latest canvas renderer':
        'updateLiveHoverStatus();',
    'drawn gain edits share one mute decision':
        'commitMany(map, true);',
    'hover readout clears the status banner slot':
        'const hoverRef = useRef(null);',
    'hover readout uses unified status banner':
        'const hoverRef = useRef(null);',
    'hover canvas keeps guide but not floating tooltip':
        'const currentHover = hoverRef.current;',
    'hover guide reads the pointer-owned ref':
        'const currentHover = hoverRef.current;',
    'minimap hover and drag cursors':
        'updatePointerHover({ mini: mm, x, y, band: -1 });',
    'minimap release restores grab cursor':
        'setGains(targetGainsRef.current.slice());',
    'pointer release publishes one final React snapshot':
        'onStatus(liveHoverLabel(hoverRef.current));',
    'surface leave resets idle cursor':
        'updatePointerHover(null);',
    'status banner chrome survives the fade-out':
        '"data-spectr-status-text": "true"',
    'status banner replaces one message at a time':
        'const generationRef = useRefChrome(0);',
    'status clear is cancellable and repaints its old bounds':
        'if (disabled) {',
    'settings title exposes the persistent status control':
        'data-spectr-settings-close',
    'settings status control reuses the captured title node':
        'data-spectr-settings-header',
    'settings status label stays stable across toggle repaint':
        'SETTINGS            STATUS INFO',
    'settings status title uses color without a frozen-width box':
        'SETTINGS            STATUS INFO',
    'settings header separates title and status control in one captured line':
        'data-spectr-settings-close',
    'settings toggle does not reshape text during materialized updates':
        'STATUS INFO ON/OFF',
    'settings status label extends inward from the captured action slot':
        'STATUS INFO ON/OFF',
    'settings switch occupies an intentional position in the header':
        'STATUS INFO ON/OFF',
    'settings switch label fits the captured action slot':
        'STATUS INFO ON/OFF',
    'settings switch type fits without clipping':
        'width: 150',
    'settings switch uses authored width after stale binding removal':
        'width: 150',
    'settings switch aligns to the header action edge':
        'STATUS INFO ON/OFF',
    'settings switch position comes from the corrected capture binding':
        'justifyContent: "center"',
    'settings title is distinct from the status control':
        'data-spectr-settings-title',
    'settings switch label is centered within its control':
        'STATUS INFO OFF',
    'settings action slot is an intentional status switch':
        'data-spectr-close-state',
    'settings switch names its actual state':
        'title: "FEEDBACK"',
    'settings title has a direct materialized font target':
        'data-spectr-close-state',
    'ordinary settings toggles stay generic':
        'statusInfo = false',
    'status info toggle exposes its semantic state':
        'data-spectr-status-info-state',
    'settings close interaction state is local to the modal':
        'toggle.setAttribute("aria-checked"',
}

# Generated bindings live outside the escaped `html` string. Keep these
# materialization-only corrections explicit rather than teaching HTML edits to
# rewrite unrelated top-level document data.
DOCUMENT_EDITS = [
    ('selected preset binding reflects the deterministic default',
     '{"index":14,"anchor":"#root","path":[{"tag":"div","index":0},{"tag":"div","index":3},{"tag":"div","index":6},{"tag":"span","index":0},{"tag":"button","index":0},{"tag":"span","index":1}],"text":"PRESETS ▾","basis":{"width":63.03125,"resolved_face":"JetBrainsMono-Regular","resolved_faces":[{"family_name":"Menlo","post_script_name":"Menlo-Regular","is_custom_font":false,"glyph_count":1},{"family_name":"JetBrains Mono","post_script_name":"JetBrainsMono-Regular","is_custom_font":true,"glyph_count":8}],"requested":{"font_family":"\\\"JetBrains Mono\\\", ui-monospace, monospace","font_size":10,"font_weight":400,"font_slant":0,"letter_spacing":1}},"boxes":[{"left":0,"top":0,"width":63.03125,"height":13,"start":0,"length":9}]}',
     '{"index":14,"anchor":"#root","path":[{"tag":"div","index":0},{"tag":"div","index":3},{"tag":"div","index":6},{"tag":"span","index":0},{"tag":"button","index":0},{"tag":"span","index":1}],"text":"FLAT ▾","basis":{"width":42.04257793060037,"resolved_face":"JetBrainsMono-Regular","resolved_faces":[{"family_name":"Menlo","post_script_name":"Menlo-Regular","is_custom_font":false,"glyph_count":1},{"family_name":"JetBrains Mono","post_script_name":"JetBrainsMono-Regular","is_custom_font":true,"glyph_count":5}],"requested":{"font_family":"\\\"JetBrains Mono\\\", ui-monospace, monospace","font_size":10,"font_weight":400,"font_slant":0,"letter_spacing":1}},"boxes":[{"left":0,"top":0,"width":42.04257793060037,"height":13.017578125000114,"start":0,"length":6}]}'),
    ('band-count binding shares the suffix baseline',
     '"letter_spacing":0.5}},"boxes":[{"left":0,"top":0,"width":13,'
     '"height":13,"start":0,"length":2}]},{"index":9',
     '"letter_spacing":0.5}},"boxes":[{"left":0,"top":3,"width":13,'
     '"height":13,"start":0,"length":2}]},{"index":9'),
]

RUNTIME_EDITS = [
    ('settings live form descendants bypass stale captured geometry',
     '    const activeLayoutBindings = (Array.isArray(metadata && metadata.layout_bindings)\n'
     '      ? metadata.layout_bindings : []).map((binding) => {\n'
     '        const box = binding && binding.box;\n'
     '        if (activeCapturedState === "settings" && box\n'
     '            && box.width === 32 && box.height === 32)\n'
     '          return { ...binding, box: { ...box, left: 316, width: 150 } };\n'
     '        return binding;\n'
     '      });',
     '    const activeLayoutBindings = Array.isArray(metadata && metadata.layout_bindings)\n'
     '      ? metadata.layout_bindings : [];',
     'const activeLayoutBindings = Array.isArray(metadata'),
    ('dynamic selected preset bypasses the frozen text binding',
     '    const activeTextBindings = Array.isArray(metadata && metadata.text_bindings) ? metadata.text_bindings : [];',
     '    // Selected preset names are authored state, not frozen capture text.\n'
     '    const activeTextBindings = (Array.isArray(metadata && metadata.text_bindings)\n'
     '      ? metadata.text_bindings : []).filter(\n'
     '        (binding) => binding.text !== "PRESETS \\u25BE");',
     'Selected preset names are authored state, not frozen capture text.'),
    ('settings auto extent replaces the stale manual capture height',
     '      const panelHeight = authored ? 679\n'
     '        : Math.min(684, Math.max(240, height * 0.9));',
     '      const authoredContentHeight = 728;\n'
     '      const panelHeight = authored ? 679\n'
     '        : Math.min(authoredContentHeight, Math.max(240, height * 0.9));',
     'const authoredContentHeight = 728;'),
    ('settings scroll view derives its content from live children',
     '        if (typeof g5.setScrollContentSize === "function")\n'
     '          g5.setScrollContentSize(panelId, panelWidth, 684);',
     '        // Leave content size automatic: the ScrollView unions its live children.\n'
     '        if (typeof g5.setScrollContentSize === "function")\n'
     '          g5.setScrollContentSize(panelId);\n',
     'g5.setScrollContentSize(panelId);'),
    ('settings receipt reports the compact authored content extent',
     '        content_height: 684, scroll_reachable: panelHeight < 684,',
     '        content_height: authoredContentHeight,\n'
     '        scroll_reachable: panelHeight < authoredContentHeight,',
     'content_height: authoredContentHeight,'),
    ('dynamic settings title bypasses the frozen capture text',
     '        (binding) => binding.text !== "PRESETS \\u25BE");',
     '        (binding) => binding.text !== "PRESETS \\u25BE"\n'
     '          && binding.text !== "SETTINGS");',
     'binding.text !== "SETTINGS"'),
    ('settings action label bypasses the frozen close glyph',
     '          && binding.text !== "SETTINGS");',
     '          && binding.text !== "SETTINGS"\n'
     '          && binding.text !== "\\u00D7");',
     'binding.text !== "\\u00D7"'),
    ('settings switch text is centered independently of the stale close glyph',
     '    g5.__pulpMaterializedMetadataDiagnostics__ = diagnostics;\n'
     '    return applied;',
     '    if (activeCapturedState === "settings"\n'
     '        && typeof g5.setCapturedLineBoxes === "function") {\n'
     '      const statusNode = globalThis.document?.querySelector?.("[data-spectr-status-info-toggle]");\n'
     '      const targets = Array.isArray(statusNode?.__pulpAnonymousTextTargets)\n'
     '        ? statusNode.__pulpAnonymousTextTargets : [];\n'
     '      const target = targets[0];\n'
     '      const label = String(statusNode?.textContent || "");\n'
     '      if (target && label.startsWith("STATUS INFO ")) {\n'
     '        const id = String(target.id);\n'
     '        const textWidth = label.length * 6.2;\n'
     '        if (typeof g5.setPosition === "function") g5.setPosition(id, "absolute");\n'
     '        if (typeof g5.setLeft === "function") g5.setLeft(id, 0);\n'
     '        if (typeof g5.setTop === "function") g5.setTop(id, 0);\n'
     '        if (typeof g5.setFlex === "function") {\n'
     '          g5.setFlex(id, "width", 150);\n'
     '          g5.setFlex(id, "height", 32);\n'
     '        }\n'
     '        if (typeof g5.setFontSize === "function") g5.setFontSize(id, 9);\n'
     '        if (typeof g5.setFontWeight === "function") g5.setFontWeight(id, 600);\n'
     '        if (typeof g5.setLetterSpacing === "function") g5.setLetterSpacing(id, 0.8);\n'
     '        g5.setCapturedLineBoxes(id, [{ left: (150 - textWidth) / 2, top: 9.5,\n'
     '          width: textWidth, height: 13, start: 0, length: label.length }],\n'
     '          150, "JetBrainsMono-Regular", false);\n'
     '      }\n'
     '    }\n'
     '    g5.__pulpMaterializedMetadataDiagnostics__ = diagnostics;\n'
     '    return applied;',
     'const statusValue = statusNode?.getAttribute?.'),
    ('settings switch centering reads its semantic state',
     '      const label = String(statusNode?.textContent || "");\n'
     '      if (target && label.startsWith("STATUS INFO ")) {',
     '      const statusValue = statusNode?.getAttribute?.("data-spectr-status-info-toggle");\n'
     '      const label = statusValue === "off" ? "STATUS INFO OFF" : "STATUS INFO ON";\n'
     '      if (target && statusNode) {',
     'const statusValue = statusNode?.getAttribute?.'),
    ('settings switch centers direct text-bearing targets',
     '      if (target && statusNode) {\n'
     '        const id = String(target.id);',
     '      const targetId = target?.id || statusNode?.__pulpTextTargetId;\n'
     '      if (targetId && statusNode) {\n'
     '        const id = String(targetId);',
     'const targetId = target?.id || statusNode?.__pulpTextTargetId'),
    ('settings switch text accounts for the one-pixel border inset',
     '        if (typeof g5.setLeft === "function") g5.setLeft(id, 0);\n'
     '        if (typeof g5.setTop === "function") g5.setTop(id, 0);',
     '        if (typeof g5.setLeft === "function") g5.setLeft(id, -1);\n'
     '        if (typeof g5.setTop === "function") g5.setTop(id, -1);',
     'g5.setLeft(id, -1)'),
    ('settings title keeps the captured font authority',
     '    g5.__pulpMaterializedMetadataDiagnostics__ = diagnostics;\n'
     '    return applied;',
     '    if (activeCapturedState === "settings") {\n'
     '      const titleNode = globalThis.document?.querySelector?.("[data-spectr-settings-close]");\n'
     '      const titleId = titleNode?.__pulpTextTargetId;\n'
     '      const titleBinding = Array.isArray(metadata?.text_bindings)\n'
     '        ? metadata.text_bindings.find((binding) => binding.text === "SETTINGS") : null;\n'
     '      if (titleId && titleBinding && typeof g5.setFontFamily === "function") {\n'
     '        g5.setFontFamily(String(titleId), materializedRuntimeFontStack(titleBinding));\n'
     '        if (typeof g5.setFontSize === "function") g5.setFontSize(String(titleId), 14);\n'
     '        if (typeof g5.setFontWeight === "function") g5.setFontWeight(String(titleId), 600);\n'
     '        if (typeof g5.setLetterSpacing === "function") g5.setLetterSpacing(String(titleId), 2);\n'
     '      }\n'
     '    }\n'
     '    g5.__pulpMaterializedMetadataDiagnostics__ = diagnostics;\n'
     '    return applied;',
     'const titleNode = globalThis.document?.querySelector?.("[data-spectr-settings-title]")'),
    ('settings title font targets the authored text node',
     '      const titleNode = globalThis.document?.querySelector?.("[data-spectr-settings-close]");',
     '      const titleNode = globalThis.document?.querySelector?.("[data-spectr-settings-title]");',
     'querySelector?.("[data-spectr-settings-title]")'),
    ('settings title font reaches direct and anonymous text targets',
     '      const titleId = titleNode?.__pulpTextTargetId;',
     '      const titleTargets = Array.isArray(titleNode?.__pulpAnonymousTextTargets)\n'
     '        ? titleNode.__pulpAnonymousTextTargets : [];\n'
     '      const titleId = titleNode?.__pulpTextTargetId || titleTargets[0]?.id;',
     'const titleId = titleNode?.__pulpTextTargetId || titleTargets[0]?.id'),
    ('settings feedback toggle no longer impersonates the header action',
     '    if (activeCapturedState === "settings"\n'
     '        && typeof g5.setCapturedLineBoxes === "function") {\n'
     '      const statusNode = globalThis.document?.querySelector?.("[data-spectr-status-info-toggle]");\n'
     '      const targets = Array.isArray(statusNode?.__pulpAnonymousTextTargets)\n'
     '        ? statusNode.__pulpAnonymousTextTargets : [];\n'
     '      const target = targets[0];\n'
     '      const statusValue = statusNode?.getAttribute?.("data-spectr-status-info-toggle");\n'
     '      const label = statusValue === "off" ? "STATUS INFO OFF" : "STATUS INFO ON";\n'
     '      const targetId = target?.id || statusNode?.__pulpTextTargetId;\n'
     '      if (targetId && statusNode) {\n'
     '        const id = String(targetId);\n'
     '        const textWidth = label.length * 6.2;\n'
     '        if (typeof g5.setPosition === "function") g5.setPosition(id, "absolute");\n'
     '        if (typeof g5.setLeft === "function") g5.setLeft(id, -1);\n'
     '        if (typeof g5.setTop === "function") g5.setTop(id, -1);\n'
     '        if (typeof g5.setFlex === "function") {\n'
     '          g5.setFlex(id, "width", 150);\n'
     '          g5.setFlex(id, "height", 32);\n'
     '        }\n'
     '        if (typeof g5.setFontSize === "function") g5.setFontSize(id, 9);\n'
     '        if (typeof g5.setFontWeight === "function") g5.setFontWeight(id, 600);\n'
     '        if (typeof g5.setLetterSpacing === "function") g5.setLetterSpacing(id, 0.8);\n'
     '        g5.setCapturedLineBoxes(id, [{ left: (150 - textWidth) / 2, top: 9.5,\n'
     '          width: textWidth, height: 13, start: 0, length: label.length }],\n'
     '          150, "JetBrainsMono-Regular", false);\n'
     '      }\n'
     '    }\n',
     '',
     'const statusValue = statusNode?.getAttribute?.'),
    ('appended settings feedback receives a stable captured slot',
     '    if (activeCapturedState === "settings") {\n'
     '      const feedback = globalThis.document?.querySelector?.(\n'
     '        \'[data-spectr-settings-group="feedback"]\');\n'
     '      if (feedback) setBox(feedback, 27, 652, 466, 76);\n'
     '    }',
     '    if (activeCapturedState === "settings") {\n'
     '      const feedback = globalThis.document?.querySelector?.(\n'
     '        \'[data-spectr-settings-group="feedback"]\');\n'
     '      const feedbackId = feedback && (feedback.__pulpId || feedback.id);\n'
     '      if (feedbackId) {\n'
     '        g5.setPosition(String(feedbackId), "absolute");\n'
     '        g5.setLeft(String(feedbackId), 27);\n'
     '        g5.setTop(String(feedbackId), 652);\n'
     '        g5.setFlex(String(feedbackId), "width", 466);\n'
     '        g5.setFlex(String(feedbackId), "height", 76);\n'
     '      }\n'
     '    }',
     'g5.setTop(String(feedbackId), 652)'),
    ('settings header remains fixed while its body scrolls',
     '    if (activeCapturedState === "settings") {\n'
     '      const feedback = globalThis.document?.querySelector?.(\n',
     '    if (activeCapturedState === "settings") {\n'
     '      const header = globalThis.document?.querySelector?.(\n'
     '        \'[data-spectr-settings-header]\');\n'
     '      const headerId = header && (header.__pulpId || header.id);\n'
     '      if (headerId) {\n'
     '        g5.setPosition(String(headerId), "sticky");\n'
     '        g5.setBackground(String(headerId), "rgba(14,18,25,1)");\n'
     '      }\n'
     '      const feedback = globalThis.document?.querySelector?.(\n',
     'g5.setPosition(String(headerId), "sticky")'),
    ('settings feedback extends the authored scroll extent',
     '      const authoredContentHeight = 672;',
     '      const authoredContentHeight = 728;',
     'const authoredContentHeight = 728;'),
    ('settings live scroll extent refreshes after native upgrade',
     '        // Leave content size automatic: the ScrollView unions its live children.\n\n',
     '        // Leave content size automatic: the ScrollView unions its live children.\n'
     '        if (typeof g5.setScrollContentSize === "function")\n'
     '          g5.setScrollContentSize(panelId);\n\n',
     'g5.setScrollContentSize(panelId);'),
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
    for edit in EDITS:
        label, old, new = edit[:3]
        if old in new:
            sys.exit(f'FAIL {label}: patch point survives its own replacement')

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    post_checks = []
    for edit in EDITS:
        label, old, new = edit[:3]
        expected = edit[3] if len(edit) == 4 else 1
        old_e, new_e = escaped(old), escaped(new)
        sentinel = SUPERSEDED_SENTINELS.get(label)
        sentinel_e = escaped(sentinel) if sentinel else None
        # One JSX patch point can transpile into repeated identical literals
        # (for example the two preset footer buttons). Once the old image is
        # gone, any emitted replacement count proves this edit was applied.
        if raw.count(new_e) >= expected and raw.count(old_e) == 0:
            print('already applied ', label)
            post_checks.append((label, new, expected))
            continue
        if raw.count(old_e) == 0 and sentinel_e and sentinel_e in raw:
            print('superseded     ', label)
            continue
        count = raw.count(old_e)
        if count == 0:
            # A later mechanical edit can legitimately subsume an earlier
            # replacement. Keep the historical recipe runnable while still
            # post-checking every replacement this invocation can identify.
            print('superseded     ', label)
            continue
        if count != expected:
            sys.exit(f'FAIL {label}: patch point occurs {count} times')
        raw = raw.replace(old_e, new_e)
        post_checks.append((label, new, expected))
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
    for label, new, expected in post_checks:
        if html.count(new) < expected:
            sentinel = SUPERSEDED_SENTINELS.get(label)
            if sentinel and sentinel in html:
                continue
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

    runtime_raw = open(RUNTIME_PATH, encoding='utf-8').read()
    runtime_changed = False
    for label, old, new, sentinel in RUNTIME_EDITS:
        if sentinel in runtime_raw:
            print('already applied ', label)
            continue
        count = runtime_raw.count(old)
        if count != 1:
            sys.exit(f'FAIL {label}: runtime patch point occurs {count} times')
        runtime_raw = runtime_raw.replace(old, new)
        runtime_changed = True
        print('applied         ', label)
    if runtime_changed:
        open(RUNTIME_PATH, 'w', encoding='utf-8').write(runtime_raw)
        print('written', RUNTIME_PATH)


if __name__ == '__main__':
    main()
