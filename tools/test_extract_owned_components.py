import hashlib
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from extract_owned_components import component_slices, extract, verify

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / "native-ui/materialized/materialized-document.runtime.json"


class OwnedComponentSlicesTest(unittest.TestCase):
    def test_current_artifact_has_stable_owned_component_boundaries(self):
        source = json.loads(ARTIFACT.read_text())["html"].encode()
        components = component_slices(source)
        self.assertEqual(len(components), 63)
        self.assertEqual(components[0]["name"], "FilterBank")
        self.assertEqual(components[-1]["name"], "App")
        self.assertEqual(len({c["name"] for c in components}), 63)
        for component in components:
            raw = source[component["start"]:component["end"]]
            self.assertEqual(hashlib.sha256(raw).hexdigest(), component["sha256"])

    def test_extract_writes_exact_slices_and_manifest(self):
        with tempfile.TemporaryDirectory() as td:
            report = extract(ARTIFACT, pathlib.Path(td))
            manifest = json.loads((pathlib.Path(td) / "owned-components.manifest.json").read_text())
            self.assertEqual(report["component_count"], 63)
            self.assertEqual(manifest["component_count"], 63)
            for component in manifest["components"]:
                path = pathlib.Path(td) / "components" / f"{component['name']}.tsx"
                self.assertEqual(path.stat().st_size, component["bytes"])

    def test_verify_rejects_tampered_module(self):
        with tempfile.TemporaryDirectory() as td:
            extract(ARTIFACT, pathlib.Path(td))
            path = pathlib.Path(td) / "components" / "App.tsx"
            path.write_bytes(path.read_bytes() + b"\n")
            with self.assertRaisesRegex(ValueError, "module changed"):
                verify(ARTIFACT, pathlib.Path(td))

    def test_unterminated_component_fails_closed(self):
        with self.assertRaises(ValueError):
            component_slices(b"function Broken() { return React.createElement(\"div\")")


if __name__ == "__main__":
    unittest.main()
