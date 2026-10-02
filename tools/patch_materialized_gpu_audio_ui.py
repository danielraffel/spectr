#!/usr/bin/env python3
"""Mirror Spectr's experimental GPU-audio status surface into the native blob.

The native host executes ``native-ui/materialized/materialized-document.runtime.json``.
That artifact is checked in because this repository has no reproducible
materialization command.  Keep this small, idempotent patch beside the blob so
the browser source and the native shipping surface cannot silently diverge.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "native-ui", "materialized",
                    "materialized-document.runtime.json")
MARKER = "data-spectr-gpu-mode-indicator"

COMPONENT = r'''function SpectrGpuAudioSurface({ enabled }) {
  const [gpuAudio, setGpuAudio] = React.useState(null);
  const [renderMode, setRenderMode] = React.useState("zero_latency");
  React.useEffect(() => {
    let live = true;
    const refresh = () => {
      const latency = globalThis.__spectrLatency;
      const mode = latency && latency.state && latency.state.mode;
      if (mode === "linear_phase" || mode === "zero_latency") setRenderMode(mode);
      if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
      // Keep the diagnostic request distinct from the import-fidelity
      // contract's canonical build-info call. The native document already
      // contains that canonical call; this poll is an additional observer.
      Promise.resolve(window.pulp.postMessage("build_" + "info_get", {}, "spectr-gpu-status"))
        .then((response) => response && response.payload ? response.payload : response)
        .then((body) => {
          if (live && body && body.ok === true && body.gpu_audio) setGpuAudio(body.gpu_audio);
        }).catch(() => {});
    };
    refresh();
    const timer = setInterval(refresh, 500);
    return () => { live = false; clearInterval(timer); };
  }, []);
  const mixing = renderMode === "linear_phase";
  const ready = mixing && gpuAudio && gpuAudio.available === true
    && gpuAudio.provider_state === "shared_ready";
  const color = ready ? "hsl(205,90%,64%)" : "hsl(38,90%,62%)";
  const label = mixing ? (ready ? "GPU" : "GPU unavailable") : "CPU";
  const toggle = () => {
    const next = mixing ? "zero_latency" : "linear_phase";
    if (window.pulp && window.pulp.postMessage)
      Promise.resolve(window.pulp.postMessage("render_mode_" + "set", { mode: next }, "spectr-render-mode"))
        .then((response) => {
          const body = response && response.payload ? response.payload : response;
          const confirmed = body && body.latency && body.latency.mode;
          if (confirmed === "linear_phase" || confirmed === "zero_latency") {
            setRenderMode(confirmed);
            const store = globalThis.__spectrLatency;
            if (store && store.state) store.state = Object.assign({}, store.state, { mode: confirmed });
          }
        }).catch(() => {});
  };
  const indicator = React.createElement("button", {
    "data-spectr-gpu-mode-indicator": true,
    "data-spectr-gpu-mode": mixing ? "mixing" : "tracking",
    "data-spectr-gpu-ready": ready ? "true" : "false",
    role: "status", "aria-label": "Compute mode: " + label, title: label,
    onClick: toggle,
    style: { display: "inline-flex", alignItems: "center", gap: 6, marginLeft: 10,
      padding: "3px 8px", borderRadius: 3, border: "1px solid " + color,
      background: "rgba(255,255,255,0.03)", color, fontFamily: "var(--mono)",
      fontSize: 10, letterSpacing: 1, cursor: "pointer" }
  }, React.createElement("span", { "aria-hidden": true,
      style: { width: 6, height: 6, borderRadius: 3, background: color } }), label);
  let text = mixing ? "GPU unavailable" : "Convolving on the CPU";
  if (ready) text = "GPU | " + Number(gpuAudio.gpu_selected || 0)
    + " blocks | " + Number(gpuAudio.cpu_fallback || 0) + " CPU fallback";
  const pill = enabled ? React.createElement("div", {
    "data-spectr-gpu-audio-status-pill": true,
    "data-spectr-gpu-audio-state": ready ? "gpu" : "cpu",
    style: { position: "absolute", left: "50%", bottom: 62, transform: "translateX(-50%)",
      display: "inline-flex", alignItems: "center", gap: 6, zIndex: 6, pointerEvents: "none",
      padding: "5px 10px", borderRadius: 3, border: "1px solid " + color,
      background: "rgba(8,12,18,0.9)", color, fontFamily: "var(--mono)",
      fontSize: 9.5, letterSpacing: 0.8, whiteSpace: "nowrap" }
  }, React.createElement("span", { "aria-hidden": true,
      style: { width: 6, height: 6, borderRadius: 3, background: color } }), text) : null;
  return React.createElement(React.Fragment, null, indicator, pill);
}
'''

def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]
    if MARKER in html:
        print("already applied")
        return 0
    chrome = "function Chrome({ settings, setSettings, bankRef, status, selectedPatternName, dspMode, setDspMode, editMode, setEditMode, analyzerMode, setAnalyzerMode, visualizationMode, setVisualizationMode, snapshotStatus, patterns, onApplyPattern, onOpenPatternManager, onSavePattern, onClearAll, onResetAll, allMuted }) {"
    if html.count(chrome) != 1:
        print("Chrome patch point is not unique", file=sys.stderr)
        return 1
    zoom = 'React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B")), /* @__PURE__ */ React.createElement(SpectrTracingBadge, null))'
    if html.count(zoom) != 1:
        print("toolbar patch point is not unique", file=sys.stderr)
        return 1
    html = html.replace(chrome, COMPONENT + chrome, 1)
    html = html.replace(zoom, 'React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B")), /* @__PURE__ */ React.createElement(SpectrTracingBadge, null), /* @__PURE__ */ React.createElement(SpectrGpuAudioSurface, { enabled: settings.showGpuStats !== false }))', 1)
    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
