#!/usr/bin/env python3
"""Test whether Pulp OWNS a popup, not whether it has PAINTED a cursor in it.

Three of this document's keydown listeners ask "is a Pulp dropdown open?" and
all three asked it with `[data-pulp-popup-active="true"]`. That selector does
not answer that question. It answers a narrower one -- "is the keyboard cursor
currently PAINTED on a row" -- and the two stopped being the same thing.

Pulp's popup owner writes `data-pulp-popup-active` on EVERY option of a popup
it owns, `"true"` on the cursor row and `"false"` on the rest, and REMOVES the
attribute from every row when it lets the popup go. So:

    [data-pulp-popup-active]          -> Pulp owns a popup   (presence)
    [data-pulp-popup-active="true"]   -> the cursor is drawn  (value)

Those coincided while the owner painted its cursor the moment a popup opened.
It no longer does: a popup opened with the POINTER keeps its cursor unpainted
until the user asks for one -- an arrow key, a row hover, or an arrow that
opened the menu -- because an app that paints its own selected row plus a
framework cursor on a different row reads as two selections. The cursor still
EXISTS from the moment the popup is owned; it is simply invisible, and a
value-matching selector cannot see it.

Measured on the built standalone with the pattern dropdown opened by pointer:
`[data-pulp-popup-active]` matched 10 rows, `[data-pulp-popup-active="true"]`
matched 0, and every row read back `"false"`.

WHAT THAT COST. `SettingsModal` registers its Escape listener on `document` in
the CAPTURE phase, unconditionally, for the whole session. With the guard
missing an open-but-unrevealed dropdown, that listener answered Escape itself:
`preventDefault()` + `stopPropagation()` + `onClose()`. `document.dispatchEvent`
only offers an event to the popup owner while `!event.defaultPrevented`, so the
owner -- which does have working Escape handling -- was never offered the key
at all. An open dropdown therefore could not be dismissed with Escape, and the
press leaked to Settings instead.

Proven on the built standalone, all three readings from one run: dispatching
Escape reported `offered=false defaultPrevented=true` and left the menu up
(`containers=1 state=present`), dispatching ArrowDown reported `offered=true`
and moved the cursor, and calling `__pulpPopupDefaultHandle__` directly with
the same Escape closed it (`state=null containers=0`).

The preset manager's two guards are the same selector making the same mistake
on the same popup, so both move with it. They are not known to have misfired
in the same visible way -- they DECLINE to act when a popup is open, so the
stale reading made them consume a key the popup owner also wanted rather than
eat one it needed -- but a guard that is wrong about ownership is wrong in both
directions and there is no reason to leave two of them behind.

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


OWNED = "'[data-pulp-popup-active]'"
PAINTED = "'[data-pulp-popup-active=\"true\"]'"

# Selector-only replacements, and deliberately so. The same swap is made in the
# two generator scripts that write these guards into a freshly materialized
# document (`patch_materialized_editor.py`,
# `patch_materialized_preset_apply_gestures.py`), so the generator chain and
# this migration converge on byte-identical text. Adding commentary here that
# the generators do not also emit would make one path stop recognising the
# other's output as already applied.
#
# Each `find` carries the comment block that already sits above its guard, so
# the three are distinguishable at one occurrence apiece; the bare selector
# appears three times and identifies nothing on its own. Those comments are
# preserved verbatim.
PRESET_RETURN_OLD = (
    '        // the search and rename inputs both submit with it.\n'
    '        if (document.querySelector(' + PAINTED + ')) return;\n')
PRESET_RETURN_NEW = (
    '        // the search and rename inputs both submit with it.\n'
    '        if (document.querySelector(' + OWNED + ')) return;\n')

PRESET_ARROWS_OLD = (
    '      // same way a second menu keyboard owner once did.\n'
    '      if (document.querySelector(' + PAINTED + ')) return;\n')
PRESET_ARROWS_NEW = (
    '      // same way a second menu keyboard owner once did.\n'
    '      if (document.querySelector(' + OWNED + ')) return;\n')

SETTINGS_OLD = (
    'if (event.key === "Escape" && !document.querySelector(' + PAINTED + ')) {')
SETTINGS_NEW = (
    'if (event.key === "Escape" && !document.querySelector(' + OWNED + ')) {')

EDITS = (
    ("preset manager: Return defers to an owned popup",
     (PRESET_RETURN_OLD, PRESET_RETURN_NEW), PRESET_RETURN_NEW),
    ("preset manager: arrows defer to an owned popup",
     (PRESET_ARROWS_OLD, PRESET_ARROWS_NEW), PRESET_ARROWS_NEW),
    ("settings Escape defers to an owned popup",
     (SETTINGS_OLD, SETTINGS_NEW), SETTINGS_NEW),
)

REQUIRED_AFTER = (
    'if (event.key === "Escape" && !document.querySelector(' + OWNED + ')) {',
    'document.addEventListener("keydown", onKey, true);',
)


def main():
    raw = open(PATH, encoding="utf-8").read()
    before = len(raw)

    # CONTROL, read before anything is written. Each edit is anchored inside a
    # named component; a document missing one of them is a document this
    # script must refuse rather than no-op into "already applied".
    anchors = {
        "SettingsModal": "function SettingsModal({ settings, setSettings, onClose",
        "preset manager arrows": 'if (event.key !== "ArrowDown" && event.key !== "ArrowUp") return;',
        "preset manager Return": 'const commit = applyTargetRef.current;',
    }
    control = {name: raw.count(enc(text)) for name, text in anchors.items()}
    print("control: " + ", ".join("%s=%d" % kv for kv in sorted(control.items())))
    if sorted(control.values()) != [1, 1, 1]:
        print("FAIL: expected each anchor exactly once -- wrong document",
              file=sys.stderr)
        return 1

    applied, already = [], []
    for label, (find, replace), done in EDITS:
        if raw.count(enc(done)) >= 1:
            already.append(label)
            continue
        found = raw.count(enc(find))
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

    # The whole point of the change: no ownership question is still asked with
    # the painted-cursor selector. This document has no other use for it.
    survivors = raw.count(enc(PAINTED))
    if survivors != 0:
        print("FAIL: the painted-cursor selector still appears %d time(s); an "
              "ownership guard is still keyed on it" % survivors,
              file=sys.stderr)
        return 1
    owners = raw.count(enc("document.querySelector(" + OWNED + ")"))
    if owners != 3:
        print("FAIL: %d ownership guard(s) after patching, expected 3"
              % owners, file=sys.stderr)
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
    # This edit only rewrites comment text and a selector string inside two
    # event handlers, so it must not move a single binding, and it must not
    # create or remove an element.
    counts = {key: len(document.get(key) or [])
              for key in ("text_bindings", "layout_bindings", "paint_bindings",
                          "canvas_bindings", "semantic_bindings")}
    if counts != {"text_bindings": 24, "layout_bindings": 81,
                  "paint_bindings": 17, "canvas_bindings": 2,
                  "semantic_bindings": 18}:
        print("FAIL: binding counts moved: %r" % (counts,), file=sys.stderr)
        return 1
    elements = document["html"].count("createElement")
    if elements != 502:
        print("FAIL: createElement count moved to %d, expected 502"
              % elements, file=sys.stderr)
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
