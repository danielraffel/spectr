#!/usr/bin/env python3
"""Positive and negative controls for the WP-1 authored module emitter."""
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
ROOTS = ("ContextMenu", "PatternManager", "SpectrModulationSettings")


def run_manifest(artifact: pathlib.Path, output: pathlib.Path,
                 roots: tuple[str, ...] = ROOTS) -> subprocess.CompletedProcess[str]:
    args = ["node", str(MANIFEST_CLI), "--artifact", str(artifact)]
    for root in roots:
        args.extend(["--root", root])
    args.extend(["--out", str(output)])
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=False)


def run_emitter(artifact: pathlib.Path, manifest: pathlib.Path, output: pathlib.Path,
                *, verify: bool = False) -> subprocess.CompletedProcess[str]:
    args = ["node", str(EMITTER), "--artifact", str(artifact),
            "--manifest", str(manifest), "--out", str(output)]
    if verify:
        args.append("--verify")
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True,
                          check=False, timeout=90)


class AuthoredModuleEmitterTest(unittest.TestCase):
    def make_manifest(self, root: pathlib.Path, artifact: pathlib.Path = ARTIFACT,
                      roots: tuple[str, ...] = ROOTS) -> pathlib.Path:
        manifest = root / "three-roots.manifest.json"
        result = run_manifest(artifact, manifest, roots)
        self.assertEqual(result.returncode, 0, result.stderr)
        return manifest

    def test_three_root_closure_is_dependency_first_and_deterministic(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = self.make_manifest(root)
            one, two = root / "one", root / "two"
            first = run_emitter(ARTIFACT, manifest, one)
            second = run_emitter(ARTIFACT, manifest, two)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            one_manifest = one / "authored-modules.manifest.json"
            two_manifest = two / "authored-modules.manifest.json"
            self.assertEqual(one_manifest.read_bytes(), two_manifest.read_bytes())
            report = json.loads(one_manifest.read_text())
            self.assertEqual(report["schema"], "spectr-owned-authored-module-emission-v1")
            # The current-main artifact has four more reachable components
            # than the rc3 snapshot this experiment started from.  Keep the
            # expected closure pinned to this artifact so a future source
            # drift cannot silently change the emission contract.
            self.assertEqual(report["component_count"] if "component_count" in report else len(report["modules"]), 14)
            self.assertEqual(len(report["modules"]), 14)
            positions = {entry["id"]: index for index, entry in enumerate(report["modules"])}
            for entry in report["modules"]:
                for dependency in entry["dependencies"]:
                    self.assertLess(positions[dependency], positions[entry["id"]])
                module = one / entry["path"]
                self.assertEqual(module.stat().st_size, entry["output_bytes"])
                self.assertEqual(hashlib.sha256(module.read_bytes()).hexdigest(), entry["output_sha256"])
            for entry in report["modules"]:
                self.assertEqual((one / entry["path"]).read_bytes(), (two / entry["path"]).read_bytes())

    def test_verify_rejects_tampered_authored_module(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = self.make_manifest(root)
            output = root / "emitted"
            result = run_emitter(ARTIFACT, manifest, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            module = output / "components" / "PatternManager.tsx"
            module.write_bytes(module.read_bytes() + b"\n")
            checked = run_emitter(ARTIFACT, manifest, output, verify=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("output hash changed for PatternManager", checked.stderr)

    def test_verify_rejects_duplicate_module_identity(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = self.make_manifest(root)
            output = root / "emitted"
            result = run_emitter(ARTIFACT, manifest, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            emission = output / "authored-modules.manifest.json"
            report = json.loads(emission.read_text())
            report["modules"][1]["id"] = report["modules"][0]["id"]
            emission.write_text(json.dumps(report, indent=2) + "\n")
            checked = run_emitter(ARTIFACT, manifest, output, verify=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("emission component identity changed", checked.stderr)

    def test_verify_rejects_module_path_traversal(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = self.make_manifest(root)
            output = root / "emitted"
            result = run_emitter(ARTIFACT, manifest, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            emission = output / "authored-modules.manifest.json"
            report = json.loads(emission.read_text())
            report["modules"][0]["path"] = "components/../escape.tsx"
            emission.write_text(json.dumps(report, indent=2) + "\n")
            checked = run_emitter(ARTIFACT, manifest, output, verify=True)
            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("emission module path changed", checked.stderr)

    def test_destination_on_checkout_volume_uses_same_filesystem_staging(self):
        # tempfile.TemporaryDirectory(dir=ROOT) deliberately keeps the output
        # beside this checkout. The emitter must not stage in /var/folders and
        # then fail its final atomic rename with EXDEV.
        with tempfile.TemporaryDirectory(dir=ROOT) as td:
            root = pathlib.Path(td)
            manifest = self.make_manifest(root)
            output = root / "emitted"
            result = run_emitter(ARTIFACT, manifest, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((output / "authored-modules.manifest.json").exists())

    def test_full_app_root_emits_dependency_closure(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = self.make_manifest(root, roots=("App",))
            output = root / "app-emitted"
            result = run_emitter(ARTIFACT, manifest, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((output / "authored-modules.manifest.json").read_text())
            self.assertEqual(len(report["modules"]), 59)
            positions = {entry["id"]: index for index, entry in enumerate(report["modules"])}
            for entry in report["modules"]:
                for dependency in entry["dependencies"]:
                    self.assertLess(positions[dependency], positions[entry["id"]])

    def test_artifact_identity_drift_fails_before_emission(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            mutated = root / "mutated-artifact.json"
            document = json.loads(ARTIFACT.read_text())
            self.assertIn("Spectr", document["html"])
            document["html"] = document["html"].replace("Spectr", "SpectrX", 1)
            mutated.write_text(json.dumps(document, separators=(",", ":")))
            manifest = self.make_manifest(root)
            output = root / "emitted"
            result = run_emitter(mutated, manifest, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("artifact digest does not match", result.stderr)
            self.assertFalse(output.exists())

    def test_tampered_dependency_identity_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = self.make_manifest(root)
            document = json.loads(manifest.read_text())
            document["components"][0]["sha256"] = "a" * 64
            tampered = root / "tampered.manifest.json"
            tampered.write_text(json.dumps(document, indent=2) + "\n")
            result = run_emitter(ARTIFACT, tampered, root / "emitted")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("id/hash identity diverges", result.stderr)

    def test_existing_output_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = self.make_manifest(root)
            output = root / "emitted"
            output.mkdir()
            sentinel = output / "sentinel"
            sentinel.write_text("preserve")
            result = run_emitter(ARTIFACT, manifest, output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("output directory already exists", result.stderr)
            self.assertEqual(sentinel.read_text(), "preserve")


if __name__ == "__main__":
    unittest.main()
