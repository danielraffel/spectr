#!/usr/bin/env python3
"""Keep the browser/native parity authority boundary explicit.

The authored Claude HTML and the shipping materialized runtime are different
artifacts. This contract prevents a diagnostic cross-artifact image join from
being mistaken for importer pixel parity.
"""

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "native-ui/capture-states/manifest.json"


def fail(message):
    raise SystemExit(f"FAIL: {message}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plant-missing-authority", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    authority = {} if args.plant_missing_authority else manifest.get("authority")
    if manifest.get("schema") != "spectr-native-parity-matrix-v1":
        fail("unsupported parity matrix schema")
    if not isinstance(authority, dict):
        fail("parity matrix must declare artifact authority")
    expected = {
        "browserArtifact": "resources/editor.html",
        "nativeArtifact": "native-ui/materialized/materialized-document.runtime.json",
        "joinMode": "diagnostic-authority-mismatch",
    }
    if authority != expected:
        fail(f"authority declaration does not match expected boundary: {authority!r}")
    browser = ROOT / authority["browserArtifact"]
    native = ROOT / authority["nativeArtifact"]
    if not browser.is_file() or not native.is_file():
        fail("declared authority artifact is missing")
    browser_text = browser.read_text()
    native_text = json.loads(native.read_text()).get("html", "")
    if "SpectrFreezeLength" in browser_text:
        fail("browser artifact unexpectedly contains native-only freeze control")
    if "SpectrFreezeLength" not in native_text:
        fail("native artifact no longer contains the mature freeze control")
    if browser_text == native_text:
        fail("authority boundary disappeared; artifacts unexpectedly identical")
    print("PASS: browser/native artifact authority is explicit and diagnostic")


if __name__ == "__main__":
    main()
