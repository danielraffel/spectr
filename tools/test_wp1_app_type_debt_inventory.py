#!/usr/bin/env python3
"""Controls for the full-App semantic debt inventory."""
from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "native-ui" / "materialized" / "materialized-document.runtime.json"
MANIFEST_CLI = ROOT / "tools" / "wp1_dependency_manifest.mjs"
EMITTER = ROOT / "tools" / "wp1_authored_module_emitter.mjs"
INVENTORY = ROOT / "tools" / "wp1_app_type_debt_inventory.mjs"


class AppTypeDebtInventoryTest(unittest.TestCase):
    def test_full_app_report_is_machine_readable_and_planted_unknown_is_caught(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = root / "manifest.json"
            made = subprocess.run(
                ["node", str(MANIFEST_CLI), "--artifact", str(ARTIFACT), "--root", "App", "--out", str(manifest)],
                cwd=ROOT, text=True, capture_output=True, check=False,
            )
            self.assertEqual(made.returncode, 0, made.stderr)
            emission = root / "emitted"
            emitted = subprocess.run(
                ["node", str(EMITTER), "--artifact", str(ARTIFACT), "--manifest", str(manifest), "--out", str(emission)],
                cwd=ROOT, text=True, capture_output=True, check=False, timeout=120,
            )
            self.assertEqual(emitted.returncode, 0, emitted.stderr)
            report_path = root / "app-type-debt.json"
            result = subprocess.run(
                ["node", str(INVENTORY), "--artifact", str(ARTIFACT), "--manifest", str(manifest),
                 "--emission", str(emission), "--out-report", str(report_path), "--plant-unknown", "PlantedInventoryType"],
                cwd=ROOT, text=True, capture_output=True, check=False, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(report_path.read_text())
            self.assertEqual(report["schema"], "spectr-owned-app-type-debt-inventory-v1")
            self.assertEqual(report["graph"]["root"], "App")
            self.assertEqual(report["graph"]["modules"], 57)
            self.assertIn("React", report["bindings"]["groups"]["facade_provided"])
            self.assertIn("window", report["bindings"]["groups"]["browser_runtime"])
            self.assertTrue(report["baseline"]["diagnostics"])
            self.assertIn("browser-window", report["baseline"]["counts"])
            self.assertIn("prop-or-type", report["baseline"]["counts"])
            self.assertEqual(report["negative_control"]["status"], "passed")
            self.assertEqual(report["negative_control"]["diagnostic"]["code"], "TS2304")
            self.assertEqual(report["scope"]["runtime_artifact_changed"], False)


if __name__ == "__main__":
    unittest.main()
