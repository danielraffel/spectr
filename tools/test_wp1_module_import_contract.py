#!/usr/bin/env python3
"""Positive and adversarial controls for the WP-1 ES module contract seam."""
from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "tools" / "fixtures" / "wp1-module-import-contract.artifact.json"
MANIFEST_CLI = ROOT / "tools" / "wp1_dependency_manifest.mjs"
EMITTER = ROOT / "tools" / "wp1_authored_module_emitter.mjs"
CONTRACT = ROOT / "tools" / "wp1_module_import_contract.mjs"


def run_manifest(artifact: pathlib.Path, output: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["node", str(MANIFEST_CLI), "--artifact", str(artifact), "--root", "Panel", "--out", str(output)],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )


def run_emitter(artifact: pathlib.Path, manifest: pathlib.Path,
                output: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["node", str(EMITTER), "--artifact", str(artifact), "--manifest", str(manifest), "--out", str(output)],
        cwd=ROOT, text=True, capture_output=True, check=False, timeout=90,
    )


def run_contract(artifact: pathlib.Path, manifest: pathlib.Path, emission: pathlib.Path,
                 output: pathlib.Path, *, verify: bool = False) -> subprocess.CompletedProcess[str]:
    args = ["node", str(CONTRACT), "--artifact", str(artifact), "--manifest", str(manifest),
            "--emission", str(emission), "--out", str(output)]
    if verify:
        args.append("--verify")
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=False, timeout=90)


class ModuleImportContractTest(unittest.TestCase):
    def prepare(self, root: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path]:
        manifest = root / "manifest.json"
        made = run_manifest(ARTIFACT, manifest)
        self.assertEqual(made.returncode, 0, made.stderr)
        emission = root / "emitted"
        emitted = run_emitter(ARTIFACT, manifest, emission)
        self.assertEqual(emitted.returncode, 0, emitted.stderr)
        return manifest, emission, root / "contract"

    def test_dependency_aware_imports_exports_and_semantic_resolution_pass(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission, contract = self.prepare(root)
            result = run_contract(ARTIFACT, manifest, emission, contract)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((contract / "module-import-contract.json").read_text())
            self.assertEqual(report["schema"], "spectr-owned-module-import-contract-v1")
            self.assertEqual(report["typescript"]["diagnostics"], 0)
            self.assertEqual([item["name"] for item in report["modules"]], ["Leaf", "Panel"])
            self.assertEqual(report["modules"][0]["imports"], [])
            self.assertEqual(report["modules"][1]["imports"][0]["name"], "Leaf")
            self.assertEqual(report["modules"][1]["export"], "Panel")
            verified = run_contract(ARTIFACT, manifest, emission, contract, verify=True)
            self.assertEqual(verified.returncode, 0, verified.stderr)
            self.assertIn('"verified": true', verified.stdout)

    def test_missing_dependency_fails_closed_before_staging(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission, contract = self.prepare(root)
            data = json.loads(manifest.read_text())
            data["components"][-1]["dependencies"].append("component:MissingImport:" + "a" * 64)
            manifest.write_text(json.dumps(data, indent=2) + "\n")
            emission_data = json.loads((emission / "authored-modules.manifest.json").read_text())
            emission_data["dependency_manifest"]["sha256"] = hashlib.sha256(manifest.read_bytes()).hexdigest()
            (emission / "authored-modules.manifest.json").write_text(json.dumps(emission_data, indent=2) + "\n")
            result = run_contract(ARTIFACT, manifest, emission, contract)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("dependency references unknown id", result.stderr)
            self.assertFalse(contract.exists())

    def test_omitted_emitted_module_fails_closed_before_staging(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission, contract = self.prepare(root)
            emission_manifest = emission / "authored-modules.manifest.json"
            data = json.loads(emission_manifest.read_text())
            data["modules"] = data["modules"][:1]
            data["order"] = data["order"][:1]
            emission_manifest.write_text(json.dumps(data, indent=2) + "\n")
            result = run_contract(ARTIFACT, manifest, emission, contract)
            self.assertNotEqual(result.returncode, 0)
            self.assertRegex(result.stderr, r"emission (?:module count|dependency order) changed")
            self.assertFalse(contract.exists())

    def test_tampered_emitted_module_receipt_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission, contract = self.prepare(root)
            module = emission / "components" / "Leaf.tsx"
            module.write_text(module.read_text() + "\nconst tampered = true;\n")
            emission_manifest = emission / "authored-modules.manifest.json"
            data = json.loads(emission_manifest.read_text())
            entry = next(item for item in data["modules"] if item["name"] == "Leaf")
            entry["output_bytes"] = module.stat().st_size
            entry["output_sha256"] = hashlib.sha256(module.read_bytes()).hexdigest()
            emission_manifest.write_text(json.dumps(data, indent=2) + "\n")
            result = run_contract(ARTIFACT, manifest, emission, contract)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("emission regeneration differs", result.stderr)
            self.assertFalse(contract.exists())

    def test_missing_export_is_rejected_by_import_edge_control(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission, contract = self.prepare(root)
            result = run_contract(ARTIFACT, manifest, emission, contract)
            self.assertEqual(result.returncode, 0, result.stderr)
            leaf = contract / "components" / "Leaf.tsx"
            leaf.write_text(leaf.read_text().replace("export { Leaf };\n", ""))
            checked = run_contract(ARTIFACT, manifest, emission, contract, verify=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("missing export Leaf", checked.stderr)

    def test_tampered_ambient_globals_receipt_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission, contract = self.prepare(root)
            result = run_contract(ARTIFACT, manifest, emission, contract)
            self.assertEqual(result.returncode, 0, result.stderr)
            globals_file = contract / "globals.d.ts"
            globals_file.write_text(globals_file.read_text() + "declare const forgedRuntime: any;\n")
            checked = run_contract(ARTIFACT, manifest, emission, contract, verify=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("globals.d.ts bytes changed", checked.stderr)

    def test_captured_component_is_rejected_instead_of_lifted(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            artifact = root / "captured.json"
            artifact.write_text(json.dumps({
                "html": "<script>function Panel() { const label = 'x'; function Inner() { return React.createElement('button', null, label); } return React.createElement(Inner, null); }</script>"
            }))
            manifest = root / "manifest.json"
            made = run_manifest(artifact, manifest)
            self.assertEqual(made.returncode, 0, made.stderr)
            emission = root / "emitted"
            emitted = run_emitter(artifact, manifest, emission)
            self.assertEqual(emitted.returncode, 0, emitted.stderr)
            result = run_contract(artifact, manifest, emission, root / "contract")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cannot create a standalone ES module for captured component", result.stderr)


if __name__ == "__main__":
    unittest.main()
