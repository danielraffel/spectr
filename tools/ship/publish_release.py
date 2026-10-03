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
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_SLUG = "danielraffel/spectr"
LATEST = f"https://github.com/{REPO_SLUG}/releases/latest/download"


def plan(version: str, pkg: Path, appcast: Path, notes: Path, target: str,
         title: str | None = None) -> list[list[str]]:
    tag = f"v{version}"
    if appcast.name != "appcast.xml":
        raise SystemExit("the release feed asset must be named appcast.xml")
    return [
        ["gh", "release", "create", tag, "--repo", REPO_SLUG, "--draft",
         "--target", target, "--title", title or f"Spectr {version}",
         "--notes-file", str(notes)],
        ["gh", "release", "upload", tag, "--repo", REPO_SLUG, str(pkg), str(appcast)],
        ["gh", "release", "edit", tag, "--repo", REPO_SLUG, "--draft=false", "--latest"],
        [sys.executable, str(HERE / "check_sparkle.py"), "appcast",
         "--appcast", f"{LATEST}/appcast.xml", "--pkg", f"{LATEST}/{pkg.name}",
         "--expect-version", version, "--channel", "release", "--require-notes"],
    ]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--version", required=True)
    ap.add_argument("--pkg", type=Path, required=True)
    ap.add_argument("--appcast", type=Path, required=True)
    ap.add_argument("--notes", type=Path, required=True)
    ap.add_argument("--title")
    ap.add_argument("--target", help="commit to tag (default: HEAD; must be the newest)")
    ap.add_argument("--verify-attempts", type=int, default=12,
                    help="live-feed checks, 15 s apart, before calling the publish broken")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    if args.pkg.name != f"Spectr-{args.version}.pkg":
        raise SystemExit(f"--pkg must be Spectr-{args.version}.pkg")
    target = args.target
    if not target:
        target = "HEAD" if args.dry_run else subprocess.run(
            ["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True
        ).stdout.strip()
    steps = plan(args.version, args.pkg, args.appcast, args.notes, target, args.title)
    if args.dry_run:
        for cmd in steps:
            print(" ".join(cmd))
        return 0
    for f in (args.pkg, args.appcast, args.notes):
        if not f.is_file():
            raise SystemExit(f"missing {f}")
    for cmd in steps[:3]:
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
