#!/usr/bin/env python3
"""Adapt Spectr-editor-open-probe JSON to the WP-0 open scenario schema."""
import argparse, json, pathlib

ap = argparse.ArgumentParser()
ap.add_argument("json_path", type=pathlib.Path)
ns = ap.parse_args()
doc = json.loads(ns.json_path.read_text())
opens = doc.get("opens")
if not isinstance(opens, list) or not opens:
    raise SystemExit("editor-open adapter: JSON has no opens[]")
row = opens[0]
if "factory_ms" not in row or "first_present_ms" not in row:
    raise SystemExit("editor-open adapter: opens[0] lacks factory_ms/first_present_ms")
print(json.dumps({"open_ms": row["factory_ms"],
                  "first_frame_ms": row["first_present_ms"],
                  "presents": row.get("presents", 0),
                  "max_stall_ms": row.get("max_stall_ms", 0)}))
