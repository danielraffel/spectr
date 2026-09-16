#!/usr/bin/env python3
"""Stop a marquee drag re-rendering the band component on every pointer sample.

Applied by hand to the shipping document, because no recipe in this repo
reproduces the checked-in materialized artifact and both generators are broken
on main. This script IS the durable record of the change; re-running it after a
successful pass reports "already applied" and exits 0.

WHAT IS WRONG, measured rather than assumed
-------------------------------------------
`FilterBank` is a 2,226-line component function. A band-gain drag never
re-runs it: `commitDrawnGains` calls `commitMany(map, deferReact=true)`, which
writes refs and posts to native without touching React state. A marquee drag
does the opposite -- every pointer sample calls BOTH `setMarquee(...)` (a fresh
object) and `setSelection(...)` (a fresh Set), so every sample schedules a
React render and re-executes that whole body under QuickJS.

Measured on the shipping standalone with a traced SDK, identical 180-sample
pointer path, 460 rendered frames, audio off (wall clock to complete the run):

    marquee (Command)        20.35 s      frame-gap p95 73.2 ms
    additive (Command+Shift) 20.60 s
    band-gain drag           10.88 s      frame-gap p95 21.1 ms
    prologue only (no mode)  10.14 s
    AppKit band drag         10.24 s

Per delivered pointer sample the marquee spent 57.1 ms (p50) inside
`dom_event_dispatch` against 0.24 ms for the drag. Doubling the band count
32 -> 64 moved the marquee arm by 4%, so the cost is per-sample and fixed, not
the loop over bands.

THE TWO EDITS
-------------
1. The marquee RECTANGLE leaves React state for a ref. It has exactly one
   reader, `drawMarquee`, which is called from `renderAll` -- and `renderAll`
   already runs unconditionally from the component's permanent
   requestAnimationFrame loop, the same loop that animates the spectrum. So the
   rectangle was never being drawn BECAUSE of the state update; the state
   update only forced a render that redrew what the next frame would have
   redrawn anyway. A ref keeps the rubber band at frame rate and costs nothing.

2. The SELECTION stays React state -- the context menu takes it as a prop and
   `selectionRef` mirrors it for the keyboard path -- but the marquee's move
   handler now hands `setSelection` a functional update that returns the
   PREVIOUS Set when membership is unchanged. React bails out on Object.is, so
   only a sample that actually crosses a band boundary re-renders. Over a
   26-band sweep sampled 180 times that is 26 renders instead of 180.

Both edits preserve the selection SET exactly, which is what
test/test_materialized_additive_marquee.mjs asserts, and the new
test/test_materialized_marquee_render_cost.mjs asserts the cost half: the
number of state changes a marquee produces must scale with the bands it
crosses, not with how finely the pointer was sampled.

Only the shipping document is patched. `native-ui/materialized/materialized-document.json`
is an intermediate no build consumes (SPECTR_NATIVE_ASSET_SOURCES embeds
`materialized-document.runtime.json`, `runtime.js`, `design.js` and
`help-content.js`, and nothing else).
"""
from __future__ import annotations

import json
import pathlib
import sys

DOC = pathlib.Path("native-ui/materialized/materialized-document.runtime.json")

