#!/usr/bin/env python3
"""Run the production native shot and emit its authenticated parity receipt."""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from parity_state_canonical import state_digest

from PIL import Image


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fail(message):
    print(f"spectr-native-parity: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


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
    if shot != binary:
        fail("--binary must identify the executable invoked by --native-shot")
    state = json.loads(state_path.read_text())
    if state.get("schema") != "spectr-parity-state-v1" or not integer(state.get("version")) or state.get("version") != 1:
        fail("unsupported parity state schema")
    viewport = state.get("viewport", {})
    if not integer(viewport.get("width")) or viewport["width"] <= 0 or not integer(viewport.get("height")) or viewport["height"] <= 0:
        fail("viewport width and height must be positive integers")
    scale = viewport.get("deviceScaleFactor")
    if not isinstance(scale, (int, float)) or isinstance(scale, bool) or scale <= 0:
        fail("viewport deviceScaleFactor must be positive")
    declared_state = state.get("stateSha256")
    try:
        computed_state = state_digest(state)
    except (TypeError, ValueError) as error:
        fail(f"invalid canonical parity state: {error}")
    if not declared_state or declared_state != computed_state:
        fail("invalid parity state digest")
    binary_digest = sha256(binary)
    expected_binary = state.get("native", {}).get("binarySha256")
    if not expected_binary or expected_binary != binary_digest:
        fail("native binary SHA does not match parity state before launch")

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
    if "OK  parity-deterministic-analyzer-ready contract=spectr-parity-v1 sequence=2" not in run.stdout:
        fail("native shot did not report the named parity readiness contract")

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
        "binarySha256": binary_digest,
        "dimensions": {"width": width, "height": height},
        "backend": args.backend,
        "png": str(png),
        "binary": str(binary),
        "deterministicAnalyzer": True,
        "readiness": {"contract": "spectr-parity-v1", "analyzerSequence": 2},
    }
    receipt_path = output / "native-receipt.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"spectr-native-parity: receipt {receipt_path}")


if __name__ == "__main__":
    main()
