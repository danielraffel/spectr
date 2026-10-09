#!/usr/bin/env python3
"""Embed the generated runtime-client facade into the maintained service source."""
from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERVICE = ROOT / "native-ui" / "materialized" / "spectr-native-services.js"
INLINE = ROOT / "native-ui" / "materialized" / "generated" / "spectr-runtime-client.inline.js"
BEGIN = "  // BEGIN GENERATED SPECTR RUNTIME CLIENT"
END = "  // END GENERATED SPECTR RUNTIME CLIENT"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    source = SERVICE.read_text()
    inline = INLINE.read_text().rstrip("\n")
    if source.count(BEGIN) != 1 or source.count(END) != 1:
        raise SystemExit("runtime client markers must occur exactly once in service source")
    begin = source.index(BEGIN)
    end = source.index(END, begin)
    if begin > end:
        raise SystemExit("runtime client marker order is invalid")
    replacement = f"{BEGIN}\n{inline}\n{END}"
    updated = source[:begin] + replacement + source[end + len(END):]
    if updated != source:
        if args.check:
            raise SystemExit("generated runtime client facade is stale; run this script without --check")
        SERVICE.write_text(updated)
    print("Generated runtime client facade matches service source.")


if __name__ == "__main__":
    main()
