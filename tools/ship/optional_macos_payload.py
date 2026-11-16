#!/usr/bin/env python3
"""Verify optional macOS installer payloads before they enter the package."""
import argparse
import json
import plistlib
from pathlib import Path

try:
    from .macos_architectures import architectures, verify_binary
except ImportError:
    from macos_architectures import architectures, verify_binary


def verify_bundle(bundle, expected_archs, kind, version, source_sha, sdk_sha):
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
                or sdk.get('source_git_sha') != sdk_sha or build.get('type') != 'Release'):
            raise ValueError(f'{bundle}: AAX build provenance does not match the release')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=('aax', 'diagnostics'))
    parser.add_argument('bundle', type=Path)
    parser.add_argument('cache', type=Path)
    parser.add_argument('version')
    parser.add_argument('source_sha')
    parser.add_argument('sdk_sha')
    args = parser.parse_args()
    verify_bundle(args.bundle, architectures(args.cache.read_text()), args.kind,
                  args.version, args.source_sha, args.sdk_sha)


if __name__ == '__main__':
    main()
