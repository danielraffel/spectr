#!/usr/bin/env python3
"""Pure qualification oracle controls. Never launches REAPER or installs plugins."""
import math
import struct
import tempfile
import unittest
from unittest.mock import patch
from argparse import Namespace
import plistlib
from pathlib import Path

import reaper_production_audio as qual


def facts():
    lines = [f"PARAM\t{i}\t{name}\t0\t1\t{value}"
             for i, (name, value) in enumerate(qual.REQUIRED.items())]
    lines += ["POINT\t0\t0.5\t1", "POINT\t3\t0.25\t1", "POINT\t6\t0.5\t1", "DEVICE\tnone"]
    return "\n".join(lines)


def receipt():
    return {"status": "PASS", "pid": 124, "actual_path": "/stage/Spectr",
            "expected_sha256": "a" * 64, "actual_sha256": "a" * 64}


class HostFactsTests(unittest.TestCase):
    def test_parameters_and_cold_saved_automation(self):
        parsed = qual.parse_host_facts(facts())
        self.assertEqual(set(parsed["params"]), set(qual.REQUIRED))
        self.assertEqual(parsed["points"][1], (3, 0.25, 1))

    def test_fail_closed_on_wrong_or_ambiguous_parameters_and_state(self):
        corruptions = [
            facts().replace("\tIntensity\t", "\tWrong parameter\t"),
            facts() + "\nPARAM\t8\tIntensity\t0\t1\t0.37",
            facts().replace("PARAM\t2\tIntensity", "PARAM\t1\tIntensity"),
            facts().replace("\t0.37", "\t1"),
            facts().replace("\t0.37", "\tnan"),
            facts().replace("POINT\t3\t0.25", "POINT\t3\t0.5"),
            facts().replace("POINT\t3\t0.25\t1", "POINT\t3\t0.25\t0"),
            facts().replace("DEVICE\tnone", "DEVICE\topen"),
            "\n".join(facts().splitlines()[1:]),
            facts().replace("POINT\t3", "POINT\tnan"),
        ]
        for corrupted in corruptions:
            with self.subTest(corrupted=corrupted):
                with self.assertRaises(ValueError):
                    qual.parse_host_facts(corrupted)

    def test_author_values_can_be_pending_until_first_audio_process(self):
        pending = facts().replace("\t0.37", "\t0.5")
        qual.parse_host_facts(pending, check_values=False)
        with self.assertRaises(ValueError):
            qual.parse_host_facts(pending)

    def test_receipts_required_before_and_after_same_rendering_process(self):
        qual.require_receipt_pair(receipt(), receipt(), "a" * 64)
        for key, value in (("status", "FAIL"), ("actual_sha256", "b" * 64),
                           ("expected_sha256", "b" * 64), ("pid", 125),
                           ("actual_path", "/stale/Spectr")):
            changed = receipt()
            changed[key] = value
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    qual.require_receipt_pair(receipt(), changed, "a" * 64)
        with self.assertRaises(ValueError):
            qual.require_receipt_pair(receipt(), {}, "a" * 64)

    def test_inherited_plugin_fixture_environment_is_removed(self):
        environment = {"PATH": "/usr/bin", "SPECTR_EVAL": "inherited script",
                       "SPECTR_AUTOMATION_PERF_FIXTURE": "1", "OTHER": "keep"}
        self.assertEqual(qual.host_environment(environment), {"PATH": "/usr/bin", "OTHER": "keep"})
        self.assertEqual(environment["SPECTR_EVAL"], "inherited script")

    def test_lua_precedes_cfgfile_and_cold_project_is_explicit(self):
        args = qual.host_command(Path("/REAPER"), Path("/drive.lua"), Path("/reaper.ini"), Path("/saved.rpp"))
        self.assertLess(args.index("/drive.lua"), args.index("-cfgfile"))
        self.assertEqual(args[-1], "/saved.rpp")
        self.assertIn("-newinst", args)


