#!/usr/bin/env python3
"""Say why the CPU chip does nothing in Tracking, instead of ignoring the tap.

Tracking always runs on the CPU (its minimum-phase renderer has no GPU path),
so in Tracking the header's CPU/GPU chip cannot switch anything. It used to be
a disabled readout: a tap did nothing and said nothing, which reads as a
broken control. In a build with GPU processing it now answers a tap with a
short notice under the chip, and its hover tooltip says the same. The tap
still sends nothing to the processor: there is no write path from it.

Idempotent: applies on top of patch_materialized_gpu_audio_ui.py, and a second
run reports "already applied".
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "native-ui", "materialized",
                    "materialized-document.runtime.json")
SURFACE_MARKER = "data-spectr-gpu-mode-indicator"
MARKER = "data-spectr-gpu-tracking-notice"
NOTICE = ("GPU processing is available in Mixing only \\u2014 Tracking runs on "
          "the CPU for lowest latency")

EDITS = [
    # State for the notice, and its own timeout: it says its piece and goes.
    ('const [gpuAudio, setGpuAudio] = React.useState(null);',
     'const [gpuAudio, setGpuAudio] = React.useState(null);\n'
     '  const [trackingNotice, setTrackingNotice] = React.useState(0);\n'
     '  React.useEffect(() => {\n'
     '    if (!trackingNotice) return undefined;\n'
     '    const timer = setTimeout(() => setTrackingNotice(0), 3200);\n'
     '    return () => clearTimeout(timer);\n'
     '  }, [trackingNotice]);'),
    # The hover tooltip in Tracking.
    ('!mixing ? "Tracking always runs on the CPU"',
     '!mixing ? "' + NOTICE + '"'),
    # Pressable in Tracking when the build has a GPU path, so the tap can be
    # answered; still aria-disabled, since it switches nothing.
    ('disabled: !toggleable,',
     'disabled: !toggleable && !(gpuAvailable && !mixing),'),
    ('onClick: toggleable ? () => spectrSetGpuProcessing(!gpuOn) : undefined,',
     'onClick: toggleable ? () => spectrSetGpuProcessing(!gpuOn)\n'
     '      : (gpuAvailable && !mixing ? () => setTrackingNotice((n) => n + 1) : undefined),'),
    ('return React.createElement(React.Fragment, null, indicator, pill);',
     'const notice = trackingNotice && !mixing ? React.createElement("div", {\n'
     '    "' + MARKER + '": true, role: "status", "aria-live": "polite",\n'
     '    style: { position: "absolute", right: 12, top: 34, width: 276, boxSizing: "border-box",\n'
     '      zIndex: 8, pointerEvents: "none", padding: "6px 9px", borderRadius: 3,\n'
     '      border: "1px solid " + neutral, background: "rgba(8,12,18,0.94)", color: "rgba(214,222,235,0.92)",\n'
     '      fontFamily: "var(--mono)", fontSize: 9.5, lineHeight: 1.35, letterSpacing: 0.4,\n'
     '      whiteSpace: "normal", textAlign: "left" }\n'
     '  }, "' + NOTICE + '") : null;\n'
     '  return React.createElement(React.Fragment, null, indicator, pill, notice);'),
]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]
    if MARKER in html:
        print("already applied")
        return 0
    if SURFACE_MARKER not in html:
        print("the GPU audio surface is not in the document; run "
              "patch_materialized_gpu_audio_ui.py first", file=sys.stderr)
        return 1
    for old, new in EDITS:
        if html.count(old) != 1:
            print("patch point is not unique: " + old[:60], file=sys.stderr)
            return 1
        html = html.replace(old, new, 1)
    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
