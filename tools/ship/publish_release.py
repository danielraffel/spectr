#!/usr/bin/env python3
"""Publish a Spectr release so the Sparkle feed never points at a half release.

    publish_release.py --version 1.0.7 --pkg artifacts/Spectr-1.0.7.pkg \\
        --appcast artifacts/appcast.xml --notes release-notes-1.0.7.md \\
        [--target <commit>] [--dry-run]

The release feed is GitHub's `releases/latest/download/appcast.xml`, which
follows whichever release GitHub calls "latest" the moment it is published.
Uploading to an already-published release therefore opens a window in which
"latest" is the new release but its appcast.xml (or its package) is not there
yet: the feed 404s, or offers a package that does not exist. So the order is
fixed here:

  1. gh release create vX.Y.Z --draft     (nothing public; "latest" unchanged)
  2. gh release upload  vX.Y.Z pkg appcast (still a draft)
  3. gh release edit    vX.Y.Z --draft=false --latest   (publish, atomically
                                           complete)
  4. verify the LIVE feed: fetch releases/latest/download/appcast.xml and the
     package through the same `latest` URL, and run `check_sparkle.py appcast
     --expect-version X.Y.Z --channel release --require-notes` -- the newest
     item must be the version just published, signed for that very package.

"Latest" is the newest non-draft, non-prerelease release sorted by the date of
the COMMIT its tag points at, not by when it was published. Tag the newest
commit (--target defaults to HEAD); a release tagged on an older commit than
the current latest never becomes latest, and step 4 says so.

--dry-run prints the commands and runs nothing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

from check_macos_release import check_package as check_macos_package
from check_linux_release import check_archive as check_linux_archive
from check_windows_release import check_package as check_windows_package

HERE = Path(__file__).resolve().parent
REPO_SLUG = "danielraffel/spectr"
LATEST = f"https://github.com/{REPO_SLUG}/releases/latest/download"
PLATFORM_GH = "ghapp" if shutil.which("ghapp") else "gh"


def _digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return "sha256:" + h.hexdigest()


def check_draft(version: str, target: str, windows: tuple[Path, Path],
                macos: tuple[Path, Path] | None = None,
                linux: tuple[Path, Path] | None = None,
                allow_existing_macos: bool = False) -> None:
    """Refuse a changed draft, tag, or asset set before any publication step."""
    tag = f"v{version}"
    result = subprocess.run(
        ["gh", "release", "view", tag, "--repo", REPO_SLUG,
         "--json", "isDraft,assets"], capture_output=True, text=True, check=True)
    release = json.loads(result.stdout)
    tag_ref = subprocess.run(
        ["gh", "api", f"repos/{REPO_SLUG}/git/ref/tags/{tag}",
         "--jq", ".object.sha"], capture_output=True, text=True, check=True)
    if release.get("isDraft") is not True or tag_ref.stdout.strip() != target:
        raise ValueError(f"{tag}: draft state or tag target changed")
    assets = release.get("assets")
    if not isinstance(assets, list):
        raise ValueError(f"{tag}: release assets are unavailable")
    by_name = {}
    for asset in assets:
        name = asset.get("name")
        if not isinstance(name, str) or name in by_name:
            raise ValueError(f"{tag}: duplicate or invalid release asset name")
        by_name[name] = asset
    expected = {path.name: _digest(path) for path in windows}
    if macos:
        expected.update({path.name: _digest(path) for path in macos})
    if linux:
        expected.update({path.name: _digest(path) for path in linux})
    for name, digest in expected.items():
        if by_name.get(name, {}).get("digest") != digest:
            raise ValueError(f"{tag}: missing or changed draft asset {name}")
    allowed = {path.name for path in windows}
    if macos or allow_existing_macos:
        allowed.update((f"Spectr-{version}.pkg", "appcast.xml"))
    if linux:
        allowed.update(path.name for path in linux)
    if not (set(by_name) <= allowed if allow_existing_macos and not macos
            else set(by_name) == allowed):
        raise ValueError(f"{tag}: unexpected draft asset set")


def plan(version: str, pkg: Path, appcast: Path, notes: Path, target: str,
         title: str | None = None,
         windows: tuple[Path, Path] | None = None,
         linux: tuple[Path, Path] | None = None,
         reuse_draft: bool = False) -> list[list[str]]:
    tag = f"v{version}"
    if appcast.name != "appcast.xml":
        raise SystemExit("the release feed asset must be named appcast.xml")
    steps = [] if reuse_draft else [["gh", "release", "create", tag, "--repo", REPO_SLUG, "--draft",
         "--target", target, "--title", title or f"Spectr {version}",
         "--notes-file", str(notes)]]
    if reuse_draft:
        # A pre-created draft may have stale or empty body text. Keep the
        # public release notes tied to the exact source revision we publish.
        steps.append(["gh", "release", "edit", tag, "--repo", REPO_SLUG,
                      "--notes-file", str(notes)])
    upload = ["gh", "release", "upload", tag, "--repo", REPO_SLUG]
    if reuse_draft:
        upload.append("--clobber")
    upload.extend((str(pkg), str(appcast)))
    if not reuse_draft and windows:
        upload.extend(str(asset) for asset in windows)
    if not reuse_draft and linux:
        upload.extend(str(asset) for asset in linux)
    steps += [
        upload,
        ["gh", "release", "edit", tag, "--repo", REPO_SLUG, "--draft=false", "--latest"],
        [sys.executable, str(HERE / "check_sparkle.py"), "appcast",
         "--appcast", f"{LATEST}/appcast.xml", "--pkg", f"{LATEST}/{pkg.name}",
         "--expect-version", version, "--channel", "release", "--require-notes"],
    ]
    return steps


def intel_linux_plan(version: str, pkg: Path, linux: tuple[Path, Path],
                     notes: Path, target: str, title: str | None = None) -> list[list[str]]:
    """A separately versioned platform release must not move the ARM feed."""
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("invalid platform release version")
    if not re.fullmatch(r"[0-9a-f]{40}", target):
        raise ValueError("Intel/Linux publication requires an exact target SHA")
    expected = (f"Spectr-{version}-macos-x86_64.pkg",
                f"Spectr-v{version}-linux-aarch64.tar.xz",
                f"Spectr-v{version}-linux-x86_64.tar.xz")
    assets = (pkg, *linux)
    if tuple(path.name for path in assets) != expected:
        raise ValueError("Intel/Linux filenames must match the version and architectures")
    tag = f"v{version}"
    return [
        [PLATFORM_GH, "release", "create", tag, "--repo", REPO_SLUG, "--draft",
         "--latest=false", "--target", target, "--title", title or f"Spectr {version}",
         "--notes-file", str(notes)],
        [PLATFORM_GH, "release", "upload", tag, "--repo", REPO_SLUG,
         *(str(path) for path in assets)],
        [PLATFORM_GH, "release", "edit", tag, "--repo", REPO_SLUG,
         "--draft=false", "--latest=false"],
    ]


def _github_json(path: str) -> dict:
    result = subprocess.run([PLATFORM_GH, "api", path], capture_output=True,
                            text=True, check=True)
    return json.loads(result.stdout)


def _asset_digests(release: dict) -> dict[str, str]:
    assets = release.get("assets")
    if not isinstance(assets, list):
        raise ValueError("release assets are unavailable")
    result = {}
    for asset in assets:
        name, digest = asset.get("name"), asset.get("digest")
        if (not isinstance(name, str) or not name or name in result
                or not isinstance(digest, str)
                or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest)):
            raise ValueError("release contains duplicate names or missing asset digests")
        result[name] = digest
    return result


def _updater_snapshot() -> dict:
    release = _github_json(f"repos/{REPO_SLUG}/releases/latest")
    tag = release.get("tag_name")
    if (release.get("draft") is not False or release.get("prerelease") is not False
            or not isinstance(tag, str) or not re.fullmatch(r"v\d+\.\d+\.\d+", tag)):
        raise ValueError("current updater release is unavailable")
    assets = _asset_digests(release)
    if "appcast.xml" not in assets or f"Spectr-{tag[1:]}.pkg" not in assets:
        raise ValueError("current updater release is missing its feed or ARM package")
    return {"id": release["id"], "tag": tag, "assets": assets}


def _check_intel_linux_remote(version: str, target: str, assets: dict[str, str],
                              *, draft: bool) -> None:
    tag = f"v{version}"
    release = _github_json(f"repos/{REPO_SLUG}/releases/tags/{tag}")
    if (release.get("tag_name") != tag or release.get("draft") is not draft
            or release.get("prerelease") is not False
            or _asset_digests(release) != assets):
        raise ValueError("platform release state or exact asset set changed")
    ref = _github_json(f"repos/{REPO_SLUG}/git/ref/tags/{tag}").get("object", {})
    # The release may use an already-created annotated tag. Resolve its
    # immutable object rather than comparing the tag object's SHA to a commit.
    for _ in range(4):
        if ref.get("type") != "tag":
            break
        ref = _github_json(f"repos/{REPO_SLUG}/git/tags/{ref['sha']}").get("object", {})
    if ref.get("type") != "commit" or ref.get("sha") != target:
        raise ValueError("platform release tag does not name the qualified source")


def publish_intel_linux(version: str, pkg: Path, linux: tuple[Path, Path],
                        notes: Path, target: str, title: str | None = None,
                        *, dry_run: bool = False) -> int:
    steps = intel_linux_plan(version, pkg, linux, notes, target, title)
    if dry_run:
        for cmd in steps:
            print(" ".join(cmd))
        return 0
    assets = (pkg, *linux)
    for path in (*assets, notes):
        if not path.is_file():
            raise ValueError(f"missing {path}")
    expected = {path.name: _digest(path) for path in assets}
    check_macos_package(pkg, version, target, architectures=("x86_64",))
    subprocess.run(["pkgutil", "--check-signature", str(pkg)], check=True,
                   capture_output=True, text=True)
    subprocess.run(["xcrun", "stapler", "validate", str(pkg)], check=True,
                   capture_output=True, text=True)
    subprocess.run(["spctl", "--assess", "--type", "install", str(pkg)], check=True,
                   capture_output=True, text=True)
    for path, arch in zip(linux, ("aarch64", "x86_64")):
        check_linux_archive(path, arch, version, target)
    if {path.name: _digest(path) for path in assets} != expected:
        raise ValueError("qualified platform inputs changed during validation")
    updater = _updater_snapshot()
    subprocess.run(steps[0], check=True)
    _check_intel_linux_remote(version, target, {}, draft=True)
    subprocess.run(steps[1], check=True)
    _check_intel_linux_remote(version, target, expected, draft=True)
    if _updater_snapshot() != updater:
        raise ValueError("updater release changed; leaving platform release as a draft")
    subprocess.run(steps[2], check=True)
    _check_intel_linux_remote(version, target, expected, draft=False)
    if _updater_snapshot() != updater:
        raise ValueError("platform release published, but updater preservation is unproven")
    print(f"published v{version}: Intel/Linux assets verified; updater unchanged")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--version", required=True)
    ap.add_argument("--pkg", type=Path)
    ap.add_argument("--appcast", type=Path)
    ap.add_argument("--intel-pkg", type=Path,
                    help="publish a separate Intel/Linux release without moving the ARM updater")
    ap.add_argument("--notes", type=Path, required=True)
    ap.add_argument("--title")
    ap.add_argument("--target", help="commit to tag (default: HEAD; must be the newest)")
    ap.add_argument("--windows-arm64ec", type=Path,
                    help="verified Windows ARM64EC package to publish with macOS")
    ap.add_argument("--windows-arm64", type=Path,
                    help="verified native Windows ARM64 package to publish with macOS")
    ap.add_argument("--windows-x64", type=Path,
                    help="verified Windows x64 package to publish with macOS")
    ap.add_argument("--require-windows", action="store_true",
                    help="require native ARM64, ARM64EC, and x64 Windows assets")
    ap.add_argument("--linux-aarch64", type=Path,
                    help="verified Linux AArch64 archive")
    ap.add_argument("--linux-x86_64", type=Path,
                    help="verified Linux x86_64 archive")
    ap.add_argument("--require-linux", action="store_true",
                    help="require both qualified Linux archives")
    ap.add_argument("--verify-attempts", type=int, default=12,
                    help="live-feed checks, 15 s apart, before calling the publish broken")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--reuse-draft", action="store_true",
                    help="upload into an existing draft vX.Y.Z release")
    args = ap.parse_args()
    if args.intel_pkg:
        if (args.pkg or args.appcast or args.windows_arm64 or args.windows_arm64ec
                or args.windows_x64 or args.require_windows or args.reuse_draft):
            raise SystemExit("Intel/Linux publication cannot include updater/Windows inputs or reuse a draft")
        if not args.linux_aarch64 or not args.linux_x86_64 or not args.target:
            raise SystemExit("--intel-pkg requires both Linux archives and an exact --target")
        return publish_intel_linux(args.version, args.intel_pkg,
                                  (args.linux_aarch64, args.linux_x86_64),
                                  args.notes, args.target, args.title, dry_run=args.dry_run)
    if not args.pkg or not args.appcast:
        raise SystemExit("updater publication requires --pkg and --appcast")
    if args.pkg.name != f"Spectr-{args.version}.pkg":
        raise SystemExit(f"--pkg must be Spectr-{args.version}.pkg")
    if bool(args.windows_arm64ec) != bool(args.windows_x64):
        raise SystemExit("ARM64EC and x64 Windows assets must be supplied together")
    windows = ((args.windows_arm64, args.windows_arm64ec, args.windows_x64)
               if args.windows_arm64 else
               (args.windows_arm64ec, args.windows_x64)
               if args.windows_arm64ec else None)
    complete_windows = windows == (
        args.windows_arm64, args.windows_arm64ec, args.windows_x64)
    if (args.require_windows or args.reuse_draft) and not complete_windows:
        raise SystemExit(
            "complete release publication requires --windows-arm64, "
            "--windows-arm64ec, and --windows-x64 together")
    linux = ((args.linux_aarch64, args.linux_x86_64)
             if args.linux_aarch64 and args.linux_x86_64 else None)
    if args.require_linux and linux is None:
        raise SystemExit(
            "complete Linux publication requires --linux-aarch64 and --linux-x86_64 together")
    if (args.linux_aarch64 is None) != (args.linux_x86_64 is None):
        raise SystemExit("Linux AArch64 and x86_64 assets must be supplied together")
    if linux and (linux[0].name != f"Spectr-v{args.version}-linux-aarch64.tar.xz"
                  or linux[1].name != f"Spectr-v{args.version}-linux-x86_64.tar.xz"):
        raise SystemExit("Linux package filenames must match the release version and architecture")
    if windows and ((len(windows) == 3 and windows[0].name != f"Spectr-windows-arm64-{args.version}.zip")
                    or windows[-2].name != f"Spectr-windows-arm64ec-{args.version}.zip"
                    or windows[-1].name != f"Spectr-windows-x64-{args.version}.zip"):
        raise SystemExit("Windows package filenames must match the release version and architecture")
    target = args.target
    if not target:
        target = "HEAD" if args.dry_run else subprocess.run(
            ["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True
        ).stdout.strip()
    steps = plan(args.version, args.pkg, args.appcast, args.notes, target,
                 args.title, windows, linux, args.reuse_draft)
    if args.dry_run:
        for cmd in steps:
            print(" ".join(cmd))
        return 0
    for f in (args.pkg, args.appcast, args.notes):
        if not f.is_file():
            raise SystemExit(f"missing {f}")
    check_macos_package(args.pkg, args.version, target)
    print(f"verified macOS package source={target}", flush=True)
    if windows:
        arches = ("arm64-win", "arm64ec-win", "x86_64-win") if len(windows) == 3 else ("arm64ec-win", "x86_64-win")
        for asset, arch in zip(windows, arches):
            if not asset.is_file():
                raise SystemExit(f"missing {asset}")
            print(f"verified {arch} sha256=" +
                  check_windows_package(asset, arch, args.version, target), flush=True)
    if linux:
        for asset, arch in zip(linux, ("aarch64", "x86_64")):
            print(f"verified linux-{arch} sha256=" +
                  check_linux_archive(asset, arch, args.version, target), flush=True)
    if args.reuse_draft:
        if not windows or not re.fullmatch(r"[0-9a-f]{40}", target):
            raise SystemExit("--reuse-draft requires both Windows assets and an exact target SHA")
        check_draft(args.version, target, windows, linux=linux,
                    allow_existing_macos=True)
    mutation_steps = steps[:3]
    for index, cmd in enumerate(mutation_steps):
        if args.reuse_draft and index == 2:
            check_draft(args.version, target, windows,
                        macos=(args.pkg, args.appcast), linux=linux)
        print("+ " + " ".join(cmd), flush=True)
        if subprocess.run(cmd).returncode != 0:
            print("publish_release: stopped; the release is still a draft unless the "
                  "last step above was the publish", file=sys.stderr)
            return 1
    verify = steps[3]
    for attempt in range(1, args.verify_attempts + 1):
        print(f"+ ({attempt}/{args.verify_attempts}) " + " ".join(verify), flush=True)
        if subprocess.run(verify).returncode == 0:
            print(f"publish_release: the live feed offers Spectr {args.version}")
            return 0
        time.sleep(15)
    print("publish_release: PUBLISHED, BUT THE LIVE FEED DOES NOT OFFER THIS RELEASE. "
          "Check that it is not a prerelease, carries appcast.xml, and that its tag is on "
          "the newest commit (GitHub's 'latest' sorts by commit date).", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