class PreflightTests(unittest.TestCase):
    def test_wrong_release_digest_writes_failure_without_launching_host(self):
        with tempfile.TemporaryDirectory(prefix="spectr-preflight-") as root:
            root = Path(root)
            bundle = root / "Spectr.clap"
            (bundle / "Contents/MacOS").mkdir(parents=True)
            (bundle / "Contents/MacOS/Spectr").write_bytes(b"different release bytes")
            (bundle / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "Spectr"}))
            args = Namespace(out=root / "evidence", plugin=bundle, expected_sha256="a" * 64)
            with patch("reaper_production_audio.sys.platform", "darwin"):
                with patch("reaper_production_audio.subprocess.Popen") as launch:
                    self.assertEqual(qual.run(args), 1)
                    launch.assert_not_called()
            import json
            result = json.loads((args.out / "result.json").read_text())
            self.assertEqual(result["status"], "FAIL")
            self.assertIn("exact-release SHA256", result["error"])


class AudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="spectr-production-oracles-")
        cls.input = Path(cls.temp.name) / "input.wav"
        qual.write_input(cls.input)
        cls.source = qual.read_audio(cls.input)
        cls.rendered = (qual.SR, [[v * (10 ** (-12 / 20) if 3 * qual.SR <= i < 6 * qual.SR else 1)
                                  for i, v in enumerate(channel)] for channel in cls.source[1]])

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_expected_audio_and_reopen_pass(self):
        self.assertEqual(len(qual.analyse_audio(self.source, self.rendered)["window_gains"]), 6)
        self.assertEqual(qual.compare_reopened(self.rendered, self.rendered)["peak_sample_error"], 0)

    def test_ignored_envelope_and_bypassed_plugin_fail(self):
        with self.assertRaisesRegex(ValueError, "expected -12"):
            qual.analyse_audio(self.source, self.source)

    def test_silence_and_wrong_uniform_gain_fail(self):
        for gain in (0, 0.1):
            with self.subTest(gain=gain):
                with self.assertRaises(ValueError):
                    qual.analyse_audio(self.source, (qual.SR, [[v * gain for v in x] for x in self.source[1]]))

    def test_equal_rms_wrong_spectrum_fails(self):
        wrong = [[v for v in channel] for channel in self.rendered[1]]
        a, b = qual.SR, 2 * qual.SR
        for channel in wrong:
            power = sum(v*v for v in channel[a:b]) / (b-a)
            channel[a:b] = [math.sqrt(2*power) * math.sin(2*math.pi*733*i/qual.SR)
                            for i in range(b-a)]
        with self.assertRaisesRegex(ValueError, "probe tone"):
            qual.analyse_audio(self.source, (qual.SR, wrong))

    def test_changed_reopened_audio_fails(self):
        with self.assertRaisesRegex(ValueError, "reopened audio differs"):
            qual.compare_reopened(self.rendered, self.source)

    def test_truncated_or_wrong_rate_audio_fails(self):
        for rate, channels in ((44100, self.rendered[1]),
                               (qual.SR, [x[:-1] for x in self.rendered[1]])):
            with self.assertRaisesRegex(ValueError, "rate or duration"):
                qual.analyse_audio(self.source, (rate, channels))

    def test_pcm32_is_integer_and_float32_is_float(self):
        def wav(kind, raw):
            fmt = struct.pack("<HHIIHH", kind, 2, qual.SR, qual.SR * 8, 8, 32)
            payload = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(raw)) + raw
            path = Path(self.temp.name) / f"kind{kind}.wav"
            path.write_bytes(b"RIFF" + struct.pack("<I", len(payload)) + payload)
            return path
        rate, decoded = qual.read_audio(wav(1, struct.pack("<ii", 1 << 30, -(1 << 30))))
        self.assertEqual(decoded, [[0.5], [-0.5]])
        rate, decoded = qual.read_audio(wav(3, struct.pack("<ff", 0.5, -0.5)))
        self.assertEqual(decoded, [[0.5], [-0.5]])
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            qual.read_audio(wav(3, struct.pack("<ff", math.nan, 0)))


if __name__ == "__main__":
    unittest.main()
