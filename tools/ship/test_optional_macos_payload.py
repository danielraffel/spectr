import json
import plistlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.check_no_test_seams import seam_names
from tools.ship.optional_macos_payload import verify_bundle

MACHO = b'\xcf\xfa\xed\xfe'
REAL_RUN = subprocess.run


def fake_lipo(arch):
    # Only the architecture probe is faked; the seam scanner really runs.
    def run(cmd, *args, **kwargs):
        if 'check_no_test_seams.py' in ' '.join(map(str, cmd)):
            return REAL_RUN(cmd, *args, **kwargs)
        return subprocess.CompletedProcess(cmd, 0, stdout=arch)
    return patch('tools.ship.macos_architectures.subprocess.run', side_effect=run)


class OptionalPayloadTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path.cwd())
        self.addCleanup(self.temp.cleanup)
        self.bundle = Path(self.temp.name) / 'Spectr.aaxplugin'
        contents = self.bundle / 'Contents'
        (contents / 'MacOS').mkdir(parents=True)
        (contents / 'Resources').mkdir()
        (contents / 'MacOS' / 'Spectr').write_bytes(MACHO + b'fixture')
        (contents / 'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'Spectr'}))
        self.info = {'schema': 'pulp.build-info.v1',
                     'product': {'name': 'Spectr', 'format': 'AAX', 'version': '1.0.9',
                                 'source_git_sha': 'a' * 40, 'source_git_dirty': False},
                     'pulp_sdk': {'source_git_sha': 'b' * 40, 'provenance_kind': 'release'}, 'build': {'type': 'Release'}}
        self.write_info()

    def write_info(self):
        (self.bundle / 'Contents/Resources/pulp-build-info.json').write_text(json.dumps(self.info))

    def verify(self, kind, arch='x86_64'):
        with fake_lipo(arch):
            verify_bundle(self.bundle, ('x86_64',), kind, '1.0.9', 'a' * 40, 'b' * 40,
                          Path.cwd())

    def test_package_checks_optional_payloads_before_adding_them(self):
        package = Path('package.sh').read_text()
        for kind, arg in [('diagnostics', 'args+=(--app \"Diagnostics app\"'),
                          ('aax', 'args+=(--plugin aax')]:
            with self.subTest(kind=kind):
                self.assertLess(package.index(f'optional_macos_payload.py\" {kind}'),
                                package.index(arg))

    def test_valid_aax_and_diagnostics(self):
        self.verify('aax')
        self.verify('diagnostics')

    def test_optional_binaries_reject_wrong_architecture(self):
        for kind in ('aax', 'diagnostics'):
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, 'Mach-O architectures'):
                self.verify(kind, 'arm64')

    def test_aax_rejects_each_wrong_provenance_field(self):
        for section, field, value in [('product', 'source_git_sha', 'c' * 40),
                                      ('product', 'source_git_dirty', True),
                                      ('pulp_sdk', 'source_git_sha', 'c' * 40),
                                      ('pulp_sdk', 'provenance_kind', 'development'),
                                      ('pulp_sdk', 'provenance_kind', 'source-install'),
                                      ('pulp_sdk', 'provenance_kind', 'source-tree'),
                                      ('pulp_sdk', 'provenance_kind', 'unmarked'),
                                      ('pulp_sdk', 'provenance_kind', None),
                                      ('build', 'type', 'Debug')]:
            with self.subTest(field=field, value=value):
                old = self.info[section][field]
                self.info[section][field] = value
                self.write_info()
                with self.assertRaisesRegex(ValueError, 'provenance'):
                    self.verify('aax')
                self.info[section][field] = old
                self.write_info()

    def test_aax_with_matching_provenance_rejects_test_seams(self):
        # Same clean source, same release SDK, Release build, but configured
        # with SPECTR_ENABLE_TEST_SEAMS: the seam name reaches the binary.
        seam = seam_names(Path.cwd())[0][0]
        (self.bundle / 'Contents/MacOS/Spectr').write_bytes(
            MACHO + b'\0' + seam.encode() + b'\0')
        with self.assertRaisesRegex(ValueError, 'test-seam'):
            self.verify('aax')
        self.verify('diagnostics')

    def test_aax_requires_source_root_for_seam_check(self):
        with fake_lipo('x86_64'), self.assertRaisesRegex(ValueError, 'source root'):
            verify_bundle(self.bundle, ('x86_64',), 'aax', '1.0.9', 'a' * 40, 'b' * 40)

    def test_package_passes_source_root_to_aax_check(self):
        package = Path('package.sh').read_text()
        call = package[package.index('optional_macos_payload.py" aax'):]
        self.assertIn('--source-root "$ROOT"', call[:call.index('\n  args+=')])


if __name__ == '__main__':
    unittest.main()
