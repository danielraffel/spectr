#!/usr/bin/env python3
"""Git merge driver for `native-ui/materialized/materialized-document.runtime.json`.

That artifact is a checked-in, minified, single-line JSON document ~830 KB
long. Because it is one physical line, git's default content merge treats
*any* two changes to it as a conflict -- even when they edit regions of the
document thousands of lines apart once decoded. On 2026-09-15 that cost four
PRs (#150, #154, #159, #160) repeated rebases for changes that had nothing to
do with each other: a menu fix, a header label and a marquee perf fix.

The document's bulk is one JSON key, `html`, holding the editor's compiled
React source -- 361 KB of text carrying 8024 newlines, all escaped as `\n`
inside that single JSON string. So the conflicts are an artifact-encoding
accident, not a real overlap: the two sides almost always edit different
*lines* of a perfectly ordinary multi-line text, which git cannot see because
JSON escaping has collapsed it onto one line.

This driver decodes `html` from all three inputs, runs git's own three-way
text merge over the decoded lines, and splices the result back.

WHAT IT DELIBERATELY REFUSES
----------------------------
The document also carries `text_bindings`, `layout_bindings`, `paint_bindings`,
`semantic_bindings` and `canvas_bindings`. Those address DOM nodes by
*positional path* (`[{"tag":"div","index":0}, ...]`), and they are derived from
the rendered `html`. A binding set computed against one side's html is not
valid for a document whose html also carries the other side's insertions:
inserting a child anywhere but last renumbers its later siblings and silently
re-points every binding after it. Two composed insertions can therefore yield
a document where both edits survive and both binding sets are wrong -- which
is far worse than a conflict, because nothing downstream would report it.

So the driver merges *only* when `html` is the single key either side changed.
If any binding list (or any other key) differs, it refuses. Of the 76 changes
in this file's history whose parent is comparable, 73 touched `html` alone and
would be permitted; the other 3 also moved a binding list and would be refused.

Two lanes inserting a child at the SAME point in the same parent's child list
also refuses, because in the decoded source that is two edits to one line and
the three-way text merge reports it as a conflict rather than guessing an
order. That is the case that must never compose: both children would survive
and every binding indexing a later sibling would be off by a different amount
than either lane assumed, producing a document that loads, renders, and is
quietly wrong.

WHAT THIS DRIVER CANNOT FIX, stated plainly
-------------------------------------------
Two insertions at DIFFERENT points do merge, and the bindings that index past
them are then stale. This is not a regression the merge introduces: a single
insertion renumbers its later siblings by exactly the same mechanism, and the
document's own history shows lanes shipping `html` insertions without touching
bindings (73 of 76 changes). The related damage -- a node appended to the
header's flex row leaving a neighbouring label remeasured at the wrong size,
present and correct and dead -- comes from ONE edit changing a row, not from
combining two, and no merge driver can see it. Refusing every pair in which
both sides insert was measured and rejected: it would refuse 3 of the 6 pairs
among the lanes live on 2026-09-16 while preventing nothing, since one
insertion invalidates a positional path just as thoroughly as two. What the
driver owes is narrower and is enforced: never emit a document that differs
from what both sides wrote.

It also refuses when the decoded three-way text merge itself conflicts, when
either input is not parseable JSON, and when the post-merge self-check below
fails. Every refusal falls through to git's ordinary conflict for the path --
never to a guess.

THE SELF-CHECK
--------------
This is also what catches a counter that both sides advance to the same value.
When two branches each add one item and each bump a tally from N to N+1, a
three-way merge sees one line changed to the same value on both sides, takes it
once, and yields N+1 where the answer is N+2 -- no conflict, both sides
"agree", result wrong. (That corrupted a header census count elsewhere in this
organisation on 2026-09-16.) Arithmetically it is a dropped edit -- both sides
added the line, the merge has it once -- so the count below rejects it. The
driver refuses instead of summing, because "both incremented" and "both made
the same edit" are indistinguishable from the text, and this document really
does carry such fields (`PATTERN_SCHEMA_VERSION` inside `html`; `version` and
`runtime_canonicalization.jsx_scripts_compiled` in the envelope, the latter two
already covered by the html-only rule).

A text merge that silently drops or duplicates a hunk still exits 0. So before
writing anything, the driver verifies the merge composed *additively*: for
every distinct line, the merged document must contain exactly
    count_base + (count_ours - count_base) + (count_theirs - count_base)
occurrences. A dropped hunk, a duplicated hunk, or a silently reverted region
makes some line's count disagree, and the driver refuses. This is the check
that would have caught the rebase elsewhere in this repo that reverted 408
lines while succeeding textually.

What the count invariant does NOT see: it compares line multisets, so a merge
that returned exactly the right lines in the wrong ORDER would satisfy it.
`git merge-file` does not reorder, so this is a property of the check rather
than a known hole in the driver -- but it is the reason the check is a
guardrail on a trusted merge engine, not a proof of correctness on an untrusted
one.

BYTE FIDELITY
-------------
The merged `html` is spliced into *ours' original bytes* -- the raw file is not
re-serialised from a parsed object. Only the `html` token's byte range is
replaced, so every other byte of the artifact is carried through untouched and
the file's existing compact formatting cannot drift. The replacement token is
produced with `json.dumps(..., ensure_ascii=False)`, which reproduces this
document's existing escaping exactly (verified byte-identical against the
checked-in artifact by tools/git/test_merge_materialized_runtime.py).

Invoked by git as: %O (ancestor) %A (ours; result is written here) %B (theirs)
%L (marker size) %P (pathname). Exit 0 merged, 1 conflict.
"""

