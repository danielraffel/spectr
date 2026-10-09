#!/usr/bin/env python3
"""Positive and planted-negative controls for the staging full-App mount."""
from __future__ import annotations

import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"
ALLOWLIST = ROOT / "tools" / "fixtures" / "wp1-full-app-runtime-surface.allowlist.json"
MANIFEST_TOOL = ROOT / "tools" / "wp1_dependency_manifest.mjs"
EMITTER = ROOT / "tools" / "wp1_authored_module_emitter.mjs"
SURFACE = ROOT / "tools" / "wp1_full_app_runtime_surface.mjs"
GATE = ROOT / "tools" / "wp1_full_app_import_mount.mjs"


def run(args: list[str], timeout: int = 180) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=False, timeout=timeout)


class FullAppImportMountTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp = tempfile.TemporaryDirectory(prefix="spectr-wp1-full-app-mount-")
        cls.root = pathlib.Path(cls.temp.name)
        cls.manifest = cls.root / "manifest.json"
        made = run(["node", str(MANIFEST_TOOL), "--artifact", str(ARTIFACT), "--root", "App", "--out", str(cls.manifest)])
        if made.returncode:
            raise AssertionError(made.stderr)
        cls.emission = cls.root / "emitted"
        emitted = run(["node", str(EMITTER), "--artifact", str(ARTIFACT), "--manifest", str(cls.manifest), "--out", str(cls.emission)])
        if emitted.returncode:
            raise AssertionError(emitted.stderr)
        cls.surface = cls.root / "surface"
        surface = run(["node", str(SURFACE), "--artifact", str(ARTIFACT), "--manifest", str(cls.manifest), "--emission", str(cls.emission), "--allowlist", str(ALLOWLIST), "--out", str(cls.surface)])
        if surface.returncode:
            raise AssertionError(surface.stderr)
        cls.artifact_sha = hashlib.sha256(ARTIFACT.read_bytes()).hexdigest()
        cls.editor = ROOT / "resources" / "editor.html"
        cls.editor_sha = hashlib.sha256(cls.editor.read_bytes()).hexdigest()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temp.cleanup()

    def gate(self, *, emission: pathlib.Path | None = None, surface: pathlib.Path | None = None,
             out: pathlib.Path | None = None, allowlist: pathlib.Path | None = None,
             verify: bool = False) -> subprocess.CompletedProcess[str]:
        out = out or (self.root / "mount")
        args = ["node", str(GATE), "--artifact", str(ARTIFACT), "--manifest", str(self.manifest),
                "--emission", str(emission or self.emission), "--surface", str(surface or self.surface), "--out", str(out)]
        if allowlist:
            args.extend(["--allowlist", str(allowlist)])
        if verify:
            args.append("--verify")
        return run(args)

    def test_mount_executes_entire_closure_and_verify_is_deterministic(self):
        first = self.root / "first"
        result = self.gate(out=first)
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads((first / "full-app-import-mount.json").read_text())
        self.assertEqual(receipt["schema"], "spectr-owned-full-app-import-mount-v1")
        self.assertEqual(receipt["mount"]["modules"], 59)
        self.assertEqual(receipt["mount"]["render_tree"]["type"], "div")
        self.assertTrue(receipt["checks"]["explicit_runtime_facade"])
        self.assertFalse(receipt["checks"]["runtime_command_facade"])
        checked = self.gate(out=first, verify=True)
        self.assertEqual(checked.returncode, 0, checked.stderr)
        self.assertIn('"verified": true', checked.stdout)

        second = self.root / "second"
        repeated = self.gate(out=second)
        self.assertEqual(repeated.returncode, 0, repeated.stderr)
        self.assertEqual((first / "full-app-import-mount.json").read_bytes(), (second / "full-app-import-mount.json").read_bytes())

    def test_missing_dependency_helper_is_rejected(self):
        emission = self.root / "missing-helper" / "emitted"
        emission.parent.mkdir()
        shutil.copytree(self.emission, emission)
        (emission / "components" / "MBtn.tsx").unlink()
        result = self.gate(emission=emission, out=self.root / "missing-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("emitted module missing or symlinked for MBtn", result.stderr)

    def test_module_hash_source_drift_is_rejected(self):
        emission = self.root / "stale-module" / "emitted"
        emission.parent.mkdir()
        shutil.copytree(self.emission, emission)
        module = emission / "components" / "App.tsx"
        module.write_text(module.read_text() + "\n// planted source drift\n")
        result = self.gate(emission=emission, out=self.root / "stale-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("emitted module identity changed for App", result.stderr)

    def test_runtime_surface_member_drift_is_rejected(self):
        surface = self.root / "drift-surface"
        shutil.copytree(self.surface, surface)
        contract = surface / "runtime-surface-contract.json"
        data = json.loads(contract.read_text())
        data["runtime_surface"]["window"]["used"].append("__wp1_planted_runtime_drift")
        contract.write_text(json.dumps(data, indent=2) + "\n")
        result = self.gate(surface=surface, allowlist=ALLOWLIST, out=self.root / "drift-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("runtime surface regeneration differs from supplied receipt", result.stderr)

    def test_canonical_artifact_and_editor_are_unchanged(self):
        result = self.gate(out=self.root / "immutability-output")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(hashlib.sha256(ARTIFACT.read_bytes()).hexdigest(), self.artifact_sha)
        self.assertEqual(hashlib.sha256(self.editor.read_bytes()).hexdigest(), self.editor_sha)


if __name__ == "__main__":
    unittest.main()
