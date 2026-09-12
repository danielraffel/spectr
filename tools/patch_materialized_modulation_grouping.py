#!/usr/bin/env python3
"""Group the Settings MODULATION rows per LFO, and separate the two destination
controls that read as one duplicated control.

Two user-reported defects, one edit.

DEFECT 1 -- the rows do not group by LFO.  Every row in the group is a sibling
of every other, so the reader has to reconstruct the grouping from the label
prefixes.  `Target` made that worse: it was gated on `value.enabled`, which
places it inside LFO 1's disclosure, so LFO 1 owned five rows and LFO 2 owned
three, and the shared destination row sat past the end of both.  The wanted
shape is the one the user described: enable LFO 1, see LFO 1's settings, then
LFO 2's toggle, then LFO 2's settings -- and the rows that belong to NEITHER
LFO last, once, where they read as shared.

DEFECT 2 -- `Target` and `Targets` read as the same control twice.  They are
not duplicates, and neither is vestigial; the audit is in the commit message
and below.  Both write the SAME destination field, at two different authority
levels, and `resolve_modulation_target_mask` (include/spectr/modulation.hpp)
is the single decider:

    target_mask == kModulationTargetMaskUnset (0xFF) -> the `target` enum
    otherwise                                        -> the mask

  * `Target` is kParamLfoTarget (4004), a registered host-automatable Enum
    parameter ("LFO Target", labels Whole Bank / Snapshot A / Snapshot B /
    Morph).  It is the ONLY destination lane a DAW can automate or a host
    preset can carry as a parameter.  src/param_surface.cpp resets the mask to
    the sentinel whenever the host moves this lane, so automation always wins.
  * `Targets` is `target_mask`, editor-only state written by the
    `modulation_targets_set` bridge message.  It is not a parameter.  It is the
    only way to select MORE THAN ONE destination (and ALL / NONE).
  * src/editor_bridge.cpp publishes the RESOLVED mask, never the sentinel, so
    the chips are also a live readout of what is actually being modulated.

Deleting either one loses a real capability: `Target` is the automation lane
(and test_built_clap.cpp pins it in the parameter census); `Targets` is
multi-destination selection.  So this renames instead of deleting, and -- the
part that actually fixes the confusion -- moves `Target` OUT of LFO 1's
disclosure, because it was never LFO 1's.  src/spectr.cpp copies the whole
ModulationSettings struct into the second LFO's pass, overriding only
shape/rate/depth, so `target` and `target_mask` are BOTH shared by both LFOs.
Gating `Target` on LFO 1 stated the opposite.

Resulting order (gates in parentheses):

    LFO                                     always
    Shape / Rate / Depth                    value.enabled
    LFO 2                                   always
    LFO 2 shape / rate / depth              value.lfo2Enabled
    Target        -> automatable lane       value.enabled || value.lfo2Enabled
    Destinations  -> multi-select override   value.enabled || value.lfo2Enabled

WHY THE ROWS ARE ALL MOUNTED AND HIDDEN, RATHER THAN CONDITIONALLY RENDERED --
this is the part that actually fixes what the user saw.  Reordering the JSX is
NOT sufficient, because the native runtime does not render children in DOM
order.  `attach()` in native-ui/materialized/runtime.js computes the right
insert index, keeps `childIds` and the DOM shim in that order, and then calls
`materialize(parent, child)` -> `materializeUnder(parent.id, child)` ->
`createWidget(type, id, parentId, props)` WITHOUT the index.  The widget bridge
has no insert-at-index and no move: probed live against the shipping native
editor, `globalThis.insertChild` and `globalThis.moveWidget` are both
`undefined` (while `removeWidget`, `setVisible`, `setStyle` and `createCol` are
functions), so the `g4.insertChild` / `g4.moveWidget` branch inside
`insertBefore` is dead code on this host.  A row that mounts LATER is therefore
APPENDED, wherever it sits in the JSX.

That is exactly the arrangement the user described: both toggles are
unconditional, so they take the first two slots at mount, and every row that
appears when a toggle is flipped lands after them in the order it was flipped.
Captured before this change with LFO 1 on: LFO, LFO 2, Shape, Rate, Depth,
Target, Destinations -- "we stack the lfo1/2 toggles and then put both their
settings on top".

So every row is mounted once, at mount, in the wanted order, and the enable
state drives VISIBILITY instead of mounting.  `SpectrSettingsField` takes a
`hidden` prop and puts `display: "none"` on its own root, which the runtime's
style applier maps to `setVisible(id, false)` -- a documented bridge path, and
View::visible() is the canonical "skip render + don't lay out" signal, so a
hidden row occupies no space and leaves no gap.  The rows still appear only
when their LFO is enabled, which is what the user asked for; they simply no
longer move when they do.

`Targets` is renamed to `Destinations` because the defect the user reported is
that "Target" and "Targets" are one keystroke apart while sitting in the same
group; co-locating them without renaming would have made that worse.  The
hints now name the override direction in both directions.

The LFO 2 rows keep their "LFO 2 ..." prefixes.  They are redundant under the
toggle now, but the prefix is what keeps every label in the group unique, and
tools/native_shot.cpp and the detectors address rows by label text.

Applied by hand.  `tools/patch_materialized_editor.py` -- the mirror that would
normally carry an edit like this -- does not run on a clean checkout: it aborts
at a stale needle and writes nothing, and the materialized generator itself
exits 1 writing 0 patches.  So this script edits the `html` payload of the
shipping runtime document by exact-text substitution, asserts every patch point
is unique before writing, and re-checks the result.  That makes the change
replayable after a merge conflict and reviewable as text rather than as an
opaque artifact diff.

resources/editor.html is deliberately NOT mirrored: it is the browser
bootstrap, not the shipping surface.  The native editor loads the runtime
document, and the browser design source carries no MODULATION group at all --
the native patch layer injects it -- so a browser screenshot cannot review this
surface.  tools/native_shot.cpp is the instrument.

Idempotent: a second run reports "already applied" and writes nothing.
Exit codes: 0 applied or already applied, 1 a patch point is missing/ambiguous.
"""