from __future__ import annotations

import collections
import json
import os
import subprocess
import sys
import tempfile

KEY = "html"
MERGEABLE_KEYS = frozenset({KEY})


def _log(msg: str) -> None:
    sys.stderr.write(f"[merge-materialized-runtime] {msg}\n")


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def locate_html_token(raw: str) -> tuple[int, int]:
    """Return (start, end) byte offsets of the `html` value token, quotes included.

    Located by scanning for the key at the top level rather than by regex over
    the whole file, so a `"html":"` sequence occurring *inside* some other
    string value cannot be mistaken for the real key.
    """
    idx = 0
    needle = '"html"'
    while True:
        idx = raw.find(needle, idx)
        if idx < 0:
            raise ValueError("no `html` key found")
        cursor = idx + len(needle)
        while cursor < len(raw) and raw[cursor] in " \t\r\n":
            cursor += 1
        if cursor >= len(raw) or raw[cursor] != ":":
            idx = cursor
            continue
        cursor += 1
        while cursor < len(raw) and raw[cursor] in " \t\r\n":
            cursor += 1
        if cursor >= len(raw) or raw[cursor] != '"':
            idx = cursor
            continue
        try:
            _, end = json.decoder.scanstring(raw, cursor + 1)
        except ValueError:
            idx = cursor
            continue
        return cursor, end


def three_way_text_merge(base: str, ours: str, theirs: str, marker_size: int,
                         path: str) -> str | None:
    """Git's own three-way merge over decoded text. None when it conflicts."""
    with tempfile.TemporaryDirectory() as tmp:
        paths = {}
        for name, text in (("base", base), ("ours", ours), ("theirs", theirs)):
            paths[name] = os.path.join(tmp, name)
            with open(paths[name], "w", encoding="utf-8") as handle:
                handle.write(text)
        result = subprocess.run(
            [
                "git", "merge-file", "-p", f"--marker-size={marker_size}",
                "-L", f"{path} (ours)", "-L", f"{path} (base)",
                "-L", f"{path} (theirs)",
                paths["ours"], paths["base"], paths["theirs"],
            ],
            capture_output=True,
            text=True,
        )
    if result.returncode != 0:
        return None
    return result.stdout


def fabricated_content(base: str, ours: str, theirs: str,
                       merged: str) -> str | None:
    """Reject a merge containing a line that is in none of the three inputs.

    A merge is only ever allowed to select and arrange material that already
    existed; inventing a line is a failure mode distinct from losing one, and a
    stronger property to guarantee, because it catches fabrication rather than
    omission. It is not hypothetical: a three-way merge elsewhere in this
    organisation produced 204 placeholder entries in a file that had zero on
    both the PR head and the base.

    Checked separately from the count invariant below -- which subsumes it
    arithmetically -- so that the property is stated in its own right, reports
    its own diagnostic, and survives anyone loosening the counting rule.

    Returns None when nothing was invented, else a human-readable reason.
    """
    known = set(base.splitlines()) | set(ours.splitlines()) \
        | set(theirs.splitlines())
    for line in merged.splitlines():
        if line not in known:
            excerpt = line.strip()[:110] or "(blank line)"
            return (f"the merge invented a line present in NONE of base, ours "
                    f"or theirs: {excerpt!r}")
    return None


def composed_additively(base: str, ours: str, theirs: str, merged: str) -> str | None:
    """Verify the merge is exactly both sides' line deltas applied to base.

    Returns None when it composed, else a human-readable reason.
    """
    cb = collections.Counter(base.splitlines(keepends=True))
    co = collections.Counter(ours.splitlines(keepends=True))
    ct = collections.Counter(theirs.splitlines(keepends=True))
    cm = collections.Counter(merged.splitlines(keepends=True))

    for line in set(cb) | set(co) | set(ct) | set(cm):
        expected = cb[line] + (co[line] - cb[line]) + (ct[line] - cb[line])
        if cm[line] != expected:
            excerpt = line.strip()[:110] or "(blank line)"
            return (
                f"line-count mismatch: expected {expected}, merged has "
                f"{cm[line]} -- {excerpt!r}"
            )
    return None


def refuse(reason: str, ours_path: str, base_path: str, theirs_path: str,
           marker_size: int, path: str) -> int:
    """Fall through to git's ordinary conflict for this path, loudly."""
    _log(f"REFUSING to auto-merge {path}: {reason}")
    _log("falling back to a normal conflict -- resolve by hand, and do NOT use "
         "`git checkout --theirs`, which discards the other side's edits.")
    subprocess.run(
        [
            "git", "merge-file", f"--marker-size={marker_size}",
            "-L", f"{path} (ours)", "-L", f"{path} (base)",
            "-L", f"{path} (theirs)",
            ours_path, base_path, theirs_path,
        ],
        check=False,
    )
    return 1


