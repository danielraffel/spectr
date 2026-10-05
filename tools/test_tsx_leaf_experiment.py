#!/usr/bin/env python3
"""Positive and negative controls for the bounded WP-1 TSX leaf experiment."""
from __future__ import annotations
import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLI = ROOT / "tools" / "tsx_leaf_experiment.mjs"
ARTIFACT = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["node", str(CLI), *args], cwd=ROOT, text=True, capture_output=True, check=False)


class TsxLeafExperimentTest(unittest.TestCase):
    def test_mbtn_from_artifact_is_readable_tsx(self):
        with tempfile.TemporaryDirectory() as td:
            output = pathlib.Path(td) / "MBtn.tsx"
            result = run("--artifact", str(ARTIFACT), "--component", "MBtn", "--out", str(output))
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            converted = output.read_text()
            self.assertEqual(report["schema"], "spectr-owned-tsx-leaf-v1")
            self.assertEqual(report["component"], "MBtn")
            self.assertEqual(report["script_index"], 5)
            self.assertIn("<button", converted)
            self.assertIn('data-spectr-manager-action={action}', converted)
            self.assertIn('style={{', converted)
            self.assertNotIn("React.createElement", converted)
            self.assertEqual(report["output_bytes"], len(converted.encode()))

    def test_mbtn_output_is_deterministic(self):
        with tempfile.TemporaryDirectory() as td:
            one, two = pathlib.Path(td) / "one.tsx", pathlib.Path(td) / "two.tsx"
            first = run("--artifact", str(ARTIFACT), "--component", "MBtn", "--out", str(one))
            second = run("--artifact", str(ARTIFACT), "--component", "MBtn", "--out", str(two))
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(one.read_bytes(), two.read_bytes())
            self.assertEqual(json.loads(first.stdout)["output_sha256"], json.loads(second.stdout)["output_sha256"])

    def test_spread_props_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            source = pathlib.Path(td) / "spread.jsx"
            source.write_text('function MBtn(props) { return React.createElement("button", { ...props }); }')
            result = run("--source", str(source), "--component", "MBtn")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("spread props", result.stderr)

    def test_multiple_return_trees_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            source = pathlib.Path(td) / "many.jsx"
            source.write_text('function MBtn({ok}) { if (ok) return React.createElement("button"); return React.createElement("span"); }')
            result = run("--source", str(source), "--component", "MBtn")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("expected one returned createElement tree", result.stderr)


if __name__ == "__main__":
    unittest.main()
