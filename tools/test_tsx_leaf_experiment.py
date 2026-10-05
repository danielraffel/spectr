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

    def test_multiple_conditional_return_trees_are_converted(self):
        with tempfile.TemporaryDirectory() as td:
            source = pathlib.Path(td) / "many.jsx"
            source.write_text('function MBtn({ok}) { if (ok) return React.createElement("button", { title: "yes" }); return React.createElement("span", { title: "no" }); }')
            result = run("--source", str(source), "--component", "MBtn")
            self.assertEqual(result.returncode, 0, result.stderr)
            json.loads(result.stdout)
            with tempfile.NamedTemporaryFile() as output:
                written = run("--source", str(source), "--component", "MBtn", "--out", output.name)
                self.assertEqual(written.returncode, 0, written.stderr)
                text = pathlib.Path(output.name).read_text()
            self.assertIn("<button", text)
            self.assertIn("<span", text)
            self.assertNotIn("React.createElement", text)

    def test_nested_callback_returns_are_converted(self):
        with tempfile.TemporaryDirectory() as td:
            source = pathlib.Path(td) / "callback.jsx"
            source.write_text('function MBtn({items}) { return React.createElement("div", null, items.map((item) => item.ok ? React.createElement("button", { title: item.title }) : React.createElement("span", { title: item.title }))); }')
            result = run("--source", str(source), "--component", "MBtn")
            self.assertEqual(result.returncode, 0, result.stderr)
            with tempfile.NamedTemporaryFile() as output:
                written = run("--source", str(source), "--component", "MBtn", "--out", output.name)
                self.assertEqual(written.returncode, 0, written.stderr)
                text = pathlib.Path(output.name).read_text()
            self.assertIn("<button", text)
            self.assertIn("<span", text)
            self.assertNotIn("React.createElement", text)

    def test_dynamic_element_tags_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            source = pathlib.Path(td) / "dynamic.jsx"
            source.write_text('function MBtn({tag}) { return React.createElement(tag, { title: "x" }); }')
            result = run("--source", str(source), "--component", "MBtn")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("dynamic element tag", result.stderr)

    def test_malformed_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            source = pathlib.Path(td) / "malformed.jsx"
            source.write_text('function MBtn({ok}) { return React.createElement("button", { title: ok ? "yes" : "no" };')
            result = run("--source", str(source), "--component", "MBtn")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("parser rejected", result.stderr)


if __name__ == "__main__":
    unittest.main()
