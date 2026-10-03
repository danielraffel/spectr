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
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import check_sparkle  # noqa: E402
import ed25519_verify  # noqa: E402
import make_appcast  # noqa: E402
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


if __name__ == "__main__":
    unittest.main()
