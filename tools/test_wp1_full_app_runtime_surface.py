#!/usr/bin/env python3
"""Positive and fail-closed controls for the full-App runtime surface gate."""
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
GATE = ROOT / "tools" / "wp1_full_app_runtime_surface.mjs"


def run(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=False, timeout=120)


class FullAppRuntimeSurfaceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp = tempfile.TemporaryDirectory(prefix="spectr-wp1-runtime-surface-")
        cls.root = pathlib.Path(cls.temp.name)
        cls.manifest = cls.root / "app.manifest.json"
        made = run(["node", str(MANIFEST_TOOL), "--artifact", str(ARTIFACT), "--root", "App", "--out", str(cls.manifest)])
        if made.returncode:
            raise AssertionError(made.stderr)
        cls.emission = cls.root / "emitted"
        emitted = run(["node", str(EMITTER), "--artifact", str(ARTIFACT), "--manifest", str(cls.manifest), "--out", str(cls.emission)])
        if emitted.returncode:
            raise AssertionError(emitted.stderr)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temp.cleanup()

    def gate(self, artifact: pathlib.Path = ARTIFACT, manifest: pathlib.Path | None = None,
             emission: pathlib.Path | None = None, allowlist: pathlib.Path = ALLOWLIST,
             out: pathlib.Path | None = None, verify: bool = False) -> subprocess.CompletedProcess[str]:
        out = out or (self.root / "surface")
        args = ["node", str(GATE), "--artifact", str(artifact), "--manifest", str(manifest or self.manifest),
                "--emission", str(emission or self.emission), "--allowlist", str(allowlist), "--out", str(out)]
        if verify:
            args.append("--verify")
        return run(args)

    def test_full_app_surface_emits_typed_facade_and_verifies(self):
        output = self.root / "positive"
        result = self.gate(out=output)
        self.assertEqual(result.returncode, 0, result.stderr)
        contract = json.loads((output / "runtime-surface-contract.json").read_text())
        self.assertEqual(contract["schema"], "spectr-owned-app-runtime-surface-v1")
        self.assertEqual(contract["root"], "App")
        self.assertEqual(contract["module_count"], 59)
        self.assertEqual(contract["typescript"]["diagnostics"], 0)
        self.assertIn("pulp", contract["runtime_surface"]["window"]["used"])
        self.assertIn("spectrSetRangeDb", contract["runtime_surface"]["globalThis"]["used"])
        facade = (output / "runtime-surface.d.ts").read_text()
        self.assertIn("pulp?: SpectrWp1RuntimeValue", facade)
        self.assertNotIn("[key: string]", facade)
        checked = self.gate(out=output, verify=True)
        self.assertEqual(checked.returncode, 0, checked.stderr)
        self.assertIn('"verified": true', checked.stdout)

    def test_unknown_runtime_member_is_rejected_after_module_hash_update(self):
        emission = self.root / "unknown-emitted"
        shutil.copytree(self.emission, emission)
        module = emission / "components" / "App.tsx"
        module.write_text(module.read_text() + "\nvoid window.__wp1_unlisted_runtime_member;\n")
        emission_manifest = emission / "authored-modules.manifest.json"
        data = json.loads(emission_manifest.read_text())
        entry = next(item for item in data["modules"] if item["name"] == "App")
        payload = module.read_bytes()
        entry["output_bytes"] = len(payload)
        entry["output_sha256"] = hashlib.sha256(payload).hexdigest()
        emission_manifest.write_text(json.dumps(data, indent=2) + "\n")
        result = self.gate(emission=emission, out=self.root / "unknown-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("undeclared window.__wp1_unlisted_runtime_member", result.stderr)

    def test_unknown_runtime_member_through_alias_is_rejected(self):
        emission = self.root / "alias-emitted"
        shutil.copytree(self.emission, emission)
        module = emission / "components" / "App.tsx"
        module.write_text(module.read_text() + "\nconst wp1HostAlias = window;\nvoid wp1HostAlias.__wp1_unlisted_alias_member;\n")
        emission_manifest = emission / "authored-modules.manifest.json"
        data = json.loads(emission_manifest.read_text())
        entry = next(item for item in data["modules"] if item["name"] == "App")
        payload = module.read_bytes()
        entry["output_bytes"] = len(payload)
        entry["output_sha256"] = hashlib.sha256(payload).hexdigest()
        emission_manifest.write_text(json.dumps(data, indent=2) + "\n")
        result = self.gate(emission=emission, out=self.root / "alias-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("undeclared window.__wp1_unlisted_alias_member", result.stderr)

    def test_lexical_alias_shadowing_is_rejected(self):
        emission = self.root / "shadow-emitted"
        shutil.copytree(self.emission, emission)
        module = emission / "components" / "App.tsx"
        module.write_text(module.read_text() + "\nconst wp1ShadowAlias = window;\nfunction wp1ShadowScope(wp1ShadowAlias) { return wp1ShadowAlias.pulp; }\n")
        emission_manifest = emission / "authored-modules.manifest.json"
        data = json.loads(emission_manifest.read_text())
        entry = next(item for item in data["modules"] if item["name"] == "App")
        payload = module.read_bytes()
        entry["output_bytes"] = len(payload)
        entry["output_sha256"] = hashlib.sha256(payload).hexdigest()
        emission_manifest.write_text(json.dumps(data, indent=2) + "\n")
        result = self.gate(emission=emission, out=self.root / "shadow-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("rebinds runtime-surface alias wp1ShadowAlias", result.stderr)

    def test_runtime_object_shadowing_is_rejected(self):
        emission = self.root / "root-shadow-emitted"
        shutil.copytree(self.emission, emission)
        module = emission / "components" / "App.tsx"
        module.write_text(module.read_text() + "\nfunction wp1RootShadow(window) { return window.pulp; }\n")
        emission_manifest = emission / "authored-modules.manifest.json"
        data = json.loads(emission_manifest.read_text())
        entry = next(item for item in data["modules"] if item["name"] == "App")
        payload = module.read_bytes()
        entry["output_bytes"] = len(payload)
        entry["output_sha256"] = hashlib.sha256(payload).hexdigest()
        emission_manifest.write_text(json.dumps(data, indent=2) + "\n")
        result = self.gate(emission=emission, out=self.root / "root-shadow-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shadows the runtime object window", result.stderr)

    def test_dynamic_runtime_member_is_rejected(self):
        emission = self.root / "dynamic-emitted"
        shutil.copytree(self.emission, emission)
        module = emission / "components" / "App.tsx"
        module.write_text(module.read_text() + "\nconst wp1DynamicRuntimeName = 'pulp';\nvoid window[wp1DynamicRuntimeName];\n")
        emission_manifest = emission / "authored-modules.manifest.json"
        data = json.loads(emission_manifest.read_text())
        entry = next(item for item in data["modules"] if item["name"] == "App")
        payload = module.read_bytes()
        entry["output_bytes"] = len(payload)
        entry["output_sha256"] = hashlib.sha256(payload).hexdigest()
        emission_manifest.write_text(json.dumps(data, indent=2) + "\n")
        result = self.gate(emission=emission, out=self.root / "dynamic-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("dynamic window member", result.stderr)

    def test_allowlist_tamper_rejects_existing_runtime_member(self):
        allowlist = self.root / "tampered-allowlist.json"
        data = json.loads(ALLOWLIST.read_text())
        data["objects"]["window"]["properties"] = [
            item for item in data["objects"]["window"]["properties"] if item["name"] != "pulp"
        ]
        allowlist.write_text(json.dumps(data, indent=2) + "\n")
        result = self.gate(allowlist=allowlist, out=self.root / "tampered-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("uses undeclared window.pulp", result.stderr)

    def test_artifact_identity_tamper_rejects_stale_allowlist(self):
        artifact = self.root / "tampered-artifact.json"
        artifact.write_bytes(ARTIFACT.read_bytes() + b"\n")
        result = self.gate(artifact=artifact, out=self.root / "artifact-output")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("allowlist is pinned to a different artifact", result.stderr)


if __name__ == "__main__":
    unittest.main()
