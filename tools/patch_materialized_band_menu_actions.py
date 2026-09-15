#!/usr/bin/env python3
"""Make the band context menu's unmute restore, and replace its arbitrary
neighbourhood selections with "Select all" / "Select none".

TWO PATHS FOR ONE ACTION, AND ONLY ONE OF THEM WAS COMPLETE.

    The user reported it as "mute in context menu works but unmute doesn't
    return to prior state while clicking the mute speaker does".  That is
    exactly what the document said.  Muting stashes the band's level in
    `mutedGainDbRef` (`commitGain` does this itself, so the stash is written
    no matter which surface issued the mute).  Unmuting is supposed to read
    it back.  Three paths did:

        the CLICK path         `commitGain(p.band, isMuted(cur) ? restored : -Infinity)`
        the SHIFT+DRAG brush   `restored = ... mutedGainDbRef.current[index] ...`
        the `m` group toggle   `map.set(i, Number.isFinite(db) ? ... : 0)`

    and the band context menu did not:

        commitGain(b, isMuted(cur) ? 0 : -Infinity);
                                     ^ flattens to 0 dB, discarding the level

    So the same gesture kept or destroyed the user's level depending only on
    which surface issued it.  The menu is the young code (its SDK only started
    reaching the product at v0.854.1), which is why the older paths are the
    correct ones.

ONE NAMED RULE INSTEAD OF FOUR SPELLINGS.  `restoreMutedGain(index)` is added
next to `editBaseGain`, which already computed exactly this and is rewritten
to call it.  The menu's two selection actions use it too.  The remaining two
historical spellings (the click path and the `m` toggle) are left alone
deliberately: they are already CORRECT, a sibling patch script asserts their
exact text, and this change is about the path that was wrong.  The duplication
is recorded rather than swept, so the next reader sees one named rule and two
known copies instead of four anonymous ones.

`onMuteSel` ALSO COULD NOT UNMUTE.  It was an unconditional mute:

    for (const i of selection) map.set(i, -Infinity);

There is no second press that reverses it, so a group mute from the menu was
a one-way trip.  The bank already owns the correct rule -- `toggleMuteSelection`
in `sharedState.current`, which implements the mixed-state rule and restores
rather than flattens -- so the menu now CALLS that owner instead of being a
third writer of band state.  Its label becomes "Mute / Unmute selection" so it
names what it does, matching the per-band row.

SELECT ±3 / ±8 WERE ARBITRARY.  Nothing in the product motivates 3 or 8, and
neither is discoverable.  They become "Select all" and "Select none".

    "Select none" and the old "Clear selection" are THE SAME ACTION -- both
    were `setSelection(new Set())`.  They are collapsed into one item rather
    than shipped as two names for one behaviour.

    The two select items are UNGATED.  They previously sat inside the
    `hasBand &&` group, but a right-press outside the plot opens the menu with
    `band = -1`, and removing "Clear selection" from the selection group while
    leaving "Select none" gated on a band would have left that state with no
    way to clear a live selection.  Rendering them unconditionally closes that
    hole instead of trading one reachability gap for another.

    Both gain `data-spectr-band-action` ids (`select-all` / `select-none`).
    Menu rows are otherwise unaddressable -- only `mute-band` and `reset-band`
    carried ids -- so a test could not reach them, and `:nth-of-type` does not
    resolve in this runtime's DOM shim.

NOT IN SCOPE, DELIBERATELY: the menu's dismissal (it does not close on an
outside press, and its rows' `onClose()` never reaches the native runtime) and
the row interleaving / stranded indent.  Those are menu MECHANICS and belong
with exposing `pulp::view::ContextMenu` through the widget bridge, not with a
patch to this document.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
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


# Each entry: (label, old, new). Every `old` must occur EXACTLY once.
EDITS = [
    (
        "one named restore rule, used by editBaseGain",
        """const editBaseGain = (value, index) => {
    if (!isMuted(value)) return value;
    const db = mutedGainDbRef.current[index];
    return Number.isFinite(db) ? clamp(db / 24, -1, 1) : 0;
  };""",
        """const restoreMutedGain = (index) => {
    const db = mutedGainDbRef.current[index];
    return Number.isFinite(db) ? clamp(db / 24, -1, 1) : 0;
  };
  const editBaseGain = (value, index) => {
    if (!isMuted(value)) return value;
    return restoreMutedGain(index);
  };""",
    ),
    (
        "the menu's unmute restores instead of flattening to 0 dB",
        """onMuteBand: (b) => {
          const cur = targetGainsRef.current[b];
          commitGain(b, isMuted(cur) ? 0 : -Infinity);""",
        """onMuteBand: (b) => {
          const cur = targetGainsRef.current[b];
          commitGain(b, isMuted(cur) ? restoreMutedGain(b) : -Infinity);""",
    ),
    (
        "Select all / Select none replace the neighbourhood selections",
        """onSelectAround: (b, r) => {
          const nxt = /* @__PURE__ */ new Set();
          for (let i = Math.max(0, b - r); i <= Math.min(N - 1, b + r); i++) nxt.add(i);
          setSelection(nxt);
        },
        onClearSel: () => setSelection(/* @__PURE__ */ new Set()),""",
        """onSelectAll: () => {
          const nxt = /* @__PURE__ */ new Set();
          for (let i = 0; i < N; i++) nxt.add(i);
          setSelection(nxt);
          if (onStatus) onStatus(`${N} BANDS SELECTED`);
        },
        onSelectNone: () => {
          setSelection(/* @__PURE__ */ new Set());
          if (onStatus) onStatus("SELECTION CLEARED");
        },""",
    ),
    (
        "the menu's group mute defers to the bank's toggle owner",
        """onMuteSel: () => {
          const map = /* @__PURE__ */ new Map();
          for (const i of selection) map.set(i, -Infinity);
          commitMany(map);
        },""",
        """onMuteSel: () => {
          const owner = sharedState && sharedState.current;
          const result = owner && typeof owner.toggleMuteSelection === "function"
            ? owner.toggleMuteSelection() : null;
          if (!result) { if (onStatus) onStatus("NO SELECTION"); return; }
          if (onStatus) onStatus(`${result.count} BAND${result.count === 1 ? "" : "S"} ${result.muted ? "MUTED" : "UNMUTED"}`);
        },""",
    ),
    (
        "ContextMenu takes the two select props",
        """function ContextMenu({ x, y, band, N, selection, editMode, onClose, onEditMode, onMuteBand, onZeroBand, onSoloBand, onSelectAround, onClearSel, onZeroSel, onMuteSel, onFitView }) {""",
        """function ContextMenu({ x, y, band, N, selection, editMode, onClose, onEditMode, onMuteBand, onZeroBand, onSoloBand, onSelectAll, onSelectNone, onZeroSel, onMuteSel, onFitView }) {""",
    ),
    (
        "the rows: ungated Select all / Select none, no duplicate clear",
        """React.createElement(Item, { label: "Solo", onClick: () => onSoloBand(band), sub: "mute others" }), /* @__PURE__ */ React.createElement(Item, { label: "Select \\xB13", onClick: () => onSelectAround(band, 3) }), /* @__PURE__ */ React.createElement(Item, { label: "Select \\xB18", onClick: () => onSelectAround(band, 8) })),""",
        """React.createElement(Item, { label: "Solo", onClick: () => onSoloBand(band), sub: "mute others" })),
    /* @__PURE__ */ React.createElement(Item, { action: "select-all", label: "Select all", onClick: onSelectAll }),
    /* @__PURE__ */ React.createElement(Item, { action: "select-none", label: "Select none", onClick: onSelectNone, disabled: !hasSel }),""",
    ),
    (
        "drop the duplicated Clear selection, name the toggle honestly",
        """React.createElement(Item, { label: "Mute selection", onClick: onMuteSel }), /* @__PURE__ */ React.createElement(Item, { label: "Clear selection", onClick: onClearSel })),""",
        """React.createElement(Item, { action: "mute-selection", label: "Mute / Unmute selection", onClick: onMuteSel })),""",
    ),
]

# The old Solo row is consumed by the rows edit above (it closes the band
# fragment there), so its original spelling must be gone afterwards.
FORBIDDEN_AFTER = (
    'Select \\xB13',
    'Select \\xB18',
    'onSelectAround',
    'onClearSel',
    '"Clear selection"',
    'commitGain(b, isMuted(cur) ? 0 : -Infinity);',
    'for (const i of selection) map.set(i, -Infinity);',
)

REQUIRED_AFTER = (
    'const restoreMutedGain = (index) => {',
    'commitGain(b, isMuted(cur) ? restoreMutedGain(b) : -Infinity);',
    '"select-all"',
    '"select-none"',
    'label: "Select all"',
    'label: "Select none"',
    'label: "Mute / Unmute selection"',
    'owner.toggleMuteSelection()',
    # The already-correct restore paths this change deliberately leaves alone.
    'commitGain(p.band, isMuted(cur) ? restored : -Infinity);',
    'const mute = bands.some((i) => !isMuted(targetGainsRef.current[i]));',
    # The per-band rows that keep their ids.
    'action: "mute-band"',
    'action: "reset-band"',
    # Zero selection is NOT the same action as Select none and stays.
    'label: "Zero selection"',
)


# Rows that must appear EXACTLY ONCE afterwards. The presence checks above
# cannot see a DUPLICATE: the first version of this script re-emitted the Solo
# row while the original survived upstream of the patch point, and every
# assertion still passed because each individual token was present and the new
# text was unique. A menu with two Solo rows is precisely the "rows in the
# wrong place" class of defect this change is adjacent to, so it is counted.
COUNTS_AFTER = {
    'label: "Solo"': 1,
    'label: "Select all"': 1,
    'label: "Select none"': 1,
    'label: "Mute / Unmute"': 1,
    'label: "Mute / Unmute selection"': 1,
    'label: "Zero selection"': 1,
    'label: "Reset to 0 dB"': 1,
    'label: "Fit full range"': 1,
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
