#!/usr/bin/env python3
"""Validate and enumerate assets referenced by a materialized document."""

import argparse
import json
import re
from pathlib import Path


def referenced_assets(document: Path, extra_sources: list[Path]) -> list[str]:
    payload = json.loads(document.read_text())
    found: set[str] = set()

    def visit(value):
        if isinstance(value, dict):
            asset = value.get("asset_path")
            if isinstance(asset, str) and asset.startswith("assets/"):
                found.add(asset)
            for child in value.values():
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    visit(payload)
    for source in extra_sources:
        found.update(re.findall(r"assets/[0-9a-f]+\.png", source.read_text()))
    return sorted(found)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("document", type=Path)
    parser.add_argument("asset_root", type=Path)
    parser.add_argument("--source", action="append", type=Path, default=[])
    parser.add_argument("--package-header", type=Path)
    parser.add_argument("--cmake", action="store_true")
    parser.add_argument("--plant-missing", action="store_true")
    args = parser.parse_args()

    assets = referenced_assets(args.document, args.source)
    missing = [asset for asset in assets if not (args.asset_root / asset).is_file()]
    if args.package_header:
        header = args.package_header.read_text()
        packaged = set(re.findall(r'EmbeddedFile\{"([^\"]+)"', header))
        missing.extend(asset for asset in assets if asset not in packaged)
    if args.plant_missing:
        missing.append("assets/__planted_missing_asset__.png")
    if missing:
        for asset in missing:
            print(f"missing materialized asset: {asset}")
        return 1

    if args.cmake:
        for asset in assets:
            print((args.asset_root / asset).as_posix())
    else:
        print(f"materialized assets: {len(assets)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
