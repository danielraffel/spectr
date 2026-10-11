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
from parity_state_canonical import state_digest

from PIL import Image, ImageChops, ImageStat


def digest(value):
    return hashlib.sha256(value).hexdigest()


def fail(message):
    print(f"spectr-parity: FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser-receipt", required=True)
    parser.add_argument("--browser-png", required=True)
    parser.add_argument("--native-png", required=True)
    parser.add_argument("--native-receipt", required=True)
    parser.add_argument("--source-artifact", required=True)
    parser.add_argument("--native-binary", required=True)
    parser.add_argument("--state", required=True,
                        help="JSON state manifest shared by both captures")
    parser.add_argument("--output", required=True)
    parser.add_argument("--plant-negative", action="store_true",
                        help="mutate one comparison pixel; must fail")
    args = parser.parse_args()

    browser_path = Path(args.browser_receipt)
    browser_png_path = Path(args.browser_png).resolve()
    native_path = Path(args.native_png)
    native_receipt_path = Path(args.native_receipt)
    source_artifact = Path(args.source_artifact)
    native_binary = Path(args.native_binary)
    state_path = Path(args.state)
    for path in (browser_path, native_path, native_receipt_path, state_path, source_artifact, native_binary):
        if not path.exists():
            fail(f"missing input: {path}")

    browser = json.loads(browser_path.read_text())
    native = json.loads(native_receipt_path.read_text())
    state = json.loads(state_path.read_text())
    if browser.get("schema") != "spectr-html-cdp-comparison-v1":
        fail("unsupported browser receipt schema")
    if native.get("schema") != "spectr-native-shot-receipt-v1":
        fail("unsupported native receipt schema")
    if native.get("deterministicAnalyzer") is not True:
        fail("native receipt lacks deterministic analyzer proof")
    native_ready = native.get("readiness", {})
    if native_ready.get("contract") != "spectr-parity-v1" or native_ready.get("analyzerSequence") != 2:
        fail("native receipt lacks deterministic parity readiness")
    browser_positive = browser.get("positive", {})
    browser_ready = browser_positive.get("ready", {})
    browser_info = browser_positive.get("info", {})
    if not browser.get("checks", {}).get("strict"):
        fail("browser receipt was not captured in strict mode")
    if browser_ready.get("ready") != "complete":
        fail("browser receipt is not ready")
    if browser_ready.get("sourceEditorReady") is not True:
        fail("browser receipt lacks source editor readiness")
    parity_ready = browser_ready.get("parityReady") or {}
    if not isinstance(parity_ready, dict):
        fail("browser receipt has invalid parity readiness")
    if (parity_ready.get("contract") != "spectr-parity-v1"
            or parity_ready.get("analyzerSequence") != 2
            or parity_ready.get("analyzerAccepted") is not True):
        fail("browser receipt lacks deterministic parity readiness")
    canvas_info = browser_info.get("canvas", [])
    canvas_count = len(canvas_info) if isinstance(canvas_info, list) else canvas_info
    if browser_info.get("rootChildren", 0) < 1 or canvas_count < 1:
        fail("browser receipt has no rendered root/canvas")
    if browser_positive.get("consoleErrors") or browser_positive.get("networkFailures"):
        fail("browser receipt contains console or network errors")
    if state.get("schema") != "spectr-parity-state-v1" or not integer(state.get("version")) or state.get("version") != 1:
        fail("unsupported state schema")
    viewport = state.get("viewport", {})
    width, height, scale = viewport.get("width"), viewport.get("height"), viewport.get("deviceScaleFactor")
    if not integer(width) or width <= 0 or not integer(height) or height <= 0:
        fail("viewport width and height must be positive integers")
    if not isinstance(scale, (int, float)) or isinstance(scale, bool) or scale <= 0:
        fail("viewport deviceScaleFactor must be positive")
    fixed_viewport = browser.get("fixedViewport", {})
    if fixed_viewport != {"width": width, "height": height, "deviceScaleFactor": scale}:
        fail("browser receipt viewport does not match parity state")

    browser_source = Path(browser["positive"]["before"]["path"]).resolve()
    if browser_source != browser_png_path:
        fail("browser receipt PNG path does not match explicit browser PNG")
    if not browser_png_path.exists():
        fail(f"browser PNG missing: {browser_png_path}")
    browser_bytes = browser_png_path.read_bytes()
    native_bytes = native_path.read_bytes()
    if digest(browser_bytes) != browser["positive"]["before"]["sha256"]:
        fail("browser PNG hash does not match its receipt")
    if native.get("pngSha256") != digest(native_bytes):
        fail("native PNG hash does not match its receipt")

    browser_image = Image.open(browser_png_path).convert("RGBA")
    native_image = Image.open(native_path).convert("RGBA")
    if browser_image.size != native_image.size:
        fail(f"dimension mismatch: browser={browser_image.size} native={native_image.size}")

    browser_artifact = browser.get("sourceSha256")
    expected_artifact = state.get("source", {}).get("sha256")
    if not expected_artifact:
        fail("state manifest is missing source.sha256")
    if expected_artifact and browser_artifact != expected_artifact:
        fail("state source SHA does not match browser receipt")
    expected_state = state.get("stateSha256")
    receipt_state = browser.get("stateSha256")
    if not expected_state or not receipt_state:
        fail("state digest is required in both manifest and browser receipt")
    try:
        computed_state = state_digest(state)
    except (TypeError, ValueError) as error:
        fail(f"invalid canonical parity state: {error}")
    if expected_state != computed_state:
        fail("state manifest contains an invalid state digest")
    if receipt_state != expected_state:
        fail("state digest does not match browser receipt")
    if native.get("stateSha256") != expected_state:
        fail("state digest does not match native receipt")
    if native.get("sourceSha256") != expected_artifact:
        fail("source SHA does not match native receipt")
    expected_fixture = state.get("analyzer", {}).get("fixtureSha256")
    if expected_fixture:
        browser_fixture = (browser.get("parityState", {}).get("analyzer", {})
                           .get("fixtureSha256"))
        if browser_fixture != expected_fixture:
            fail("analyzer fixture SHA does not match browser receipt")
        if native.get("analyzerFixtureSha256") != expected_fixture:
            fail("analyzer fixture SHA does not match native receipt")
    if digest(source_artifact.read_bytes()) != expected_artifact:
        fail("source artifact SHA does not match parity state")
    expected_binary = state.get("native", {}).get("binarySha256")
    if not expected_binary or not native.get("binarySha256"):
        fail("native binary SHA is required in state and native receipt")
    if native["binarySha256"] != expected_binary:
        fail("native binary SHA does not match parity state")
    if digest(native_binary.read_bytes()) != expected_binary:
        fail("native binary SHA does not match parity state")
    expected_size = state.get("viewport", {}).get("png")
    if expected_size is not None:
        if not isinstance(expected_size, dict) or not integer(expected_size.get("width")) or expected_size["width"] <= 0 or not integer(expected_size.get("height")) or expected_size["height"] <= 0:
            fail("viewport png dimensions must be positive integers")
    if not expected_size and state.get("viewport", {}).get("width"):
        expected_size = {"width": state["viewport"]["width"] * state["viewport"].get("deviceScaleFactor", 1),
                         "height": state["viewport"]["height"] * state["viewport"].get("deviceScaleFactor", 1)}
    if expected_size:
        expected_dimensions = (expected_size["width"], expected_size["height"])
        if browser_image.size != expected_dimensions:
            fail(f"browser dimensions {browser_image.size} do not match state {expected_dimensions}")
    if native.get("dimensions") != {"width": native_image.width, "height": native_image.height}:
        fail("native receipt dimensions do not match native PNG")

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

    regions = state.get("parityRegions", [])
    if not isinstance(regions, list):
        fail("parityRegions must be an array")
    region_reports = []
    for region in regions:
        region_id = region.get("id")
        box = tuple(region.get(key) for key in ("x", "y", "width", "height"))
        if not region_id or any(isinstance(value, bool) or not isinstance(value, int) for value in box):
            fail("parity region requires id and integer x/y/width/height")
        x, y, width, height = box
        if x < 0 or y < 0 or width <= 0 or height <= 0 or x + width > left.width or y + height > left.height:
            fail(f"parity region is outside image bounds: {region_id}")
        region_diff = diff.crop((x, y, x + width, y + height))
        region_extrema = region_diff.getextrema()
        region_max = max(high for _, high in region_extrema)
        region_pixels = list(region_diff.getdata())
        threshold = region.get("errorThreshold", 0)
        if isinstance(threshold, bool) or not isinstance(threshold, int) or threshold < 0:
            fail(f"parity region errorThreshold must be a non-negative integer: {region_id}")
        region_reports.append({
            "id": region_id,
            "rect": {"x": x, "y": y, "width": width, "height": height},
            "pixelCount": len(region_pixels),
            "differingPixels": sum(any(channel for channel in px) for px in region_pixels),
            "pixelsAboveThreshold": sum(max(px) > threshold for px in region_pixels),
            "errorThreshold": threshold,
            "meanAbsoluteError": sum(ImageStat.Stat(region_diff).mean) / 4,
            "maxAbsoluteError": region_max,
        })

    report = {
        "schema": "spectr-browser-native-parity-v1",
        "version": 1,
        "state": state,
        "stateSha256": computed_state,
        "browser": {
            "receipt": str(browser_path.resolve()),
            "png": str(browser_png_path),
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
            "regions": region_reports,
        },
        "pass": (not args.plant_negative) and exact,
    }
    Path(args.output).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if args.plant_negative:
        return 0
    return 0 if args.plant_negative or exact else 1


if __name__ == "__main__":
    raise SystemExit(main())
