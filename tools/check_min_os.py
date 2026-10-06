#!/usr/bin/env python3
"""Check that every shipped bundle declares the macOS floor it is built for.

    check_min_os.py --expected 13.4 --bundle Spectr.app --bundle Spectr.clap ...
    check_min_os.py --self-test

For each bundle: Contents/Info.plist must carry LSMinimumSystemVersion equal to
--expected, and the bundle's main executable must be built for that same floor
(LC_BUILD_VERSION minos, read with `otool -l`). A plist that claims a floor the
binary does not have is as wrong as a missing key, so both are checked.

--self-test runs the negative controls: a bundle with no key, a wrong key and a
binary/plist disagreement must each be rejected, so a reader that silently
returns nothing cannot pass.
"""
from __future__ import annotations

import argparse
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def binary_min_os(binary: Path) -> list[str]:
    out = subprocess.run(["otool", "-l", str(binary)], capture_output=True,
                         text=True, check=False).stdout
    found = re.findall(r"cmd LC_BUILD_VERSION\n(?:.*\n){0,3}?\s*minos (\S+)", out)
    found += re.findall(r"cmd LC_VERSION_MIN_MACOSX\n(?:.*\n){0,2}?\s*version (\S+)", out)
    return found


def bundle_errors(bundle: Path, expected: str, check_binary: bool = True) -> list[str]:
    info = bundle / "Contents" / "Info.plist"
    if not info.is_file():
        return [f"{bundle}: no Contents/Info.plist"]
    with info.open("rb") as fh:
        plist = plistlib.load(fh)
    errors = []
    declared = plist.get("LSMinimumSystemVersion")
    if declared != expected:
        errors.append(f"{bundle.name}: LSMinimumSystemVersion is {declared!r}, "
                      f"expected {expected!r}")
    if check_binary:
        name = plist.get("CFBundleExecutable")
        binary = bundle / "Contents" / "MacOS" / str(name)
        if not name or not binary.is_file():
            errors.append(f"{bundle.name}: main executable {name!r} not found")
        else:
            floors = binary_min_os(binary)
            if not floors:
                errors.append(f"{bundle.name}: no LC_BUILD_VERSION minos in {binary.name}")
            for floor in floors:
                if floor != expected:
                    errors.append(f"{bundle.name}: binary minos {floor}, plist "
                                  f"declares {declared!r}, expected {expected!r}")
    return errors


def self_test() -> int:
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        def make(name: str, plist: dict) -> Path:
            bundle = Path(tmp) / name
            (bundle / "Contents").mkdir(parents=True)
            with (bundle / "Contents" / "Info.plist").open("wb") as fh:
                plistlib.dump(plist, fh)
            return bundle

        good = make("good.bundle", {"LSMinimumSystemVersion": "13.4"})
        missing = make("missing.bundle", {"CFBundleExecutable": "x"})
        wrong = make("wrong.bundle", {"LSMinimumSystemVersion": "11.0"})
        cases = [("good plist", good, False, True),
                 ("missing key", missing, False, False),
                 ("wrong key", wrong, False, False)]
        # A real binary whose floor is not 13.4 must be rejected even when the
        # plist says 13.4. /usr/bin/true is built for the running OS, never 13.4.
        true_bin = shutil.which("true")
        if true_bin and binary_min_os(Path(true_bin)):
            lying = make("lying.bundle", {"LSMinimumSystemVersion": "13.4",
                                          "CFBundleExecutable": "true"})
            (lying / "Contents" / "MacOS").mkdir()
            shutil.copy(true_bin, lying / "Contents" / "MacOS" / "true")
            cases.append(("binary floor disagrees", lying, True, False))
        else:
            bad += 1
            print("self-test: cannot read any LC_BUILD_VERSION; the binary reader is broken")
        for label, bundle, check_binary, want_ok in cases:
            ok = not bundle_errors(bundle, "13.4", check_binary)
            if ok != want_ok:
                bad += 1
                print(f"self-test {label}: expected ok={want_ok}, got ok={ok}")
    print("self-test: " + ("ok" if bad == 0 else f"{bad} control(s) misjudged"))
    return 0 if bad == 0 else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--expected")
    parser.add_argument("--bundle", action="append", type=Path, default=[])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.expected or not args.bundle:
        parser.error("--expected and at least one --bundle are required")
    errors = []
    for bundle in args.bundle:
        errors += bundle_errors(bundle, args.expected)
    for error in errors:
        print(error, file=sys.stderr)
    if not errors:
        print(f"min-os: {len(args.bundle)} bundle(s) declare and are built for "
              f"macOS {args.expected}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
