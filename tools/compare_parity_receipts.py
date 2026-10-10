#!/usr/bin/env python3
"""Join browser/native captures for one explicit parity state.

This is deliberately a small, dependency-light oracle.  It never resizes or
crops images: a geometry mismatch is a failed comparison.  The browser
receipt remains browser evidence and the native PNG remains native evidence;
this tool only joins them after checking their provenance and dimensions.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


def digest(value):
    return hashlib.sha256(value).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def fail(message):
    print(f"spectr-parity: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser-receipt", required=True)
    parser.add_argument("--native-png", required=True)
    parser.add_argument("--state", required=True,
                        help="JSON state manifest shared by both captures")
    parser.add_argument("--output", required=True)
    parser.add_argument("--plant-negative", action="store_true",
                        help="mutate one comparison pixel; must fail")
    args = parser.parse_args()

    browser_path = Path(args.browser_receipt)
    native_path = Path(args.native_png)
    state_path = Path(args.state)
    for path in (browser_path, native_path, state_path):
        if not path.exists():
            fail(f"missing input: {path}")

    browser = json.loads(browser_path.read_text())
    state = json.loads(state_path.read_text())
    if browser.get("schema") != "spectr-html-cdp-comparison-v1":
        fail("unsupported browser receipt schema")
    if state.get("schema") != "spectr-parity-state-v1":
        fail("unsupported state schema")

    browser_source = Path(browser["positive"]["before"]["path"])
    if not browser_source.exists():
        fail(f"browser PNG missing: {browser_source}")
    browser_bytes = browser_source.read_bytes()
    native_bytes = native_path.read_bytes()
    if digest(browser_bytes) != browser["positive"]["before"]["sha256"]:
        fail("browser PNG hash does not match its receipt")

    browser_image = Image.open(browser_source).convert("RGBA")
    native_image = Image.open(native_path).convert("RGBA")
    if browser_image.size != native_image.size:
        fail(f"dimension mismatch: browser={browser_image.size} native={native_image.size}")

    browser_artifact = browser.get("sourceSha256")
    expected_artifact = state.get("source", {}).get("sha256")
    if expected_artifact and browser_artifact != expected_artifact:
        fail("state source SHA does not match browser receipt")

    left = browser_image
    right = native_image
    if args.plant_negative:
        pixel = list(right.getpixel((0, 0)))
        pixel[0] = (pixel[0] + 1) % 256
        right.putpixel((0, 0), tuple(pixel))

    diff = ImageChops.difference(left, right)
    extrema = diff.getextrema()
    max_error = max(high for _, high in extrema)
    differing = sum(1 for px in diff.getdata() if any(channel for channel in px))
    channels = browser_image.width * browser_image.height * 4
    mean_error = sum(ImageStat.Stat(diff).mean) / 4
    exact = differing == 0
    if args.plant_negative and exact:
        fail("planted negative did not change the comparison")

    report = {
        "schema": "spectr-browser-native-parity-v1",
        "version": 1,
        "state": state,
        "stateSha256": digest(canonical(state)),
        "browser": {
            "receipt": str(browser_path.resolve()),
            "png": str(browser_source.resolve()),
            "sha256": digest(browser_bytes),
        },
        "native": {
            "png": str(native_path.resolve()),
            "sha256": digest(native_bytes),
        },
        "dimensions": {"width": browser_image.width, "height": browser_image.height},
        "comparison": {
            "exact": exact,
            "differingPixels": differing,
            "pixelCount": browser_image.width * browser_image.height,
            "meanAbsoluteError": mean_error,
            "maxAbsoluteError": max_error,
            "channelCount": channels,
            "plantedNegative": args.plant_negative,
        },
        "pass": (not args.plant_negative) and exact,
    }
    Path(args.output).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if args.plant_negative:
        return 0
    return 0 if exact else 1


if __name__ == "__main__":
    main()
