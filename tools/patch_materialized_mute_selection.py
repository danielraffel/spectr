#!/usr/bin/env python3
"""Mute or unmute a whole band selection from the keyboard, with `m`.

WHY THIS IS NOT ALREADY REACHABLE, which is the first thing worth settling.
The band context menu wires `onMuteSel`, so "group mute exists, it was just
unreachable because right-click never worked" is the obvious read. It is not
what the code says:

    onMuteSel: () => {
      const map = new Map();
      for (const i of selection) map.set(i, -Infinity);
      commitMany(map);
    },

That is an UNCONDITIONAL MUTE. There is no group unmute anywhere in the
document -- `onZeroSel` flattens the selection to 0 dB, which unmutes by side
effect and throws every band's level away, and the whole-bank `unmuteAll`
restores to a flat 0 rather than to what each band was. So the toggle this
implements is a capability the product does not have by any route, menu or
otherwise, rather than a faster path to one it does.

(Right-click is separately dead on the pinned SDK: the fix for it, pulp#8307,
is not an ancestor of v0.850.0's `source_git_sha`. So today the menu item
cannot be reached at all. That is a reason the shortcut helps sooner, not the
reason it is worth having.)

THE MIXED-STATE RULE. If ANY selected band is unmuted, mute them all;
otherwise unmute them all. One keypress always has an obvious result and a
second always reverses it. Toggling each band independently would leave a
mixed selection mixed, so the user could not tell what the key did.

NOT THE TAP-ONE VARIANT. Tapping one selected band to mute the whole selection
would silently change what a plain CLICK does whenever a selection happens to
be live -- `CLICK` already toggles mute on a single band, there is no visual
cue at click time, and the user may have forgotten the selection exists. The
keyboard has no such collision.

UNMUTE RESTORES, IT DOES NOT FLATTEN. `mutedGainDbRef` is the level stashed at
mute time; the SHIFT+DRAG mute brush already restores from it, and a group
unmute that returned every band to 0 dB would destroy exactly the state the
user muted in order to audition. Mute is the discrete `-Infinity` sentinel,
not a low value, so "is it muted" is `isMuted(...)` and never a threshold.

THE STALE-CLOSURE TRAP, and it is the reason this is a bank method rather than
three lines in the handler. `selection` is React STATE held in `FilterBank`,
while the global keydown handler lives in `App` -- a different subtree, with no
access to it. The bank object is the seam between them, and it is rebuilt by an
effect whose dependency list is:

    }, [N, snapshots, view, onNativeState]);

`selection` is NOT in it. A bank method closing over `selection` therefore
reads the empty Set the state was created with, FOREVER: the shortcut would do
nothing, silently, in exactly the case it exists for, and would look correct in
review. So the selection is read through a ref mirror kept in sync by its own
effect.

The keyboard guard is inherited rather than rebuilt: the branch sits inside
`onKey`, below the compound guard that already rejects every modifier, IME
composition, key autorepeat, a focused text field, and any open overlay. `m` is
unbound today -- it collides with none of the edit-mode letters (S/L/B/F/G),
neither analyzer key (A/6), and no digit.

WHAT THIS FILE DOES NOT OWN. The SHORTCUTS panel row that advertises the key is
`tools/patch_materialized_selection_shortcuts.py`'s: that script owns the
panel's row list and re-derives the captured geometry from it, and the panel is
laid out FROM ITS CAPTURE rather than live, so a row added here would render at
a box the capture never described. One owner, one patch point.

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


def predecessors(old):
    return (old,) if isinstance(old, str) else tuple(old)


def choose(old, raw):
    """The most advanced predecessor present, or a reason there is none."""
    for cand in reversed(predecessors(old)):
        count = raw.count(enc(cand))
        if count == 1:
            return cand, None
        if count > 1:
            return None, ('patch point %r occurs %d times, expected 1'
                          % (cand, count))
    return None, 'no known predecessor text is present'


SELECTION_DECL = (
    "  const [selection, setSelection] = useState(() => "
    "/* @__PURE__ */ new Set());\n")

SELECTION_DECL_NEW = (
    SELECTION_DECL +
    "  // The bank object is rebuilt on [N, snapshots, view, onNativeState] and\n"
    "  // NOT on selection, so a bank method closing over `selection` would read\n"
    "  // the empty Set this state was created with, forever. The keyboard path\n"
    "  // has to go through the bank -- the global handler lives in App, another\n"
    "  // subtree -- so it reads the selection through this mirror instead.\n"
    "  const selectionRef = useRef(selection);\n"
    "  useEffect(() => { selectionRef.current = selection; }, [selection]);\n")

ALL_MUTED = ("      allMuted: () => targetGainsRef.current.every((v) => "
             "isMuted(v)),\n")

TOGGLE_METHOD = (
    ALL_MUTED +
    "      // Mute or unmute the whole selection, as one action with one\n"
    "      // obvious result. If ANY selected band is unmuted, mute them all;\n"
    "      // otherwise unmute them all -- so a second press always reverses\n"
    "      // the first, and a mixed selection never stays mixed.\n"
    "      //\n"
    "      // Unmute RESTORES rather than flattens: mutedGainDbRef holds the\n"
    "      // level stashed at mute time, the same source the SHIFT+DRAG mute\n"
    "      // brush restores from. Returning every band to 0 dB would destroy\n"
    "      // the state the user muted in order to audition.\n"
    "      //\n"
    "      // Returns null when nothing is selected, so the caller can say so\n"
    "      // rather than reporting a silent success.\n"
    "      toggleMuteSelection: () => {\n"
    "        const sel = selectionRef.current;\n"
    "        if (!sel || sel.size === 0) return null;\n"
    "        const bands = [...sel].filter((i) => i >= 0 && i < N);\n"
    "        if (bands.length === 0) return null;\n"
    "        const mute = bands.some((i) => !isMuted(targetGainsRef.current[i]));\n"
    "        const map = /* @__PURE__ */ new Map();\n"
    "        for (const i of bands) {\n"
    "          if (mute) { map.set(i, -Infinity); continue; }\n"
    "          const db = mutedGainDbRef.current[i];\n"
    "          map.set(i, Number.isFinite(db) ? clamp(db / 24, -1, 1) : 0);\n"
    "        }\n"
    "        commitMany(map);\n"
    "        return { count: bands.length, muted: mute };\n"
    "      },\n")

ON_KEY_ANCHOR = ("        return;\n"
                 "      }\n"
                 "      // Both surfaces that name this key are now true: the ANALYZER")

ON_KEY_NEW = (
    "        return;\n"
    "      }\n"
    "      // Group mute, on the key the SHORTCUTS panel advertises. The guard\n"
    "      // above already rejected every modifier, a focused text field and\n"
    "      // any open overlay, so this needs no guard of its own.\n"
    "      //\n"
    "      // The bank owns the rule because the selection lives in FilterBank\n"
    "      // and this handler does not. A null result means nothing was\n"
    "      // selected, which is reported rather than swallowed: a key that\n"
    "      // silently does nothing is indistinguishable from one that is\n"
    "      // broken.\n"
    "      if (k === \"m\") {\n"
    "        e.preventDefault();\n"
    "        const bank = bankRef.current;\n"
    "        const result = bank && typeof bank.toggleMuteSelection === \"function\"\n"
    "          ? bank.toggleMuteSelection() : null;\n"
    "        if (!result) fireStatus(\"NO SELECTION\");\n"
    "        else fireStatus(result.count + \" BAND\" + (result.count === 1 ? \"\" : \"S\")\n"
    "          + (result.muted ? \" MUTED\" : \" UNMUTED\"));\n"
    "        return;\n"
    "      }\n"
    "      // Both surfaces that name this key are now true: the ANALYZER")

EDITS = [
    ("the selection has a ref the bank can read", SELECTION_DECL,
     SELECTION_DECL_NEW),
    ("the bank can mute or unmute a whole selection", ALL_MUTED,
     TOGGLE_METHOD),
    ("`m` mutes or unmutes the selection", ON_KEY_ANCHOR, ON_KEY_NEW),
]

FORBIDDEN_AFTER = (
    # A bank method reading `selection` directly would be frozen at mount.
    "toggleMuteSelection: () => {\n        const sel = selection;",
    # The tap-one variant: CLICK must keep meaning "toggle THIS band".
    "onMuteBand: (b) => {\n          const sel = selectionRef.current;",
)

REQUIRED_AFTER = (
    "const selectionRef = useRef(selection);",
    "useEffect(() => { selectionRef.current = selection; }, [selection]);",
    "toggleMuteSelection: () => {",
    "const sel = selectionRef.current;",
    # The mixed-state rule, in the one spelling that implements it: mute when
    # ANY selected band is unmuted.
    "const mute = bands.some((i) => !isMuted(targetGainsRef.current[i]));",
    # Unmute restores the remembered level rather than flattening to 0.
    "map.set(i, Number.isFinite(db) ? clamp(db / 24, -1, 1) : 0);",
    "if (k === \"m\") {",
    "fireStatus(\"NO SELECTION\");",
    # The keys this one must not collide with, still bound to what they were.
    "if (modeKeys[k]) {",
    "if (k === \"a\" || k === \"6\") {",
    # CLICK still toggles the band under the pointer, selection or not. This
    # is the `p.band` spelling -- the pointer-release path. An earlier version
    # of this list named the `b` spelling instead and called it the click path;
    # that was the band context MENU's handler, which is a different surface.
    "commitGain(p.band, isMuted(cur) ? restored : -Infinity);",
    # The menu's group mute is no longer a one-way trip and no longer a third
    # writer of band state: it defers to `toggleMuteSelection`, the owner this
    # script installed. What must stay true is that the owner still exists and
    # still restores rather than flattening.
    "owner.toggleMuteSelection()",
)


def main():
    raw = open(PATH, encoding='utf-8').read()
    changed = False
    for label, old, new in EDITS:
        new_e = enc(new)
        if raw.count(new_e) >= 1:
            print('already applied ', label)
            continue
        cand, why = choose(old, raw)
        if cand is None:
            sys.exit('FAIL %s: %s' % (label, why))
        raw = raw.replace(enc(cand), new_e, 1)
        changed = True
        print('applied         ', label)

    for label, _old, new in EDITS:
        count = raw.count(enc(new))
        if count != 1:
            sys.exit('FAIL %s: final text occurs %d times after patching, '
                     'expected 1' % (label, count))

    for token in FORBIDDEN_AFTER:
        if raw.count(enc(token)):
            sys.exit('FAIL: %r appears after patching' % (token,))
    for token in REQUIRED_AFTER:
        if raw.count(enc(token)) == 0:
            sys.exit('FAIL: %r is absent after patching' % (token,))

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
