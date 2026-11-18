#!/usr/bin/env python3
"""Mac package provenance must cover every installed product format."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.ship.check_macos_release import PRODUCTS, check_package


SHA = "a" * 40


class MacReleaseTest(unittest.TestCase):
    def verify_fixture(self, *, stale_format=None, payload_archs=("arm64",),
                       expected_archs=("arm64",), sdk_kind="release", stale_sdk=None,
                       expected_sdk=None):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "Spectr-1.0.9.pkg"
            package.write_bytes(b"fixture")

            def expand(command, **kwargs):
                expanded = Path(command[-1])
                for relative, fmt in PRODUCTS.items():
                    info = {
                        "schema": "pulp.build-info.v1",
                        "product": {"name": "Spectr", "format": fmt,
                                    "version": "1.0.9",
                                    "source_git_sha": "b" * 40 if fmt == stale_format else SHA,
                                    "source_git_dirty": False},
                        "build": {"type": "Release", "archs": list(payload_archs)},
                        "pulp_sdk": {"provenance_kind": sdk_kind,
                                     "source_git_sha": "c" * 40 if fmt == stale_sdk else "d" * 40},
                    }
                    path = expanded / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(json.dumps(info))
            with patch("tools.ship.check_macos_release.subprocess.run", side_effect=expand):
                check_package(package, "1.0.9", SHA, expected_archs, sdk_source_sha=expected_sdk)

    def test_all_four_formats_match(self):
        self.verify_fixture()

    def test_one_stale_format_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "CLAP does not match"):
            self.verify_fixture(stale_format="CLAP")

    def test_intel_package(self):
        self.verify_fixture(payload_archs=("x86_64",), expected_archs=("x86_64",))

    def test_universal_package(self):
        self.verify_fixture(payload_archs=("x86_64", "arm64"),
                            expected_archs=("arm64", "x86_64"))

    def test_wrong_architecture_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.verify_fixture(payload_archs=("arm64",), expected_archs=("x86_64",))

    def test_extra_slice_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.verify_fixture(payload_archs=("arm64", "x86_64"))

    def test_pinned_official_sdk_for_every_format(self):
        self.verify_fixture(expected_sdk="d" * 40)
        for fmt in PRODUCTS.values():
            with self.subTest(fmt=fmt), self.assertRaisesRegex(ValueError, "official release SDK"):
                self.verify_fixture(expected_sdk="d" * 40, stale_sdk=fmt)
        for kind in ("development", "source-install", "source-tree", None):
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "official release SDK"):
                self.verify_fixture(expected_sdk="d" * 40, sdk_kind=kind)


if __name__ == "__main__":
    unittest.main()
