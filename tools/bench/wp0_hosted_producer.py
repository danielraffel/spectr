#!/usr/bin/env python3
"""Produce a strict three-run hosted WP-0 receipt from the real AU probe.

The Cocoa probe is the only current in-process host that opens Spectr's native
editor through an actual format boundary.  This producer runs that probe in a
fresh child for every sample, captures the child's RSS, and maps the
product-owned bridge/layout/raw-RGBA seam into the common UI receipt schema.
Missing hosted instrumentation is an error.  A VST3 request fails explicitly
until a VST3 GUI host with the same seam exists; no AU result is relabelled as
VST3 evidence.
"""

from __future__ import annotations

import argparse
import copy
import datetime as _datetime
import hashlib
import json
import math
import pathlib
import re
import subprocess
import sys
import tempfile
import uuid


SHA40 = re.compile(r"^[0-9a-f]{40}$")
DARWIN_RSS = re.compile(r"\s+([0-9]+)\s+maximum resident set size\s*$", re.MULTILINE)


class ProducerError(RuntimeError):
    pass


def fail(message: str) -> "NoReturn":
    raise ProducerError(message)


def digest_artifact(path: pathlib.Path) -> tuple[str, int]:
    if path.is_file():
        payload = path.read_bytes()
        return hashlib.sha256(payload).hexdigest(), len(payload)
    if not path.is_dir():
        fail(f"artifact does not exist: {path}")
    digest = hashlib.sha256()
    total = 0
    for child in sorted(p for p in path.rglob("*") if p.is_file()):
        relative = child.relative_to(path).as_posix().encode("utf-8")
        payload = child.read_bytes()
        digest.update(len(relative).to_bytes(8, "big"))
        digest.update(relative)
        digest.update(len(payload).to_bytes(8, "big"))
        digest.update(payload)
        total += len(payload)
    if total <= 0:
        fail("artifact contains no files")
    return digest.hexdigest(), total


def read_build_info(bundle: pathlib.Path) -> dict:
    path = bundle / "Contents" / "Resources" / "pulp-build-info.json"
    try:
        document = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read build identity {path}: {error}")
    if not isinstance(document, dict):
        fail("build identity is not an object")
    product = document.get("product")
    build = document.get("build")
    sdk = document.get("pulp_sdk")
    if not all(isinstance(value, dict) for value in (product, build, sdk)):
        fail("build identity lacks product/build/pulp_sdk objects")
    product_sha = product.get("source_git_sha")
    sdk_sha = sdk.get("source_git_sha")
    build_type = build.get("type")
    if not isinstance(product_sha, str) or not SHA40.fullmatch(product_sha):
        fail("build identity has no 40-character product source SHA")
    if not isinstance(sdk_sha, str) or not SHA40.fullmatch(sdk_sha):
        fail("build identity has no 40-character Pulp SDK source SHA")
    if build_type not in {"Debug", "RelWithDebInfo", "Release"}:
        fail(f"unsupported build type: {build_type!r}")
    return document


def finite_positive(value, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"{label} is not numeric")
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        fail(f"{label} must be finite and positive")
    return value


def rss_kb(stderr: str) -> float:
    match = DARWIN_RSS.search(stderr)
    if match is None:
        fail("/usr/bin/time -l did not report child RSS")
    return finite_positive(int(match.group(1)) / 1024.0, "child RSS")


def metric(values: list[float]) -> dict:
    values = [finite_positive(value, "metric value") for value in values]
    mean = sum(values) / len(values)
    return {
        "values": values,
        "mean": mean,
        "min": min(values),
        "max": max(values),
        "spread": (max(values) - min(values)) / mean if mean else 0.0,
    }


