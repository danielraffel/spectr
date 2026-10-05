#!/usr/bin/env python3
"""Positive and planted-negative tests for the hosted WP-0 adapter."""

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


ADAPTER = pathlib.Path(__file__).with_name("wp0_hosted_receipt.py")
HOSTS = ((990, 645), (1100, 700), (1320, 860), (1600, 1000))


def native_fixture(root: pathlib.Path, *, negative=True) -> dict:
    rows = []
    for width, height in HOSTS:
        stem = f"wp0-{width}x{height}"
        (root / f"{stem}.png").write_bytes(b"p" * 17)
        (root / f"{stem}.layout.json").write_bytes(b"l" * 23)
        rows.append({
            "host_width": width, "host_height": height,
            "root_width": 1320, "root_height": 860,
            "resize_ms": 1, "layout_ms": 2, "paint_ms": 3,
            "resize_bridge_calls": 4, "rgba_bytes": 1320 * 860 * 4,
            "rendered_width": 1320, "rendered_height": 860,
            "rss_bytes": 100, "png_bytes": 17, "layout_bytes": 23,
        })
    return {
        "schema": "spectr-wp0-runtime-baseline-v1",
        "mode": "counter-enabled-native-shot",
        "layout_mode": "forced_full_tree",
        "paint_measurement": "raw_rgba_skia_including_layout",
        "resize_measurement": "host_resize_plus_24_synthetic_frames",
        "capture_backend": "skia", "capture_scale": 1,
        "bridge_counter_available": True,
        "bridge_counter_scope": "registered_native_api_only",
        "hosted_capture": True,
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
        "identity": {
            "host_id": "spectr-gate-fast-m5",
            "host_format": "Standalone",
            "build_id": "4ffbde645dba833ae8dc88081d2af6676d175c9d",
            "artifact_sha256": "a" * 64,
            "artifact_bytes": 48647344,
        },
    }


def ui_fixture(runs=3):
    required = {
        "open": ("open_ms", "first_frame_ms"),
        "frame": ("frame_ms",), "bridge": ("bridge_calls",),
        "layout": ("layout_ms",), "paint": ("paint_ms",),
        "size": ("size_bytes",),
    }
    scenarios = {}
    for scenario, metrics in required.items():
        records = {metric: {"values": [float(i + 1) for i in range(runs)]}
                   for metric in (*metrics, "rss_kb")}
        scenarios[scenario] = {"runs": runs, "metrics": records}
    return {"schema": "spectr-ui-bench-v1", "runs": runs,
            "scenarios": scenarios, "identity": {
                "host_id": "spectr-gate-fast-m5",
                "host_format": "Standalone",
                "build_id": "4ffbde645dba833ae8dc88081d2af6676d175c9d",
                "artifact_sha256": "a" * 64,
                "artifact_bytes": 48647344,
            }}


class HostedReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spectr-wp0-hosted-")
        self.root = pathlib.Path(self.temp.name)
        self.ui = self.root / "ui.json"
        self.native = self.root / "native.json"
        self.log = self.root / "negative.log"
        self.ui.write_text(json.dumps(ui_fixture()))
        self.native.write_text(json.dumps(native_fixture(self.root)))
        self.log.write_text(
            "[wp0] planted negative control id=__behavior_pr_e1 rejected=yes\n"
            "[wp0] OFFSCREEN __behavior_pr_e1\n"
        )

    def tearDown(self):
        self.temp.cleanup()

    def run_adapter(self, *extra):
        return subprocess.run(
            [sys.executable, str(ADAPTER), str(self.ui), str(self.native),
             "--negative-log", str(self.log), *extra],
            capture_output=True, text=True,
        )

    def test_complete_three_run_receipt_passes(self):
        result = self.run_adapter("--negative-log", str(self.log))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"schema": "spectr-wp0-hosted-baseline-v1"', result.stdout)

    def test_plain_native_shot_is_rejected(self):
        document = json.loads(self.native.read_text())
        document.pop("hosted_capture")
        self.native.write_text(json.dumps(document))
        result = self.run_adapter("--negative-log", str(self.log))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("hosted_capture", result.stderr)

    def test_negative_log_is_required(self):
        result = subprocess.run(
            [sys.executable, str(ADAPTER), str(self.ui), str(self.native)],
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--negative-log", result.stderr)

    def test_identity_mismatch_is_rejected(self):
        document = json.loads(self.native.read_text())
        document["identity"]["host_id"] = "different-host"
        self.native.write_text(json.dumps(document))
        result = self.run_adapter("--negative-log", str(self.log))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("identity records do not match", result.stderr)

    def test_missing_scenario_is_planted_negative(self):
        document = ui_fixture()
        del document["scenarios"]["paint"]
        self.ui.write_text(json.dumps(document))
        result = self.run_adapter()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing scenarios", result.stderr)

    def test_two_runs_are_rejected(self):
        self.ui.write_text(json.dumps(ui_fixture(runs=2)))
        result = self.run_adapter()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("at least 3 runs", result.stderr)

    def test_zero_rss_is_rejected(self):
        document = ui_fixture()
        document["scenarios"]["open"]["metrics"]["rss_kb"]["values"][0] = 0
        self.ui.write_text(json.dumps(document))
        result = self.run_adapter()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("rss_kb contains zero", result.stderr)

    def test_zero_size_is_rejected(self):
        document = ui_fixture()
        document["scenarios"]["size"]["metrics"]["size_bytes"]["values"][0] = 0
        self.ui.write_text(json.dumps(document))
        result = self.run_adapter()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("size_bytes contains zero", result.stderr)


if __name__ == "__main__":
    unittest.main()
