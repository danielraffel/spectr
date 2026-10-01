#!/usr/bin/env python3
"""Check that every artifact Spectr ships names the same product version.

The product version is CMake's PROJECT_VERSION. This check reads what the
built artifacts actually declare -- each bundle's Info.plist, the AU
component's version integer, the version compiled into the plugin binary
(which Settings > About displays), and optionally an installer package --
and fails on any disagreement.

    check_release_version.py --expected 1.0.3 --bundle A.component ... \
        [--binary-version-bundle Spectr.clap] [--pkg Spectr-1.0.3.pkg] \
        [--diagnostics-app "Spectr Diagnostics.app"] \
        [--diagnostics-pin tools/ship/diagnostics-kit.json]

The bundled Spectr Diagnostics app is part of the release too. It carries two
identities, and both are checked:

  * its product version (CFBundleShortVersionString / CFBundleVersion) is the
    Spectr release it ships in, like every other bundle -- users report one
    number;
  * its kit identity (DiagnosticKitVersion / DiagnosticKitCommit /
    DiagnosticKitDirty, written by DiagnosticKit's build_app.sh) names the
    DiagnosticKit source that built it, which must be a clean build of the
    version and commit pinned in tools/ship/diagnostics-kit.json.

--diagnostics-kit APP checks only the kit identity, for an app that has not
been stamped with the release version yet.

--self-test runs the negative controls: a planted mismatch in each reader
must be rejected, so a reader that silently returns nothing cannot pass.
"""
from __future__ import annotations

import argparse
import json
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


DIAGNOSTICS_BUNDLE_ID = "com.pulp.spectr.diagnostics"
SEMVER = re.compile(r"\d+\.\d+\.\d+")
SHA = re.compile(r"[0-9a-f]{40}")


def load_kit_pin(path: Path) -> dict:
    pin = json.loads(path.read_text())
    if not (SEMVER.fullmatch(str(pin.get("version", ""))) and SHA.fullmatch(str(pin.get("commit", "")))):
        raise ValueError(f"{path}: needs a MAJOR.MINOR.PATCH 'version' and a 40-hex 'commit'")
    return pin


def kit_identity_errors(plist: dict, name: str, pin: dict | None) -> list[str]:
    """The DiagnosticKit build behind the app: versioned, exact, clean, pinned."""
    errors = []
    version = plist.get("DiagnosticKitVersion")
    commit = plist.get("DiagnosticKitCommit")
    if not (isinstance(version, str) and SEMVER.fullmatch(version)):
        errors.append(f"{name}: DiagnosticKitVersion is {version!r}; the app was not built "
                      "by a versioned DiagnosticKit (Scripts/build_app.sh with a VERSION file)")
    if not (isinstance(commit, str) and SHA.fullmatch(commit)):
        errors.append(f"{name}: DiagnosticKitCommit is {commit!r}, not an exact commit")
    if plist.get("DiagnosticKitDirty") is not False:
        errors.append(f"{name}: DiagnosticKitDirty is {plist.get('DiagnosticKitDirty')!r}; "
                      "ship only a build from a clean DiagnosticKit checkout")
    if pin is not None:
        if version != pin["version"]:
            errors.append(f"{name}: DiagnosticKit {version!r} is not the pinned {pin['version']!r}")
        if commit != pin["commit"]:
            errors.append(f"{name}: DiagnosticKit commit {commit!r} is not the pinned {pin['commit']!r}")
    return errors


def read_plist(bundle: Path) -> dict | None:
    info = bundle / "Contents" / "Info.plist"
    if not info.is_file():
        return None
    with info.open("rb") as fh:
        return plistlib.load(fh)


def diagnostics_kit_errors(app: Path, pin: dict | None) -> list[str]:
    plist = read_plist(app)
    if plist is None:
        return [f"{app}: no Contents/Info.plist"]
    errors = []
    if plist.get("CFBundleIdentifier") != DIAGNOSTICS_BUNDLE_ID:
        errors.append(f"{app.name}: CFBundleIdentifier is {plist.get('CFBundleIdentifier')!r}, "
                      f"expected {DIAGNOSTICS_BUNDLE_ID!r}")
    return errors + kit_identity_errors(plist, app.name, pin)


