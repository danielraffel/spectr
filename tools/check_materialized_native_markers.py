#!/usr/bin/env python3
"""Fail-closed contract for selectors required by Spectr's native bridge."""
from __future__ import annotations

import json
import sys
from pathlib import Path


REQUIRED_MARKERS = {
    "data-spectr-settings-open": 1,
    "data-spectr-band-context-menu": 1,
    "data-spectr-menu-root": 1,
}


def check_runtime(path: Path) -> dict[str, int]:
    document = json.loads(path.read_text(encoding="utf-8"))
    html = document.get("html", "")
    counts = {marker: html.count(marker) for marker in REQUIRED_MARKERS}
    missing = [marker for marker, minimum in REQUIRED_MARKERS.items() if counts[marker] < minimum]
    if missing:
        raise ValueError(f"{path}: missing native bridge markers: {', '.join(sorted(missing))}")
    return counts


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(f"usage: {Path(argv[0]).name} RUNTIME_JSON [...]", file=sys.stderr)
        return 2
    try:
        for raw_path in argv[1:]:
            path = Path(raw_path)
            counts = check_runtime(path)
            rendered = ", ".join(f"{key}={counts[key]}" for key in REQUIRED_MARKERS)
            print(f"{path}: PASS ({rendered})")
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
