#!/usr/bin/env python3
"""Validate the shared identity contract for Spectr GPU-audio traces.

This is a receipt-side fixture. It does not run a GPU workload. A future
provider can export Perfetto rows as JSONL and use this to prove that every
admitted block has one terminal disposition.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

EVENTS = frozenset({
    "gpu_audio_admission", "gpu_audio_submit", "gpu_audio_completion",
    "gpu_audio_terminal",
})
TERMINAL_DISPOSITIONS = frozenset({
    "gpu_delivered", "cpu_fallback", "silence", "stale_rejected",
    "late_rejected", "device_lost", "cancelled",
})


def _identity(row: dict[str, Any], index: int) -> tuple[int, int]:
    try:
        epoch, sequence = row["stream_epoch"], row["block_sequence"]
        if (isinstance(epoch, bool) or not isinstance(epoch, int)
                or isinstance(sequence, bool) or not isinstance(sequence, int)
                or epoch < 0 or sequence < 0):
            raise ValueError
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(
            f"row {index}: stream_epoch and block_sequence must be "
            "non-negative integers") from exc
    return epoch, sequence


def validate_rows(rows: Iterable[dict[str, Any]]) -> dict[str, Any]:
    """Validate lifecycle ordering and return a compact receipt summary."""
    admitted: set[tuple[int, int]] = set()
    terminal: dict[tuple[int, int], str] = {}
    counts: Counter[str] = Counter()
    total = 0
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"row {index}: expected an object")
        name = row.get("event")
        if name not in EVENTS:
            raise ValueError(f"row {index}: unknown event {name!r}")
        identity = _identity(row, index)
        total += 1
        counts[str(name)] += 1
        if name == "gpu_audio_admission":
            if identity in admitted:
                raise ValueError(f"row {index}: duplicate admission for {identity}")
            admitted.add(identity)
        elif identity not in admitted:
            raise ValueError(f"row {index}: {name} precedes admission for {identity}")
        if name == "gpu_audio_terminal":
            disposition = row.get("terminal_disposition")
            if disposition not in TERMINAL_DISPOSITIONS:
                raise ValueError(
                    f"row {index}: unknown terminal disposition {disposition!r}")
            if identity in terminal:
                raise ValueError(f"row {index}: duplicate terminal for {identity}")
            terminal[identity] = str(disposition)
    orphaned = sorted(set(admitted) - set(terminal))
    if orphaned:
        raise ValueError(f"admitted blocks without terminal disposition: {orphaned}")
    return {
        "admitted_blocks": len(admitted),
        "terminal_blocks": len(terminal),
        "event_counts": dict(sorted(counts.items())),
        "terminal_dispositions": dict(sorted(Counter(terminal.values()).items())),
        "rows": total,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path, help="JSONL lifecycle receipt")
    args = parser.parse_args(argv)
    try:
        rows = (json.loads(line) for line in args.receipt.read_text().splitlines()
                if line.strip())
        summary = validate_rows(rows)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"gpu-audio-contention-trace: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
