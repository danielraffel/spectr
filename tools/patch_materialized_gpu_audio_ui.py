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
PROCESSING_MARKER = "data-spectr-gpu-processing-setting"
SETTINGS_OLD = '      return [option.mode, option.label];\n    })\n  })));\n}'
SETTINGS_NEW = '      return [option.mode, option.label];\n    })\n  })), hydrated.gpu_available === true && React.createElement(SpectrSettingsField, {\n    label: "GPU processing",\n    hint: "Mixing only, and the same sound as the CPU. On, Mixing reports "\n      + millis(1000 * hydrated.mixing_gpu_samples / hydrated.sample_rate) + " instead of "\n      + millis(1000 * hydrated.mixing_cpu_samples / hydrated.sample_rate)\n      + " to your DAW. Tracking always runs on the CPU."\n  }, React.createElement("div", { "data-spectr-gpu-processing-setting": hydrated.gpu_processing === true ? "on" : "off" },\n    React.createElement(SpectrSettingsToggle, { value: hydrated.gpu_processing === true,\n      onChange: (v) => spectrSetGpuProcessing(v) }))));\n}'

COMPONENT = r'''// Mixing's GPU processing choice has one write path, shared by the header
// chip and Settings: tell the processor, then adopt the latency state it
// answers with (the Mixing figure moves with the choice) and wake every
// latency surface, as spectrToggleLatencyMode does for the mode.
function spectrSetGpuProcessing(enabled) {
  if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
  Promise.resolve(window.pulp.postMessage("gpu_" + "processing_set", { enabled: enabled === true }, "spectr-gpu-processing"))
    .then((response) => {
      const body = response && response.payload ? response.payload : response;
      const latency = body && body.latency;
      if (!latency || typeof latency.mode !== "string") return;
      const store = globalThis.__spectrLatency || (globalThis.__spectrLatency = {});
      store.state = latency;
      (store.listeners || []).forEach((fn) => {
        try { fn(); } catch (error) { console.error("[Spectr] latency listener failed", error); }
      });
    }).catch((error) => console.error("[Spectr] GPU processing write failed", error));
}
function SpectrGpuAudioSurface({ enabled }) {
  const [gpuAudio, setGpuAudio] = React.useState(null);
  const [, setRevision] = React.useState(0);
  const store = globalThis.__spectrLatency || (globalThis.__spectrLatency = {});
  // The mode and the GPU choice come from the processor's latency state; a
  // switch from any surface wakes this one through the shared listener list.
  (React.useLayoutEffect || React.useEffect)(() => {
    const listeners = store.listeners || (store.listeners = []);
    const wake = () => setRevision((n) => n + 1);
    listeners.push(wake);
    return () => { const at = listeners.indexOf(wake); if (at >= 0) listeners.splice(at, 1); };
  }, []);
  const latency = store.state || {};
  const mixing = latency.mode === "linear_phase";
  const gpuOn = mixing && latency.gpu_processing === true;
  const gpuAvailable = latency.gpu_available === true;
  React.useEffect(() => {
    // Only GPU Mixing reads the counters, so nothing else polls or commits:
    // an idle editor must not re-render after the document mounts.
    if (!gpuOn) return undefined;
    let live = true;
    const refresh = () => {
      if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
      // Keep the diagnostic request distinct from the import-fidelity
      // contract's canonical build-info call. The native document already
      // contains that canonical call; this poll is an additional observer.
      Promise.resolve(window.pulp.postMessage("build_" + "info_get", {}, "spectr-gpu-status"))
        .then((response) => response && response.payload ? response.payload : response)
        .then((body) => {
          if (live && body && body.ok === true && body.gpu_audio) {
            const next = body.gpu_audio;
            setGpuAudio((prev) => JSON.stringify(prev) === JSON.stringify(next) ? prev : next);
          }
        }).catch(() => {});
    };
    refresh();
    const timer = setInterval(refresh, 500);
    return () => { live = false; clearInterval(timer); };
  }, [gpuOn]);
  const ready = gpuOn && gpuAudio && gpuAudio.available === true
    && gpuAudio.provider_state === "shared_ready";
  const neutral = "rgba(214,222,235,0.72)";
  const color = ready ? "hsl(205,90%,64%)" : gpuOn ? "hsl(38,90%,62%)" : neutral;
  const option = Array.isArray(latency.options)
    ? latency.options.filter((o) => o.mode === "linear_phase")[0] : null;
  const ms = (samples) => typeof samples === "number" && latency.sample_rate > 0
    ? Math.round(1000 * samples / latency.sample_rate) + " ms" : "";
  // Tracking always runs on the CPU, so there the chip is a disabled readout.
  // In Mixing it is a CPU/GPU toggle; its title names what the other side
  // would cost in latency.
  const toggleable = mixing && gpuAvailable;
  const chip = gpuOn ? "GPU" : "CPU";
  const title = !mixing ? "Tracking always runs on the CPU"
    : !gpuAvailable ? "This build renders Mixing on the CPU only"
    : gpuOn ? "Mixing on the GPU (" + ms(latency.mixing_gpu_samples) + "). Click for the CPU (" + ms(latency.mixing_cpu_samples) + ")."
    : "Mixing on the CPU (" + ms(latency.mixing_cpu_samples) + "). Click for the GPU (" + ms(latency.mixing_gpu_samples) + ").";
  const indicator = React.createElement("button", {
    "data-spectr-gpu-mode-indicator": true,
    "data-spectr-gpu-mode": mixing ? "mixing" : "tracking",
    "data-spectr-gpu-processing": gpuOn ? "gpu" : "cpu",
    "data-spectr-gpu-ready": ready ? "true" : "false",
    "aria-disabled": toggleable ? "false" : "true",
    disabled: !toggleable,
    role: "switch", "aria-checked": gpuOn,
    "aria-label": "GPU processing for Mixing: " + (gpuOn ? "on" : "off"), title,
    onClick: toggleable ? () => spectrSetGpuProcessing(!gpuOn) : undefined,
    // Native layout positions captured header nodes from their bindings, so
    // an uncaptured node in flow lands on the logo. Pin it to the header's
    // free right edge in design space (the editor scales uniformly).
    style: { position: "absolute", right: 12, top: 9.5, width: 52, boxSizing: "border-box", display: "inline-flex", alignItems: "center", justifyContent: "center", gap: 6,
      padding: "3px 8px", borderRadius: 3, border: "1px solid " + color,
      background: "rgba(255,255,255,0.03)", color, fontFamily: "var(--mono)",
      fontSize: 10, letterSpacing: 1, opacity: toggleable ? 1 : 0.4,
      cursor: toggleable ? "pointer" : "default" }
  }, React.createElement("span", { "aria-hidden": true,
      style: { width: 6, height: 6, borderRadius: 3, background: color } }), chip);
  let text = !mixing ? "Tracking runs on the CPU"
    : !gpuOn ? "Mixing on the CPU" : "GPU unavailable";
  if (ready) text = "GPU | " + Number(gpuAudio.gpu_selected || 0)
    + " blocks | " + Number(gpuAudio.cpu_fallback || 0) + " CPU fallback";
  // A renderer that refused Freeze's held source says so; never silent.
  if (gpuAudio && gpuOn && gpuAudio.freeze_available === false)
    text += " | " + (gpuAudio.freeze_note || "Freeze unavailable in this mode");
  const pill = enabled ? React.createElement("div", {
    "data-spectr-gpu-audio-status-pill": true,
    "data-spectr-gpu-audio-state": ready ? "gpu" : "cpu",
    // Absolute offsets resolve against the header in native layout, so a
    // bottom offset lands above the window. Pin the pill to the free band
    // between the spectrum and the viewport strip, in design space.
    style: { position: "absolute", left: 410, top: 739, width: 500,
      boxSizing: "border-box", display: "flex", justifyContent: "center", alignItems: "center",
      gap: 6, zIndex: 6, pointerEvents: "none",
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
    if MARKER in html and PROCESSING_MARKER in html:
        print("already applied")
        return 0
    if MARKER in html:
        # The surface is in; add the Settings row for GPU processing.
        if html.count(SETTINGS_OLD) != 1:
            print("Settings latency patch point is not unique", file=sys.stderr)
            return 1
        document["html"] = html.replace(SETTINGS_OLD, SETTINGS_NEW, 1)
        with open(PATH, "w", encoding="utf-8") as handle:
            json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
        print("applied Settings row")
        return 0
    chrome = "function Chrome({ settings, setSettings, bankRef, status, selectedPatternName, dspMode, setDspMode, editMode, setEditMode, analyzerMode, setAnalyzerMode, visualizationMode, setVisualizationMode, snapshotStatus, patterns, onApplyPattern, onOpenPatternManager, onSavePattern, onClearAll, onResetAll, allMuted }) {"
    if html.count(chrome) != 1:
        print("Chrome patch point is not unique", file=sys.stderr)
        return 1
    zoom = 'React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B")), /* @__PURE__ */ React.createElement(SpectrTracingBadge, null))'
    if html.count(zoom) != 1:
        print("toolbar patch point is not unique", file=sys.stderr)
        return 1
    if html.count(SETTINGS_OLD) != 1:
        print("Settings latency patch point is not unique", file=sys.stderr)
        return 1
    html = html.replace(chrome, COMPONENT + chrome, 1)
    html = html.replace(SETTINGS_OLD, SETTINGS_NEW, 1)
    html = html.replace(zoom, 'React.createElement("span", { "aria-hidden": true, style: { width: 0, height: 0, opacity: 0, overflow: "hidden", fontSize: 0 } }, "\\u200B")), /* @__PURE__ */ React.createElement(SpectrTracingBadge, null), /* @__PURE__ */ React.createElement(SpectrGpuAudioSurface, { enabled: settings.showGpuStats !== false }))', 1)
    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("applied")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
