#!/usr/bin/env python3
"""Decide whether a change can reach the Spectr product.

Exit 0 -> documentation only; the acceptance gate's heavy steps may be skipped.
Exit 1 -> anything else; run the full gate.

The exit codes are deliberately asymmetric in their risk. Exit 0 skips an
expensive gate, so it is only ever returned when every changed path positively
matched a documentation pattern. An unreadable file, an empty diff, an
unexpected exception -- all of it returns 1 and builds. A product change misread
as documentation would merge unvalidated, which is the one outcome worse than a
wasted build.

This lives in Python rather than in the shell step that calls it for a concrete
reason: /usr/bin/grep on the spectr-gate hosts is ugrep, where `grep -qv`
reports "no match" on input that `grep -cv` correctly counts. A `grep -qv`
classifier would silently call a product change documentation-only.
"""
from __future__ import annotations

import sys
from pathlib import PurePosixPath

DOC_PREFIXES = ("docs/", "planning/")
DOC_SUFFIX = ".md"


def is_doc_path(path: str) -> bool:
    """True when `path` matches the documentation set, mirroring the patterns
    this gate used to carry as `paths-ignore`: docs/**, planning/**, **/*.md."""
    candidate = path.strip()
    if not candidate:
        return False
    # Normalise separators and reject anything that escapes the tree, so a
    # crafted or malformed diff line cannot be classified as documentation.
    posix = PurePosixPath(candidate.replace("\\", "/"))
    if posix.is_absolute() or ".." in posix.parts:
        return False
    text = posix.as_posix()
    if text.startswith(DOC_PREFIXES):
        return True
    return text.endswith(DOC_SUFFIX)


def classify(paths: list[str]) -> bool:
    """True only when there is at least one path and all of them are docs."""
    real = [p for p in (line.strip() for line in paths) if p]
    if not real:
        # An empty diff is not evidence of a documentation change; it is
        # evidence the diff failed to produce. Build.
        return False
    return all(is_doc_path(p) for p in real)


def selftest() -> int:
    """Prove this classifier can both pass and fail.

    A checker that can only return one verdict proves nothing about the other,
    and this one is load-bearing in the direction that skips a gate.
    """
    must_be_docs = [
        ["docs/guide.md"],
        ["planning/notes.md"],
        ["README.md"],
        ["docs/a.md", "planning/b.txt", "deep/nested/CHANGELOG.md"],
    ]
    must_build = [
        [],
        ["src/spectr.cpp"],
        ["docs/guide.md", "src/spectr.cpp"],
        ["CMakeLists.txt"],
        ["native-ui/materialized/materialized-document.runtime.json"],
        ["tools/ci/classify_change_scope.py"],
        ["../escape.md"],
        ["/etc/passwd.md"],
        ["   "],
    ]
    failures = []
    for case in must_be_docs:
        if not classify(case):
            failures.append(f"expected docs-only, got build: {case!r}")
    for case in must_build:
        if classify(case):
            failures.append(f"expected build, got docs-only: {case!r}")
    for line in failures:
        print(f"classify_change_scope selftest: {line}", file=sys.stderr)
    if failures:
        return 1
    print(
        "classify_change_scope selftest: "
        f"{len(must_be_docs)} docs-only and {len(must_build)} build cases agree"
    )
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[1] == "--selftest":
        return selftest()
    if len(argv) != 2:
        print(f"usage: {argv[0]} <changed-paths-file> | --selftest", file=sys.stderr)
        return 1
    try:
        paths = open(argv[1], encoding="utf-8").read().splitlines()
    except OSError as exc:
        print(f"cannot read changed-paths file, running full gate: {exc}", file=sys.stderr)
        return 1
    docs_only = classify(paths)
    print(f"changed paths: {len([p for p in paths if p.strip()])}, docs_only={docs_only}")
    return 0 if docs_only else 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Exception as exc:  # fail closed on anything unforeseen
        print(f"classification failed, running full gate: {exc}", file=sys.stderr)
        sys.exit(1)
