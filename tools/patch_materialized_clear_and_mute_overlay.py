#!/usr/bin/env python3
"""Make CLEAR reach native, and stop muted bands flickering under an LFO.

Two defects, one seam: native publications write the editor's JS refs
directly, and both halves of that seam were leaking.

CLEAR DID NOT CLEAR (in the plugin; it worked in the standalone)
---------------------------------------------------------------
The band field reaches C++ through one effect, which publishes
`processing_state_set` whenever `gains`/`view`/`N` change.  That effect
carries an echo suppressor:

    if (nativeProjectionRef.current && !nativeEditPendingRef.current) {
      nativeProjectionRef.current = false;
      return;
    }

The suppressor exists so a render *caused by* a native projection is not
echoed straight back at native.  It is a one-shot: the projection arms it,
the render it causes consumes it.

`applyHostAutomationState` armed it and never fired the shot.  It writes the
paint and target refs directly and calls no state setter, so the publication
effect's dependencies never change and the render it is arming against never
happens.  The flag simply stays true -- until some unrelated later render
consumes it and gets swallowed instead.

CLEAR is exactly that unrelated render.  `clearGains`, `resetAll`, `reset`
and the imperative `setGains` mutate `targetGainsRef` and call `setGains`
WITHOUT setting `nativeEditPendingRef`, which is the signal `commitGain` and
`commitMany` use to declare "this is a local edit, publish it regardless".
So a CLEAR arriving after a latched projection is swallowed whole: the status
pill reads CLEARED GAINS, the field in C++ never changes, and the next
projection repaints the old shape.

That is why it differs between hosts.  `processing_state_live` is published
only when `host_automation_revision()` moves, and that only moves when
something OUTSIDE the plugin writes a parameter -- the plugin's own writes
update the applied cache in the same call.  A DAW does that; the standalone
has no such writer, so the flag is never latched there and CLEAR publishes.

Fixed on both halves, because either alone leaves the seam able to swallow a
different command tomorrow: the projection no longer arms a suppression it
cannot consume, and every local mutator declares its edit.

MUTED BANDS FLICKER WHILE AN LFO RUNS
-------------------------------------
A muted band's painted value is the sentinel `-Infinity`, and the draw loop
holds it there:

    if (isMuted(tg[i])) {
      if (!isMuted(rg[i])) { ...collapse rg[i] toward -1.02, then -Infinity }
    }

Every projection into the paint refs writes `0` for a muted band instead of
that sentinel, so the draw loop sees a finite value on a muted band and
re-runs the collapse animation -- about eight frames of ramp -- and the next
projection resets it to 0 again.  With an LFO running, modulation frames
arrive continuously, so a muted band sweeps a sawtooth instead of sitting
collapsed.  Measured on the shipping document: a muted band paints one
constant value across 20 idle frames, and -0.36 / -0.59 / -0.74 repeating
once modulation frames arrive.

The overlay writer landed in 30d9384 ("draw the modulation the audio owner is
playing"), which is when the fight started.  The later `modulationActiveRef`
guard covered the UNMUTED branch of the same loop and left the muted branch
alone, so this survived it.

The fix is the invariant the draw loop already assumes: a projection into
renderGains preserves the muted sentinel rather than flattening it to 0.

resources/editor.html is deliberately NOT mirrored -- it is the browser
bootstrap, not the shipping surface, and test_import_fidelity.cpp pins its
pre-patch shape on purpose.  This file is the durable record.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(REPO, "native-ui", "materialized",
                    "materialized-document.runtime.json")

# (label, expected occurrences, needle, replacement)
EDITS = [
    ("host-automation projection stops arming an echo suppressor it never fires",
     1,
     '      applyHostAutomationState: (state) => {\n'
     '        if (!Number.isSafeInteger(state.revision) || state.revision < nativeAppliedRevisionRef.current) return false;\n'
     '        nativeProjectionRef.current = true;\n'
     '        nativeAppliedRevisionRef.current = state.revision;\n',
     '      applyHostAutomationState: (state) => {\n'
     '        if (!Number.isSafeInteger(state.revision) || state.revision < nativeAppliedRevisionRef.current) return false;\n'
     '        // No nativeProjectionRef arm here, deliberately. That flag is a\n'
     '        // ONE-SHOT suppressor for the render a projection causes, and this\n'
     '        // path calls no state setter -- it writes refs only, so the\n'
     '        // publication effect never re-runs for it. Arming it leaves the\n'
     '        // flag latched until some unrelated later edit consumes it, and\n'
     '        // that edit is the one that silently never reaches native.\n'
     '        nativeAppliedRevisionRef.current = state.revision;\n'),

    ("clearGains declares a local edit",
     1,
     '      clearGains: () => {\n'
     '        const z = new Array(N).fill(0);\n'
     '        targetGainsRef.current = z.slice();\n'
     '        setGains(z);\n'
     '      },\n',
     '      clearGains: () => {\n'
     '        // Declare the local edit. Without this the publication effect\n'
     '        // cannot tell a user command from a native echo, and a CLEAR that\n'
     '        // lands after a projection is dropped on the floor while the\n'
     '        // status pill still reports success.\n'
     '        nativeEditPendingRef.current = true;\n'
     '        const z = new Array(N).fill(0);\n'
     '        targetGainsRef.current = z.slice();\n'
     '        setGains(z);\n'
     '      },\n'),

    ("resetAll declares a local edit",
     1,
     '      resetAll: () => {\n'
     '        const z = new Array(N).fill(0);\n'
     '        targetGainsRef.current = z.slice();\n',
     '      resetAll: () => {\n'
     '        nativeEditPendingRef.current = true;\n'
     '        const z = new Array(N).fill(0);\n'
     '        targetGainsRef.current = z.slice();\n'),

    ("reset declares a local edit",
     1,
     '      reset: () => {\n'
     '        const z = new Array(N).fill(0);\n'
     '        targetGainsRef.current = z.slice();\n',
     '      reset: () => {\n'
     '        nativeEditPendingRef.current = true;\n'
     '        const z = new Array(N).fill(0);\n'
     '        targetGainsRef.current = z.slice();\n'),

    ("the imperative gain setter declares a local edit",
     1,
     '      setGains: (arr) => {\n'
     '        const next = arr.slice(0, N);\n',
     '      setGains: (arr) => {\n'
     '        nativeEditPendingRef.current = true;\n'
     '        const next = arr.slice(0, N);\n'),

    ("native field projections preserve the muted sentinel",
     3,
     'renderGainsRef.current = state.gains.map((value, index) => state.muted[index] ? 0 : clamp(value, -1.02, 1.02)).slice(0, N);',
     'renderGainsRef.current = state.gains.map((value, index) => state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);'),

    ("the modulation overlay's release preserves the muted sentinel",
     1,
     'renderGainsRef.current = targetGainsRef.current.map((value) => isMuted(value) ? 0 : clamp(value, -1.02, 1.02)).slice(0, N);',
     'renderGainsRef.current = targetGainsRef.current.map((value) => isMuted(value) ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N);'),
]


def escaped(value):
    # The document stores the page as a JSON string, so every needle has to be
    # escaped the way the file stores it.  Raw-text surgery, not a load/dump
    # round trip: this file's escaping is not uniform (a handful of sites carry
    # \\/ where the rest carry a bare /), so re-serialising it would rewrite
    # ~866 bytes that have nothing to do with this change and collide with the
    # other lanes editing the same artifact.
    return json.dumps(value)[1:-1]


# Proof the patch landed, independent of the substitution bookkeeping above.
FORBIDDEN_AFTER = [
    # The suppressor must no longer be armed by a path that schedules no render.
    '        if (!Number.isSafeInteger(state.revision) || state.revision < '
    'nativeAppliedRevisionRef.current) return false;\n'
    '        nativeProjectionRef.current = true;\n',
    # No projection into the paint refs may flatten a muted band to 0.
    'state.muted[index] ? 0 : clamp(value, -1.02, 1.02)',
    'isMuted(value) ? 0 : clamp(value, -1.02, 1.02)',
]

REQUIRED_AFTER = [
    # The suppressor is still armed by the two projections that DO commit.
    '      hydrateProcessingState: (state) => {\n'
    '        nativeProjectionRef.current = true;\n',
    'state.muted[index] ? -Infinity : clamp(value, -1.02, 1.02)',
]


def main():
    for label, expected, needle, replacement in EDITS:
        if needle and needle in replacement:
            print(f"FAIL: patch point \"{label}\" survives its own replacement",
                  file=sys.stderr)
            return 1

    raw = open(PATH, encoding="utf-8").read()
    changed = False
    applied = 0
    already = 0

    for label, expected, needle, replacement in EDITS:
        old_e, new_e = escaped(needle), escaped(replacement)
        found = raw.count(old_e)
        done = raw.count(new_e)
        if found == 0 and done >= expected:
            print("already applied ", label)
            already += 1
            continue
        if found != expected:
            print(f"FAIL: patch point \"{label}\" occurs {found} times, "
                  f"expected {expected} -- refusing to write a partial patch",
                  file=sys.stderr)
            return 1
        raw = raw.replace(old_e, new_e)
        if raw.count(new_e) < expected:
            print(f"FAIL: patch point \"{label}\" did not verify after "
                  "substitution", file=sys.stderr)
            return 1
        changed = True
        applied += 1
        print("applied         ", label)

    if applied and already:
        print("FAIL: the document is half patched; refusing to write",
              file=sys.stderr)
        return 1

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            print(f"FAIL: {token!r} still appears {count} times after patching",
                  file=sys.stderr)
            return 1
    for token in REQUIRED_AFTER:
        if raw.count(escaped(token)) == 0:
            print(f"FAIL: {token!r} is absent after patching", file=sys.stderr)
            return 1

    document = json.loads(raw)
    if not isinstance(document.get("html"), str) or not document["html"]:
        print("FAIL: the patched document no longer carries an html payload",
              file=sys.stderr)
        return 1

    if not changed:
        print(f"already applied: {already}/{len(EDITS)} edits, nothing written")
        return 0

    open(PATH, "w", encoding="utf-8").write(raw)
    print(f"wrote {PATH} ({applied} applied, {already} already present)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