def load_probe(path: pathlib.Path) -> dict:
    try:
        document = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read probe JSON {path}: {error}")
    if not isinstance(document, dict) or not isinstance(document.get("opens"), list):
        fail("probe JSON has no opens[]")
    opens = document["opens"]
    if len(opens) != 1 or not isinstance(opens[0], dict):
        fail("producer requires one open row per child process")
    row = opens[0]
    if row.get("hosted_metrics_available") is not True:
        fail("probe did not provide hosted bridge/layout/paint metrics")
    for key in ("factory_ms", "first_present_ms", "frame_p95_ms",
                "hosted_bridge_calls", "hosted_layout_ms", "hosted_paint_ms",
                "hosted_width", "hosted_height", "hosted_rgba_bytes"):
        finite_positive(row.get(key), f"probe opens[0].{key}")
    return row


def validate_receipt(document: dict) -> None:
    if document.get("schema") != "spectr-ui-bench-v1":
        fail("generated receipt has an unexpected schema")
    scenarios = document.get("scenarios")
    if not isinstance(scenarios, dict):
        fail("generated receipt has no scenarios")
    required = {
        "open": ("open_ms", "first_frame_ms"),
        "frame": ("frame_ms",),
        "bridge": ("bridge_calls",),
        "layout": ("layout_ms",),
        "paint": ("paint_ms",),
        "size": ("size_bytes",),
    }
    runs = document.get("runs")
    if not isinstance(runs, int) or runs < 3:
        fail("generated receipt must contain at least three runs")
    for scenario, names in required.items():
        summary = scenarios.get(scenario)
        if not isinstance(summary, dict) or summary.get("runs") != runs:
            fail(f"scenario {scenario} has no matching run count")
        metrics = summary.get("metrics")
        if not isinstance(metrics, dict):
            fail(f"scenario {scenario} has no metrics")
        for name in (*names, "rss_kb"):
            record = metrics.get(name)
            if not isinstance(record, dict) or not isinstance(record.get("values"), list):
                fail(f"scenario {scenario} is missing {name}")
            values = record["values"]
            if len(values) != runs:
                fail(f"scenario {scenario}.{name} has the wrong row count")
            for index, value in enumerate(values):
                finite_positive(value, f"scenario {scenario}.{name}[{index}]")


def planted_negative_control(document: dict) -> tuple[bool, str]:
    """Prove this producer rejects a missing paint measurement."""
    broken = copy.deepcopy(document)
    broken["scenarios"]["paint"]["metrics"]["paint_ms"]["values"][0] = 0
    try:
        validate_receipt(broken)
    except ProducerError as error:
        return True, str(error)
    return False, "the zero-paint planted negative control unexpectedly passed"


