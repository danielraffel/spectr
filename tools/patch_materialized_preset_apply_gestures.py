#!/usr/bin/env python3
"""Make the Preset Manager's advertised commit gestures real.

THE DEFECT

  The manager's own footer reads "DOUBLE-CLICK to apply", and double-clicking
  a preset row did nothing at all on the shipping standalone.  Measured on the
  built app, one row, three runs of the same instrument:

      1 click  on factory:comb -> manager OPEN, 0/32 bands moved  (select only)
      2 clicks on factory:comb -> manager OPEN, 0/32 bands moved  (dead)
      APPLY button (control)   -> manager CLOSED, 32/32 bands moved

  So the footer advertised a gesture that could never fire.  The prop was
  there -- `onDoubleClick: onDblClick` on the row -- which is why this reads
  as wired on a source skim.  It is not.  `prop-applier`'s `eventNameFor`
  lowercases the prop name, so `onDoubleClick` registers the React callback
  under the bridge event name `doubleclick`, and NOTHING in Pulp dispatches
  that name: a sweep of core/ packages/ tools/ finds 0 files containing
  `doubleclick`, against controls `dblclick` 4, `mouseenter` 12, `panchange`
  4.  The one `dblclick` path that does exist is web-compat's
  `addEventListener("dblclick")`, which is a different name and a different
  mechanism -- React never calls it.

  Return was dead for a DIFFERENT reason, and it is worth naming because a
  sibling lane had just found the Edit Mode shortcut handler dead behind an
  always-matching overlay guard.  That is not what happened here.  This
  dialog's keydown handler simply has no Enter branch: it handles Escape,
  then returns for anything that is not ArrowDown/ArrowUp.  Proved three
  ways on the built app -- the key fixture reported `listeners_fired=2` (the
  dispatch is alive), `popup_active_guard=null` (this handler's guard does
  not match), and Escape through that same handler closed the dialog.

THE FIX

  One owner, `applyPattern`, reached by all three commit gestures -- the
  APPLY button, the second click of a double-click, and Return.  `onApply`
  at the App call site already applies the pattern AND closes the manager,
  so every caller gets the whole gesture rather than half of it.

  The double-click is detected from the CLICK STREAM rather than from
  `onDoubleClick`, because two clicks are what a real double-click delivers
  in both renderers and `doubleclick` is deliverable in neither.  The dead
  prop is removed rather than left beside the live path: a second writer for
  one gesture is how the two drift apart.

  The window is 500 ms, matching AppKit's own `NSEvent.doubleClickInterval`
  default.  Deliberately not tighter: a threshold below the platform's
  interval would reject genuine system double-clicks near the top of the
  range, and an affordance that works four times in five is worse than one
  that does not exist.

  Return reuses the arrow keys' guards -- a Pulp popup owns the keyboard, and
  a text field owns its own Return (the search and rename inputs submit with
  it).  The selection is read through a ref republished every render, because
  the keydown listener is registered once per open and its closure would
  otherwise commit whatever was selected at that moment.

WHY A SCRIPT AND NOT AN ARTIFACT DIFF

  `native-ui/materialized/materialized-document.runtime.json` is a checked-in
  artifact and neither generator runs on this checkout.  So the edit is exact
  text substitution against the `html` payload, every patch point asserted
  unique before anything is written, and the result re-parsed as JSON --
  replayable and re-appliable after the merge conflicts parallel lanes
  guarantee.

  `resources/editor.html` is deliberately NOT mirrored: it is dead code in
  Spectr, and test_import_fidelity.cpp pins its pre-patch shape on purpose.

THIS SCRIPT ALSO OWNS TWO THINGS THAT ARE NOT GESTURES

  It owns the manager's PER-OPENING RESET and the shape of `PatternRow`, so
  two later fixes land here rather than growing a second writer for the same
  two regions.

  1. THE SEARCH OUTLIVED THE DIALOG.  `App` renders `PatternManager`
     unconditionally and passes `open` as a PROP, so `if (!open) return null`
     tears the DOM down while every `usePM` cell in the component survives.
     A search therefore persisted across close and reopen with nothing on
     screen admitting it: measured on the built app, a fresh open listed 8
     rows; after a search, 0; after the panel's X and a reopen, still 0, with
     the list captions reading "FACTORY \u00b7 0" / "USER \u00b7 0" beneath a
     header still reading "\u2014 0 user \u00b7 8 factory".

     The header and the list are not disagreeing by accident -- the header
     reads `factory.length` (the inventory) and the list reads
     `filteredFactory.length` (the view), which is correct in itself and is
     exactly why the state was invisible.  And NO control anywhere clears the
     search: the panel's `\u00d7` is `onClose`, and the rail's CLEAR is
     `onClearAll`, which zeroes the band gains.

     The reset is its OWN effect keyed on `[open]` ALONE, and that is the
     whole design.  The sibling keydown effect next to it lists
     `[open, onClose, setSelectedId]`, and `onClose` is an inline arrow at the
     App call site -- a fresh identity on every App render.  Putting
     `setQuery("")` in THAT effect would therefore wipe the field while the
     user was still typing in it.  Keyed on the boolean, the reset runs on the
     open transition and nowhere else.

     `selectedId` is deliberately NOT reset: reopening on the preset you were
     looking at is the useful behaviour, and it is not what was reported.

  2. THE ROW'S TRAILING F / U CHIP IS GONE.  Each row painted a small
     right-aligned "F" or "U" in the same bordered pill the editor uses for
     real keyboard shortcuts (the Sculpt menu's letters, the EDIT MODE
     chips), so it read as a keybinding.  It was not one: the element carries
     no onClick, no ref, no data attribute, no role and no tabIndex -- it is a
     text node in a styled box -- and the rows are already grouped under
     FACTORY and USER headings that say the same thing.

     `F` IS a live accelerator elsewhere (`modeKeys.f` -> the FLARE edit
     mode), which is what made the chip actively misleading rather than merely
     redundant, and that accelerator is suppressed while this dialog is up:
     measured, `f` with the manager closed paints "EDIT \u2192 FLARE" and `f`
     with the manager open paints no status at all.  `u` is bound to nothing
     anywhere.  Removing the chip removes decoration and no behaviour.

     The row does not lose its right edge with it.  The name's wrapper is the
     `flex: 1` item, so it -- not the chip -- is what holds the row open; the
     chip's 16.313px plus the 10px gap go to the name, which is the element
     that was being ellipsised.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

EDITS = [
    ('the commit gestures have somewhere to keep their state',
     'const [showImport, setShowImport] = usePM(false);\n'
     '  usePE(() => {\n'
     '    if (!open) return;\n',
     'const [showImport, setShowImport] = usePM(false);\n'
     '  // The manager is a browser, not a picker: a row click previews, and a\n'
     '  // second gesture commits. Both committing gestures -- the second click\n'
     '  // of a double-click and Return -- route through one owner below so they\n'
     '  // cannot drift apart, and so the footer\'s promise stays true.\n'
     '  const lastRowClickRef = usePR({ id: null, at: 0 });\n'
     '  const applyTargetRef = usePR(null);\n'
     '  usePE(() => {\n'
     '    if (!open) return;\n'
     '    // A click from a PREVIOUS opening is not the first half of a\n'
     '    // double-click in this one. Without this, dismissing and reopening\n'
     '    // fast enough makes the next click on the same row commit.\n'
     '    lastRowClickRef.current = { id: null, at: 0 };\n'),

    ('Return commits the selected preset',
     '      if (event.key !== "ArrowDown" && event.key !== "ArrowUp") return;\n',
     '      if (event.key === "Enter") {\n'
     '        // The same two guards the arrows use below: an open Pulp popup\n'
     '        // owns the keyboard, and a text field owns its own Return --\n'
     '        // the search and rename inputs both submit with it.\n'
     '        if (document.querySelector(\'[data-pulp-popup-active]\')) return;\n'
     '        if (editingText()) return;\n'
     '        // Read through the ref, not a closure: this listener is\n'
     '        // registered once per open, so a captured `selected` would\n'
     '        // commit whatever was selected at that moment -- usually\n'
     '        // nothing at all.\n'
     '        const commit = applyTargetRef.current;\n'
     '        if (typeof commit !== "function") return;\n'
     '        event.preventDefault();\n'
     '        event.stopPropagation();\n'
     '        commit();\n'
     '        return;\n'
     '      }\n'
     '      if (event.key !== "ArrowDown" && event.key !== "ArrowUp") return;\n'),

    ('one owner commits a preset, and both gestures reach it',
     'const selected = [...factory, ...userPatterns].find((p) => p.id === selectedId);\n'
     '  const saveCurrent = () => onRequestSave && onRequestSave();\n',
     'const selected = [...factory, ...userPatterns].find((p) => p.id === selectedId);\n'
     '  // The ONE place a preset is committed. `onApply` both applies the\n'
     '  // pattern and closes the manager, so every caller here gets the whole\n'
     '  // gesture rather than the half the user then has to finish by hand.\n'
     '  const applyPattern = (pattern) => {\n'
     '    if (!pattern) return;\n'
     '    lastRowClickRef.current = { id: null, at: 0 };\n'
     '    onApply(pattern);\n'
     '    onStatus && onStatus(`APPLIED "${pattern.name}"`);\n'
     '  };\n'
     '  // Republished every render so the keydown listener -- registered once\n'
     '  // per open -- always commits the CURRENT selection.\n'
     '  usePE(() => {\n'
     '    applyTargetRef.current = selected ? () => applyPattern(selected) : null;\n'
     '  });\n'
     '  // AppKit\'s own default double-click interval. Not tighter on purpose:\n'
     '  // a window below the platform\'s would reject genuine system\n'
     '  // double-clicks near the top of the range, and a gesture that works\n'
     '  // four times in five is worse than one that does not exist.\n'
     '  const DOUBLE_CLICK_MS = 500;\n'
     '  // A row click selects; a second click on the same row inside the\n'
     '  // window commits. Detected from the click stream rather than from\n'
     '  // React\'s `onDoubleClick`, which resolves to the bridge event name\n'
     '  // `doubleclick` -- a name nothing in the runtime ever dispatches, so\n'
     '  // that prop was a handler that could not run. Two clicks are what a\n'
     '  // real double-click delivers in both renderers.\n'
     '  const activateRow = (pattern) => {\n'
     '    const now = Date.now();\n'
     '    const last = lastRowClickRef.current;\n'
     '    const second = last.id === pattern.id\n'
     '      && now - last.at <= DOUBLE_CLICK_MS;\n'
     '    // A committed pair closes the window, so a third click starts a new\n'
     '    // pair instead of committing again.\n'
     '    lastRowClickRef.current = { id: pattern.id, at: second ? 0 : now };\n'
     '    setSelectedId(pattern.id);\n'
     '    if (second) applyPattern(pattern);\n'
     '  };\n'
     '  const saveCurrent = () => onRequestSave && onRequestSave();\n'),

    ('APPLY reads the shared owner',
     '      onApply: () => {\n'
     '        onApply(selected);\n'
     '        onStatus && onStatus(`APPLIED "${selected.name}"`);\n'
     '      },\n',
     '      onApply: () => applyPattern(selected),\n'),

    ('a factory row commits on its second click',
     'filteredFactory.map((p) => /* @__PURE__ */ React.createElement(\n'
     '    PatternRow,\n'
     '    {\n'
     '      key: p.id,\n'
     '      pattern: p,\n'
     '      selected: selectedId === p.id,\n'
     '      isDefault: defaultId === p.id,\n'
     '      onClick: () => setSelectedId(p.id),\n'
     '      onDblClick: () => {\n'
     '        onApply(p);\n'
     '      },\n'
     '      N\n'
     '    }\n'
     '  ))',
     'filteredFactory.map((p) => /* @__PURE__ */ React.createElement(\n'
     '    PatternRow,\n'
     '    {\n'
     '      key: p.id,\n'
     '      pattern: p,\n'
     '      selected: selectedId === p.id,\n'
     '      isDefault: defaultId === p.id,\n'
     '      onClick: () => activateRow(p),\n'
     '      N\n'
     '    }\n'
     '  ))'),

    ('a user row commits on its second click',
     'filteredUser.map((p) => /* @__PURE__ */ React.createElement(\n'
     '    PatternRow,\n'
     '    {\n'
     '      key: p.id,\n'
     '      pattern: p,\n'
     '      selected: selectedId === p.id,\n'
     '      isDefault: defaultId === p.id,\n'
     '      onClick: () => setSelectedId(p.id),\n'
     '      onDblClick: () => {\n'
     '        onApply(p);\n'
     '      },\n'
     '      N\n'
     '    }\n'
     '  ))',
     'filteredUser.map((p) => /* @__PURE__ */ React.createElement(\n'
     '    PatternRow,\n'
     '    {\n'
     '      key: p.id,\n'
     '      pattern: p,\n'
     '      selected: selectedId === p.id,\n'
     '      isDefault: defaultId === p.id,\n'
     '      onClick: () => activateRow(p),\n'
     '      N\n'
     '    }\n'
     '  ))'),

    ('a reopened manager starts unfiltered',
     "  }, [open, onClose, setSelectedId]);\n"
     "  const factory = window.Spectr.FACTORY_PATTERNS;",
     "  }, [open, onClose, setSelectedId]);\n"
     "  // Keyed on `open` ALONE, and that is the whole design.\n"
     "  //\n"
     "  // This component is never unmounted: App renders it unconditionally\n"
     "  // and `open` is a prop, so `if (!open) return null` tears the DOM\n"
     "  // down and leaves every usePM cell standing. A search therefore\n"
     "  // outlived the dialog -- reopening listed 0 of 8 presets under a\n"
     "  // header still reporting the whole inventory -- and nothing clears\n"
     "  // it: the panel\'s X is onClose, and the rail\'s CLEAR zeroes the\n"
     "  // band gains.\n"
     "  //\n"
     "  // It cannot share the keydown effect above, whose deps are\n"
     "  // `[open, onClose, setSelectedId]`: `onClose` is an inline arrow at\n"
     "  // the App call site, so it is a fresh identity on every App render\n"
     "  // and that effect re-runs constantly. Resetting there would empty\n"
     "  // the field while the user was still typing in it.\n"
     "  //\n"
     "  // `selectedId` is deliberately left alone: reopening on the preset\n"
     "  // you were looking at is useful, and is not what was reported.\n"
     "  usePE(() => {\n"
     "    if (!open) return;\n"
     "    setQuery(\"\");\n"
     "    setShowImport(false);\n"
     "    setImportText(\"\");\n"
     "  }, [open]);\n"
     "  const factory = window.Spectr.FACTORY_PATTERNS;"),

    ('the row drops the chip that reads as a keybinding',
     '),\n    /* @__PURE__ */ React.createElement("span", { style: {\n'
     '      fontSize: 8,\n'
     '      letterSpacing: 1.5,\n'
     '      opacity: 0.4,\n'
     '      padding: "1px 4px",\n'
     '      border: "1px solid rgba(255,255,255,0.12)",\n'
     '      borderRadius: 2\n'
     '    } }, pattern.source === "factory" ? "F" : "U")',
     ')\n'
     '    // No trailing chip. The row used to end in a small bordered "F" or\n'
     '    // "U" pill -- the same surface the editor gives REAL keyboard\n'
     '    // shortcuts -- which bound nothing and only repeated the FACTORY /\n'
     '    // USER heading the row already sits under.'),

    ('the row drops the prop that could never fire',
     'function PatternRow({ pattern, selected, isDefault, onClick, onDblClick, N }) {',
     'function PatternRow({ pattern, selected, isDefault, onClick, N }) {'),

    ('the row has one click owner',
     '      "data-spectr-pattern-default": isDefault ? "true" : "false",\n'
     '      onClick,\n'
     '      onDoubleClick: onDblClick,\n',
     '      "data-spectr-pattern-default": isDefault ? "true" : "false",\n'
     '      // One click owner. The second click of a double-click is detected\n'
     '      // from this same stream in PatternManager; there is no second\n'
     '      // handler here to drift away from it.\n'
     '      onClick,\n'),

    # The footer is the app's promise. It now names both commit gestures,
    # because both are real.
    # THE FOOTER CAPTION IS DELIBERATELY UNCHANGED, and that is a measured
    # decision rather than an omission. Advertising Return there was tried and
    # reverted: this row's sibling positions come from the capture and do not
    # reflow, so a longer caption has nowhere to go. Measured on the built app,
    # "DOUBLE-CLICK or \u23CE to apply" in place of "DOUBLE-CLICK to apply":
    #
    #   unstyled              box height 16 -> 31 (the text wrapped to a second
    #                         line) while the mid-dot stayed at x 420.703, the
    #                         star caption at 442.406 and both EXPORT buttons
    #                         did not move at all;
    #   whiteSpace: nowrap    one line again (h 14.9) but the box grew to
    #   + flexShrink: 0       w 149, running from x 285 to 434 straight through
    #                         that same unmoved mid-dot -- a 5.7 x 13.0 px
    #                         overlap that tools/spectr-detectors/
    #                         box_intersection.py reports and the pre-patch
    #                         dump does not.
    #
    # Return also matches the arrow keys, which have worked in this dialog for
    # some time and are not advertised in the footer either. So the caption
    # keeps its shipped wording and its shipped geometry, and the only promise
    # it makes -- DOUBLE-CLICK -- is now true.
]

# The two edits added after the commit-gesture work shipped. A document that
# already carries the gestures and not these is NOT "half patched" -- it is
# every checkout that predates them -- so the half-patch guard below has to
# exempt exactly this set, and nothing else.
LATER_EDITS = frozenset({
    'a reopened manager starts unfiltered',
    'the row drops the chip that reads as a keybinding',
})

# A surviving dead prop is exactly the shape this patch exists to remove, and a
# surviving second copy of the commit body is how one gesture grows two owners.
FORBIDDEN_AFTER = (
    # The chip that read as a keybinding and bound nothing.
    'pattern.source === "factory" ? "F" : "U"',
    'onDblClick',
    # The PROP, not the word: the comment that explains why this prop could
    # never fire deliberately names it.
    'onDoubleClick:',
    '        onApply(selected);\n        onStatus && onStatus(`APPLIED "${selected.name}"`);',
    'onClick: () => setSelectedId(p.id),',
)
REQUIRED_AFTER = (
    # The per-opening reset, and -- load-bearing -- its dep list. Keyed on
    # anything that changes per App render it would clear the field mid-typing.
    'setQuery("");',
    'setShowImport(false);',
    'setImportText("");',
    '  }, [open]);',
    'const lastRowClickRef = usePR({ id: null, at: 0 });',
    'const applyTargetRef = usePR(null);',
    '    // A click from a PREVIOUS opening is not the first half of a',
    'if (event.key === "Enter") {',
    'const commit = applyTargetRef.current;',
    'const applyPattern = (pattern) => {',
    'const DOUBLE_CLICK_MS = 500;',
    'const activateRow = (pattern) => {',
    'onApply: () => applyPattern(selected),',
    'function PatternRow({ pattern, selected, isDefault, onClick, N }) {',
    '      // One click owner. The second click of a double-click is detected',
)
# `activateRow` must reach BOTH lists; one converted call site and one missed
# would leave user presets un-committable and look fine in every screenshot.
REQUIRED_COUNTS = (('onClick: () => activateRow(p),', 2),)


def escaped(value):
    return json.dumps(value)[1:-1]


def main():
    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    applied_labels = set()
    for label, old, new in EDITS:
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(new_e) >= 1:
            print('already applied ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        raw = raw.replace(old_e, new_e)
        changed = True
        applied += 1
        applied_labels.add(label)
        print('applied         ', label)

    if already and applied and not applied_labels <= LATER_EDITS:
        sys.exit('FAIL: the document is half patched; refusing to write')

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))
    for token, expected in REQUIRED_COUNTS:
        count = raw.count(escaped(token))
        if count != expected:
            sys.exit('FAIL: %r appears %d times after patching, expected %d'
                     % (token, count, expected))

    document = json.loads(raw)
    if not isinstance(document.get('html'), str):
        sys.exit('FAIL: the patched document no longer carries an html payload')

    if not changed:
        print('no change; document already carries the commit gestures')
        return 0
    open(PATH, 'w', encoding='utf-8').write(raw)
    print('wrote', PATH)
    return 0


if __name__ == '__main__':
    sys.exit(main())
