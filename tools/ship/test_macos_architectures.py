import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.ship.macos_architectures import architectures, verify_binary


class ArchitecturesTest(unittest.TestCase):
    def test_explicit_targets(self):
        for value in ("arm64", "x86_64", "x86_64;arm64"):
            self.assertEqual(architectures(f"CMAKE_OSX_ARCHITECTURES:STRING={value}"),
                             tuple(sorted(value.split(";"))))

    def test_missing_empty_unknown_and_duplicate_targets_fail(self):
        for cache in ("", "CMAKE_OSX_ARCHITECTURES:STRING=",
                      "CMAKE_OSX_ARCHITECTURES:STRING=x64",
                      "CMAKE_OSX_ARCHITECTURES:STRING=arm64;arm64"):
            with self.subTest(cache=cache), self.assertRaises(ValueError):
                architectures(cache)

    def test_actual_payload_must_match_target(self):
        with patch("tools.ship.macos_architectures.subprocess.run",
                   return_value=subprocess.CompletedProcess([], 0, stdout="x86_64\n")):
            verify_binary(Path("Spectr"), ("x86_64",))
            with self.assertRaisesRegex(ValueError, "Mach-O architectures"):
                verify_binary(Path("Spectr"), ("arm64",))


if __name__ == "__main__":
    unittest.main()
