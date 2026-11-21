#!/usr/bin/env python3
"""Add the independent Tracking GPU opt-in to the shipping native document.

The native document is a checked-in artifact. This patch keeps the Tracking
control beside the existing latency and Mixing GPU controls, reads the native
projection's measured CPU/GPU PDC figures, and reports unsupported-platform and
provider-refused states without treating the saved selection as proof of wet
GPU delivery.
"""
from __future__ import annotations

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "native-ui", "materialized",
                    "materialized-document.runtime.json")
MARKER = "data-spectr-tracking-gpu-toggle"
REQUIRED = "function SpectrLatencySettings()"

COMPONENT = r'''function spectrRefreshTrackingGpu() {
  if (!window.pulp || typeof window.pulp.postMessage !== "function")
    return Promise.resolve();
  const store = globalThis.__spectrLatency || (globalThis.__spectrLatency = {});
  return Promise.resolve(window.pulp.postMessage("build_info_get", {},
    "spectr-tracking-gpu-refresh"))
    .then((response) => response && response.payload ? response.payload : response)
    .then((body) => {
      const tracking = body && body.tracking_gpu;
      if (!tracking || !store.state) return;
      store.state = { ...store.state, tracking_gpu: tracking };
      (store.listeners || []).slice().forEach((fn) => {
        try { fn(); } catch (error) {
          console.error("[Spectr] Tracking GPU latency listener failed", error);
        }
      });
    });
}
function spectrSetTrackingGpuProcessing(enabled) {
  if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
  Promise.resolve(window.pulp.postMessage("tracking_gpu_processing_set",
    { enabled: enabled === true }, "spectr-tracking-gpu"))
    .then((response) => {
      const body = response && response.payload ? response.payload : response;
      const latency = body && body.latency;
      if (!latency || typeof latency.mode !== "string") {
        return spectrRefreshTrackingGpu();
      }
      const store = globalThis.__spectrLatency || (globalThis.__spectrLatency = {});
      store.state = latency;
      (store.listeners || []).slice().forEach((fn) => {
        try { fn(); } catch (error) {
          console.error("[Spectr] Tracking GPU latency listener failed", error);
        }
      });
    })
    .catch((error) => {
      console.error("[Spectr] Tracking GPU write failed", error);
      spectrRefreshTrackingGpu().catch((refreshError) =>
        console.error("[Spectr] Tracking GPU refusal refresh failed", refreshError));
    });
}
function SpectrTrackingGpuSettings() {
  const store = globalThis.__spectrLatency
    || (globalThis.__spectrLatency = {});
  const [, setRevision] = React.useState(0);
  (React.useLayoutEffect || React.useEffect)(() => {
    const listeners = store.listeners || (store.listeners = []);
    const wake = () => setRevision((n) => n + 1);
    listeners.push(wake);
    return () => {
      const at = listeners.indexOf(wake);
      if (at >= 0) listeners.splice(at, 1);
    };
  }, []);
  const hydrated = store.state;
  const tracking = hydrated && hydrated.tracking_gpu;
  if (!tracking) return null;
  const selected = tracking.processing === true;
  const built = tracking.built === true;
  const unavailable = tracking.eligibility === "unsupported_platform";
  const refused = tracking.eligibility === "provider_refused";
  const active = tracking.eligibility === "active";
  const ready = tracking.eligibility === "ready";
  const provider = tracking.provider_state
    || (tracking.actual && tracking.actual.reason) || "unprobed";
  const status = spectrTrackingGpuDisplay(tracking).text;
  const color = unavailable || refused ? "hsl(38,90%,62%)"
    : active ? "hsl(205,90%,64%)" : "rgba(255,255,255,0.62)";
  const pdc = (samples) => Number.isFinite(Number(samples))
    ? String(Math.round(Number(samples))) + " samples" : "—";
  const request = (next) => {
    if (unavailable && next) return;
    spectrSetTrackingGpuProcessing(next);
  };
  return React.createElement(SpectrSettingsGroup, {
    marker: "tracking-gpu", title: "TRACKING GPU",
    subtitle: "Opt-in backend for Tracking. Delivery remains provider-dependent."
  }, React.createElement(SpectrSettingsField, {
    label: "Processing",
    hint: "CPU " + pdc(tracking.cpu_samples) + " · GPU " + pdc(tracking.gpu_samples)
  }, React.createElement("div", {
    "data-spectr-tracking-gpu-toggle": true,
    "data-spectr-tracking-gpu-state": selected ? "on" : "off"
  }, React.createElement(SpectrSettingsToggle, {
    value: selected, onChange: request, "aria-label": "Use Tracking GPU"
  }))), React.createElement("div", {
    "data-spectr-tracking-gpu-status": tracking.eligibility,
    style: { display: "flex", justifyContent: "space-between", gap: 12,
      padding: "8px 10px", borderRadius: 3,
      border: "1px solid rgba(255,255,255,0.1)",
      background: "rgba(255,255,255,0.025)", fontFamily: "var(--sans)",
      fontSize: 10, color }
  }, React.createElement("span", null, status), React.createElement("span", {
    style: { opacity: 0.7 }
  }, built ? "provider: " + provider : "build: unsupported")));
}
'''