# Each entry is (description, before, after, applied_probe). `before` must occur
# EXACTLY once in the un-patched document; `applied_probe` must occur exactly
# once once the edit has landed and never before it. The probe is separate from
# `after` because one replacement deliberately CONTAINS its own anchor (the
# helper is inserted in front of `function FilterBank({`), so "before is gone"
# is not a usable applied-test for that edit. A count of anything but one is a
# refusal, not a warning: the document is one minified line, so a silently
# skipped edit leaves a half-applied handler that still parses.
EDITS: list[tuple[str, str, str, str]] = [
    (
        "marquee rectangle: state -> ref",
        "const [marquee, setMarquee] = useState(null);",
        "const marqueeRef = useRef(null);",
        "const marqueeRef = useRef(null);",
    ),
    (
        "drawMarquee reads the ref",
        "function drawMarquee(ctx, g) {\n    if (!marquee) return;",
        "function drawMarquee(ctx, g) {\n"
        "    // Read live rather than through a render: renderAll is called from\n"
        "    // the permanent rAF loop every frame, so the rubber band tracks the\n"
        "    // pointer at frame rate without a React commit per sample.\n"
        "    const marquee = marqueeRef.current;\n"
        "    if (!marquee) return;",
        "    const marquee = marqueeRef.current;",
    ),
    (
        "renderAll no longer depends on the rectangle",
        "theme, hover, marquee, selection, snapshots",
        "theme, hover, selection, snapshots",
        "theme, hover, selection, snapshots",
    ),
    (
        "press seeds the ref",
        "setMarquee({ x1: x, y1: y, x2: x, y2: y });",
        "marqueeRef.current = { x1: x, y1: y, x2: x, y2: y };",
        "marqueeRef.current = { x1: x, y1: y, x2: x, y2: y };",
    ),
    (
        "move updates the ref",
        "setMarquee({ x1: p.startX, y1: p.startY, x2: x, y2: y });",
        "marqueeRef.current = { x1: p.startX, y1: p.startY, x2: x, y2: y };",
        "marqueeRef.current = { x1: p.startX, y1: p.startY, x2: x, y2: y };",
    ),
    (
        "release clears the ref (no active gesture)",
        "if (!p || !p.mode) {\n      setMarquee(null);\n      return;\n    }",
        "if (!p || !p.mode) {\n      marqueeRef.current = null;\n      return;\n    }",
        "if (!p || !p.mode) {\n      marqueeRef.current = null;",
    ),
    (
        "release clears the ref (marquee gesture)",
        'if (p.mode === "marquee") {\n      setMarquee(null);\n      return;\n    }',
        'if (p.mode === "marquee") {\n      marqueeRef.current = null;\n      return;\n    }',
        'if (p.mode === "marquee") {\n      marqueeRef.current = null;',
    ),
    (
        "selection re-renders only when membership moves",
        "      setSelection(sel);\n      return;",
        "      // Same membership as the last sample -> hand React back the SAME\n"
        "      // Set so it bails out. A fresh Set every sample re-ran this\n"
        "      // 2,226-line component on every pointer move, which is the whole\n"
        "      // of the marquee's cost; a band-gain drag never re-runs it.\n"
        "      setSelection((prev) => sameBandSet(prev, sel) ? prev : sel);\n"
        "      return;",
        "setSelection((prev) => sameBandSet(prev, sel) ? prev : sel);",
    ),
    (
        "sameBandSet helper at module scope",
        "function FilterBank({",
        "// Set equality by membership. Marquee samples arrive far faster than the\n"
        "// pointer crosses band boundaries, so most samples recompute a set equal\n"
        "// to the one already held.\n"
        "function sameBandSet(a, b) {\n"
        "  if (a === b) return true;\n"
        "  if (!a || !b || a.size !== b.size) return false;\n"
        "  for (const v of a) if (!b.has(v)) return false;\n"
        "  return true;\n"
        "}\n"
        "function FilterBank({",
        "function sameBandSet(a, b) {",
    ),
]


def main() -> int:
    if not DOC.exists():
        print(f"not found: {DOC} (run from the repository root)", file=sys.stderr)
        return 2
    document = json.loads(DOC.read_text())
    html = document["html"]

    applied = 0
    already = 0
    for description, before, after, probe in EDITS:
        if html.count(probe) == 1:
            already += 1
            continue
        if html.count(probe) != 0:
            print(
                f"REFUSING: applied-probe for '{description}' occurs "
                f"{html.count(probe)} times, expected 0 or 1",
                file=sys.stderr,
            )
            return 1
        have_before = html.count(before)
        if have_before != 1:
            print(
                f"REFUSING: anchor for '{description}' occurs {have_before} times, "
                "expected exactly 1; the document is not the shape this patch "
                "was written against",
                file=sys.stderr,
            )
            return 1
        html = html.replace(before, after, 1)
        applied += 1

    if applied == 0:
        print(f"already applied ({already}/{len(EDITS)} edits present)")
        return 0
    if already:
        print(
            f"REFUSING: {already} edits were already applied and {applied} were not; "
            "the document is half patched",
            file=sys.stderr,
        )
        return 1

    document["html"] = html
    # Re-encode in the artifact's OWN form: one compact line, no trailing
    # newline, non-ASCII left as-is. An indent=2 round-trip parses identically
    # and reads identically in the app, but rewrites all 847 KB, so it turns a
    # nine-line change into a whole-file conflict with every other patch
    # touching this document.
    DOC.write_text(json.dumps(document, ensure_ascii=False, separators=(",", ":")))
    print(f"applied {applied} edits to {DOC}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