import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# argv[1] lets the edit be replayed onto a copy -- a conflicted merge result, or
# a pristine blob when proving that this script alone reproduces the artifact.
PATH = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(
    REPO, "native-ui", "materialized", "materialized-document.runtime.json")

SEP = ",\n    "

# --- the rows, verbatim from the document -----------------------------------

TARGET_OLD = (
    'value.enabled && /* @__PURE__ */ React.createElement(SpectrSettingsField, '
    '{ label: "Target", hint: "Shape the bank, snapshots, or morph" }, '
    '/* @__PURE__ */ React.createElement(SpectrSettingsChips, '
    '{ value: value.target, onChange: (next) => publish("target", 4004, next), '
    'opts: [[0,"Bank"],[1,"A"],[2,"B"],[3,"Morph"] ] })) '
)

# Same element, same parameter, same options. Only the gate, the label copy and
# the position move.
TARGET_NEW = (
    '(value.enabled || value.lfo2Enabled) && /* @__PURE__ */ '
    'React.createElement(SpectrSettingsField, '
    '{ label: "Target", hint: "Automatable; clears Destinations" }, '
    '/* @__PURE__ */ React.createElement(SpectrSettingsChips, '
    '{ value: value.target, onChange: (next) => publish("target", 4004, next), '
    'opts: [[0,"Bank"],[1,"A"],[2,"B"],[3,"Morph"] ] }))'
)

LFO2_TOGGLE = (
    'React.createElement(SpectrSettingsField, '
    '{ label: "LFO 2", hint: "Enable second modulation source" }, '
    'React.createElement(SpectrSettingsToggle, '
    '{ value: value.lfo2Enabled || false, onChange: (next) => publish("lfo2Enabled", 4010, next) }))'
)

LFO2_SHAPE = (
    'value.lfo2Enabled && React.createElement(SpectrSettingsField, '
    '{ label: "LFO 2 shape", hint: "Second waveform" }, '
    'React.createElement(SpectrSettingsChips, '
    '{ value: value.lfo2Shape || 0, onChange: (next) => publish("lfo2Shape", 4011, next), '
    'opts: [[0,"Sin"],[1,"Tri"],[2,"Square"],[3,"Saw"]] }))'
)

LFO2_RATE = (
    'value.lfo2Enabled && React.createElement(SpectrSettingsField, '
    '{ label: "LFO 2 rate", hint: "Beats per cycle" }, '
    'React.createElement(SpectrSettingsSlider, '
    '{ value: value.lfo2Rate || 4, min: 0.25, max: 16, step: 0.25, '
    'onChange: (next) => publish("lfo2Rate", 4012, next), fmt: (next) => next.toFixed(2) }))'
)