def replace_once(html: str, before: str, after: str) -> str:
    if html.count(before) != 1:
        raise ValueError(f"expected a unique materialized anchor: {before[:82]}")
    return html.replace(before, after, 1)


def apply_document(html: str) -> str:
    with open(os.path.join(ROOT, 'resources', 'editor.html'), encoding='utf-8') as src:
        helper_match = re.search(r'function spectrTrackingGpuDisplay\(tracking\) \{.*?^\}',
                                 src.read(), re.MULTILINE | re.DOTALL)
    if not helper_match:
        raise ValueError('browser Tracking status formatter missing')
    helper = helper_match.group(0) + '\n'
    start = 'function spectrRefreshTrackingGpu()'
    if start in html:
        # Migrate the original insertion, including the published artifact
        # that accidentally carried the same Settings component twice.
        before, section = html.split(start, 1)
        old, after = section.split(REQUIRED, 1)
        if old.count('function SpectrTrackingGpuSettings()') not in (1, 2):
            raise ValueError('unexpected Tracking settings section')
        html = before + REQUIRED + after
    html = replace_once(html, REQUIRED, COMPONENT + REQUIRED)
    mount = 'React.createElement(SpectrTrackingGpuSettings, null),'
    if mount not in html:
        anchor = 'React.createElement(SpectrLatencySettings, null),'
        html = replace_once(html, anchor, anchor + ' ' + mount)
    if html.count(mount) != 1:
        raise ValueError('expected exactly one Tracking settings mount')

    anchor = 'function SpectrGpuAudioSurface({ enabled }) {'
    if 'function spectrTrackingGpuDisplay(tracking)' in html:
        html = re.sub(r'function spectrTrackingGpuDisplay\(tracking\) \{.*?^\}\n', '',
                      html, count=1, flags=re.MULTILINE | re.DOTALL)
    html = replace_once(html, anchor, helper + anchor)
    if 'const trackingView = spectrTrackingGpuDisplay' in html:
        return html
    # Preserve the Mixing control and its own write path. Tracking's chip
    # remains an informational readout pointing to its independent setting.
    for before, after in [
        ('  const gpuAvailable = latency.gpu_available === true;', '''  const gpuAvailable = latency.gpu_available === true;
  const trackingView = spectrTrackingGpuDisplay(latency.tracking_gpu);
  const trackingSelected = !mixing && latency.tracking_gpu && latency.tracking_gpu.processing === true;'''),
        ('''    // Only GPU Mixing reads the counters, so nothing else polls or commits:
    // an idle editor must not re-render after the document mounts.
    if (!gpuOn) return undefined;''', '''    if (!gpuOn && !trackingSelected) return undefined;'''),
        ('''            const next = body.gpu_audio;
            setGpuAudio''', '''            const current = store.state;
            if (trackingSelected && current && current.mode === "zero_latency" && body.tracking_gpu &&
                current.tracking_gpu.processing === body.tracking_gpu.processing &&
                JSON.stringify(current.tracking_gpu) !== JSON.stringify(body.tracking_gpu)) {
              store.state = { ...current, tracking_gpu: body.tracking_gpu };
              (store.listeners || []).slice().forEach((fn) => fn());
            }
            const next = body.gpu_audio;
            setGpuAudio'''),
        ('  }, [gpuOn]);','  }, [gpuOn, trackingSelected]);'),
        ('  // Tracking always runs on the CPU, so there the chip is a disabled readout.',
         '  // Tracking displays the processor observation and opens a settings hint.'),
        ('  const chip = gpuOn ? "GPU" : "CPU";',
         '  const chip = mixing ? (gpuOn ? "GPU" : "CPU") : trackingView.chip;'),
        (r'!mixing ? "Tracking always uses the CPU for lowest latency \u2014 switch to Mixing for GPU"',
         '!mixing ? trackingView.text + " · Tracking GPU in Settings"'),
        ('"data-spectr-gpu-processing": gpuOn ? "gpu" : "cpu"',
         '"data-spectr-gpu-processing": mixing ? (gpuOn ? "gpu" : "cpu") : trackingView.state'),
        ('"data-spectr-gpu-ready": ready ? "true" : "false"',
         '"data-spectr-gpu-ready": (mixing ? ready : ["ready", "delivered"].includes(trackingView.state)) ? "true" : "false"'),
        ('"aria-disabled": toggleable ? "false" : "true"',
         '"aria-disabled": !mixing || toggleable ? "false" : "true"'),
        ('disabled: !toggleable && !(gpuAvailable && !mixing)', 'disabled: mixing && !toggleable'),
        ('role: "switch", "aria-checked": gpuOn,',
         'role: mixing ? "switch" : "button", "aria-checked": mixing ? gpuOn : undefined,'),
        ('"aria-label": "GPU processing for Mixing: " + (gpuOn ? "on" : "off")',
         '"aria-label": mixing ? "GPU processing for Mixing: " + (gpuOn ? "on" : "off") : trackingView.text'),
        ('(gpuAvailable && !mixing ? () => setTrackingNotice', '(!mixing ? () => setTrackingNotice'),
        ('right: 12, top: 9.5, width: 52,', 'right: 12, top: 9.5, width: mixing ? 52 : 76,'),
        ('opacity: toggleable ? 1 : 0.4,\n      cursor: toggleable ? "pointer" : "default"',
         'opacity: !mixing || toggleable ? 1 : 0.4,\n      cursor: !mixing || toggleable ? "pointer" : "default"'),
        ('let text = !mixing ? "Tracking runs on the CPU"', 'let text = !mixing ? trackingView.text'),
        ('"data-spectr-gpu-audio-state": ready ? "gpu" : "cpu"',
         '"data-spectr-gpu-audio-state": mixing ? (ready ? "gpu" : "cpu") : trackingView.state'),
        ('"GPU runs in Mixing only"', '"Tracking GPU in Settings"'),
        (' to your DAW. Tracking always runs on the CPU.', ' to your DAW. Tracking has its own GPU setting.'),
    ]:
        html = replace_once(html, before, after)
    return html


def main() -> int:
    with open(PATH, encoding='utf-8') as handle:
        document = json.load(handle)
    try:
        updated = apply_document(document['html'])
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1
    if updated != document['html']:
        document['html'] = updated
        with open(PATH, 'w', encoding='utf-8') as handle:
            json.dump(document, handle, ensure_ascii=False, separators=(',', ':'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
