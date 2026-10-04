#!/usr/bin/env python3
"""Adapt Spectr-editor-open-probe JSON to the WP-0 open scenario schema.

The native probe records one row for every ``--opens`` iteration.  Earlier
versions silently selected ``opens[0]`` which made a warm reopen regression
invisible.  Keep the schema flat (``ui_bench`` requires numeric values) while
reporting cold and warm rows separately and retaining the worst values as
headline metrics.
"""
import argparse, json, pathlib

ap = argparse.ArgumentParser()
ap.add_argument("json_path", type=pathlib.Path)
ap.add_argument("--open-index", type=int, default=0,
                 help="row to expose as open_ms/first_frame_ms (default: 0)")
ns = ap.parse_args()
doc = json.loads(ns.json_path.read_text())
opens = doc.get("opens")
if not isinstance(opens, list) or not opens:
    raise SystemExit("editor-open adapter: JSON has no opens[]")
if ns.open_index < 0 or ns.open_index >= len(opens):
    raise SystemExit(f"editor-open adapter: --open-index {ns.open_index} outside opens[]")

def number(row, key, index, *, required=True):
    value = row.get(key)
    if required and (isinstance(value, bool) or not isinstance(value, (int, float))):
        raise SystemExit(f"editor-open adapter: opens[{index}] lacks numeric {key}")
    return float(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else 0.0

for i, row in enumerate(opens):
    if not isinstance(row, dict):
        raise SystemExit(f"editor-open adapter: opens[{i}] is not an object")
    number(row, "factory_ms", i)
    number(row, "first_present_ms", i)

row = opens[ns.open_index]
warm = opens[1:]
result = {
    "open_ms": number(row, "factory_ms", ns.open_index),
    "first_frame_ms": number(row, "first_present_ms", ns.open_index),
    "presents": number(row, "presents", ns.open_index, required=False),
    "max_stall_ms": number(row, "max_stall_ms", ns.open_index, required=False),
    "open_count": float(len(opens)),
    "warm_open_ms_max": max((number(r, "factory_ms", i + 1) for i, r in enumerate(warm)), default=0.0),
    "warm_first_frame_ms_max": max((number(r, "first_present_ms", i + 1) for i, r in enumerate(warm)), default=0.0),
}
print(json.dumps(result, sort_keys=True))
