#!/usr/bin/env python3
"""Measure the editor's painted-frame cadence during a scripted pointer gesture.

The interaction acceptance gate scores the DURATION of `frame` trace slices. A
frame that is never painted produces no slice, so a stall that swallows whole
vsyncs is invisible to it: the slices that do exist stay short and the run reads
as fast. This probe measures the complementary quantity -- the wall-clock GAP
between consecutive painted frames -- by stamping every `[partial-render]` line
the host emits under `PULP_PARTIAL_RENDERING_DEBUG`.

Three properties make the numbers trustworthy, and each is enforced rather than
assumed:

* Present mode is Fifo, so the median gap is pinned near one vsync and carries
  no information. Only p95, the maximum, and the count of gaps at or beyond a
  quarter-second-scale threshold are reported as headline figures.
* Absolute timings drift substantially between sessions with host load, so a
  reading is only meaningful against a baseline captured in the same session.
  Every invocation therefore interleaves idle runs with gesture runs and reports
  the gesture against its own idle control, never against a stored number.
* A gesture driver that silently does nothing produces a clean, fast run that
  looks like a pass. The idle render is bit-deterministic run to run, so the
  probe requires the gesture run's screenshot to diverge from the idle
  screenshot before it will report a verdict at all. Too little divergence exits
  3 (premise unproven), which must never be read as a pass.

The press and release of a gesture are reported as their own line items and are
excluded from the sustained figures. They are a different defect class from the
steady-state cadence -- a single expensive handler at the transition versus a
drag that cannot hold its frame rate -- and conflating them hides both.

Exit codes: 0 pass, 1 a supplied threshold is violated, 3 the premise is
unproven (no liveness, no frames, no gaps), 77 the app is unavailable.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

FRAME_RE = re.compile(r"\[partial-render\] frame=(\d+) ")

EXIT_OK = 0
EXIT_VIOLATED = 1
EXIT_UNPROVEN = 3
EXIT_SKIP = 77

# The host's scripted drag warms up for a fixed number of frames, dispatches one
# sample per frame, then releases. Press and release therefore land on known
# frame indices. They are options because the schedule belongs to the SDK.
DEFAULT_PRESS_FRAME = 45
DEFAULT_RELEASE_FRAME = 225
DEFAULT_TRANSITION_WINDOW = 2


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = min(len(ordered) - 1, int(q * len(ordered)))
    return ordered[idx]


def summarize(gaps: list[tuple[int, float]]) -> dict:
    values = [g for _, g in gaps]
    return {
        "samples": len(values),
        "p50_ms": round(percentile(values, 0.50), 3),
        "p95_ms": round(percentile(values, 0.95), 3),
        "max_ms": round(max(values), 3) if values else 0.0,
        "ge25_ms": sum(1 for v in values if v >= 25.0),
        "ge100_ms": sum(1 for v in values if v >= 100.0),
        "worst": [[f, round(v, 3)] for f, v in sorted(gaps, key=lambda kv: -kv[1])[:8]],
    }


def split_transitions(gaps, press, release, window):
    """Separate the press/release transition gaps from the sustained cadence."""
    transition, sustained = {}, []
    for frame, value in gaps:
        if abs(frame - press) <= window:
            transition.setdefault("press", []).append([frame, round(value, 3)])
        elif abs(frame - release) <= window:
            transition.setdefault("release", []).append([frame, round(value, 3)])
        else:
            sustained.append((frame, value))
    return transition, sustained


def run_once(app: str, mode: str, frames: int, shot: str) -> dict:
    env = dict(os.environ)
    env["PULP_FRAMES"] = str(frames)
    env["PULP_SCREENSHOT"] = shot
    env["PULP_PARTIAL_RENDERING_DEBUG"] = "1"
    env["SPECTR_BANDS_PERF_FIXTURE"] = "1"
    if mode == "idle":
        env.pop("PULP_TEST_POINTER_DRAG", None)
    else:
        env["PULP_TEST_POINTER_DRAG"] = "1" if mode == "bands" else mode

    stamps: list[tuple[int, float]] = []
    started = time.monotonic()
    proc = subprocess.Popen(
        [app], env=env, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, text=True, bufsize=1,
    )
    assert proc.stderr is not None
    for line in proc.stderr:
        match = FRAME_RE.search(line)
        if match:
            stamps.append((int(match.group(1)), time.monotonic() - started))
    proc.wait()

    gaps = [
        (stamps[i][0], (stamps[i][1] - stamps[i - 1][1]) * 1000.0)
        for i in range(1, len(stamps))
    ]
    return {
        "mode": mode,
        "returncode": proc.returncode,
        "wall_s": round(time.monotonic() - started, 3),
        "painted_frames": len(stamps),
        "screenshot": shot,
        "gaps": gaps,
    }


def divergence(reference: str, candidate: str) -> float | None:
    """Fraction of pixels that differ between two captures, or None if unreadable.

    The idle render is deterministic, so this is a sharp liveness signal: a
    gesture that actually moved something cannot produce a zero here.
    """
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return None
    try:
        a = Image.open(reference).convert("RGB")
        b = Image.open(candidate).convert("RGB")
    except (OSError, ValueError):
        return None
    if a.size != b.size:
        return 1.0
    mask = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 8 else 0)
    histogram = mask.histogram()
    changed = histogram[255]
    return changed / float(a.size[0] * a.size[1])


def self_test() -> int:
    """Confirm the analyzer reacts to a planted stall and stays quiet without one.

    A cadence analyzer that cannot fail is not evidence. Both directions are
    checked, because an analyzer that flags everything is equally useless.
    """
    clean = [(i, 16.7) for i in range(2, 300)]
    stalled = list(clean)
    stalled[120] = (stalled[120][0], 220.0)

    clean_summary = summarize(clean)
    stalled_summary = summarize(stalled)
    failures = []
    if clean_summary["ge100_ms"] != 0:
        failures.append(f"clean series reported {clean_summary['ge100_ms']} stalls")
    if stalled_summary["ge100_ms"] != 1:
        failures.append(f"planted stall not detected (ge100={stalled_summary['ge100_ms']})")
    if stalled_summary["max_ms"] < 219.0:
        failures.append(f"planted stall understated (max={stalled_summary['max_ms']})")

    # The transition split must route a press-window gap out of the sustained set.
    transition, sustained = split_transitions(
        [(44, 18.0), (45, 160.0), (46, 17.0), (120, 17.0)],
        DEFAULT_PRESS_FRAME, DEFAULT_RELEASE_FRAME, DEFAULT_TRANSITION_WINDOW,
    )
    if "press" not in transition:
        failures.append("press-window gap was not separated")
    if any(frame == 45 for frame, _ in sustained):
        failures.append("press-window gap leaked into the sustained set")

    for line in failures:
        print(f"self-test FAIL: {line}", file=sys.stderr)
    if failures:
        return EXIT_VIOLATED
    print("self-test OK: planted stall detected, clean series quiet, transitions split")
    return EXIT_OK


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", help="path to the standalone editor binary")
    parser.add_argument("--mode", default="bands", choices=["bands", "minimap"])
    parser.add_argument("--runs", type=int, default=2,
                        help="idle/gesture pairs to interleave (default 2)")
    parser.add_argument("--frames", type=int, default=420)
    parser.add_argument("--json", dest="json_out")
    parser.add_argument("--press-frame", type=int, default=DEFAULT_PRESS_FRAME)
    parser.add_argument("--release-frame", type=int, default=DEFAULT_RELEASE_FRAME)
    parser.add_argument("--transition-window", type=int, default=DEFAULT_TRANSITION_WINDOW)
    parser.add_argument("--min-divergence", type=float, default=0.005,
                        help="minimum gesture/idle pixel divergence to accept liveness")
    parser.add_argument("--max-sustained-p95-ratio", type=float, default=None,
                        help="fail if gesture sustained p95 exceeds idle p95 by this factor")
    parser.add_argument("--max-sustained-ge25", type=int, default=None,
                        help="fail if the gesture's sustained gaps at or above 25ms exceed this")
    parser.add_argument("--self-test", action="store_true",
                        help="run the analyzer's own negative control and exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.app:
        print("--app is required unless --self-test is given", file=sys.stderr)
        return EXIT_SKIP
    if not os.access(args.app, os.X_OK):
        print(f"no runnable editor at {args.app}", file=sys.stderr)
        return EXIT_SKIP

    tmp = tempfile.mkdtemp(prefix="frame-cadence-")
    idle_runs, gesture_runs = [], []
    for index in range(args.runs):
        idle_runs.append(run_once(args.app, "idle", args.frames,
                                  os.path.join(tmp, f"idle-{index}.png")))
        gesture_runs.append(run_once(args.app, args.mode, args.frames,
                                     os.path.join(tmp, f"{args.mode}-{index}.png")))

    report: dict = {"mode": args.mode, "runs": args.runs, "frames": args.frames,
                    "press_frame": args.press_frame, "release_frame": args.release_frame,
                    "idle": [], "gesture": []}

    for label, runs in (("idle", idle_runs), ("gesture", gesture_runs)):
        for run in runs:
            transition, sustained = split_transitions(
                run["gaps"], args.press_frame, args.release_frame, args.transition_window)
            report[label].append({
                "returncode": run["returncode"],
                "painted_frames": run["painted_frames"],
                "wall_s": run["wall_s"],
                "all": summarize(run["gaps"]),
                "sustained": summarize(sustained),
                "transition": transition,
            })

    unproven = []
    if any(r["painted_frames"] == 0 for r in idle_runs + gesture_runs):
        unproven.append("a run painted no frames")
    if any(not r["gaps"] for r in idle_runs + gesture_runs):
        unproven.append("a run produced no inter-frame gaps")

    divergences = []
    for idle, gesture in zip(idle_runs, gesture_runs):
        value = divergence(idle["screenshot"], gesture["screenshot"])
        divergences.append(value)
    report["divergence"] = divergences
    if any(value is None for value in divergences):
        unproven.append("captures could not be compared (Pillow missing or unreadable)")
    elif max(value for value in divergences if value is not None) < args.min_divergence:
        unproven.append(
            f"gesture captures match idle (max divergence "
            f"{max(v for v in divergences if v is not None):.5f} < {args.min_divergence}); "
            f"the {args.mode} driver did not move anything")

    def median_of(label: str, key: str) -> float:
        return statistics.median(entry["sustained"][key] for entry in report[label])

    report["summary"] = {
        "idle_sustained_p95_ms": round(median_of("idle", "p95_ms"), 3),
        "gesture_sustained_p95_ms": round(median_of("gesture", "p95_ms"), 3),
        "idle_sustained_ge25": int(median_of("idle", "ge25_ms")),
        "gesture_sustained_ge25": int(median_of("gesture", "ge25_ms")),
    }
    idle_p95 = report["summary"]["idle_sustained_p95_ms"]
    gesture_p95 = report["summary"]["gesture_sustained_p95_ms"]
    report["summary"]["sustained_p95_ratio"] = (
        round(gesture_p95 / idle_p95, 3) if idle_p95 else None)

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=1)

    print(json.dumps(report["summary"], indent=1))
    for index, entry in enumerate(report["gesture"]):
        print(f"gesture run {index}: transition {entry['transition']}")

    if unproven:
        for line in unproven:
            print(f"UNPROVEN: {line}", file=sys.stderr)
        return EXIT_UNPROVEN

    violations = []
    ratio = report["summary"]["sustained_p95_ratio"]
    if args.max_sustained_p95_ratio is not None and ratio is not None:
        if ratio > args.max_sustained_p95_ratio:
            violations.append(
                f"sustained p95 ratio {ratio} exceeds {args.max_sustained_p95_ratio}")
    if args.max_sustained_ge25 is not None:
        observed = report["summary"]["gesture_sustained_ge25"]
        if observed > args.max_sustained_ge25:
            violations.append(
                f"sustained gaps >=25ms {observed} exceeds {args.max_sustained_ge25}")

    for line in violations:
        print(f"VIOLATION: {line}", file=sys.stderr)
    return EXIT_VIOLATED if violations else EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
