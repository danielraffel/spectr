#!/usr/bin/env python3
"""Register each captured font file once per editor open, not once per rule.

The browser capture emits one `font_bindings` entry per `@font-face` rule. The
Inter and JetBrains Mono captures are variable fonts, so the stylesheet names
the SAME file under several weights: 59 bindings resolve to 13 distinct
(asset, runtime family) pairs. The native runtime registers every binding --
decoding the woff2, building a typeface and bumping the process-wide font
generation -- and reads nothing from a binding except its asset and runtime
family (pulp core/view/src/claude_bundle.cpp, the only consumer). The 46
duplicates therefore cost 46 woff2 decodes per editor open and, because Pulp
deduplicates registrations by typeface pointer rather than by content, they
also grow the process font registry by 46 faces per open; every later
SkParagraph font-collection rebuild then clones each of those variable faces
at nine CSS weights. That rebuild was ~190 ms of every Spectr editor open.

Keeping one binding of each pair (the regular 400 rule where there is one)
registers exactly the typefaces the duplicates did, so text renders identically. The weight/style/unicode-range
fields of a dropped duplicate were never read natively.

Text-level splice of the one `font_bindings` array, so the rest of the
generated document stays byte-identical. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = (Path(__file__).resolve().parents[1]
        / "native-ui/materialized/materialized-document.runtime.json")
ANCHOR = '"font_bindings":['


def array_end(text, start):
    """Index just past the `]` closing the array whose `[` is at start-1."""
    depth, in_string, escape = 1, False, False
    for i in range(start, len(text)):
        c = text[i]
        if in_string:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c in "[{":
            depth += 1
        elif c in "]}":
            depth -= 1
            if depth == 0:
                return i + 1
    return -1


def main(path=PATH):
    text = path.read_text()
    if text.count(ANCHOR) != 1:
        print("FAIL: font_bindings anchor occurs %d times, expected 1"
              % text.count(ANCHOR))
        return 1
    begin = text.index(ANCHOR) + len(ANCHOR) - 1
    end = array_end(text, begin + 1)
    if end < 0:
        print("FAIL: font_bindings array is not terminated")
        return 1
    original = text[begin:end]
    bindings = json.loads(original)
    # The splice must not restyle what it keeps: prove the serializer
    # reproduces the original array before trusting it with the result.
    if json.dumps(bindings, separators=(",", ":"), ensure_ascii=False) != original:
        print("FAIL: font_bindings does not round-trip byte-for-byte")
        return 1
    # One survivor per pair. Prefer the regular (400, normal) rule so a reader
    # that looks the regular face up by weight -- as
    # test/test_materialized_output_meter.mjs does -- still finds exactly one;
    # otherwise keep the first. Survivors stay in document order.
    def key_of(binding):
        return (binding.get("asset_id"),
                binding.get("runtime_family") or binding.get("family"))

    def is_regular(binding):
        return (str(binding.get("weight")) == "400"
                and binding.get("style", "normal") == "normal")

    survivor = {}
    for index, binding in enumerate(bindings):
        key = key_of(binding)
        if key not in survivor or (is_regular(binding)
                                   and not is_regular(bindings[survivor[key]])):
            survivor[key] = index
    kept = [bindings[i] for i in sorted(survivor.values())]
    if len(kept) == len(bindings):
        print("font_bindings already unique (%d)" % len(kept))
        return 0
    replacement = json.dumps(kept, separators=(",", ":"), ensure_ascii=False)
    path.write_text(text[:begin] + replacement + text[end:])
    print("font_bindings: %d -> %d (one registration per captured font file)"
          % (len(bindings), len(kept)))
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]) if len(sys.argv) > 1 else PATH))
