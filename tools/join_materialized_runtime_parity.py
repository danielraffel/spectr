#!/usr/bin/env python3
"""Join the materialized-runtime Chromium and native captures.

The browser oracle and the native shot have different receipt schemas because
they prove different host paths. This join is the narrow, authority-aware
boundary between them: it authenticates the checked-in materialized artifact,
the deterministic analyzer fixture, the viewport, the native binary, and the
image dimensions before reporting pixel metrics. A diagnostic run may record a
measured mismatch; it never turns one into a pass or changes the thresholds.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageStat
except ModuleNotFoundError as error:
    if error.name != "PIL":
        raise
    # The provenance/receipt boundary helpers are dependency free.  Keep
    # those checks runnable in the lean CTest Python while making an actual
    # image join fail clearly when Pillow is unavailable.
    Image = ImageChops = ImageStat = None

from parity_state_canonical import state_digest


SCHEMA = "spectr-browser-native-materialized-parity-v1"


def fail(message: str) -> None:
    raise SystemExit(f"spectr-materialized-parity: FAIL: {message}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_embedded_artifact(binary: Path, artifact: Path) -> None:
    """Prove the executable actually carries the checked-in runtime bytes.

    The native target embeds ``materialized-document.runtime.json`` as a
    binary-data section.  Matching only the executable's SHA would allow a
    stale binary to be paired with a newly selected artifact in a hand-built
    state manifest.  Require the exact artifact byte sequence to occur in the
    executable before accepting the join.
    """
    source = artifact.read_bytes()
    image = binary.read_bytes()
    if source not in image:
        fail("native binary does not embed the exact source artifact bytes")


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        fail(f"{label} is missing: {path}")


def integer(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def receipt_path(receipt: dict, *keys: str) -> Path | None:
    value = receipt
    for key in keys:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return Path(value).resolve() if isinstance(value, str) else None


def dimensions(image: Image.Image) -> dict:
    return {"width": image.width, "height": image.height}


def normalize_viewport(viewport: object) -> dict:
    """Normalize the browser receipt's viewport into the state schema.

    The CDP oracle records the runtime value as ``dpr`` while the durable
    parity state uses the more explicit ``deviceScaleFactor`` field.  Treating
    those as separate authorities made an otherwise valid join fail before it
    reached image comparison.  Keep the normalization narrow and reject a
    receipt that declares conflicting aliases rather than choosing one.
    """
    if not isinstance(viewport, dict):
        fail("browser receipt viewport is not an object")
    normalized = dict(viewport)
    dpr = normalized.pop("dpr", None)
    scale = normalized.get("deviceScaleFactor")
    if dpr is not None:
        if scale is not None and scale != dpr:
            fail("browser viewport declares conflicting dpr and deviceScaleFactor")
        normalized["deviceScaleFactor"] = dpr
    return normalized


def validate_browser_state(browser: dict, state: dict, source_sha: str,
                           fixture_sha: str, expected_viewport: dict,
                           expected_dimensions: dict) -> None:
    """Authenticate the oracle's own state envelope before joining images.

    Browser and native receipts intentionally use different state schemas:
    the browser envelope carries the materialized HTML/asset identity while
    the native manifest carries the executable binary and regional thresholds.
    They still must agree on the shared authority fields.  Without this check
    a stale browser receipt could pass the outer SHA checks while its embedded
    state described a different viewport or fixture.
    """
    embedded = browser.get("state")
    if not isinstance(embedded, dict) \
            or embedded.get("schema") != "spectr-materialized-browser-state-v1" \
            or embedded.get("version") != 1:
        fail("unsupported or missing browser state envelope")
    if browser.get("stateSha256") != embedded.get("stateSha256"):
        fail("browser receipt state digest does not match its embedded state")
    embedded_source = embedded.get("source", {})
    if embedded_source.get("artifactSha256") != source_sha:
        fail("browser embedded state artifact SHA does not match state manifest")
    if embedded_source.get("htmlSha256") != browser.get("source", {}).get("htmlSha256"):
        fail("browser embedded state HTML SHA does not match its receipt")
    if not integer(embedded_source.get("assetCount")) \
            or embedded_source.get("assetCount") <= 0:
        fail("browser embedded state lacks a positive asset count")
    embedded_analyzer = embedded.get("analyzer", {})
    if embedded_analyzer.get("fixtureSha256") != fixture_sha \
            or embedded_analyzer.get("contract") != "spectr-parity-v1" \
            or embedded_analyzer.get("sequences") != [1, 2]:
        fail("browser embedded analyzer state does not match the fixture authority")
    embedded_viewport = normalize_viewport(embedded.get("viewport", {}))
    embedded_dimensions = embedded_viewport.pop("png", None)
    if embedded_viewport != expected_viewport:
        fail(f"browser embedded viewport {embedded_viewport} does not match state {expected_viewport}")
    if embedded_dimensions != expected_dimensions:
        fail("browser embedded PNG dimensions do not match state manifest")
    if embedded.get("authority") != browser.get("authority"):
        fail("browser embedded authority does not match its receipt")
    vendors = embedded.get("environment", {}).get("reactVendorScripts")
    if not isinstance(vendors, list) or len(vendors) != 2:
        fail("browser state lacks the two authenticated React vendor scripts")
    for vendor in vendors:
        if (not isinstance(vendor, dict) or not isinstance(vendor.get("id"), str)
                or not isinstance(vendor.get("sha256"), str)
                or len(vendor["sha256"]) != 64):
            fail("browser state contains an invalid React vendor identity")


def image_metrics(left: Image.Image, right: Image.Image, regions: list[dict]) -> dict:
    if Image is None:
        fail("Pillow is required for image parity comparison")
    if left.size != right.size:
        fail(f"image dimensions differ: browser={left.size}, native={right.size}")
    diff = ImageChops.difference(left, right)
    extrema = diff.getextrema()
    differing = sum(1 for pixel in diff.getdata() if any(pixel))
    mean_error = sum(ImageStat.Stat(diff).mean) / 4.0
    reports = []
    for region in regions:
        region_id = region.get("id")
        box = tuple(region.get(key) for key in ("x", "y", "width", "height"))
        if not region_id or any(not integer(value) for value in box):
            fail(f"invalid parity region: {region!r}")
        x, y, width, height = box
        if x < 0 or y < 0 or width <= 0 or height <= 0 \
                or x + width > left.width or y + height > left.height:
            fail(f"parity region is outside image bounds: {region_id}")
        region_diff = diff.crop((x, y, x + width, y + height))
        region_pixels = list(region_diff.getdata())
        region_mean = sum(ImageStat.Stat(region_diff).mean) / 4.0
        threshold = region.get("errorThreshold", 0)
        if not integer(threshold) or threshold < 0:
            fail(f"invalid errorThreshold for region {region_id}")
        reports.append({
            "id": region_id,
            "rect": {"x": x, "y": y, "width": width, "height": height},
            "pixelCount": len(region_pixels),
            "differingPixels": sum(1 for pixel in region_pixels if any(pixel)),
            "pixelsAboveThreshold": sum(1 for pixel in region_pixels
                                         if max(pixel) > threshold),
            "errorThreshold": threshold,
            "meanAbsoluteError": region_mean,
            "maxAbsoluteError": max(high for _, high in region_diff.getextrema()),
        })
    return {
        "exact": differing == 0,
        "differingPixels": differing,
        "pixelCount": left.width * left.height,
        "channels": left.width * left.height * 4,
        "meanAbsoluteError": mean_error,
        "maxAbsoluteError": max(high for _, high in extrema),
        "regions": reports,
    }


def authenticate(browser: dict, native: dict, state: dict, artifact: Path,
                 binary: Path, fixture: Path, browser_png: Path,
                 native_png: Path) -> dict:
    if browser.get("schema") != "spectr-materialized-runtime-browser-receipt-v1":
        fail("unsupported browser receipt schema")
    if native.get("schema") != "spectr-native-shot-receipt-v1":
        fail("unsupported native receipt schema")
    if native.get("deterministicAnalyzer") is not True:
        fail("native receipt lacks deterministic analyzer proof")
    if native.get("stateSha256") != state.get("stateSha256"):
        fail("native receipt state digest does not match the shared state manifest")
    readiness = native.get("readiness", {})
    if readiness.get("contract") != "spectr-parity-v1" \
            or readiness.get("analyzerSequence") != 2:
        fail("native receipt lacks sequence-2 analyzer readiness")
    if not browser.get("checks", {}).get("strict") \
            or not browser.get("checks", {}).get("repeatedSettledCapture"):
        fail("browser receipt is not strict and repeatable")
    if browser.get("checks", {}).get("consoleNetworkClean") is not True:
        fail("browser receipt lacks clean console/network proof")
    if state.get("schema") != "spectr-parity-state-v1" \
            or state.get("version") != 1:
        fail("unsupported parity state manifest")
    try:
        computed_state = state_digest(state)
    except (TypeError, ValueError) as error:
        fail(f"invalid parity state digest: {error}")
    if state.get("stateSha256") != computed_state:
        fail("parity state digest is invalid")

    source = state.get("source", {})
    source_sha = source.get("sha256")
    artifact_sha = sha256(artifact)
    if not source_sha or source_sha != artifact_sha:
        fail("source artifact SHA does not match state manifest")
    if browser.get("source", {}).get("artifactSha256") != source_sha:
        fail("browser artifact SHA does not match state manifest")
    if native.get("sourceSha256") != source_sha:
        fail("native artifact SHA does not match state manifest")
    fixture_sha = state.get("analyzer", {}).get("fixtureSha256")
    if not fixture_sha or sha256(fixture) != fixture_sha:
        fail("analyzer fixture SHA does not match state manifest")
    if browser.get("source", {}).get("fixtureSha256") != fixture_sha \
            or native.get("analyzerFixtureSha256") != fixture_sha:
        fail("analyzer fixture SHA does not match both receipts")

    expected_binary = state.get("native", {}).get("binarySha256")
    binary_sha = sha256(binary)
    if not expected_binary or binary_sha != expected_binary \
            or native.get("binarySha256") != expected_binary:
        fail("native binary SHA does not match state manifest")
    require_embedded_artifact(binary, artifact)

    viewport = state.get("viewport", {})
    expected_viewport = {
        "width": viewport.get("width"), "height": viewport.get("height"),
        "deviceScaleFactor": viewport.get("deviceScaleFactor"),
    }
    expected_dimensions = viewport.get("png")
    if not isinstance(expected_dimensions, dict) \
            or not integer(expected_dimensions.get("width")) \
            or not integer(expected_dimensions.get("height")):
        fail("state manifest lacks PNG dimensions")
    validate_browser_state(browser, state, source_sha, fixture_sha,
                           expected_viewport, expected_dimensions)
    browser_viewport = normalize_viewport(
        browser.get("browser", {}).get("viewport", {}))
    if browser_viewport != expected_viewport:
        fail(f"browser viewport {browser_viewport} does not match state {expected_viewport}")
    if native.get("dimensions") != expected_dimensions:
        fail("native receipt dimensions do not match state manifest")

    authority = browser.get("authority", {})
    if authority.get("browserArtifact") != authority.get("nativeArtifact"):
        fail("browser/native authority identities differ")

    if native.get("pngSha256") != sha256(native_png):
        fail("native PNG SHA does not match receipt")
    settled = receipt_path(browser, "browser", "screenshots", "settled", "path")
    if settled is not None and settled != browser_png:
        fail("explicit browser PNG is not the settled image in its receipt")
    if browser.get("browser", {}).get("screenshots", {}).get("settled", {}).get("sha256") \
            != sha256(browser_png):
        fail("browser settled PNG SHA does not match receipt")
    with Image.open(browser_png) as image:
        browser_image = image.convert("RGBA")
    with Image.open(native_png) as image:
        native_image = image.convert("RGBA")
    if dimensions(browser_image) != expected_dimensions \
            or dimensions(native_image) != expected_dimensions:
        fail("capture dimensions do not match state manifest")
    return {
        "stateSha256": computed_state,
        "sourceSha256": source_sha,
        "fixtureSha256": fixture_sha,
        "binarySha256": expected_binary,
        "authority": authority,
        "viewport": expected_viewport,
        "dimensions": expected_dimensions,
        "browserImage": browser_image,
        "nativeImage": native_image,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser-receipt", required=True)
    parser.add_argument("--native-receipt", required=True)
    parser.add_argument("--state", required=True)
    parser.add_argument("--source-artifact", required=True)
    parser.add_argument("--native-binary", required=True)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--browser-png")
    parser.add_argument("--native-png")
    parser.add_argument("--output", required=True)
    parser.add_argument("--diagnostic", action="store_true",
                        help="emit a measured mismatch without failing the process")
    parser.add_argument("--plant-negative", action="store_true",
                        help="mutate one pixel and require the comparator to notice")
    args = parser.parse_args(argv)

    browser_path = Path(args.browser_receipt).resolve()
    native_path = Path(args.native_receipt).resolve()
    state_path = Path(args.state).resolve()
    artifact = Path(args.source_artifact).resolve()
    binary = Path(args.native_binary).resolve()
    fixture = Path(args.fixture).resolve()
    for path, label in ((browser_path, "browser receipt"),
                        (native_path, "native receipt"), (state_path, "state manifest"),
                        (artifact, "source artifact"), (binary, "native binary"),
                        (fixture, "analyzer fixture")):
        require_file(path, label)
    browser = json.loads(browser_path.read_text())
    native = json.loads(native_path.read_text())
    state = json.loads(state_path.read_text())
    browser_png = Path(args.browser_png).resolve() if args.browser_png \
        else receipt_path(browser, "browser", "screenshots", "settled", "path")
    native_png = Path(args.native_png).resolve() if args.native_png \
        else receipt_path(native, "png")
    if browser_png is None or native_png is None:
        fail("capture PNG paths are missing")
    require_file(browser_png, "browser PNG")
    require_file(native_png, "native PNG")

    auth = authenticate(browser, native, state, artifact, binary, fixture,
                        browser_png, native_png)
    right = auth["nativeImage"]
    if args.plant_negative:
        pixel = list(right.getpixel((0, 0)))
        pixel[0] = (pixel[0] + 1) % 256
        right = right.copy()
        right.putpixel((0, 0), tuple(pixel))
    comparison = image_metrics(auth["browserImage"], right,
                               state.get("parityRegions", []))
    if args.plant_negative:
        if comparison["differingPixels"] == 0:
            fail("planted negative did not change the comparison")
        status = "negative_control_passed"
    elif comparison["exact"]:
        status = "exact_pass"
    else:
        status = "diagnostic_fail"
    receipt = {
        "schema": SCHEMA,
        "version": 1,
        "status": status,
        "stateSha256": auth["stateSha256"],
        "sourceSha256": auth["sourceSha256"],
        "fixtureSha256": auth["fixtureSha256"],
        "binarySha256": auth["binarySha256"],
        "authority": auth["authority"],
        "viewport": auth["viewport"],
        "dimensions": auth["dimensions"],
        "browser": {"receipt": str(browser_path), "png": str(browser_png),
                     "sha256": sha256(browser_png)},
        "native": {"receipt": str(native_path), "png": str(native_png),
                    "sha256": sha256(native_png),
                    "backend": native.get("backend")},
        "comparison": comparison,
        "scope": {"sameMaterializedArtifact": True,
                  "productionCutover": False,
                  "pixelParity": comparison["exact"]},
    }
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))
    if not args.plant_negative and not args.diagnostic and not comparison["exact"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
