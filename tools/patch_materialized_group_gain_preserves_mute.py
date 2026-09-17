#!/usr/bin/env python3
"""A selection drag moves every member's LEVEL and touches no member's mute.

THE DEFECT, AS THE USER MET IT

    Select all, Shift-drag a few bands to mute them, then drag the whole
    selection up.  The muted bands unmuted and came back.

    Measured on the shipping standalone before this change, driven entirely by
    real presses through `SPECTR_GESTURES` (paint, then a Shift-held mute
    brush, then a Command-held marquee, then the group drag):

        after mute      muted [6, 7, 8]   gains 6..12 = +7.0 dB
        after the drag  muted []          gains 6..12 = +15.0 dB

    The mute flag was CLEARED, not lost.  Nothing re-derived the band from a
    field that had no room for mute; the arithmetic was already right --
    `editBaseGain` resolved each muted member's stashed +7 dB and added the
    shared +8 dB delta to get +15 dB.  What went wrong is that the result was
    committed through a path that reads a finite gain as "no longer muted".

WHY THE GROUP DRAG CANNOT ROUTE THROUGH `commitDrawnGains`

    That path answers a different question -- what should DRAWING over a muted
    band do -- and `unmuteOnDraw` is the user's answer to it: draw on a muted
    band and the band comes back.  That is right for the sculpt / level /
    boost / flare / glide brushes, which paint an absolute level onto whatever
    they sweep, and those keep using it.

    A selection drag is not a draw.  It is an OFFSET: one shared dB delta
    applied to every member, which is what preserves the shape the user drew
    (and what the macro lane chose for the same reason).  Sending an offset
    through the draw path gave two wrong answers and no right one -- with
    `unmuteOnDraw` on it silently unmuted every muted member, and with it off
    it held the mute but threw the offset away, freezing those bands while the
    rest of the selection moved.

    So mute and gain are separate axes here, which is the rule the rest of the
    model already follows: a muted band keeps the level it will return to,
    which is the whole reason unmuting restores that level instead of landing
    on 0 dB.  The offset lands on the level UNDERNEATH a muted member; the
    member stays silent; unmuting later returns it to where the group moved it.

WHY THE BASE IS RESOLVED AT PRESS TIME

    `commitGroupOffset` writes a muted member's new level into
    `mutedGainDbRef`, which is the same store `editBaseGain` reads.  The move
    handler recomputes from the press-time snapshot on every sample, so had it
    kept calling `editBaseGain` live it would have added the delta to a base
    that already carried the previous sample's delta, and a muted band would
    have run away up the plot while its unmuted neighbours tracked the pointer.
    Resolving the base once, at press, makes the snapshot mean what its name
    says.  The boost / flare / glide brushes still call `editBaseGain` against
    `p.startSnap`; they never write that store, so they cannot accumulate.
"""
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def enc(text):
    """The page is stored as a JSON *string*, so every `"` is `\\"` on disk."""
    return json.dumps(text)[1:-1]