LFO2_DEPTH = (
    'value.lfo2Enabled && React.createElement(SpectrSettingsField, '
    '{ label: "LFO 2 depth", hint: "Modulation amount" }, '
    'React.createElement(SpectrSettingsSlider, '
    '{ value: value.lfo2Depth || 0, min: 0, max: 1, step: 0.01, '
    'onChange: (next) => publish("lfo2Depth", 4013, next) }))'
)

LFO2_BLOCK = SEP.join([LFO2_TOGGLE, LFO2_SHAPE, LFO2_RATE, LFO2_DEPTH])

EDITS = [
    # Target stops being LFO 1's fifth row and becomes the first of the two
    # shared destination rows, directly above Destinations.
    # A 4th element is the "already applied" sentinel. This edit's replacement
    # text is itself rewritten by the always-mounted edits below, so its own
    # `new` cannot be the evidence that it already ran; the surviving evidence
    # is the ADJACENCY it creates -- LFO 2's last row immediately followed by
    # the Target row -- which is exactly what the edit is for.
    ('Target moves below LFO 2 and out of LFO 1 disclosure',
     TARGET_OLD + SEP + LFO2_BLOCK,
     LFO2_BLOCK + SEP + TARGET_NEW,
     'publish("lfo2Depth", 4013, next) }))' + SEP
     + '/* @__PURE__ */ React.createElement(SpectrSettingsField, '
     '{ hidden: !(value.enabled || value.lfo2Enabled), label: "Target"'),

    # The plural of the row above is not a name. Destinations says what it is,
    # and the hint says which way the override runs.
    ('Targets becomes Destinations and names the override',
     'label: "Targets", hint: "Destinations both LFOs modulate"',
     'label: "Destinations", hint: "Both LFOs; overrides Target"'),

    # The row gains a visibility prop. `display: "none"` is the runtime's
    # documented bridge path: the style applier maps it to
    # setVisible(id, false), and View::visible() skips both render and layout,
    # so a hidden row leaves no gap. The component prefix keeps this unique --
    # SettingsModal has its own inline `Field` helper with the same style.
    ('SpectrSettingsField can hide its own row',
     'function SpectrSettingsField({ label, hint, children }) {\n'
     '  return /* @__PURE__ */ React.createElement("div", '
     '{ style: { display: "flex", alignItems: "center", gap: 14 } },',
     'function SpectrSettingsField({ label, hint, children, hidden }) {\n'
     '  return /* @__PURE__ */ React.createElement("div", '
     '{ style: { display: hidden ? "none" : "flex", alignItems: "center", gap: 14 } },'),
]

# Every conditional row becomes an ALWAYS-MOUNTED row with a `hidden` prop, so
# the group's order is fixed at mount and can never depend on the order the
# user flips the toggles in. See the module docstring: the widget bridge has no
# insert-at-index, so a row that mounts later is appended regardless of its
# position in the JSX.
for _label, _pure, _gate in [
        ('Shape', True, 'value.enabled'),
        ('Rate', True, 'value.enabled'),
        ('Depth', True, 'value.enabled'),
        ('LFO 2 shape', False, 'value.lfo2Enabled'),
        ('LFO 2 rate', False, 'value.lfo2Enabled'),
        ('LFO 2 depth', False, 'value.lfo2Enabled'),
        ('Target', True, '(value.enabled || value.lfo2Enabled)'),
        ('Destinations', False, '(value.enabled || value.lfo2Enabled)')]:
    _pure_prefix = '/* @__PURE__ */ ' if _pure else ''
    _call = _pure_prefix + 'React.createElement(SpectrSettingsField, { label: "%s"' % _label
    _hidden = '!' + _gate if _gate.startswith('(') else '!' + _gate
    EDITS.append((
        '%s is always mounted and hidden instead' % _label,
        '%s && %s' % (_gate, _call),
        '%sReact.createElement(SpectrSettingsField, { hidden: %s, label: "%s"'
        % (_pure_prefix, _hidden, _label)))

