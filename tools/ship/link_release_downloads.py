#!/usr/bin/env python3
"""Link bare archive/installer filenames in release tables to their release assets."""
from __future__ import annotations

import argparse
import re
from pathlib import Path
from urllib.parse import quote


def link_downloads(notes: str, repository: str, tag: str) -> str:
    base = f"https://github.com/{repository}/releases/download/{quote(tag, safe='')}"
    # Match whole table cells so existing links and filenames in prose stay intact.
    pattern = re.compile(r"(?<=\|)(\s*)`([^`/\n]+\.(?:pkg|dmg|zip|exe|tar\.xz|tar\.gz|AppImage))`(\s*)(?=\|)")
    return pattern.sub(lambda m: f"{m[1]}[{m[2]}]({base}/{quote(m[2], safe='')}){m[3]}", notes)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--notes', type=Path, required=True)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    text = args.notes.read_text(encoding='utf-8')
    args.notes.write_text(link_downloads(text, args.repository, args.tag), encoding='utf-8')


if __name__ == '__main__':
    main()
