#!/usr/bin/env python3
"""Give every keyboard-shortcut affordance one owner, and make the keys it
advertises real.

THE DEFECT, in two halves

  1. The EDIT MODE dropdown advertises `1 2 3 4 5`.  The reviewed design
     (`Spectr (standalone).html`) advertises `S L B F G`, one mnemonic letter
     per mode -- SCULPT/LEVEL/BOOST/FLARE/GLIDE -- and its handler is keyed on
     exactly those letters:

         const modeKeys = { 's': 'sculpt', 'l': 'level', 'b': 'boost',
                            'f': 'flare', 'g': 'glide' };
         const k = e.key.toLowerCase();

     The shipping document lost the letters on BOTH sides: the chips read
     `1..5` and the handler's table is keyed `"1".."5"` with no
     `toLowerCase()`.  So restoring the letters on the chip alone would make
     the UI lie -- a chip naming a key that does nothing is worse than no
     chip.  The handler is therefore keyed on the letters here as well.

     AND NEITHER KEY WORKED AT ALL.  The handler's final guard clause was
     `|| document.querySelector('[data-spectr-overlay="true"]')`, and the
     Settings dialog is mounted at all times -- it hides with
     `display: open ? "flex" : "none"` -- with a hard-coded
     `data-spectr-overlay="true"` on both its scrim and its panel.  So that
     selector matched on every keystroke in every state and the handler
     returned before reading the key.  Measured on the shipping standalone:
     2 matches at rest, 3 with the EDIT MODE menu open.  Restoring the chip
     letters without repairing that would have replaced one dead label with
     another, so the guard is repaired here too -- by fixing the PREDICATE,
     not the marker, for the reason recorded at that edit.

     The digits keep working.  They are shipped behaviour, they are what the
     SHORTCUTS popover has been advertising, and an additional accepted key
     cannot break a caller.  The popover now leads with the letters, so both
     surfaces name keys the handler actually accepts.

  2. The preset menu renders its MANAGE shortcut as plain text welded onto
     the caption -- `"MANAGE\\u2026  \\u21e7\\u2318P"`, two spaces and the glyphs,
     left-aligned immediately after the label.  The EDIT MODE rows render the
     identical affordance as a small bordered chip pushed to the row's
     trailing edge.  One control, two presentations.

THE FIX

  `spectrShortcutChipStyle()` is declared once in the chrome.jsx script block
  and called by all three surfaces that draw a shortcut chip -- the EDIT MODE
  dropdown, the preset menu's MANAGE row, and the band context menu -- so none
  of them re-types its values.

  It is a FUNCTION DECLARATION, not a `const`, because two of those three
  callers live in a different `<script>` block (`src/filterbank.jsx`).  That
  mechanism is already load-bearing and proven in this exact document at
  runtime: `filterbank.jsx` calls `spectrPlaceStatusBanner()` and
  `spectrStatusBannerWidth()`, both declared in `chrome.jsx`.  A top-level
  `const` would be a global LEXICAL binding rather than a hoisted function
  binding, and nothing in this document proves that crosses a block here --
  so the shared owner uses the form that is already shipping and working.

  Its body is the EDIT MODE chip's own five declared properties, plus three
  that are provably inert there and required for a multi-glyph shortcut:

      fontFamily    "var(--mono)"   -- already the computed value on every
                                       owner (the edit-mode row, `menuItem`,
                                       and the context-menu row each declare
                                       it), so pinning it changes nothing and
                                       guarantees the chips cannot diverge.
      letterSpacing 0.5             -- the edit-mode row's own value, which its
                                       chip inherits today.  `menuItem` sets
                                       0.8 and the context-menu row 0.3, so
                                       WITHOUT this pin the MANAGE chip would
                                       render at a different tracking from the
                                       chip it is supposed to match.
      whiteSpace / flexShrink       -- `\u21e7\u2318P` is three glyphs wide where
                                       `S` is one.  Without these a narrow row
                                       may wrap or compress it into the label.

  The MANAGE row drops its `display: "block"` override so it uses `menuItem`'s
  own `display: "flex"` -- the same display every pattern row above it already
  uses -- and gains a `flex: 1` spacer before the chip.  That is the exact
  construction the EDIT MODE row uses to right-align its hint.

WHY A SCRIPT AND NOT AN ARTIFACT DIFF

  `native-ui/materialized/materialized-document.runtime.json` is a checked-in
  artifact and neither generator runs on this checkout (the materialized
  generator exits 1 having written 0 patches; `patch_materialized_editor.py`
  exits 1 at a stale needle).  So the edit is exact-text substitution against
  the `html` payload, every patch point asserted unique before anything is
  written, and the result re-parsed as JSON -- replayable, reviewable, and
  re-appliable after the merge conflicts parallel lanes guarantee.

  `resources/editor.html` is deliberately NOT mirrored: it is dead code in
  Spectr, and test_import_fidelity.cpp pins its pre-patch shape on purpose.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# One owner for the shortcut chip.  Both the EDIT MODE rows and the preset
# menu's MANAGE row read this object; neither re-types its values.
CHIP = """function spectrShortcutChipStyle() {
  return {
    fontFamily: "var(--mono)",
    fontSize: 8.5,
    letterSpacing: 0.5,
    opacity: 0.5,
    padding: "1px 5px",
    border: "1px solid rgba(255,255,255,0.14)",
    borderRadius: 2,
    whiteSpace: "nowrap",
    flexShrink: 0
  };
}
"""

# One owner for a menu-row CAPTION, for the same reason and by the same
# mechanism as the chip above.  A bare text child of one of these rows does NOT
# take the row's horizontal padding while a child BOX does, so every row in the
# preset menu has to draw its caption as a box or its ink falls back to the
# row's own left edge and breaks the column.  The factory rows already did;
# SAVE CURRENT... and MANAGE... now do too, reading the same style so a future
# edit cannot move one without the other.
CAPTION = """function spectrMenuItemCaptionStyle() {
  return {
    fontFamily: "var(--mono)",
    fontSize: 10.5,
    letterSpacing: 0.8,
    lineHeight: "14px",
    whiteSpace: "nowrap",
    flexShrink: 0
  };
}
"""

CHROME_HEAD = ('<script type="text/javascript">const { useState: useStateChrome, '
               'useEffect: useEffectChrome, useRef: useRefChrome } = React;\n')

EDITS = [
    ('the shortcut chip and the row caption each have one owner',
     (CHROME_HEAD, CHROME_HEAD + CHIP),
     CHROME_HEAD + CHIP + CAPTION),

    ('EDIT MODE rows read the shared chip, and it is reachable by name',
     '/* @__PURE__ */ React.createElement("span", { style: {\n'
     '        fontSize: 8.5,\n'
     '        opacity: 0.5,\n'
     '        padding: "1px 5px",\n'
     '        border: "1px solid rgba(255,255,255,0.14)",\n'
     '        borderRadius: 2\n'
     '      } }, m.hint)',
     '/* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": m.k, '
     'style: spectrShortcutChipStyle() }, m.hint)'),

    ('SCULPT is reached by S',
     'k: "sculpt",\n    label: "SCULPT",\n    hint: "1",',
     'k: "sculpt",\n    label: "SCULPT",\n    hint: "S",'),
    ('LEVEL is reached by L',
     'k: "level",\n    label: "LEVEL",\n    hint: "2",',
     'k: "level",\n    label: "LEVEL",\n    hint: "L",'),
    ('BOOST is reached by B',
     'k: "boost",\n    label: "BOOST",\n    hint: "3",',
     'k: "boost",\n    label: "BOOST",\n    hint: "B",'),
    ('FLARE is reached by F',
     'k: "flare",\n    label: "FLARE",\n    hint: "4",',
     'k: "flare",\n    label: "FLARE",\n    hint: "F",'),
    ('GLIDE is reached by G',
     'k: "glide",\n    label: "GLIDE",\n    hint: "5",',
     'k: "glide",\n    label: "GLIDE",\n    hint: "G",'),

    # The chips now name letters, so the handler has to accept letters or the
    # UI lies.  Digits stay accepted: they are shipped behaviour and an extra
    # accepted key cannot break a caller.
    ('the handler accepts the letters the chips advertise',
     '    const modeKeys = {\n'
     '      "1": "sculpt",\n'
     '      "2": "level",\n'
     '      "3": "boost",\n'
     '      "4": "flare",\n'
     '      "5": "glide"\n'
     '    };',
     '    const modeKeys = {\n'
     '      s: "sculpt",\n'
     '      l: "level",\n'
     '      b: "boost",\n'
     '      f: "flare",\n'
     '      g: "glide",\n'
     '      "1": "sculpt",\n'
     '      "2": "level",\n'
     '      "3": "boost",\n'
     '      "4": "flare",\n'
     '      "5": "glide"\n'
     '    };'),
    ('a letter key is matched case-insensitively',
     '      const k = e.key;\n'
     '      if (modeKeys[k]) {',
     '      // The chips advertise letters, so the lookup has to be the same\n'
     '      // key the chip names regardless of how the keyboard delivered it.\n'
     '      // A bare `e.key` misses nothing today only because the guard above\n'
     '      // rejects shiftKey; caps lock carries no modifier and would.\n'
     '      const k = typeof e.key === "string" ? e.key.toLowerCase() : e.key;\n'
     '      if (modeKeys[k]) {'),

    # The SHORTCUTS popover is the other surface that names these keys.  It was
    # truthful before (digits worked) and stays truthful now, but it must lead
    # with the letters the chips show or the two surfaces disagree.
    ('the shortcuts popover names the same letters as the chips',
     'React.createElement(Hrow, { k: "1 / 2 / 3" }, "Sculpt \\xB7 Level \\xB7 Boost"), '
     '/* @__PURE__ */ React.createElement(Hrow, { k: "4 / 5" }, "Flare \\xB7 Glide")',
     'React.createElement(Hrow, { k: "S / L / B" }, "Sculpt \\xB7 Level \\xB7 Boost"), '
     '/* @__PURE__ */ React.createElement(Hrow, { k: "F / G" }, "Flare \\xB7 Glide")'),

    # SAVE CURRENT... and MANAGE... are the only two rows in this menu that
    # were not drawing their caption as a box, and they were the only two whose
    # ink sat 9px left of the factory column (measured: 385.09 against 394.09).
    # Both are repaired the same way rather than one of them being offset to
    # meet the other.
    ('SAVE CURRENT draws its caption in a box, so the row padding reaches it',
     '      "data-spectr-save-current": true,\n'
     '      onClick: () => {\n'
     '        onSavePattern();\n'
     '        setPatternMenu(false);\n'
     '      },\n'
     '      style: { ...menuItem, color: "hsl(200,85%,70%)", display: "block", width: "100%" }\n'
     '    },\n'
     '    "SAVE CURRENT\\u2026"\n'
     '  )',
     '      "data-spectr-save-current": true,\n'
     '      onClick: () => {\n'
     '        onSavePattern();\n'
     '        setPatternMenu(false);\n'
     '      },\n'
     '      // No `display: "block"` override, and the caption is a BOX. A bare\n'
     '      // text child of one of these rows does not take the row\'s horizontal\n'
     '      // padding while a child box does, so `display: block` plus a bare\n'
     '      // caption painted this row\'s ink at the row\'s own left edge --\n'
     '      // 9px left of the column every factory row above it uses, which is\n'
     '      // the same 9px the MANAGE row below was hand-compensating for.\n'
     '      style: { ...menuItem, color: "hsl(200,85%,70%)", width: "100%" }\n'
     '    },\n'
     '    /* @__PURE__ */ React.createElement("span", '
     '{ style: spectrMenuItemCaptionStyle() }, "SAVE CURRENT\\u2026")\n'
     '  )'),

    ('MANAGE renders its shortcut as the shared chip at the row trailing edge',
     ('      "data-spectr-pattern-manage": true,\n'
      '      onClick: () => {\n'
      '        onOpenPatternManager();\n'
      '        setPatternMenu(false);\n'
      '      },\n'
      '      style: { ...menuItem, color: "hsl(200,85%,70%)", display: "block", width: "100%" }\n'
      '    },\n'
      '    "MANAGE\\u2026  \\u21e7\\u2318P"\n'
      '  )',
      # The shape this row was left in when the chip first landed: the caption
      # was wrapped in a box (correct) and the row's left padding was then
      # zeroed to compensate (not), because SAVE CURRENT... next door was still
      # a bare text child painting at the row's own left edge.  Both captions
      # are boxes now, so the compensation comes back out.
      '      "data-spectr-pattern-manage": true,\n'
      '      onClick: () => {\n'
      '        onOpenPatternManager();\n'
      '        setPatternMenu(false);\n'
      '      },\n'
      '      // No `display: "block"` override: `menuItem` is already the flex\n'
      '      // row every pattern item above uses, and the spacer below needs\n'
      '      // that flex context to push the chip to the trailing edge.\n'
      '      // padding-left 0, deliberately. A bare text child of these rows does\n'
      '      // NOT take the row\'s horizontal padding -- SAVE CURRENT... above\n'
      '      // paints its ink at the row\'s own left edge -- while a child BOX\n'
      '      // does. Wrapping the caption in a span therefore indented it 10px\n'
      '      // past its neighbour (measured: ink 579 -> 594 screen px). Dropping\n'
      '      // the left padding puts the span back on the shipping caption column\n'
      '      // and leaves padding-right owning the chip\'s inset.\n'
      '      style: { ...menuItem, color: "hsl(200,85%,70%)", width: "100%",\n'
      '               padding: "7px 10px 7px 0" }\n'
      '    },\n'
      '    // The caption keeps its own single-line box, and pins the three type\n'
      '    // properties `menuItem` already sets rather than inheriting them. As a\n'
      '    // bare text child of a flex row it reports NO intrinsic width and drops\n'
      '    // out of the pattern-menu caption census; wrapped but unpinned it\n'
      '    // measures a 16.1 line box against its 14.0 siblings -- a nested span\n'
      '    // resolves line height at ~1.53x where a bare text child of the button\n'
      '    // resolves 1.333x, so the box is pinned rather than inherited.\n'
      '    /* @__PURE__ */ React.createElement("span", '
      '{ style: { fontFamily: "var(--mono)", fontSize: 10.5, letterSpacing: 0.8, '
      'lineHeight: "14px", whiteSpace: "nowrap", flexShrink: 0 } }, "MANAGE\\u2026"),\n'
      '    /* @__PURE__ */ React.createElement("span", { style: { flex: 1 } }),\n'
      '    /* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": "manage", style: spectrShortcutChipStyle() }, "\\u21e7\\u2318P")\n'
      '  )'),
     '      "data-spectr-pattern-manage": true,\n'
     '      onClick: () => {\n'
     '        onOpenPatternManager();\n'
     '        setPatternMenu(false);\n'
     '      },\n'
     '      // `menuItem` is taken unmodified: it is already the flex row every\n'
     '      // pattern item above uses, the spacer below needs that flex context\n'
     '      // to push the chip to the trailing edge, and its own\n'
     '      // `padding: "7px 10px"` is what puts this caption on the same column\n'
     '      // as the factory rows and holds the chip 10px inside the row.\n'
     '      // The left padding is NOT zeroed. It was, to compensate for a boxed\n'
     '      // caption sitting beside a SAVE CURRENT... that was still a bare text\n'
     '      // child -- a bare text child does not take these rows\' horizontal\n'
     '      // padding, a child box does, and the row those two agreed on was the\n'
     '      // wrong one: 9px left of every factory caption. Both captions are\n'
     '      // boxes now, so the row keeps its real inset and nothing offsets\n'
     '      // anything.\n'
     '      style: { ...menuItem, color: "hsl(200,85%,70%)", width: "100%" }\n'
     '    },\n'
     '    // The caption reads the shared row-caption style. It pins the three\n'
     '    // type properties `menuItem` already sets rather than inheriting them:\n'
     '    // a nested span resolves line height at ~1.53x where a bare text child\n'
     '    // of the button resolves 1.333x, so an unpinned wrapper measures a 16.1\n'
     '    // line box against its 14.0 siblings.\n'
     '    /* @__PURE__ */ React.createElement("span", '
     '{ style: spectrMenuItemCaptionStyle() }, "MANAGE\\u2026"),\n'
     '    /* @__PURE__ */ React.createElement("span", { style: { flex: 1 } }),\n'
     '    /* @__PURE__ */ React.createElement("span", { "data-spectr-shortcut-chip": "manage", style: spectrShortcutChipStyle() }, "\\u21e7\\u2318P")\n'
     '  )'),

    # The band context menu is the third surface carrying these same
    # shortcuts, in a different <script> block.  Its table lost the letters
    # the same way, and its chip is a third literal copy of the same five
    # properties.  Both are repaired here so all three surfaces name the same
    # keys and read the same style.
    ('the band context menu names the letters too',
     'const modes = [\n'
     '    { k: "sculpt", label: "Sculpt", hint: "1" },\n'
     '    { k: "level", label: "Level", hint: "2" },\n'
     '    { k: "boost", label: "Boost", hint: "3" },\n'
     '    { k: "flare", label: "Flare", hint: "4" },\n'
     '    { k: "glide", label: "Glide", hint: "5" }\n'
     '  ];',
     'const modes = [\n'
     '    { k: "sculpt", label: "Sculpt", hint: "S" },\n'
     '    { k: "level", label: "Level", hint: "L" },\n'
     '    { k: "boost", label: "Boost", hint: "B" },\n'
     '    { k: "flare", label: "Flare", hint: "F" },\n'
     '    { k: "glide", label: "Glide", hint: "G" }\n'
     '  ];'),

    ('the band context menu reads the shared chip',
     'hint && /* @__PURE__ */ React.createElement("span", { style: {\n'
     '      fontSize: 8.5,\n'
     '      opacity: 0.5,\n'
     '      padding: "1px 5px",\n'
     '      border: "1px solid rgba(255,255,255,0.14)",\n'
     '      borderRadius: 2\n'
     '    } }, hint)',
     'hint && /* @__PURE__ */ React.createElement("span", '
     '{ "data-spectr-shortcut-chip": "band", style: spectrShortcutChipStyle() }, hint)'),

    # WITHOUT THIS the chips above name keys that do nothing at all --
    # letters AND digits.  The handler's last guard clause is
    #
    #     || document.querySelector('[data-spectr-overlay="true"]')
    #
    # and the Settings dialog is mounted at all times (it hides with
    # `display: open ? "flex" : "none"`), its scrim and its panel each carrying
    # a hard-coded `data-spectr-overlay="true"`.  So that selector matched on
    # every keystroke, in every state, and the handler returned before it ever
    # read the key.  Measured on the shipping standalone at rest:
    # `[data-spectr-overlay="true"]` -> 2 matches with nothing open, 3 with the
    # EDIT MODE menu open, and `[data-spectr-settings-panel]` -> 1 with the
    # dialog closed.
    #
    # The MARKER cannot be the thing that is fixed, and that was established by
    # trying it: making it `open ? "true" : void 0` displaced the entire
    # toolbar (CLEAR x 21.0 -> 31.0, its line box 13.2 -> 15.2, and the same
    # shift on every other chrome caption).  runtime.js's
    # `materializedNodeAtPath` filters the CLOSED settings scrim out of its
    # sibling walk keyed on exactly `aria-label === "Settings"` AND
    # `data-spectr-overlay === "true"`, so dropping the attribute while closed
    # un-filters that sibling and every path-resolved binding after it lands on
    # the wrong node.  The attribute is load-bearing; leave it alone.
    #
    # So the PREDICATE is fixed instead.  The settings dialog already publishes
    # an honest live flag (`data-spectr-settings-live`, which runtime.js reads
    # for the same purpose), and every other overlay in the document is
    # conditionally MOUNTED -- so for those, presence still means open.
    ('the overlay guard asks whether an overlay is actually open',
     '    const onKey = (e) => {\n'
     '      const t = e.target;\n',
     '    // The Settings dialog is mounted at ALL times and both its scrim and\n'
     '    // its panel carry a hard-coded data-spectr-overlay="true", so a bare\n'
     '    // `querySelector(\'[data-spectr-overlay="true"]\')` is true in every\n'
     '    // state and killed every shortcut below it. The marker cannot be made\n'
     '    // conditional -- runtime.js\'s materializedNodeAtPath filters the\n'
     '    // closed scrim out of its sibling walk keyed on that exact attribute,\n'
     '    // and dropping it displaces every path-resolved binding after it. The\n'
     '    // dialog\'s own live flag is the honest signal; every other overlay\n'
     '    // here is conditionally mounted, so presence still means open.\n'
     '    function overlayBlocksShortcut() {\n'
     '      const panel = document.querySelector("[data-spectr-settings-panel]");\n'
     '      if (panel && typeof panel.getAttribute === "function"\n'
     '          && panel.getAttribute("data-spectr-settings-live") === "true")\n'
     '        return true;\n'
     '      const nodes = document.querySelectorAll(\'[data-spectr-overlay="true"]\');\n'
     '      for (let i = 0; i < nodes.length; i += 1) {\n'
     '        const node = nodes[i];\n'
     '        if (!node || typeof node.getAttribute !== "function") return true;\n'
     '        if (node.getAttribute("aria-label") === "Settings") continue;\n'
     '        if (node.getAttribute("data-spectr-settings-panel") !== null) continue;\n'
     '        return true;\n'
     '      }\n'
     '      return false;\n'
     '    }\n'
     '    const onKey = (e) => {\n'
     '      const t = e.target;\n'),

    # THE ANALYZER KEY, WHICH TWO SURFACES DISAGREED ABOUT
    #
    #   SHORTCUTS popover:  `6`  -- Cycle analyzer
    #   ANALYZER popover:   "ANALYZER . A to cycle"
    #
    # Only `6` was bound. `A` did nothing, in any state, and the popover that
    # advertised it is the one a user reads while looking at the analyzer. No
    # screenshot can see this and no layout assertion can: both popovers render
    # exactly the same whether the key they name works or not.
    #
    # `A` is BOUND rather than the claim deleted, and `6` is kept, for the same
    # reason the edit-mode chips above kept their digits: this document already
    # settled on a mnemonic letter per surface (S/L/B/F/G), the ANALYZER
    # popover's header is that scheme applied to the analyzer, and deleting it
    # would leave the one dropdown in the editor with no mnemonic while every
    # sibling has one. `a` collides with nothing -- `modeKeys` is s/l/b/f/g plus
    # 1..5, and the only other key comparisons anywhere in the document are
    # Enter, Escape and ArrowDown. An additional accepted key cannot break a
    # caller, so the shipped digit stays live.
    ('the shortcuts popover names both keys the analyzer accepts',
     'React.createElement(Hrow, { k: "6" }, "Cycle analyzer")',
     'React.createElement(Hrow, { k: "A / 6" }, "Cycle analyzer")'),

    ('the analyzer cycles on the letter the ANALYZER popover advertises',
     '      if (k === "6") {\n',
     '      // Both surfaces that name this key are now true: the ANALYZER\n'
     '      // popover header says "A to cycle" and the SHORTCUTS row says\n'
     '      // "A / 6". `k` is already lower-cased above, so this matches A\n'
     '      // however the keyboard delivered it.\n'
     '      if (k === "a" || k === "6") {\n'),

    ('the guard reads the predicate rather than a bare selector',
     't.isContentEditable) || document.querySelector(\'[data-spectr-overlay="true"]\')) return;',
     't.isContentEditable) || overlayBlocksShortcut()) return;'),
]

# A surviving second copy is exactly how two owners drift apart again, and a
# surviving digit hint is exactly the lie this patch exists to remove.
FORBIDDEN_AFTER = (
    '"MANAGE\\u2026  \\u21e7\\u2318P"',
    'border: "1px solid rgba(255,255,255,0.14)",\n        borderRadius: 2\n      } }, m.hint)',
    'label: "SCULPT",\n    hint: "1"',
    'label: "GLIDE",\n    hint: "5"',
    'Hrow, { k: "1 / 2 / 3" }',
    't.isContentEditable) || document.querySelector(\'[data-spectr-overlay="true"]\')) return;',
    'border: "1px solid rgba(255,255,255,0.14)",\n      borderRadius: 2\n    } }, hint)',
    'label: "Sculpt", hint: "1"',
    'label: "Glide", hint: "5"',
    # The SHORTCUTS row that named a key the ANALYZER popover contradicted.
    'React.createElement(Hrow, { k: "6" }, "Cycle analyzer")',
    # The analyzer branch that accepted only the digit while a second surface
    # advertised the letter.
    '      if (k === "6") {\n        e.preventDefault();',
    # The two compensations this row used to carry.  Either one surviving means
    # the caption column is back to being hand-offset rather than laid out.
    'padding: "7px 10px 7px 0"',
    'style: { ...menuItem, color: "hsl(200,85%,70%)", display: "block", width: "100%" }',
)
REQUIRED_AFTER = (
    'function spectrShortcutChipStyle() {',
    '"data-spectr-shortcut-chip": m.k, style: spectrShortcutChipStyle() }, m.hint)',
    'style: spectrShortcutChipStyle() }, "\\u21e7\\u2318P")',
    '"data-spectr-shortcut-chip": "manage"',
    'function spectrMenuItemCaptionStyle() {',
    '{ style: spectrMenuItemCaptionStyle() }, "MANAGE\\u2026")',
    '{ style: spectrMenuItemCaptionStyle() }, "SAVE CURRENT\\u2026")',
    '"data-spectr-shortcut-chip": m.k',
    's: "sculpt",',
    'g: "glide",',
    '"1": "sculpt",',
    'e.key.toLowerCase()',
    'Hrow, { k: "S / L / B" }',
    'hint: "S",',
    'hint: "G",',
    'label: "Sculpt", hint: "S"',
    'label: "Glide", hint: "G"',
    'function overlayBlocksShortcut() {',
    '|| overlayBlocksShortcut()) return;',
    '"data-spectr-shortcut-chip": "band", style: spectrShortcutChipStyle() }, hint)',
    'Hrow, { k: "A / 6" }',
    'if (k === "a" || k === "6") {',
    # The claim the letter binding exists to make true. If a future edit drops
    # this header, the binding is no longer serving a surface and this sweep
    # says so rather than letting the two drift apart again.
    'ANALYZER \\xB7 A to cycle',
)


def escaped(value):
    return json.dumps(value)[1:-1]


def predecessors(old):
    """An edit's `old` is one text, or several ordered OLDEST FIRST.

    This script is replayed against two different starting points: a freshly
    generated document, and the checked-in artifact that already carries an
    earlier revision of the same edit.  Naming both keeps one writer for a
    patch point instead of a second script competing for the same lines.
    """
    return (old,) if isinstance(old, str) else tuple(old)


def choose(old, raw):
    """The most advanced predecessor present, or a reason there is none.

    Newest first: an older predecessor is frequently still a substring of a
    newer one (an edit that appends to a block keeps the block's head), so
    "the first candidate that matches" would re-apply from a state the
    document has already moved past.
    """
    for cand in reversed(predecessors(old)):
        count = raw.count(escaped(cand))
        if count == 1:
            return cand, None
        if count > 1:
            return None, 'patch point occurs %d times, expected 1' % count
    return None, 'no known predecessor text is present'


def main():
    for label, _old, new in EDITS:
        if not new:
            sys.exit('FAIL %s: empty replacement has no applied marker' % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for label, old, new in EDITS:
        new_e = escaped(new)
        if raw.count(new_e) >= 1:
            print('already applied ', label)
            already += 1
            continue
        cand, why = choose(old, raw)
        if cand is None:
            sys.exit('FAIL %s: %s' % (label, why))
        raw = raw.replace(escaped(cand), new_e)
        changed = True
        applied += 1
        print('applied         ', label)

    # A mix of "applied" and "already applied" is the NORMAL state once an edit
    # names more than one predecessor: the document carries the settled edits
    # and this run advances the one that moved.  The proof that the result is
    # whole is the FORBIDDEN/REQUIRED sweep below, which adjudicates the final
    # text rather than the bookkeeping -- a strictly stronger check than the
    # counter comparison that used to stand here.
    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change needed')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('written', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
