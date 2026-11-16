import json
import plistlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.ship.optional_macos_payload import verify_bundle


class OptionalPayloadTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path.cwd())
        self.addCleanup(self.temp.cleanup)
        self.bundle = Path(self.temp.name) / 'Spectr.aaxplugin'
        contents = self.bundle / 'Contents'
        (contents / 'MacOS').mkdir(parents=True)
        (contents / 'Resources').mkdir()
        (contents / 'MacOS' / 'Spectr').write_bytes(b'fixture')
        (contents / 'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'Spectr'}))
        self.info = {'schema': 'pulp.build-info.v1',
                     'product': {'name': 'Spectr', 'format': 'AAX', 'version': '1.0.9',
                                 'source_git_sha': 'a' * 40, 'source_git_dirty': False},
                     'pulp_sdk': {'source_git_sha': 'b' * 40}, 'build': {'type': 'Release'}}
        self.write_info()

    def write_info(self):
        (self.bundle / 'Contents/Resources/pulp-build-info.json').write_text(json.dumps(self.info))

    def verify(self, kind, arch='x86_64'):
        with patch('tools.ship.macos_architectures.subprocess.run',
                   return_value=subprocess.CompletedProcess([], 0, stdout=arch)):
            verify_bundle(self.bundle, ('x86_64',), kind, '1.0.9', 'a' * 40, 'b' * 40)

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
                                      ('build', 'type', 'Debug')]:
            with self.subTest(field=field):
                old = self.info[section][field]
                self.info[section][field] = value
                self.write_info()
                with self.assertRaisesRegex(ValueError, 'provenance'):
                    self.verify('aax')
                self.info[section][field] = old
                self.write_info()


if __name__ == '__main__':
    unittest.main()
