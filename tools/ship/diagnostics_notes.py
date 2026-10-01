#!/usr/bin/env python3
"""Print the release-notes line for Spectr Diagnostics' kit, only if it moved.

    diagnostics_notes.py --since v1.0.5

Compares tools/ship/diagnostics-kit.json at the previous release tag with the
working tree. When the pinned DiagnosticKit version changed, prints one line
for the "Built with" paragraph of the release notes; when it did not, prints
nothing, so an unchanged kit is never announced. A commit-only change under
the same kit version prints nothing too: DiagnosticKit refuses app changes
without a version bump, so such a change cannot alter the app.

--self-test runs the controls.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

PIN = Path("tools/ship/diagnostics-kit.json")


def notes_line(previous: dict | None, current: dict) -> str:
    if previous is not None and previous.get("version") == current["version"]:
        return ""
    line = f"Spectr Diagnostics is built from DiagnosticKit {current['version']}"
    if previous is not None:
        line += f" (was {previous['version']})"
    return line + "."


def pin_at(ref: str, root: Path) -> dict | None:
    result = subprocess.run(["git", "-C", str(root), "show", f"{ref}:{PIN}"],
                            capture_output=True, text=True)
    return json.loads(result.stdout) if result.returncode == 0 else None


def self_test() -> int:
    a = {"version": "1.1.0", "commit": "a" * 40}
    cases = [
        ("first pinned kit announced", notes_line(None, a) == "Spectr Diagnostics is built from DiagnosticKit 1.1.0."),
        ("unchanged kit not announced", notes_line(a, dict(a)) == ""),
        ("same version, other commit not announced", notes_line(a, {"version": "1.1.0", "commit": "b" * 40}) == ""),
        ("new kit announced with the old one",
         notes_line(a, {"version": "1.2.0", "commit": "c" * 40}) ==
         "Spectr Diagnostics is built from DiagnosticKit 1.2.0 (was 1.1.0)."),
    ]
    failures = 0
    for name, ok in cases:
        print(("PASS " if ok else "FAIL ") + name)
        failures += not ok
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--since", help="the previous release tag, e.g. v1.0.5")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.since:
        print("--since is required", file=sys.stderr)
        return 2
    root = Path(__file__).resolve().parents[2]
    line = notes_line(pin_at(args.since, root), json.loads((root / PIN).read_text()))
    if line:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
