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
    env = os.environ.copy()
    env.update({
        "SPECTR_DETERMINISTIC_ANALYZER": "1",
        "SPECTR_ANALYZER_FIXTURE": str(fixture),
    })
    return subprocess.run([
        str(binary), f"--out={Path(args.output).resolve()}", "--backend=gpu",
    ], env=env).returncode


if __name__ == "__main__":
    raise SystemExit(main())
