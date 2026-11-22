#!/usr/bin/env python3
"""Tests for Spectr's update-feed tooling: the What's New HTML rendered from a
GitHub release body, and an appcast item that carries it.

Runs without the private key: fixtures sign with a throwaway seed through the
pure-Python Ed25519 in ed25519_verify.py, and the checks verify against that
seed's public key exactly as they verify release items against SUPublicEDKey.
"""
from __future__ import annotations

import base64
import json
import subprocess
import sys
import tempfile
import unittest
import urllib.error
from unittest import mock
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import check_sparkle  # noqa: E402
import ed25519_verify  # noqa: E402
import make_appcast  # noqa: E402
import publish_release  # noqa: E402
import release_notes_html  # noqa: E402
import release_github_cli  # noqa: E402

RELEASE_BODY = """Spectr 1.0.7 adds Sparkle updates.

**Install:** download `Spectr-1.0.7.pkg` and open it.

## What's new
- **Check for Updates.** Spectr's app menu can now find and install new versions.
- Notes render <b>safely</b> & with [a link](https://example.com/x?a=1&b=2).
  A wrapped continuation line stays in its bullet.

Built with the Pulp v0.896.0 SDK.
"""
RELEASE_URL = "https://github.com/danielraffel/spectr/releases/tag/v1.0.7"


class WhatsNewHtml(unittest.TestCase):
    def setUp(self) -> None:
        self.html = release_notes_html.render("1.0.7", RELEASE_BODY, RELEASE_URL)

    def test_lists_the_release_bullets(self) -> None:
        self.assertIn("<h2>What's new</h2>", self.html)
        self.assertIn("<li><strong>Check for Updates.</strong> Spectr", self.html)
        self.assertIn("A wrapped continuation line stays in its bullet.</li>", self.html)

    def test_escapes_raw_html_but_keeps_markdown_links(self) -> None:
        self.assertIn("&lt;b&gt;safely&lt;/b&gt; &amp;", self.html)
        self.assertNotIn("<b>safely</b>", self.html)
        self.assertIn('<a href="https://example.com/x?a=1&amp;b=2">a link</a>', self.html)

    def test_drops_installer_instructions_and_links_the_release(self) -> None:
        self.assertNotIn("Install:", self.html)
        self.assertIn(f'<a href="{RELEASE_URL}">Full release notes on GitHub', self.html)

    def test_is_styled_dark_like_the_editor(self) -> None:
        self.assertIn("background: #05070a", self.html)
        self.assertIn("color-scheme: dark", self.html)

    def test_never_closes_the_cdata_section_it_lives_in(self) -> None:
        html = release_notes_html.render("1.0.7", "- odd ]]> text", RELEASE_URL)
        self.assertNotIn("]]>", html)


class AppcastCarriesNotes(unittest.TestCase):
    def test_item_carries_signed_package_and_whats_new(self) -> None:
        seed = b"\x07" * 32
        public = base64.b64encode(ed25519_verify.public_key_from_seed(seed)).decode()
        pkg = b"fixture installer package bytes"
        sig = base64.b64encode(ed25519_verify.sign(seed, pkg)).decode()
        notes = release_notes_html.render("1.0.7", RELEASE_BODY, RELEASE_URL)
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "appcast.xml"
            make_appcast._shim_write(out, None, {
                "short": "1.0.7", "build": "1.0.7", "notes": notes,
                "pub_date": "Fri, 02 Oct 2026 12:00:00 GMT", "min_os": "13.4",
                "channel": "", "release_url": RELEASE_URL,
                "url": "https://github.com/danielraffel/spectr/releases/download/v1.0.7/Spectr-1.0.7.pkg",
                "length": len(pkg), "signature": sig, "feed": make_appcast.RELEASE_FEED})
            xml = out.read_text()
            self.assertEqual(check_sparkle.appcast_errors(xml, pkg, public, True, "release"), [])
            item = check_sparkle.parse_items(xml)[0]
            self.assertIn("Check for Updates.", item["description"])
            self.assertIn(RELEASE_URL, item["description"])
            self.assertEqual(item["full_notes"], RELEASE_URL)
            self.assertEqual(item["type"], "package")

            # Negative control: the same feed without notes is refused.
            bare = xml.replace(notes, "")
            self.assertNotEqual(check_sparkle.appcast_errors(bare, pkg, public, True, "release"), [])

            # Re-publishing 1.0.7 replaces its entry; publishing 1.0.8 keeps 1.0.7 below it.
            prev = Path(tmp) / "prev.xml"
            prev.write_text(xml)
            make_appcast._shim_write(out, prev, {
                "short": "1.0.8", "build": "1.0.8", "notes": notes,
                "pub_date": "Sat, 03 Oct 2026 12:00:00 GMT", "min_os": "13.4",
                "channel": "", "release_url": RELEASE_URL, "url": "https://x/Spectr-1.0.8.pkg",
                "length": len(pkg), "signature": sig, "feed": make_appcast.RELEASE_FEED})
            builds = [i["version"] for i in check_sparkle.parse_items(out.read_text())]
            self.assertEqual(builds, ["1.0.8", "1.0.7"])