def run_probe(probe: pathlib.Path, bundle: pathlib.Path, json_path: pathlib.Path,
              settle_ms: float) -> tuple[dict, float, str]:
    command = [
        "/usr/bin/time", "-l", str(probe),
        "--bundle", str(bundle), "--opens", "1", "--settle-ms", str(settle_ms),
        "--hosted-metrics", "--json", str(json_path),
    ]
    process = subprocess.run(command, capture_output=True, text=True)
    if process.returncode:
        detail = process.stdout[-1000:] + process.stderr[-1000:]
        fail(f"hosted AU probe exited {process.returncode}: {detail.strip()}")
    return load_probe(json_path), rss_kb(process.stderr), process.stderr


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--bundle", type=pathlib.Path, required=True)
    parser.add_argument("--format", default="AU")
    parser.add_argument("--host-id", required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--settle-ms", type=float, default=600.0)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--negative-log", type=pathlib.Path)
    parser.add_argument("--allow-dirty", action="store_true",
                        help="allow a build-info source_git_dirty=true artifact")
    args = parser.parse_args(argv)
    if args.format != "AU":
        fail(f"hosted format {args.format!r} is unsupported: only AU Cocoa has a real probe")
    if args.runs < 3:
        fail("--runs must be >= 3")
    probe = args.probe.resolve()
    bundle = args.bundle.resolve()
    if not probe.is_file() or not bundle.is_dir():
        fail("--probe must be a file and --bundle must be a bundle directory")
    build_info = read_build_info(bundle)
    product = build_info["product"]
    build = build_info["build"]
    sdk = build_info["pulp_sdk"]
    if product.get("source_git_dirty") is True and not args.allow_dirty:
        fail("artifact was built from a dirty source tree; rebuild clean or pass --allow-dirty")
    artifact_sha, artifact_bytes = digest_artifact(bundle)
    run_id = "wp0-au-" + _datetime.datetime.now(_datetime.timezone.utc).strftime(
        "%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:12]
    identity = {
        "host_id": args.host_id,
        "host_format": "AU",
        "build_id": product["source_git_sha"],
        "build_type": build["type"],
        "run_id": run_id,
        "artifact_path": str(bundle),
        "artifact_sha256": artifact_sha,
        "artifact_bytes": artifact_bytes,
        "product_source_sha": product["source_git_sha"],
        "pulp_sdk_source_sha": sdk["source_git_sha"],
    }

    rows: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="spectr-wp0-au-") as temporary:
        for index in range(args.runs):
            row, rss, _stderr = run_probe(
                probe, bundle, pathlib.Path(temporary) / f"open-{index + 1}.json",
                args.settle_ms)
            row["rss_kb"] = rss
            rows.append(row)

    rss_values = [row["rss_kb"] for row in rows]
    result = {
        "schema": "spectr-ui-bench-v1",
        "runs": args.runs,
        "identity": identity,
        "scenarios": {
            "open": {"runs": args.runs, "metrics": {
                "open_ms": metric([row["factory_ms"] for row in rows]),
                "first_frame_ms": metric([row["first_present_ms"] for row in rows]),
                "rss_kb": metric(rss_values),
            }},
            "frame": {"runs": args.runs, "metrics": {
                "frame_ms": metric([row["frame_p95_ms"] for row in rows]),
                "rss_kb": metric(rss_values),
            }},
            "bridge": {"runs": args.runs, "metrics": {
                "bridge_calls": metric([row["hosted_bridge_calls"] for row in rows]),
                "rss_kb": metric(rss_values),
            }},
            "layout": {"runs": args.runs, "metrics": {
                "layout_ms": metric([row["hosted_layout_ms"] for row in rows]),
                "rss_kb": metric(rss_values),
            }},
            "paint": {"runs": args.runs, "metrics": {
                "paint_ms": metric([row["hosted_paint_ms"] for row in rows]),
                "rss_kb": metric(rss_values),
            }},
            "size": {"runs": args.runs, "metrics": {
                "size_bytes": metric([artifact_bytes] * args.runs),
                "rss_kb": metric(rss_values),
            }},
        },
        "producer": {
            "schema": "spectr-wp0-au-hosted-producer-v1",
            "probe": str(probe),
            "probe_mode": "real-au-cocoa-inprocess",
            "hosted_metrics": "spectr_wp0_hosted_measure_v1",
            "unsupported_formats": ["VST3"],
        },
    }
    validate_receipt(result)
    rejected, reason = planted_negative_control(result)
    if not rejected:
        fail(reason)
    result["negative_control"] = {
        "id": "__hosted_metric_unavailable",
        "requested": True,
        "rejected": True,
        "reason": reason,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    log_path = args.negative_log or args.out.with_suffix(".negative.log")
    log_path.write_text(
        "[wp0] planted negative control id=__hosted_metric_unavailable "
        "rejected=yes\n[wp0] ZERO_PAINT __hosted_metric_unavailable\n")
    print(json.dumps({
        "receipt": str(args.out), "schema": result["schema"],
        "runs": args.runs, "host_format": "AU",
        "artifact_sha256": artifact_sha, "artifact_bytes": artifact_bytes,
        "negative_control": "rejected",
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ProducerError, ValueError) as error:
        print(f"wp0 hosted producer: ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
