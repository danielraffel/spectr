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
        self.assertEqual(len(components), 65)
        self.assertEqual(components[0]["name"], "FilterBank")
        self.assertEqual(components[-1]["name"], "App")
        self.assertEqual(len({c["name"] for c in components}), 65)
        for component in components:
            raw = source[component["start"]:component["end"]]
            self.assertTrue(raw.startswith(f"function {component['name']}".encode()))
            self.assertEqual(hashlib.sha256(raw).hexdigest(), component["sha256"])

    def test_utf8_offsets_are_byte_exact_and_not_decoded_character_offsets(self):
        source = json.loads(ARTIFACT.read_text())["html"].encode()
        components = component_slices(source)
        self.assertGreater(sum(byte > 127 for byte in source), 0)
        for component in components:
            raw = source[component["start"]:component["end"]]
            self.assertEqual(raw, component["source"])
            self.assertEqual(raw[:len(b"function ")], b"function ")

    def test_extract_writes_exact_slices_and_manifest(self):
        with tempfile.TemporaryDirectory() as td:
            report = extract(ARTIFACT, pathlib.Path(td))
            manifest = json.loads((pathlib.Path(td) / "owned-components.manifest.json").read_text())
            self.assertEqual(report["component_count"], 65)
            self.assertEqual(manifest["component_count"], 65)
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

    def test_verify_rejects_tampered_manifest_schema(self):
        with tempfile.TemporaryDirectory() as td:
            output = pathlib.Path(td)
            extract(ARTIFACT, output)
            manifest_path = output / "owned-components.manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["schema"] = "tampered"
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "schema"):
                verify(ARTIFACT, output)

    def test_verify_rejects_missing_manifest_entry(self):
        with tempfile.TemporaryDirectory() as td:
            output = pathlib.Path(td)
            extract(ARTIFACT, output)
            manifest_path = output / "owned-components.manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["components"].pop()
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "component count"):
                verify(ARTIFACT, output)

    def test_verify_rejects_extra_manifest_entry(self):
        with tempfile.TemporaryDirectory() as td:
            output = pathlib.Path(td)
            extract(ARTIFACT, output)
            manifest_path = output / "owned-components.manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["components"].append(dict(manifest["components"][0]))
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "duplicate component"):
                verify(ARTIFACT, output)

    def test_verify_rejects_stale_component_module(self):
        with tempfile.TemporaryDirectory() as td:
            output = pathlib.Path(td)
            extract(ARTIFACT, output)
            (output / "components" / "Removed.tsx").write_text("stale")
            with self.assertRaisesRegex(ValueError, "unexpected component module"):
                verify(ARTIFACT, output)

    def test_extract_removes_stale_tsx_but_preserves_other_files(self):
        with tempfile.TemporaryDirectory() as td:
            output = pathlib.Path(td)
            components = output / "components"
            components.mkdir(parents=True)
            stale = components / "Removed.tsx"
            note = components / "README.md"
            stale.write_text("stale")
            note.write_text("keep")
            extract(ARTIFACT, output)
            self.assertFalse(stale.exists())
            self.assertEqual(note.read_text(), "keep")

    def test_extract_unlinks_stale_symlink_without_touching_target(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            output = root / "output"
            components = output / "components"
            components.mkdir(parents=True)
            target = root / "outside.tsx"
            target.write_text("outside")
            stale = components / "Removed.tsx"
            stale.symlink_to(target)
            extract(ARTIFACT, output)
            self.assertFalse(stale.exists())
            self.assertEqual(target.read_text(), "outside")

    def test_unterminated_component_fails_closed(self):
        with self.assertRaises(ValueError):
            component_slices(b"function Broken() { return React.createElement(\"div\")")


if __name__ == "__main__":
    unittest.main()
