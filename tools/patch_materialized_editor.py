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
import glob
import json
import os
import shutil
import sys

PATH = 'native-ui/materialized/materialized-document.runtime.json'
RUNTIME_PATH = 'native-ui/materialized/runtime.js'
SOURCE_PATH = 'resources/editor.html'
CAPTURE_DOCUMENT_PATHS = [
    'native-ui/materialized/materialized-document.json',
    *sorted(glob.glob('native-ui/materialized/states/*.materialized.json')),
]

EDITS = [
    ('save dialog inherits global Escape dismissal',
     '  usePE(() => {\n'
     '    if (open) setDraft(defaultName);\n'
     '  }, [open, defaultName]);\n'
     '  if (!open) return null;',
     '  usePE(() => {\n'
     '    if (open) setDraft(defaultName);\n'
     '  }, [open, defaultName]);\n'
     '  usePE(() => {\n'
     '    if (!open) return;\n'
     '    const dismiss = (event) => {\n'
     '      if (event.key !== "Escape") return;\n'
     '      event.preventDefault();\n'
     '      event.stopPropagation();\n'
     '      onCancel();\n'
     '    };\n'
     '    document.addEventListener("keydown", dismiss, true);\n'
     '    return () => document.removeEventListener("keydown", dismiss, true);\n'
     '  }, [open, onCancel]);\n'
     '  if (!open) return null;'),

    # The full-screen settings scrim owns the modal. Guard its click handler so
    # bubbled clicks from inert panel content cannot be mistaken for an outside
    # press; a direct scrim press still dismisses in browser and native hosts.
    ('settings backdrop only dismisses a true outside click',
     'React.createElement("div", { "data-spectr-overlay": "true", role: "dialog", "aria-modal": "true", "aria-label": "Settings", onClick: onClose, style: {',
     'React.createElement("div", { "data-spectr-overlay": "true", role: "dialog", "aria-modal": "true", "aria-label": "Settings", onClick: (event) => { if (event.target === event.currentTarget) onClose(); }, style: {'),

    # Migration for the short-lived panel-owned implementation. On a fresh
    # materialization the replacement is already present and this is a no-op.
    ('settings backdrop migration retains overlay identity',
     'React.createElement("div", { role: "dialog", "aria-modal": "true", "aria-label": "Settings", onClick: (event) => { if (event.target === event.currentTarget) onClose(); }, style: {',
     'React.createElement("div", { "data-spectr-overlay": "true", role: "dialog", "aria-modal": "true", "aria-label": "Settings", onClick: (event) => { if (event.target === event.currentTarget) onClose(); }, style: {'),

    ('settings panel owns native overlay containment',
     'React.createElement("div", { "data-spectr-settings-panel": true, onClick: (e) => e.stopPropagation(), style: {',
     'React.createElement("div", { "data-spectr-settings-panel": true, "data-spectr-overlay": "true", overlay: true, onDismiss: onClose, onClick: (e) => e.stopPropagation(), style: {'),

    ('normal chrome does not expose spectral resolution diagnostics',
     '  )))), /* @__PURE__ */ React.createElement("span", null, "\\xB7"), /* @__PURE__ */ React.createElement("span", { className: "tnum" }, info.zoom, "\\xD7 zoom"), /* @__PURE__ */ React.createElement("span", null, "\\xB7"), /* @__PURE__ */ React.createElement("span", { "data-spectr-resolution": true, className: "tnum", title: "Distinct FFT-bin coverage; all bands remain editable", "aria-label": "Spectral resolution", onPointerEnter: () => onStatus && onStatus(`SPECTRAL RESOLUTION: ${resolution ? resolution.represented + "/" + resolution.active : "\\u2014/\\u2014"} DISTINCT \\xB7 ALL BANDS EDITABLE`), onClick: () => onStatus && onStatus(`SPECTRAL RESOLUTION: ${resolution ? resolution.represented + "/" + resolution.active : "\\u2014/\\u2014"} DISTINCT \\xB7 ALL BANDS EDITABLE`), style: {\n'
     '    color: resolution && resolution.represented < resolution.active ? "rgba(255,176,96,0.88)" : "rgba(255,255,255,0.38)"\n'
     '  } }, "RES ", resolution ? resolution.represented + "/" + resolution.active : "\\u2014/\\u2014")))',
     '  )))), /* @__PURE__ */ React.createElement("span", null, "\\xB7"), /* @__PURE__ */ React.createElement("span", { className: "tnum" }, info.zoom, "\\xD7 zoom"), /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\xB7"), /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B")))'),

    # The materialized capture expects the former separator and readout nodes.
    # Keep zero-size inert nodes so removing product chrome does not invalidate
    # the captured topology; neither node exposes text or an interaction hook.
    ('normal chrome preserves inert captured topology',
     '  )))), /* @__PURE__ */ React.createElement("span", null, "\\xB7"), /* @__PURE__ */ React.createElement("span", { className: "tnum" }, info.zoom, "\\xD7 zoom")))',
     '  )))), /* @__PURE__ */ React.createElement("span", null, "\\xB7"), /* @__PURE__ */ React.createElement("span", { className: "tnum" }, info.zoom, "\\xD7 zoom"), /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\xB7"), /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B")))'),

    ('hidden separator retains captured text receipt',
     'React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B"), /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B")))',
     'React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\xB7"), /* @__PURE__ */ React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B")))'),

    ('chrome drops unused resolution props',
     'function Chrome({ settings, setSettings, bankRef, info, status, onStatus, selectedPatternName, dspMode, setDspMode, editMode, setEditMode, analyzerMode, setAnalyzerMode, visualizationMode, setVisualizationMode, snapshotStatus, patterns, onApplyPattern, onOpenPatternManager, onSavePattern, onClearAll, onResetAll, allMuted, resolution }) {',
     'function Chrome({ settings, setSettings, bankRef, info, status, selectedPatternName, dspMode, setDspMode, editMode, setEditMode, analyzerMode, setAnalyzerMode, visualizationMode, setVisualizationMode, snapshotStatus, patterns, onApplyPattern, onOpenPatternManager, onSavePattern, onClearAll, onResetAll, allMuted }) {'),

    ('app drops unused resolution chrome props',
     '      resolution,\n'
     '      settings,\n'
     '      setSettings,\n'
     '      bankRef,\n'
     '      info,\n'
     '      status,\n'
     '      onStatus: fireStatus,\n'
     '      selectedPatternName,',
     '      settings,\n'
     '      setSettings,\n'
     '      bankRef,\n'
     '      info,\n'
     '      status,\n'
     '      selectedPatternName,'),

    ('normal app drops resolution state and polling',
     '  const [resolution, setResolution] = useAppS(null);\n',
     ''),

    ('normal app drops resolution listener and refresh timer',
     '    const unsubscribeResolution = window.pulp.on("spectral_resolution", (message) => {\n'
     '      const payload = message && message.payload;\n'
     '      const represented = payload && Number(payload.represented_bands);\n'
     '      const active = payload && Number(payload.active_bands);\n'
     '      const minHz = payload && Number(payload.min_hz);\n'
     '      const maxHz = payload && Number(payload.max_hz);\n'
     '      if (!Number.isInteger(represented) || !Number.isInteger(active) || active <= 0 || represented < 0 || represented > active || !Number.isFinite(minHz) || !Number.isFinite(maxHz)) {\n'
     '        console.error("[Spectr] rejected malformed resolution payload");\n'
     '        return;\n'
     '      }\n'
     '      const bank = bankRef.current;\n'
     '      if (bank) {\n'
     '        const expectedMin = Math.pow(10, bank.view.lmin);\n'
     '        const expectedMax = Math.pow(10, bank.view.lmax);\n'
     '        const closeEnough = (a, b) => Math.abs(a - b) <= Math.max(1e-3, b * 1e-5);\n'
     '        if (bank.N !== active || !closeEnough(minHz, expectedMin) || !closeEnough(maxHz, expectedMax)) return;\n'
     '      }\n'
     '      setResolution({ represented, active });\n'
     '    });\n'
     '    const resolutionRefresh = setInterval(() => {\n'
     '      try {\n'
     '        Promise.resolve(window.pulp.postMessage(\n'
     '          "spectral_resolution_request",\n'
     '          {},\n'
     '          "spectr-spectral-resolution-refresh"\n'
     '        )).catch(() => {\n'
     '        });\n'
     '      } catch {\n'
     '      }\n'
     '    }, 1e3);\n',
     ''),

    ('normal app cleanup drops resolution subscriptions',
     '      if (typeof unsubscribeHydration === "function") unsubscribeHydration();\n'
     '      if (typeof unsubscribeResolution === "function") unsubscribeResolution();\n'
     '      clearInterval(resolutionRefresh);',
     '      if (typeof unsubscribeHydration === "function") unsubscribeHydration();'),

    ('processing publication does not request UI resolution diagnostics',
     '        const publication = window.pulp.postMessage("processing_state_set", {\n'
     '          n_visible: N,\n'
     '          gain_db: gainDb2,\n'
     '          muted,\n'
     '          min_hz: Math.pow(10, view.lmin),\n'
     '          max_hz: Math.pow(10, view.lmax)\n'
     '        }, "spectr-processing-state");\n'
     '        const geometryKey = N + ":" + view.lmin + ":" + view.lmax;\n'
     '        if (resolutionGeometryRef.current !== geometryKey) {\n'
     '          Promise.resolve(publication).then((response) => {\n'
     '            if (!response || response.ok !== true || !response.payload || response.payload.ok !== true) {\n'
     '              throw new Error("processing state rejected");\n'
     '            }\n'
     '            resolutionGeometryRef.current = geometryKey;\n'
     '            window.pulp.postMessage(\n'
     '              "spectral_resolution_request",\n'
     '              {},\n'
     '              "spectr-spectral-resolution-request"\n'
     '            );\n'
     '          }).catch((error) => {\n'
     '            console.error("[Spectr] native resolution request failed", error);\n'
     '          });\n'
     '        }',
     '        window.pulp.postMessage("processing_state_set", {\n'
     '          n_visible: N,\n'
     '          gain_db: gainDb2,\n'
     '          muted,\n'
     '          min_hz: Math.pow(10, view.lmin),\n'
     '          max_hz: Math.pow(10, view.lmax)\n'
     '        }, "spectr-processing-state");'),

    ('filter bank drops unused resolution geometry ref',
     '  const resolutionGeometryRef = useRef("");\n',
     ''),

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
     'display: "block", width: "100%" }', 2),

    # Text changes never affect this node's geometry: the status shell owns the
    # content-sized banner, while this live label fills that resolved box in
    # both axes. This lets Pulp update its text without scheduling layout.
    ('live status text has text-independent geometry',
     'React.createElement("span", { "data-spectr-status-text": "true", style: { display: "block", textAlign: "center", width: "100%" } }, text)',
     'React.createElement("span", { "data-spectr-status-text": "true", style: { display: "flex", alignItems: "center", justifyContent: "center", textAlign: "center", width: "100%", height: "100%" } }, text)'),

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
     '    const holdMs = /\\b(?:MUTED|UNMUTED)\\b/.test(display) ? 2000 : 1400;\n'
     '    timers.push(setTimeout(() => {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '    }, holdMs + (replacing ? 150 : 0)));\n'
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

    ('hover status expires after inactivity',
     '    const keepAlive = setInterval(() => onStatus(label), 1e3);\n'
     '    return () => clearInterval(keepAlive);\n',
     ''),

    ('mute status gets a longer independent hold',
     '    const timer = hide(1400);\n'
     '    return () => clearTimeout(timer);',
     '    const holdMs = /\\b(?:MUTED|UNMUTED)\\b/.test(display) ? 2000 : 1400;\n'
     '    const timer = hide(holdMs);\n'
     '    return () => clearTimeout(timer);'),

    ('status info hint fits the settings field',
     'hint: "Hover, mute, and drag details in the top banner"',
     'hint: "Hover, mute, and drag"'),

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

    ('band trigger matches settings chip metrics',
     '    padding: "2px 7px",\n'
     '    borderRadius: 3,\n'
     '    fontFamily: "var(--mono)",\n'
     '    fontSize: 10,\n'
     '    letterSpacing: 0.5,\n'
     '    cursor: "pointer",\n'
     '    display: "inline-flex",\n'
     '    alignItems: "center",\n'
     '    gap: 4,\n'
     '    lineHeight: 1',
     '    padding: "5px 10px",\n'
     '    width: 92,\n'
     '    flexShrink: 0,\n'
     '    minHeight: 26,\n'
     '    boxSizing: "border-box",\n'
     '    borderRadius: 3,\n'
     '    fontFamily: "var(--mono)",\n'
     '    fontSize: 10,\n'
     '    letterSpacing: 0.8,\n'
     '    cursor: "pointer",\n'
     '    display: "inline-flex",\n'
     '    alignItems: "center",\n'
     '    justifyContent: "center",\n'
     '    gap: 4,\n'
     '    lineHeight: 1'),

    ('band trigger suffix shares the centered flex line',
     'React.createElement("span", { className: "tnum", style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, settings.bandCount), " bands \\u25BE")',
     'React.createElement("span", { className: "tnum", style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, settings.bandCount), /* @__PURE__ */ React.createElement("span", { style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, "bands \\u25BE"))'),

    ('band trigger suffix uses native-supported spacing',
     'React.createElement("span", { style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, "bands \\u25BE"))',
     'React.createElement("span", { style: { display: "inline-flex", alignItems: "center", lineHeight: 1, marginLeft: 4 } }, "bands \\u25BE"))'),

    ('band trigger suffix spacing survives materialization',
     'React.createElement("span", { style: { display: "inline-flex", alignItems: "center", lineHeight: 1, marginLeft: 4 } }, "bands \\u25BE"))',
     'React.createElement("span", { style: { display: "inline-flex", alignItems: "center", lineHeight: 1, paddingLeft: 4 } }, "bands \\u25BE"))'),

    ('band trigger uses one deterministic native label',
     'React.createElement("span", { className: "tnum", style: { display: "inline-flex", alignItems: "center", lineHeight: 1 } }, settings.bandCount), /* @__PURE__ */ React.createElement("span", { style: { display: "inline-flex", alignItems: "center", lineHeight: 1, paddingLeft: 4 } }, "bands \\u25BE"))',
     'React.createElement("span", { className: "tnum", style: { lineHeight: 1, whiteSpace: "nowrap" } }, settings.bandCount + " bands \\u25BE"))'),

    ('band trigger reserves deterministic native width',
     '    padding: "5px 10px",\n'
     '    minHeight: 26,',
     '    padding: "5px 10px",\n'
     '    width: 92,\n'
     '    flexShrink: 0,\n'
     '    minHeight: 26,'),

    ('band trigger shares the segmented tab painted height',
     '    padding: "5px 10px",\n'
     '    width: 92,\n'
     '    flexShrink: 0,\n'
     '    minHeight: 26,\n'
     '    boxSizing: "border-box",',
     '    padding: "5px 10px",\n'
     '    width: 92,\n'
     '    flexShrink: 0,\n'
     '    minHeight: 22,\n'
     '    boxSizing: "border-box",'),

    ('band root reserves the metadata separator gap',
     '"data-spectr-menu-root": "bands", style: { position: "relative" }',
     '"data-spectr-menu-root": "bands", style: { position: "relative", marginRight: 4 }'),

    ('band dropdown matches settings chip metrics',
     '        background: info.N === n ? "rgba(120,180,255,0.18)" : "rgba(255,255,255,0.025)",\n'
     '        border: "1px solid " + (info.N === n ? "rgba(180,210,255,0.4)" : "rgba(255,255,255,0.06)"),\n'
     '        color: "#fff",\n'
     '        padding: "4px 8px",\n'
     '        borderRadius: 2,\n'
     '        fontFamily: "var(--mono)",\n'
     '        fontSize: 10,\n'
     '        cursor: "pointer",\n'
     '        minWidth: 28',
     '        background: info.N === n ? "rgba(120,180,255,0.18)" : "rgba(255,255,255,0.03)",\n'
     '        border: "1px solid " + (info.N === n ? "rgba(180,210,255,0.4)" : "rgba(255,255,255,0.1)"),\n'
     '        color: info.N === n ? "#fff" : "rgba(255,255,255,0.7)",\n'
     '        padding: "5px 10px",\n'
     '        borderRadius: 3,\n'
     '        fontFamily: "var(--mono)",\n'
     '        fontSize: 10,\n'
     '        letterSpacing: 0.8,\n'
     '        cursor: "pointer",\n'
     '        width: 44,\n'
     '        minWidth: 44,\n'
     '        flexShrink: 0,\n'
     '        minHeight: 26,\n'
     '        boxSizing: "border-box",\n'
     '        display: "inline-flex",\n'
     '        alignItems: "center",\n'
     '        justifyContent: "center",\n'
     '        lineHeight: 1'),

    ('band dropdown options reserve deterministic native widths',
     '        minWidth: 40,\n'
     '        minHeight: 26,',
     '        width: 44,\n'
     '        minWidth: 44,\n'
     '        flexShrink: 0,\n'
     '        minHeight: 26,'),

    ('selected preset detail owns one stable live-layout subtree',
     '  return /* @__PURE__ */ React.createElement(React.Fragment, null, /* @__PURE__ */ React.createElement("div", { style: { display: "flex", alignItems: "center", gap: 8 } }, editName && !isFactory ?',
     '  return /* @__PURE__ */ React.createElement("div", { "data-spectr-manager-detail": true, style: { flex: 1, minWidth: 0, minHeight: 0, display: "flex", flexDirection: "column", gap: 12 } }, /* @__PURE__ */ React.createElement("div", { "data-spectr-manager-heading": true, style: { display: "flex", alignItems: "center", gap: 8, minHeight: 26 } }, editName && !isFactory ?'),

    ('selected preset preview has a stable semantic subject',
     '), !isFactory && !editName && /* @__PURE__ */ React.createElement("button", { "data-spectr-manager-action": "rename-start", onClick: () => setEditName(true), style: iconBtn }, "\\u270E")), /* @__PURE__ */ React.createElement("div", { style: {\n'
     '    background: "rgba(0,0,0,0.35)",',
     '), !isFactory && !editName && /* @__PURE__ */ React.createElement("button", { "data-spectr-manager-action": "rename-start", onClick: () => setEditName(true), style: iconBtn }, "\\u270E")), /* @__PURE__ */ React.createElement("div", { "data-spectr-manager-preview": true, style: {\n'
     '    background: "rgba(0,0,0,0.35)",'),

    ('selected preset metadata has a stable semantic subject',
     '} }, /* @__PURE__ */ React.createElement(MiniPreview, { gains, w: 380, h: 86 })), /* @__PURE__ */ React.createElement("div", { style: { fontSize: 9.5, opacity: 0.55, display: "flex", gap: 14, flexWrap: "wrap" } },',
     '} }, /* @__PURE__ */ React.createElement(MiniPreview, { gains, w: 380, h: 86 })), /* @__PURE__ */ React.createElement("div", { "data-spectr-manager-meta": true, style: { fontSize: 9.5, opacity: 0.55, display: "flex", gap: 14, flexWrap: "wrap" } },'),

    ('selected preset actions have one stable live-layout row',
     '), /* @__PURE__ */ React.createElement("div", { style: { flex: 1 } }), /* @__PURE__ */ React.createElement("div", { style: { display: "flex", gap: 6, flexWrap: "wrap" } },',
     '), /* @__PURE__ */ React.createElement("div", { style: { flex: 1, minHeight: 12 } }), /* @__PURE__ */ React.createElement("div", { "data-spectr-manager-actions": true, style: { display: "flex", gap: 6, flexWrap: "wrap", alignItems: "center", minHeight: 58 } },'),

    ('selected preset actions are individually addressable',
     'React.createElement(MBtn, { onClick: onSetDefault }, isDefault ? "\\u2605 DEFAULT" : "SET AS DEFAULT"), /* @__PURE__ */ React.createElement(MBtn, { onClick: onDuplicate }, "DUPLICATE"),',
     'React.createElement(MBtn, { action: "set-default", onClick: onSetDefault }, isDefault ? "\\u2605 DEFAULT" : "SET AS DEFAULT"), /* @__PURE__ */ React.createElement(MBtn, { action: "duplicate", onClick: onDuplicate }, "DUPLICATE"),'),

    ('selected preset export actions are individually addressable',
     'React.createElement(MBtn, { onClick: () => onExport("file") }, "EXPORT (FILE)"), /* @__PURE__ */ React.createElement(MBtn, { onClick: () => onExport("clipboard") }, "EXPORT (CLIP)")))',
     'React.createElement(MBtn, { action: "export-file", onClick: () => onExport("file") }, "EXPORT (FILE)"), /* @__PURE__ */ React.createElement(MBtn, { action: "export-clip", onClick: () => onExport("clipboard") }, "EXPORT (CLIP)")))'),

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
     '        top: 84,'),

    ('status banner clears the ruler line',
     '        top: 76,',
     '        top: 84,'),

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
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right" ? "col-resize" : "grabbing";\n'
     '      const fullMin = Math.log10(20), fullMax = Math.log10(2e4);'),

    ('minimap hover and drag cursors',
     '    if (mm) {\n'
     '      setHover({ mini: mm, x, y, band: -1 });\n'
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right" ? "col-resize" : mm === "window" ? "grab" : "pointer";',
     '    if (mm) {\n'
     '      setHover({ mini: mm, x, y, band: -1 });\n'
     '      const activeMini = pointerRef.current\n'
     '        && (pointerRef.current.mode === "minimap-drag"\n'
     '          || pointerRef.current.mode === "minimap-resize");\n'
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right"\n'
     '        ? "col-resize"\n'
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
     '    if (p && p.mode === "minimap-resize") wrapRef.current.style.cursor = "col-resize";\n'
     '    if (!p || !p.mode) {'),

    ('response ticks omit first and last edges',
     '      for (let i = 0; i <= N; i++) {\n'
     '        const x = inner.x + i * (bandW + bandGap) + 0.5;',
     '      for (let i = 1; i < N; i++) {\n'
     '        const x = inner.x + i * (bandW + bandGap) + 0.5;'),

    ('minimap edges retain horizontal resize cursor',
     '      wrapRef.current.style.cursor = activeMini ? "grabbing" : "grab";',
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right"\n'
     '        ? "col-resize"\n'
     '        : activeMini ? "grabbing" : mm === "window" ? "grab" : "pointer";'),

    ('minimap release retains physical cursor',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize"))\n'
     '      wrapRef.current.style.cursor = "grab";',
     '    if (p && p.mode === "minimap-drag") wrapRef.current.style.cursor = "grab";\n'
     '    if (p && p.mode === "minimap-resize") wrapRef.current.style.cursor = "col-resize";'),

    ('minimap deferred release retains physical cursor',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize")) {\n'
     '      setView({ ...viewRef.current });\n'
     '      wrapRef.current.style.cursor = "grab";\n'
     '    }',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize")) {\n'
     '      setView({ ...viewRef.current });\n'
     '      wrapRef.current.style.cursor = p.mode === "minimap-resize" ? "col-resize" : "grab";\n'
     '    }'),

    # The macOS cursor catalog names this shape `resizeleftright`, whose CSS
    # semantic is `col-resize`. Pulp maps it to the native horizontal-resize
    # cursor; keep both the hover and post-release paths on that exact keyword.
    ('minimap trims use the mac resizeleftright cursor',
     '"ew-resize"',
     '"col-resize"',
     1),

    ('minimap trim press retains resizeleftright cursor',
     '      wrapRef.current.style.cursor = "grabbing";\n'
     '      const fullMin = Math.log10(20), fullMax = Math.log10(2e4);',
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right" ? "col-resize" : "grabbing";\n'
     '      const fullMin = Math.log10(20), fullMax = Math.log10(2e4);'),

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

    ('selected preset trigger truncates without losing its full title',
     'React.createElement("span", { "data-spectr-selected-preset": true, style: { marginLeft: 6, display: "inline-flex", alignItems: "center", lineHeight: 1 } }, selectedPatternName, " \\u25BE")',
     'React.createElement("span", { "data-spectr-selected-preset": true, title: selectedPatternName, style: { marginLeft: 6, display: "inline-flex", alignItems: "center", lineHeight: 1, width: 63, overflow: "hidden", whiteSpace: "nowrap" } }, selectedPatternName.length > 6 ? selectedPatternName.slice(0, 6) + "\u2026 \\u25BE" : selectedPatternName + " \\u25BE")'),

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

    ('semantic popup surfaces claim native dismissal ownership',
     '"data-spectr-menu-options": true, "data-spectr-overlay": "true", role:',
     '"data-spectr-menu-options": true, "data-spectr-overlay": "true", overlay: true, onDismiss: () => setOpenMenu(null), role:',
     5),

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
     '  const [ctxMenu, setCtxMenu] = useState(null);\n'
     '  const edgeGlowRef = useRef({ left: 0, right: 0, top: 0, bottom: 0 });',
     '  const [hover, setHover] = useState(null);\n'
     '  const [ctxMenu, setCtxMenu] = useState(null);\n'
     '  const hoverRef = useRef(null);\n'
     '  const updatePointerHover = (next) => {\n'
     '    hoverRef.current = next;\n'
     '    if (!pointerRef.current || !pointerRef.current.mode) setHover(next);\n'
     '  };\n'
     '  const edgeGlowRef = useRef({ left: 0, right: 0, top: 0, bottom: 0 });\n'
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
     '  const wheelCommitRef = useRef(0);\n'
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

    ('wheel zoom reads and writes the live viewport lane',
     '    const anchor = view.lmin + (x - g.inner.x) / g.inner.w * (view.lmax - view.lmin);\n'
     '    let span = (view.lmax - view.lmin) * factor;\n'
     '    span = clamp(span, 0.1, Math.log10(2e4) - Math.log10(20));\n'
     '    const t = (anchor - view.lmin) / (view.lmax - view.lmin);',
     '    const liveView = viewRef.current;\n'
     '    const anchor = liveView.lmin + (x - g.inner.x) / g.inner.w * (liveView.lmax - liveView.lmin);\n'
     '    let span = (liveView.lmax - liveView.lmin) * factor;\n'
     '    span = clamp(span, 0.1, Math.log10(2e4) - Math.log10(20));\n'
     '    const t = (anchor - liveView.lmin) / (liveView.lmax - liveView.lmin);'),

    ('horizontal trackpad motion pans the minimap rigidly',
     '    const delta = e.deltaY;\n'
     '    const factor = Math.exp(delta * 12e-4);\n'
     '    const liveView = viewRef.current;',
     '    const liveView = viewRef.current;\n'
     '    if (Math.abs(e.deltaX) > Math.abs(e.deltaY)) {\n'
     '      const panFullMin = Math.log10(20), panFullMax = Math.log10(2e4);\n'
     '      const panFullSpan = panFullMax - panFullMin;\n'
     '      const span = liveView.lmax - liveView.lmin;\n'
     '      const shift = e.deltaX / g.inner.w * panFullSpan;\n'
     '      const lmin = clamp(liveView.lmin + shift, panFullMin, panFullMax - span);\n'
     '      commitLiveViewport({ lmin, lmax: lmin + span });\n'
     '      clearTimeout(wheelCommitRef.current);\n'
     '      wheelCommitRef.current = setTimeout(() => setReactView({ ...viewRef.current }), 80);\n'
     '      return;\n'
     '    }\n'
     '    const delta = e.deltaY;\n'
     '    const factor = Math.exp(delta * 12e-4);'),

    ('wheel zoom owns one deferred semantic commit',
     '  const viewRef = useRef({ ...initialView });\n'
     '  const view = viewRef.current;',
     '  const viewRef = useRef({ ...initialView });\n'
     '  const wheelCommitRef = useRef(0);\n'
     '  const view = viewRef.current;'),

    ('wheel zoom defers one semantic viewport snapshot',
     '    setView({ lmin, lmax });\n'
     '  };\n'
     '  useEffect(() => {\n'
     '    if (!nativeHydrated) return;',
     '    commitLiveViewport({ lmin, lmax });\n'
     '    clearTimeout(wheelCommitRef.current);\n'
     '    wheelCommitRef.current = setTimeout(() => setReactView({ ...viewRef.current }), 80);\n'
     '  };\n'
     '  useEffect(() => () => clearTimeout(wheelCommitRef.current), []);\n'
     '  useEffect(() => {\n'
     '    if (!nativeHydrated) return;'),

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

    ('mute brush publishes status on pointer down',
     '      brush.paint(band);\n'
     '      pointerRef.current = brush;\n'
     '      return;',
     '      brush.paint(band);\n'
     '      pointerRef.current = brush;\n'
     '      hoverRef.current = { band, x, y };\n'
     '      if (onStatus) onStatus(liveHoverLabel(hoverRef.current));\n'
     '      return;'),

    ('gain drag publishes status on pointer down',
     '      didDrag: false\n'
     '    };\n'
     '  };',
     '      didDrag: false\n'
     '    };\n'
     '    hoverRef.current = { band, x, y };\n'
     '    if (onStatus) onStatus(liveHoverLabel(hoverRef.current));\n'
     '  };'),

    ('live status uses the materialized text surface only',
     '    const shell = document.querySelector("[data-spectr-status-shell]");\n'
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!shell || !text) return;\n'
     '    text.textContent = label;\n'
     '    shell.style.width = Math.max(96, Math.min(520, label.length * 8 + 28)) + "px";',
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!text) return;\n'
     '    text.textContent = label;'),

    ('live status renews its inactivity deadline off the paint hot path',
     '  const hoverRef = useRef(null);\n'
     '  const hoverBand = hover && !hover.mini ? hover.band : -1;',
     '  const hoverRef = useRef(null);\n'
     '  const statusRefreshAtRef = useRef(0);\n'
     '  const hoverBand = hover && !hover.mini ? hover.band : -1;'),

    ('live status deadline refresh is throttled',
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!text) return;\n'
     '    text.textContent = label;\n'
     '  };',
     '    const text = document.querySelector("[data-spectr-status-text]");\n'
     '    if (!text) return;\n'
     '    text.textContent = label;\n'
     '    const now = performance.now();\n'
     '    if (onStatus && now - statusRefreshAtRef.current >= 700) {\n'
     '      statusRefreshAtRef.current = now;\n'
     '      onStatus(label);\n'
     '    }\n'
     '  };'),

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
     '  ))), /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { marker: "feedback", title: "FEEDBACK", subtitle: "Choose which interaction details Spectr shows." }, /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Status info", hint: "Hover, mute, and drag" }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { statusInfo: true, value: settings.statusInfo !== false, onChange: (v) => persist({ statusInfo: v }) })))));\n'
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
    'preset trigger shows selected name':
        'selectedPatternName.length > 6',
    'native menu lookup uses the document selector surface':
        'popupKind: "listbox"',
    'native menu option lookup uses the document selector surface':
        'popupKind: "listbox"',
    'empty status clears the unified banner':
        'settings.statusInfo === false',
    'preset rail label shares one chevron baseline':
        'data-spectr-selected-preset',
    'band count label shares one vertical center':
        'minWidth: 40',
    'band count trigger reflects selection immediately':
        'minWidth: 40',
    'band trigger matches settings chip metrics':
        'minHeight: 22',
    'band trigger reserves deterministic native width':
        'minHeight: 22',
    'band root reserves the metadata separator gap':
        '"data-spectr-menu-root": "bands", style: { position: "relative", marginRight: 4 }',
    'band trigger suffix shares the centered flex line':
        'paddingLeft: 4',
    'band trigger suffix uses native-supported spacing':
        'paddingLeft: 4',
    'band trigger suffix spacing survives materialization':
        'settings.bandCount + " bands \\u25BE"',
    'chrome receives selected preset name':
        'onStatus, selectedPatternName',
    'selected preset name reaches chrome':
        'onStatus: fireStatus',
    'band dropdown inactive items retain a surface':
        'minWidth: 40',
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
    'live status renews its inactivity deadline off the paint hot path':
        'const statusRefreshAtRef = useRef(0);',
    'live status deadline refresh is throttled':
        'now - statusRefreshAtRef.current >= 700',
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
    'wheel zoom reads and writes the live viewport lane':
        'const anchor = liveView.lmin +',
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
    ('band-count suffix binding follows its live child span',
     '{"tag":"button","index":0}],"anonymous_text_index":0,"text":" bands ▾"',
     '{"tag":"button","index":0},{"tag":"span","index":1}],"text":"bands ▾"'),
    ('selected preset binding reflects the deterministic default',
     '],"text":"PRESETS ▾","basis":{"width":63.05546845843935,',
     '],"text":"FLAT ▾","basis":{"width":42.04257793060037,'),
]

