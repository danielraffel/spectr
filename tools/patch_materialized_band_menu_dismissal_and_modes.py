#!/usr/bin/env python3
"""The band context menu loses its EDIT MODE rows, and owns its own dismissal.

Four defects were reported together against the standalone build. Three of
them are this document's; the fourth (the menu painting UNDER the OUTPUT
slider and the stats overlay) is Pulp's overlay z-order and is deliberately
untouched here.

DEFECT 1 -- THE EDIT MODE SECTION IS OFF THE MENU. The band menu carried an
`EDIT MODE` divider plus Sculpt / Level / Boost / Flare / Glide, duplicating
controls that already have two owners: the S/L/B/F/G shortcuts in the App's
global keydown handler and the bottom-bar SCULPT control. Neither of those is
touched -- `modeKeys` and `setEditMode` live in the App, not here, so removing
the rows cannot reach them. The `editMode` and `onEditMode` props stay in the
component signature: `StableContextMenu`'s memo comparator reads `editMode`,
and a signature change is a wider blast radius than the rows are worth.

`test/test_materialized_context_menu.mjs` has asserted this removal since it
was written and has been RED the whole time, because nothing in CMakeLists
registered it. A test nobody runs is not a contract; that registration is part
of this change.

DEFECT 2 -- ESCAPE AND THE OUTSIDE PRESS DID NOT DISMISS. The menu had
delegated both to the framework (`overlay: true` + `onDismiss`), and the
framework never got the chance. Both claim arms in the materialized runtime --
the `overlay` prop case and the `role="menu"` auto-claim -- call
`releaseOverlay` instead of `claimOverlay` when `getLayoutBoxMetrics` reports
the node at 0x0, which is what a freshly mounted panel measures before its
first layout pass. Neither prop changes again afterwards, so `applyChangedProps`
never revisits the decision and the claim is never retried. The declaration is
KEPT -- it costs nothing and it is what the sibling overlays in this document
say -- but it can no longer be the only path.

WHERE EACH LISTENER HAS TO LIVE. This is measured against the DOM shim, not
assumed, because the previous hand-rolled attempt put BOTH on `document` and
the outside-press half could never have fired:

  * a keydown reaches JS only through `__dispatch__('document','keydown',e)`,
    which calls `document.dispatchEvent` directly. Escape is therefore a
    DOCUMENT listener.
  * a pointer press reaches JS on the pressed ELEMENT and bubbles the
    `_parentElement` chain (`_dispatchEvent` in web-compat.js). That chain
    ends at `document.body`; the `document` object is not an Element and is
    never on it. The outside press is therefore a BODY listener. A document
    one sees nothing -- which is exactly what was reported.
  * the body listener is registered in the CAPTURE phase, which runs
    top-down before the target, so the spectrum surface's own `onPointerDown`
    cannot consume the press first.

Escape delivery is safe in this host specifically: the standalone's
`keyDown:` calls `script_events::dispatch_global_key` BEFORE
`route_escape_to_active_overlay`, and `performKeyEquivalent:` fans Command
chords out the same way, so a JS listener always sees the key whatever the
native overlay slot does with it afterwards.

THE SUBMENU COUNTS AS INSIDE. `macrosPanel` and `modulationPanel` are siblings
of the menu in the returned fragment, not descendants of its box, so a
containment test against the menu alone would dismiss the whole menu the
moment a submenu row was pressed. All three panels are tested.

DEFECT 3 -- AN EXPANDED SUBMENU COULD NOT BE LEFT. Three separate things:

  * Escape now retires ONE layer per press -- an open submenu first, the menu
    itself second -- instead of having no effect at all.
  * `< Back` clears BOTH submenu flags rather than only its own. It was wired
    and it does fire (`test_context_modulation_behavior.mjs` exercises
    `modulation-back` and the parent panel comes back), so this is not a
    rewiring: it is removing the one state a single-flag clear cannot leave,
    which is both panels open at once.
  * opening a submenu now closes the other one. `onHover`/`onClick`/ArrowRight
    on the two `>` rows each set only their own flag, so hovering Modulation
    with Macros already open left two fixed panels stacked at the same
    coordinates with the later one painting over the earlier.

NOT IN SCOPE, and deliberately: the menu painting under the OUTPUT slider and
the stats overlay. That is the overlay z-order in core Pulp; the standing rule
is to fix it there and inherit.

A NOTE ON THE ANCHORS. This document's escaping is not uniform, and the two
`< Back` rows prove it: the macros one stores its guillemet as the JS escape
`\\u2039` while the modulation one stores the raw UTF-8 character, inside the
same JSON string. `enc()` produces the ASCII-escaped form, so an anchor
carrying that character matches one row and silently misses the other. Both
Back anchors are therefore ASCII-only tails. This is the concrete reason the
house rule says raw-text surgery and never a JSON load/dump round trip.

NOTE: the document is compiled into the binary by `pulp_add_binary_data`
(CMakeLists.txt `spectr_native_assets`), so a rebuild is REQUIRED before any
native test reflects this patch. `Encoding binary asset
materialized-document.runtime.json` in the build log is the proof; "Built
target" is not.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")


def enc(snippet):
    """The document stores the page as a JSON string, so every needle is
    escaped the way the file stores it. Raw-text surgery, never a load/dump
    round trip: this file's escaping is not uniform, so re-serialising would
    rewrite bytes that have nothing to do with this change."""
    return json.dumps(snippet)[1:-1]


# -- 1. the EDIT MODE rows, and the table that fed only them ---------------

MODES_CONST = (
    '  const modes = [\n'
    '    { k: "sculpt", label: "Sculpt", hint: "S" },\n'
    '    { k: "level", label: "Level", hint: "L" },\n'
    '    { k: "boost", label: "Boost", hint: "B" },\n'
    '    { k: "flare", label: "Flare", hint: "F" },\n'
    '    { k: "glide", label: "Glide", hint: "G" }\n'
    '  ];\n'
)
MODES_CONST_NEW = (
    '  // The edit modes are NOT a band-menu concern. They are reachable from\n'
    '  // the S/L/B/F/G shortcuts the App owns and from the bottom-bar SCULPT\n'
    '  // control; listing them here made a third surface for the same five\n'
    '  // values and put five rows in front of every right-click that wanted\n'
    '  // one of the band actions. `editMode` and `onEditMode` stay in the\n'
    '  // signature: the memo comparator reads the former.\n'
)

EDITMODE_ROWS = (
    '    /* @__PURE__ */ React.createElement(Divider, { label: "EDIT MODE" }),\n'
    '    modes.map((m) => /* @__PURE__ */ React.createElement(\n'
    '      Item,\n'
    '      {\n'
    '        key: m.k,\n'
    '        label: (editMode === m.k ? "\\u25CF " : "   ") + m.label,\n'
    '        hint: m.hint,\n'
    '        onClick: () => onEditMode(m.k)\n'
    '      }\n'
    '    )),\n'
)
EDITMODE_ROWS_NEW = ''


# -- 2. one submenu at a time, and a Back that cannot leave both open ------

MACROS_STATE = '  const [macrosOpen, setMacrosOpen] = React.useState(false);\n'
SUBMENU_HELPERS = (
    '  // Exactly one submenu is open at a time. Both panels are `position:\n'
    '  // fixed` at the same computed coordinates, so two open at once is not\n'
    '  // two menus side by side -- it is one painting over the other, with\n'
    '  // the covered one still hit-testable underneath.\n'
    '  const closeSubmenus = () => {\n'
    '    setMacrosOpen(false);\n'
    '    setModulationOpen(false);\n'
    '  };\n'
    '  const openModulation = (open) => {\n'
    '    setModulationOpen(open);\n'
    '    if (open) setMacrosOpen(false);\n'
    '  };\n'
    '  const openMacros = (open) => {\n'
    '    setMacrosOpen(open);\n'
    '    if (open) setModulationOpen(false);\n'
    '  };\n'
    '  // The two TOGGLES keep the functional updater they always had, and\n'
    '  // only gain the sibling close. A toggle written as `open(!macrosOpen)`\n'
    '  // reads the flag from the render that built the handler, and the row\n'
    '  // above it has an `onHover` that opens the same panel -- so a press\n'
    '  // that arrives after its own hover toggles from a value that is\n'
    '  // already stale and closes the panel it just opened. Measured on the\n'
    '  // shipping standalone: the first `Macros` press after a fresh open\n'
    '  // stopped opening the panel at all, and every macro row behind it\n'
    '  // became unreachable for the rest of the session.\n'
    '  const toggleMacros = () => {\n'
    '    setMacrosOpen(open => !open);\n'
    '    setModulationOpen(false);\n'
    '  };\n'
    '  const toggleModulation = () => {\n'
    '    setModulationOpen(open => !open);\n'
    '    setMacrosOpen(false);\n'
    '  };\n'
)

MOD_TOGGLE = (
    '        onHover: () => setModulationOpen(true),\n'
    '        onKeyDown: (event) => {\n'
    '          if (event.key === "ArrowRight") {\n'
    '            event.preventDefault();\n'
    '            setModulationOpen(true);\n'
    '          }\n'
    '        },\n'
    '        onClick: () => setModulationOpen(open => !open)\n'
)
MOD_TOGGLE_NEW = (
    '        onHover: () => openModulation(true),\n'
    '        onKeyDown: (event) => {\n'
    '          if (event.key === "ArrowRight") {\n'
    '            event.preventDefault();\n'
    '            openModulation(true);\n'
    '          }\n'
    '        },\n'
    '        onClick: toggleModulation\n'
)

MACROS_TOGGLE = (
    '        onHover: () => setMacrosOpen(true),\n'
    '        onKeyDown: (event) => {\n'
    '          if (event.key === "ArrowRight") {\n'
    '            event.preventDefault();\n'
    '            setMacrosOpen(true);\n'
    '          }\n'
    '        },\n'
    '        onClick: () => setMacrosOpen(open => !open)\n'
)
MACROS_TOGGLE_NEW = (
    '        onHover: () => openMacros(true),\n'
    '        onKeyDown: (event) => {\n'
    '          if (event.key === "ArrowRight") {\n'
    '            event.preventDefault();\n'
    '            openMacros(true);\n'
    '          }\n'
    '        },\n'
    '        onClick: toggleMacros\n'
)

MACROS_BACK = (
    ' Back", keepOpen: true, onClick: () => setMacrosOpen(false) }),\n'
)
MACROS_BACK_NEW = (
    ' Back", keepOpen: true, onClick: closeSubmenus }),\n'
)

MOD_BACK = (
    ' Back", keepOpen: true, onClick: () => setModulationOpen(false) }),\n'
)
MOD_BACK_NEW = (
    ' Back", keepOpen: true, onClick: closeSubmenus }),\n'
)


# -- 3. the menu's own Escape and outside-press dismissal ------------------
#
# Anchored after `modulationTop`, which is the last statement before the row
# tables: `ref`, `macrosRef` and `modulationRef` are all declared above it, so
# the effect can read every panel it has to treat as inside.

MOD_TOP = '  const modulationTop = submenuTopFor(modulationH, 420);\n'
DISMISSAL = (
    '  // Escape and the outside press, owned by the menu rather than\n'
    '  // delegated. The framework declaration below (`overlay` + `onDismiss`)\n'
    '  // is kept and is still the first thing that answers when it works --\n'
    '  // but both claim arms in the materialized runtime call\n'
    '  // `releaseOverlay` when the node still measures 0x0, which is what a\n'
    '  // freshly mounted panel measures, and neither prop changes again so\n'
    '  // the claim is never retried.\n'
    '  //\n'
    '  // The two listeners live on DIFFERENT objects, measured rather than\n'
    '  // assumed. A keydown reaches JS only via\n'
    '  // `__dispatch__(\'document\',\'keydown\')`, which calls\n'
    '  // `document.dispatchEvent` -- so Escape is a document listener. A\n'
    '  // pointer press reaches JS on the pressed element and bubbles the\n'
    '  // `_parentElement` chain, which ends at `document.body` and never\n'
    '  // reaches `document` -- so the outside press is a BODY listener, in\n'
    '  // the capture phase so the spectrum surface below cannot consume it\n'
    '  // first. The previous attempt put both on `document`, which is why the\n'
    '  // outside half never fired once.\n'
    '  React.useEffect(() => {\n'
    '    // Answers three ways, and the third is the important one. If no\n'
    '    // panel ref can answer at all -- the tree not linked to the shim\n'
    '    // yet, a ref not attached -- then inside and outside are\n'
    '    // INDISTINGUISHABLE, and the safe reading is "inside": a menu that\n'
    '    // fails to dismiss is a nuisance, while one that dismisses on every\n'
    '    // press eats its own rows and cannot be used at all.\n'
    '    const inside = (target) => {\n'
    '      if (!target) return true;\n'
    '      const panels = [ref.current, macrosRef.current, modulationRef.current];\n'
    '      let answered = false;\n'
    '      for (const panel of panels) {\n'
    '        if (!panel || typeof panel.contains !== "function") continue;\n'
    '        answered = true;\n'
    '        if (panel.contains(target)) return true;\n'
    '      }\n'
    '      if (typeof target.closest === "function") {\n'
    '        answered = true;\n'
    '        if (target.closest("[data-spectr-band-context-menu]")\n'
    '            || target.closest("[data-spectr-macros-panel]")\n'
    '            || target.closest("[data-spectr-modulation-panel]"))\n'
    '          return true;\n'
    '      }\n'
    '      return !answered;\n'
    '    };\n'
    '    const onEscapeKey = (event) => {\n'
    '      if (!event || event.key !== "Escape") return;\n'
    '      if (typeof event.preventDefault === "function") event.preventDefault();\n'
    '      // One press retires one layer, so a user inside a submenu gets\n'
    '      // back to the menu rather than losing both at once.\n'
    '      if (macrosOpen || modulationOpen) {\n'
    '        setMacrosOpen(false);\n'
    '        setModulationOpen(false);\n'
    '        return;\n'
    '      }\n'
    '      onClose();\n'
    '    };\n'
    '    const onOutsidePress = (event) => {\n'
    '      if (inside(event && event.target)) return;\n'
    '      onClose();\n'
    '    };\n'
    '    const doc = typeof document !== "undefined" ? document : null;\n'
    '    const body = doc && doc.body ? doc.body : null;\n'
    '    if (doc && typeof doc.addEventListener === "function")\n'
    '      doc.addEventListener("keydown", onEscapeKey, true);\n'
    '    if (body && typeof body.addEventListener === "function")\n'
    '      body.addEventListener("pointerdown", onOutsidePress, true);\n'
    '    return () => {\n'
    '      if (doc && typeof doc.removeEventListener === "function")\n'
    '        doc.removeEventListener("keydown", onEscapeKey, true);\n'
    '      if (body && typeof body.removeEventListener === "function")\n'
    '        body.removeEventListener("pointerdown", onOutsidePress, true);\n'
    '    };\n'
    '  }, [onClose, macrosOpen, modulationOpen]);\n'
)


EDITS = [
    ('the edit-mode table, which fed only the rows below',
     (MODES_CONST, MODES_CONST_NEW),
     'The edit modes are NOT a band-menu concern.'),

    ('the EDIT MODE divider and its five rows',
     (EDITMODE_ROWS, EDITMODE_ROWS_NEW),
     None),                       # a pure removal: absence of `find` is done

    ('one submenu open at a time',
     (MACROS_STATE, MACROS_STATE + SUBMENU_HELPERS),
     'const openModulation = (open) => {'),

    ('the Modulation row routes through it',
     (MOD_TOGGLE, MOD_TOGGLE_NEW),
     'onClick: toggleModulation'),

    ('the Macros row routes through it',
     (MACROS_TOGGLE, MACROS_TOGGLE_NEW),
     'onClick: toggleMacros'),

    # Both Back rows reduce to the SAME replacement tail, so neither can
    # carry a `done` marker of its own: applying the first would make the
    # second read as already applied. Their own `find` disappearing is the
    # marker instead (`done is None`), and REQUIRED_COUNTS below asserts the
    # shared tail appears exactly twice so one-of-two cannot pass quietly.
    ('macros Back cannot leave the other panel open',
     (MACROS_BACK, MACROS_BACK_NEW),
     None),

    ('modulation Back cannot leave the other panel open',
     (MOD_BACK, MOD_BACK_NEW),
     None),

    ('the menu owns its Escape and its outside press',
     (MOD_TOP, MOD_TOP + DISMISSAL),
     'return !answered;'),
]

# Asserted present after every run. The first four are this change; the last
# three are the surfaces it depends on and must not have moved -- a document
# where the framework declaration, the two submenu panels or the shortcut
# table had been restructured is one where the reasoning above no longer holds.
REQUIRED_AFTER = (
    'const closeSubmenus = () => {',
    'doc.addEventListener("keydown", onEscapeKey, true);',
    'body.addEventListener("pointerdown", onOutsidePress, true);',
    'return !answered;',
    'if (macrosOpen || modulationOpen) {',
    'onDismiss: onClose,',
    '"data-spectr-macros-panel": true,',
    '"data-spectr-modulation-panel": true,',
)

# Tokens whose COUNT is the contract, not their presence. Both Back rows
# reduce to the same replacement, so "it is there" cannot tell one applied
# from two.
REQUIRED_COUNTS = (
    (' Back", keepOpen: true, onClick: closeSubmenus }),', 2),
    ('data-spectr-band-action', 1),
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. Every edit is anchored inside
    # ContextMenu; a document without its component, its two submenu toggles
    # and its framework overlay declaration is one this script must refuse
    # rather than no-op into "already applied".
    anchors = {
        "ContextMenu": "function ContextMenu({ x, y, band, N, selection",
        "macros toggle": 'action: "macros-toggle"',
        "modulation toggle": 'action: "modulation-toggle"',
        # `onDismiss: onClose,` is shared by nine overlays in this
        # document, so it identifies nothing. The band menu's own marker is
        # the attribute the App's shortcut guard also keys on.
        "band menu root": '"data-spectr-band-context-menu": "true",',
    }
    control = {name: raw.count(enc(text)) for name, text in anchors.items()}
    print("control: " + ", ".join("%s=%d" % kv for kv in sorted(control.items())))
    if sorted(control.values()) != [1, 1, 1, 1]:
        print("FAIL: expected each anchor exactly once -- wrong document",
              file=sys.stderr)
        return 1

    applied, already = [], []
    for label, (find, replace), done in EDITS:
        if done is not None and raw.count(enc(done)) >= 1:
            already.append(label)
            continue
        found = raw.count(enc(find))
        if done is None and found == 0:
            # A pure removal has no marker of its own: its own absence IS the
            # marker. Distinguishing that from a missing anchor is what the
            # control above is for.
            already.append(label)
            continue
        if found != 1:
            print("FAIL: %r matched %d times, expected exactly 1"
                  % (label, found), file=sys.stderr)
            return 1
        raw = raw.replace(enc(find), enc(replace), 1)
        applied.append(label)

    for token in REQUIRED_AFTER:
        if raw.count(enc(token)) == 0:
            print("FAIL: %r is absent after patching" % (token,), file=sys.stderr)
            return 1

    for token, want in REQUIRED_COUNTS:
        got = raw.count(enc(token))
        if got != want:
            print("FAIL: %r appears %d times after patching, expected %d"
                  % (token, got, want), file=sys.stderr)
            return 1

    # The rows are gone, and nothing still reaches for what fed them.
    for token in ('label: "EDIT MODE"', "modes.map", "onEditMode("):
        if raw.count(enc(token)) != 0:
            print("FAIL: %r survived the patch" % (token,), file=sys.stderr)
            return 1

    # Parse to adjudicate, never to write: a broken payload here is an editor
    # that does not load at all, and the artifact is one logical line so a
    # human diff will not catch it.
    document = json.loads(raw)
    if not isinstance(document.get("html"), str):
        print("FAIL: the patched document no longer carries an html payload",
              file=sys.stderr)
        return 1
    # The bindings address the STATIC materialized tree by positional path.
    # The menu is built by React at runtime and is not in that tree, so this
    # change must not move a single binding. Counted here so a future edit
    # that does move one cannot pass quietly.
    counts = {key: len(document.get(key) or [])
              for key in ("text_bindings", "layout_bindings", "paint_bindings")}
    if counts != {"text_bindings": 24, "layout_bindings": 81,
                  "paint_bindings": 17}:
        print("FAIL: binding counts moved: %r" % (counts,), file=sys.stderr)
        return 1

    if not applied:
        print("already applied: %d edit(s), nothing written" % len(already))
        return 0

    open(PATH, "w", encoding="utf-8").write(raw)
    print("applied %d edit(s), %d already present; %d -> %d bytes"
          % (len(applied), len(already), before, len(raw)))
    for label in applied:
        print("  + " + label)
    return 0


if __name__ == "__main__":
    sys.exit(main())