class FallbackSigner(unittest.TestCase):
    """With a Pulp CLI that predates `pulp ship appcast --sign-key-file`, the
    documented command (no --sign-update) must still find Sparkle's signer: the
    one the build tree that made --app unpacked."""

    def _tree(self, tmp: str, *versions: str) -> Path:
        build = Path(tmp) / "build"
        (build / "Spectr.app").mkdir(parents=True)
        for v in versions:
            tool = build / "_deps" / f"sparkle-{v}" / "dist" / "bin" / "sign_update"
            tool.parent.mkdir(parents=True)
            tool.write_text("#!/bin/sh\n")
        return build

    def test_finds_the_signer_beside_the_app(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = self._tree(tmp, "2.10.0")
            self.assertEqual(make_appcast.default_sign_update(build / "Spectr.app"),
                             (build / "_deps/sparkle-2.10.0/dist/bin/sign_update").resolve())

    def test_prefers_the_newest_sparkle(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            build = self._tree(tmp, "2.9.1", "2.10.0")
            self.assertIn("sparkle-2.10.0",
                          str(make_appcast.default_sign_update(build / "Spectr.app")))

    def test_none_without_a_build_tree(self) -> None:
        # Negative controls: no --app, and an app whose tree unpacked no Sparkle.
        self.assertIsNone(make_appcast.default_sign_update(None))
        with tempfile.TemporaryDirectory() as tmp:
            build = self._tree(tmp)
            self.assertIsNone(make_appcast.default_sign_update(build / "Spectr.app"))


class PublishOrdering(unittest.TestCase):
    """`latest/download/appcast.xml` follows a release the moment it is
    published, so the assets must already be on it: create a draft, upload,
    publish, then prove the LIVE feed's newest item is this version."""

    def test_draft_then_upload_then_publish_then_verify(self) -> None:
        steps = publish_release.plan("1.0.7", Path("a/Spectr-1.0.7.pkg"),
                                     Path("a/appcast.xml"), Path("n.md"), "abc123")
        self.assertEqual([s[2] if s[0] == "gh" else "verify" for s in steps],
                         ["create", "upload", "edit", "verify"])
        self.assertIn("--draft", steps[0])
        self.assertIn("abc123", steps[0])
        self.assertIn("a/Spectr-1.0.7.pkg", steps[1])
        self.assertIn("a/appcast.xml", steps[1])
        self.assertIn("--draft=false", steps[2])
        verify = steps[3]
        self.assertIn(f"{publish_release.LATEST}/appcast.xml", verify)
        self.assertIn(f"{publish_release.LATEST}/Spectr-1.0.7.pkg", verify)
        self.assertEqual(verify[verify.index("--expect-version") + 1], "1.0.7")

    def test_windows_assets_join_the_draft_before_publication(self) -> None:
        arm = Path("a/Spectr-windows-arm64-1.0.9.zip")
        x64 = Path("a/Spectr-windows-x64-1.0.9.zip")
        steps = publish_release.plan("1.0.9", Path("a/Spectr-1.0.9.pkg"),
                                     Path("a/appcast.xml"), Path("n.md"),
                                     "a" * 40, windows=(arm, x64))
        self.assertLess(steps.index(steps[1]), steps.index(steps[2]))
        self.assertIn(str(arm), steps[1])
        self.assertIn(str(x64), steps[1])

    def test_native_windows_publication_does_not_require_arm64ec(self) -> None:
        argv = [
            "publish_release.py", "--version", "1.0.9",
            "--pkg", "a/Spectr-1.0.9.pkg", "--appcast", "a/appcast.xml",
            "--notes", "n.md", "--windows-arm64",
            "a/Spectr-windows-arm64-1.0.9.zip", "--windows-x64",
            "a/Spectr-windows-x64-1.0.9.zip", "--require-windows",
            "--dry-run",
        ]
        with mock.patch.object(sys, "argv", argv):
            self.assertEqual(publish_release.main(), 0)

    def test_require_windows_rejects_an_incomplete_platform_set(self) -> None:
        argv = [
            "publish_release.py", "--version", "1.0.9",
            "--pkg", "a/Spectr-1.0.9.pkg", "--appcast", "a/appcast.xml",
            "--notes", "n.md", "--windows-arm64ec",
            "a/Spectr-windows-arm64ec-1.0.9.zip", "--windows-x64",
            "a/Spectr-windows-x64-1.0.9.zip", "--require-windows",
            "--dry-run",
        ]
        with mock.patch.object(sys, "argv", argv):
            with self.assertRaises(SystemExit) as error:
                publish_release.main()
        self.assertIn("requires --windows-arm64", str(error.exception))

    def test_require_linux_rejects_an_incomplete_platform_set(self) -> None:
        argv = [
            "publish_release.py", "--version", "1.0.9",
            "--pkg", "a/Spectr-1.0.9.pkg", "--appcast", "a/appcast.xml",
            "--notes", "n.md", "--linux-aarch64",
            "a/Spectr-v1.0.9-linux-aarch64.tar.xz", "--require-linux",
            "--dry-run",
        ]
        with mock.patch.object(sys, "argv", argv):
            with self.assertRaises(SystemExit) as error:
                publish_release.main()
        self.assertIn("requires --linux-aarch64", str(error.exception))

    def test_reuse_draft_never_creates_or_publishes_partial_assets(self) -> None:
        native = Path("a/Spectr-windows-arm64-1.0.9.zip")
        steps = publish_release.plan("1.0.9", Path("a/Spectr-1.0.9.pkg"),
                                     Path("a/appcast.xml"), Path("n.md"),
                                     "a" * 40, windows=(
                                         native,
                                         Path("a/Spectr-windows-arm64ec-1.0.9.zip"),
                                         Path("a/Spectr-windows-x64-1.0.9.zip")),
                                     reuse_draft=True)
        self.assertEqual([s[2] if s[0] == "gh" else "verify" for s in steps],
                         ["edit", "upload", "edit", "verify"])
        self.assertIn("--notes-file", steps[0])
        self.assertNotIn(str(Path("a/Spectr-windows-arm64ec-1.0.9.zip")), steps[1])
        self.assertNotIn(str(Path("a/Spectr-windows-x64-1.0.9.zip")), steps[1])
        self.assertIn("--clobber", steps[1])

    def test_reuse_draft_rechecks_asset_identity_before_publish(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            native = root / "Spectr-windows-arm64-1.0.9.zip"
            arm = root / "Spectr-windows-arm64ec-1.0.9.zip"
            x64 = root / "Spectr-windows-x64-1.0.9.zip"
            pkg = root / "Spectr-1.0.9.pkg"
            appcast = root / "appcast.xml"
            notes = root / "notes.md"
            for path in (native, arm, x64, pkg, appcast, notes):
                path.write_bytes(path.name.encode())
            target = "a" * 40
            windows = (native, arm, x64)
            expected = {path.name: publish_release._digest(path)
                        for path in (native, arm, x64, pkg, appcast)}
            calls = []

            def gh(cmd, **kwargs):
                calls.append(cmd)
                if cmd[:3] == ["gh", "release", "view"]:
                    names = [native.name, arm.name, x64.name]
                    if any(c[:3] == ["gh", "release", "upload"] for c in calls):
                        names += [pkg.name, appcast.name]
                    return subprocess.CompletedProcess(cmd, 0, json.dumps({
                        "isDraft": True,
                        "assets": [{"name": name, "digest": expected[name]}
                                   for name in names],
                    }))
                if cmd[:2] == ["gh", "api"]:
                    return subprocess.CompletedProcess(cmd, 0, target + "\n")
                return subprocess.CompletedProcess(cmd, 0, "")

            argv = ["publish_release.py", "--version", "1.0.9", "--target", target,
                    "--pkg", str(pkg), "--appcast", str(appcast), "--notes", str(notes),
                    "--windows-arm64", str(native), "--windows-arm64ec", str(arm),
                    "--windows-x64", str(x64),
                    "--reuse-draft", "--verify-attempts", "1"]
            with mock.patch.object(sys, "argv", argv), \
                    mock.patch.object(publish_release, "check_macos_package"), \
                    mock.patch.object(publish_release, "check_windows_package", return_value="digest"), \
                    mock.patch.object(publish_release.subprocess, "run", side_effect=gh):
                self.assertEqual(publish_release.main(), 0)
            self.assertEqual(len([c for c in calls if c[:3] == ["gh", "release", "view"]]), 2)
            self.assertEqual(len([c for c in calls if c[:3] == ["gh", "release", "edit"]]), 2)
            self.assertEqual(len([c for c in calls if c[:3] == ["gh", "release", "create"]]), 0)

            def wrong_tag(cmd, **kwargs):
                result = gh(cmd, **kwargs)
                if cmd[:2] == ["gh", "api"]:
                    return subprocess.CompletedProcess(cmd, 0, "b" * 40)
                return result

            calls.clear()
            with mock.patch.object(sys, "argv", argv), \
                    mock.patch.object(publish_release, "check_macos_package"), \
                    mock.patch.object(publish_release, "check_windows_package", return_value="digest"), \
                    mock.patch.object(publish_release.subprocess, "run", side_effect=wrong_tag):
                with self.assertRaises(ValueError):
                    publish_release.main()
            self.assertFalse(any(c[:3] == ["gh", "release", "edit"] for c in calls))

            def changed_after_upload(cmd, **kwargs):
                result = gh(cmd, **kwargs)
                if cmd[:3] == ["gh", "release", "view"] and any(
                        c[:3] == ["gh", "release", "upload"] for c in calls):
                    payload = json.loads(result.stdout)
                    payload["assets"][-1]["digest"] = "sha256:" + "0" * 64
                    return subprocess.CompletedProcess(cmd, 0, json.dumps(payload))
                return result

            calls.clear()
            with mock.patch.object(sys, "argv", argv), \
                    mock.patch.object(publish_release, "check_macos_package"), \
                    mock.patch.object(publish_release, "check_windows_package", return_value="digest"), \
                    mock.patch.object(publish_release.subprocess, "run", side_effect=changed_after_upload):
                with self.assertRaises(ValueError):
                    publish_release.main()
            self.assertTrue(any(c[:3] == ["gh", "release", "upload"] for c in calls))
            self.assertFalse(any(c[:3] == ["gh", "release", "edit"] and
                                 "--draft=false" in c for c in calls))

    def test_expect_version_reads_the_newest_item(self) -> None:
        feed = ('<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">'
                '<channel><item><sparkle:version>1.0.6</sparkle:version>'
                '<sparkle:shortVersionString>1.0.6</sparkle:shortVersionString></item>'
                '</channel></rss>')
        self.assertEqual(check_sparkle.expected_version_errors(feed, "1.0.6"), [])
        # Negative control: "latest" still on the previous release.
        self.assertNotEqual(check_sparkle.expected_version_errors(feed, "1.0.7"), [])


class NoAccidentalNewFeed(unittest.TestCase):
    """A 404 on the live feed usually means the newest release lacks its
    appcast; starting a fresh feed then would drop every older item."""

    def _404(self, *a, **k):
        raise urllib.error.HTTPError(make_appcast.RELEASE_FEED, 404, "Not Found", {}, None)

    def test_a_404_is_refused_without_new_feed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch.object(make_appcast.urllib.request, "urlopen", self._404):
            dest = Path(tmp) / "prev.xml"
            with self.assertRaises(SystemExit):
                make_appcast.fetch_previous(None, "release", dest)
            self.assertFalse(make_appcast.fetch_previous(None, "release", dest, new_feed=True))

    def test_previous_none_on_the_release_channel_needs_new_feed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / "prev.xml"
            with self.assertRaises(SystemExit):
                make_appcast.fetch_previous("none", "release", dest)
            self.assertFalse(make_appcast.fetch_previous("none", "release", dest, new_feed=True))
            # The practice feed is throwaway and documented with --previous none.
            self.assertFalse(make_appcast.fetch_previous("none", "practice", dest))


class IntelLinuxPublish(unittest.TestCase):
    def test_plan_preserves_the_updater_and_never_clobbers_assets(self) -> None:
        paths = (Path("Spectr-1.0.11-macos-x86_64.pkg"),
                 Path("Spectr-v1.0.11-linux-aarch64.tar.xz"),
                 Path("Spectr-v1.0.11-linux-x86_64.tar.xz"))
        steps = publish_release.intel_linux_plan(
            "1.0.11", paths[0], paths[1:], Path("notes.md"), "a" * 40)
        self.assertEqual([cmd[2] for cmd in steps], ["create", "upload", "edit"])
        self.assertIn("--draft", steps[0])
        self.assertIn("--latest=false", steps[0])
        self.assertIn("--draft=false", steps[2])
        self.assertIn("--latest=false", steps[2])
        self.assertNotIn("--clobber", steps[1])
        self.assertFalse(any("appcast.xml" in arg for cmd in steps for arg in cmd))
        for target, pkg in (("main", paths[0]),
                            ("a" * 40, Path("Spectr-1.0.11.pkg"))):
            with self.assertRaises(ValueError):
                publish_release.intel_linux_plan(
                    "1.0.11", pkg, paths[1:], Path("notes.md"), target)

    def test_cli_refuses_partial_or_mixed_publication_modes(self) -> None:
        base = ["publish_release.py", "--version", "1.0.11", "--intel-pkg",
                "Spectr-1.0.11-macos-x86_64.pkg", "--notes", "notes.md",
                "--target", "a" * 40, "--dry-run"]
        linux = ["--linux-aarch64", "Spectr-v1.0.11-linux-aarch64.tar.xz",
                 "--linux-x86_64", "Spectr-v1.0.11-linux-x86_64.tar.xz"]
        for extra in ([], linux + ["--appcast", "appcast.xml"],
                      linux + ["--require-windows"], linux + ["--reuse-draft"]):
            with self.subTest(extra=extra), mock.patch.object(sys, "argv", base + extra), \
                    mock.patch.object(publish_release.subprocess, "run") as run:
                with self.assertRaises(SystemExit):
                    publish_release.main()
                run.assert_not_called()

    def _exercise(self, fault: str | None = None) -> list[list[str]]:
        calls = []
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pkg = root / "Spectr-1.0.11-macos-x86_64.pkg"
            linux = (root / "Spectr-v1.0.11-linux-aarch64.tar.xz",
                     root / "Spectr-v1.0.11-linux-x86_64.tar.xz")
            notes = root / "notes.md"
            for path in (pkg, *linux, notes):
                path.write_bytes(path.name.encode())
            expected = {path.name: publish_release._digest(path) for path in (pkg, *linux)}
            updater = {"id": 7, "tag_name": "v1.0.10", "draft": False,
                       "prerelease": False, "assets": [
                           {"name": "appcast.xml", "digest": "sha256:" + "1" * 64},
                           {"name": "Spectr-1.0.10.pkg", "digest": "sha256:" + "2" * 64}]}
            snapshots = 0
            uploaded = False
            published = False

            def gh(cmd, **kwargs):
                nonlocal snapshots, uploaded, published
                calls.append(cmd)
                if cmd[0] in ("pkgutil", "xcrun", "spctl"):
                    if ((fault == "invalid-signature" and cmd[0] == "pkgutil")
                            or (fault == "invalid-notarization" and cmd[0] == "xcrun")
                            or (fault == "gatekeeper-refusal" and cmd[0] == "spctl")):
                        raise subprocess.CalledProcessError(1, cmd)
                    return subprocess.CompletedProcess(cmd, 0)
                if cmd[1] == "api":
                    if cmd[2].endswith("/releases/latest"):
                        snapshots += 1
                        value = json.loads(json.dumps(updater))
                        if ((fault == "updater-before-publish" and snapshots >= 2)
                                or (fault == "updater-after-publish" and snapshots >= 3)):
                            value["assets"][0]["digest"] = "sha256:" + "9" * 64
                    elif "/git/ref/" in cmd[2]:
                        if fault == "tag-api-forbidden":
                            raise subprocess.CalledProcessError(
                                1, cmd, stderr="gh: Forbidden (HTTP 403)")
                        if published and fault == "missing-published-tag":
                            raise subprocess.CalledProcessError(
                                1, cmd, stderr="gh: Not Found (HTTP 404)")
                        if not published and fault != "annotated-tag" and fault != "wrong-source":
                            raise subprocess.CalledProcessError(
                                1, cmd, stderr="gh: Not Found (HTTP 404)")
                        value = {"object": {"type": "commit", "sha": (
                            "b" * 40 if fault == "wrong-source" else "a" * 40)}}
                        if fault == "annotated-tag":
                            value = {"object": {"type": "tag", "sha": "c" * 40}}
                    elif "/git/tags/" in cmd[2]:
                        value = {"object": {"type": "commit", "sha": "a" * 40}}
                    else:
                        if "/releases/tags/" in cmd[2] and not published:
                            raise subprocess.CalledProcessError(
                                1, cmd, stderr="gh: Not Found (HTTP 404)")
                        assets = [{"name": name, "digest": digest}
                                  for name, digest in expected.items()] if uploaded else []
                        value = {"id": 11, "tag_name": "v1.0.11", "draft": not published,
                                 "target_commitish": "a" * 40,
                                 "prerelease": False, "assets": assets}
                        if uploaded and fault == "changed-upload":
                            value["assets"][0]["digest"] = "sha256:" + "0" * 64
                        if uploaded and fault == "extra-appcast":
                            value["assets"].append({"name": "appcast.xml",
                                                    "digest": "sha256:" + "0" * 64})
                        if uploaded and fault == "missing-digest":
                            value["assets"][0].pop("digest")
                        if uploaded and fault == "duplicate-name":
                            value["assets"].append(value["assets"][0])
                        if fault == "wrong-draft-target" or (
                                uploaded and fault == "changed-draft-target"):
                            value["target_commitish"] = "b" * 40
                        if uploaded and fault == "replaced-release":
                            value["id"] = 12
                        if "/releases?" in cmd[2]:
                            self.assertIn("--paginate", cmd)
                            self.assertIn("--slurp", cmd)
                            value = [[updater], [value]]
                            if fault == "missing-draft":
                                value = [[updater]]
                            elif fault == "duplicate-draft":
                                value[1].append(value[1][0])
                            elif fault == "invalid-release-list":
                                value = {"message": "not a release list"}
                    return subprocess.CompletedProcess(cmd, 0, json.dumps(value))
                if cmd[2] == "upload":
                    uploaded = True
                if cmd[2] == "edit":
                    published = True
                return subprocess.CompletedProcess(cmd, 0)

            def macos(*args, **kwargs):
                self.assertEqual(kwargs["architectures"], ("x86_64",))
                if fault == "invalid-package":
                    raise ValueError("invalid package provenance")
                if fault == "changed-local-input":
                    pkg.write_bytes(b"replacement after validation began")

            with mock.patch.object(publish_release, "check_macos_package", side_effect=macos), \
                    mock.patch.object(publish_release, "check_linux_archive") as validate_linux, \
                    mock.patch.object(publish_release.subprocess, "run", side_effect=gh):
                if fault and fault != "annotated-tag":
                    with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                        publish_release.publish_intel_linux(
                            "1.0.11", pkg, linux, notes, "a" * 40)
                else:
                    self.assertEqual(publish_release.publish_intel_linux(
                        "1.0.11", pkg, linux, notes, "a" * 40), 0)
                    self.assertEqual(validate_linux.call_args_list, [
                        mock.call(linux[0], "aarch64", "1.0.11", "a" * 40),
                        mock.call(linux[1], "x86_64", "1.0.11", "a" * 40)])
        return calls

    def test_publication_binds_both_tag_forms_assets_and_updater(self) -> None:
        for fault in (None, "annotated-tag"):
            with self.subTest(fault=fault):
                calls = self._exercise(fault)
                self.assertEqual([cmd[2] for cmd in calls if cmd[1] == "release"],
                                 ["create", "upload", "edit"])
                self.assertFalse(any("/releases/tags/" in cmd[2] for cmd in calls
                                     if cmd[1] == "api"))
                self.assertEqual(sum("/releases/11" in cmd[2] for cmd in calls
                                     if cmd[1] == "api"), 3)

    def test_invalid_local_inputs_do_not_create_a_release(self) -> None:
        for fault in ("invalid-package", "changed-local-input", "invalid-signature",
                      "invalid-notarization", "gatekeeper-refusal"):
            with self.subTest(fault=fault):
                calls = self._exercise(fault)
                self.assertFalse(any(cmd[0] == publish_release.PLATFORM_GH for cmd in calls))

    def test_remote_changes_leave_the_release_unpublished(self) -> None:
        for fault in ("wrong-source", "changed-upload", "extra-appcast",
                      "missing-digest", "duplicate-name", "updater-before-publish",
                      "wrong-draft-target", "changed-draft-target", "replaced-release",
                      "missing-draft", "duplicate-draft", "invalid-release-list",
                      "tag-api-forbidden"):
            with self.subTest(fault=fault):
                calls = self._exercise(fault)
                self.assertFalse(any(cmd[1:3] == ["release", "edit"] for cmd in calls))

    def test_changed_updater_after_publication_is_not_reported_as_success(self) -> None:
        calls = self._exercise("updater-after-publish")
        self.assertTrue(any(cmd[1:3] == ["release", "edit"] for cmd in calls))

    def test_published_release_requires_the_actual_tag(self) -> None:
        calls = self._exercise("missing-published-tag")
        self.assertTrue(any(cmd[1:3] == ["release", "edit"] for cmd in calls))


class IntelBackfill(unittest.TestCase):
    """A targeted repair must never turn a six-platform release into three."""

    def exercise(self, fault=None, *, absent=False, same_latest=False, dry_run=False):
        import copy
        with tempfile.TemporaryDirectory() as directory:
            pkg = Path(directory) / "Spectr-1.0.11-macos-x86_64.pkg"
            pkg.write_bytes(b"qualified Intel installer")
            digest = publish_release._digest(pkg)
            names = [pkg.name, "Spectr-1.0.11-macos-arm64.pkg",
                     "Spectr-windows-arm64-1.0.11.zip", "Spectr-windows-x64-1.0.11.zip",
                     "Spectr-v1.0.11-linux-aarch64.tar.xz", "Spectr-v1.0.11-linux-x86_64.tar.xz",
                     "appcast.xml"]
            baseline = {"metadata": {"id": 11, "tag_name": "v1.0.11", "draft": False,
                                     "prerelease": False, "body": "| ARM | Intel | Linux | Windows |",
                                     "name": "Spectr 1.0.11", "published_at": "date"},
                        "assets": {name: {"id": i + 1, "url": f"asset/{i + 1}", "size": 7,
                                          "digest": "sha256:" + "1" * 64}
                                   for i, name in enumerate(names)}}
            if absent:
                baseline["assets"].pop(pkg.name)
            latest = copy.deepcopy(baseline)
            if not same_latest:
                latest["metadata"]["id"] = 10
                latest["metadata"]["tag_name"] = "v1.0.10"
            uploaded = False
            snapshots = 0
            mutation_calls = []

            def snapshot(endpoint):
                nonlocal snapshots
                snapshots += 1
                is_latest = endpoint.endswith("/latest")
                result = copy.deepcopy(latest if is_latest else baseline)
                if uploaded and (not is_latest or same_latest):
                    result["assets"][pkg.name] = {"id": 50, "url": "asset/50", "size": pkg.stat().st_size,
                                                  "digest": digest}
                    if fault == "wrong-upload":
                        result["assets"][pkg.name]["digest"] = "sha256:" + "0" * 64
                    if fault == "wrong-size":
                        result["assets"][pkg.name]["size"] += 1
                    if fault == "deleted-linux":
                        result["assets"].pop(names[-2])
                    if fault == "changed-windows":
                        result["assets"][names[2]]["digest"] = "sha256:" + "0" * 64
                    if fault == "changed-notes":
                        result["metadata"]["body"] = "incomplete replacement table"
                    if fault == "extra-asset":
                        result["assets"]["unexpected"] = {"digest": digest}
                if fault == "concurrent-before-upload" and snapshots >= 3:
                    result["metadata"]["body"] = "changed concurrently"
                if fault == "changed-latest" and uploaded and is_latest:
                    result["metadata"]["id"] = 999
                if fault == "read-failed" and snapshots >= 3:
                    raise subprocess.CalledProcessError(1, ["ghapp", "api"])
                return result

            def run(cmd, **kwargs):
                nonlocal uploaded
                if cmd[0] in ("pkgutil", "xcrun", "spctl"):
                    if fault == cmd[0]:
                        raise subprocess.CalledProcessError(1, cmd)
                else:
                    mutation_calls.append(cmd)
                    uploaded = True
                return subprocess.CompletedProcess(cmd, 0, stdout="")

            def package(*args, **kwargs):
                self.assertEqual(kwargs, {"architectures": ("x86_64",), "sdk_source_sha": "b" * 40})
                if fault == "invalid-package":
                    raise ValueError("package rejected")
                if fault == "changed-input":
                    pkg.write_bytes(b"not the qualified bytes")

            with mock.patch.object(publish_release, "_published_snapshot", side_effect=snapshot), \
                    mock.patch.object(publish_release, "_backfill_actions_context", return_value=True), \
                    mock.patch.object(publish_release, "actions_gh", return_value="/opt/homebrew/Cellar/gh/fixture/bin/gh"), \
                    mock.patch.object(publish_release, "_check_backfill_tag") as tag, \
                    mock.patch.object(publish_release, "check_macos_package", side_effect=package), \
                    mock.patch.object(publish_release.subprocess, "run", side_effect=run):
                if fault == "wrong-tag":
                    tag.side_effect = ValueError("tag moved")
                kwargs = dict(version="1.0.11", pkg=pkg, target="a" * 40, sdk_sha="b" * 40,
                              package_sha256=digest[7:],
                              previous_sha256="absent" if absent else "1" * 64, dry_run=dry_run)
                if fault == "wrong-original":
                    kwargs["previous_sha256"] = "2" * 64
                if fault == "wrong-qualified-hash":
                    kwargs["package_sha256"] = "3" * 64
                if fault:
                    with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                        publish_release.backfill_intel(**kwargs)
                else:
                    self.assertEqual(publish_release.backfill_intel(**kwargs), 0)
                    self.assertEqual(tag.call_count, 2 if dry_run else 3)
            return mutation_calls

    def test_add_and_replace_preserve_other_platforms_notes_and_updater(self):
        for absent in (False, True):
            for same_latest in (False, True):
                with self.subTest(absent=absent, same_latest=same_latest):
                    calls = self.exercise(absent=absent, same_latest=same_latest)
                    self.assertEqual(len(calls), 1)
                    self.assertEqual(calls[0][1:3], ["release", "upload"])
                    self.assertEqual(calls[0][0], "/opt/homebrew/Cellar/gh/fixture/bin/gh")
                    self.assertEqual("--clobber" in calls[0], not absent)
                    self.assertFalse(any("arm64ec" in arg for arg in calls[0]))

    def test_dry_run_performs_preflight_without_upload(self):
        self.assertEqual(self.exercise(dry_run=True), [])
        self.assertEqual(self.exercise("wrong-original", dry_run=True), [])

    def test_preflight_failures_never_mutate_release(self):
        for fault in ("pkgutil", "xcrun", "spctl", "invalid-package", "wrong-original",
                      "wrong-qualified-hash", "wrong-tag", "changed-input",
                      "concurrent-before-upload", "read-failed"):
            with self.subTest(fault=fault):
                self.assertEqual(self.exercise(fault), [])

    def test_successful_upload_is_not_successful_qualification(self):
        for fault in ("wrong-upload", "wrong-size", "deleted-linux", "changed-windows",
                      "changed-notes", "extra-asset", "changed-latest"):
            with self.subTest(fault=fault):
                self.assertEqual(len(self.exercise(fault)), 1)

    def test_cli_selects_only_the_backfill_lane_and_requires_exact_pins(self):
        base = ["publish_release.py", "--version", "1.0.11", "--backfill-intel-pkg",
                "Spectr-1.0.11-macos-x86_64.pkg", "--target", "a" * 40,
                "--sdk-source-sha", "b" * 40, "--intel-sha256", "1" * 64,
                "--previous-intel-sha256", "2" * 64, "--dry-run"]
        with mock.patch.object(sys, "argv", base), \
                mock.patch.object(publish_release, "backfill_intel", return_value=0) as backfill:
            self.assertEqual(publish_release.main(), 0)
            backfill.assert_called_once()
        for extra in (["--notes", "notes.md"], ["--appcast", "appcast.xml"],
                      ["--intel-pkg", "other.pkg"], ["--windows-x64", "x.zip"],
                      ["--require-windows"], ["--require-linux"], ["--reuse-draft"]):
            with self.subTest(extra=extra), mock.patch.object(sys, "argv", base + extra), \
                    mock.patch.object(publish_release, "backfill_intel") as backfill:
                with self.assertRaises(SystemExit):
                    publish_release.main()
                backfill.assert_not_called()
        with mock.patch.object(sys, "argv", base[:5]), self.assertRaises(SystemExit):
            publish_release.main()

    def test_actual_publication_requires_the_named_main_actions_workflow(self):
        context = {"GITHUB_ACTIONS": "true", "GITHUB_REPOSITORY": "danielraffel/spectr",
                   "GITHUB_REF": "refs/heads/main", "GH_TOKEN": "fixture-not-a-token",
                   "GITHUB_WORKFLOW_REF": "danielraffel/spectr/.github/workflows/release-intel-backfill.yml@refs/heads/main"}
        with mock.patch.dict(release_github_cli.os.environ, context, clear=True):
            self.assertTrue(publish_release._backfill_actions_context())
        for key in context:
            invalid = dict(context)
            invalid.pop(key)
            with self.subTest(key=key), mock.patch.dict(release_github_cli.os.environ, invalid, clear=True), \
                    mock.patch.object(publish_release.subprocess, "run") as run:
                with self.assertRaisesRegex(ValueError, "main-only"):
                    publish_release.backfill_intel("1.0.11", Path("unread.pkg"), "a" * 40,
                                                   "b" * 40, "1" * 64, "2" * 64)
                run.assert_not_called()

    def test_snapshot_reads_all_pages_and_rejects_invalid_identity(self):
        release = {"id": 11, "tag_name": "v1.0.11", "name": "Spectr", "body": "table",
                   "draft": False, "prerelease": False, "published_at": "date", "target_commitish": "main"}
        asset = {"id": 17, "name": "Spectr.pkg", "state": "uploaded", "size": 7,
                 "url": "https://api.github.com/repos/danielraffel/spectr/releases/assets/17",
                 "digest": "sha256:" + "1" * 64}
        for changes in (None, {"digest": None}, {"url": "https://other/17"},
                        {"state": "starter"}, {"size": 0}):
            with self.subTest(changes=changes):
                current = dict(asset, **(changes or {}))
                with mock.patch.object(publish_release, "_github_json", side_effect=[release, [[], [current]]]) as api:
                    if changes:
                        with self.assertRaises(ValueError):
                            publish_release._published_snapshot("release")
                    else:
                        self.assertIn(asset["name"], publish_release._published_snapshot("release")["assets"])
                    self.assertTrue(api.call_args.kwargs["paginate"])

    def test_backfill_tag_peels_annotated_tags_and_refuses_other_source(self):
        for annotated in (False, True):
            replies = [{"object": {"type": "commit", "sha": "a" * 40}}]
            if annotated:
                replies.insert(0, {"object": {"type": "tag", "sha": "b" * 40}})
            with mock.patch.object(publish_release, "_github_json", side_effect=replies):
                publish_release._check_backfill_tag("1.0.11", "a" * 40)
        with mock.patch.object(publish_release, "_github_json", return_value={"object": {"type": "commit", "sha": "b" * 40}}):
            with self.assertRaisesRegex(ValueError, "qualified product"):
                publish_release._check_backfill_tag("1.0.11", "a" * 40)

    def test_workflow_staged_download_checks_identity_and_bytes(self):
        import hashlib
        import os
        import textwrap
        workflow = (HERE.parents[1] / ".github/workflows/release-intel-backfill.yml").read_text()
        existing = (HERE.parents[1] / ".github/workflows/release.yml").read_text()
        self.assertIn("github.ref == 'refs/heads/main'", workflow)
        self.assertIn("ref: ${{ github.sha }}", workflow)
        for text in (workflow, existing):
            self.assertIn("group: spectr-release\n", text)
        run_text = workflow.split("        run: |", 1)[1]
        self.assertNotIn("${{ inputs.", run_text)
        source = textwrap.dedent(workflow.split("python3 - <<'PY'\n", 1)[1].split("\n          PY", 1)[0])
        for fault in (None, "name", "digest", "url", "id", "state", "size", "bytes", "input"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                data = b"staged qualified bytes"
                digest = hashlib.sha256(data).hexdigest()
                env = {"RUNNER_TEMP": directory, "GITHUB_RUN_ID": "123", "VERSION": "1.0.11",
                       "GITHUB_ENV": str(Path(directory) / "environment"), "PRODUCT_SHA": "a" * 40,
                       "SDK_SHA": "b" * 40, "INTEL_ASSET_ID": "17", "INTEL_SHA256": digest,
                       "PREVIOUS_INTEL_SHA256": "absent"}
                if fault == "input":
                    env["INTEL_ASSET_ID"] = "17/../../other"
                asset = {"id": 17, "name": "Spectr-1.0.11-macos-x86_64.pkg", "state": "uploaded",
                         "url": "https://api.github.com/repos/danielraffel/spectr/releases/assets/17",
                         "digest": "sha256:" + digest, "size": len(data)}
                if fault in asset:
                    asset[fault] = "wrong"

                def staged(cmd, **kwargs):
                    if "stdout" in kwargs:
                        kwargs["stdout"].write(b"wrong bytes" if fault == "bytes" else data)
                    return subprocess.CompletedProcess(cmd, 0, stdout=json.dumps(asset))

                with mock.patch.dict(os.environ, env), mock.patch.object(subprocess, "run", side_effect=staged) as run, \
                        mock.patch.object(release_github_cli, "actions_gh", return_value="/opt/homebrew/Cellar/gh/fixture/bin/gh"):
                    if fault:
                        with self.assertRaises(SystemExit):
                            exec(compile(source, "staged-download", "exec"), {})
                        self.assertFalse(Path(env["GITHUB_ENV"]).exists())
                    else:
                        exec(compile(source, "staged-download", "exec"), {})
                        self.assertIn("INTEL_PKG=", Path(env["GITHUB_ENV"]).read_text())
                        self.assertEqual(run.call_count, 2)
                        self.assertTrue(all(call.args[0][0] == "/opt/homebrew/Cellar/gh/fixture/bin/gh"
                                            for call in run.call_args_list))

    def test_shadowed_path_cannot_select_the_fleet_gh_shim(self):
        import os
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            actual = root / "Cellar/gh/2.83.0/bin/gh"
            actual.parent.mkdir(parents=True)
            actual.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture native executable")
            actual.chmod(0o500)
            actual = actual.resolve()
            opt = root / "opt/gh"
            opt.parent.mkdir()
            opt.symlink_to(root / "Cellar/gh/2.83.0", target_is_directory=True)
            shim = root / "shadow/gh"
            shim.parent.mkdir()
            shim.write_text("#!/bin/sh\nexit 99\n")
            shim.chmod(0o700)
            resolver = release_github_cli.resolve_binary
            with mock.patch.dict(os.environ, {"PATH": str(shim.parent),
                                              "SPECTR_RELEASE_GH": str(actual),
                                              "SPECTR_RELEASE_GH_SHA256": release_github_cli.digest(actual)}), \
                    mock.patch.object(release_github_cli, "actions_context", return_value=True), \
                    mock.patch.object(release_github_cli, "resolve_binary", side_effect=lambda: resolver(root)), \
                    mock.patch.object(publish_release, "_backfill_actions_context", return_value=True), \
                    mock.patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "{}")) as run:
                self.assertEqual(release_github_cli.actions_gh(), str(actual))
                publish_release._github_json("repos/danielraffel/spectr/releases/latest")
                self.assertEqual(run.call_args.args[0][0], str(actual))
                with mock.patch.dict(os.environ, {"SPECTR_RELEASE_GH_SHA256": "0" * 64}):
                    with self.assertRaisesRegex(ValueError, "verified workflow selection"):
                        release_github_cli.actions_gh()
            actual.chmod(0o700)
            actual.write_text("#!/bin/sh\nexec ghapp \"$@\"\n")
            actual.chmod(0o500)
            with self.assertRaisesRegex(ValueError, "Mach-O"):
                resolver(root)


if __name__ == "__main__":
    unittest.main()
