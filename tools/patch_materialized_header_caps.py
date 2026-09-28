#!/usr/bin/env python3
"""Set the header's band-count and zoom words in capitals, like every other header word.

Every word in the top bar is a capital -- BARS, RESPONSE, BOTH, the TRACING
reminder -- except two: the bands trigger ("32 bands ▾") and the zoom readout
("· 1.00× zoom"). Their baselines match the captions, but a lowercase word
with no ascender ("zoom", and most of "bands") is x-height only, so its ink
sits in the lower part of the line and reads as lower than the capitals
beside it. Setting both words in capitals puts every word's ink on the same
rows.

Monospace capitals have the same advance as the lowercase letters they
replace, so every captured line box and basis width stays exact. The change
touches:

  document  the bands trigger label, the zoom readout's suffix, and the
            trigger's text binding, which the runtime compares against the
            node's live text;
  runtime   the caption string the optical-centring pass installs, and the
            pattern that recognises a live band-count caption. The captured
            " bands ▾" suffix bindings stay as captured: the runtime already
            excludes them by text, and both spellings are excluded.

Why a script and not a hand edit: both artifacts are generated one-line or
bundled files, so two hand edits always conflict and neither is replayable.
This substitutes exact text, asserts each patch point occurs exactly once,
refuses a half-patched file, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCUMENT = os.path.join(REPO, "native-ui", "materialized",
                        "materialized-document.runtime.json")
RUNTIME = os.path.join(REPO, "native-ui", "materialized", "runtime.js")


def escaped(value):
    return json.dumps(value)[1:-1]


# (label, old, new) on the document's JSON text. The html edits are written as
# the source text and escaped; the binding edit is raw JSON already.
DOCUMENT_EDITS = [
    ('the bands trigger reads in capitals',
     escaped('settings.bandCount + " bands \\u25BE"'),
     escaped('settings.bandCount + " BANDS \\u25BE"')),
    ('the zoom readout reads in capitals',
     escaped('zoom, "\\xD7 zoom")'),
     escaped('zoom, "\\xD7 ZOOM")')),
    ('the bands trigger binding matches its live text',
     '"text":"32 bands \u25be"',
     '"text":"32 BANDS \u25be"'),
]

RUNTIME_EDITS = [
    ('the centring pass installs the capital caption',
     'nativeTextOwner(bandTrigger), liveBandCount + " bands \\u25BE",',
     'nativeTextOwner(bandTrigger), liveBandCount + " BANDS \\u25BE",'),
    ('a live band-count caption is recognised in capitals',
     'if (/^(32|40|48|56|64) bands \\u25BE$/.test(text)) return {',
     'if (/^(32|40|48|56|64) BANDS \\u25BE$/.test(text)) return {'),
    ('both spellings of the captured suffix stay excluded',
     '&& binding.text !== "bands \\u25BE"\n'
     '          && binding.text !== " bands \\u25BE"\n',
     '&& binding.text !== "bands \\u25BE"\n'
     '          && binding.text !== " BANDS \\u25BE"\n'
     '          && binding.text !== " bands \\u25BE"\n'),
]


def apply(path, edits, validate):
    raw = open(path, encoding="utf-8").read()
    applied = already = 0
    for label, old, new in edits:
        if raw.count(old) == 0 and raw.count(new) == 1:
            print("already applied ", label)
            already += 1
            continue
        count = raw.count(old)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                     % (label, count))
        raw = raw.replace(old, new, 1)
        applied += 1
        print("applied         ", label)
    if already and applied:
        sys.exit("FAIL: %s is half patched; refusing to write" % path)
    if applied:
        validate(raw)
        open(path, "w", encoding="utf-8").write(raw)
        print("written", path)


def validate_document(raw):
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")


def main():
    apply(DOCUMENT, DOCUMENT_EDITS, validate_document)
    apply(RUNTIME, RUNTIME_EDITS, lambda raw: None)
    return 0


if __name__ == "__main__":
    sys.exit(main())
