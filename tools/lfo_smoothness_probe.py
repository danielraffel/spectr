#!/usr/bin/env python3
"""Score modulation-overlay smoothness from a Perfetto trace.

Two independent defects make an animation look glitchy, and only one of them
is visible in frame timing:

  * frame cadence -- the display skipped or stalled.  Measured here as the
    gap between one painted frame ending and the next one starting, which is
    the shape `analyze_interaction_trace.py` is structurally blind to because
    it scores `frame` slice DURATIONS and a gap contains no frame at all.
  * sample cadence -- every frame painted on time, but the VALUE it painted
    did not advance evenly.  A display-rate consumer reading a producer that
    publishes on an unrelated clock resamples a staircase: some ticks repeat
    the previous sample (a frozen frame) and some jump two producer steps at
    once.  Nothing about the frame timing records this.

`spectr_mod_seq` / `spectr_mod_mean_db` are emitted once per UI tick by
`Spectr::tick_native_analyzer_`, unconditionally, so a held tick is recorded
as a real sample rather than as missing data.
"""
from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

FRAME_SQL = """
SELECT s.ts, s.dur FROM slice s
WHERE s.category = 'render' AND s.name = 'frame' AND s.dur >= 0
ORDER BY s.ts;
"""

COUNTER_SQL = """
SELECT t.name, c.ts, c.value FROM counter c
JOIN counter_track t ON c.track_id = t.id
WHERE t.name IN ('spectr_mod_seq', 'spectr_mod_active', 'spectr_mod_mean_db')
ORDER BY c.ts;
"""


def shell() -> str:
    explicit = os.environ.get("PULP_TRACE_PROCESSOR")
    if explicit:
        return explicit
    default = Path.home() / ".pulp/tools/trace-processor/v57.2/mac-arm64/trace_processor_shell"
    if default.exists():
        return str(default)
    sys.exit("trace_processor_shell not found; export PULP_TRACE_PROCESSOR")


def query(trace: Path, sql: str) -> list[list[str]]:
    proc = subprocess.run(
        [shell(), "-q", "/dev/stdin", str(trace)],
        input=sql, capture_output=True, text=True, check=False,
    )
    if proc.returncode != 0:
        sys.exit(f"trace_processor failed: {proc.stderr.strip()[:400]}")
    rows: list[list[str]] = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line or line.startswith('"') and "ts" in line and "dur" in line:
            continue
        parts = [p.strip().strip('"') for p in line.split(",")]
        rows.append(parts)
    return rows


def pct(values: list[float], q: float) -> float:
    if not values:
        return float("nan")
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round(q / 100.0 * len(ordered) + 0.5)) - 1))
    return ordered[index]


