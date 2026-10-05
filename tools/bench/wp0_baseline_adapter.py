#!/usr/bin/env python3
"""Fail-closed validation for a Spectr WP-0 native baseline receipt.

The native probe deliberately keeps its normal stdout contract.  This adapter
is the review gate for the opt-in JSON receipt: it rejects missing provenance,
empty counters, malformed rows, and a planted reachability control that does
not report an off-screen node.
"""

import argparse
import json
import math
import pathlib
import re


HEX40 = re.compile(r"^[0-9a-f]{40}$")
REQUIRED_ROW_KEYS = {
    "host_width", "host_height", "root_width", "root_height", "resize_ms",
    "layout_ms", "paint_ms", "resize_bridge_calls", "rgba_bytes",
    "rendered_width", "rendered_height", "rss_bytes", "png_bytes",
    "layout_bytes",
}


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"wp0 baseline adapter: {message}")


def number(value, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"{label} is not numeric")
    if not math.isfinite(value):
        fail(f"{label} is not finite")
    if value < 0:
        fail(f"{label} is negative")
    return float(value)


parser = argparse.ArgumentParser()
parser.add_argument("receipt", type=pathlib.Path)
parser.add_argument(
    "--negative-log", type=pathlib.Path,
    help="stdout/stderr captured from a planted SPECTR_PLANT_OFFSCREEN run",
)
parser.add_argument(
    "--require-negative", action="store_true",
    help="require the receipt and log to prove a planted control was rejected",
)
args = parser.parse_args()

try:
    document = json.loads(args.receipt.read_text())
except (OSError, json.JSONDecodeError) as error:
    fail(f"cannot read receipt: {error}")
if not isinstance(document, dict):
    fail("receipt root is not an object")

if document.get("schema") != "spectr-wp0-runtime-baseline-v1":
    fail("unexpected schema")
if document.get("mode") != "counter-enabled-native-shot":
    fail("unexpected mode")
if document.get("layout_mode") != "forced_full_tree":
    fail("layout timing mode is not declared as forced_full_tree")
if document.get("paint_measurement") != "raw_rgba_skia_including_layout":
    fail("paint timing source is not declared")
if document.get("resize_measurement") != "host_resize_plus_24_synthetic_frames":
    fail("resize timing scope is not declared")
if not isinstance(document.get("capture_backend"), str) or not document["capture_backend"]:
    fail("capture backend is missing")
if number(document.get("capture_scale"), "capture_scale") <= 0:
    fail("capture scale is zero")
if document.get("bridge_counter_scope") != "registered_native_api_only":
    fail("bridge counter scope is not declared")
if document.get("bridge_counter_available") is not True:
    fail("bridge counter is unavailable; refusing a non-authoritative baseline")
if document.get("pulp_sdk_provenance_exact") is not True:
    fail("SDK provenance is not exact")
if document.get("product_source_dirty") is not False:
    fail("product source was dirty; refusing a non-reproducible baseline")
if document.get("resize_control_reached") is not True:
    fail("resize reachability control did not pass")
for key in ("product_source_sha", "pulp_sdk_source_sha"):
    if not isinstance(document.get(key), str) or not HEX40.fullmatch(document[key]):
        fail(f"{key} is not an exact 40-character SHA")
if number(document.get("mount_bridge_calls"), "mount_bridge_calls") <= 0:
    fail("mount_bridge_calls is zero")
if number(document.get("max_rss_bytes"), "max_rss_bytes") <= 0:
    fail("max_rss_bytes is zero")
if document.get("rss_supported") is not True:
    fail("RSS measurement is unavailable")
if number(document.get("executable_bytes"), "executable_bytes") <= 0:
    fail("executable_bytes is zero")

if args.require_negative and args.negative_log is None:
    fail("--require-negative needs --negative-log")
if args.negative_log is not None or args.require_negative:
    if document.get("negative_control_requested") is not True:
        fail("receipt does not record a requested negative control")
    if document.get("negative_control_rejected") is not True:
        fail("receipt did not reject its planted negative control")