def diagnostics_app_errors(app: Path, expected: str, pin: dict | None) -> list[str]:
    """Spectr's version on the outside, the pinned kit on the inside."""
    return bundle_errors(app, expected) + diagnostics_kit_errors(app, pin)


def distribution_errors(dist_xml: str, name: str, expected: str) -> list[str]:
    root = ET.fromstring(dist_xml)
    errors = []
    title = (root.findtext("title") or "").strip()
    if title != f"Spectr {expected}":
        errors.append(f"{name}: installer title is {title!r}, expected 'Spectr {expected}'")
    refs = [r for r in root.iter("pkg-ref") if r.get("version") is not None]
    if not refs:
        errors.append(f"{name}: no versioned pkg-ref")
    for ref in refs:
        if ref.get("version") != expected:
            errors.append(f"{name}: pkg-ref {ref.get('id')} version is "
                          f"{ref.get('version')!r}, expected {expected!r}")
    # Every bundle, the Diagnostics app included, carries Spectr's version.
    bundles = list(root.iter("bundle"))
    if not bundles:
        errors.append(f"{name}: declares no Spectr bundle versions")
    for b in bundles:
        for key in ("CFBundleShortVersionString", "CFBundleVersion"):
            if b.get(key) != expected:
                errors.append(f"{name}: bundle {b.get('id')} {key} is "
                              f"{b.get(key)!r}, expected {expected!r}")
    return errors


def pkg_errors(pkg: Path, expected: str, diagnostics_pin: dict | None = None) -> list[str]:
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "x"
        # --expand-full also unpacks each payload, so the bundled Diagnostics
        # app's own Info.plist is read, not only the Distribution's summary.
        subprocess.run(["pkgutil", "--expand-full", str(pkg), str(out)], check=True)
        dist = out / "Distribution"
        if not dist.is_file():
            return [f"{pkg.name}: no Distribution"]
        errors = distribution_errors(dist.read_text(), pkg.name, expected)
        infos = sorted(out.glob("*/PackageInfo"))
        if not infos:
            errors.append(f"{pkg.name}: no component PackageInfo")
        for info in infos:
            version = ET.parse(info).getroot().get("version")
            if version != expected:
                errors.append(f"{pkg.name}: {info.parent.name} version is "
                              f"{version!r}, expected {expected!r}")
        diagnostics = [app for app in sorted(out.glob("*/Payload/**/*.app"))
                       if (read_plist(app) or {}).get("CFBundleIdentifier") == DIAGNOSTICS_BUNDLE_ID]
        for app in diagnostics:
            errors += [f"{pkg.name}: {e}" for e in diagnostics_app_errors(app, expected, diagnostics_pin)]
        if diagnostics_pin is not None and not diagnostics:
            errors.append(f"{pkg.name}: a DiagnosticKit pin was given but no Diagnostics app is inside")
    return errors


