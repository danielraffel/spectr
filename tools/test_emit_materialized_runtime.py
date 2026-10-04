#!/usr/bin/env python3
import json, pathlib, sys, tempfile, unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from emit_materialized_runtime import freeze, build

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARTIFACT = ROOT / 'native-ui/materialized/materialized-document.runtime.json'

class OwnedRuntimeEmitterTest(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.td.name)
        self.owned = self.root / 'owned'
        self.manifest = freeze(ARTIFACT, self.owned)
    def tearDown(self): self.td.cleanup()
    def test_rebuild_is_byte_identical(self):
        out = self.root / 'rebuilt.json'
        report = build(self.owned/'editor.owned.js', self.owned/'editor.owned.metadata.json', self.owned/'editor.owned.manifest.json', out)
        self.assertTrue(report['matches_frozen_artifact'])
        self.assertEqual(out.read_bytes(), ARTIFACT.read_bytes())
    def _mutated_source_fails(self, label, mutate):
        source = self.owned/'editor.owned.js'; original = source.read_bytes(); source.write_bytes(mutate(original))
        with self.assertRaisesRegex(ValueError, 'digest mismatch|sequence changed'):
            build(source, self.owned/'editor.owned.metadata.json', self.owned/'editor.owned.manifest.json', self.root/(label+'.json'))
    def test_key_swap_negative_control(self):
        self._mutated_source_fails('key-swap', lambda b: b.replace(b'"data-spectr-pattern-id": pattern.id', b'"data-spectr-pattern-id": pattern.source', 1))
    def test_dropped_effect_dependency_negative_control(self):
        self._mutated_source_fails('effect-dependency', lambda b: b.replace(b'useEffect(() => {', b'useEffect(() => { /* dependency removed */', 1))
    def test_sibling_reorder_negative_control(self):
        self._mutated_source_fails('sibling-reorder', lambda b: b.replace(b'React.createElement("div",', b'React.createElement("span",', 1))

if __name__ == '__main__': unittest.main()