RUNTIME_EDITS = [
    ('imported HTML buttons inherit Pulp semantic hover',
     '            call2("setPointerEvents", textId, "none");\n'
     '            return;\n'
     '          }\n'
     '          case "input": {',
     '            call2("setPointerEvents", textId, "none");\n'
     '            call2("setAccessibilityRole", id, "button");\n'
     '            if (text) call2("setAccessibilityLabel", id, text);\n'
     '            return;\n'
     '          }\n'
     '          case "input": {',
     'call2("setAccessibilityRole", id, "button")'),
    ('semantic popup roles consume their outside dismissal press',
     '        if (r === "dialog" || r === "alertdialog" || r === "menu" || r === "listbox") {\n'
     '          call("claimOverlay", id);\n'
     '          return true;\n'
     '        }',
     '        if (r === "dialog" || r === "alertdialog" || r === "menu" || r === "listbox") {\n'
     '          call("claimOverlay", id, r === "menu" || r === "listbox");\n'
     '          return true;\n'
     '        }',
     'call("claimOverlay", id, r === "menu" || r === "listbox")'),
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
     'const authoredLayoutState = activeCapturedState === "bands"'),
    ('band popup uses the authored option geometry',
     '    const activeLayoutBindings = Array.isArray(metadata && metadata.layout_bindings)\n'
     '      ? metadata.layout_bindings : [];',
     '    const activeLayoutBindings = (Array.isArray(metadata && metadata.layout_bindings)\n'
     '      ? metadata.layout_bindings : []).map((binding) => {\n'
     '        if (activeCapturedState !== "bands" || !binding?.box) return binding;\n'
     '        const box = binding.box;\n'
     '        const path = Array.isArray(binding.path) ? binding.path : [];\n'
     '        const tail = path[path.length - 1] || {};\n'
     '        if (box.width === 166 && box.height === 31)\n'
     '          return { ...binding, box: { ...box, left: -154.96875, width: 236 } };\n'
     '        if (box.width === 30 && box.height === 23 && tail.tag === "button")\n'
     '          return { ...binding, box: { ...box, left: 4 + tail.index * 46, width: 44 } };\n'
     '        if (box.width === 228.53125 && box.height === 19)\n'
     '          return { ...binding, box: { ...box, width: 239.53125 } };\n'
     '        if (box.width === 81.03125 && box.height === 19)\n'
     '          return { ...binding, box: { ...box, width: 92 } };\n'
     '        if (box.top === 3 && box.left >= 87.03125)\n'
     '          return { ...binding, box: { ...box, left: box.left + 10.96875 } };\n'
     '        return binding;\n'
     '      });',
     'const authoredLayoutState = activeCapturedState === "bands"'),
    ('band count text is optically centered in trigger and popup cells',
     '    const belongsTo = (node, root) => {\n'
     '      let current = node;\n'
     '      while (current) {\n'
     '        if (current === root) return true;\n'
     '        current = current.parentElement || current._parentElement || null;\n'
     '      }\n'
     '      return false;\n'
     '    };\n'
     '    const receipt = [];',
     '    const belongsTo = (node, root) => {\n'
     '      let current = node;\n'
     '      while (current) {\n'
     '        if (current === root) return true;\n'
     '        current = current.parentElement || current._parentElement || null;\n'
     '      }\n'
     '      return false;\n'
     '    };\n'
     '    const centerBandText = (owner, width, textWidth, positionOwner) => {\n'
     '      if (!owner || typeof g5.setCapturedLineBoxes !== "function") return null;\n'
     '      const ownerId = owner.__pulpId || owner.id;\n'
     '      const targets = Array.isArray(owner.__pulpAnonymousTextTargets)\n'
     '        ? owner.__pulpAnonymousTextTargets : [];\n'
     '      const targetId = owner.__pulpTextTargetId || targets[0]?.id;\n'
     '      if (!targetId) return null;\n'
     '      if (positionOwner && ownerId) {\n'
     '        g5.setPosition(String(ownerId), "absolute");\n'
     '        g5.setLeft(String(ownerId), 0);\n'
     '        g5.setTop(String(ownerId), 0);\n'
     '        g5.setFlex(String(ownerId), "width", width);\n'
     '        g5.setFlex(String(ownerId), "height", 26);\n'
     '      }\n'
     '      const id = String(targetId);\n'
     '      g5.setPosition(id, "absolute");\n'
     '      g5.setLeft(id, 0);\n'
     '      g5.setTop(id, 0);\n'
     '      g5.setFlex(id, "width", width);\n'
     '      g5.setFlex(id, "height", 26);\n'
     '      if (typeof g5.setFontSize === "function") g5.setFontSize(id, 10);\n'
     '      if (typeof g5.setFontWeight === "function") g5.setFontWeight(id, 400);\n'
     '      if (typeof g5.setLetterSpacing === "function") g5.setLetterSpacing(id, 0.8);\n'
     '      const label = String(owner.textContent || "").trim();\n'
     '      const left = (width - textWidth) / 2;\n'
     '      g5.setCapturedLineBoxes(id, [{ left, top: 6.5, width: textWidth,\n'
     '        height: 13, start: 0, length: label.length }], width,\n'
     '        "JetBrainsMono-Regular", false);\n'
     '      return { label, left, top: 6.5, width, text_width: textWidth };\n'
     '    };\n'
     '    const bandRoot = globalThis.document?.querySelector?.(\n'
     '      \'[data-spectr-menu-root="bands"]\');\n'
     '    const bandTrigger = globalThis.document?.querySelector?.(\n'
     '      \'[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]\');\n'
     '    const bandTriggerLabel = bandTrigger && values.find((candidate) =>\n'
     '      belongsTo(candidate, bandTrigger)\n'
     '        && /^(32|40|48|56|64) bands \\u25BE$/.test(\n'
     '          String(candidate && candidate.textContent || "")));\n'
     '    const bandCountReceipt = {\n'
     '      trigger: centerBandText(bandTriggerLabel, 92, 73.03125, true),\n'
     '      options: []\n'
     '    };\n'
     '    const bandOptions = bandRoot ? Array.from(globalThis.document?.querySelectorAll?.(\n'
     '      \'[data-spectr-menu-root="bands"] [data-spectr-band-count]\') || []) : [];\n'
     '    for (const option of bandOptions) {\n'
     '      const centered = centerBandText(option, 44, 13, false);\n'
     '      if (centered) bandCountReceipt.options.push(centered);\n'
     '    }\n'
     '    g5.__spectrBandCountCenteringReceipt__ = bandCountReceipt;\n'
     '    const receipt = [];',
     '__spectrBandCountCenteringReceipt__'),
    ('band count centering uses the document selector surface',
     '    const bandRoot = g5.__pulpFindMaterializedElement__(\n'
     '      \'[data-spectr-menu-root="bands"]\');\n'
     '    const bandTrigger = g5.__pulpFindMaterializedElement__(\n'
     '      \'[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]\');',
     '    const bandRoot = globalThis.document?.querySelector?.(\n'
     '      \'[data-spectr-menu-root="bands"]\');\n'
     '    const bandTrigger = globalThis.document?.querySelector?.(\n'
     '      \'[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]\');',
     'const bandRoot = globalThis.document?.querySelector?.'),
    ('band count centering targets flattened native text owners',
     '    const centerBandText = (owner, width, textWidth, positionOwner) => {\n'
     '      if (!owner || typeof g5.setCapturedLineBoxes !== "function") return null;\n'
     '      const ownerId = owner.__pulpId || owner.id;\n'
     '      const targets = Array.isArray(owner.__pulpAnonymousTextTargets)\n'
     '        ? owner.__pulpAnonymousTextTargets : [];\n'
     '      const targetId = owner.__pulpTextTargetId || targets[0]?.id;\n'
     '      if (!targetId) return null;\n'
     '      if (positionOwner && ownerId) {\n'
     '        g5.setPosition(String(ownerId), "absolute");\n'
     '        g5.setLeft(String(ownerId), 0);\n'
     '        g5.setTop(String(ownerId), 0);\n'
     '        g5.setFlex(String(ownerId), "width", width);\n'
     '        g5.setFlex(String(ownerId), "height", 26);\n'
     '      }\n'
     '      const id = String(targetId);\n'
     '      g5.setPosition(id, "absolute");\n'
     '      g5.setLeft(id, 0);\n'
     '      g5.setTop(id, 0);\n'
     '      g5.setFlex(id, "width", width);\n'
     '      g5.setFlex(id, "height", 26);\n'
     '      if (typeof g5.setFontSize === "function") g5.setFontSize(id, 10);\n'
     '      if (typeof g5.setFontWeight === "function") g5.setFontWeight(id, 400);\n'
     '      if (typeof g5.setLetterSpacing === "function") g5.setLetterSpacing(id, 0.8);\n'
     '      const label = String(owner.textContent || "").trim();\n'
     '      const left = (width - textWidth) / 2;\n'
     '      g5.setCapturedLineBoxes(id, [{ left, top: 6.5, width: textWidth,\n'
     '        height: 13, start: 0, length: label.length }], width,\n'
     '        "JetBrainsMono-Regular", false);\n'
     '      return { label, left, top: 6.5, width, text_width: textWidth };\n'
     '    };',
     '    const centerBandText = (owner, label, width, textWidth) => {\n'
     '      if (!owner || typeof g5.setCapturedLineBoxes !== "function") return null;\n'
     '      const targets = Array.isArray(owner.__pulpAnonymousTextTargets)\n'
     '        ? owner.__pulpAnonymousTextTargets : [];\n'
     '      const targetId = owner.__pulpTextTargetId || targets[0]?.id;\n'
     '      if (!targetId) return null;\n'
     '      const id = String(targetId);\n'
     '      g5.setPosition(id, "absolute");\n'
     '      g5.setLeft(id, 0);\n'
     '      g5.setTop(id, 0);\n'
     '      g5.setFlex(id, "width", width);\n'
     '      g5.setFlex(id, "height", 26);\n'
     '      if (typeof g5.setFontSize === "function") g5.setFontSize(id, 10);\n'
     '      if (typeof g5.setFontWeight === "function") g5.setFontWeight(id, 400);\n'
     '      if (typeof g5.setLetterSpacing === "function") g5.setLetterSpacing(id, 0.8);\n'
     '      const left = (width - textWidth) / 2;\n'
     '      g5.setCapturedLineBoxes(id, [{ left, top: 6.5, width: textWidth,\n'
     '        height: 13, start: 0, length: label.length }], width,\n'
     '        "JetBrainsMono-Regular", false);\n'
     '      return { label, left, top: 6.5, width, text_width: textWidth };\n'
     '    };',
     'const nativeTextOwner = (owner)'),
    ('band count centering labels flattened native owners explicitly',
     '    const bandTriggerLabel = bandTrigger && values.find((candidate) =>\n'
     '      belongsTo(candidate, bandTrigger)\n'
     '        && /^(32|40|48|56|64) bands \\u25BE$/.test(\n'
     '          String(candidate && candidate.textContent || "")));\n'
     '    const bandCountReceipt = {\n'
     '      trigger: centerBandText(bandTriggerLabel, 92, 73.03125, true),\n'
     '      options: []\n'
     '    };',
     '    const liveBandCount = Number(\n'
     '      globalThis.__spectrTestHooks?.appState?.()?.settings?.bandCount) || 32;\n'
     '    const bandCountReceipt = {\n'
     '      trigger: centerBandText(\n'
     '        bandTrigger, liveBandCount + " bands \\u25BE", 92, 73.03125),\n'
     '      options: []\n'
     '    };',
     'liveBandCount + " bands \\u25BE"'),
    ('band popup centering labels flattened option owners explicitly',
     '      const centered = centerBandText(option, 44, 13, false);',
     '      const optionLabel = String(option.getAttribute?.(\n'
     '        "data-spectr-band-count") || "");\n'
     '      const centered = centerBandText(option, optionLabel, 44, 13);',
     'const optionLabel = String(option.getAttribute?.'),
    ('band count centering can target a flattened native label directly',
     '      const targetId = owner.__pulpTextTargetId || targets[0]?.id;',
     '      const targetId = owner.__pulpTextTargetId || targets[0]?.id\n'
     '        || owner.__pulpId || owner.id;',
     'targets[0]?.id\n        || owner.__pulpId || owner.id'),
    ('band count centering resolves the painted native child',
     '    const liveBandCount = Number(\n'
     '      globalThis.__spectrTestHooks?.appState?.()?.settings?.bandCount) || 32;\n'
     '    const bandCountReceipt = {\n'
     '      trigger: centerBandText(\n'
     '        bandTrigger, liveBandCount + " bands \\u25BE", 92, 73.03125),',
     '    const nativeTextOwner = (owner) => values.find((candidate) => {\n'
     '      const parent = candidate?.parentElement || candidate?._parentElement || null;\n'
     '      return parent === owner && materializedNodeTag(candidate) === "span";\n'
     '    }) || owner;\n'
     '    const liveBandCount = Number(\n'
     '      globalThis.__spectrTestHooks?.appState?.()?.settings?.bandCount) || 32;\n'
     '    const bandCountReceipt = {\n'
     '      trigger: centerBandText(\n'
     '        nativeTextOwner(bandTrigger), liveBandCount + " bands \\u25BE",\n'
     '        92, 73.03125),',
     'const nativeTextOwner = (owner) => values.find'),
    ('band popup centering resolves each painted native child',
     '      const centered = centerBandText(option, optionLabel, 44, 13);',
     '      const centered = centerBandText(\n'
     '        nativeTextOwner(option), optionLabel, 44, 13);',
     'nativeTextOwner(option), optionLabel, 44, 13'),
    ('dynamic popup and manager states keep authored live layout',
     '    const activeLayoutBindings = (Array.isArray(metadata && metadata.layout_bindings)\n'
     '      ? metadata.layout_bindings : []).map((binding) => {\n'
     '        if (activeCapturedState !== "bands" || !binding?.box) return binding;\n'
     '        const box = binding.box;\n'
     '        const path = Array.isArray(binding.path) ? binding.path : [];\n'
     '        const tail = path[path.length - 1] || {};\n'
     '        if (box.width === 166 && box.height === 31)\n'
     '          return { ...binding, box: { ...box, left: -154.96875, width: 236 } };\n'
     '        if (box.width === 30 && box.height === 23 && tail.tag === "button")\n'
     '          return { ...binding, box: { ...box, left: 4 + tail.index * 46, width: 44 } };\n'
     '        if (box.width === 228.53125 && box.height === 19)\n'
     '          return { ...binding, box: { ...box, width: 239.53125 } };\n'
     '        if (box.width === 81.03125 && box.height === 19)\n'
     '          return { ...binding, box: { ...box, width: 92 } };\n'
     '        if (box.top === 3 && box.left >= 87.03125)\n'
     '          return { ...binding, box: { ...box, left: box.left + 10.96875 } };\n'
     '        return binding;\n'
     '      });',
     '    // These states are live, responsive UI. Their capture metadata is useful\n'
     '    // as a visual oracle, but applying its fixed boxes at runtime makes the\n'
     '    // header reflow and collapses the selected-preset action layout.\n'
     '    const authoredLayoutState = activeCapturedState === "bands";\n'
     '    const activeLayoutBindings = authoredLayoutState ? []\n'
     '      : (Array.isArray(metadata && metadata.layout_bindings)\n'
     '          ? metadata.layout_bindings : []);',
     'const authoredLayoutState = activeCapturedState === "bands"'),
    ('dynamic selected preset bypasses the frozen text binding',
     '    const activeTextBindings = Array.isArray(metadata && metadata.text_bindings) ? metadata.text_bindings : [];',
     '    // Selected preset names are authored state, not frozen capture text.\n'
     '    const activeTextBindings = (Array.isArray(metadata && metadata.text_bindings)\n'
     '      ? metadata.text_bindings : []).filter(\n'
     '        (binding) => binding.text !== "PRESETS \\u25BE");',
     'Selected preset names are authored state, not frozen capture text.'),
    ('dynamic popup and manager states keep authored live text layout',
     '    const activeTextBindings = (Array.isArray(metadata && metadata.text_bindings)\n'
     '      ? metadata.text_bindings : []).filter(',
     '    const authoredTextState = activeCapturedState === "bands";\n'
     '    const activeTextBindings = (authoredTextState ? []\n'
     '      : (Array.isArray(metadata && metadata.text_bindings)\n'
     '          ? metadata.text_bindings : [])).filter(',
     'const authoredTextState = activeCapturedState === "bands"'),
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
    ('live band count keeps captured vertical text alignment',
     '          && binding.text !== "\\u00D7");',
     '          && binding.text !== "\\u00D7"\n'
     '          && binding.text !== "bands \\u25BE"\n'
     '          && binding.text !== " bands \\u25BE").map((binding) => {\n'
     '        // Band count is live state and now owns one non-wrapping text node.\n'
     '        // Merge the old number and suffix captures into one stable line box.\n'
     '        if (binding.text === "32") {\n'
     '          const node = materializedNodeAtPath(binding, values);\n'
     '          const text = String(node?.textContent || "");\n'
     '          if (/^(32|40|48|56|64) bands \\u25BE$/.test(text)) return {\n'
     '            ...binding, text, basis: { ...binding.basis, width: 73.03125 },\n'
     '            boxes: [{ left: 0, top: 3, width: 73.03125, height: 13,\n'
     '              start: 0, length: text.length }],\n'
     '          };\n'
     '        }\n'
     '        return binding;\n'
     '      });',
     'Merge the old number and suffix captures into one stable line box.'),
    ('materialized text mismatches name the stale binding',
     '      text_content_mismatch: 0,\n'
     '      text_target_miss: 0,',
     '      text_content_mismatch: 0,\n'
     '      text_mismatches: [],\n'
     '      text_target_miss: 0,',
     'text_mismatches: []'),
    ('anonymous text mismatch diagnostics preserve expected and actual text',
     '        if (String(anonymousTarget.text || "") !== binding.text) {\n'
     '          if (optional) ++diagnostics.text_optional_miss;',
     '        if (String(anonymousTarget.text || "") !== binding.text) {\n'
     '          diagnostics.text_mismatches.push({ index: binding.index,\n'
     '            expected: binding.text, actual: String(anonymousTarget.text || ""),\n'
     '            anonymous: true });\n'
     '          if (optional) ++diagnostics.text_optional_miss;',
     'anonymous: true'),
    ('direct text mismatch diagnostics preserve expected and actual text',
     '      } else if (String(node.textContent || "") !== binding.text) {\n'
     '        if (optional) ++diagnostics.text_optional_miss;',
     '      } else if (String(node.textContent || "") !== binding.text) {\n'
     '        diagnostics.text_mismatches.push({ index: binding.index,\n'
     '          expected: binding.text, actual: String(node.textContent || ""),\n'
     '          anonymous: false });\n'
     '        if (optional) ++diagnostics.text_optional_miss;',
     'anonymous: false'),
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
    ('settings scroll upgrade restores native overlay ownership',
     '      if (panelId) {\n'
     '        // Replacing the captured overflow container with a real native\n',
     '      if (panelId) {\n'
     '        // React claimed the captured View before this ScrollView upgrade.\n'
     '        // Re-claim the stable id on the replacement so the framework\n'
     '        // routes Escape and outside presses against the panel bounds.\n'
     '        if (typeof g5.claimOverlay === "function") g5.claimOverlay(panelId);\n'
     '        // Replacing the captured overflow container with a real native\n',
     'g5.claimOverlay(panelId);'),
    ('band trigger shares the segmented-control header baseline',
     '    const centerBandText = (owner, label, width, textWidth) => {\n'
     '      if (!owner || typeof g5.setCapturedLineBoxes !== "function") return null;',
     '    const centerBandText = (owner, label, width, textWidth,\n'
     '                            height = 26, lineTop = 6.5) => {\n'
     '      if (!owner || typeof g5.setCapturedLineBoxes !== "function") return null;',
     'height = 26, lineTop = 6.5'),
    ('band text height is explicit per control',
     '      g5.setFlex(id, "height", 26);',
     '      g5.setFlex(id, "height", height);',
     'g5.setFlex(id, "height", height);'),
    ('band text receipt records its control geometry',
     '      g5.setCapturedLineBoxes(id, [{ left, top: 6.5, width: textWidth,\n'
     '        height: 13, start: 0, length: label.length }], width,\n'
     '        "JetBrainsMono-Regular", false);\n'
     '      return { label, left, top: 6.5, width, text_width: textWidth };',
     '      g5.setCapturedLineBoxes(id, [{ left, top: lineTop, width: textWidth,\n'
     '        height: 13, start: 0, length: label.length }], width,\n'
     '        "JetBrainsMono-Regular", false);\n'
     '      return { label, left, top: lineTop, width, height,\n'
     '               text_width: textWidth };',
     'return { label, left, top: lineTop, width, height'),
    ('band trigger rail aligns with header peers',
     '    const nativeTextOwner = (owner) => values.find((candidate) => {',
     '    const bandRootId = bandRoot && (bandRoot.__pulpId || bandRoot.id);\n'
     '    const bandTriggerId = bandTrigger\n'
     '      && (bandTrigger.__pulpId || bandTrigger.id);\n'
     '    // The captured band control was 19px tall and sat 2.5px below the two\n'
     '    // 24px segmented controls. Give its trigger the same 24px rail and let the\n'
     '    // header\'s normal flex alignment establish the shared baseline. Reserve\n'
     '    // the trigger\'s full painted width so it cannot overlap the zoom readout.\n'
     '    if (bandRootId) {\n'
     '      g5.setFlex(String(bandRootId), "width", 94);\n'
     '      g5.setFlex(String(bandRootId), "height", 24);\n'
     '    }\n'
     '    if (bandTriggerId) {\n'
     '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
     '      g5.setFlex(String(bandTriggerId), "height", 24);\n'
     '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5);\n'
     '    }\n'
     '    const nativeTextOwner = (owner) => values.find((candidate) => {',
     'const bandRootId = bandRoot && (bandRoot.__pulpId || bandRoot.id);'),
    ('band trigger reserves metadata gap and exact peer baseline',
     '      g5.setFlex(String(bandRootId), "width", 94);\n'
     '      g5.setFlex(String(bandRootId), "height", 24);\n'
     '    }\n'
     '    if (bandTriggerId) {\n'
     '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
     '      g5.setFlex(String(bandTriggerId), "height", 24);\n'
     '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5);\n',
     '      g5.setFlex(String(bandRootId), "width", 104);\n'
     '      g5.setFlex(String(bandRootId), "height", 24);\n'
     '    }\n'
     '    if (bandTriggerId) {\n'
     '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
     '      g5.setFlex(String(bandTriggerId), "height", 20);\n'
     '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5);\n',
     'g5.setFlex(String(bandRootId), "width", 104);'),
    ('band trigger uses the measured peer painted rail',
     '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
     '      g5.setFlex(String(bandTriggerId), "height", 20);\n'
     '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5);',
     '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
     '      g5.setFlex(String(bandTriggerId), "height", 22);\n'
     '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5);',
     'g5.setFlex(String(bandTriggerId), "height", 22);\n'
     '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5)'),
    ('band trigger text centers in the common 24px rail',
     '        92, 73.03125, 20, 3.5),',
     '        92, 73.03125, 22, 4.5),',
     '92, 73.03125, 22, 4.5'),
    ('dropdown optical centering follows every selected label',
     '      const label = descendants.find(\n'
     '        (node) => String(node && node.textContent || "") === correction.text\n'
     '      );',
     '      const label = descendants.find((node) => {\n'
     '        if (materializedNodeTag(node) !== "span"\n'
     '            || !/\\u25BE$/.test(String(node?.textContent || "").trim()))\n'
     '          return false;\n'
     '        return !descendants.some((candidate) => {\n'
     '          const parent = candidate?.parentElement\n'
     '            || candidate?._parentElement || null;\n'
     '          return parent === node && materializedNodeTag(candidate) === "span"\n'
     '            && /\\u25BE$/.test(String(candidate?.textContent || "").trim());\n'
     '        });\n'
     '      });',
     '!/\\u25BE$/.test(String(node?.textContent || "").trim())'),
    ('preset menu actions use separate full-width rows',
     '    g5.__spectrToolbarOpticalCenteringReceipt__ = receipt;\n'
     '    return receipt.length;',
     '    g5.__spectrToolbarOpticalCenteringReceipt__ = receipt;\n'
     '    if (activeCapturedState === "pattern") {\n'
     '      const popup = globalThis.document?.querySelector?.(\n'
     '        \'[data-spectr-menu-root="pattern"] [data-spectr-menu-options]\');\n'
     '      const save = globalThis.document?.querySelector?.(\n'
     '        \'[data-spectr-save-current]\');\n'
     '      const manage = globalThis.document?.querySelector?.(\n'
     '        \'[data-spectr-pattern-manage]\');\n'
     '      const footer = save && (save.parentElement || save._parentElement);\n'
     '      const setBox = (node, left, top, width, height) => {\n'
     '        const id = node && (node.__pulpId || node.id);\n'
     '        if (!id) return false;\n'
     '        g5.setPosition(String(id), "absolute");\n'
     '        g5.setLeft(String(id), left);\n'
     '        g5.setTop(String(id), top);\n'
     '        g5.setFlex(String(id), "width", width);\n'
     '        g5.setFlex(String(id), "height", height);\n'
     '        return true;\n'
     '      };\n'
     '      const patternReceipt = {\n'
     '        popup: setBox(popup, 0, -336, 220, 334),\n'
     '        footer: setBox(footer, 5, 264, 210, 63),\n'
     '        save: setBox(save, 0, 3, 210, 28),\n'
     '        manage: setBox(manage, 0, 33, 210, 28)\n'
     '      };\n'
     '      g5.__spectrPatternMenuLayoutReceipt__ = patternReceipt;\n'
     '    }\n'
     '    return receipt.length;',
     '__spectrPatternMenuLayoutReceipt__'),
]


