#!/usr/bin/env python3
"""Give the editor the macro overlay: assign a selection to a macro, drag a
member band to drive that macro's host lane, and DRAW the offset.

WHY THE DRAG RE-ROUTE IS THE POINT, NOT A CONVENIENCE.

    Before this, dragging a selection committed one `Band NN Gain` write per
    selected band, each with its own host gesture bracket:

        if (p.groupStart) { ... commitDrawnGains(map); return; }

    A host's parameter-learn records the FIRST parameter it sees a gesture
    on, so Logic's Modulator latched onto whichever band happened to be
    written first and the user ended up automating one arbitrary band
    instead of the group they drew.  Adding a macro parameter without
    re-routing the drag would have changed nothing about that: the user's
    gesture would still have announced 64 band lanes.  So a member drag now
    drives the MACRO -- one parameter, one bracket -- and a modifier drag
    (Alt / Option) still edits the members directly, which is how the user
    adjusts the shape inside a group after assigning it.

WHY THE OFFSET IS ADDED AT READ TIME AND NEVER WRITTEN INTO THE PAINT REF.

    Two existing paths already OVERWRITE `renderGainsRef` wholesale --
    `applyModulationFrame` every LFO frame, and `applyHostAutomationState`
    every live revision.  A macro that wrote its offset into that ref would
    be clobbered by either, intermittently, which reads as a flickering UI
    rather than as a bug with a cause.  So `macroAdjustedGain` is a read-time
    rule applied at the four places a displayed gain is derived, and the
    paint refs keep their single-writer contract.

    It is GATED on `modulationActiveRef`.  While an LFO runs, the field the
    audio owner publishes has ALREADY had the macros composed into it (the
    processor applies macros before the LFOs, so a Whole Bank LFO wobbles
    around what you hear).  Adding them again on this side would double-count
    them, and only while a modulator happened to be running.

ONE NAMED RULE, FOUR CALL SITES.  `macroAdjustedGain(value, index)` is
defined once next to `modulationActiveRef` -- the latch it reads -- and used
by the bars (`effectiveGains`), the response curve (`drawMaskResponse`), and
both hover readouts.  A displayed gain that skipped it would show the user a
number they are not hearing, and the hover readout is the surface where that
is least forgivable.

MEMBERSHIP ARRIVES THROUGH THE PARSERS, WHICH WHITELIST.  Both live-state
parsers and the hydration parser rebuild a fixed object, so an unknown
`macros` member would be silently dropped.  All three are extended, and all
three treat ABSENCE as null rather than as a rejection -- a payload from a
build without macros must still parse.

NOT IN SCOPE, DELIBERATELY: a Macros panel in Settings (the values are
ordinary parameters and the host already lists them), and per-macro naming.

AND NOT THE CONTEXT-MENU ROWS, WHICH WERE MEASURED AND WITHDRAWN.  An
"Assign selection to Macro N" / "Clear Macro N" block was written, applied and
driven through `tools/menu_scenario_check.py` against the real standalone.  It
took that gate from 0 failures to 6.  Two of them are the serious kind: with
the extra rows present, a press aimed at `Zero selection` and one aimed at
`Sculpt` fire something else, because the menu's container carries a stale
layout solve and does not grow when children are added.  Three more are
structural child COUNTS the gate pins exactly (17 rows with a selection, 14
without), which ANY added row breaks regardless of where it is placed -- so
there is no position in the menu that avoids this.

The bridge command `macro_set_members` is deliberately kept: the affordance is
one patch away once the container fix lands, and the command is covered by its
own tests in the meantime.  Re-measure with the menu gate before re-adding the
rows; do not assume the pinned misaim map still holds.

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


EDITS = [
    # ── 1. Head-script parser: a shared macro reader, absence-tolerant ──
    (
        "head script parses macros, treating absence as null",
        """  const parseNativeLiveState = payload => {""",
        """  const parseNativeMacros = (payload, n) => {
    // Absent is not malformed. A payload written by a build without macros
    // must still parse, so this returns null rather than failing the whole
    // state read -- the render treats null as "no macros assigned".
    const raw = payload && payload.macros;
    if (!Array.isArray(raw)) return null;
    const macros = [];
    for (const entry of raw) {
      const db = entry && Number(entry.value_db);
      const slots = entry && entry.slots;
      if (!Number.isFinite(db) || !Array.isArray(slots)) return null;
      const members = [];
      for (const slot of slots) {
        if (!Number.isFinite(slot) || Math.floor(slot) !== slot
            || slot < 0 || slot >= n) return null;
        members.push(slot);
      }
      macros.push({ valueDb: db, slots: members });
    }
    return macros;
  };
  const parseNativeLiveState = payload => {""",
    ),
    (
        "head script live state carries macros",
        """      minHz, maxHz, revision,
      motionMode, analyzerMode, editMode, visualizationMode,
    };
  };
  const nativeState = { parse: parseNativeState, parseLive: parseNativeLiveState };""",
        """      minHz, maxHz, revision,
      motionMode, analyzerMode, editMode, visualizationMode,
      macros: parseNativeMacros(payload, n),
    };
  };
  const nativeState = { parse: parseNativeState, parseLive: parseNativeLiveState, parseMacros: parseNativeMacros };""",
    ),
    # ── 2. Bundle copy of the live parser ──
    (
        "bundle live parser carries macros",
        """    minHz, maxHz, revision,
    motionMode, analyzerMode, editMode, visualizationMode,
  };
}""",
        """    minHz, maxHz, revision,
    motionMode, analyzerMode, editMode, visualizationMode,
    // Same reader as the head script's, reached through the export rather
    // than duplicated: two copies of a validation rule drift, and this one
    // decides whether a band is drawn where it is heard.
    macros: window.SpectrNativeState && typeof window.SpectrNativeState.parseMacros === "function"
      ? window.SpectrNativeState.parseMacros(payload, n) : null,
  };
}""",
    ),
    # ── 3. The read-time rule, next to the latch it reads ──
    (
        "macroAdjustedGain, defined beside the modulation latch",
        """  const modulationActiveRef = useRef(false);""",
        """  const modulationActiveRef = useRef(false);
  // Macro overlay. `macroStateRef` is what native last published;
  // `macroOffsetsRef` is that flattened to one normalised offset per band so
  // the render path does no per-frame searching.
  const macroStateRef = useRef(null);
  const macroOffsetsRef = useRef(new Float32Array(64));
  const recomputeMacroOffsets = (macros) => {
    const out = new Float32Array(64);
    if (Array.isArray(macros)) {
      for (const macro of macros) {
        if (!macro || !Array.isArray(macro.slots)) continue;
        // Offsets SUM across overlapping macros, matching the processor.
        // Clamping per macro here would disagree with what is audible.
        const normalised = macro.valueDb / 24;
        for (const slot of macro.slots)
          if (slot >= 0 && slot < out.length) out[slot] += normalised;
      }
    }
    macroOffsetsRef.current = out;
  };
  const setMacroState = (macros) => {
    macroStateRef.current = Array.isArray(macros) ? macros : null;
    recomputeMacroOffsets(macroStateRef.current);
  };
  const macroOwning = (band) => {
    const macros = macroStateRef.current;
    if (!Array.isArray(macros)) return null;
    for (let i = 0; i < macros.length; i++) {
      const slots = macros[i] && macros[i].slots;
      if (Array.isArray(slots) && slots.includes(band)) return i;
    }
    return null;
  };
  const macroValueOf = (index) => {
    const macros = macroStateRef.current;
    const macro = Array.isArray(macros) ? macros[index] : null;
    return macro && Number.isFinite(macro.valueDb) ? macro.valueDb : 0;
  };
  // THE display rule. Read-time, never written into a paint ref: both
  // applyModulationFrame and applyHostAutomationState overwrite that ref
  // wholesale, so an offset parked there would be clobbered intermittently.
  //
  // Gated on the LFO latch because the published modulated field has already
  // had macros composed into it by the audio owner -- adding them again here
  // would double-count them, and only while a modulator happened to run.
  //
  // A muted band is non-finite here and is returned untouched, which is the
  // same rule the processor applies: a macro modulates levels and never
  // reaches a band the user silenced.
  const macroAdjustedGain = (value, index) => {
    if (modulationActiveRef.current) return value;
    if (!Number.isFinite(value)) return value;
    const offset = macroOffsetsRef.current[index];
    return offset === 0 ? value : clamp(value + offset, -1.02, 1.02);
  };""",
    ),
    # ── 4. The four display sites ──
    (
        "the bars draw the macro-adjusted gain",
        """    const effectiveGains = rg.map((value) => Number.isFinite(value) ? clamp(value, -1.02, 1.02) : 0);""",
        """    const effectiveGains = rg.map((value, index) => Number.isFinite(value) ? clamp(macroAdjustedGain(value, index), -1.02, 1.02) : 0);""",
    ),
    (
        "the response curve draws the macro-adjusted gain",
        """const rendered = Number.isFinite(rg[i]) ? clamp(rg[i], -1, 1) : 0;""",
        """const rendered = Number.isFinite(rg[i]) ? clamp(macroAdjustedGain(rg[i], i), -1, 1) : 0;""",
    ),
    (
        "the hover readout reports the macro-adjusted gain",
        """    const rendered = renderGainsRef.current[band];""",
        """    const rendered = macroAdjustedGain(renderGainsRef.current[band], band);""",
    ),
    (
        "the hover status effect reports the macro-adjusted gain",
        """    const rendered = renderGainsRef.current[hoverBand];""",
        """    const rendered = macroAdjustedGain(renderGainsRef.current[hoverBand], hoverBand);""",
    ),
    # ── 5. Native state receivers adopt the membership ──
    (
        "hydration adopts macro state",
        """        snapshotsRef.current = state.snapshots;
        setSnapshots(state.snapshots);""",
        """        setMacroState(state.macros);
        snapshotsRef.current = state.snapshots;
        setSnapshots(state.snapshots);""",
    ),
    (
        "live host automation adopts macro state",
        """        nativeAppliedRevisionRef.current = state.revision;
        mutedGainDbRef.current = state.gainDb.slice(0, N);""",
        """        nativeAppliedRevisionRef.current = state.revision;
        // Macros ride the live projection because they ARE host parameters:
        // automation moves them, and the drawn offset has to follow without
        // waiting for a re-hydration.
        setMacroState(state.macros);
        mutedGainDbRef.current = state.gainDb.slice(0, N);""",
    ),
    # ── 6. Hydration parser ──
    (
        "hydration parser carries macros",
        """      minHz, maxHz, snapshots: { A, B },
      revision: Number(payload.revision) || 0,""",
        """      minHz, maxHz, snapshots: { A, B },
      macros: parseNativeMacros(payload, n),
      revision: Number(payload.revision) || 0,""",
    ),
    # ── 7. The drag re-route: a member drag drives the macro ──
    (
        "pointer down records the macro a member drag will drive",
        """      groupStart: selection.size > 1 && selection.has(band) ? new Map([...selection].map((i) => [i, targetGainsRef.current[i]])) : null,""",
        """      groupStart: selection.size > 1 && selection.has(band) ? new Map([...selection].map((i) => [i, targetGainsRef.current[i]])) : null,
      // Dragging a band that belongs to a macro drives the MACRO, not the
      // band. That is the whole gesture story: one parameter and one host
      // bracket, so a host's learn grabs the group the user drew instead of
      // whichever band happened to be written first.
      //
      // Alt / Option opts out and edits the members directly, which is how
      // the shape inside a group is adjusted after it has been assigned.
      macroDrag: (e.altKey || e.metaKey) ? null : macroOwning(band),
      macroStart: clamp(macroValueOf(macroOwning(band)) / 24, -1, 1),""",
    ),
    (
        "a member drag moves the macro lane, before the group-drag path",
        """      if (p.groupStart) {
        const delta = -dy / g.halfH;""",
        """      if (p.macroDrag !== null && p.macroDrag !== undefined) {
        const delta = -dy / g.halfH;
        const next = clamp(p.macroStart + delta, -1, 1);
        driveMacro(p.macroDrag, next * 24);
        return;
      }
      if (p.groupStart) {
        const delta = -dy / g.halfH;""",
    ),
    (
        "the macro drag opens and closes one host bracket",
        """  const onPointerUp = (e) => {
    const p = pointerRef.current;
    pointerRef.current = { mode: null };""",
        """  const onPointerUp = (e) => {
    const p = pointerRef.current;
    pointerRef.current = { mode: null };
    if (p && p.macroDrag !== null && p.macroDrag !== undefined) {
      // Closes the epoch opened on pointer down. The native side brackets
      // one begin/end per touched parameter per epoch, so a drag of any
      // length announces exactly one gesture on exactly one lane.
      postNative("macro_drag_end", {});
    }""",
    ),
    (
        "driveMacro publishes optimistically, then to native",
        """  const minimapHit = (x, y, g) => {""",
        """  // Writing a macro. The local update comes FIRST so the bars follow the
  // pointer at display rate rather than at the native round-trip rate --
  // the same optimistic shape `publish` uses in the modulation panel.
  // Native remains the owner: the next live projection overwrites this.
  const driveMacro = (index, valueDb) => {
    const macros = macroStateRef.current;
    if (!Array.isArray(macros) || !macros[index]) return;
    const next = macros.map((macro, i) => i === index
      ? { valueDb: clamp(valueDb, -24, 24), slots: macro.slots } : macro);
    setMacroState(next);
    postNative("macro_set", { macro: index, value_db: clamp(valueDb, -24, 24) });
  };
  const postNative = (type, payload) => {
    if (!window.pulp || typeof window.pulp.postMessage !== "function") return;
    Promise.resolve(window.pulp.postMessage(type, payload, "spectr-" + type))
      .catch((error) => console.error("[Spectr] " + type + " failed", error));
  };
  const minimapHit = (x, y, g) => {""",
    ),
    (
        "pointer down opens the macro gesture epoch",
        """      didDrag: false
    };
    hoverRef.current = { band, x, y, n: N };""",
        """      didDrag: false
    };
    if (pointerRef.current.macroDrag !== null
        && pointerRef.current.macroDrag !== undefined)
      postNative("macro_drag_start", {});
    hoverRef.current = { band, x, y, n: N };""",
    ),
]


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

    # Every display site must be routed. A surviving raw read is a surface
    # that would show a value the user is not hearing.
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


FORBIDDEN_AFTER = (
    # The unrouted display reads. Each of these would draw or report a gain
    # that ignores the macro the user is driving.
    'const effectiveGains = rg.map((value) => Number.isFinite(value)',
    'const rendered = Number.isFinite(rg[i]) ? clamp(rg[i], -1, 1) : 0;',
    'const rendered = renderGainsRef.current[band];',
    'const rendered = renderGainsRef.current[hoverBand];',
)

REQUIRED_AFTER = (
    'const macroAdjustedGain = (value, index) => {',
    'const driveMacro = (index, valueDb) => {',
    'macroDrag: (e.altKey || e.metaKey) ? null : macroOwning(band),',
    'postNative("macro_drag_start", {});',
    'postNative("macro_drag_end", {});',
    'macros: parseNativeMacros(payload, n),',
    'const parseNativeMacros = (payload, n) => {',
    'parseMacros: parseNativeMacros',
    'const macroOwning = (band) => {',
    'setMacroState(state.macros);',
    # The latch the display rule defers to must still be the LFO overlay's.
    'if (!modulationActiveRef.current) rg[i] = smooth(rg[i], target, dt * k);',
)

# Rows that must appear EXACTLY ONCE. Presence checks cannot see a
# DUPLICATE, and a second copy of the display rule reading a stale ref is
# exactly the failure this file's sibling scripts were bitten by.
COUNTS_AFTER = {
    'const macroAdjustedGain = (value, index) => {': 1,
    'const macroStateRef = useRef(null);': 1,
    'const parseNativeMacros = (payload, n) => {': 1,
    'macroAdjustedGain(renderGainsRef.current[band], band)': 1,
    'macroAdjustedGain(renderGainsRef.current[hoverBand], hoverBand)': 1,
    'const driveMacro = (index, valueDb) => {': 1,
    'postNative("macro_drag_start", {});': 1,
    'postNative("macro_drag_end", {});': 1,
    # The macro branch must sit BEFORE the group-drag path, and there must be
    # exactly one of each: a second groupStart branch would make the
    # re-route depend on which one the interpreter reached first.
    'if (p.groupStart) {': 1,
    'macros: parseNativeMacros(payload, n),': 2,
}


if __name__ == '__main__':
    sys.exit(main())
