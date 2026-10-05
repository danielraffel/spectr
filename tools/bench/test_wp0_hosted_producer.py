#!/usr/bin/env python3
"""Adversarial controls for the strict hosted AU receipt producer."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest


PRODUCER = pathlib.Path(__file__).with_name("wp0_hosted_producer.py")


class HostedProducerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not pathlib.Path("/usr/bin/time").exists():
            raise unittest.SkipTest("/usr/bin/time is unavailable")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="spectr-wp0-producer-")
        self.root = pathlib.Path(self.temp.name)
        self.bundle = self.root / "Spectr.component"
        resources = self.bundle / "Contents" / "Resources"
        resources.mkdir(parents=True)
        (resources / "payload").write_bytes(b"realistic-artifact-payload")
        (resources / "pulp-build-info.json").write_text(json.dumps({
            "product": {"source_git_sha": "a" * 40, "source_git_dirty": False},
            "build": {"type": "Release"},
            "pulp_sdk": {"source_git_sha": "b" * 40},
        }))
        self.probe = self.root / "fake-probe.py"
        self.probe.write_text("""#!/usr/bin/env python3
import json, pathlib, sys
out = pathlib.Path(sys.argv[sys.argv.index('--json') + 1])
out.write_text(json.dumps({'opens': [{
  'factory_ms': 10, 'first_present_ms': 20, 'frame_p95_ms': 16.7,
  'hosted_metrics_available': True, 'hosted_bridge_calls': 100,
  'hosted_layout_ms': 1.0, 'hosted_paint_ms': 2.0,
  'hosted_width': 1320, 'hosted_height': 860,
  'hosted_rgba_bytes': 4540800
}]}))
""")
        self.probe.chmod(0o755)

    def tearDown(self):
        self.temp.cleanup()

    def run_producer(self, *extra):
        out = self.root / "receipt.json"
        return subprocess.run([
            sys.executable, str(PRODUCER), "--probe", str(self.probe),
            "--bundle", str(self.bundle), "--host-id", "test-host",
            "--out", str(out), *extra,
        ], capture_output=True, text=True)

    def test_complete_receipt_and_negative_control_pass(self):
        result = self.run_producer()
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads((self.root / "receipt.json").read_text())
        self.assertEqual(receipt["runs"], 3)
        self.assertTrue(receipt["negative_control"]["rejected"])
        self.assertEqual(receipt["scenarios"]["bridge"]["metrics"]["bridge_calls"]["values"],
                         [100.0, 100.0, 100.0])
        self.assertTrue((self.root / "receipt.negative.log").exists())

    def test_vst3_is_explicitly_unsupported(self):
        result = self.run_producer("--format", "VST3")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("only AU Cocoa", result.stderr)

    def test_missing_hosted_metric_is_rejected(self):
        self.probe.write_text(self.probe.read_text().replace("True, 'hosted_bridge_calls'", "False, 'hosted_bridge_calls'"))
        result = self.run_producer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("did not provide hosted", result.stderr)


if __name__ == "__main__":
    unittest.main()
