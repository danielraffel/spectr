#!/usr/bin/env python3
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from check_materialized_inventory import inspect


ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "native-ui/materialized/materialized-document.runtime.json"


class MaterializedInventoryTest(unittest.TestCase):
    def test_shipping_artifact_has_stable_inventory(self):
        report = inspect(ARTIFACT)
        self.assertEqual(report["schema"], "pulp-materialized-browser-document-v1")
        self.assertGreater(report["html_bytes"], 100_000)
        self.assertGreater(report["binding_counts"]["layout_bindings"], 0)

    def test_tampered_schema_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            bad = pathlib.Path(td) / ARTIFACT.name
            bad.write_bytes(ARTIFACT.read_bytes().replace(
                b'"schema":"pulp-materialized-browser-document-v1"',
                b'"schema":"tampered"', 1))
            with self.assertRaises(ValueError):
                inspect(bad)


if __name__ == "__main__":
    unittest.main()
