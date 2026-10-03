#!/usr/bin/env python3
"""Produce the Sparkle appcast for a signed, notarized Spectr installer package.

    make_appcast.py --pkg artifacts/Spectr-1.0.7.pkg --version 1.0.7 \
        --notes release-notes-1.0.7.md --out artifacts/appcast.xml
    make_appcast.py --channel practice --pkg artifacts/Spectr-1.0.7.2.pkg \
        --version 1.0.7 --build 1.0.7.2 --notes notes.md \
        --previous artifacts/appcast-practice.xml --out artifacts/appcast-practice.xml

Writes, next to --out: the appcast and the What's New HTML it embeds. Upload
both the package and the appcast to the GitHub release (see docs/updates.md).

  release   build == version (X.Y.Z). The enclosure URL is the versioned
            release asset https://github.com/danielraffel/spectr/releases/download/vX.Y.Z/Spectr-X.Y.Z.pkg.
            --previous defaults to the live release feed, so the new item
            must be newer than everything already offered.
  practice  build X.Y.Z.N, assets on the fixed `sparkle-practice` prerelease
            (feed appcast-practice.xml), which release builds never read.

The private key is read from --key-file (default
~/.config/pulp/secrets/sparkle/spectr_ed25519) by the signing tool itself; this
script never reads, prints or passes the key on a command line.

Signing goes through `pulp ship appcast --sign-key-file` when the installed
Pulp CLI supports it. Older CLIs fall back to Sparkle's own sign_update and an
XML writer here -- delete that fallback (the _shim_* functions) on the Pulp SDK
bump that ships `pulp ship appcast --sign-key-file`.
"""
from __future__ import annotations

import argparse
import email.utils
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import check_sparkle  # noqa: E402
import release_notes_html  # noqa: E402

REPO = "https://github.com/danielraffel/spectr"
RELEASE_FEED = f"{REPO}/releases/latest/download/appcast.xml"
PRACTICE_TAG = "sparkle-practice"
PRACTICE_FEED = f"{REPO}/releases/download/{PRACTICE_TAG}/appcast-practice.xml"
DEFAULT_KEY = Path.home() / ".config/pulp/secrets/sparkle/spectr_ed25519"
APP_BUNDLE_ID = "com.pulp.spectr"


def fail(msg: str) -> "None":
    print(f"make_appcast: {msg}", file=sys.stderr)
    sys.exit(2)


def pkg_app_info(pkg: Path) -> tuple[str, str]:
    """(CFBundleShortVersionString, CFBundleVersion) of Spectr.app inside pkg."""
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "x"
        subprocess.run(["pkgutil", "--expand", str(pkg), str(out)], check=True)
        root = ET.parse(out / "Distribution").getroot()
        for b in root.iter("bundle"):
            if b.get("id") == APP_BUNDLE_ID:
                return b.get("CFBundleShortVersionString", ""), b.get("CFBundleVersion", "")
    fail(f"{pkg.name} contains no {APP_BUNDLE_ID} app bundle")
    raise AssertionError


def app_min_os(app: Path) -> str:
    exe = app / "Contents" / "MacOS" / "Spectr"
    text = subprocess.run(["otool", "-l", str(exe)], capture_output=True, text=True).stdout
    m = re.search(r"cmd LC_BUILD_VERSION.*?minos (\d+\.\d+(?:\.\d+)?)", text, re.S)
    if not m:
        fail(f"cannot read the deployment target of {exe}")
    return m.group(1)


def fetch_previous(source: str | None, channel: str, dest: Path) -> bool:
    if source == "none":
        return False
    if source is None:
        source = RELEASE_FEED if channel == "release" else PRACTICE_FEED
    if re.match(r"^https?://", source):
        try:
            with urllib.request.urlopen(source, timeout=30) as r:
                dest.write_bytes(r.read())
            return True
        except urllib.error.HTTPError as e:
            if e.code == 404:
                print(f"make_appcast: no feed at {source} yet; starting a new one")
                return False
            fail(f"could not fetch {source}: {e}")
    p = Path(source)
    if not p.is_file():
        fail(f"--previous {source} does not exist")
    shutil.copyfile(p, dest)
    return True


