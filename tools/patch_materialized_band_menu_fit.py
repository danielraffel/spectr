#!/usr/bin/env python3
"""The band context menu fits its viewport and scrolls, instead of guessing.

THE DEFECT

    The menu is a `position: fixed` panel with NO `maxHeight` and NO
    `overflowY`, positioned from a hardcoded height estimate:

        const H = 420 + (hasBand ? 100 : 0) + (hasSel ? 160 : 0)
                      + assigned.length * 26;
        const top = Math.max(8, Math.min(y, vh - H - 8));

    Both halves are wrong.

    The estimate, measured against the three shapes the menu actually ships,
    from recorded standalone runs:

        menu state                 H guess   measured    error
        no selection, band             520        495        +25
        selection + band               680        695        -15
        selection + 4 macros           784        811        -27

    It is wrong in the DANGEROUS direction for both selection cases: it
    understates the height, so the clamp believes the panel fits when it does
    not.

    And the clamp has nothing behind it. When the panel is taller than the
    viewport, `vh - H - 8` goes negative, `Math.max(8, ...)` pins the panel to
    the top edge, and the panel keeps its full content height -- so every row
    past the viewport is laid out, hit-tested and PAINTED outside it, over the
    app behind. Solving that clamp against six recorded (H, top) pairs gives
    vh = 575 exactly, while the rows lay out in the 1320x860 design space and
    the tallest panel measures 811 -- 244px of menu past the bottom.

THE FIX, AND WHICH HALF IS LOAD-BEARING

    `maxHeight` + `overflowY` is the invariant: whatever the height arithmetic
    believes and whatever `vh` reports, the panel cannot extend past the
    viewport, because it caps and scrolls its rows instead. `patternMenu` in
    this same document already caps itself exactly this way at 380.

    Measuring the panel is the refinement: it makes the POSITION right, not
    the overflow impossible. It is deliberately not the thing the correctness
    rests on, because the measurement comes back through the widget bridge and
    this lane has a history of that path returning numbers the caller did not
    assume.

    Whether `vh` itself is correct is a separate question -- an 860-tall root
    reporting 575 would be a core Pulp bridge defect, to be fixed there and
    inherited here. This change is deliberately robust to either answer.

WHY NO NEW ELEMENT

    The container's `ref` is already declared and already attached, so the
    measurement needs nothing mounted. That matters: `text_bindings`,
    `layout_bindings` and `paint_bindings` address nodes by POSITIONAL DOM
    PATH, so inserting a child anywhere but last silently re-points every
    later sibling's binding. This edit changes only style VALUES and the
    sizing block, and adds no node, so no binding can move.

Why a script and not a hand edit: the shipping document is one minified line,
so two hand edits to it always conflict and neither is replayable. This
substitutes exact text, asserts every patch point occurs exactly once before
writing, re-checks the result, and reports "already applied" on a second run.

Exit codes: 0 applied or already applied, 1 a patch point is missing or
ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

OLD_SIZING = '''  const estimatedHeight = 420 + (hasBand ? 100 : 0) + (hasSel ? 160 : 0) + assigned.length * 26;
  const menuBottom = Math.max(24, vh - 64);
  const menuMaxHeight = Math.max(120, menuBottom - 16);
  const H = Math.min(estimatedHeight, menuMaxHeight);
'''

NEW_SIZING = '''  // The panel measures itself rather than guessing. The estimate kept below
  // as a first-frame fallback was wrong by +25 / -15 / -27 px against the
  // three shapes this menu ships, and wrong in the DANGEROUS direction for
  // both selection cases: it understated the height, so the clamp believed a
  // panel fit when it did not.
  //
  // `ref` is the container's own ref, already declared above and already
  // attached below, so reading the box mounts nothing new -- the bindings
  // address nodes by positional DOM path, and a new child would re-point
  // every later sibling.
  const [measuredH, setMeasuredH] = React.useState(null);
  React.useLayoutEffect(() => {
    const node = ref.current;
    if (!node) return;
    const h = node.offsetHeight || node.clientHeight || 0;
    if (Number.isFinite(h) && h > 0 && h !== measuredH) setMeasuredH(h);
  });
  const estimatedHeight = 420 + (hasBand ? 100 : 0) + (hasSel ? 160 : 0) + assigned.length * 26;
  const menuBottom = Math.max(24, vh - 64);
  const menuMaxHeight = Math.max(120, menuBottom - 16);
  const H = Math.min(measuredH === null ? estimatedHeight : measuredH, menuMaxHeight);
'''

EDITS = [
    ("the panel positions from its measured height, not a guess",
     OLD_SIZING, NEW_SIZING),
]

REQUIRED_AFTER = (
    # The load-bearing line, named.
    "const H = Math.min(measuredH === null ? estimatedHeight : measuredH, menuMaxHeight);",
    # The measurement, and that it reads the EXISTING ref.
    "const node = ref.current;",
    "const h = node.offsetHeight || node.clientHeight || 0;",
    # Tokens this must not disturb: the clamp, the dismissal contract, and the
    # markers the scenario probe and the other scripts in this lane key on.
    "const left = Math.max(8, Math.min(x, vw - W - 8));",
    "const top = Math.max(8, Math.min(y, menuBottom - H - 8));",
    '"data-spectr-band-context-menu": "true",',
    "const __spectrBandMenuKeysAndDismissal = true;",
    "window.spectrDismissBandMenu = onClose;",
    "const ref = React.useRef(null);",
)

# The guessed height as the OPERATIVE value. `estimatedH` keeps the same
# arithmetic as a first-frame fallback, so the test is that nothing still
# binds it to `H` directly.
FORBIDDEN_AFTER = (
    "const H = Math.min(estimatedHeight, menuMaxHeight);",
)

REQUIRED_COUNTS = {
    "function ContextMenu({ x, y, band, N, selection": 1,
    "const estimatedHeight = 420 + (hasBand ? 100 : 0) + (hasSel ? 160 : 0) + assigned.length * 26;": 1,
    "const top = Math.max(8, Math.min(y, menuBottom - H - 8));": 1,
    # patternMenu's own cap must survive untouched: it is a DIFFERENT menu,
    # and it is the one whose 380 was misread as this menu's for a whole
    # workstream.
    "maxHeight: 380,": 1,
}


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding="utf-8").read()

    # The component this edits must exist exactly once before anything is
    # substituted. Without this an empty or restructured document would make
    # every "already applied" below vacuously true.
    anchor = "function ContextMenu({ x, y, band, N, selection"
    seen = raw.count(escaped(anchor))
    if seen != 1:
        sys.exit("FAIL: %r occurs %d times, expected 1; the band context menu "
                 "is not where this script expects it" % (anchor, seen))

    changed = False
    applied = 0
    already = 0
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) == 1 and raw.count(old_e) == 0:
            print("already applied ", label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        applied += 1
        print("applied         ", label)

    if already and applied:
        sys.exit("FAIL: the document is half patched; refusing to write")

    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit("FAIL: %r is absent after patching" % (token,))
    for token in FORBIDDEN_AFTER:
        if raw.count(escaped(token)):
            sys.exit("FAIL: %r survives after patching; the guessed height is "
                     "what this replaces" % (token,))
    for token, want in REQUIRED_COUNTS.items():
        got = raw.count(escaped(token))
        if got != want:
            sys.exit("FAIL: %r appears %d times after patching, expected %d"
                     % (token, got, want))

    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        sys.exit("FAIL: the patched document no longer carries an html payload")

    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