def escaped(value):
    return json.dumps(value)[1:-1]


def repair_capture_band_count_binding(document, path):
    """Point the captured suffix binding at the span used by live HTML."""
    stale = [binding for binding in document.get('text_bindings', [])
             if binding.get('text') == ' bands ▾'
             and binding.get('anonymous_text_index') == 0]
    current = [binding for binding in document.get('text_bindings', [])
               if binding.get('text') == 'bands ▾'
               and binding.get('path', [])[-1:] == [{'tag': 'span', 'index': 1}]
               and 'anonymous_text_index' not in binding]
    if not stale:
        if len(current) != 1:
            sys.exit(
                f'FAIL band-count suffix binding: {path} has '
                f'{len(current)} current bindings')
        return False
    if len(stale) != 1 or current:
        sys.exit(
            f'FAIL band-count suffix binding: {path} has '
            f'{len(stale)} stale and {len(current)} current bindings')
    binding = stale[0]
    binding['path'].append({'tag': 'span', 'index': 1})
    del binding['anonymous_text_index']
    binding['text'] = 'bands ▾'
    return True


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

    # The runtime document is the shipping surface, while the unexecuted
    # document and captured-state metadata are the visual/test authorities.
    # Keep structural binding corrections identical across all three; leaving
    # the capture copies pointed at a removed anonymous text node makes a later
    # materialization silently restore the old band-selector regression.
    for capture_path in CAPTURE_DOCUMENT_PATHS:
        capture_raw = open(capture_path, encoding='utf-8').read()
        capture_document = json.loads(capture_raw)
        capture_changed = repair_capture_band_count_binding(
            capture_document, capture_path)
        if capture_changed:
            with open(capture_path, 'w', encoding='utf-8') as handle:
                json.dump(capture_document, handle, ensure_ascii=False, indent=2)
                handle.write('\n')
            print('applied          band-count suffix binding', capture_path)
            print('written', capture_path)

    runtime_raw = open(RUNTIME_PATH, encoding='utf-8').read()
    runtime_changed = False
    # 04d9ac3 already materialized a 24px trigger with a zero Y transform.
    # Normalize that one stale output back to this recipe's preceding state so
    # the measured -1.5px rail correction below is both replayable on a fresh
    # import and applicable to an editor patched by the older recipe.
    stale_band_rail = (
        '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
        '      g5.setFlex(String(bandTriggerId), "height", 24);\n'
        '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, 0);')
    replayable_band_rail = (
        '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
        '      g5.setFlex(String(bandTriggerId), "height", 20);\n'
        '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5);')
    previous_band_rail = (
        '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
        '      g5.setFlex(String(bandTriggerId), "height", 24);\n'
        '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5);')
    desired_band_rail = (
        '      g5.setFlex(String(bandTriggerId), "width", 92);\n'
        '      g5.setFlex(String(bandTriggerId), "height", 22);\n'
        '      g5.setTransform(String(bandTriggerId), 1, 0, 0, 1, 0, -1.5);')
    if desired_band_rail not in runtime_raw:
        for stale in (stale_band_rail, previous_band_rail):
            if stale in runtime_raw:
                runtime_raw = runtime_raw.replace(stale, replayable_band_rail, 1)
                runtime_changed = True
                print('normalized      band trigger rail regression')
                break
    previous_band_text = '        92, 73.03125, 24, 5.5),'
    replayable_band_text = '        92, 73.03125, 20, 3.5),'
    desired_band_text = '        92, 73.03125, 22, 4.5),'
    if (desired_band_text not in runtime_raw
            and previous_band_text in runtime_raw):
        runtime_raw = runtime_raw.replace(
            previous_band_text, replayable_band_text, 1)
        runtime_changed = True
        print('normalized      band trigger text regression')
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
