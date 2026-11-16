#!/usr/bin/env python3
"""Verify optional macOS installer payloads before they enter the package."""
import argparse
import json
import plistlib
import subprocess
import sys
from pathlib import Path

try:
    from .macos_architectures import architectures, verify_binary
except ImportError:
    from macos_architectures import architectures, verify_binary


SEAM_CHECK = Path(__file__).resolve().parents[1] / 'check_no_test_seams.py'


def verify_no_test_seams(bundle, source_root):
    # The rebuilt formats are scanned in package.sh; an external AAX gets the
    # same scan, with seam names taken from the source being packaged.
    result = subprocess.run([sys.executable, str(SEAM_CHECK), '--expect', 'absent',
                             '--source-root', str(source_root), str(bundle)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise ValueError(f'{bundle}: AAX test-seam check failed:\n'
                         f'{result.stdout}{result.stderr}')


def verify_bundle(bundle, expected_archs, kind, version, source_sha, sdk_sha, source_root=None):
    contents = bundle / 'Contents'
    with (contents / 'Info.plist').open('rb') as stream:
        executable = plistlib.load(stream)['CFBundleExecutable']
    if not isinstance(executable, str) or not executable or Path(executable).name != executable:
        raise ValueError(f'{bundle}: invalid bundle executable')
    verify_binary(contents / 'MacOS' / executable, expected_archs)
    if kind == 'aax':
        info = json.loads((contents / 'Resources/pulp-build-info.json').read_text())
        product = info.get('product', {})
        sdk = info.get('pulp_sdk', {})
        build = info.get('build', {})
        if (info.get('schema') != 'pulp.build-info.v1'
                or product.get('name') != 'Spectr' or product.get('format') != 'AAX'
                or product.get('version') != version or product.get('source_git_sha') != source_sha
                or product.get('source_git_dirty') is not False
                or sdk.get('source_git_sha') != sdk_sha
                # Pulp's find_package refuses a release-kind SDK marker unless
                # it is distribution_eligible, clean, Release and official-release
                # (PulpSdkProvenance.cmake), so this kind is artifact-bound proof
                # of an eligible SDK; development, source-install, source-tree
                # and unmarked SDKs at the same SHA are rejected.
                or sdk.get('provenance_kind') != 'release'
                or build.get('type') != 'Release'):
            raise ValueError(f'{bundle}: AAX build provenance does not match the release')
        if source_root is None:
            raise ValueError(f'{bundle}: AAX needs the packaged source root for its seam check')
        verify_no_test_seams(bundle, source_root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=('aax', 'diagnostics'))
    parser.add_argument('bundle', type=Path)
    parser.add_argument('cache', type=Path)
    parser.add_argument('version')
    parser.add_argument('source_sha')
    parser.add_argument('sdk_sha')
    parser.add_argument('--source-root', type=Path)
    args = parser.parse_args()
    verify_bundle(args.bundle, architectures(args.cache.read_text()), args.kind,
                  args.version, args.source_sha, args.sdk_sha, args.source_root)


if __name__ == '__main__':
    main()
