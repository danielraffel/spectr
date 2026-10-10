#!/usr/bin/env python3
"""Run the production native shot and emit its authenticated parity receipt."""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical(value):
    return json.dumps(canonical_value(value), sort_keys=True,
                      separators=(",", ":"), ensure_ascii=False).encode()


def canonical_value(value):
    if isinstance(value, dict):
        return {key: canonical_value(value[key]) for key in sorted(value)}
    if isinstance(value, list):
        return [canonical_value(item) for item in value]
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value


def fail(message):
    print(f"spectr-native-parity: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-shot", required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--state", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--backend", default="skia")
    args = parser.parse_args()

    shot = Path(args.native_shot).resolve()
    binary = Path(args.binary).resolve()
    state_path = Path(args.state).resolve()
    output = Path(args.output).resolve()
    if not shot.exists() or not binary.exists() or not state_path.exists():
        fail("native shot, binary, and state manifest must exist")
    state = json.loads(state_path.read_text())
    if state.get("schema") != "spectr-parity-state-v1":
        fail("unsupported parity state schema")
    declared_state = state.get("stateSha256")
    unsigned_state = dict(state)
    unsigned_state.pop("stateSha256", None)
    if not declared_state or declared_state != hashlib.sha256(canonical(unsigned_state)).hexdigest():
        fail("invalid parity state digest")

    output.mkdir(parents=True, exist_ok=True)
    png = output / "parity-deterministic-analyzer.png"
    if png.exists():
        png.unlink()
    scale = state.get("viewport", {}).get("deviceScaleFactor", 1)
    run = subprocess.run([
        str(shot), f"--out={output}", f"--backend={args.backend}",
        f"--scale={scale}", "--prefix=parity-",
    ], env={**__import__("os").environ, "SPECTR_DETERMINISTIC_ANALYZER": "1"},
        text=True, capture_output=True)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr)
    if run.returncode == 77:
        raise SystemExit(77)
    if run.returncode != 0:
        fail(f"native shot exited {run.returncode}")
    if "OK  parity-deterministic-analyzer" not in run.stdout:
        fail("native shot did not report a successful deterministic capture")

    if not png.exists():
        fail(f"native shot did not produce {png}")
    with Image.open(png) as image:
        width, height = image.size
    expected = state.get("viewport", {}).get("png")
    if expected and (width, height) != (expected["width"], expected["height"]):
        fail(f"native dimensions {(width, height)} do not match state {expected}")
    receipt = {
        "schema": "spectr-native-shot-receipt-v1",
        "version": 1,
        "stateSha256": declared_state,
        "sourceSha256": state.get("source", {}).get("sha256"),
        "pngSha256": sha256(png),
        "binarySha256": sha256(binary),
        "dimensions": {"width": width, "height": height},
        "backend": args.backend,
        "png": str(png),
        "binary": str(binary),
        "deterministicAnalyzer": True,
    }
    receipt_path = output / "native-receipt.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"spectr-native-parity: receipt {receipt_path}")


if __name__ == "__main__":
    main()