def numeric(rows: list[list[str]], width: int) -> list[tuple[float, ...]]:
    out = []
    for row in rows:
        if len(row) < width:
            continue
        try:
            out.append(tuple(float(row[i]) for i in range(width)))
        except ValueError:
            continue
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True, type=Path)
    ap.add_argument("--label", required=True)
    ap.add_argument("--warmup-frames", type=int, default=90,
                    help="frames dropped from the head; startup is not the workload")
    ap.add_argument("--spectr-sha", default="")
    ap.add_argument("--sdk-sha", default="")
    ap.add_argument("--output", type=Path)
    args = ap.parse_args()

    frames = numeric(query(args.trace, FRAME_SQL), 2)
    if len(frames) <= args.warmup_frames + 10:
        sys.exit(f"{args.label}: only {len(frames)} frame slices; instrument is dead, "
                 "not a clean result")
    frames = frames[args.warmup_frames:]

    gaps = []
    starts = []
    durs = [f[1] / 1e6 for f in frames]
    for prev, cur in zip(frames, frames[1:]):
        gaps.append((cur[0] - (prev[0] + prev[1])) / 1e6)
        starts.append((cur[0] - prev[0]) / 1e6)

    counters: dict[str, list[tuple[float, float]]] = {}
    for row in query(args.trace, COUNTER_SQL):
        if len(row) < 3:
            continue
        try:
            counters.setdefault(row[0], []).append((float(row[1]), float(row[2])))
        except ValueError:
            continue

    seq = counters.get("spectr_mod_seq", [])
    active = counters.get("spectr_mod_active", [])
    mean_db = counters.get("spectr_mod_mean_db", [])
    active_from = None
    for ts, value in active:
        if value >= 0.5:
            active_from = ts
            break

    sample: dict[str, object] = {"ticks": len(seq), "active_ticks": 0}
    if active_from is not None:
        live = [(ts, value) for ts, value in seq if ts >= active_from]
        deltas = [int(b[1] - a[1]) for a, b in zip(live, live[1:])]
        live_db = [(ts, value) for ts, value in mean_db if ts >= active_from]
        db_deltas = [abs(b[1] - a[1]) for a, b in zip(live_db, live_db[1:])]
        moving = [d for d in db_deltas if d > 1e-9]
        sample = {
            "ticks": len(seq),
            "active_ticks": len(live),
            "seq_delta_zero": sum(1 for d in deltas if d == 0),
            "seq_delta_min": min(deltas) if deltas else None,
            "seq_delta_max": max(deltas) if deltas else None,
            "seq_delta_mean": round(statistics.fmean(deltas), 4) if deltas else None,
            "value_frozen_ticks": sum(1 for d in db_deltas if d <= 1e-9),
            "value_step_p50_db": round(pct(moving, 50), 6) if moving else None,
            "value_step_p95_db": round(pct(moving, 95), 6) if moving else None,
            "value_step_max_db": round(max(moving), 6) if moving else None,
            # Velocity jitter: how much the painted per-frame excursion varies.
            # 1.0 is perfectly even motion; 2.0 means some frames move twice as
            # far as the median, which reads as judder at any waveform.
            "value_step_jitter_ratio":
                round(max(moving) / pct(moving, 50), 3) if moving and pct(moving, 50) > 0 else None,
        }
        # Velocity, not step: |dv| / dt over the SAME tick pair. A late frame
        # legitimately paints a bigger step, so step jitter alone cannot tell
        # "the display stalled" from "the value path added its own jitter".
        # Velocity divides the frame pacing back out, so with a constant-slope
        # shape (triangle/saw) anything left is the value path.
        pairs = list(zip(live_db, live_db[1:]))
        vel = [abs(b[1] - a[1]) / ((b[0] - a[0]) / 1e6)
               for a, b in pairs if b[0] > a[0] and abs(b[1] - a[1]) > 1e-9]
        if vel:
            sample["value_velocity_db_per_ms"] = {
                "p50": round(pct(vel, 50), 6),
                "p95": round(pct(vel, 95), 6),
                "p99": round(pct(vel, 99), 6),
                "max": round(max(vel), 6),
                "jitter_p95_over_p50": round(pct(vel, 95) / pct(vel, 50), 3)
                    if pct(vel, 50) > 0 else None,
                "jitter_max_over_p50": round(max(vel) / pct(vel, 50), 3)
                    if pct(vel, 50) > 0 else None,
            }

    receipt = {
        "label": args.label,
        "trace": str(args.trace),
        "spectr_sha": args.spectr_sha,
        "sdk_sha": args.sdk_sha,
        "frames_scored": len(frames),
        "frame_dur_ms": {
            "p50": round(pct(durs, 50), 3),
            "p95": round(pct(durs, 95), 3),
            "p99": round(pct(durs, 99), 3),
            "worst": round(max(durs), 3),
        },
        "frame_gap_ms": {
            "p50": round(pct(gaps, 50), 3),
            "p95": round(pct(gaps, 95), 3),
            "p99": round(pct(gaps, 99), 3),
            "worst": round(max(gaps), 3),
            "ge25": sum(1 for g in gaps if g >= 25.0),
            "ge100": sum(1 for g in gaps if g >= 100.0),
        },
        "frame_interval_ms": {
            "p50": round(pct(starts, 50), 3),
            "p95": round(pct(starts, 95), 3),
            "p99": round(pct(starts, 99), 3),
            "worst": round(max(starts), 3),
            "ge25": sum(1 for s in starts if s >= 25.0),
        },
        "modulation_sampling": sample,
    }
    text = json.dumps(receipt, indent=2)
    if args.output:
        args.output.write_text(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
