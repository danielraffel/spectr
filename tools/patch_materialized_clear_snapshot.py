#!/usr/bin/env python3
"""Let a snapshot slot be emptied -- and make RESET ALL mean what it says.

THE CAPABILITY WAS ABSENT, not merely unexposed. Measured on `main`, with
controls on the same instruments:

    clear_snapshot     0 C++ files, 0 occurrences in the shipping document
    capture_snapshot   6 C++ files, 1 occurrence  (the control)

So once a slot was filled it could only be OVERWRITTEN, never emptied, by any
route. The right-click gesture is the surface; the missing handler behind it
is the substance, and it is worth building on its own account:

RESET ALL ADVERTISES "gains . view . snapshots" AND CLEARED TWO OF THE THREE.
The editor's `resetAll` dropped its own mirror (`snapshotsRef`, `snapshots`,
`snapshotStatus`) and told the processor nothing, because there was nothing to
tell it. The bank kept both slots `populated`, and the next full native
projection re-lit the dots from them -- `acceptNativeState` sets
`snapshotStatus` from `state.snapshots`, so the reset visibly undid itself.
That is a defect independent of any gesture, and the same handler fixes it.

THE GESTURE. Right-click a FILLED recall button. Not a capture button -- a
context gesture there would be ambiguous with the capture it is named for --
and not an empty one, which has nothing to do.

CLEARING IS IDEMPOTENT, deliberately: an empty slot clears to itself rather
than erroring, so RESET ALL can clear both without first asking which were
filled. This mirrors `SnapshotBank::clear`, which resets the whole slot rather
than only the flag, so a cleared slot and one never captured are the same
thing to every reader.

THE AFFORDANCE, which the issue asks to be judged rather than assumed. The
issue records filled-vs-empty as signalled "only by opacity", and that is not
what `SnapBtn` does. A filled RECALL button differs from an empty one in four
painted channels plus its interactive state:

    background   rgba(40,80,120,0.22)   vs  rgba(255,255,255,0.03)
    border       rgba(140,190,240,0.35) vs  rgba(255,255,255,0.08)
    color        rgba(255,255,255,0.9)  vs  rgba(255,255,255,0.4)
    opacity      1                      vs  0.45
    disabled     false                  vs  true (cursor: not-allowed)

So the distinction is already legible and this adds no decoration to it. What
it adds is the confirmation every other destructive action here already gives:
a status line, in the same vocabulary as `SNAPSHOT A CAPTURED`, `CLEARED
GAINS` and `RESET ALL`. Clearing a slot also unmutes the morph control's
precondition -- the caption reappears reading `SET A TO MORPH` and the slider
dims -- which is a second, much larger confirmation that costs nothing here
because `snapshotStatus` already drives it reactively.

Not undoable, and consistent: capture is destructive today too (it overwrites
without confirmation), so a clear that asked for confirmation would be the
odd one out.

OWNERSHIP. `SnapBtn` and `recallSnap` have no other writer;
`patch_materialized_hit_targets.py` mentions SnapBtn only in prose. `resetAll`
is `patch_materialized_clear_and_mute_overlay.py`'s at its HEAD -- it inserts
the edit declaration on the first line -- and this appends at its TAIL, a
distinct patch point that leaves that anchor contiguous.

NOTE: the document is compiled into the binary by `pulp_add_binary_data`, so a
rebuild is REQUIRED before any native test reflects this patch. `Encoding
binary asset materialized-document.runtime.json` in the build log is the
proof; "Built target" is not.

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
    return json.dumps(snippet)[1:-1]


def predecessors(old):
    return (old,) if isinstance(old, str) else tuple(old)


def choose(old, raw):
    for cand in reversed(predecessors(old)):
        count = raw.count(enc(cand))
        if count == 1:
            return cand, None
        if count > 1:
            return None, ('patch point %r occurs %d times, expected 1'
                          % (cand, count))
    return None, 'no known predecessor text is present'


RECALL_HEAD = "      recallSnap: (slot) => {"

CLEAR_METHOD = (
    "      // Empty a slot. Until this existed a filled slot could only be\n"
    "      // OVERWRITTEN, never emptied, by any route -- which is also why\n"
    "      // RESET ALL could not do what its name says: it dropped this\n"
    "      // mirror while the processor kept both slots, and the next full\n"
    "      // projection lit them again from `populated`.\n"
    "      //\n"
    "      // Idempotent on purpose: clearing an empty slot is a no-op rather\n"
    "      // than an error, so RESET ALL can clear both without first asking\n"
    "      // which of them were filled.\n"
    "      clearSnap: (slot) => {\n"
    "        snapshotsRef.current = { ...snapshotsRef.current, [slot]: null };\n"
    "        setSnapshots(snapshotsRef.current);\n"
    "        if (nativeOwnsSnapshots) {\n"
    "          issueNativeCommand(\n"
    "            \"clear_snapshot\",\n"
    "            { slot },\n"
    "            \"SNAPSHOT \" + slot + \" CLEARED\"\n"
    "          );\n"
    "          return;\n"
    "        }\n"
    "        if (onStatus) onStatus(\"SNAPSHOT \" + slot + \" CLEARED\");\n"
    "      },\n"
    + RECALL_HEAD)

RESET_TAIL = ("        snapshotsRef.current = { A: null, B: null };\n"
              "        setSnapshots({ A: null, B: null });\n"
              "      },")

RESET_TAIL_NEW = (
    "        snapshotsRef.current = { A: null, B: null };\n"
    "        setSnapshots({ A: null, B: null });\n"
    "        // RESET ALL advertises \"gains . view . snapshots\" and cleared\n"
    "        // two of the three: without this the bank kept both slots and\n"
    "        // the next projection re-lit the dots from `populated`, so the\n"
    "        // reset visibly undid itself. skipState, because the gains\n"
    "        // publication already carries the field this just zeroed.\n"
    "        if (nativeOwnsSnapshots) {\n"
    "          issueNativeCommand(\"clear_snapshot\", { slot: \"A\" }, null, true);\n"
    "          issueNativeCommand(\"clear_snapshot\", { slot: \"B\" }, null, true);\n"
    "        }\n"
    "      },")

SNAPBTN_SIG = ("function SnapBtn({ id, action, slot, filled, onClick, "
               "capture, label }) {")
SNAPBTN_SIG_NEW = ("function SnapBtn({ id, action, slot, filled, onClick, "
                   "onClear, capture, label }) {")

SNAPBTN_PROPS = ("      id,\n"
                 "      \"data-spectr-snapshot-action\": action,\n"
                 "      \"data-spectr-snapshot-slot\": slot,\n"
                 "      onClick: handle,")

SNAPBTN_PROPS_NEW = (
    "      id,\n"
    "      \"data-spectr-snapshot-action\": action,\n"
    "      \"data-spectr-snapshot-slot\": slot,\n"
    "      // Filled/empty is already painted four ways plus the disabled\n"
    "      // state; this is the same fact in a form a probe can read, so a\n"
    "      // test never has to infer a slot's state from a colour.\n"
    "      \"data-spectr-snapshot-filled\": filled ? \"true\" : \"false\",\n"
    "      onClick: handle,\n"
    "      // Right-click empties a filled slot. Only on a RECALL button --\n"
    "      // the gesture would be ambiguous on the CAPTURE button it sits\n"
    "      // beside -- and only when the slot holds something, so an empty\n"
    "      // slot keeps the host's own menu rather than swallowing the press\n"
    "      // to do nothing.\n"
    "      onContextMenu: onClear && !isCapture && filled ? (event) => {\n"
    "        if (event && event.preventDefault) event.preventDefault();\n"
    "        onClear(event);\n"
    "      } : void 0,")

RECALL_A = ('React.createElement(SnapBtn, { id: "spectr-snapshot-recall-a", '
            'action: "recall", slot: "A", filled: snapshotStatus.A, '
            'onClick: act((b) => b.recallSnap("A")), label: "\\u25B8 A" })')
RECALL_A_NEW = ('React.createElement(SnapBtn, { id: "spectr-snapshot-recall-a", '
                'action: "recall", slot: "A", filled: snapshotStatus.A, '
                'onClick: act((b) => b.recallSnap("A")), '
                'onClear: act((b) => b.clearSnap("A")), label: "\\u25B8 A" })')

RECALL_B = ('React.createElement(SnapBtn, { id: "spectr-snapshot-recall-b", '
            'action: "recall", slot: "B", filled: snapshotStatus.B, '
            'onClick: act((b) => b.recallSnap("B")), label: "\\u25B8 B" })')
RECALL_B_NEW = ('React.createElement(SnapBtn, { id: "spectr-snapshot-recall-b", '
                'action: "recall", slot: "B", filled: snapshotStatus.B, '
                'onClick: act((b) => b.recallSnap("B")), '
                'onClear: act((b) => b.clearSnap("B")), label: "\\u25B8 B" })')

STATUS_PARSE = ("    if (/SNAPSHOT ([AB]) CAPTURED/.test(msg)) {\n"
                "      const slot = msg.match(/SNAPSHOT ([AB])/)[1];\n"
                "      setSnapshotStatus((s) => ({ ...s, [slot]: true }));\n"
                "    }")

STATUS_PARSE_NEW = (
    "    if (/SNAPSHOT ([AB]) CAPTURED/.test(msg)) {\n"
    "      const slot = msg.match(/SNAPSHOT ([AB])/)[1];\n"
    "      setSnapshotStatus((s) => ({ ...s, [slot]: true }));\n"
    "    }\n"
    "    // The falling edge of the same signal. Without it a cleared slot\n"
    "    // keeps its lit dot and its enabled recall button until some later\n"
    "    // full projection happens to correct them -- and the morph slider,\n"
    "    // which reads the same flags, would stay enabled over a slot that\n"
    "    // is gone.\n"
    "    if (/SNAPSHOT ([AB]) CLEARED/.test(msg)) {\n"
    "      const slot = msg.match(/SNAPSHOT ([AB])/)[1];\n"
    "      setSnapshotStatus((s) => ({ ...s, [slot]: false }));\n"
    "    }")

EDITS = [
    ("the bank can empty a slot", RECALL_HEAD, CLEAR_METHOD),
    ("RESET ALL clears the processor's bank too", RESET_TAIL, RESET_TAIL_NEW),
    ("the snapshot button accepts a clear", SNAPBTN_SIG, SNAPBTN_SIG_NEW),
    ("right-click empties a filled recall button", SNAPBTN_PROPS,
     SNAPBTN_PROPS_NEW),
    ("recall A can be cleared", RECALL_A, RECALL_A_NEW),
    ("recall B can be cleared", RECALL_B, RECALL_B_NEW),
    ("a cleared slot goes dark", STATUS_PARSE, STATUS_PARSE_NEW),
]

FORBIDDEN_AFTER = (
    # RESET ALL leaving the processor's bank populated.
    RESET_TAIL,
    # A capture button accepting the gesture, or an empty slot swallowing it.
    "onContextMenu: onClear ? (event) => {",
)

REQUIRED_AFTER = (
    "clearSnap: (slot) => {",
    "issueNativeCommand(\n            \"clear_snapshot\",",
    "issueNativeCommand(\"clear_snapshot\", { slot: \"A\" }, null, true);",
    "issueNativeCommand(\"clear_snapshot\", { slot: \"B\" }, null, true);",
    "onContextMenu: onClear && !isCapture && filled ? (event) => {",
    "onClear: act((b) => b.clearSnap(\"A\")),",
    "onClear: act((b) => b.clearSnap(\"B\")),",
    "if (/SNAPSHOT ([AB]) CLEARED/.test(msg)) {",
    "setSnapshotStatus((s) => ({ ...s, [slot]: false }));",
    "\"data-spectr-snapshot-filled\": filled ? \"true\" : \"false\",",
    # Capture is untouched: this adds a way to empty a slot, not a new way to
    # fill one, and the rising edge of the status signal must still work.
    "\"capture_snapshot\",",
    "setSnapshotStatus((s) => ({ ...s, [slot]: true }));",
    # The morph precondition still reads the same two flags, so a cleared slot
    # disables the control and brings its caption back with no extra wiring.
    "hasBoth ? \"enabled\" : \"disabled\"",
    "hasA ? \"SET B TO MORPH\" : (hasB ? \"SET A TO MORPH\" "
    ": \"SET A + B TO MORPH\")",
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
