#!/usr/bin/env python3
"""Authenticated CTest wrapper for the production deterministic native shot."""

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--fixture-sha256", required=True)
    parser.add_argument("--binary-sha256")
    args = parser.parse_args()
    binary = Path(args.binary).resolve()
    fixture = Path(args.fixture).resolve()
    if not binary.is_file() or not fixture.is_file():
        print("SKIP: native parity binary or fixture is unavailable", file=sys.stderr)
        return 77
    actual = sha256(fixture)
    if actual != args.fixture_sha256:
        print(f"FAIL: deterministic fixture provenance mismatch: {actual} != {args.fixture_sha256}", file=sys.stderr)
        return 1
    binary_digest = sha256(binary)
    if args.binary_sha256 and binary_digest != args.binary_sha256:
        print(f"FAIL: native binary provenance mismatch: {binary_digest} != {args.binary_sha256}", file=sys.stderr)
        return 1
    env = os.environ.copy()
    env.update({
        "SPECTR_DETERMINISTIC_ANALYZER": "1",
        "SPECTR_ANALYZER_FIXTURE": str(fixture),
    })
    result = subprocess.run([
        str(binary), f"--out={Path(args.output).resolve()}", "--backend=gpu",
    ], env=env).returncode
    if result != 0:
        return result
    png = Path(args.output).resolve() / "deterministic-analyzer.png"
    if not png.is_file() or png.stat().st_size < 24:
        print("FAIL: native parity capture did not emit a PNG", file=sys.stderr)
        return 1
    receipt = {
        "schema": "spectr-native-ctest-receipt-v1",
        "binarySha256": binary_digest,
        "analyzerFixtureSha256": actual,
        "pngSha256": sha256(png),
        "png": str(png),
    }
    (Path(args.output).resolve() / "native-ctest-receipt.json").write_text(
        __import__("json").dumps(receipt, indent=2) + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
