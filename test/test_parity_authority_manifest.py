#!/usr/bin/env python3
"""Keep the browser/native parity authority boundary explicit.

The authored Claude HTML and the shipping materialized runtime are different
artifacts. This contract prevents a diagnostic cross-artifact image join from
being mistaken for importer pixel parity.
"""

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "native-ui/capture-states/manifest.json"
EXPECTED_AUTHORITY = {
    "browserArtifact": "resources/editor.html",
    "browserArtifactSha256": "5425add49d538713ef24b25f8a50b15d5f981b2db47fe1c0e880d749a931f558",
    "nativeArtifact": "native-ui/materialized/materialized-document.runtime.json",
    "nativeArtifactSha256": "00a8ca66cd628f3d960ddcf1486ee89e33e432df4691f7f7e409ba4c75ac5212",
    "joinMode": "diagnostic-authority-mismatch",
}


def fail(message):
    raise SystemExit(f"FAIL: {message}")


def validate_manifest(manifest):
    authority = manifest.get("authority")
    if manifest.get("schema") != "spectr-native-parity-matrix-v1":
        fail("unsupported parity matrix schema")
    if not isinstance(authority, dict):
        fail("parity matrix must declare artifact authority")
    if manifest.get("version") != 1:
        fail("unsupported parity matrix version")
    if manifest.get("source") != "../../resources/editor.html":
        fail("parity matrix source must remain the authored browser artifact")
    states = manifest.get("states")
    if not isinstance(states, list) or not states:
        fail("parity matrix must contain at least one state")
    if any(not isinstance(item, dict) or not isinstance(item.get("id"), str)
           or not item["id"] for item in states):
        fail("parity matrix states must have non-empty ids")
    if authority != EXPECTED_AUTHORITY:
        fail(f"authority declaration does not match expected boundary: {authority!r}")
    browser = ROOT / authority["browserArtifact"]
    native = ROOT / authority["nativeArtifact"]
    if not browser.is_file() or not native.is_file():
        fail("declared authority artifact is missing")
    if hashlib.sha256(browser.read_bytes()).hexdigest() != authority["browserArtifactSha256"]:
        fail("browser authority SHA does not match the checked-in artifact")
    if hashlib.sha256(native.read_bytes()).hexdigest() != authority["nativeArtifactSha256"]:
        fail("native authority SHA does not match the checked-in artifact")
    browser_text = browser.read_text()
    native_text = json.loads(native.read_text()).get("html", "")
    if "SpectrFreezeLength" in browser_text:
        fail("browser artifact unexpectedly contains native-only freeze control")
    if "SpectrFreezeLength" not in native_text:
        fail("native artifact no longer contains the mature freeze control")
    if browser_text == native_text:
        fail("authority boundary disappeared; artifacts unexpectedly identical")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--expect-missing-authority", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    if args.expect_missing_authority:
        planted = dict(manifest)
        planted.pop("authority", None)
        try:
            validate_manifest(planted)
        except SystemExit as error:
            assert "authority" in str(error)
            print("PASS: missing authority rejected with the intended diagnostic")
            return
        fail("planted missing authority was unexpectedly accepted")
    validate_manifest(manifest)
    print("PASS: browser/native artifact authority is explicit and diagnostic")


if __name__ == "__main__":
    main()
