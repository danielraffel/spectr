#!/usr/bin/env python3
"""Exercise the browser/native join's receipt-boundary normalization."""

import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

try:
    from join_materialized_runtime_parity import (normalize_viewport,
                                                  require_embedded_artifact,
                                                  require_native_executable,
                                                  state_digest,
                                                  validate_browser_state)
except ModuleNotFoundError as error:
    if error.name != "PIL":
        raise
    print("SKIP: Pillow is required for parity join checks")
    raise SystemExit(77)


def main() -> None:
    assert normalize_viewport({"width": 1320, "height": 860, "dpr": 2}) == {
        "width": 1320,
        "height": 860,
        "deviceScaleFactor": 2,
    }
    assert normalize_viewport({
        "width": 1320,
        "height": 860,
        "deviceScaleFactor": 2,
    }) == {
        "width": 1320,
        "height": 860,
        "deviceScaleFactor": 2,
    }
    try:
        normalize_viewport({
            "width": 1320,
            "height": 860,
            "dpr": 2,
            "deviceScaleFactor": 1,
        })
    except SystemExit as error:
        assert "conflicting dpr and deviceScaleFactor" in str(error)
    else:
        raise AssertionError("conflicting viewport aliases were accepted")

    authority = {
        "browserArtifact": "native-ui/materialized/materialized-document.runtime.json",
        "nativeArtifact": "native-ui/materialized/materialized-document.runtime.json",
        "joinMode": "same-materialized-runtime-diagnostic",
    }
    expected_viewport = {
        "width": 1320,
        "height": 860,
        "deviceScaleFactor": 2,
    }
    expected_dimensions = {"width": 2640, "height": 1720}
    embedded = {
        "schema": "spectr-materialized-browser-state-v1",
        "version": 1,
        "source": {"artifactSha256": "artifact", "htmlSha256": "html", "assetCount": 13},
        "viewport": {**expected_viewport, "png": expected_dimensions},
        "analyzer": {"fixtureSha256": "fixture", "contract": "spectr-parity-v1",
                     "sequences": [1, 2]},
        "environment": {"reactVendorScripts": [
            {"id": "react", "sha256": "a" * 64},
            {"id": "react-dom", "sha256": "b" * 64},
        ]},
        "authority": authority,
        "stateSha256": "placeholder",
    }
    embedded["stateSha256"] = state_digest(embedded)
    browser = {
        "state": embedded,
        "stateSha256": embedded["stateSha256"],
        "source": {"htmlSha256": "html"},
        "authority": authority,
    }
    shared_state = {"authority": authority, "stateSha256": "shared"}
    browser["sharedStateSha256"] = "shared"
    validate_browser_state(browser, shared_state, "artifact", "fixture",
                           expected_viewport, expected_dimensions)
    wrong_authority = {**authority, "joinMode": "wrong-authority"}
    browser["authority"] = wrong_authority
    browser["state"]["authority"] = wrong_authority
    browser["state"]["stateSha256"] = state_digest(browser["state"])
    browser["stateSha256"] = browser["state"]["stateSha256"]
    try:
        validate_browser_state(browser, shared_state, "artifact", "fixture",
                               expected_viewport, expected_dimensions)
    except SystemExit as error:
        assert "authority" in str(error)
    else:
        raise AssertionError("browser authority mismatch was accepted")
    browser["authority"] = authority
    browser["state"]["authority"] = authority
    browser["state"]["stateSha256"] = state_digest(browser["state"])
    browser["stateSha256"] = browser["state"]["stateSha256"]
    browser["state"]["version"] = True
    browser["stateSha256"] = state_digest(browser["state"])
    try:
        validate_browser_state(browser, shared_state, "artifact", "fixture",
                               expected_viewport, expected_dimensions)
    except SystemExit as error:
        assert "unsupported or missing browser state envelope" in str(error)
    else:
        raise AssertionError("boolean browser state version was accepted")
    browser["state"]["version"] = 1
    browser["stateSha256"] = state_digest(browser["state"])
    browser["stateSha256"] = "stale-state"
    try:
        validate_browser_state(browser, shared_state, "artifact", "fixture",
                               expected_viewport, expected_dimensions)
    except SystemExit as error:
        assert "state digest" in str(error)
    else:
        raise AssertionError("stale browser state digest was accepted")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        artifact = root / "runtime.json"
        binary = root / "native-shot"
        artifact.write_bytes(b"materialized-runtime")
        binary.write_bytes(b"Mach-O prefix materialized-runtime suffix")
        require_embedded_artifact(binary, artifact)
        binary.write_bytes(b"Mach-O stale runtime")
        try:
            require_embedded_artifact(binary, artifact)
        except SystemExit as error:
            assert "does not embed the exact source artifact bytes" in str(error)
        else:
            raise AssertionError("stale binary embedding was accepted")
        os.chmod(binary, 0o755)
        try:
            require_native_executable(binary, {"binary": str(binary)})
        except SystemExit as error:
            assert "Mach-O" in str(error)
        else:
            raise AssertionError("non-Mach-O fake binary was accepted")

    print("PASS: materialized parity viewport aliases normalize safely")


if __name__ == "__main__":
    main()
