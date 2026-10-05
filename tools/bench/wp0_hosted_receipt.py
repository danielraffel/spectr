#!/usr/bin/env python3
"""Validate and combine the hosted WP-0 UI and native receipts.

``ui_bench.py`` accepts a subset of scenarios for focused local probes. A
hosted baseline needs the complete workload set in one receipt: open, frame,
bridge, layout, paint, and artifact size. This adapter is the fail-closed
boundary for that distinction. It also requires three or more rows per metric
and a non-zero RSS measurement from the child process. The native receipt is
checked by the existing native adapter, preserving its dimension, byte-count,
provenance, and planted negative-control checks.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import subprocess
import sys
import re


SCENARIOS = {
    "open": ("open_ms", "first_frame_ms"),
    "frame": ("frame_ms",),
    "bridge": ("bridge_calls",),
    "layout": ("layout_ms",),
    "paint": ("paint_ms",),
    "size": ("size_bytes",),
}
NATIVE_ADAPTER = pathlib.Path(__file__).with_name("wp0_baseline_adapter.py")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
FORMATS = {"AU", "VST3", "CLAP", "Standalone"}


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"wp0 hosted receipt: {message}")


def finite(value, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"{label} is not numeric")
    if not math.isfinite(value):
        fail(f"{label} is not finite")
    if value < 0:
        fail(f"{label} is negative")
    return float(value)


def load(path: pathlib.Path, label: str):
    try:
        value = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read {label}: {error}")
    if not isinstance(value, dict):
        fail(f"{label} is not an object")
    return value


def validate_identity(document: dict, label: str) -> dict:
    identity = document.get("identity")
    if not isinstance(identity, dict):
        fail(f"{label} is missing identity")
    for key in ("host_id", "host_format", "build_id", "artifact_sha256"):
        if not isinstance(identity.get(key), str) or not identity[key]:
            fail(f"{label}.identity.{key} is missing")
    if identity["host_format"] not in FORMATS:
        fail(f"{label}.identity.host_format is unsupported")
    if not SHA256.fullmatch(identity["artifact_sha256"]):
        fail(f"{label}.identity.artifact_sha256 is not a SHA-256")
    artifact_bytes = identity.get("artifact_bytes")
    if (isinstance(artifact_bytes, bool) or
            not isinstance(artifact_bytes, int) or artifact_bytes <= 0):
        fail(f"{label}.identity.artifact_bytes is not positive")
    return identity


def validate_ui(path: pathlib.Path, minimum_runs: int) -> dict:
    document = load(path, "UI receipt")
    if document.get("schema") != "spectr-ui-bench-v1":
        fail("UI receipt has an unexpected schema")
    identity = validate_identity(document, "UI receipt")
    runs = document.get("runs")
    if isinstance(runs, bool) or not isinstance(runs, int) or runs < minimum_runs:
        fail(f"UI receipt needs at least {minimum_runs} runs")
    scenarios = document.get("scenarios")
    if not isinstance(scenarios, dict):
        fail("UI receipt has no scenarios object")
    missing_scenarios = sorted(set(SCENARIOS) - scenarios.keys())
    if missing_scenarios:
        fail(f"UI receipt is missing scenarios: {', '.join(missing_scenarios)}")

    validated = {}
    for scenario, required in SCENARIOS.items():
        summary = scenarios[scenario]
        if not isinstance(summary, dict):
            fail(f"scenarios.{scenario} is not an object")
        if summary.get("runs") != runs:
            fail(f"scenarios.{scenario}.runs does not match the receipt run count")
        metrics = summary.get("metrics")
        if not isinstance(metrics, dict):
            fail(f"scenarios.{scenario} has no metrics object")
        for metric in (*required, "rss_kb"):
            record = metrics.get(metric)
            if not isinstance(record, dict):
                fail(f"scenarios.{scenario} is missing metric {metric}")
            values = record.get("values")
            if not isinstance(values, list) or len(values) != runs:
                fail(f"scenarios.{scenario}.{metric}.values is not {runs} rows")
            for index, value in enumerate(values):
                finite(value, f"scenarios.{scenario}.{metric}.values[{index}]")
            if metric == "rss_kb" and min(values) <= 0:
                fail(f"scenarios.{scenario}.rss_kb contains zero")
            if metric == "size_bytes" and min(values) <= 0:
                fail("scenarios.size.size_bytes contains zero")
        validated[scenario] = {
            metric: metrics[metric] for metric in (*required, "rss_kb")
        }
    return {"runs": runs, "scenarios": validated, "identity": identity}


def validate_native(path: pathlib.Path, negative_log: pathlib.Path | None) -> dict:
    document = load(path, "native receipt")
    if document.get("hosted_capture") is not True:
        fail("native receipt is not explicitly marked hosted_capture")
    command = [sys.executable, str(NATIVE_ADAPTER), str(path)]
    if negative_log is not None:
        command.extend(("--negative-log", str(negative_log), "--require-negative"))
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "unknown failure"
        fail(f"native receipt rejected: {detail}")
    try:
        validated = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        fail(f"native adapter returned invalid JSON: {error}")
    return {"validation": validated, "identity": validate_identity(document, "native receipt")}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ui_receipt", type=pathlib.Path)
    ap.add_argument("native_receipt", type=pathlib.Path)
    ap.add_argument("--negative-log", type=pathlib.Path, required=True)
    ap.add_argument("--min-runs", type=int, default=3)
    ap.add_argument("--out", type=pathlib.Path)
    ns = ap.parse_args(argv)
    if ns.min_runs < 3:
        ap.error("--min-runs must be >= 3")
    ui = validate_ui(ns.ui_receipt, ns.min_runs)
    native = validate_native(ns.native_receipt, ns.negative_log)
    if ui["identity"] != native["identity"]:
        fail("UI and native identity records do not match")
    output = {
        "schema": "spectr-wp0-hosted-baseline-v1",
        "runs": ui["runs"],
        "scenarios": ui["scenarios"],
        "native": native,
        "negative_control": "rejected",
    }
    payload = json.dumps(output, indent=2, sort_keys=True) + "\n"
    if ns.out:
        ns.out.write_text(payload)
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"wp0 hosted receipt: ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
