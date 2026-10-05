#!/usr/bin/env python3
"""Small, reproducible UI baseline runner (WP-0).

The probe is deliberately supplied by the caller: this keeps the harness
independent of a host or a built plug-in while giving every probe one schema.
Each command must print one JSON object.  Use ``{scenario}`` and ``{run}``
placeholders to select a workload.  A command is executed three times by
default; missing required metrics are errors, never reported as zero.
"""
from __future__ import annotations
import argparse, json, pathlib, re, shlex, subprocess, time

SCENARIOS = ("open", "frame", "bridge", "layout", "paint", "size")
REQUIRED = {
    "open": ("open_ms", "first_frame_ms"),
    "frame": ("frame_ms",),
    "bridge": ("bridge_calls",),
    "layout": ("layout_ms",),
    "paint": ("paint_ms",),
    "size": ("size_bytes",),
}
_DARWIN_RSS = re.compile(r"\s+([0-9]+)\s+maximum resident set size\s*$", re.MULTILINE)
_POSIX_RSS = re.compile(r"Maximum resident set size \(kbytes\):\s*([0-9]+)")

def _number(value, key):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key} must be numeric")
    return float(value)


def _child_rss_kb(stderr: str) -> float:
    """Read the RSS for this child, rather than RUSAGE_CHILDREN's process maximum.

    ``resource.getrusage(RUSAGE_CHILDREN).ru_maxrss`` is cumulative and keeps
    the largest child seen by the parent.  Reusing it made later runs inherit
    the first large run's RSS, which can turn an unavailable measurement into
    a false positive.  ``time -l`` reports the wait4 result for this one
    command on macOS; GNU time's verbose spelling is accepted for Linux CI.
    """
    match = _DARWIN_RSS.search(stderr) or _POSIX_RSS.search(stderr)
    if not match:
        raise ValueError("child RSS was not reported by time(1)")
    value = int(match.group(1))
    if value <= 0:
        raise ValueError("child RSS is zero")
    return float(value) / 1024.0

def run(command: str, scenario: str, run_no: int) -> dict:
    rendered = command.format(scenario=scenario, run=run_no)
    started = time.monotonic()
    # Keep the caller's shell syntax (pipes, redirects, and quoted probes) but
    # put one ``time`` envelope around exactly this child invocation.
    time_cmd = "/usr/bin/time -l" if pathlib.Path("/usr/bin/time").exists() else "/usr/bin/time -v"
    proc = subprocess.run(
        f"{time_cmd} /bin/sh -c {shlex.quote(rendered)}",
        shell=True, capture_output=True, text=True,
    )
    elapsed = (time.monotonic() - started) * 1000.0
    if proc.returncode:
        raise RuntimeError(f"{scenario} run {run_no} exited {proc.returncode}: {proc.stderr[-500:]}")
    try:
        value = json.loads(proc.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError) as exc:
        raise ValueError(f"{scenario} run {run_no} did not print JSON") from exc
    if not isinstance(value, dict):
        raise ValueError(f"{scenario} run {run_no} JSON is not an object")
    missing = [k for k in REQUIRED[scenario] if k not in value]
    if missing:
        raise ValueError(f"{scenario} run {run_no} missing required metrics: {', '.join(missing)}")
    metrics = {k: _number(value[k], k) for k in value}
    metrics["harness_elapsed_ms"] = elapsed
    metrics["rss_kb"] = _child_rss_kb(proc.stderr)
    return metrics

def summarize(rows):
    keys = sorted(set().union(*(r.keys() for r in rows)))
    out = {"runs": len(rows), "metrics": {}}
    for key in keys:
        vals = [r[key] for r in rows if key in r]
        if not vals: continue
        mean = sum(vals) / len(vals)
        spread = (max(vals) - min(vals)) / mean if mean else 0.0
        out["metrics"][key] = {"values": vals, "mean": mean, "min": min(vals), "max": max(vals), "spread": spread}
    return out

def self_test():
    good = run("python3 -c 'import json; print(json.dumps({{\"open_ms\":10,\"first_frame_ms\":20}}))'", "open", 1)
    assert good["open_ms"] == 10
    try:
        run("python3 -c 'print(\"{{}}\")'", "open", 1)
    except ValueError as exc:
        assert "missing required metrics" in str(exc)
    else:
        raise AssertionError("negative control unexpectedly passed")

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--command", action="append", required=False, help="scenario=command; command prints JSON")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--out", type=pathlib.Path)
    ap.add_argument("--artifact", type=pathlib.Path)
    ap.add_argument("--identity-json", type=pathlib.Path,
                    help="JSON identity object for hosted receipts")
    ap.add_argument("--self-test", action="store_true")
    ns = ap.parse_args(argv)
    if ns.self_test:
        self_test(); print("ui_bench self-test: PASS"); return 0
    if ns.runs < 3: ap.error("--runs must be >= 3 for WP-0 noise measurement")
    commands = {}
    for item in ns.command or []:
        name, sep, command = item.partition("=")
        if name not in SCENARIOS or not sep or not command: ap.error(f"invalid --command {item!r}")
        commands[name] = command
    if not commands: ap.error("at least one --command scenario=... is required")
    result = {"schema": "spectr-ui-bench-v1", "runs": ns.runs, "scenarios": {}}
    for scenario, command in commands.items():
        rows = [run(command, scenario, i + 1) for i in range(ns.runs)]
        result["scenarios"][scenario] = summarize(rows)
    if ns.artifact:
        artifact = ns.artifact.resolve()
        result["artifact"] = {"path": str(artifact), "bytes": artifact.stat().st_size}
    if ns.identity_json:
        identity = json.loads(ns.identity_json.read_text())
        if not isinstance(identity, dict):
            raise ValueError("--identity-json must contain an object")
        result["identity"] = identity
    payload = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if ns.out: ns.out.write_text(payload)
    else: print(payload, end="")
    return 0

if __name__ == "__main__":
    try: raise SystemExit(main())
    except (ValueError, RuntimeError, OSError) as exc:
        print(f"ui_bench: ERROR: {exc}", file=sys.stderr); raise SystemExit(2)
