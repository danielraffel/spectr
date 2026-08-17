#!/usr/bin/env python3
"""Matched Standalone operational benchmark for Spectr's renderer cutover.

This is intentionally an evidence tool rather than a pass/fail performance
test: host load and thermal state vary.  It launches the WebView and native
Standalone products with the same Pulp screenshot frame budget, records the
kernel's wall/user/system/RSS counters, checks that every requested frame
produced a PNG, and proves each process exited cleanly.  The JSON output keeps
the individual samples so a report cannot hide variance behind one average.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import statistics
import subprocess
import tempfile
import time


TIME_LINE = re.compile(
    r"^\s*([0-9.]+)\s+real\s+([0-9.]+)\s+user\s+([0-9.]+)\s+sys\s*$",
    re.MULTILINE,
)
RSS_LINE = re.compile(r"^\s*(\d+)\s+maximum resident set size\s*$", re.MULTILINE)


def median(values: list[float]) -> float:
    return round(float(statistics.median(values)), 6)


def summarize(samples: list[dict[str, float | int | str]]) -> dict[str, float]:
    return {
        "wall_seconds_median": median([float(s["wall_seconds"]) for s in samples]),
        "user_seconds_median": median([float(s["user_seconds"]) for s in samples]),
        "system_seconds_median": median([float(s["system_seconds"]) for s in samples]),
        "max_rss_mib_median": median([float(s["max_rss_mib"]) for s in samples]),
        "aggregate_peak_rss_mib_median": median(
            [float(s["aggregate_peak_rss_mib"]) for s in samples]
        ),
        "aggregate_peak_cpu_percent_median": median(
            [float(s["aggregate_peak_cpu_percent"]) for s in samples]
        ),
        "aggregate_peak_process_count_median": median(
            [float(s["aggregate_peak_process_count"]) for s in samples]
        ),
    }


def process_count(executable: pathlib.Path) -> int:
    result = subprocess.run(
        ["pgrep", "-f", str(executable)], capture_output=True, text=True, check=False
    )
    if result.returncode == 1:
        return 0
    if result.returncode != 0:
        raise RuntimeError(f"pgrep failed for {executable}: {result.stderr.strip()}")
    return len([line for line in result.stdout.splitlines() if line.strip()])


def process_rows() -> dict[int, dict[str, object]]:
    result = subprocess.run(
        ["ps", "-axo", "pid=,ppid=,rss=,%cpu=,command="],
        capture_output=True,
        text=True,
        check=True,
    )
    rows: dict[int, dict[str, object]] = {}
    for line in result.stdout.splitlines():
        fields = line.strip().split(None, 4)
        if len(fields) != 5:
            continue
        try:
            pid, ppid, rss_kib = (int(fields[index]) for index in range(3))
            cpu_percent = float(fields[3])
        except ValueError:
            continue
        rows[pid] = {
            "ppid": ppid,
            "rss_kib": rss_kib,
            "cpu_percent": cpu_percent,
            "command": fields[4],
        }
    return rows


def webkit_pids(rows: dict[int, dict[str, object]]) -> set[int]:
    return {
        pid
        for pid, row in rows.items()
        if "/com.apple.WebKit." in str(row["command"])
    }


def descendants(root_pid: int, rows: dict[int, dict[str, object]]) -> set[int]:
    found = {root_pid}
    changed = True
    while changed:
        changed = False
        for pid, row in rows.items():
            if pid not in found and int(row["ppid"]) in found:
                found.add(pid)
                changed = True
    return found


def capture_once(
    executable: pathlib.Path,
    lane_name: str,
    frames: int,
    ordinal: int,
    scratch: pathlib.Path,
    extra_env: dict[str, str] | None = None,
) -> dict[str, float | int | str]:
    scenario = "audio" if extra_env else "silent"
    png = scratch / (
        f"{executable.parent.parent.parent.name}-{scenario}-{frames}-{ordinal}.png"
    )
    env = os.environ.copy()
    env.update(
        {
            "PULP_HEADLESS": "1",
            "PULP_SCREENSHOT": str(png),
            "PULP_FRAMES": str(frames),
        }
    )
    if extra_env:
        env.update(extra_env)
    include_webkit = lane_name == "webview"
    webkit_before = webkit_pids(process_rows()) if include_webkit else set()
    started = time.monotonic()
    process = subprocess.Popen(
        ["/usr/bin/time", "-l", str(executable)],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    peak_process_count = 0
    peak_tree_rss_kib = 0
    peak_tree_cpu_percent = 0.0
    attributed_webkit: set[int] = set()
    deadline = started + 120.0
    while process.poll() is None:
        if time.monotonic() > deadline:
            process.kill()
            raise RuntimeError(f"capture timed out: {executable}")
        rows = process_rows()
        if include_webkit:
            attributed_webkit.update(webkit_pids(rows) - webkit_before)
        owned = descendants(process.pid, rows) | attributed_webkit
        live = [rows[pid] for pid in owned if pid in rows]
        peak_process_count = max(peak_process_count, len(live))
        peak_tree_rss_kib = max(
            peak_tree_rss_kib, sum(int(row["rss_kib"]) for row in live)
        )
        peak_tree_cpu_percent = max(
            peak_tree_cpu_percent,
            sum(float(row["cpu_percent"]) for row in live),
        )
        time.sleep(0.05)
    stdout, stderr = process.communicate(timeout=5)
    observed_wall = time.monotonic() - started
    diagnostic = stdout + "\n" + stderr
    timing = TIME_LINE.search(diagnostic)
    rss = RSS_LINE.search(diagnostic)
    if process.returncode != 0 or timing is None or rss is None:
        raise RuntimeError(
            f"capture failed ({executable}, frames={frames}, rc={process.returncode}):\n"
            f"{diagnostic[-4000:]}"
        )
    if not png.is_file() or png.stat().st_size < 1024:
        raise RuntimeError(f"capture did not produce a valid PNG: {png}")
    if process_count(executable) != 0:
        raise RuntimeError(f"process survived completed capture: {executable}")

    # WebKit helpers are launchd-owned and therefore absent from wait4()/time's
    # root-process counters. Give helpers created by this launch a short grace
    # period to terminate, then report any survivors instead of silently
    # treating them as somebody else's process.
    helper_deadline = time.monotonic() + 2.0
    surviving_helpers = attributed_webkit & set(process_rows())
    while surviving_helpers and time.monotonic() < helper_deadline:
        time.sleep(0.05)
        surviving_helpers = attributed_webkit & set(process_rows())
    return {
        "ordinal": ordinal,
        "frames": frames,
        "wall_seconds": float(timing.group(1)),
        "user_seconds": float(timing.group(2)),
        "system_seconds": float(timing.group(3)),
        "max_rss_mib": round(int(rss.group(1)) / (1024.0 * 1024.0), 6),
        "aggregate_peak_rss_mib": round(peak_tree_rss_kib / 1024.0, 6),
        "aggregate_peak_cpu_percent": round(peak_tree_cpu_percent, 3),
        "aggregate_peak_process_count": peak_process_count,
        "attributed_webkit_helper_count": len(attributed_webkit),
        "surviving_webkit_helpers_after_2s": len(surviving_helpers),
        "audio_device_opened": "CoreAudio: opened device" in diagnostic,
        "observer_wall_seconds": round(observed_wall, 6),
        "png_bytes": png.stat().st_size,
    }


def capture_concurrent(
    executable: pathlib.Path,
    lane_name: str,
    instances: int,
    frames: int,
    scratch: pathlib.Path,
) -> dict[str, float | int]:
    include_webkit = lane_name == "webview"
    webkit_before = webkit_pids(process_rows()) if include_webkit else set()
    processes: list[subprocess.Popen[str]] = []
    pngs: list[pathlib.Path] = []
    for index in range(instances):
        png = scratch / f"multi-{lane_name}-{index}.png"
        pngs.append(png)
        env = os.environ.copy()
        env.update(
            {
                "PULP_HEADLESS": "1",
                "PULP_SCREENSHOT": str(png),
                "PULP_FRAMES": str(frames),
            }
        )
        processes.append(
            subprocess.Popen(
                [str(executable)],
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                text=True,
            )
        )

    started = time.monotonic()
    deadline = started + 120.0
    attributed_webkit: set[int] = set()
    peak_process_count = 0
    peak_rss_kib = 0
    peak_cpu_percent = 0.0
    while any(process.poll() is None for process in processes):
        if time.monotonic() > deadline:
            for process in processes:
                if process.poll() is None:
                    process.kill()
            raise RuntimeError(f"concurrent capture timed out: {lane_name}")
        rows = process_rows()
        if include_webkit:
            attributed_webkit.update(webkit_pids(rows) - webkit_before)
        owned = set(attributed_webkit)
        for process in processes:
            owned.update(descendants(process.pid, rows))
        live = [rows[pid] for pid in owned if pid in rows]
        peak_process_count = max(peak_process_count, len(live))
        peak_rss_kib = max(peak_rss_kib, sum(int(row["rss_kib"]) for row in live))
        peak_cpu_percent = max(
            peak_cpu_percent, sum(float(row["cpu_percent"]) for row in live)
        )
        time.sleep(0.05)

    return_codes = [process.wait(timeout=5) for process in processes]
    if any(code != 0 for code in return_codes):
        raise RuntimeError(f"concurrent {lane_name} return codes: {return_codes}")
    if any(not png.is_file() or png.stat().st_size < 1024 for png in pngs):
        raise RuntimeError(f"concurrent {lane_name} capture missed a PNG")
    if process_count(executable) != 0:
        raise RuntimeError(f"concurrent {lane_name} process survived capture")

    helper_deadline = time.monotonic() + 2.0
    surviving_helpers = attributed_webkit & set(process_rows())
    while surviving_helpers and time.monotonic() < helper_deadline:
        time.sleep(0.05)
        surviving_helpers = attributed_webkit & set(process_rows())

    peak_rss_mib = peak_rss_kib / 1024.0
    return {
        "instances": instances,
        "frames": frames,
        "wall_seconds": round(time.monotonic() - started, 6),
        "aggregate_peak_rss_mib": round(peak_rss_mib, 6),
        "peak_rss_mib_per_editor": round(peak_rss_mib / instances, 6),
        "aggregate_peak_cpu_percent": round(peak_cpu_percent, 3),
        "aggregate_peak_process_count": peak_process_count,
        "attributed_webkit_helper_count": len(attributed_webkit),
        "surviving_webkit_helpers_after_2s": len(surviving_helpers),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--instances", type=int, default=3)
    args = parser.parse_args()
    if args.repetitions < 2:
        parser.error("--repetitions must be at least 2 (first and warm samples)")
    if args.instances < 2:
        parser.error("--instances must be at least 2")

    build = args.build_dir.resolve()
    lanes = {
        "webview": build
        / "Spectr WebView Reference.app"
        / "Contents"
        / "MacOS"
        / "Spectr WebView Reference",
        "native": build / "Spectr.app" / "Contents" / "MacOS" / "Spectr",
    }
    for name, executable in lanes.items():
        if not executable.is_file():
            parser.error(f"{name} executable is missing: {executable}")
        if process_count(executable):
            parser.error(f"{name} executable is already running: {executable}")

    report: dict[str, object] = {
        "schema": "spectr-native-cutover-performance-v2",
        "method": {
            "runner": "/usr/bin/time -l plus 50ms ps process-tree sampling",
            "headless": True,
            "audio": "disabled for ordinary scenarios; explicitly enabled with an inaudible zero-amplitude test signal for analyzer scenarios",
            "frame_budgets": [1, 120],
            "repetitions": args.repetitions,
            "cold_definition": "first process launch in this ordered run",
            "warm_definition": "median of subsequent process launches",
            "gpu_counter": "unavailable; aggregate CPU includes WebKit GPU/WebContent helpers",
            "webkit_attribution": "WebKit helper PIDs created after each lane launch",
        },
        "lanes": {},
    }

    with tempfile.TemporaryDirectory(prefix="spectr-cutover-benchmark-") as temp:
        scratch = pathlib.Path(temp)
        for lane_name, executable in lanes.items():
            lane_report: dict[str, object] = {"executable": str(executable)}
            for frames in (1, 120):
                samples = [
                    capture_once(executable, lane_name, frames, ordinal, scratch)
                    for ordinal in range(args.repetitions)
                ]
                lane_report[f"frames_{frames}"] = {
                    "first": samples[0],
                    "warm_samples": samples[1:],
                    "warm_summary": summarize(samples[1:]),
                    "all_samples": samples,
                }
            audio_env = {
                "PULP_SCREENSHOT_KEEP_AUDIO": "1",
                "PULP_TEST_SIGNAL": "sine",
                # Exercise the complete audio/analyzer publication path without
                # emitting an audible signal through the selected device.
                "PULP_TEST_SIGNAL_AMPLITUDE": "0",
                "PULP_TEST_SIGNAL_FREQUENCY_HZ": "1000",
            }
            audio_samples = [
                capture_once(
                    executable, lane_name, 120, ordinal, scratch, audio_env
                )
                for ordinal in range(args.repetitions)
            ]
            if not all(bool(sample["audio_device_opened"]) for sample in audio_samples):
                raise RuntimeError(f"{lane_name} audio/analyzer scenario did not open CoreAudio")
            lane_report["frames_120_audio_analyzer"] = {
                "first": audio_samples[0],
                "warm_samples": audio_samples[1:],
                "warm_summary": summarize(audio_samples[1:]),
                "all_samples": audio_samples,
            }
            lane_report["multiple_editors"] = capture_concurrent(
                executable, lane_name, args.instances, 120, scratch
            )
            report["lanes"][lane_name] = lane_report

    report["teardown"] = {
        name: {"surviving_processes": process_count(executable)}
        for name, executable in lanes.items()
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