rows = document.get("rows")
if not isinstance(rows, list) or not rows:
    fail("rows is empty")
expected_hosts = {(990.0, 645.0), (1100.0, 700.0), (1320.0, 860.0), (1600.0, 1000.0)}
seen_hosts = set()
if len(rows) != len(expected_hosts):
    fail(f"expected {len(expected_hosts)} host rows, got {len(rows)}")
for index, row in enumerate(rows):
    if not isinstance(row, dict):
        fail(f"rows[{index}] is not an object")
    missing = REQUIRED_ROW_KEYS - row.keys()
    if missing:
        fail(f"rows[{index}] missing {sorted(missing)}")
    for key in REQUIRED_ROW_KEYS:
        number(row[key], f"rows[{index}].{key}")
    host = (number(row["host_width"], f"rows[{index}].host_width"),
            number(row["host_height"], f"rows[{index}].host_height"))
    seen_hosts.add(host)
    if host not in expected_hosts:
        fail(f"rows[{index}] has unexpected host size {host}")
    if row["root_width"] != 1320.0 or row["root_height"] != 860.0:
        fail(f"rows[{index}] root is not the authored 1320x860 viewport")
    if number(row["root_width"], f"rows[{index}].root_width") <= 0:
        fail(f"rows[{index}] has zero root width")
    if number(row["root_height"], f"rows[{index}].root_height") <= 0:
        fail(f"rows[{index}] has zero root height")
    rendered_width = number(row["rendered_width"], f"rows[{index}].rendered_width")
    rendered_height = number(row["rendered_height"], f"rows[{index}].rendered_height")
    expected_width = int(1320 * document["capture_scale"])
    expected_height = int(860 * document["capture_scale"])
    if rendered_width != expected_width or rendered_height != expected_height:
        fail(f"rows[{index}] rendered dimensions do not match the authored viewport at capture scale")
    if number(row["rgba_bytes"], f"rows[{index}].rgba_bytes") <= 0:
        fail(f"rows[{index}] has empty RGBA output")
    if row["rgba_bytes"] != int(rendered_width * rendered_height * 4):
        fail(f"rows[{index}] RGBA size does not match rendered dimensions")
    if number(row["png_bytes"], f"rows[{index}].png_bytes") <= 0:
        fail(f"rows[{index}] has empty PNG output")
    if number(row["layout_bytes"], f"rows[{index}].layout_bytes") <= 0:
        fail(f"rows[{index}] has empty layout output")
    stem = f"wp0-{int(host[0])}x{int(host[1])}"
    for suffix, key in ((".png", "png_bytes"), (".layout.json", "layout_bytes")):
        artifact = args.receipt.parent / (stem + suffix)
        try:
            actual_size = artifact.stat().st_size
        except OSError as error:
            fail(f"rows[{index}] artifact {artifact} is unavailable: {error}")
        if actual_size != row[key]:
            fail(f"rows[{index}] {artifact.name} size does not match the receipt")
if seen_hosts != expected_hosts:
    fail(f"host size set is incomplete: {sorted(seen_hosts)}")

if args.negative_log is not None:
    try:
        negative = args.negative_log.read_text()
    except OSError as error:
        fail(f"cannot read negative-control log: {error}")
    negative_id = document.get("negative_control_id")
    if not isinstance(negative_id, str) or not negative_id:
        fail("negative_control_id is missing")
    marker = f"[wp0] planted negative control id={negative_id} rejected=yes"
    if marker not in negative or f"[wp0] OFFSCREEN {negative_id}" not in negative:
        fail("planted negative control did not report a rejected result")

print(json.dumps({
    "schema": document["schema"],
    "rows": len(rows),
    "mount_bridge_calls": document["mount_bridge_calls"],
    "negative_control": "rejected" if args.negative_log else "not-run",
}, sort_keys=True))
