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
    ('opt-in performance diagnostics surface',
     '  }\n'
     '  const validTrace = (trace, expectedLength) => trace',
     '  }\n'
     '  const perfDiagnostics = globalThis.SpectrPerfDiagnostics || {\n'
     '    active: null,\n'
     '    enable() {\n'
     '      this.active = { rawPointerSamples: 0, analyzerSamples: 0, animationFrames: 0,\n'
     '        canvasRedraws: 0, statePublications: 0, frameIntervalsMs: [],\n'
     '        drawDurationsMs: [], inputToPublicationMs: [], lastFrameMs: null,\n'
     '        latestInputMs: null };\n'
     '      return this.snapshot();\n'
     '    },\n'
     '    disable() { const result = this.snapshot(); this.active = null; return result; },\n'
     '    snapshot() { return this.active ? JSON.parse(JSON.stringify(this.active)) : null; },\n'
     '  };\n'
     '  globalThis.SpectrPerfDiagnostics = perfDiagnostics;\n'
     '  if (typeof window !== "undefined") window.SpectrPerfDiagnostics = perfDiagnostics;\n'
     '  const validTrace = (trace, expectedLength) => trace'),

    ('accepted analyzer grids are immutable',
     'magnitude_db: payload.visible.magnitude_db.slice() },\n'
     '      overview: { ...payload.overview,\n'
     '        magnitude_db: payload.overview.magnitude_db.slice() },',
     'magnitude_db: Object.freeze(payload.visible.magnitude_db.slice()) },\n'
     '      overview: { ...payload.overview,\n'
     '        magnitude_db: Object.freeze(payload.overview.magnitude_db.slice()) },'),

    ('analyzer grid diagnostic counters',
     'this.active = { rawPointerSamples: 0, analyzerSamples: 0, animationFrames: 0,',
     'this.active = { rawPointerSamples: 0, analyzerSamples: 0, analyzerGridHits: 0,\n'
     '        analyzerGridMisses: 0, animationFrames: 0,'),

    ('analyzer diagnostics count scalar samples',
     "    sample(logFrequency, _time, traceName = 'visible') {\n"
     '      if (!analyzerFrame) return 0;',
     "    sample(logFrequency, _time, traceName = 'visible') {\n"
     '      if (perfDiagnostics.active) perfDiagnostics.active.analyzerSamples += 1;\n'
     '      if (!analyzerFrame) return 0;'),

    ('exact analyzer grid lookup',
     '    project(amount, zeroY, halfH) {\n'
     '      return projectAnalyzerAmount(amount, zeroY, halfH);\n'
     '    },\n'
     '    debugSnapshot() {',
     '    project(amount, zeroY, halfH) {\n'
     '      return projectAnalyzerAmount(amount, zeroY, halfH);\n'
     '    },\n'
     '    grid(traceName, minHz, maxHz, pointCount) {\n'
     '      if (!analyzerFrame) return null;\n'
     "      const trace = traceName === 'overview' ? analyzerFrame.overview : analyzerFrame.visible;\n"
     '      return trace.magnitude_db.length === pointCount\n'
     '          && trace.min_hz === minHz && trace.max_hz === maxHz\n'
     '        ? trace.magnitude_db : null;\n'
     '    },\n'
     '    debugSnapshot() {'),

    ('analyzer grid diagnostics record eligibility',
     '      return trace.magnitude_db.length === pointCount\n'
     '          && close(trace.min_hz, minHz) && close(trace.max_hz, maxHz)\n'
     '        ? trace.magnitude_db : null;',
     '      const matches = trace.magnitude_db.length === pointCount\n'
     '          && trace.min_hz === minHz && trace.max_hz === maxHz;\n'
     '      if (perfDiagnostics.active)\n'
     '        perfDiagnostics.active[matches ? "analyzerGridHits" : "analyzerGridMisses"] += 1;\n'
     '      return matches ? trace.magnitude_db : null;'),

    ('analyzer grids require identical endpoints',
     '      const close = (a, b) => Math.abs(a - b)\n'
     '        <= Math.max(0.001, Math.abs(b) * 1e-6);\n'
     '      const matches = trace.magnitude_db.length === pointCount\n'
     '          && close(trace.min_hz, minHz) && close(trace.max_hz, maxHz)\n',
     '      const matches = trace.magnitude_db.length === pointCount\n'
     '          && trace.min_hz === minHz && trace.max_hz === maxHz;\n'),
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

    ('popup keyboard and pointer share one visible active item',
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
     '      document.removeEventListener("keydown", onKey, true);',
     '    const options = Array.from(document.querySelectorAll(\n'
     '      rootSelector + " [data-spectr-menu-options] button:not([disabled])"));\n'
     '    const menuState = {activeIndex: -1};\n'
     '    globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};\n'
     '    globalThis.__spectrTestHooks.menuState = globalThis.__spectrTestHooks.menuState || {};\n'
     '    globalThis.__spectrTestHooks.menuState[key] = menuState;\n'
     '    const markActive = (index, moveFocus) => {\n'
     '      menuState.activeIndex = index;\n'
     '      options.forEach((option, optionIndex) => {\n'
     '        if (option.__spectrMenuBaseBackground === void 0) {\n'
     '          option.__spectrMenuBaseBackground = option.style.background || "";\n'
     '          option.__spectrMenuBaseBorderColor = option.style.borderColor || "";\n'
     '        }\n'
     '        const active = optionIndex === index;\n'
     '        option.setAttribute("data-spectr-menu-active", active ? "true" : "false");\n'
     '        option.setAttribute("aria-selected", active ? "true" : "false");\n'
     '        option.style.background = active ? "rgba(120,180,255,0.18)" : option.__spectrMenuBaseBackground;\n'
     '        option.style.borderColor = active ? "rgba(180,210,255,0.42)" : option.__spectrMenuBaseBorderColor;\n'
     '      });\n'
     '      if (moveFocus && options[index]) options[index].focus();\n'
     '    };\n'
     '    const hoverHandlers = options.map((option, index) => {\n'
     '      const onEnter = () => markActive(index, false);\n'
     '      option.addEventListener("pointerenter", onEnter);\n'
     '      return onEnter;\n'
     '    });\n'
     '    if (options.length) markActive(0, false);\n'
     '    const navigationFocusClaimed =\n'
     '      typeof globalThis.claimDocumentNavigationFocus !== "function"\n'
     '      || globalThis.claimDocumentNavigationFocus();\n'
     '    if (!navigationFocusClaimed) return;\n'
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
     '      if (!options.length) return;\n'
     '      if (event.key === "ArrowDown" || event.key === "ArrowUp" || event.key === "Home" || event.key === "End") {\n'
     '        event.preventDefault();\n'
     '        event.stopPropagation();\n'
     '        const current = menuState.activeIndex;\n'
     '        const next = event.key === "Home" ? 0 : event.key === "End" ? options.length - 1 : event.key === "ArrowDown" ? (current + 1 + options.length) % options.length : (current - 1 + options.length) % options.length;\n'
     '        markActive(next, true);\n'
     '      } else if ((event.key === "Enter" || event.key === " ") && options.includes(document.activeElement)) {\n'
     '        event.preventDefault();\n'
     '        event.stopPropagation();\n'
     '        restoreMenuFocus.current = true;\n'
     '        document.activeElement.click();\n'
     '      }\n'
     '    };\n'
     '    document.addEventListener("mousedown", onPointer);\n'
     '    document.addEventListener("pointerdown", onPointer);\n'
     '    document.addEventListener("keydown", onKey, true);\n'
     '    return () => {\n'
     '      document.removeEventListener("mousedown", onPointer);\n'
     '      document.removeEventListener("pointerdown", onPointer);\n'
     '      document.removeEventListener("keydown", onKey, true);\n'
     '      options.forEach((option, index) => {\n'
     '        option.removeEventListener("pointerenter", hoverHandlers[index]);\n'
     '      });\n'
     '      if (globalThis.__spectrTestHooks?.menuState?.[key] === menuState)\n'
     '        delete globalThis.__spectrTestHooks.menuState[key];\n'
     '      if (typeof globalThis.releaseDocumentNavigationFocus === "function")\n'
     '        globalThis.releaseDocumentNavigationFocus();'),

    ('native menu avoids unsupported negation selector',
     '    const options = Array.from(document.querySelectorAll(\n'
     '      rootSelector + " [data-spectr-menu-options] button:not([disabled])"));',
     '    const options = Array.from(document.querySelectorAll(\n'
     '      rootSelector + " [data-spectr-menu-options] button"))\n'
     '      .filter(option => !option.disabled);'),

    ('native menu tracks active row independently of mutable attributes',
     '    const markActive = (index, moveFocus) => {\n'
     '      options.forEach((option, optionIndex) => {',
     '    const menuState = {activeIndex: -1};\n'
     '    globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};\n'
     '    globalThis.__spectrTestHooks.menuState = globalThis.__spectrTestHooks.menuState || {};\n'
     '    globalThis.__spectrTestHooks.menuState[key] = menuState;\n'
     '    const markActive = (index, moveFocus) => {\n'
     '      menuState.activeIndex = index;\n'
     '      options.forEach((option, optionIndex) => {'),

    ('native menu effect exposes its bounded test receipt',
     '    const key = openMenu || (helpOpen ? "help" : null);\n'
     '    if (!key) return;\n'
     '    const rootSelector =',
     '    const key = openMenu || (helpOpen ? "help" : null);\n'
     '    if (!key) return;\n'
     '    globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};\n'
     '    globalThis.__spectrTestHooks.menuEffectKey = key;\n'
     '    const rootSelector ='),

    ('native bootstrap service leaves React popup effects authoritative',
     '    if (globalThis.__spectrNativeMenuService) return;\n'
     '    globalThis.__spectrTestHooks =',
     '    globalThis.__spectrTestHooks ='),

    ('native menu navigation reads authoritative active row',
     '        const current = options.findIndex((option) => option.getAttribute("data-spectr-menu-active") === "true");',
     '        const current = menuState.activeIndex;'),

    ('native menu clears active-row test receipt on teardown',
     '      options.forEach((option, index) => {\n'
     '        option.removeEventListener("pointerenter", hoverHandlers[index]);\n'
     '      });\n'
     '      if (typeof globalThis.releaseDocumentNavigationFocus === "function")',
     '      options.forEach((option, index) => {\n'
     '        option.removeEventListener("pointerenter", hoverHandlers[index]);\n'
     '      });\n'
     '      if (globalThis.__spectrTestHooks?.menuState?.[key] === menuState)\n'
     '        delete globalThis.__spectrTestHooks.menuState[key];\n'
     '      if (globalThis.__spectrTestHooks?.menuEffectKey === key)\n'
     '        delete globalThis.__spectrTestHooks.menuEffectKey;\n'
     '      if (typeof globalThis.releaseDocumentNavigationFocus === "function")'),

    ('native menu clears effect test receipt on teardown',
     '      if (globalThis.__spectrTestHooks?.menuState?.[key] === menuState)\n'
     '        delete globalThis.__spectrTestHooks.menuState[key];\n'
     '      if (typeof globalThis.releaseDocumentNavigationFocus === "function")',
     '      if (globalThis.__spectrTestHooks?.menuState?.[key] === menuState)\n'
     '        delete globalThis.__spectrTestHooks.menuState[key];\n'
     '      if (globalThis.__spectrTestHooks?.menuEffectKey === key)\n'
     '        delete globalThis.__spectrTestHooks.menuEffectKey;\n'
     '      if (typeof globalThis.releaseDocumentNavigationFocus === "function")'),

    ('materialized popup claims bounded native navigation focus',
     '    if (options.length) markActive(0, false);\n'
     '    const onPointer = (event) => {',
     '    if (options.length) markActive(0, false);\n'
     '    const navigationFocusClaimed =\n'
     '      typeof globalThis.claimDocumentNavigationFocus !== "function"\n'
     '      || globalThis.claimDocumentNavigationFocus();\n'
     '    if (!navigationFocusClaimed) return;\n'
     '    const onPointer = (event) => {'),

    ('materialized popup releases native navigation focus',
     '      options.forEach((option, index) => {\n'
     '        option.removeEventListener("pointerenter", hoverHandlers[index]);\n'
     '      });\n'
     '      if (restoreMenuFocus.current) {',
     '      options.forEach((option, index) => {\n'
     '        option.removeEventListener("pointerenter", hoverHandlers[index]);\n'
     '      });\n'
     '      if (typeof globalThis.releaseDocumentNavigationFocus === "function")\n'
     '        globalThis.releaseDocumentNavigationFocus();\n'
     '      if (restoreMenuFocus.current) {'),

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
     '      rafRef.current = requestAnimationFrame(draw);\n'
     '    };'),

    ('opt-in frame diagnostics',
     '      (renderAllRef.current || renderAll)();\n'
     '      rafRef.current = requestAnimationFrame(draw);',
     '      const perf = window.SpectrPerfDiagnostics && window.SpectrPerfDiagnostics.active;\n'
     '      const drawStarted = perf ? performance.now() : 0;\n'
     '      if (perf) {\n'
     '        perf.animationFrames += 1;\n'
     '        if (perf.lastFrameMs !== null) {\n'
     '          perf.frameIntervalsMs.push(now - perf.lastFrameMs);\n'
     '          if (perf.frameIntervalsMs.length > 512) perf.frameIntervalsMs.shift();\n'
     '        }\n'
     '        perf.lastFrameMs = now;\n'
     '      }\n'
     '      (renderAllRef.current || renderAll)();\n'
     '      if (perf) {\n'
     '        perf.canvasRedraws += 1;\n'
     '        perf.drawDurationsMs.push(performance.now() - drawStarted);\n'
     '        if (perf.drawDurationsMs.length > 512) perf.drawDurationsMs.shift();\n'
     '      }\n'
     '      rafRef.current = requestAnimationFrame(draw);'),

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

    ('direct publication frame slot',
     '  const nativeDirectPublicationSignatureRef = useRef("");\n'
     '  const nativeCommandSequenceRef = useRef(0);',
     '  const nativeDirectPublicationSignatureRef = useRef("");\n'
     '  const nativeDirectPublicationFrameRef = useRef(0);\n'
     '  const nativeCommandSequenceRef = useRef(0);'),

    ('direct processing-state publication is frame-coalesced',
     'const queueNativeProcessingStatePublication = () => {\n'
     '    if (!window.pulp || typeof window.pulp.postMessage !== "function") return;\n'
     '    const current = targetGainsRef.current;\n'
     '    const muted = current.map((value) => isMuted(value));\n'
     '    const gainDb2 = current.map((value, i) => isMuted(value) ? Number.isFinite(mutedGainDbRef.current[i]) ? mutedGainDbRef.current[i] : 0 : clamp(Number.isFinite(value) ? value : 0, -1, 1) * 24);\n'
     '    nativeDirectPublicationSignatureRef.current = JSON.stringify([\n'
     '      N, gainDb2, muted, view.lmin, view.lmax\n'
     '    ]);\n'
     '    ++nativeCommandSequenceRef.current;\n'
     '    Promise.resolve(window.pulp.postMessage("processing_state_set", {\n'
     '      n_visible: N,\n'
     '      gain_db: gainDb2,\n'
     '      muted,\n'
     '      min_hz: Math.pow(10, view.lmin),\n'
     '      max_hz: Math.pow(10, view.lmax)\n'
     '    }, "spectr-processing-state")).catch((error) => {\n'
     '      console.error("[Spectr] native direct-edit publication failed", error);\n'
     '    });\n'
     '  };',
     'const queueNativeProcessingStatePublication = () => {\n'
     '    if (!window.pulp || typeof window.pulp.postMessage !== "function"\n'
     '        || nativeDirectPublicationFrameRef.current) return;\n'
     '    nativeDirectPublicationFrameRef.current = requestAnimationFrame(() => {\n'
     '      nativeDirectPublicationFrameRef.current = 0;\n'
     '      const current = targetGainsRef.current;\n'
     '      const muted = current.map((value) => isMuted(value));\n'
     '      const gainDb2 = current.map((value, i) => isMuted(value) ? Number.isFinite(mutedGainDbRef.current[i]) ? mutedGainDbRef.current[i] : 0 : clamp(Number.isFinite(value) ? value : 0, -1, 1) * 24);\n'
     '      nativeDirectPublicationSignatureRef.current = JSON.stringify([\n'
     '        N, gainDb2, muted, view.lmin, view.lmax\n'
     '      ]);\n'
     '      ++nativeCommandSequenceRef.current;\n'
     '      Promise.resolve(window.pulp.postMessage("processing_state_set", {\n'
     '        n_visible: N,\n'
     '        gain_db: gainDb2,\n'
     '        muted,\n'
     '        min_hz: Math.pow(10, view.lmin),\n'
     '        max_hz: Math.pow(10, view.lmax)\n'
     '      }, "spectr-processing-state")).catch((error) => {\n'
     '        console.error("[Spectr] native direct-edit publication failed", error);\n'
     '      });\n'
     '    });\n'
     '  };'),

    ('opt-in publication diagnostics',
     '      nativeDirectPublicationFrameRef.current = 0;\n'
     '      const current = targetGainsRef.current;',
     '      nativeDirectPublicationFrameRef.current = 0;\n'
     '      const perf = window.SpectrPerfDiagnostics && window.SpectrPerfDiagnostics.active;\n'
     '      if (perf) {\n'
     '        perf.statePublications += 1;\n'
     '        if (perf.latestInputMs !== null) {\n'
     '          perf.inputToPublicationMs.push(performance.now() - perf.latestInputMs);\n'
     '          if (perf.inputToPublicationMs.length > 512) perf.inputToPublicationMs.shift();\n'
     '          perf.latestInputMs = null;\n'
     '        }\n'
     '      }\n'
     '      const current = targetGainsRef.current;'),

    ('opt-in raw pointer diagnostics',
     '  const onPointerMove = (e) => {\n'
     '    const g = getGeom();',
     '  const onPointerMove = (e) => {\n'
     '    const perf = window.SpectrPerfDiagnostics && window.SpectrPerfDiagnostics.active;\n'
     '    if (perf) {\n'
     '      perf.rawPointerSamples += 1;\n'
     '      perf.latestInputMs = Number.isFinite(e.timeStamp) ? e.timeStamp : performance.now();\n'
     '    }\n'
     '    const g = getGeom();'),

    ('input diagnostics use the performance clock',
     '      perf.latestInputMs = Number.isFinite(e.timeStamp) ? e.timeStamp : performance.now();',
     '      perf.latestInputMs = performance.now();'),

    ('spectrum uses an exact bulk analyzer grid',
     '    const arr = new Float32Array(steps + 1);\n'
     '    for (let i = 0; i <= steps; i++) {\n'
     '      const lf = view.lmin + i / steps * span;\n'
     '      arr[i] = window.SpectrAnalyzer.sample(lf, t, "visible");\n'
     '    }',
     '    const arr = new Float32Array(steps + 1);\n'
     '    const analyzerGrid = window.SpectrAnalyzer.grid("visible",\n'
     '      Math.pow(10, view.lmin), Math.pow(10, view.lmax), steps + 1);\n'
     '    for (let i = 0; i <= steps; i++) {\n'
     '      const lf = view.lmin + i / steps * span;\n'
     '      arr[i] = analyzerGrid\n'
     '        ? window.SpectrAnalyzer.normalizeDb(analyzerGrid[i])\n'
     '        : window.SpectrAnalyzer.sample(lf, t, "visible");\n'
     '    }'),

    ('spectrum bulk grid is capability guarded',
     '    const analyzerGrid = window.SpectrAnalyzer.grid("visible",\n'
     '      Math.pow(10, view.lmin), Math.pow(10, view.lmax), steps + 1);',
     '    const analyzerGrid = typeof window.SpectrAnalyzer.grid === "function"\n'
     '      ? window.SpectrAnalyzer.grid("visible", Math.pow(10, view.lmin),\n'
     '          Math.pow(10, view.lmax), steps + 1)\n'
     '      : null;'),

    ('minimap uses its exact bulk analyzer grid',
     '    const steps = 120;\n'
     '    ctx.beginPath();\n'
     '    ctx.moveTo(mx, my + mh);\n'
     '    for (let i = 0; i <= steps; i++) {\n'
     '      const lf = fullMin + i / steps * fullSpan;\n'
     '      const v = window.SpectrAnalyzer.sample(lf, timeRef.current, "overview");',
     '    const steps = 120;\n'
     '    const analyzerGrid = window.SpectrAnalyzer.grid("overview", 20, 20000, steps + 1);\n'
     '    ctx.beginPath();\n'
     '    ctx.moveTo(mx, my + mh);\n'
     '    for (let i = 0; i <= steps; i++) {\n'
     '      const lf = fullMin + i / steps * fullSpan;\n'
     '      const v = analyzerGrid\n'
     '        ? window.SpectrAnalyzer.normalizeDb(analyzerGrid[i])\n'
     '        : window.SpectrAnalyzer.sample(lf, timeRef.current, "overview");'),

    ('minimap bulk grid is capability guarded',
     '    const analyzerGrid = window.SpectrAnalyzer.grid("overview", 20, 20000, steps + 1);',
     '    const analyzerGrid = typeof window.SpectrAnalyzer.grid === "function"\n'
     '      ? window.SpectrAnalyzer.grid("overview", 20, 20000, steps + 1)\n'
     '      : null;'),

    # SAVE CURRENT... and MANAGE... shared one row because `menuItem` sets no
    # `display`, so the buttons defaulted to inline. MANAGE read as a modifier
    # on SAVE rather than its own action, and users did not find it. Two rows.
    ('preset dropdown footer actions get their own rows',
     'style: { ...menuItem, color: "hsl(200,85%,70%)" }',
     'style: { ...menuItem, color: "hsl(200,85%,70%)", '
     'display: "block", width: "100%" }'),

    ('default-on status info setting',
     '  ))), /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { title: "STRUCTURE", subtitle: "Band count, mute behavior, chrome." }',
     '  ))), /* @__PURE__ */ React.createElement(SpectrSettingsField, { label: "Status info", hint: "Hover and action readouts" }, /* @__PURE__ */ React.createElement("span", { "data-spectr-status-info-setting": true }, /* @__PURE__ */ React.createElement(SpectrSettingsToggle, { value: settings.showStatusInfo !== false, onChange: (v) => persist({ showStatusInfo: v }) }))), /* @__PURE__ */ React.createElement(SpectrSettingsGroup, { title: "STRUCTURE", subtitle: "Band count, mute behavior, chrome." }'),

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

    ('status info setting gates the publisher without skipping actions',
     '  const fireStatus = useAppC((msg) => {\n'
     '    if (!msg) {\n'
     '      setStatus("");\n'
     '      return;\n'
     '    }\n'
     '    setStatus(msg + "|" + Date.now());',
     '  const fireStatus = useAppC((msg) => {\n'
     '    if (!msg || settings.showStatusInfo === false) setStatus("");\n'
     '    else setStatus(msg + "|" + Date.now());'),

    ('status info setting refreshes the publisher',
     '  }, []);\n'
     '  const applyPattern = useAppC((p) => {',
     '  }, [settings.showStatusInfo]);\n'
     '  const applyPattern = useAppC((p) => {'),

    ('test hook reports current unified status',
     '      editMode,\n'
     '      analyzerMode,\n'
     '      visualizationMode,\n'
     '      snapshotStatus:',
     '      editMode,\n'
     '      analyzerMode,\n'
     '      visualizationMode,\n'
     '      status,\n'
     '      snapshotStatus:'),

    ('status banner replacements keep newest value immediate',
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
     '  }, [message]);',
     '  const [text, setText] = useStateChrome("");\n'
     '  const shownRef = useRefChrome("");\n'
     '  const [settled, setSettled] = useStateChrome(true);\n'
     '  const reducedMotion = !!(window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches);\n'
     '  useEffectChrome(() => {\n'
     '    const display = message ? message.split("|")[0].trim() : "";\n'
     '    if (!display) {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '      return;\n'
     '    }\n'
     '    const replacing = !!shownRef.current && shownRef.current !== display;\n'
     '    setText(display);\n'
     '    setVisible(true);\n'
     '    setSettled(!replacing || reducedMotion);\n'
     '    const settleFrame = replacing && !reducedMotion ? requestAnimationFrame(() => setSettled(true)) : 0;\n'
     '    shownRef.current = display;\n'
     '    const hideTimer = setTimeout(() => {\n'
     '      setVisible(false);\n'
     '      setText("");\n'
     '      shownRef.current = "";\n'
     '    }, 1400);\n'
     '    return () => {\n'
     '      if (settleFrame) cancelAnimationFrame(settleFrame);\n'
     '      clearTimeout(hideTimer);\n'
     '    };\n'
     '  }, [message]);'),

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

    ('status banner settles without delaying text',
     '        transform: "translateX(-50%)",\n'
     '        width: Math.max(96, Math.min(520, text.length * 8 + 28)),',
     '        transform: "translateX(-50%) scale(" + (settled ? 1 : 0.985) + ")",\n'
     '        width: Math.max(96, Math.min(520, text.length * 8 + 28)),'),

    ('status banner honors reduced motion',
     '        transition: "width 0.18s ease, opacity 0.15s ease",',
     '        transition: reducedMotion ? "none" : "width 0.18s ease, opacity 0.15s ease, transform 0.12s ease",'),

    # ----------------------------- task 4: mute consistency across edit modes
    ('redraw-unmutes setting is read by the bank',
     '  const { bandCount, metaphor, bloom, spectrumIntensity, muteStyle, '
     'motionMode, showMinimap, showRulers, theme } = settings;',
     '  const { bandCount, metaphor, bloom, spectrumIntensity, muteStyle, '
     'motionMode, showMinimap, showRulers, theme, unmuteOnDraw } = settings;'),

    ('status info setting is read by the bank',
     'motionMode, showMinimap, showRulers, theme, unmuteOnDraw } = settings;',
     'motionMode, showMinimap, showRulers, theme, unmuteOnDraw, showStatusInfo } = settings;'),

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

    ('hover status respects its default-on setting',
     '    if (!onStatus) return;\n'
     '    if (hoverBand < 0) {',
     '    if (!onStatus) return;\n'
     '    if (showStatusInfo === false) {\n'
     '      onStatus("");\n'
     '      return;\n'
     '    }\n'
     '    if (hoverBand < 0) {'),

    ('hover status setting invalidates its effect',
     '  }, [hoverBand, N, onStatus]);\n'
     '  const [ctxMenu, setCtxMenu] = useState(null);',
     '  }, [hoverBand, N, onStatus, showStatusInfo]);\n'
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
     '      wrapRef.current.style.cursor = activeMini ? "grabbing" : "grab";'),

    ('minimap release restores grab cursor',
     '  const onPointerUp = (e) => {\n'
     '    const p = pointerRef.current;\n'
     '    pointerRef.current = { mode: null };\n'
     '    if (!p || !p.mode) {',
     '  const onPointerUp = (e) => {\n'
     '    const p = pointerRef.current;\n'
     '    pointerRef.current = { mode: null };\n'
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize"))\n'
     '      wrapRef.current.style.cursor = "grab";\n'
     '    if (!p || !p.mode) {'),

    ('surface leave resets idle cursor',
     '      onPointerLeave: () => setHover(null),',
     '      onPointerLeave: () => {\n'
     '        setHover(null);\n'
     '        if (!pointerRef.current || !pointerRef.current.mode)\n'
     '          wrapRef.current.style.cursor = "default";\n'
     '      },'),

    ('minimap press cursor preserves hit role',
     '    if (mm) {\n'
     '      wrapRef.current.style.cursor = "grabbing";\n'
     '      const fullMin = Math.log10(20), fullMax = Math.log10(2e4);',
     '    if (mm) {\n'
     '      wrapRef.current.style.cursor = mm === "left" || mm === "right"\n'
     '        ? "ew-resize" : mm === "window" ? "grabbing" : "pointer";\n'
     '      const fullMin = Math.log10(20), fullMax = Math.log10(2e4);'),

    ('minimap cursors preserve each hit role',
     '      const activeMini = pointerRef.current\n'
     '        && (pointerRef.current.mode === "minimap-drag"\n'
     '          || pointerRef.current.mode === "minimap-resize");\n'
     '      wrapRef.current.style.cursor = activeMini ? "grabbing" : "grab";',
     '      const activeMini = pointerRef.current && pointerRef.current.mode;\n'
     '      wrapRef.current.style.cursor = activeMini === "minimap-resize"\n'
     '        ? "ew-resize"\n'
     '        : activeMini === "minimap-drag"\n'
     '          ? "grabbing"\n'
     '          : mm === "left" || mm === "right"\n'
     '            ? "ew-resize"\n'
     '            : mm === "window" ? "grab" : "pointer";'),

    ('minimap release restores role cursor',
     '    if (p && (p.mode === "minimap-drag" || p.mode === "minimap-resize"))\n'
     '      wrapRef.current.style.cursor = "grab";',
     '    if (p && p.mode === "minimap-drag")\n'
     '      wrapRef.current.style.cursor = "grab";\n'
     '    else if (p && p.mode === "minimap-resize")\n'
     '      wrapRef.current.style.cursor = "ew-resize";'),

    ('overflow popup exposes semantic trigger state',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => setOverflowMenu((v) => !v), active: overflowMenu }',
     'React.createElement("span", { "data-spectr-menu-trigger": true, "aria-haspopup": "menu", "aria-expanded": overflowMenu }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => setOverflowMenu((v) => !v), active: overflowMenu }'),

    ('edit popup exposes semantic trigger state',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => toggleMenu("edit"), active: editMenu }',
     'React.createElement("span", { "data-spectr-menu-trigger": true, "aria-haspopup": "listbox", "aria-expanded": editMenu }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => toggleMenu("edit"), active: editMenu }'),

    ('analyzer popup exposes semantic trigger state',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => toggleMenu("analyzer"), active: analyzerMenu }',
     'React.createElement("span", { "data-spectr-menu-trigger": true, "aria-haspopup": "listbox", "aria-expanded": analyzerMenu }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => toggleMenu("analyzer"), active: analyzerMenu }'),

    ('pattern popup exposes semantic trigger state',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => setPatternMenu((v) => !v), active: patternMenu }',
     'React.createElement("span", { "data-spectr-menu-trigger": true, "aria-haspopup": "menu", "aria-expanded": patternMenu }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => setPatternMenu((v) => !v), active: patternMenu }'),

    ('rail buttons accept semantic trigger properties',
     'function RailBtn({ children, onClick, active }) {',
     'function RailBtn({ children, onClick, active, ...buttonProps }) {'),

    ('rail buttons forward semantic trigger properties',
     '    {\n      onClick: handle,\n      style: {',
     '    {\n      ...buttonProps,\n      onClick: handle,\n      style: {'),

    ('overflow semantics reach the focusable trigger',
     'React.createElement("span", { "data-spectr-menu-trigger": true, "aria-haspopup": "menu", "aria-expanded": overflowMenu }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => setOverflowMenu((v) => !v), active: overflowMenu }',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn, { "aria-haspopup": "menu", "aria-expanded": overflowMenu, onClick: () => setOverflowMenu((v) => !v), active: overflowMenu }'),

    ('edit semantics reach the focusable trigger',
     'React.createElement("span", { "data-spectr-menu-trigger": true, "aria-haspopup": "listbox", "aria-expanded": editMenu }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => toggleMenu("edit"), active: editMenu }',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn, { "aria-haspopup": "listbox", "aria-expanded": editMenu, onClick: () => toggleMenu("edit"), active: editMenu }'),

    ('analyzer semantics reach the focusable trigger',
     'React.createElement("span", { "data-spectr-menu-trigger": true, "aria-haspopup": "listbox", "aria-expanded": analyzerMenu }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => toggleMenu("analyzer"), active: analyzerMenu }',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn, { "aria-haspopup": "listbox", "aria-expanded": analyzerMenu, onClick: () => toggleMenu("analyzer"), active: analyzerMenu }'),

    ('pattern semantics reach the focusable trigger',
     'React.createElement("span", { "data-spectr-menu-trigger": true, "aria-haspopup": "menu", "aria-expanded": patternMenu }, /* @__PURE__ */ React.createElement(RailBtn, { onClick: () => setPatternMenu((v) => !v), active: patternMenu }',
     'React.createElement("span", { "data-spectr-menu-trigger": true }, /* @__PURE__ */ React.createElement(RailBtn, { "aria-haspopup": "menu", "aria-expanded": patternMenu, onClick: () => setPatternMenu((v) => !v), active: patternMenu }'),

]

# These recipes migrated an app-specific popup controller that is now owned by
# Pulp's semantic materialized-control default. Keep them out of the active
# patch stream while the cleanup below removes already-emitted legacy code.
OBSOLETE_POPUP_EDITS = {
    'native menu lookup uses the document selector surface',
    'native menu option lookup uses the document selector surface',
    'popup keyboard and pointer share one visible active item',
    'native menu avoids unsupported negation selector',
    'native menu tracks active row independently of mutable attributes',
    'native menu effect exposes its bounded test receipt',
    'native bootstrap service leaves React popup effects authoritative',
    'native menu navigation reads authoritative active row',
    'native menu clears active-row test receipt on teardown',
    'native menu clears effect test receipt on teardown',
    'materialized popup claims bounded native navigation focus',
    'materialized popup releases native navigation focus',
}
EDITS = [edit for edit in EDITS if edit[0] not in OBSOLETE_POPUP_EDITS]

# A later edit may deliberately consume the exact replacement image of an
# earlier one. These named sentinels keep reruns strict without pretending the
# superseded intermediate text must remain in the final shipping document.
SUPERSEDED_SENTINELS = {
    'opt-in performance diagnostics surface':
        'analyzerGridHits',
    'exact analyzer grid lookup':
        'analyzerGridMisses',
    'spectrum uses an exact bulk analyzer grid':
        'typeof window.SpectrAnalyzer.grid === "function"',
    'minimap uses its exact bulk analyzer grid':
        'window.SpectrAnalyzer.grid("overview", 20, 20000, steps + 1)',
    'native menu option lookup uses the document selector surface':
        'const markActive = (index, moveFocus)',
    'popup keyboard and pointer share one visible active item':
        '.filter(option => !option.disabled)',
    'empty status clears the unified banner':
        'settings.showStatusInfo === false',
    'redraw-unmutes setting is read by the bank':
        'unmuteOnDraw, showStatusInfo',
    'hover readout uses unified status banner':
        'showStatusInfo === false',
    'animation loop paints the latest canvas renderer':
        'perf.animationFrames += 1',
    'direct processing-state publication is frame-coalesced':
        'perf.statePublications += 1',
    'opt-in raw pointer diagnostics':
        'perf.latestInputMs = performance.now()',
    'hover readout clears the status banner slot':
        'if (!hover || hover.mini) return;',
    'status banner chrome survives the fade-out':
        '"data-spectr-status-text": "true"',
    'minimap hover and drag cursors':
        'activeMini === "minimap-resize"',
    'minimap release restores grab cursor':
        'else if (p && p.mode === "minimap-resize")',
    'minimap press uses grabbing cursor':
        'mm === "left" || mm === "right"',
    'overflow popup exposes semantic trigger state':
        '"aria-expanded": overflowMenu',
    'edit popup exposes semantic trigger state':
        '"aria-expanded": editMenu',
    'analyzer popup exposes semantic trigger state':
        '"aria-expanded": analyzerMenu',
    'pattern popup exposes semantic trigger state':
        '"aria-expanded": patternMenu',
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
    for edit in EDITS:
        label, old, new = edit[:3]
        if old in new:
            sys.exit(f'FAIL {label}: patch point survives its own replacement')

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    post_checks = []
    # Remove the short-lived app-specific popup service. Popup keyboard and
    # dismissal behavior belongs to Pulp's generic materialized control layer;
    # Spectr contributes only semantic trigger/options markup.
    legacy_start = escaped(
        '  // Dynamic React effects are not a reliable lifecycle seam')
    legacy_end = escaped('  globalThis.__spectrPublishNativeMessage = emit;\n')
    if legacy_start in raw:
        start = raw.index(legacy_start)
        end = raw.index(legacy_end, start)
        raw = raw[:start] + raw[end:]
        changed = True
        print('removed         app-specific native rich-menu service')
    legacy_ref = escaped('  const restoreMenuFocus = useRefChrome(false);\n')
    if legacy_ref in raw:
        raw = raw.replace(legacy_ref, '', 1)
        changed = True
        print('removed         app-specific rich-menu focus state')
    legacy_effect_start = escaped(
        '  useEffectChrome(() => {\n'
        '    const key = openMenu || (helpOpen ? "help" : null);\n')
    legacy_effect_end = escaped('  }, [openMenu, helpOpen]);\n')
    if legacy_effect_start in raw:
        start = raw.index(legacy_effect_start)
        end = raw.index(legacy_effect_end, start) + len(legacy_effect_end)
        raw = raw[:start] + raw[end:]
        changed = True
        print('removed         app-specific rich-menu event effect')
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


if __name__ == '__main__':
    main()
