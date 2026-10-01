#!/usr/bin/env python3
"""Check that every artifact Spectr ships names the same product version.

The product version is CMake's PROJECT_VERSION. This check reads what the
built artifacts actually declare -- each bundle's Info.plist, the AU
component's version integer, the version compiled into the plugin binary
(which Settings > About displays), and optionally an installer package --
and fails on any disagreement.

    check_release_version.py --expected 1.0.3 --bundle A.component ... \
        [--binary-version-bundle Spectr.clap] [--pkg Spectr-1.0.3.pkg]

--self-test runs the negative controls: a planted mismatch in each reader
must be rejected, so a reader that silently returns nothing cannot pass.
"""
from __future__ import annotations

import argparse
import plistlib
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


def au_version_int(version: str) -> int:
    major, minor, patch = (int(p) for p in version.split("."))
    return (major << 16) | (minor << 8) | patch


def bundle_errors(bundle: Path, expected: str) -> list[str]:
    info = bundle / "Contents" / "Info.plist"
    if not info.is_file():
        return [f"{bundle}: no Contents/Info.plist"]
    with info.open("rb") as fh:
        plist = plistlib.load(fh)
    errors = []
    for key in ("CFBundleShortVersionString", "CFBundleVersion"):
        value = plist.get(key)
        if value != expected:
            errors.append(f"{bundle.name}: {key} is {value!r}, expected {expected!r}")
    components = plist.get("AudioComponents")
    if components is not None:
        want = au_version_int(expected)
        for i, component in enumerate(components):
            got = component.get("version")
            if got != want:
                errors.append(f"{bundle.name}: AudioComponents[{i}].version is "
                              f"{got!r}, expected {want} ({expected})")
    return errors


def binary_version_errors(bundle: Path, expected: str) -> list[str]:
    """The descriptor version lives in the executable as a C string; the
    About panel and the CLAP/VST3 descriptors read it from there."""
    info = bundle / "Contents" / "Info.plist"
    if not info.is_file():
        return [f"{bundle}: no Contents/Info.plist"]
    with info.open("rb") as fh:
        name = plistlib.load(fh).get("CFBundleExecutable")
    # Contents/MacOS also holds bundled dylibs and sidecars; the version lives
    # in the executable the bundle names, never in whichever file sorts first.
    exe = bundle / "Contents" / "MacOS" / name if name else None
    if exe is None or not exe.is_file():
        return [f"{bundle}: CFBundleExecutable {name!r} is not under Contents/MacOS"]
    data = exe.read_bytes()
    if (b"\0" + expected.encode() + b"\0") not in data:
        return [f"{bundle.name}: executable carries no {expected!r} version string"]
    return []


# The Diagnostics helper is its own product with its own version; every other
# bundle in the installer is Spectr and must carry Spectr's version.
FOREIGN_BUNDLE_IDS = {"com.pulp.spectr.diagnostics"}


def distribution_errors(dist_xml: str, name: str, expected: str,
                        product: str = "Spectr") -> list[str]:
    root = ET.fromstring(dist_xml)
    errors = []
    title = (root.findtext("title") or "").strip()
    if title != f"{product} {expected}":
        errors.append(f"{name}: installer title is {title!r}, expected '{product} {expected}'")
    refs = [r for r in root.iter("pkg-ref") if r.get("version") is not None]
    if not refs:
        errors.append(f"{name}: no versioned pkg-ref")
    for ref in refs:
        if ref.get("version") != expected:
            errors.append(f"{name}: pkg-ref {ref.get('id')} version is "
                          f"{ref.get('version')!r}, expected {expected!r}")
    bundles = [b for b in root.iter("bundle") if b.get("id") not in FOREIGN_BUNDLE_IDS]
    if not bundles:
        errors.append(f"{name}: declares no Spectr bundle versions")
    for b in bundles:
        for key in ("CFBundleShortVersionString", "CFBundleVersion"):
            if b.get(key) != expected:
                errors.append(f"{name}: bundle {b.get('id')} {key} is "
                              f"{b.get(key)!r}, expected {expected!r}")
    return errors


