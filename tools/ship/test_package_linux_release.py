"""Linux packager must refuse mismatched source and SDK provenance."""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.ship.package_linux_release import validate_source_provenance

SOURCE = "a" * 40
SDK = "b" * 40


class LinuxPackageProvenanceTest(unittest.TestCase):
    def _source(self, *, sdk: str = SDK) -> Path:
        directory = Path(tempfile.mkdtemp())
        (directory / "build").mkdir()
        (directory / "build" / "CMakeCache.txt").write_text(
            "\n".join(
                (
                    f"SPECTR_SOURCE_GIT_SHA:INTERNAL={SOURCE}",
                    f"PULP_SDK_SOURCE_GIT_SHA:INTERNAL={sdk}",
                    "CMAKE_BUILD_TYPE:STRING=Release",
                    "SPECTR_SOURCE_GIT_DIRTY:INTERNAL=FALSE",
                    "PULP_SDK_DISTRIBUTION_ELIGIBLE:BOOL=TRUE",
                    "PULP_SDK_PROVENANCE_KIND:STRING=release",
                )
            )
        )
        return directory

    @patch("tools.ship.package_linux_release.subprocess.check_output")
    def test_source_sha_mismatch_is_rejected(self, check_output):
        check_output.side_effect = [SOURCE + "\n", ""]
        with self.assertRaisesRegex(ValueError, "source checkout is"):
            validate_source_provenance(self._source(), "c" * 40, SDK)

    @patch("tools.ship.package_linux_release.subprocess.check_output")
    def test_sdk_sha_mismatch_is_rejected(self, check_output):
        check_output.side_effect = [SOURCE + "\n", ""]
        with self.assertRaisesRegex(ValueError, "PULP_SDK_SOURCE_GIT_SHA"):
            validate_source_provenance(self._source(sdk="c" * 40), SOURCE, SDK)


if __name__ == "__main__":
    unittest.main()
