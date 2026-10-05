#!/usr/bin/env python3
"""Controls for the fixture-only typed runtime facade experiment."""
from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "tools" / "fixtures" / "wp1-module-import-contract.artifact.json"
MANIFEST_CLI = ROOT / "tools" / "wp1_dependency_manifest.mjs"
EMITTER = ROOT / "tools" / "wp1_authored_module_emitter.mjs"
FACADE = ROOT / "tools" / "wp1_runtime_facade_contract.mjs"


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


def run_facade(artifact: pathlib.Path, manifest: pathlib.Path, emission: pathlib.Path,
               output: pathlib.Path, *, verify: bool = False) -> subprocess.CompletedProcess[str]:
    args = ["node", str(FACADE), "--artifact", str(artifact), "--manifest", str(manifest),
            "--emission", str(emission), "--out", str(output)]
    if verify:
        args.append("--verify")
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=False, timeout=90)


class RuntimeFacadeContractTest(unittest.TestCase):
    def prepare(self, root: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
        manifest = root / "manifest.json"
        made = run_manifest(ARTIFACT, manifest)
        self.assertEqual(made.returncode, 0, made.stderr)
        emission = root / "emitted"
        emitted = run_emitter(ARTIFACT, manifest, emission)
        self.assertEqual(emitted.returncode, 0, emitted.stderr)
        return manifest, emission

    def test_typed_facade_compiles_and_node_smoke_passes(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission = self.prepare(root)
            output = root / "facade"
            result = run_facade(ARTIFACT, manifest, emission, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((output / "runtime-facade-contract.json").read_text())
            self.assertEqual(report["schema"], "spectr-owned-runtime-facade-contract-v1")
            self.assertEqual(report["typescript"]["diagnostics"], 0)
            self.assertEqual(report["runtime_facade"]["smoke"]["status"], "passed")
            self.assertIn("React", report["runtime_facade"]["source"]["exports"])
            self.assertIn("claimDocumentNavigationFocus", report["runtime_facade"]["source"]["exports"])
            self.assertEqual(report["modules"][0]["runtime_imports"], ["React", "claimDocumentNavigationFocus"])
            verified = run_facade(ARTIFACT, manifest, emission, output, verify=True)
            self.assertEqual(verified.returncode, 0, verified.stderr)
            self.assertIn('"verified": true', verified.stdout)

    def test_missing_facade_export_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission = self.prepare(root)
            output = root / "facade"
            built = run_facade(ARTIFACT, manifest, emission, output)
            self.assertEqual(built.returncode, 0, built.stderr)
            source = output / "runtime-bindings.mts"
            source.write_text(source.read_text().replace(
                "export const claimDocumentNavigationFocus: ClaimDocumentNavigationFocus = () => true;\n", ""))
            checked = run_facade(ARTIFACT, manifest, emission, output, verify=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("runtime facade missing export claimDocumentNavigationFocus", checked.stderr)

    def test_tampered_facade_module_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission = self.prepare(root)
            output = root / "facade"
            built = run_facade(ARTIFACT, manifest, emission, output)
            self.assertEqual(built.returncode, 0, built.stderr)
            module = output / "components" / "Leaf.tsx"
            module.write_text(module.read_text() + "\nconst forged = true;\n")
            checked = run_facade(ARTIFACT, manifest, emission, output, verify=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("runtime facade module output identity changed", checked.stderr)

    def test_tampered_facade_globals_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission = self.prepare(root)
            output = root / "facade"
            built = run_facade(ARTIFACT, manifest, emission, output)
            self.assertEqual(built.returncode, 0, built.stderr)
            globals_file = output / "globals.d.ts"
            globals_file.write_text(globals_file.read_text() + "declare const forgedRuntime: any;\n")
            checked = run_facade(ARTIFACT, manifest, emission, output, verify=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("runtime facade globals identity changed", checked.stderr)


if __name__ == "__main__":
    unittest.main()
