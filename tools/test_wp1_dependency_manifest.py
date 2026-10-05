#!/usr/bin/env python3
"""Focused positive and negative controls for the WP-1 parser experiment."""
from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLI = ROOT / "tools" / "wp1_dependency_manifest.mjs"
PARSER_PACKAGE = ROOT / "tools" / "wp1-parser" / "package.json"
FIXTURE = ROOT / "tools" / "fixtures" / "wp1-nested-components.jsx"
ARTIFACT = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"
EXPECTED_FIXTURE = ROOT / "tools" / "fixtures" / "wp1-nested-components.manifest.json"
EXPECTED_MBTN = ROOT / "planning" / "Spectr-WP1-MBtn-dependency.manifest.json"


def run_manifest(*, source: pathlib.Path | None = None,
                 artifact: pathlib.Path | None = None,
                 root: str) -> subprocess.CompletedProcess[str]:
    if (source is None) == (artifact is None):
        raise AssertionError("choose exactly one input")
    input_flag = "--source" if source is not None else "--artifact"
    input_path = source or artifact
    return subprocess.run(
        ["node", str(CLI), input_flag, str(input_path), "--root", root],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


class ParserDependencyManifestTest(unittest.TestCase):
    def test_fixture_nested_arrow_closure_is_deterministic(self):
        first = run_manifest(source=FIXTURE, root="Panel")
        second = run_manifest(source=FIXTURE, root="Panel")
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(json.loads(first.stdout), json.loads(second.stdout))

        manifest = json.loads(first.stdout)
        self.assertEqual(manifest["schema"], "spectr-owned-component-dependency-v1")
        self.assertEqual(manifest["parser"]["name"], "@babel/parser")
        self.assertEqual(manifest["parser"]["plugins"], ["jsx", "typescript"])
        self.assertEqual(manifest["component_count"], 3)
        components = {entry["name"]: entry for entry in manifest["components"]}
        self.assertEqual(set(components), {"Panel", "Inner", "MBtn"})
        panel = components["Panel"]
        inner = components["Inner"]
        mbtn = components["MBtn"]
        self.assertEqual(panel["kind"], "function")
        self.assertEqual(inner["kind"], "arrow")
        self.assertEqual(inner["owner"], panel["id"])
        self.assertEqual(panel["dependencies"], [inner["id"]])
        self.assertEqual(inner["dependencies"], [mbtn["id"]])
        self.assertEqual(inner["captures"], ["label"])
        self.assertEqual(mbtn["dependencies"], [])
        self.assertEqual(mbtn["captures"], [])
        for entry in components.values():
            self.assertEqual(entry["unresolved"], [])
            self.assertRegex(entry["id"], r"^component:[A-Za-z_$][\w$]*:[0-9a-f]{64}$")

    def test_runtime_nested_arrow_component_is_parsed_with_owner(self):
        result = run_manifest(artifact=ARTIFACT, root="Group")
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(result.stdout)
        self.assertEqual(manifest["component_count"], 1)
        entry = manifest["components"][0]
        self.assertEqual(entry["name"], "Group")
        self.assertEqual(entry["kind"], "arrow")
        self.assertTrue(entry["owner"].startswith("component:SettingsModal:"))
        self.assertEqual(entry["unresolved"], [])

    def test_runtime_mbtn_leaf_manifest_has_no_unresolved_refs(self):
        result = run_manifest(artifact=ARTIFACT, root="MBtn")
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads(result.stdout)
        self.assertEqual(manifest["source"]["kind"], "artifact")
        self.assertEqual(manifest["source"]["path"], ARTIFACT.name)
        self.assertEqual(manifest["component_count"], 1)
        entry = manifest["components"][0]
        self.assertEqual(entry["name"], "MBtn")
        self.assertEqual(entry["kind"], "function")
        self.assertEqual(entry["dependencies"], [])
        self.assertEqual(entry["captures"], [])
        self.assertEqual(entry["external_bindings"], ["React"])
        self.assertEqual(entry["unresolved"], [])

        expected = json.loads(EXPECTED_MBTN.read_text())
        self.assertEqual(manifest, expected)

    def test_unresolved_nested_reference_fails_closed(self):
        source = FIXTURE.read_text()
        self.assertIn("MBtn, {", source)
        with tempfile.TemporaryDirectory() as td:
            bad = pathlib.Path(td) / "missing.jsx"
            bad.write_text(source.replace("MBtn, {", "MissingButton, {", 1))
            result = run_manifest(source=bad, root="Panel")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unresolved identifiers for Inner: MissingButton", result.stderr)
        self.assertNotIn('"component_count"', result.stdout)

    def test_parser_rejects_malformed_source(self):
        with tempfile.TemporaryDirectory() as td:
            bad = pathlib.Path(td) / "malformed.jsx"
            bad.write_text("function Broken( { return React.createElement('div'); }\n")
            result = run_manifest(source=bad, root="Broken")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("parser rejected", result.stderr)

    def test_checked_in_fixture_manifest_matches_parser_output(self):
        result = run_manifest(source=FIXTURE, root="Panel")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), json.loads(EXPECTED_FIXTURE.read_text()))

    def test_parser_dependency_is_pinned(self):
        package = json.loads(PARSER_PACKAGE.read_text())
        self.assertEqual(package["dependencies"]["@babel/parser"], "7.28.4")


if __name__ == "__main__":
    unittest.main()
