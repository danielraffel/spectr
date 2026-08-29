#!/usr/bin/env python3
"""Turn a Spectr interaction Perfetto trace into a strict JSON receipt.

The capture is deliberately split into ``bands`` and ``minimap`` workloads by
``bands_perf_capture.swift``.  This tool keeps those results separate, checks
that the real AppKit -> Pulp input and GPU-frame slices are present, enforces
the 120 Hz interaction budget, and binds the receipt to the exact Spectr and
Pulp SDK source SHAs that produced the app.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


MARKER = "SPECTR_INTERACTION_PERF"
SHA_RE = re.compile(r"^[0-9a-f]{40}$")


def sql_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def build_query() -> str:
    stages = (
        "layout_children",
        "paint",
        "dom_event_dispatch",
        "editor_bridge_dispatch_json",
        "raf_flush",
        "gpu_acquire",
        "gpu_submit",
        "gpu_present",
    )
    stage_values = ", ".join(f"({sql_string(name)})" for name in stages)
    return f"""
WITH input AS (
  SELECT ts, dur,
         ROW_NUMBER() OVER (ORDER BY dur) AS rn,
         COUNT(*) OVER () AS n
  FROM slice
  WHERE name = 'native_drag_dispatch' AND dur >= 0
), bounds AS (
  SELECT MIN(ts) AS first_ts, MAX(ts + dur) AS last_ts, COUNT(*) AS input_count
  FROM input
), input_stats AS (
  SELECT
    COALESCE(MAX(CASE WHEN rn = CAST((95 * n + 99) / 100 AS INT)
                      THEN dur END), 0) / 1e6 AS p95_ms,
    COALESCE(MAX(dur), 0) / 1e6 AS max_ms
  FROM input
), frame_source AS (
  SELECT s.ts, s.dur,
         ROW_NUMBER() OVER (ORDER BY s.dur) AS rn,
         COUNT(*) OVER () AS n
  FROM slice s, bounds b
  WHERE s.category = 'render' AND s.name = 'frame' AND s.dur >= 0
    AND s.ts <= b.last_ts AND s.ts + s.dur >= b.first_ts
), frame_stats AS (
  SELECT COUNT(*) AS frame_count,
    COALESCE(MAX(CASE WHEN rn = CAST((50 * n + 99) / 100 AS INT)
                      THEN dur END), 0) / 1e6 AS p50_ms,
    COALESCE(MAX(CASE WHEN rn = CAST((95 * n + 99) / 100 AS INT)
                      THEN dur END), 0) / 1e6 AS p95_ms,
    COALESCE(MAX(CASE WHEN rn = CAST((99 * n + 99) / 100 AS INT)
                      THEN dur END), 0) / 1e6 AS p99_ms,
    COALESCE(MAX(dur), 0) / 1e6 AS max_ms,
    SUM(CASE WHEN dur > 8333000 THEN 1 ELSE 0 END) AS over_120hz,
    SUM(CASE WHEN dur > 16667000 THEN 1 ELSE 0 END) AS over_60hz
  FROM frame_source
), wanted_stage(name) AS (VALUES {stage_values}),
stage_stats AS (
  SELECT w.name,
    COUNT(s.id) AS slice_count,
    COALESCE(SUM(s.dur), 0) / 1e6 AS total_ms,
    COALESCE(MAX(s.dur), 0) / 1e6 AS max_ms
  FROM wanted_stage w
  LEFT JOIN slice s ON s.name = w.name AND s.dur >= 0
  LEFT JOIN bounds b ON 1 = 1
  WHERE s.id IS NULL OR (s.ts >= b.first_ts AND s.ts <= b.last_ts)
  GROUP BY w.name
)
SELECT '{MARKER}|summary|' || b.input_count || '|' ||
       printf('%.6f', i.p95_ms) || '|' || printf('%.6f', i.max_ms) || '|' ||
       f.frame_count || '|' || printf('%.6f', f.p50_ms) || '|' ||
       printf('%.6f', f.p95_ms) || '|' || printf('%.6f', f.p99_ms) || '|' ||
       printf('%.6f', f.max_ms) || '|' || f.over_120hz || '|' || f.over_60hz