def diagnostics_cases(tmp: Path) -> list[tuple[str, bool]]:
    pin = {"version": "1.1.0", "commit": "a" * 40}
    app = tmp / "Spectr Diagnostics.app"
    (app / "Contents").mkdir(parents=True)

    def plant(**changes) -> Path:
        plist = {"CFBundleIdentifier": DIAGNOSTICS_BUNDLE_ID,
                 "CFBundleShortVersionString": "1.0.6", "CFBundleVersion": "1.0.6",
                 "DiagnosticKitVersion": "1.1.0", "DiagnosticKitCommit": "a" * 40,
                 "DiagnosticKitDirty": False}
        for key, value in changes.items():
            if value is None:
                plist.pop(key, None)
            else:
                plist[key] = value
        (app / "Contents" / "Info.plist").write_bytes(plistlib.dumps(plist))
        return app

    return [
        ("stamped Diagnostics app at the pinned kit accepted",
         diagnostics_app_errors(plant(), "1.0.6", pin) == []),
        ("Diagnostics app at 1.0.0 in a 1.0.6 release rejected",
         diagnostics_app_errors(plant(CFBundleShortVersionString="1.0.0"), "1.0.6", pin) != []),
        ("Diagnostics app with a stale CFBundleVersion rejected",
         diagnostics_app_errors(plant(CFBundleVersion="1.0.0"), "1.0.6", pin) != []),
        ("Diagnostics app from an unversioned kit rejected",
         diagnostics_app_errors(plant(DiagnosticKitVersion=None, DiagnosticKitCommit=None,
                                      DiagnosticKitDirty=None), "1.0.6", pin) != []),
        ("Diagnostics app from a dirty kit rejected",
         diagnostics_app_errors(plant(DiagnosticKitDirty=True), "1.0.6", pin) != []),
        ("Diagnostics app from an unpinned kit version rejected",
         diagnostics_app_errors(plant(DiagnosticKitVersion="1.2.0"), "1.0.6", pin) != []),
        ("Diagnostics app from an unpinned kit commit rejected",
         diagnostics_app_errors(plant(DiagnosticKitCommit="b" * 40), "1.0.6", pin) != []),
        ("kit-only check ignores the not-yet-stamped product version",
         diagnostics_kit_errors(plant(CFBundleShortVersionString="1.0.0"), pin) == []),
        ("another bundle passed as the Diagnostics app rejected",
         diagnostics_kit_errors(plant(CFBundleIdentifier="com.pulp.spectr.app"), pin) != []),
    ]


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
        cases += diagnostics_cases(Path(tmp))
    dist = ('<installer-gui-script><title>Spectr 1.0.3</title>'
            '<pkg-ref id="a" version="1.0.3">#a</pkg-ref>'
            '<pkg-ref id="a"><bundle-version>'
            '<bundle id="com.pulp.spectr.au" CFBundleShortVersionString="{v}" '
            'CFBundleVersion="{v}"/>'
            '<bundle id="com.pulp.spectr.diagnostics" CFBundleShortVersionString="{d}" '
            'CFBundleVersion="{d}"/>'
            '</bundle-version></pkg-ref></installer-gui-script>')
    cases.append(("clean distribution accepted",
                  distribution_errors(dist.format(v="1.0.3", d="1.0.3"), "d", "1.0.3") == []))
    # The shape the 1.0.1 and 1.0.2 installers actually shipped: the package
    # said 1.0.x while every bundle inside it still said 1.0.0.
    cases.append(("stale bundle inside a relabelled package rejected",
                  distribution_errors(dist.format(v="1.0.0", d="1.0.3"), "d", "1.0.3") != []))
    # The shape 1.0.5 shipped: every Spectr bundle said 1.0.5 and the bundled
    # Diagnostics app still said 1.0.0.
    cases.append(("unstamped Diagnostics app in the distribution rejected",
                  distribution_errors(dist.format(v="1.0.3", d="1.0.0"), "d", "1.0.3") != []))
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
    ap.add_argument("--diagnostics-app", type=Path)
    ap.add_argument("--diagnostics-kit", type=Path)
    ap.add_argument("--diagnostics-pin", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    pin = None
    if args.diagnostics_pin:
        try:
            pin = load_kit_pin(args.diagnostics_pin)
        except (OSError, ValueError) as exc:
            print(f"bad DiagnosticKit pin: {exc}", file=sys.stderr)
            return 2
    errors: list[str] = []
    checked = 0
    if args.diagnostics_kit:
        errors += diagnostics_kit_errors(args.diagnostics_kit, pin)
        checked += 1
        if not args.expected:
            for e in errors:
                print("FAIL " + e)
            if errors:
                return 1
            print(f"PASS {args.diagnostics_kit.name} is a clean DiagnosticKit build"
                  + (f" of pinned {pin['version']}" if pin else ""))
            return 0
    if not args.expected or not re.fullmatch(r"\d+\.\d+\.\d+", args.expected):
        print("--expected must be MAJOR.MINOR.PATCH", file=sys.stderr)
        return 2
    if not (args.bundle or args.binary_version_bundle or args.pkg or args.diagnostics_app):
        print("nothing to check", file=sys.stderr)
        return 2
    if args.diagnostics_app:
        errors += diagnostics_app_errors(args.diagnostics_app, args.expected, pin)
        checked += 1
    for bundle in args.bundle:
        errors += bundle_errors(bundle, args.expected)
        checked += 1
    for bundle in args.binary_version_bundle:
        errors += binary_version_errors(bundle, args.expected)
        checked += 1
    if args.pkg:
        errors += pkg_errors(args.pkg, args.expected, pin)
        checked += 1
    for e in errors:
        print("FAIL " + e)
    if errors:
        return 1
    print(f"PASS {checked} artifact(s) all declare {args.expected}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