def pulp_supports_key_file(pulp: str) -> bool:
    try:
        r = subprocess.run([pulp, "ship", "--help"], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return "--sign-key-file" in (r.stdout + r.stderr) and "--notes-html-file" in (r.stdout + r.stderr)


# ── Delete on SDK bump ───────────────────────────────────────────────────────
def _shim_sign(sign_update: Path, key_file: Path, pkg: Path) -> str:
    r = subprocess.run([str(sign_update), "--ed-key-file", str(key_file), "-p", str(pkg)],
                       capture_output=True, text=True)
    sig = r.stdout.strip()
    if r.returncode != 0 or not re.fullmatch(r"[A-Za-z0-9+/]{86}==", sig):
        fail("sign_update failed; refusing to publish an unsigned item")
    return sig


def _shim_write(out: Path, previous: Path | None, item: dict) -> None:
    sparkle = check_sparkle.SPARKLE_NS
    older: list[str] = []
    if previous is not None:
        text = previous.read_text()
        for block in re.findall(r"<item>.*?</item>", text, re.S):
            if re.search(r"<sparkle:shortVersionString>" + re.escape(item["short"]) +
                         r"</sparkle:shortVersionString>", block) and \
               re.search(r"<sparkle:version>" + re.escape(item["build"]) + r"</sparkle:version>", block):
                continue  # re-running a version replaces its entry
            older.append(block)
    e = lambda s: html.escape(s, quote=True)  # noqa: E731
    channel = f"\n      <sparkle:channel>{e(item['channel'])}</sparkle:channel>" if item["channel"] else ""
    block = f"""<item>
      <title>Version {e(item['short'])}</title>
      <description><![CDATA[{item['notes']}]]></description>
      <pubDate>{item['pub_date']}</pubDate>
      <sparkle:version>{e(item['build'])}</sparkle:version>
      <sparkle:shortVersionString>{e(item['short'])}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>{e(item['min_os'])}</sparkle:minimumSystemVersion>{channel}
      <sparkle:fullReleaseNotesLink>{e(item['release_url'])}</sparkle:fullReleaseNotesLink>
      <enclosure
        url="{e(item['url'])}"
        length="{item['length']}"
        type="application/octet-stream"
        sparkle:installationType="package"
        sparkle:edSignature="{item['signature']}"
      />
    </item>"""
    body = "\n    ".join([block] + older)
    out.write_text(f"""<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="{sparkle}">
  <channel>
    <title>Spectr</title>
    <link>{e(item['feed'])}</link>
    <description>Spectr updates</description>
    <language>en</language>
    {body}
  </channel>
</rss>
""")
# ── end delete on SDK bump ───────────────────────────────────────────────────


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--pkg", type=Path, required=True)
    ap.add_argument("--version", required=True, help="product version X.Y.Z")
    ap.add_argument("--build", help="CFBundleVersion of Spectr.app (default: --version)")
    ap.add_argument("--channel", choices=("release", "practice"), default="release")
    ap.add_argument("--notes", required=True, help="release notes Markdown file (the GitHub release body)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--previous", help="existing feed (file or URL); 'none' to start fresh; "
                                       "default: the live feed for --channel")
    ap.add_argument("--download-url", help="override the enclosure URL (e.g. file:// for a local rehearsal)")
    ap.add_argument("--release-url", help="full release notes page (default: the GitHub release)")
    ap.add_argument("--app", type=Path, help="built Spectr.app, to read the deployment target")
    ap.add_argument("--min-os", help="sparkle:minimumSystemVersion (default: from --app)")
    ap.add_argument("--key-file", type=Path, default=DEFAULT_KEY)
    ap.add_argument("--pulp", default=os.environ.get("PULP_CLI", "pulp"))
    ap.add_argument("--sign-update", type=Path, help="Sparkle's bin/sign_update (fallback signer)")
    ap.add_argument("--pub-date", help="RFC 2822 date (default: now)")
    args = ap.parse_args()

    build = args.build or args.version
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        fail("--version must be MAJOR.MINOR.PATCH")
    if args.channel == "release" and build != args.version:
        fail("a release build number must equal its version (CFBundleVersion == X.Y.Z)")
    if args.channel == "practice" and not re.fullmatch(re.escape(args.version) + r"\.\d+", build):
        fail("a practice build must be <version>.<n>, e.g. 1.0.7.1")
    if not args.pkg.is_file():
        fail(f"no package at {args.pkg}")
    if not args.key_file.is_file():
        fail(f"no private key at {args.key_file} (see docs/updates.md)")
    if args.key_file.stat().st_mode & 0o077:
        fail(f"{args.key_file} is readable by others; chmod 600 it")
    short, bundle_build = pkg_app_info(args.pkg)
    if short != args.version or bundle_build != build:
        fail(f"{args.pkg.name} carries Spectr.app {short} ({bundle_build}), "
             f"expected {args.version} ({build})")
    min_os = args.min_os or (app_min_os(args.app) if args.app else None)
    if not min_os:
        fail("pass --app (to read the deployment target) or --min-os")

    if args.channel == "release":
        url = args.download_url or f"{REPO}/releases/download/v{args.version}/{args.pkg.name}"
        release_url = args.release_url or f"{REPO}/releases/tag/v{args.version}"
        feed = RELEASE_FEED
    else:
        url = args.download_url or f"{REPO}/releases/download/{PRACTICE_TAG}/{args.pkg.name}"
        release_url = args.release_url or f"{REPO}/releases/tag/{PRACTICE_TAG}"
        feed = PRACTICE_FEED

    args.out.parent.mkdir(parents=True, exist_ok=True)
    notes_html = args.out.with_name(f"whats-new-{build}.html")
    rc = subprocess.run([sys.executable, str(HERE / "release_notes_html.py"),
                         "--version", args.version, "--notes", args.notes,
                         "--release-url", release_url, "--out", str(notes_html)]).returncode
    if rc != 0:
        return rc
    pub_date = args.pub_date or email.utils.formatdate(usegmt=True)

    with tempfile.TemporaryDirectory() as tmp:
        prev = Path(tmp) / "previous.xml"
        have_prev = fetch_previous(args.previous, args.channel, prev)
        if pulp_supports_key_file(args.pulp):
            work = Path(tmp) / "appcast.xml"
            if have_prev:
                shutil.copyfile(prev, work)
            cmd = [args.pulp, "ship", "appcast", "--url", str(args.pkg), "--download-url", url,
                   "--version", args.version, "--build-number", build, "--title", "Spectr",
                   "--min-os", min_os, "--sign-key-file", str(args.key_file),
                   "--notes-html-file", str(notes_html), "--full-release-notes-url", release_url,
                   "--installation-type", "package", "--pub-date", pub_date, "--output", str(work)]
            if subprocess.run(cmd).returncode != 0:
                fail("pulp ship appcast failed")
            shutil.copyfile(work, args.out)
        else:
            # Delete on SDK bump.
            sign_update = args.sign_update or Path(os.environ.get("SPARKLE_BIN", "")) / "sign_update"
            if not sign_update.is_file():
                fail("this Pulp CLI predates `pulp ship appcast --sign-key-file`; pass "
                     "--sign-update <Sparkle>/bin/sign_update (the build tree has one under "
                     "build/_deps/sparkle-*/dist/bin)")
            sig = _shim_sign(sign_update, args.key_file, args.pkg)
            if have_prev:
                items = check_sparkle.parse_items(prev.read_text())
                for it in items:
                    if (it.get("channel") or "") == "" and it["short"] != args.version and \
                       check_sparkle.version_key(it["version"]) >= check_sparkle.version_key(build):
                        fail(f"build {build} is not newer than {it['version']} already in the feed")
            _shim_write(args.out, prev if have_prev else None, {
                "short": args.version, "build": build, "notes": notes_html.read_text(),
                "pub_date": pub_date, "min_os": min_os, "channel": "",
                "release_url": release_url, "url": url, "length": args.pkg.stat().st_size,
                "signature": sig, "feed": feed})

    errors = check_sparkle.appcast_errors(args.out.read_text(), args.pkg.read_bytes(),
                                          check_sparkle.EXPECTED_PUBLIC_KEY, True, args.channel)
    for e in errors:
        print("FAIL " + e)
    if errors:
        return 1
    print(f"make_appcast: {args.out} ({args.channel}) offers Spectr {args.version} "
          f"build {build} from {url}; signature verified with SUPublicEDKey")
    print(f"make_appcast: What's New HTML: {notes_html}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