def pkg_errors(pkg: Path, expected: str, product: str = "Spectr") -> list[str]:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "x"
        subprocess.run(["pkgutil", "--expand", str(pkg), str(out)], check=True)
        dist = out / "Distribution"
        if not dist.is_file():
            return [f"{pkg.name}: no Distribution"]
        errors = distribution_errors(dist.read_text(), pkg.name, expected, product)
        infos = sorted(out.glob("*/PackageInfo"))
        if not infos:
            errors.append(f"{pkg.name}: no component PackageInfo")
        for info in infos:
            version = ET.parse(info).getroot().get("version")
            if version != expected:
                errors.append(f"{pkg.name}: {info.parent.name} version is "
                              f"{version!r}, expected {expected!r}")
    return errors


def self_test() -> int:
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        b = Path(tmp) / "X.component"
        (b / "Contents" / "MacOS").mkdir(parents=True)
        plist = {"CFBundleShortVersionString": "1.0.3", "CFBundleVersion": "1.0.3",
                 "CFBundleExecutable": "X",
                 "AudioComponents": [{"version": au_version_int("1.0.3")}]}
        (b / "Contents" / "Info.plist").write_bytes(plistlib.dumps(plist))
        (b / "Contents" / "MacOS" / "X").write_bytes(b"xx\x001.0.3\x00yy")
        # A bundled dylib that sorts before the executable must not be read.
        (b / "Contents" / "MacOS" / "A.dylib").write_bytes(b"nothing")
        cases = [
            ("clean bundle accepted", bundle_errors(b, "1.0.3") == []),
            ("plist mismatch rejected", bundle_errors(b, "1.0.4") != []),
            ("clean binary accepted", binary_version_errors(b, "1.0.3") == []),
            ("binary mismatch rejected", binary_version_errors(b, "1.0.4") != []),
            ("prefix is not a match", binary_version_errors(b, "1.0") != []),
        ]
        plist["AudioComponents"][0]["version"] = au_version_int("1.0.0")
        (b / "Contents" / "Info.plist").write_bytes(plistlib.dumps(plist))
        cases.append(("AU integer mismatch rejected", bundle_errors(b, "1.0.3") != []))
    dist = ('<installer-gui-script><title>Spectr 1.0.3</title>'
            '<pkg-ref id="a" version="1.0.3">#a</pkg-ref>'
            '<pkg-ref id="a"><bundle-version>'
            '<bundle id="com.pulp.spectr.au" CFBundleShortVersionString="{v}" '
            'CFBundleVersion="{v}"/>'
            '<bundle id="com.pulp.spectr.diagnostics" CFBundleShortVersionString="1.0.0" '
            'CFBundleVersion="1.0.0"/>'
            '</bundle-version></pkg-ref></installer-gui-script>')
    cases.append(("clean distribution accepted",
                  distribution_errors(dist.format(v="1.0.3"), "d", "1.0.3") == []))
    # The shape the 1.0.1 and 1.0.2 installers actually shipped: the package
    # said 1.0.x while every bundle inside it still said 1.0.0.
    cases.append(("stale bundle inside a relabelled package rejected",
                  distribution_errors(dist.format(v="1.0.0"), "d", "1.0.3") != []))
    for name, ok in cases:
        print(("PASS " if ok else "FAIL ") + name)
        failures += not ok
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--expected")
    ap.add_argument("--bundle", action="append", default=[], type=Path)
    ap.add_argument("--binary-version-bundle", action="append", default=[], type=Path)
    ap.add_argument("--pkg", type=Path)
    ap.add_argument("--title", default="Spectr",
                    help="the installer's product name (a dev identity's, e.g. Spectr-Keys-Dev)")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.expected or not re.fullmatch(r"\d+\.\d+\.\d+", args.expected):
        print("--expected must be MAJOR.MINOR.PATCH", file=sys.stderr)
        return 2
    if not (args.bundle or args.binary_version_bundle or args.pkg):
        print("nothing to check", file=sys.stderr)
        return 2
    errors: list[str] = []
    checked = 0
    for bundle in args.bundle:
        errors += bundle_errors(bundle, args.expected)
        checked += 1
    for bundle in args.binary_version_bundle:
        errors += binary_version_errors(bundle, args.expected)
        checked += 1
    if args.pkg:
        errors += pkg_errors(args.pkg, args.expected, args.title)
        checked += 1
    for e in errors:
        print("FAIL " + e)
    if errors:
        return 1
    print(f"PASS {checked} artifact(s) all declare {args.expected}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
