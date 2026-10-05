#!/usr/bin/env python3
"""Build and semantic negative controls for the WP-1 authored modules."""
from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"
MANIFEST_CLI = ROOT / "tools" / "wp1_dependency_manifest.mjs"
EMITTER = ROOT / "tools" / "wp1_authored_module_emitter.mjs"
TYPECHECK = ROOT / "tools" / "wp1_typecheck_authored_modules.mjs"


def run_manifest(artifact: pathlib.Path, output: pathlib.Path, root_name: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["node", str(MANIFEST_CLI), "--artifact", str(artifact), "--root", root_name, "--out", str(output)],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )


def run_emitter(artifact: pathlib.Path, manifest: pathlib.Path,
                output: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["node", str(EMITTER), "--artifact", str(artifact), "--manifest", str(manifest), "--out", str(output)],
        cwd=ROOT, text=True, capture_output=True, check=False, timeout=90,
    )


def run_typecheck(artifact: pathlib.Path, manifest: pathlib.Path,
                  emission: pathlib.Path, report: pathlib.Path,
                  *, semantic: bool = False) -> subprocess.CompletedProcess[str]:
    args = ["node", str(TYPECHECK), "--artifact", str(artifact), "--manifest", str(manifest),
            "--emission", str(emission), "--out-report", str(report)]
    if semantic:
        args.append("--semantic")
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True,
                          check=False, timeout=90)


class TypecheckAuthoredModulesTest(unittest.TestCase):
    def emit(self, root: pathlib.Path, root_name: str = "MBtn") -> tuple[pathlib.Path, pathlib.Path]:
        manifest = root / f"{root_name}.manifest.json"
        made = run_manifest(ARTIFACT, manifest, root_name)
        self.assertEqual(made.returncode, 0, made.stderr)
        output = root / "emitted"
        emitted = run_emitter(ARTIFACT, manifest, output)
        self.assertEqual(emitted.returncode, 0, emitted.stderr)
        return manifest, output / "authored-modules.manifest.json"

    def test_mbtn_semantic_typecheck_passes_and_receipt_is_hashed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission = self.emit(root)
            report_path = root / "build-report.json"
            result = run_typecheck(ARTIFACT, manifest, emission, report_path, semantic=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(report_path.read_text())
            self.assertEqual(report["schema"], "spectr-owned-tsx-build-validation-v1")
            self.assertTrue(report["typescript"]["semantic"])
            self.assertEqual(report["typescript"]["diagnostics"], 0)
            self.assertEqual(report["emission"]["sha256"], hashlib.sha256(emission.read_bytes()).hexdigest())

    def test_full_app_syntax_build_passes_for_57_modules(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission = self.emit(root, "App")
            report_path = root / "app-build-report.json"
            result = run_typecheck(ARTIFACT, manifest, emission, report_path)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(report_path.read_text())
            self.assertFalse(report["typescript"]["semantic"])
            self.assertEqual(report["emission"]["modules"], 57)
            self.assertEqual(report["typescript"]["diagnostics"], 0)
            self.assertGreater(report["files"][0]["bytes"], 100_000)

    def test_semantic_build_rejects_planted_missing_type(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission = self.emit(root)
            module = root / "emitted" / "components" / "MBtn.tsx"
            module.write_bytes(module.read_bytes() + b"\nconst planted: MissingWp1Type = null;\n")
            report = json.loads(emission.read_text())
            entry = next(item for item in report["modules"] if item["name"] == "MBtn")
            entry["output_bytes"] = module.stat().st_size
            entry["output_sha256"] = hashlib.sha256(module.read_bytes()).hexdigest()
            emission.write_text(json.dumps(report, indent=2) + "\n")
            output_report = root / "negative-report.json"
            result = run_typecheck(ARTIFACT, manifest, emission, output_report, semantic=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Cannot find name 'MissingWp1Type'", result.stderr)
            self.assertFalse(output_report.exists())

    def test_tampered_unknown_external_binding_cannot_mask_missing_type(self):
        """Ambient declarations may only cover bindings proven by the artifact."""
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest, emission = self.emit(root)
            manifest_data = json.loads(manifest.read_text())
            component = next(item for item in manifest_data["components"] if item["name"] == "MBtn")
            component["external_bindings"].append("MissingWp1Type")
            manifest.write_text(json.dumps(manifest_data, indent=2) + "\n")

            emission_data = json.loads(emission.read_text())
            emission_data["dependency_manifest"]["sha256"] = hashlib.sha256(manifest.read_bytes()).hexdigest()
            emission.write_text(json.dumps(emission_data, indent=2) + "\n")

            output_report = root / "tampered-report.json"
            result = run_typecheck(ARTIFACT, manifest, emission, output_report, semantic=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("external binding MissingWp1Type is not declared", result.stderr)
            self.assertFalse(output_report.exists())


if __name__ == "__main__":
    unittest.main()
