#!/usr/bin/env python3
"""Check Spectr's Sparkle updater wiring and update feed.

    check_sparkle.py bundles --app Spectr.app --plugin Spectr.component \
        --plugin Spectr.vst3 --plugin Spectr.clap [--signed]
    check_sparkle.py appcast --appcast appcast.xml --pkg Spectr-1.0.7.pkg \
        [--public-key <b64>] [--require-notes] [--channel release|practice]
    check_sparkle.py --self-test

bundles: Spectr.app embeds Sparkle.framework, links it, and declares SUFeedURL
         and the expected SUPublicEDKey; no plug-in bundle carries any trace of
         Sparkle (no file, no load command, no reference to its classes).
         --signed adds the release-signature checks: codesign --verify --deep
         --strict, a Developer ID Team ID on the app and the framework, the
         hardened runtime on the framework's nested code, and spctl.
appcast: the newest item describes exactly this package (length, EdDSA
         signature verified with the PUBLIC key), is marked as an installer
         package, carries a sparkle:version higher than every other item, and
         (--require-notes) carries What's New HTML linking to the release page.
         --channel release refuses a practice-shaped build number.
"""
from __future__ import annotations

import argparse
import base64
import plistlib
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ed25519_verify  # noqa: E402

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
EXPECTED_PUBLIC_KEY = "mosCtB7H9gxWzbWUYyHiHTapl4sWMgkd4t09iIUnO2g="
RELEASE_FEED = "https://github.com/danielraffel/spectr/releases/latest/download/appcast.xml"


def version_key(v: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d+(\.\d+)*", v or ""):
        raise ValueError(f"not a dotted numeric version: {v!r}")
    return tuple(int(p) for p in v.split("."))


def _macho_files(root: Path) -> list[Path]:
    out = []
    for p in root.rglob("*"):
        if p.is_file() and not p.is_symlink():
            with p.open("rb") as fh:
                magic = fh.read(4)
            if magic in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xfe\xed\xfa\xcf",
                         b"\xbe\xba\xfe\xca"):
                out.append(p)
    return out


def plugin_errors(bundle: Path) -> list[str]:
    errors = []
    if not (bundle / "Contents").is_dir():
        return [f"{bundle}: not a bundle"]
    for p in bundle.rglob("*"):
        if "sparkle" in p.name.lower():
            errors.append(f"{bundle.name}: carries {p.relative_to(bundle)}")
    for exe in _macho_files(bundle):
        data = exe.read_bytes()
        if b"Sparkle.framework" in data:
            errors.append(f"{bundle.name}: {exe.relative_to(bundle)} references Sparkle.framework")
        if b"SPUStandardUpdaterController" in data or b"SUFeedURL" in data:
            errors.append(f"{bundle.name}: {exe.relative_to(bundle)} contains updater code")
    info = bundle / "Contents" / "Info.plist"
    if info.is_file():
        plist = plistlib.loads(info.read_bytes())
        for key in ("SUFeedURL", "SUPublicEDKey"):
            if key in plist:
                errors.append(f"{bundle.name}: Info.plist declares {key}")
    return errors


def app_errors(app: Path, public_key: str, feed: str | None) -> list[str]:
    errors = []
    info = app / "Contents" / "Info.plist"
    if not info.is_file():
        return [f"{app}: no Info.plist"]
    plist = plistlib.loads(info.read_bytes())
    if not plist.get("SUFeedURL"):
        errors.append(f"{app.name}: no SUFeedURL")
    elif feed and plist["SUFeedURL"] != feed:
        errors.append(f"{app.name}: SUFeedURL is {plist['SUFeedURL']!r}, expected {feed!r}")
    if plist.get("SUPublicEDKey") != public_key:
        errors.append(f"{app.name}: SUPublicEDKey is {plist.get('SUPublicEDKey')!r}, "
                      f"expected {public_key!r}")
    fw = app / "Contents" / "Frameworks" / "Sparkle.framework"
    if not (fw / "Versions" / "B" / "Sparkle").is_file():
        errors.append(f"{app.name}: no Contents/Frameworks/Sparkle.framework")
    exe = app / "Contents" / "MacOS" / plist.get("CFBundleExecutable", "")
    if exe.is_file():
        libs = subprocess.run(["otool", "-L", str(exe)], capture_output=True, text=True).stdout
        if "@rpath/Sparkle.framework/Versions/B/Sparkle" not in libs:
            errors.append(f"{app.name}: executable does not link Sparkle.framework")
    else:
        errors.append(f"{app.name}: no executable")
    return errors


