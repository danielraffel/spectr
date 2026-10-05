#!/usr/bin/env python3
"""Adversarial fixture suite for the WP-0 receipt adapter."""

import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ADAPTER = pathlib.Path(__file__).with_name("wp0_baseline_adapter.py")
HOSTS = ((990, 645), (1100, 700), (1320, 860), (1600, 1000))


def receipt(root: pathlib.Path, *, negative: bool = False) -> dict:
    rows = []
    for width, height in HOSTS:
        stem = f"wp0-{width}x{height}"
        png_size = 17
        layout_size = 23
        (root / f"{stem}.png").write_bytes(b"p" * png_size)
        (root / f"{stem}.layout.json").write_bytes(b"l" * layout_size)
        rows.append({
            "host_width": float(width), "host_height": float(height),
            "root_width": 1320.0, "root_height": 860.0,
            "resize_ms": 0.1, "layout_ms": 0.2, "paint_ms": 0.3,
            "resize_bridge_calls": 0, "rgba_bytes": 1320 * 860 * 4,
            "rendered_width": 1320, "rendered_height": 860,
            "rss_bytes": 100, "png_bytes": png_size,
            "layout_bytes": layout_size,
        })
    return {
        "schema": "spectr-wp0-runtime-baseline-v1",
        "mode": "counter-enabled-native-shot",
        "layout_mode": "forced_full_tree",
        "paint_measurement": "raw_rgba_skia_including_layout",
        "resize_measurement": "host_resize_plus_24_synthetic_frames",
        "capture_backend": "skia (CPU Skia raster)", "capture_scale": 1,
        "bridge_counter_available": True,
        "bridge_counter_scope": "registered_native_api_only",
        "mount_bridge_calls": 10, "resize_control_reached": True,
        "negative_control_requested": negative,
        "negative_control_rejected": negative,
        "negative_control_id": "__behavior_pr_e1",
        "max_rss_bytes": 100, "rss_supported": True,
        "executable_bytes": 100,
        "product_source_sha": "4ffbde645dba833ae8dc88081d2af6676d175c9d",
        "product_source_dirty": False,
        "pulp_sdk_source_sha": "1f43a425652a99383b27ac04d3ac6e74ba1b3e4a",
        "pulp_sdk_provenance_exact": True, "rows": rows,
    }


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spectr-wp0-adapter-")
        self.root = pathlib.Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def run_adapter(self, document, *extra, log=None):
        receipt_path = self.root / "receipt.json"
        receipt_path.write_text(json.dumps(document))
        if log is not None:
            log_path = self.root / "negative.log"
            log_path.write_text(log)
            extra = (*extra, "--negative-log", str(log_path), "--require-negative")
        return subprocess.run(
            [sys.executable, str(ADAPTER), str(receipt_path), *extra],
            capture_output=True, text=True,
        )

    def test_positive_and_planted_negative_pass(self):
        document = receipt(self.root, negative=True)
        result = self.run_adapter(
            document,
            log="[wp0] planted negative control id=__behavior_pr_e1 rejected=yes\n"
                 "[wp0] OFFSCREEN __behavior_pr_e1\n",
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def assert_rejected(self, document, *, log=None):
        result = self.run_adapter(document, log=log)
        self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_rejects_nonfinite(self):
        for value in (float("nan"), float("inf"), float("-inf")):
            with self.subTest(value=value):
                document = receipt(self.root)
                document["rows"][0]["paint_ms"] = value
                self.assert_rejected(document)

    def test_rejects_non_object_json(self):
        self.assert_rejected([])

    def test_rejects_wrong_render_dimensions(self):
        document = receipt(self.root)
        document["rows"][0]["rendered_width"] = 1
        document["rows"][0]["rendered_height"] = 1
        document["rows"][0]["rgba_bytes"] = 4
        self.assert_rejected(document)

    def test_rejects_duplicate_or_missing_host(self):
        document = receipt(self.root)
        document["rows"][1] = copy.deepcopy(document["rows"][0])
        self.assert_rejected(document)

    def test_rejects_dirty_provenance(self):
        document = receipt(self.root)
        document["product_source_dirty"] = True
        self.assert_rejected(document)

    def test_rejects_stale_artifact_size(self):
        document = receipt(self.root)
        (self.root / "wp0-990x645.png").write_bytes(b"stale")
        self.assert_rejected(document)

    def test_rejects_fake_negative_id(self):
        document = receipt(self.root, negative=True)
        self.assert_rejected(
            document,
            log="[wp0] planted negative control id=other rejected=yes\n"
                 "[wp0] OFFSCREEN other\n",
        )


if __name__ == "__main__":
    unittest.main()
