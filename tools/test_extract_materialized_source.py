#!/usr/bin/env python3
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from extract_materialized_source import extract


ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "native-ui/materialized/materialized-document.runtime.json"


class MaterializedSourceExtractionTest(unittest.TestCase):
    def test_current_artifact_roundtrips_byte_exactly(self):
        with tempfile.TemporaryDirectory() as td:
            report = extract(ARTIFACT, pathlib.Path(td))
            source = pathlib.Path(td) / "editor.owned.js"
            manifest = json.loads((pathlib.Path(td) / "editor.owned.manifest.json").read_text())
            self.assertEqual(source.read_bytes(), json.loads(ARTIFACT.read_text())["html"].encode())
            self.assertEqual(report["roundtrip"], "byte-exact")
            self.assertEqual(manifest["html_sha256"], report["html_sha256"])
            self.assertEqual(report["unsupported_shapes"], 0)

    def test_unrecognized_create_element_shape_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            bad = pathlib.Path(td) / "bad.json"
            doc = json.loads(ARTIFACT.read_text())
            doc["html"] = doc["html"].replace(
                'React.createElement(\n    "div",',
                'React.createElement(...dynamicTag,', 1)
            bad.write_text(json.dumps(doc, separators=(",", ":")))
            with self.assertRaises(ValueError):
                extract(bad)


if __name__ == "__main__":
    unittest.main()
