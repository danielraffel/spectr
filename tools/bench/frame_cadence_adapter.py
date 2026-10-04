#!/usr/bin/env python3
"""Adapt frame_cadence_probe.py --json-out output to WP-0 frame metrics."""
import argparse, json, pathlib

ap = argparse.ArgumentParser()
ap.add_argument("json_path", type=pathlib.Path)
ns = ap.parse_args()
doc = json.loads(ns.json_path.read_text())
summary = doc.get("summary")
if not isinstance(summary, dict) or "gesture_sustained_p95_ms" not in summary:
    raise SystemExit("frame-cadence adapter: missing summary.gesture_sustained_p95_ms")
print(json.dumps({
    "frame_ms": summary["gesture_sustained_p95_ms"],
    "frame_gap_p95_ms": summary["gesture_sustained_p95_ms"],
    "frame_gap_ratio": summary.get("sustained_p95_ratio"),
}))
