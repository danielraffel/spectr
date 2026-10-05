#!/usr/bin/env python3
"""Positive and negative controls for the WP-1 TSX conversion report."""
from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLI = ROOT / "tools" / "wp1_tsx_conversion_report.mjs"
ARTIFACT = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"
EXPECTED_BLOCKED = {
    "ContextMenu": "dynamic-element-tag",
    "PatternManager": "dynamic-element-tag",
    "SpectrLengthScrollbar": "unsupported-prop-shape",
    "SpectrKnob": "unsupported-prop-shape",
    "Chrome": "dynamic-element-tag",
    "PickerDropdown": "dynamic-element-tag",
    "ThemeDropdown": "dynamic-element-tag",
    "MetaphorDropdown": "dynamic-element-tag",
    "SpectrModulationSettings": "non-object-props",
}


def run(artifact: pathlib.Path, output: pathlib.Path | None = None) -> subprocess.CompletedProcess[str]:
    args = ["node", str(CLI), "--artifact", str(artifact)]
    if output is not None:
        args.extend(["--out", str(output)])
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True,
                          check=False, timeout=90)


class ConversionReportTest(unittest.TestCase):
    def test_current_artifact_has_deterministic_blocker_inventory(self):
        before = ARTIFACT.read_bytes()
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            one, two = root / "one.json", root / "two.json"
            first = run(ARTIFACT, one)
            second = run(ARTIFACT, two)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(one.read_bytes(), two.read_bytes())
            report = json.loads(one.read_text())
            self.assertEqual(report["schema"], "spectr-owned-tsx-conversion-report-v1")
            self.assertEqual(report["artifact"]["sha256"], hashlib.sha256(before).hexdigest())
            self.assertEqual(report["counts"], {
                "components": 63,
                "converted": 54,
                "blocked": 9,
                "blocked_by_category": {
                    "dynamic-element-tag": 6,
                    "non-object-props": 1,
                    "unsupported-prop-shape": 2,
                },
            })
            blocked = {entry["name"]: entry["rejection"]["category"]
                       for entry in report["components"] if entry["status"] == "blocked"}
            self.assertEqual(blocked, EXPECTED_BLOCKED)
            for entry in report["components"]:
                self.assertEqual(entry["end"] - entry["start"], entry["bytes"])
            self.assertEqual(ARTIFACT.read_bytes(), before)

    def test_mutated_source_is_reported_with_new_identity_and_rejection(self):
        document = json.loads(ARTIFACT.read_text())
        needle = 'React.createElement("button", { "data-spectr-manager-action": action, onClick, style: {'
        self.assertEqual(document["html"].count(needle), 1)
        document["html"] = document["html"].replace(needle, 'React.createElement("button", { ...props, style: {', 1)
        with tempfile.TemporaryDirectory() as td:
            artifact = pathlib.Path(td) / "mutated.json"
            output = pathlib.Path(td) / "report.json"
            artifact.write_text(json.dumps(document, separators=(",", ":")))
            result = run(artifact, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text())
            self.assertNotEqual(report["artifact"]["sha256"], hashlib.sha256(ARTIFACT.read_bytes()).hexdigest())
            mbtn = next(entry for entry in report["components"] if entry["name"] == "MBtn")
            self.assertEqual(mbtn["status"], "blocked")
            self.assertEqual(mbtn["rejection"]["category"], "spread-props")
            self.assertEqual(report["counts"]["blocked"], 10)

    def test_malformed_artifact_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            artifact = pathlib.Path(td) / "bad.json"
            artifact.write_text(json.dumps({"schema": "broken"}))
            result = run(artifact)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("artifact html must be a non-empty string", result.stderr)


if __name__ == "__main__":
    unittest.main()
