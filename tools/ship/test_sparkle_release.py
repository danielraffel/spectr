#!/usr/bin/env python3
"""Tests for Spectr's update-feed tooling: the What's New HTML rendered from a
GitHub release body, and an appcast item that carries it.

Runs without the private key: fixtures sign with a throwaway seed through the
pure-Python Ed25519 in ed25519_verify.py, and the checks verify against that
seed's public key exactly as they verify release items against SUPublicEDKey.
"""
from __future__ import annotations

import base64
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


if __name__ == "__main__":
    unittest.main()
