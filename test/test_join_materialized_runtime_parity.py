#!/usr/bin/env python3
"""Exercise the browser/native join's receipt-boundary normalization."""

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

try:
    from join_materialized_runtime_parity import (normalize_viewport,
                                                  require_embedded_artifact,
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
        "stateSha256": "browser-state",
    }
    browser = {
        "state": embedded,
        "stateSha256": "browser-state",
        "source": {"htmlSha256": "html"},
        "authority": authority,
    }
    validate_browser_state(browser, {}, "artifact", "fixture",
                           expected_viewport, expected_dimensions)
    browser["stateSha256"] = "stale-state"
    try:
        validate_browser_state(browser, {}, "artifact", "fixture",
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

    print("PASS: materialized parity viewport aliases normalize safely")


if __name__ == "__main__":
    main()
