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
                 "--emission", str(emission), "--out-report", str(report_path), "--plant-unknown", "PlantedInventoryType",
                 "--plant-prop", "SpectrSettingsField", "--plant-prop-type", "SpectrSettingsField",
                 "--plant-pulp-initial", "App", "--plant-pulp-payload", "App"],
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
            self.assertEqual(report["prop_negative_control"]["status"], "passed")
            self.assertIn(report["prop_negative_control"]["diagnostic"]["code"], ("TS2739", "TS2741"))
            self.assertEqual(report["prop_type_negative_control"]["status"], "passed")
            self.assertEqual(len(report["prop_type_negative_control"]["diagnostics"]), 2)
            self.assertTrue(all(item["code"] == "TS2322" for item in report["prop_type_negative_control"]["diagnostics"]))
            messages = [item["message"] for item in report["prop_type_negative_control"]["diagnostics"]]
            self.assertTrue(any("number" in message and "string" in message for message in messages))
            self.assertTrue(any("string" in message and "boolean" in message for message in messages))
            self.assertEqual(report["prop_contracts"][0]["name"], "SpectrSettingsField")
            self.assertLessEqual(report["prop_contract_effect"]["delta"], 0)
            self.assertEqual(report["window_pulp_contract"]["fields"], ["on", "postMessage", "initial"])
            self.assertEqual(report["window_pulp_contract"]["optional"], True)
            self.assertEqual(report["window_pulp_contract"]["initial_present"], True)
            self.assertEqual(report["window_pulp_contract"]["initial_returns"], "SpectrPulpInitialPayload | null")
            self.assertEqual(report["window_pulp_contract"]["source_receipt"]["path"], "native-ui/materialized/spectr-native-services.js")
            self.assertEqual(len(report["window_pulp_contract"]["source_receipt"]["sha256"]), 64)
            self.assertEqual(report["window_pulp_contract"]["source_receipt"]["synchronized_runtime"]["fields"], ["on", "postMessage", "initial"])
            self.assertEqual(report["window_pulp_contract"]["source_receipt"]["synchronized_runtime"]["initial_present"], True)
            self.assertEqual(report["window_pulp_contract"]["source_receipt"]["embedded_artifact"]["fields"], ["on", "postMessage"])
            self.assertEqual(report["window_pulp_contract"]["source_receipt"]["embedded_artifact"]["initial_present"], False)
            self.assertEqual(report["window_pulp_initial_negative_control"]["status"], "passed")
            self.assertEqual(report["window_pulp_initial_negative_control"]["diagnostic"]["code"], "TS2339")
            self.assertEqual(report["window_pulp_payload_negative_control"]["status"], "passed")
            self.assertEqual(report["window_pulp_payload_negative_control"]["diagnostic"]["code"], "TS2339")
            self.assertEqual(report["scope"]["runtime_artifact_changed"], False)

    def test_unproven_manifest_external_binding_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            manifest = root / "manifest.json"
            made = subprocess.run(
                ["node", str(MANIFEST_CLI), "--artifact", str(ARTIFACT), "--root", "App", "--out", str(manifest)],
                cwd=ROOT, text=True, capture_output=True, check=False,
            )
            self.assertEqual(made.returncode, 0, made.stderr)
            data = json.loads(manifest.read_text())
            data["components"][0]["external_bindings"].append("InjectedWp1Binding")
            manifest.write_text(json.dumps(data, indent=2) + "\n")
            emission = root / "emitted"
            emitted = subprocess.run(
                ["node", str(EMITTER), "--artifact", str(ARTIFACT), "--manifest", str(manifest), "--out", str(emission)],
                cwd=ROOT, text=True, capture_output=True, check=False, timeout=120,
            )
            self.assertEqual(emitted.returncode, 0, emitted.stderr)
            report_path = root / "app-type-debt.json"
            result = subprocess.run(
                ["node", str(INVENTORY), "--artifact", str(ARTIFACT), "--manifest", str(manifest),
                 "--emission", str(emission), "--out-report", str(report_path)],
                cwd=ROOT, text=True, capture_output=True, check=False, timeout=120,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("external binding InjectedWp1Binding is not proven", result.stderr)
            self.assertFalse(report_path.exists())


if __name__ == "__main__":
    unittest.main()