def main(argv: list[str]) -> int:
    if len(argv) < 4:
        sys.stderr.write(
            "usage: merge_materialized_runtime.py %O %A %B [%L] [%P]\n")
        return 2
    base_path, ours_path, theirs_path = argv[1], argv[2], argv[3]
    marker_size = int(argv[4]) if len(argv) > 4 and argv[4].isdigit() else 7
    path = argv[5] if len(argv) > 5 else "materialized-document.runtime.json"

    raw = {}
    docs = {}
    for name, p in (("base", base_path), ("ours", ours_path), ("theirs", theirs_path)):
        try:
            raw[name] = _read(p)
            docs[name] = json.loads(raw[name])
        except (OSError, ValueError) as exc:
            return refuse(f"{name} side is not readable/parseable JSON ({exc})",
                          ours_path, base_path, theirs_path, marker_size, path)

    if not all(isinstance(d, dict) for d in docs.values()):
        return refuse("document is not a JSON object", ours_path, base_path,
                      theirs_path, marker_size, path)

    changed = set()
    for side in ("ours", "theirs"):
        for key in set(docs["base"]) | set(docs[side]):
            if docs["base"].get(key) != docs[side].get(key):
                changed.add(key)

    if not changed:
        return 0  # identical on both sides; ours already holds it

    if not changed <= MERGEABLE_KEYS:
        other = sorted(changed - MERGEABLE_KEYS)
        return refuse(
            f"keys other than `html` differ: {other}. Binding lists address "
            "DOM nodes positionally and are derived from `html`, so they "
            "cannot be composed with an independent `html` change.",
            ours_path, base_path, theirs_path, marker_size, path)

    base_html = docs["base"][KEY]
    ours_html = docs["ours"][KEY]
    theirs_html = docs["theirs"][KEY]

    if theirs_html == ours_html or theirs_html == base_html:
        merged_html = ours_html          # nothing of theirs to take
    elif ours_html == base_html:
        merged_html = theirs_html        # ours untouched; take theirs whole
    else:
        merged_html = three_way_text_merge(
            base_html, ours_html, theirs_html, marker_size, path)
        if merged_html is None:
            return refuse(
                "both sides edited overlapping lines of the decoded `html`; "
                "the three-way text merge conflicts.",
                ours_path, base_path, theirs_path, marker_size, path)
        invented = fabricated_content(base_html, ours_html, theirs_html,
                                      merged_html)
        if invented is not None:
            return refuse(
                f"{invented}. Refusing rather than emit a document carrying "
                "content nobody wrote.",
                ours_path, base_path, theirs_path, marker_size, path)
        problem = composed_additively(base_html, ours_html, theirs_html, merged_html)
        if problem is not None:
            return refuse(
                f"the merged `html` is not both sides' changes composed ({problem}). "
                "This is a dropped or duplicated edit, or a value both sides "
                "advanced to the same number -- where taking it once loses one "
                "side's increment. Refusing rather than guess which.",
                ours_path, base_path, theirs_path, marker_size, path)

    # Splice into OURS' raw bytes: only the html token's range is replaced.
    try:
        start, end = locate_html_token(raw["ours"])
    except ValueError as exc:
        return refuse(f"cannot locate the `html` token in ours ({exc})",
                      ours_path, base_path, theirs_path, marker_size, path)
    spliced = raw["ours"][:start] + json.dumps(merged_html, ensure_ascii=False) \
        + raw["ours"][end:]

    # Self-check the splice before writing: the result must parse, must carry
    # the merged html exactly, and must leave every other key as ours had it.
    try:
        check = json.loads(spliced)
    except ValueError as exc:
        return refuse(f"spliced result is not valid JSON ({exc})",
                      ours_path, base_path, theirs_path, marker_size, path)
    if check.get(KEY) != merged_html:
        return refuse("spliced result's `html` is not the merged text",
                      ours_path, base_path, theirs_path, marker_size, path)
    if {k: v for k, v in check.items() if k != KEY} != \
       {k: v for k, v in docs["ours"].items() if k != KEY}:
        return refuse("splice disturbed a key other than `html`",
                      ours_path, base_path, theirs_path, marker_size, path)

    with open(ours_path, "w", encoding="utf-8") as handle:
        handle.write(spliced)
    _log(f"merged {path}: composed both sides' `html` changes "
         f"({len(base_html.splitlines())} -> {len(merged_html.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Exception as exc:  # never traceback into a merge; refuse instead
        argv = sys.argv
        if len(argv) >= 4:
            marker = int(argv[4]) if len(argv) > 4 and argv[4].isdigit() else 7
            name = argv[5] if len(argv) > 5 else "materialized-document.runtime.json"
            sys.exit(refuse(f"unexpected driver error ({exc!r})",
                            argv[2], argv[1], argv[3], marker, name))
        _log(f"unexpected driver error ({exc!r})")
        sys.exit(2)