# The FINAL shape, asserted whole. `SpectrSettingsField` is used only by the
# modulation group (SettingsModal has its own inline `Field` helper), so a
# global forbid on a short-circuited row is exact: after this patch NO row in
# the group may be conditionally mounted, because a row that mounts late is
# appended by the bridge rather than placed.
FORBIDDEN_AFTER = (
    '&& React.createElement(SpectrSettingsField',
    '&& /* @__PURE__ */ React.createElement(SpectrSettingsField',
    'label: "Targets"',
    'hint: "Shape the bank, snapshots, or morph"',
    'hint: "Destinations both LFOs modulate"',
)

REQUIRED_AFTER = (
    # LFO 1 owns exactly Shape / Rate / Depth, each always mounted.
    '/* @__PURE__ */ React.createElement(SpectrSettingsField, { hidden: !value.enabled, label: "Shape"',
    '/* @__PURE__ */ React.createElement(SpectrSettingsField, { hidden: !value.enabled, label: "Rate"',
    '/* @__PURE__ */ React.createElement(SpectrSettingsField, { hidden: !value.enabled, label: "Depth"',
    # LFO 2 owns exactly its own three.
    'React.createElement(SpectrSettingsField, { hidden: !value.lfo2Enabled, label: "LFO 2 shape"',
    'React.createElement(SpectrSettingsField, { hidden: !value.lfo2Enabled, label: "LFO 2 rate"',
    'React.createElement(SpectrSettingsField, { hidden: !value.lfo2Enabled, label: "LFO 2 depth"',
    # Both shared destination rows carry the SAME gate and sit last, adjacent,
    # immediately after LFO 2's last row.
    'React.createElement(SpectrSettingsField, { hidden: !value.lfo2Enabled, label: "LFO 2 depth", '
    'hint: "Modulation amount" }, React.createElement(SpectrSettingsSlider, { value: '
    'value.lfo2Depth || 0, min: 0, max: 1, step: 0.01, onChange: (next) => '
    'publish("lfo2Depth", 4013, next) }))' + SEP
    + '/* @__PURE__ */ React.createElement(SpectrSettingsField, '
    '{ hidden: !(value.enabled || value.lfo2Enabled), label: "Target"',
    'React.createElement(SpectrSettingsField, '
    '{ hidden: !(value.enabled || value.lfo2Enabled), label: "Destinations"',
    # The row can actually hide itself.
    'function SpectrSettingsField({ label, hint, children, hidden })',
    'display: hidden ? "none" : "flex"',
    # Neither control was deleted: both write paths survive. That is the
    # verdict on the Target-vs-Targets report.
    'publish("target", 4004, next)',
    'modulation_targets_set',
)


def escaped(value):
    """The value as it appears inside the document's JSON string payload."""
    return json.dumps(value)[1:-1]


def main():
    for edit in EDITS:
        label, old, new = edit[:3]
        if old and old in new:
            sys.exit('FAIL %s: patch point survives its own replacement' % label)

    raw = open(PATH, encoding='utf-8').read()
    changed = False
    applied = 0
    already = 0
    for edit in EDITS:
        label, old, new = edit[:3]
        sentinel = edit[3] if len(edit) == 4 else new
        old_e, new_e = escaped(old), escaped(new)
        if raw.count(old_e) == 0 and raw.count(escaped(sentinel)) == 1:
            print('already applied  ', label)
            already += 1
            continue
        count = raw.count(old_e)
        if count != 1:
            sys.exit('FAIL %s: patch point occurs %d times, expected 1'
                     % (label, count))
        if raw.count(new_e):
            sys.exit('FAIL %s: replacement text is already present' % label)
        raw = raw.replace(old_e, new_e)
        changed = True
        applied += 1
        print('applied          ', label)

    # A mixed already/applied run is EXPECTED when this script gains an edit
    # and is replayed onto a document that carries the earlier ones -- which is
    # the whole point of it being replayable after a merge conflict. The
    # document is not left half patched, because FORBIDDEN_AFTER and
    # REQUIRED_AFTER below assert the COMPLETE final shape, not just the deltas.

    for token in FORBIDDEN_AFTER:
        count = raw.count(escaped(token))
        if count:
            sys.exit('FAIL: %r still appears %d times after patching'
                     % (token, count))
    for token in REQUIRED_AFTER:
        count = raw.count(escaped(token))
        if count != 1:
            sys.exit('FAIL: %r appears %d times after patching, expected 1'
                     % (token[:72], count))

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
