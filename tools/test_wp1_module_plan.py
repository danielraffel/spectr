#!/usr/bin/env python3
"""Positive and negative controls for the WP-1 module plan experiment."""
from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import tempfile
import unittest

from tools.extract_owned_components import extract

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLI = ROOT / "tools" / "wp1_module_plan.mjs"
ARTIFACT = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"
MBTN = ROOT / "planning" / "Spectr-WP1-MBtn-dependency.manifest.json"
FIXTURE = ROOT / "tools" / "fixtures" / "wp1-nested-components.manifest.json"


def run(manifest: pathlib.Path, *, modules: pathlib.Path | None = None,
        output: pathlib.Path | None = None) -> subprocess.CompletedProcess[str]:
    args = ["node", str(CLI), "--manifest", str(manifest)]
    if modules is not None:
        args.extend(["--modules", str(modules)])
    if output is not None:
        args.extend(["--out", str(output)])
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True,
                          check=False)


class ModulePlanTest(unittest.TestCase):
    def test_nested_fixture_is_dependency_first_and_deterministic(self):
        first = run(FIXTURE)
        second = run(FIXTURE)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, second.stdout)
        plan = json.loads(first.stdout)
        names = [entry["name"] for entry in plan["modules"]]
        self.assertEqual(names, ["MBtn", "Inner", "Panel"])
        self.assertEqual(plan["order"], [entry["id"] for entry in plan["modules"]])
        self.assertEqual(plan["modules"][1]["captures"], ["label"])
        self.assertEqual(plan["modules"][1]["owner"], plan["modules"][2]["id"])

    def test_mbtn_manifest_verifies_extracted_module_bytes(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            extracted = root / "owned"
            extract(ARTIFACT, extracted)
            result = run(MBTN, modules=extracted / "components")
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertEqual(plan["schema"], "spectr-owned-tsx-module-plan-v1")
            self.assertEqual(plan["modules"][0]["name"], "MBtn")
            module = extracted / "components" / "MBtn.tsx"
            self.assertTrue(plan["modules"][0]["module"].endswith("MBtn.tsx"))
            self.assertEqual(plan["modules"][0]["source_sha256"],
                             hashlib.sha256(module.read_bytes()).hexdigest())

    def test_tampered_module_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            extracted = root / "owned"
            extract(ARTIFACT, extracted)
            module = extracted / "components" / "MBtn.tsx"
            module.write_bytes(module.read_bytes() + b"\n")
            result = run(MBTN, modules=extracted / "components")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("module bytes/hash changed for MBtn", result.stderr)

    def test_unknown_dependency_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = json.loads(MBTN.read_text())
            manifest["components"][0]["dependencies"] = ["component:Missing:" + "a" * 64]
            path = root / "unknown.json"
            path.write_text(json.dumps(manifest, indent=2) + "\n")
            result = run(path)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("dependency references unknown id", result.stderr)

    def test_dependency_cycle_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = json.loads(FIXTURE.read_text())
            by_name = {entry["name"]: entry for entry in manifest["components"]}
            by_name["MBtn"]["dependencies"].append(by_name["Panel"]["id"])
            path = root / "cycle.json"
            path.write_text(json.dumps(manifest, indent=2) + "\n")
            result = run(path)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("dependency cycle", result.stderr)
            self.assertRegex(result.stderr, r"Panel.*Inner.*MBtn.*Panel")

    def test_unreachable_component_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = json.loads(MBTN.read_text())
            extra = dict(manifest["components"][0])
            extra["name"] = "Unused"
            extra["id"] = "component:Unused:" + "b" * 64
            extra["sha256"] = extra["source_sha256"] = "b" * 64
            manifest["components"].append(extra)
            manifest["component_count"] += 1
            path = root / "unreachable.json"
            path.write_text(json.dumps(manifest, indent=2) + "\n")
            result = run(path)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not reachable from manifest roots", result.stderr)

    def test_output_file_is_byte_deterministic(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            one, two = root / "one.json", root / "two.json"
            first = run(FIXTURE, output=one)
            second = run(FIXTURE, output=two)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(one.read_bytes(), two.read_bytes())


if __name__ == "__main__":
    unittest.main()