FROM bounds b, input_stats i, frame_stats f
UNION ALL
SELECT '{MARKER}|stage|' || name || '|' || slice_count || '|' ||
       printf('%.6f', total_ms) || '|' || printf('%.6f', max_ms)
FROM stage_stats;
"""


def parse_output(output: str) -> tuple[dict[str, float | int], dict[str, dict[str, float | int]]]:
    summary_match = re.search(
        rf"{MARKER}\|summary\|(\d+)\|([0-9.]+)\|([0-9.]+)\|(\d+)\|"
        r"([0-9.]+)\|([0-9.]+)\|([0-9.]+)\|([0-9.]+)\|(\d+)\|(\d+)",
        output,
    )
    if not summary_match:
        raise ValueError("trace_processor output did not contain the summary marker")
    summary: dict[str, float | int] = {
        "input_count": int(summary_match.group(1)),
        "input_p95_ms": float(summary_match.group(2)),
        "input_max_ms": float(summary_match.group(3)),
        "frame_count": int(summary_match.group(4)),
        "frame_p50_ms": float(summary_match.group(5)),
        "frame_p95_ms": float(summary_match.group(6)),
        "frame_p99_ms": float(summary_match.group(7)),
        "frame_max_ms": float(summary_match.group(8)),
        "frames_over_120hz": int(summary_match.group(9)),
        "frames_over_60hz": int(summary_match.group(10)),
    }
    stages: dict[str, dict[str, float | int]] = {}
    for match in re.finditer(
        rf"{MARKER}\|stage\|([^|\s]+)\|(\d+)\|([0-9.]+)\|([0-9.]+)",
        output,
    ):
        stages[match.group(1)] = {
            "count": int(match.group(2)),
            "total_ms": float(match.group(3)),
            "max_ms": float(match.group(4)),
        }
    return summary, stages


def cache_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text().splitlines():
        if line.startswith("//") or line.startswith("#") or "=" not in line:
            continue
        key_and_type, value = line.split("=", 1)
        key = key_and_type.split(":", 1)[0]
        values[key] = value
    return values


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_processor(explicit: str) -> str | None:
    if explicit:
        return explicit
    configured = os.environ.get("PULP_TRACE_PROCESSOR", "")
    if configured:
        return configured
    return shutil.which("trace_processor_shell") or shutil.which("trace_processor")


def run_processor(processor: str, trace: Path) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".sql", delete=False) as handle:
        handle.write(build_query())
        query_path = Path(handle.name)
    try:
        result = subprocess.run(
            [processor, "-q", str(query_path), str(trace)],
            check=False,
            capture_output=True,
            text=True,
        )
    finally:
        query_path.unlink(missing_ok=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"trace_processor exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout


def failures_for(
    workload: str,
    summary: dict[str, float | int],
    stages: dict[str, dict[str, float | int]],
    max_input_p95_ms: float,
    max_frame_p95_ms: float,
    max_frame_p99_ms: float,
) -> list[str]:
    failures: list[str] = []
    minimum_inputs = 120 if workload == "bands" else 150
    if int(summary["input_count"]) < minimum_inputs:
        failures.append(
            f"input_count {summary['input_count']} is below {minimum_inputs} for {workload}")
    if float(summary["input_p95_ms"]) > max_input_p95_ms:
        failures.append(
            f"input p95 {summary['input_p95_ms']:.3f} ms exceeds {max_input_p95_ms:.3f} ms")
    if int(summary["frame_count"]) < minimum_inputs:
        failures.append(
            f"frame_count {summary['frame_count']} is below {minimum_inputs} for {workload}")
    if float(summary["frame_p95_ms"]) > max_frame_p95_ms:
        failures.append(
            f"frame p95 {summary['frame_p95_ms']:.3f} ms exceeds {max_frame_p95_ms:.3f} ms")
    if float(summary["frame_p99_ms"]) > max_frame_p99_ms:
        failures.append(
            f"frame p99 {summary['frame_p99_ms']:.3f} ms exceeds {max_frame_p99_ms:.3f} ms")

    required = ("layout_children", "paint", "dom_event_dispatch",
                "gpu_acquire", "gpu_submit", "gpu_present")
    missing = [name for name in required if int(stages.get(name, {}).get("count", 0)) == 0]
    if missing:
        failures.append("trace is missing required stage slices: " + ", ".join(missing))

    inputs = max(1, int(summary["input_count"]))
    for stage in ("layout_children", "paint"):
        count = int(stages.get(stage, {}).get("count", 0))
        if count / inputs > 1.10:
            failures.append(f"{stage}/input {count / inputs:.3f} exceeds 1.100")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--cmake-cache", type=Path, required=True)
    parser.add_argument("--workload", choices=("bands", "minimap"), required=True)
    parser.add_argument("--spectr-sha", required=True)
    parser.add_argument("--sdk-sha", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--processor", default="")
    parser.add_argument("--max-input-p95-ms", type=float, default=1.0)
    parser.add_argument("--max-frame-p95-ms", type=float, default=8.5)
    parser.add_argument("--max-frame-p99-ms", type=float, default=16.667)
    args = parser.parse_args(argv)

    for label, value in (("Spectr", args.spectr_sha), ("Pulp SDK", args.sdk_sha)):
        if not SHA_RE.fullmatch(value):
            parser.error(f"{label} SHA must be a lowercase 40-character commit SHA")
    binary = args.app / "Contents" / "MacOS" / "Spectr"
    for label, path in (("trace", args.trace), ("app binary", binary),
                        ("CMake cache", args.cmake_cache)):
        if not path.is_file():
            parser.error(f"{label} does not exist: {path}")
    cache = cache_values(args.cmake_cache)
    if cache.get("CMAKE_BUILD_TYPE") != "Release":
        parser.error("CMake cache is not a Release build")
    if cache.get("SPECTR_EXPECTED_PULP_SDK_SHA") != args.sdk_sha:
        parser.error("Spectr expected SDK SHA does not match --sdk-sha")
    if cache.get("PULP_SDK_SOURCE_GIT_SHA") != args.sdk_sha:
        parser.error("resolved Pulp SDK source SHA does not match --sdk-sha")
    processor = resolve_processor(args.processor)
    if not processor:
        parser.error("trace_processor not found; set PULP_TRACE_PROCESSOR or pass --processor")

    try:
        summary, stages = parse_output(run_processor(processor, args.trace))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"trace analysis error: {error}", file=sys.stderr)
        return 2
    failures = failures_for(
        args.workload, summary, stages, args.max_input_p95_ms,
        args.max_frame_p95_ms, args.max_frame_p99_ms)
    receipt = {
        "schema": "spectr-interaction-perf-v1",
        "accepted": not failures,
        "workload": args.workload,
        "provenance": {
            "spectr_sha": args.spectr_sha,
            "pulp_sdk_sha": args.sdk_sha,
            "pulp_sdk_kind": cache.get("PULP_SDK_PROVENANCE_KIND", "unknown"),
            "pulp_sdk_distribution_eligible": cache.get(
                "PULP_SDK_DISTRIBUTION_ELIGIBLE", "unknown"),
            "app_binary_sha256": sha256(binary),
        },
        "budgets": {
            "refresh_hz": 120,
            "max_input_p95_ms": args.max_input_p95_ms,
            "max_frame_p95_ms": args.max_frame_p95_ms,
            "max_frame_p99_ms": args.max_frame_p99_ms,
        },
        "summary": summary,
        "stages": stages,
        "failures": failures,
        "artifacts": {
            "trace": str(args.trace.resolve()),
            "app": str(args.app.resolve()),
            "cmake_cache": str(args.cmake_cache.resolve()),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print(json.dumps(receipt, indent=2, sort_keys=True))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
