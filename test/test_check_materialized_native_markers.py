import json
import tempfile
import unittest
from pathlib import Path

from tools.check_materialized_native_markers import check_runtime


class MaterializedNativeMarkerContractTests(unittest.TestCase):
    def write_runtime(self, html: str) -> Path:
        handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False)
        with handle:
            json.dump({"html": html}, handle)
        return Path(handle.name)

    def test_accepts_complete_native_bridge_surface(self):
        path = self.write_runtime(
            'data-spectr-settings-open data-spectr-band-context-menu data-spectr-menu-root'
        )
        self.assertEqual(
            check_runtime(path),
            {
                "data-spectr-settings-open": 1,
                "data-spectr-band-context-menu": 1,
                "data-spectr-menu-root": 1,
            },
        )

    def test_rejects_runtime_with_missing_bridge_surface(self):
        path = self.write_runtime("data-spectr-menu-root")
        with self.assertRaisesRegex(ValueError, "missing native bridge markers"):
            check_runtime(path)


if __name__ == "__main__":
    unittest.main()
