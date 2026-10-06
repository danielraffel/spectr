#!/usr/bin/env python3
"""Turn a Spectr GitHub release body (Markdown) into the "What's New" HTML that
Sparkle shows in its update dialog.

The output is an HTML fragment with its own <style>, written into the appcast
item's <description> (inline, so nothing is fetched when the dialog opens --
GitHub serves release assets as downloads, which Sparkle's web view would not
render as a page). It is styled after Spectr's own dark editor and ends with a
link to the full release page on GitHub.

    release_notes_html.py --version 1.0.7 --notes notes.md \
        --release-url https://github.com/danielraffel/spectr/releases/tag/v1.0.7 \
        --out notes.html
    release_notes_html.py ... --notes -          # read the Markdown from stdin

Only the Markdown the release notes actually use is supported: #/##/###
headings, "-" / "*" bullets, paragraphs, **bold**, *italic*, `code` and
[links](https://...). Everything else is escaped and shown as text, never
interpreted as HTML. The installer instructions paragraph ("**Install:** ...")
is dropped: someone reading it in the updater is already installing.
"""
from __future__ import annotations

import argparse
import html
import re
import sys
from pathlib import Path

STYLE = """
:root { color-scheme: dark; }
html, body { margin: 0; padding: 0; background: #05070a; }
body {
  color: #e8edf2;
  font: 13px/1.5 -apple-system, "Inter", "Helvetica Neue", sans-serif;
  padding: 14px 18px 18px;
  -webkit-font-smoothing: antialiased;
}
h1, h2, h3 { font-weight: 600; letter-spacing: 0.01em; margin: 14px 0 6px; }
h1 { font-size: 16px; }
h2 { font-size: 12px; text-transform: uppercase; letter-spacing: 0.08em; color: #7fd3f5; }
h3 { font-size: 13px; }
.spectr-version { font-size: 16px; font-weight: 600; margin: 0 0 2px; }
.spectr-tag { color: #6b7380; font-size: 11px; letter-spacing: 0.08em; text-transform: uppercase; margin: 0 0 10px; }
p { margin: 6px 0; color: #c9d1da; }
ul { margin: 6px 0 10px; padding-left: 18px; }
li { margin: 4px 0; color: #c9d1da; }
li::marker { color: #4fb8e6; }
strong { color: #e8edf2; font-weight: 600; }
code { font: 12px "JetBrains Mono", ui-monospace, Menlo, monospace; background: rgba(255,255,255,0.06);
       border: 1px solid rgba(255,255,255,0.08); border-radius: 4px; padding: 0 4px; }
a { color: #7fd3f5; text-decoration: none; }
a:hover { text-decoration: underline; }
.spectr-footer { margin-top: 14px; padding-top: 10px; border-top: 1px solid rgba(255,255,255,0.08); font-size: 12px; }
""".strip()

_INLINE = re.compile(
    r"(\*\*(?P<b>.+?)\*\*)"
    r"|(`(?P<c>[^`]+)`)"
    r"|(\[(?P<lt>[^\]]+)\]\((?P<lu>https?://[^)\s]+)\))"
    r"|((?<![*\w])\*(?P<i>[^*\s][^*]*?)\*(?![*\w]))"
)


def inline(text: str) -> str:
    out: list[str] = []
    pos = 0
    for m in _INLINE.finditer(text):
        out.append(html.escape(text[pos:m.start()], quote=False))
        if m.group("b") is not None:
            out.append(f"<strong>{inline(m.group('b'))}</strong>")
        elif m.group("c") is not None:
            out.append(f"<code>{html.escape(m.group('c'), quote=False)}</code>")
        elif m.group("lt") is not None:
            out.append(f'<a href="{html.escape(m.group("lu"))}">{inline(m.group("lt"))}</a>')
        else:
            out.append(f"<em>{inline(m.group('i'))}</em>")
        pos = m.end()
    out.append(html.escape(text[pos:], quote=False))
    return "".join(out)


def markdown_to_html(markdown: str) -> str:
    blocks: list[str] = []
    paragraph: list[str] = []
    items: list[str] = []

    def flush_paragraph() -> None:
        if paragraph:
            text = " ".join(s.strip() for s in paragraph)
            paragraph.clear()
            if not re.match(r"^\*\*Install:\*\*", text):
                blocks.append(f"<p>{inline(text)}</p>")

    def flush_list() -> None:
        if items:
            blocks.append("<ul>" + "".join(f"<li>{inline(i)}</li>" for i in items) + "</ul>")
            items.clear()

    for raw in markdown.replace("\r\n", "\n").split("\n"):
        line = raw.rstrip()
        heading = re.match(r"^(#{1,3})\s+(.*)$", line)
        bullet = re.match(r"^\s*[-*]\s+(.*)$", line)
        if heading:
            flush_paragraph(); flush_list()
            level = len(heading.group(1))
            blocks.append(f"<h{level}>{inline(heading.group(2).strip())}</h{level}>")
        elif bullet:
            flush_paragraph()
            items.append(bullet.group(1).strip())
        elif not line.strip():
            flush_paragraph(); flush_list()
        elif items and raw.startswith(("  ", "\t")):
            items[-1] += " " + line.strip()   # wrapped bullet continuation
        else:
            flush_list()
            paragraph.append(line)
    flush_paragraph(); flush_list()
    return "\n".join(blocks)


def render(version: str, markdown: str, release_url: str) -> str:
    body = markdown_to_html(markdown)
    footer = (f'<p class="spectr-footer"><a href="{html.escape(release_url)}">'
              f"Full release notes on GitHub &rarr;</a></p>")
    doc = (f"<style>{STYLE}</style>\n"
           f'<p class="spectr-version">Spectr {html.escape(version)}</p>\n'
           f'<p class="spectr-tag">What&rsquo;s new</p>\n'
           f"{body}\n{footer}\n")
    if "]]>" in doc:
        # The fragment is embedded in a CDATA section; "]]>" would end it.
        doc = doc.replace("]]>", "]]&gt;")
    return doc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--version", required=True)
    ap.add_argument("--notes", required=True, help="Markdown file, or - for stdin")
    ap.add_argument("--release-url", required=True)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    markdown = sys.stdin.read() if args.notes == "-" else Path(args.notes).read_text()
    if not markdown.strip():
        print("release notes are empty; refusing to publish an update with no notes",
              file=sys.stderr)
        return 2
    if not args.release_url.startswith("https://"):
        print("--release-url must be https://", file=sys.stderr)
        return 2
    args.out.write_text(render(args.version, markdown, args.release_url))
    return 0


if __name__ == "__main__":
    sys.exit(main())
