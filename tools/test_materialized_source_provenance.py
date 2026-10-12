#!/usr/bin/env python3
"""Positive and planted-negative controls for materialized source identity."""
from __future__ import annotations

import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "native-ui/materialized/materialized-document.provenance.json"
VERIFIER = ROOT / "tools/verify_materialized_source_provenance.mjs"
SOURCE = ROOT / "resources/editor.html"
ARTIFACT = ROOT / "native-ui/materialized/materialized-document.runtime.json"


def run(*args: str, timeout: int = 30) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["node", str(VERIFIER), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
        timeout=timeout,
    )


class MaterializedSourceProvenanceTest(unittest.TestCase):
    def test_current_source_and_artifact_match_manifest(self) -> None:
        result = run()
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads(result.stdout)
        self.assertTrue(receipt["verified"])
        self.assertFalse(receipt["scope"]["production_cutover"])
        self.assertEqual(receipt["source"]["sha256"], hashlib.sha256(SOURCE.read_bytes()).hexdigest())
        self.assertEqual(receipt["artifact"]["sha256"], hashlib.sha256(ARTIFACT.read_bytes()).hexdigest())

    def make_staged_copy(self) -> tuple[tempfile.TemporaryDirectory[str], pathlib.Path, dict]:
        temp = tempfile.TemporaryDirectory(prefix="spectr-materialized-provenance-")
        root = pathlib.Path(temp.name)
        (root / "resources").mkdir()
        (root / "native-ui/materialized").mkdir(parents=True)
        shutil.copyfile(SOURCE, root / "resources/editor.html")
        shutil.copyfile(ARTIFACT, root / "native-ui/materialized/materialized-document.runtime.json")
        manifest = json.loads(MANIFEST.read_text())
        (root / "native-ui/materialized/materialized-document.provenance.json").write_text(
            json.dumps(manifest, indent=2) + "\n"
        )
        return temp, root, manifest

    def test_source_byte_mismatch_is_rejected(self) -> None:
        temp, root, _ = self.make_staged_copy()
        self.addCleanup(temp.cleanup)
        source = root / "resources/editor.html"
        data = bytearray(source.read_bytes())
        data[0] ^= 1
        source.write_bytes(data)
        result = run("--root", str(root), "--manifest", str(root / "native-ui/materialized/materialized-document.provenance.json"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source SHA-256 mismatch", result.stderr)

    def test_artifact_byte_mismatch_is_rejected(self) -> None:
        temp, root, _ = self.make_staged_copy()
        self.addCleanup(temp.cleanup)
        artifact = root / "native-ui/materialized/materialized-document.runtime.json"
        data = bytearray(artifact.read_bytes())
        data[-1] ^= 1
        artifact.write_bytes(data)
        result = run("--root", str(root), "--manifest", str(root / "native-ui/materialized/materialized-document.provenance.json"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("artifact SHA-256 mismatch", result.stderr)

    def test_production_cutover_scope_is_fail_closed(self) -> None:
        temp, root, manifest = self.make_staged_copy()
        self.addCleanup(temp.cleanup)
        manifest["scope"]["production_cutover"] = True
        path = root / "native-ui/materialized/materialized-document.provenance.json"
        path.write_text(json.dumps(manifest, indent=2) + "\n")
        result = run("--root", str(root), "--manifest", str(path))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("production_cutover=false", result.stderr)


if __name__ == "__main__":
    unittest.main()
