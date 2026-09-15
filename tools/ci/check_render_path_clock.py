#!/usr/bin/env python3
"""Prove the zero-latency render path reads no clock.

A schedule expressed in samples is correct at any render speed by
construction. A schedule that consults wall-clock time is correct only while
the host paces at real time — and two of Spectr's three formats (AU v2, CLAP)
never receive an offline flag, so the plugin cannot tell when that stops being
true. The property is therefore not "we remembered to handle offline"; it is
"there is nothing here that could care".

This scans the region between `SPECTR-RENDER-PATH BEGIN` and
`SPECTR-RENDER-PATH END` for wall-clock, sleep, thread and lock symbols.

Exit codes
  0  the region exists, is substantial, and is clean
  1  a forbidden symbol was found in the region
  2  NO VERDICT — the instrument could not measure (markers missing, region
     empty, or the positive control failed), which is NOT a pass

The positive control is the point. A scan that finds nothing proves nothing
unless the same scan, on the same file, demonstrably finds something: this
one re-runs itself over a synthetic copy with a `steady_clock` read spliced
into the region and requires that copy to be rejected. If the control passes
clean, the scanner is broken and the exit code is 2, never 0.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

BEGIN = "SPECTR-RENDER-PATH BEGIN"
END = "SPECTR-RENDER-PATH END"

# Each entry is (compiled pattern, human reason).
FORBIDDEN = [
    (re.compile(r"\bsteady_clock\b"), "wall-clock read"),
    (re.compile(r"\bsystem_clock\b"), "wall-clock read"),
    (re.compile(r"\bhigh_resolution_clock\b"), "wall-clock read"),
    (re.compile(r"\bstd::chrono\b"), "wall-clock arithmetic"),
    (re.compile(r"\bmach_absolute_time\b"), "wall-clock read"),
    (re.compile(r"\bclock_gettime\b"), "wall-clock read"),
    (re.compile(r"\bQueryPerformanceCounter\b"), "wall-clock read"),
    (re.compile(r"\bgettimeofday\b"), "wall-clock read"),
    (re.compile(r"\bthis_thread\b"), "sleep or yield"),
    (re.compile(r"\bsleep_for\b"), "sleep"),
    (re.compile(r"\bsleep_until\b"), "sleep"),
    (re.compile(r"\bstd::thread\b"), "thread handle on the audio path"),
    (re.compile(r"\block_guard\b"), "lock on the audio path"),
    (re.compile(r"\bunique_lock\b"), "lock on the audio path"),
    (re.compile(r"\bstd::mutex\b"), "lock on the audio path"),
    (re.compile(r"\.lock\(\)"), "lock on the audio path"),
]

# Minimum meaningful region size. A file whose markers bracket three lines is
# a file whose markers were moved, not a file that got simpler.
MINIMUM_REGION_LINES = 20


def extract_region(text: str) -> tuple[int, int] | None:
    lines = text.splitlines()
    begin = end = None
    for index, line in enumerate(lines):
        if BEGIN in line and begin is None:
            begin = index
        elif END in line and begin is not None and end is None:
            end = index
    if begin is None or end is None or end <= begin:
        return None
    return begin, end


def scan(text: str) -> tuple[str, list[str]]:
    """Return (status, findings). status is one of ok / dirty / no-verdict."""
    span = extract_region(text)
    if span is None:
        return "no-verdict", [f"markers not found ({BEGIN} / {END})"]
    begin, end = span
    lines = text.splitlines()
    region = lines[begin + 1 : end]
    if len(region) < MINIMUM_REGION_LINES:
        return "no-verdict", [
            f"region is {len(region)} lines, below the {MINIMUM_REGION_LINES}-line "
            "floor — the markers no longer bracket the render path"
        ]
    findings = []
    for offset, line in enumerate(region):
        code = line.split("//", 1)[0]
        for pattern, reason in FORBIDDEN:
            if pattern.search(code):
                findings.append(
                    f"{begin + 2 + offset}: {reason}: {line.strip()}"
                )
    return ("dirty" if findings else "ok"), findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument(
        "--skip-control",
        action="store_true",
        help="internal: used by the control run itself",
    )
    args = parser.parse_args()

    if not args.source.is_file():
        print(f"NO VERDICT: {args.source} is not a file")
        return 2
    text = args.source.read_text(encoding="utf-8")

    status, findings = scan(text)

    if not args.skip_control:
        # Positive control: the same scanner, the same file, one clock read
        # spliced into the region. It MUST come back dirty.
        span = extract_region(text)
        if span is None:
            print(f"NO VERDICT: {args.source}: markers not found")
            return 2
        lines = text.splitlines()
        planted = list(lines)
        planted.insert(
            span[0] + 1,
            "        const auto planted = std::chrono::steady_clock::now();",
        )
        control_status, control_findings = scan("\n".join(planted))
        if control_status != "dirty":
            print(
                "NO VERDICT: the positive control was not rejected "
                f"(status={control_status}); the scanner cannot see a clock read, "
                "so a clean result from it means nothing"
            )
            return 2
        print(f"control: planted clock read rejected ({control_findings[0]})")

    if status == "no-verdict":
        for finding in findings:
            print(f"NO VERDICT: {args.source}: {finding}")
        return 2
    if status == "dirty":
        print(f"FAIL: {args.source}: the render path reads a clock or blocks")
        for finding in findings:
            print(f"  {args.source}:{finding}")
        return 1

    span = extract_region(text)
    assert span is not None
    print(
        f"PASS: {args.source}: render path lines "
        f"{span[0] + 2}-{span[1]} carry no clock, sleep, thread or lock"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