EDITS = [
    # ── 1. The offset commit, beside the draw commit it is NOT ──────────
    (
        "commitGroupOffset holds mute and moves the level underneath it",
        """  const restoreMutedGain = (index) => {""",
        """  // Dragging a SELECTION is an offset gesture, not a draw: one shared dB
  // delta moves every member's level, which is what preserves the shape the
  // user drew. Mute is a separate axis and this gesture says nothing about
  // it -- a muted member keeps the level it will return to (the reason
  // unmuting restores that level rather than landing on 0 dB), so the offset
  // lands UNDERNEATH the mute and the flag is left alone.
  //
  // Deliberately not `commitDrawnGains`. That path answers what DRAWING over
  // a muted band should do, and `unmuteOnDraw` is the user's answer to it.
  // Sending an offset through it gave two wrong answers and no right one:
  // with the setting on it unmuted every muted member, and with it off it
  // held the mute but discarded the offset, freezing those bands while the
  // rest of the selection moved.
  const commitGroupOffset = (map) => {
    const moved = /* @__PURE__ */ new Map();
    for (const [index, value] of map) {
      if (isMuted(targetGainsRef.current[index])) {
        mutedGainDbRef.current[index] = clamp(value, -1, 1) * 24;
        continue;
      }
      moved.set(index, value);
    }
    // Committed even when every member is muted. Nothing above moved
    // targetGains in that case, but the stashed levels DID move and the field
    // the processor reads carries them, so the publication still has to be
    // queued or the native side never learns the new levels.
    commitMany(moved, true);
  };
  const restoreMutedGain = (index) => {""",
    ),
    # ── 2. Resolve the group base at PRESS time ─────────────────────────
    (
        "group base is resolved once, at press",
        """      groupStart: selection.size > 1 && selection.has(band) ? new Map([...selection].map((i) => [i, targetGainsRef.current[i]])) : null,""",
        """      // Resolved HERE rather than per sample. A muted member's base is the
      // level stashed under its mute, and the commit below writes back into
      // that same store -- so reading it live each sample would add the delta
      // to a base that already carries it, and the band would run away.
      groupStart: selection.size > 1 && selection.has(band) ? new Map([...selection].map((i) => [i, editBaseGain(targetGainsRef.current[i], i)])) : null,""",
    ),
    # ── 3. The move handler consumes the resolved base ──────────────────
    (
        "group drag offsets the resolved base and preserves mute",
        """        for (const [i, v0] of p.groupStart.entries())
          map.set(i, clamp(editBaseGain(v0, i) + delta, -1, 1));
        commitDrawnGains(map);""",
        """        for (const [i, base] of p.groupStart.entries())
          map.set(i, clamp(base + delta, -1, 1));
        commitGroupOffset(map);""",
    ),
]

# The defect itself, spelled as text. A group branch that still routes an
# offset through the draw commit is the bug, whatever else is true.
FORBIDDEN_AFTER = (
    'for (const [i, v0] of p.groupStart.entries())',
    'map.set(i, clamp(editBaseGain(v0, i) + delta, -1, 1));',
)

REQUIRED_AFTER = (
    'const commitGroupOffset = (map) => {',
    'commitGroupOffset(map);',
    # The draw path stays, unchanged, for the brushes that ARE draws.
    'const commitDrawnGains = (map) => {',
    'if (unmuteOnDrawRef.current) {',
    # The brushes keep resolving their base against the press-time snapshot.
    'const v0 = editBaseGain(p.startSnap[b], b);',
)

COUNTS_AFTER = {
    # Exactly one offset commit, and exactly one caller of it. A second
    # groupStart branch would make the routing depend on which one the
    # interpreter reached first.
    'const commitGroupOffset = (map) => {': 1,
    'commitGroupOffset(map);': 1,
    'if (p.groupStart) {': 1,
    # The five drawing brushes still commit as draws: sculpt, level, boost,
    # flare, glide. If this drops to 4 the group branch was not the one moved.
    'commitDrawnGains(map);': 5,
    # The mute store gains exactly one new writer -- this one. Its other
    # writers are the two mute transitions in commitGain / commitMany.
    'mutedGainDbRef.current[index] = clamp(value, -1, 1) * 24;': 1,
}


def main():
    raw = open(PATH, encoding='utf-8').read()
    changed = False
    for label, old, new in EDITS:
        new_e = enc(new)
        if raw.count(new_e) >= 1:
            print('already applied ', label)
            continue
        old_e = enc(old)
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e, 1)
        changed = True
        print('applied         ', label)

    for label, _old, new in EDITS:
        count = raw.count(enc(new))
        if count != 1:
            sys.exit('FAIL %s: final text occurs %d times after patching, '
                     'expected 1' % (label, count))

    for token in FORBIDDEN_AFTER:
        if raw.count(enc(token)):
            sys.exit('FAIL: %r survives patching' % (token,))
    for token in REQUIRED_AFTER:
        if raw.count(enc(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))
    for token, want in COUNTS_AFTER.items():
        got = raw.count(enc(token))
        if got != want:
            sys.exit('FAIL: %r occurs %d times after patching, expected %d'
                     % (token, got, want))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str) or not document['html']:
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change needed')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
