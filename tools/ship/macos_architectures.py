#!/usr/bin/env python3
"""Bind macOS installer eligibility to explicit CMake and Mach-O targets."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

SUPPORTED = {"arm64", "x86_64"}


def architectures(cache: str) -> tuple[str, ...]:
    rows = [line.split("=", 1)[1] for line in cache.splitlines()
            if line.startswith("CMAKE_OSX_ARCHITECTURES:") and "=" in line]
    if len(rows) != 1:
        raise ValueError("CMAKE_OSX_ARCHITECTURES must explicitly name the target architecture")
    values = rows[0].split(";")
    if not values or not set(values) <= SUPPORTED or len(set(values)) != len(values):
        raise ValueError(f"unsupported macOS target architectures: {rows[0]!r}")
    return tuple(sorted(values))


def verify_binary(binary: Path, expected: tuple[str, ...]) -> None:
    actual = subprocess.run(["lipo", "-archs", str(binary)], check=True,
                            capture_output=True, text=True).stdout.split()
    if sorted(actual) != sorted(expected):
        raise ValueError(f"{binary}: Mach-O architectures {actual} != {list(expected)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache", type=Path)
    parser.add_argument("--binary", type=Path)
    args = parser.parse_args()
    expected = architectures(args.cache.read_text())
    if args.binary:
        verify_binary(args.binary, expected)
    print(",".join(expected))


if __name__ == "__main__":
    main()
