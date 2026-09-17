#!/usr/bin/env python3
"""The editor.html half of "a group drag moves levels and never mute".

WHY THIS FILE IS PATCHED TOO, AND NOT JUST THE SHIPPED DOCUMENT

    `resources/editor.html` carries the imported Claude source AND the outer
    adapter that rewrites it. The materialized document the native editor
    actually loads is that adapter's OUTPUT, so a fix applied only downstream
    is a fix one regeneration away from being lost -- and `editor.html` is
    itself a live surface, driven in Chrome by the `Spectr-browser-*` lane.
    `test_import_fidelity.cpp` pins the two together by counting the same
    tokens in both, which is the drift guard this keeps honest.

WHAT CHANGES

    The adapter already re-routed the group drag once, to hand it the finite
    base under a muted member (`editBaseGain`). That arithmetic was right and
    is kept. What was wrong is where the result went: `commitDrawnGains`, whose
    whole job is to answer "what should DRAWING over a muted band do" and whose
    answer, by the user's own `unmuteOnDraw` setting, is "bring it back". A
    selection drag is not a draw -- it is one shared dB offset over the members
    -- so it gets `commitGroupOffset`, which moves the level underneath a mute
    and leaves the flag alone.

    A second rule now resolves the group base at PRESS time. It has to: the
    offset commit writes into `mutedGainDbRef`, which is the store
    `editBaseGain` reads, so a base resolved live on every sample would add the
    delta to a base that already carried it.

    `replaceSpectrSource` throws on a patch point it cannot find, so a `from`
    that stops matching fails the browser lane loudly rather than silently
    emitting the old behaviour.
"""
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "resources", "editor.html")

EDITS = [
    # ── 1. The offset commit, beside the draw commit it is NOT ──────────
    (
        "adapter injects commitGroupOffset",
        """    commitMany(held, true);
  };
  // A muted band's authored gain is stashed, not destroyed.""",
        """    commitMany(held, true);
  };
  // Dragging a SELECTION is an offset, not a draw: one shared dB delta over
  // every member, which is what preserves the shape the user drew. Mute is a
  // separate axis and this gesture says nothing about it, so the offset lands
  // on the level UNDERNEATH a muted member and the flag is left alone -- the
  // band stays silent, and unmuting later returns it to where the group moved
  // it. Routing an offset through the draw policy above gave two wrong answers
  // and no right one: with unmuteOnDraw on it unmuted every muted member, and
  // with it off it held the mute and discarded the offset.
  const commitGroupOffset = (map) => {
    const moved = new Map();
    for (const [index, value] of map) {
      if (isMuted(targetGainsRef.current[index])) {
        mutedGainDbRef.current[index] = clamp(value, -1, 1) * 24;
        continue;
      }
      moved.set(index, value);
    }
    // Committed even when every member is muted: targetGains did not move in
    // that case, but the stashed levels did, and the publication carries them.
    commitMany(moved, true);
  };
  // A muted band's authored gain is stashed, not destroyed.""",
    ),
    # ── 2. The group drag commits an offset, not a draw ─────────────────
    (
        "group drag commits an offset and preserves mute",
        """      String.raw`        for (const [i, v0] of p.groupStart.entries())
          map.set(i, clamp(editBaseGain(v0, i) + delta, -1, 1));
        commitDrawnGains(map);`,
      'group drag defers the mute decision');""",
        """      String.raw`        for (const [i, base] of p.groupStart.entries())
          map.set(i, clamp(base + delta, -1, 1));
        commitGroupOffset(map);`,
      'group drag offsets levels and never mute');
    // The base is resolved ONCE, at press. commitGroupOffset writes a muted
    // member's new level into the same store editBaseGain reads, so a base
    // resolved per sample would add the delta to a base that already carried
    // it and the band would run away up the plot.
    replaceSpectrSource(
      String.raw`      groupStart: selection.size > 1 && selection.has(band)
        ? new Map([...selection].map(i => [i, targetGainsRef.current[i]]))
        : null,`,
      String.raw`      groupStart: selection.size > 1 && selection.has(band)
        ? new Map([...selection].map(i => [i, editBaseGain(targetGainsRef.current[i], i)]))
        : null,`,
      'group base resolved at press');""",
    ),
]

FORBIDDEN_AFTER = (
    # The defect: an offset handed to the draw commit.
    "map.set(i, clamp(editBaseGain(v0, i) + delta, -1, 1));\n        commitDrawnGains(map);",
)

REQUIRED_AFTER = (
    "const commitGroupOffset = (map) => {",
    "commitGroupOffset(map);",
    "'group base resolved at press'",
    # The draw policy itself is untouched: it still owns the five brushes.
    "const commitDrawnGains = (map) => {",
    "if (settings.unmuteOnDraw !== false) {",
)

COUNTS_AFTER = {
    "const commitGroupOffset = (map) => {": 1,
    "commitGroupOffset(map);": 1,
    # Five brushes still commit as draws. This is the count
    # test_import_fidelity.cpp pins against the emitted document, so the two
    # surfaces are asserted to agree rather than assumed to.
    "commitDrawnGains(map);": 5,
    "mutedGainDbRef.current[index] = clamp(value, -1, 1) * 24;": 1,
}


def main():
    raw = open(PATH, encoding="utf-8").read()
    changed = False
    for label, old, new in EDITS:
        if raw.count(new) >= 1:
            print("already applied ", label)
            continue
        count = raw.count(old)
        if count != 1:
            sys.exit("FAIL %s: patch point occurs %d times, expected 1"
                     % (label, count))
        raw = raw.replace(old, new, 1)
        changed = True
        print("applied         ", label)

    for label, _old, new in EDITS:
        if raw.count(new) != 1:
            sys.exit("FAIL %s: final text occurs %d times after patching, "
                     "expected 1" % (label, raw.count(new)))
    for token in FORBIDDEN_AFTER:
        if raw.count(token):
            sys.exit("FAIL: %r survives patching" % (token,))
    for token in REQUIRED_AFTER:
        if raw.count(token) == 0:
            sys.exit("FAIL: %r is absent after patching" % (token,))
    for token, want in COUNTS_AFTER.items():
        got = raw.count(token)
        if got != want:
            sys.exit("FAIL: %r occurs %d times after patching, expected %d"
                     % (token, got, want))

    if not changed:
        print("no change needed")
        return 0
    open(PATH, "w", encoding="utf-8").write(raw)
    print("written", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