def _codesign_info(path: Path) -> str:
    r = subprocess.run(["codesign", "-dv", "--verbose=4", str(path)],
                       capture_output=True, text=True)
    return r.stdout + r.stderr


def signed_errors(app: Path) -> list[str]:
    errors = []
    r = subprocess.run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        errors.append(f"{app.name}: codesign --verify --deep --strict failed: {r.stderr.strip()}")
    fw = app / "Contents" / "Frameworks" / "Sparkle.framework"
    nested = [app, fw, fw / "Versions" / "B" / "Autoupdate", fw / "Versions" / "B" / "Updater.app"]
    teams = set()
    for p in nested:
        if not p.exists():
            errors.append(f"missing {p}")
            continue
        info = _codesign_info(p)
        team = re.search(r"^TeamIdentifier=(\S+)", info, re.M)
        if not team or team.group(1) == "not set":
            errors.append(f"{p.relative_to(app.parent)}: no Developer ID Team ID")
        else:
            teams.add(team.group(1))
        if not re.search(r"flags=0x[0-9a-f]+\([^)]*runtime", info):
            errors.append(f"{p.relative_to(app.parent)}: hardened runtime not enabled")
        if "Timestamp=" not in info:
            errors.append(f"{p.relative_to(app.parent)}: no secure timestamp")
    if len(teams) > 1:
        errors.append(f"{app.name}: nested code signed by several teams: {sorted(teams)}")
    r = subprocess.run(["spctl", "--assess", "--type", "execute", "--verbose", str(app)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        errors.append(f"{app.name}: spctl rejects it: {(r.stdout + r.stderr).strip()}")
    return errors


def parse_items(xml_text: str) -> list[dict]:
    root = ET.fromstring(xml_text)
    items = []
    for item in root.iter("item"):
        enc = item.find("enclosure")
        items.append({
            "title": item.findtext("title"),
            "version": item.findtext(f"{{{SPARKLE_NS}}}version"),
            "short": item.findtext(f"{{{SPARKLE_NS}}}shortVersionString"),
            "min_os": item.findtext(f"{{{SPARKLE_NS}}}minimumSystemVersion"),
            "channel": item.findtext(f"{{{SPARKLE_NS}}}channel"),
            "full_notes": item.findtext(f"{{{SPARKLE_NS}}}fullReleaseNotesLink"),
            "description": item.findtext("description") or "",
            "url": enc.get("url") if enc is not None else None,
            "length": enc.get("length") if enc is not None else None,
            "type": enc.get(f"{{{SPARKLE_NS}}}installationType") if enc is not None else None,
            "signature": enc.get(f"{{{SPARKLE_NS}}}edSignature") if enc is not None else None,
        })
    return items


def appcast_errors(xml_text: str, pkg_bytes: bytes, public_key: str,
                   require_notes: bool, channel: str | None) -> list[str]:
    try:
        items = parse_items(xml_text)
    except ET.ParseError as e:
        return [f"appcast is not well-formed XML: {e}"]
    if not items:
        return ["appcast has no items"]
    errors = []
    newest = items[0]
    try:
        newest_key = version_key(newest["version"])
    except ValueError as e:
        return [f"newest item: {e}"]
    for other in items[1:]:
        if other.get("channel") != newest.get("channel"):
            continue
        try:
            if version_key(other["version"]) >= newest_key:
                errors.append(f"newest item build {newest['version']} is not higher than "
                              f"{other['version']} already in the feed")
        except ValueError as e:
            errors.append(f"older item: {e}")
    if channel == "release":
        if not re.fullmatch(r"\d+\.\d+\.\d+", newest["version"]):
            errors.append(f"release feed item has a non-release build number {newest['version']!r}")
        if newest["short"] != newest["version"]:
            errors.append("release item shortVersionString and version differ")
        if newest.get("channel"):
            errors.append("release item must be on the default channel")
    if channel == "practice" and not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", newest["version"]):
        errors.append(f"practice item build {newest['version']!r} must have four components")
    if newest["type"] != "package":
        errors.append('newest item enclosure lacks sparkle:installationType="package"')
    if newest["length"] != str(len(pkg_bytes)):
        errors.append(f"enclosure length {newest['length']} != package size {len(pkg_bytes)}")
    if not newest["signature"]:
        errors.append("newest item is unsigned")
    elif not ed25519_verify.verify_b64(public_key, pkg_bytes, newest["signature"]):
        errors.append("EdDSA signature does not verify against the public key")
    if not newest["min_os"]:
        errors.append("newest item has no sparkle:minimumSystemVersion")
    if require_notes:
        desc = newest["description"]
        if "<li>" not in desc and "<p>" not in desc:
            errors.append("newest item carries no What's New notes")
        if not newest["full_notes"] or not newest["full_notes"].startswith("https://"):
            errors.append("newest item has no https fullReleaseNotesLink")
        elif newest["full_notes"] not in desc:
            errors.append("What's New notes do not link to the full release page")
    return errors


def self_test() -> int:
    failures = 0

    def case(name: str, ok: bool) -> None:
        nonlocal failures
        print(("PASS " if ok else "FAIL ") + name)
        failures += not ok

    seed = bytes(range(32))
    pk = base64.b64encode(ed25519_verify.public_key_from_seed(seed)).decode()
    # RFC 8032 test vector 1 proves the verifier is Ed25519 and not merely
    # self-consistent with the fixture signer below.
    rfc_pk = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
    rfc_sig = bytes.fromhex(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")
    case("RFC 8032 vector 1 verifies", ed25519_verify.verify(rfc_pk, b"", rfc_sig))
    case("RFC 8032 vector 1 rejects a changed message",
         not ed25519_verify.verify(rfc_pk, b"x", rfc_sig))
    case("derived public key matches RFC 8032 vector 1",
         ed25519_verify.public_key_from_seed(bytes.fromhex(
             "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")) == rfc_pk)

    pkg = b"pretend this is Spectr-1.0.7.pkg"
    sig = base64.b64encode(ed25519_verify.sign(seed, pkg)).decode()

    def feed(items: str) -> str:
        return ('<?xml version="1.0" encoding="utf-8"?><rss version="2.0" '
                f'xmlns:sparkle="{SPARKLE_NS}"><channel><title>Spectr</title>{items}'
                '</channel></rss>')

    def item(ver: str, *, signature: str = sig, length: int = len(pkg), kind: str = "package",
             notes: bool = True, channel: str = "") -> str:
        url = f"https://github.com/danielraffel/spectr/releases/tag/v{ver}"
        desc = (f"<![CDATA[<ul><li>New</li></ul><a href=\"{url}\">Full</a>]]>" if notes else "")
        ch = f"<sparkle:channel>{channel}</sparkle:channel>" if channel else ""
        return (f"<item><title>Version {ver}</title><description>{desc}</description>"
                f"<sparkle:version>{ver}</sparkle:version>"
                f"<sparkle:shortVersionString>{ver.rsplit('.', 1)[0] if ver.count('.') == 3 else ver}"
                f"</sparkle:shortVersionString>"
                f"<sparkle:minimumSystemVersion>13.4</sparkle:minimumSystemVersion>{ch}"
                f"<sparkle:fullReleaseNotesLink>{url}</sparkle:fullReleaseNotesLink>"
                f'<enclosure url="https://x/Spectr-{ver}.pkg" length="{length}" '
                f'type="application/octet-stream" sparkle:installationType="{kind}" '
                f'sparkle:edSignature="{signature}"/></item>')

    good = feed(item("1.0.7") + item("1.0.6", signature="", notes=False))
    case("signed release item with notes accepted",
         appcast_errors(good, pkg, pk, True, "release") == [])
    case("tampered package rejected",
         appcast_errors(good, pkg + b"!", pk, True, "release") != [])
    other_pk = base64.b64encode(ed25519_verify.public_key_from_seed(b"\x01" * 32)).decode()
    case("wrong public key rejected", appcast_errors(good, pkg, other_pk, True, "release") != [])
    case("non-increasing build rejected",
         appcast_errors(feed(item("1.0.7") + item("1.0.7", notes=False)), pkg, pk, True, "release") != [])
    case("older build at the top rejected",
         appcast_errors(feed(item("1.0.6") + item("1.0.7")), pkg, pk, True, "release") != [])
    case("missing installationType rejected",
         appcast_errors(feed(item("1.0.7", kind="")), pkg, pk, True, "release") != [])
    case("missing notes rejected",
         appcast_errors(feed(item("1.0.7", notes=False)), pkg, pk, True, "release") != [])
    case("practice build refused in the release feed",
         appcast_errors(feed(item("1.0.7.1")), pkg, pk, True, "release") != [])
    case("practice build accepted in the practice feed",
         appcast_errors(feed(item("1.0.7.2") + item("1.0.7.1")), pkg, pk, True, "practice") == [])
    case("version ordering is numeric, not lexical",
         version_key("1.0.10") > version_key("1.0.9"))

    with tempfile.TemporaryDirectory() as tmp:
        plug = Path(tmp) / "Spectr.vst3"
        (plug / "Contents" / "MacOS").mkdir(parents=True)
        exe = plug / "Contents" / "MacOS" / "Spectr"
        exe.write_bytes(b"\xcf\xfa\xed\xfe" + b"\0" * 32 + b"no updater here")
        (plug / "Contents" / "Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "Spectr"}))
        case("clean plug-in accepted", plugin_errors(plug) == [])
        exe.write_bytes(b"\xcf\xfa\xed\xfe" + b"\0" * 32 + b"SPUStandardUpdaterController")
        case("plug-in with updater code rejected", plugin_errors(plug) != [])
        exe.write_bytes(b"\xcf\xfa\xed\xfe" + b"\0" * 32)
        (plug / "Contents" / "Frameworks" / "Sparkle.framework").mkdir(parents=True)
        case("plug-in carrying Sparkle.framework rejected", plugin_errors(plug) != [])
    return 1 if failures else 0


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        return self_test()
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("bundles")
    b.add_argument("--app", type=Path, required=True)
    b.add_argument("--plugin", type=Path, action="append", default=[])
    b.add_argument("--public-key", default=EXPECTED_PUBLIC_KEY)
    b.add_argument("--feed", help="expected SUFeedURL (default: any)")
    b.add_argument("--signed", action="store_true")
    a = sub.add_parser("appcast")
    a.add_argument("--appcast", type=Path, required=True)
    a.add_argument("--pkg", type=Path, required=True)
    a.add_argument("--public-key", default=EXPECTED_PUBLIC_KEY)
    a.add_argument("--require-notes", action="store_true")
    a.add_argument("--channel", choices=("release", "practice"))
    args = ap.parse_args()
    errors: list[str] = []
    if args.cmd == "bundles":
        if not args.plugin:
            print("bundles: pass every plug-in bundle with --plugin", file=sys.stderr)
            return 2
        errors += app_errors(args.app, args.public_key, args.feed)
        for p in args.plugin:
            errors += plugin_errors(p)
        if args.signed:
            errors += signed_errors(args.app)
        checked = f"{args.app.name} + {len(args.plugin)} plug-in bundle(s)"
    else:
        errors += appcast_errors(args.appcast.read_text(), args.pkg.read_bytes(),
                                 args.public_key, args.require_notes, args.channel)
        checked = f"{args.appcast.name} against {args.pkg.name}"
    for e in errors:
        print("FAIL " + e)
    if errors:
        return 1
    print(f"PASS {checked}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
