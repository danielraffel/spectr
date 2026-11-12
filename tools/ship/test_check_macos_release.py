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
    def verify_fixture(self, *, stale_format=None):
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
                        "build": {"type": "Release", "archs": ["arm64"]},
                    }
                    path = expanded / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(json.dumps(info))
            with patch("tools.ship.check_macos_release.subprocess.run", side_effect=expand):
                check_package(package, "1.0.9", SHA)

    def test_all_four_formats_match(self):
        self.verify_fixture()

    def test_one_stale_format_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "CLAP does not match"):
            self.verify_fixture(stale_format="CLAP")


if __name__ == "__main__":
    unittest.main()
