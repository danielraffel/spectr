#!/usr/bin/env python3
"""GPU status polish for the native editor document.

- The GPU stats overlay (the "GPU | N blocks | N CPU fallback" pill over the
  spectrum, and the counters in Build info) is diagnostics: it is off unless
  the user turns on Settings > GPU stats. A saved `showGpuStats: true` keeps
  it on; a new instance, or saved state without the field, starts with it off.
- In Tracking the CPU chip's notice is one short line, "GPU runs in Mixing
  only", in a box that sizes to its text; the hover tooltip carries the reason.
  The old two-line notice sat in a fixed 276 px box and broke after the dash,
  with the dash painted past the box's padding.

Idempotent: applies on top of patch_materialized_gpu_tracking_notice.py, and a
second run reports "already applied".
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "native-ui", "materialized",
                    "materialized-document.runtime.json")
REQUIRED = "data-spectr-gpu-tracking-notice"
MARKER = "data-spectr-gpu-tracking-notice-sized"
OLD_NOTICE = ("GPU processing is available in Mixing only \\u2014 Tracking runs on "
              "the CPU for lowest latency")
NOTICE = "GPU runs in Mixing only"
TITLE = "Tracking always uses the CPU for lowest latency \\u2014 switch to Mixing for GPU"

# (old, new, expected count)
EDITS = [
    ('"showGpuStats": true', '"showGpuStats": false', 1),
    ('settings.showGpuStats !== false', 'settings.showGpuStats === true', 3),
    ('function SpectrBuildInfo({ showGpuStats = true }) {',
     'function SpectrBuildInfo({ showGpuStats = false }) {', 1),
    ('!mixing ? "' + OLD_NOTICE + '"', '!mixing ? "' + TITLE + '"', 1),
    ('"' + REQUIRED + '": true, role: "status",',
     '"' + REQUIRED + '": true, "' + MARKER + '": true, role: "status",', 1),
    ('width: 276, boxSizing: "border-box",', 'boxSizing: "border-box",', 1),
    ('whiteSpace: "normal", textAlign: "left" }', 'whiteSpace: "nowrap", textAlign: "left" }', 1),
    ('}, "' + OLD_NOTICE + '") : null;', '}, "' + NOTICE + '") : null;', 1),
]


def main():
    with open(PATH, encoding="utf-8") as handle:
        document = json.load(handle)
    html = document["html"]
    if MARKER in html:
        print("already applied")
        return 0
    if REQUIRED not in html:
        print("the Tracking notice is not in the document; run "
              "patch_materialized_gpu_tracking_notice.py first", file=sys.stderr)
        return 1
    for old, new, count in EDITS:
        found = html.count(old)
        if found != count:
            print(f"patch point found {found} times, expected {count}: {old[:70]}",
                  file=sys.stderr)
            return 1
        html = html.replace(old, new)
    document["html"] = html
    with open(PATH, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, separators=(",", ":"))
    print("applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
