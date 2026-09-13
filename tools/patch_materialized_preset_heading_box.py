#!/usr/bin/env python3
"""Give the selected preset's NAME a box big enough for the name.

THE DEFECT

  The detail pane's heading paints the preset name, then a FACTORY / USER
  badge.  It shipped painting `★FLA` -- the "T" sliced through the middle of
  the glyph -- and was reported as the badge "overlapping" the name.

  It is not an overlap.  Measured on the built standalone with the manager
  open on `factory:flat` (SPECTR_LAYOUT_DUMP, 1320x860 authored space):

      star     node box x 607.000  w  9.906
      name     node box x 622.906  w 25.094      <-- the defect
      FACTORY  node box x 658.000  w 64.000

  The title's own box therefore ends at 648.000 and the badge begins at
  658.000: ten clear pixels between them, and the screenshot shows that gap.
  What the screenshot ALSO shows is the name stopping at "FLA", because the
  name's box is 25.094px and the string it paints needs about 37.6 --
  `overflow: "hidden"` on the title clips the rest.

  25.094 is not a random number.  The SAME four characters in a preset ROW
  measure 26.000 at `fontSize: 10` / `letterSpacing: 0.5` -- exactly
  4 x (0.6 x 10 + 0.5), the mono advance -- and the heading is authored at
  `fontSize: 14` / `letterSpacing: 1`, which wants 4 x (0.6 x 14 + 1) = 37.6.
  So the name is MEASURED at roughly the ~10px the surrounding panel
  inherits and PAINTED at the 14px its own span declares.

  The difference between the two is structural, and it is the same finding a
  sibling lane made on the transport row's A / B labels.  In the row, the
  name's font size comes from the row's `<div>` and is inherited by a `<span>`
  that declares none -- and it measures right.  In the heading, the font size
  is declared ON the span, whose text child is measured against the
  containing BLOCK instead.  A `<span>` is not a box here, so a font size it
  declares is not one either.

WHY THE EXISTING GATE DID NOT CATCH IT

  `test_native_state_parity.cpp` already asserts
  `titleRect.right > sourceRect.left` and calls that "source badge overlaps
  its title".  It passes, and it is right to: 648.000 < 658.000.  The gate
  compares BOXES, and the defect is INK outside a box that never grew.  A
  companion assertion on the name's own Label -- box against
  `intrinsic_width()` -- ships beside it.

THE FIX

  The name gets its own element, and that element carries the type.

    * the title becomes a `<div>` (a real box) laid out as `display: flex`,
      `minWidth: 0` so it may shrink,
    * the star becomes a `<div>` with `flexShrink: 0` and an explicit
      `fontSize`, so it is measured at the size it paints,
    * the NAME moves into its own `<div>` carrying `fontSize: 14`,
      `letterSpacing: 1`, `fontWeight: 500` -- the block its text is measured
      against is now the block that declares the type -- and keeps
      `whiteSpace: nowrap` + `overflow: hidden` + `textOverflow: ellipsis` so
      a genuinely long name ellipsises INSIDE ITS OWN BOX instead of running
      at the badge,
    * the badge becomes a `<div>` with `flexShrink: 0`, so it is never the
      element that gives when the heading is tight.

  `whiteSpace: "nowrap"` alone was NOT the fix and was not tried as one: it
  makes ink measurable while the layout box keeps its old size, which is a
  persuasive way to change nothing.  Nudging the badge's margin was not the
  fix either -- that moves the collision without growing the box, and the
  name would still lose its last characters.

  `data-spectr-manager-title` and `data-spectr-pattern-id` stay on the OUTER
  element: `test_native_state_parity.cpp` resolves the title by that
  attribute and reads both `textContent` and the pattern id off it.

WHY A SECOND WRITER FOR THIS REGION

  The nominal owner of these two elements' styles is
  `tools/patch_materialized_editor.py`, and extending it was the first
  choice.  It does not run: on this commit it exits 1 with
  `FAIL semantic popup surfaces delegate dismissal to Pulp: patch point
  occurs 3 times`, writing nothing.  An edit added to a script that cannot
  execute can never be applied or replayed, so this region gets a runnable
  writer instead.  Each edit below accepts several `before` spellings so it
  converges whether or not that script is ever repaired.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# The title, in every spelling this repo has shipped. The pristine capture had
# no identity attribute and no line-box styles; `patch_materialized_editor.py`
# added those in two passes. All three converge on the same box.
_TITLE_TAIL = (', isDefault && /* @__PURE__ */ React.createElement("span", '
               '{ style: { color: "hsl(50,90%,65%)", marginRight: 6 } }, '
               '"\\u2605"), pattern.name)')
_TITLE_HEAD = 'React.createElement("span", { "data-spectr-manager-title": true, '

TITLE_BEFORE = [
    # current on main -- identity attribute plus the centred line box
    _TITLE_HEAD + '"data-spectr-pattern-id": pattern.id, style: { fontSize: 14, '
    'letterSpacing: 1, fontWeight: 500, whiteSpace: "nowrap", overflow: '
    '"hidden", textOverflow: "ellipsis", display: "inline-flex", alignItems: '
    '"center", minHeight: 26, lineHeight: 1 } }' + _TITLE_TAIL,
    # identity attribute, before the line box was centred
    _TITLE_HEAD + '"data-spectr-pattern-id": pattern.id, style: { fontSize: 14, '
    'letterSpacing: 1, fontWeight: 500, whiteSpace: "nowrap", overflow: '
    '"hidden", textOverflow: "ellipsis" } }' + _TITLE_TAIL,
    # before the identity attribute existed at all
    'React.createElement("span", { "data-spectr-manager-title": true, style: '
    '{ fontSize: 14, letterSpacing: 1, fontWeight: 500, whiteSpace: "nowrap", '
    'overflow: "hidden", textOverflow: "ellipsis" } }' + _TITLE_TAIL,
]

TITLE_AFTER = (
    # ONE text child, in ONE box that declares the type it paints at.
    #
    # The star used to be a second, nested element, and a title with two
    # children is exactly the case this runtime measures wrong: with the star
    # present the name's box came back 21px for 38px of ink, while the SAME
    # box on a non-default preset (one child, no star) came back 123px for
    # 123px of ink. So the star folds into the string instead of sitting
    # beside it, and the box is measured on what it actually paints.
    'React.createElement("div", { "data-spectr-manager-title": true, '
    '"data-spectr-pattern-id": pattern.id, style: { fontSize: 14, '
    'letterSpacing: 1, fontWeight: 500, lineHeight: 1, minHeight: 26, '
    'display: "flex", alignItems: "center", whiteSpace: "nowrap", '
    # The name ellipsises INSIDE ITS OWN BOX. `maxWidth` is the budget a
    # 48-character user name is allowed before it does; the longest factory
    # name, "SUB ONLY (< 160 Hz)", measures 179 and fits under it.
    'overflow: "hidden", textOverflow: "ellipsis", flexShrink: 0, '
    'maxWidth: 190 } }, pattern.name)'
)

_BADGE_TAIL = ('\n    fontSize: 8.5,\n    letterSpacing: 1.5,\n'
               '    opacity: 0.6,\n    padding: "2px 6px",\n'
               '    border: "1px solid rgba(255,255,255,0.15)",\n'
               '    borderRadius: 2\n  } }, isFactory ? "FACTORY" : "USER")')

BADGE_BEFORE = [
    'React.createElement("span", { "data-spectr-manager-source": true, style: {\n'
    '    display: "inline-flex",\n    alignItems: "center",\n'
    '    minHeight: 26,\n    lineHeight: 1,' + _BADGE_TAIL,
    'React.createElement("span", { "data-spectr-manager-source": true, style: {'
    + _BADGE_TAIL,
]

BADGE_AFTER = (
    'React.createElement("div", { "data-spectr-manager-source": true, style: {\n'
    '    display: "flex",\n    alignItems: "center",\n'
    '    minHeight: 26,\n    lineHeight: 1,\n'
    # The badge is never the element that gives. Without this the heading
    # could resolve a tight row by crushing the badge instead of ellipsising
    # the name, which is the wrong half to sacrifice.
    '    flexShrink: 0,\n    whiteSpace: "nowrap",' + _BADGE_TAIL
)

EDITS = [
    ('the preset name is measured in a box that declares its own type',
     TITLE_BEFORE, TITLE_AFTER),
    ('the source badge is a box that never shrinks',
     BADGE_BEFORE, BADGE_AFTER),
]

# A surviving `<span>` for either subject is the defect itself, and a title
# whose text is not in its own box is the measurement bug coming back.
FORBIDDEN_AFTER = (
    'React.createElement("span", { "data-spectr-manager-title"',
    # The heading's default star: a second child is what this runtime
    # measures wrong, so the heading carries exactly one.
    '{ style: { color: "hsl(50,90%,65%)", marginRight: 6 } }, "\u2605")',
    'React.createElement("span", { "data-spectr-manager-source"',
)
REQUIRED_AFTER = (
    'React.createElement("div", { "data-spectr-manager-title": true,',
    'maxWidth: 190 } }, pattern.name)',
    'React.createElement("div", { "data-spectr-manager-source": true,',
    # The badge's guard against being the element that shrinks.
    '    flexShrink: 0,\n    whiteSpace: "nowrap",',
)


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    changed = False
    for label, befores, after in EDITS:
        after_e = escaped(after)
        if raw.count(after_e) == 1:
            print('already applied ', label)
            continue
        hits = [b for b in befores if raw.count(escaped(b)) == 1]
        ambiguous = [b for b in befores if raw.count(escaped(b)) > 1]
        if ambiguous:
            sys.exit('FAIL %s: a patch point is ambiguous' % label)
        if len(hits) != 1:
            sys.exit('FAIL %s: %d of %d before-variants matched exactly once'
                     % (label, len(hits), len(befores)))
        raw = raw.replace(escaped(hits[0]), after_e)
        changed = True
        print('applied         ', label)

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) != 1:
            sys.exit('FAIL: %r does not appear exactly once after patching'
                     % (token,))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change; the heading already gives the name its own box')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('wrote', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
