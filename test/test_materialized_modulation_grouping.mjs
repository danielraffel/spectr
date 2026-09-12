#!/usr/bin/env node
// Proves the shipping Settings > MODULATION group is grouped PER LFO, and that
// the two shared destination rows sit last, once, under one shared gate.
//
// The user's report: "in settings can we put the LFO1 settings UNDER the LFO
// section when enabled? AND put LFO2 settings under LFO2 -- currently we stack
// the lfo1/2 toggles and then put both their settings on top, it's hard to
// read/see." And: "I have no idea what Target vs Targets ... seems duplicative."
//
// Reordering the JSX alone does NOT fix that report, and this suite exists
// because of why. The native runtime does not render children in DOM order:
// attach() in native-ui/materialized/runtime.js computes the right insert
// index, keeps childIds and the DOM shim in that order, then calls
// materialize() -> createWidget(type, id, parentId, props) WITHOUT the index.
// The widget bridge has no insert-at-index and no move -- probed live against
// the shipping native editor, globalThis.insertChild and globalThis.moveWidget
// are both undefined while removeWidget/setVisible/setStyle/createCol are
// functions -- so a row that mounts LATE is APPENDED wherever it sits in the
// JSX. Both LFO toggles are unconditional, so they took the first two slots and
// every row revealed by a toggle landed after them, in flip order. That is
// exactly "we stack the lfo1/2 toggles and then put both their settings on top".
//
// So every row is mounted once, in the wanted order, and the enable state
// drives VISIBILITY (`hidden` -> display:none -> setVisible(id,false)) instead
// of mounting. The invariant this suite protects is therefore stronger than an
// order check: the label sequence must be IDENTICAL in all four enable states.
//
// Neither destination control is vestigial, so neither is deleted. They write
// the SAME field at two authority levels and
// resolve_modulation_target_mask (include/spectr/modulation.hpp) is the single
// decider: the `target_mask == 0xFF` sentinel means "follow the `target` enum",
// anything else means "use the mask". `Target` is kParamLfoTarget (4004), a
// registered host-automatable Enum parameter and the only destination lane a
// DAW can automate; `Destinations` is `target_mask`, editor-only state written
// by the `modulation_targets_set` bridge message and the only way to select
// more than one destination. src/spectr.cpp copies the whole settings struct
// into the second LFO's pass, overriding only shape/rate/depth, so BOTH are
// shared by BOTH LFOs -- which is why neither belongs inside one LFO's
// disclosure, and why gating `Target` on LFO 1 was the actual defect.
//
// Three gates, because the cheap ones are known to lie about this document:
//
//   PARSE  dynamic import() of the whole script block, discriminating on
//          SyntaxError. `node --check` is not a syntax checker in general, and
//          it is measurably blind to the --plant-scope defect below (it exits 0
//          on it), so it cannot stand in for either gate here.
//   SCOPE  `new Function` proves syntax, not scope: a block-scoped declaration
//          in the wrong body compiles clean. So the component is CALLED, with
//          stubs, and a free identifier surfaces as a ReferenceError instead of
//          passing silently.
//   ORDER  the rendered children are walked for all four combinations of the
//          two LFO enables and compared against the expected label sequence.
//          This is the gate the user's first report is about.
//
// Usage:
//   node test_materialized_modulation_grouping.mjs <runtime.json>
//        [--plant-order] [--plant-scope] [--plant-homograph] [--expect-fail]
//
// --plant-order moves `Target` back above the LFO 2 block (the exact pre-fix
// arrangement). --plant-scope replaces a bound identifier in an LFO 2 row with
// a free one. --plant-homograph restores the `Targets` label. --expect-fail
// inverts the verdict, so a control row is green only when the planted defect
// is REJECTED, and a missing file or a thrown extractor still fails it.
//
// Exit 0 pass, 1 fail, 2 usage/extraction error. Never 77: this needs no
// device, no GPU and no build, so a skip here would be a bug.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const args = process.argv.slice(2);
const plantOrder = args.includes('--plant-order');
const plantRemount = args.includes('--plant-remount');
const plantScope = args.includes('--plant-scope');
const plantHomograph = args.includes('--plant-homograph');
const expectFail = args.includes('--expect-fail');
const documentPath = args.find((a) => !a.startsWith('--'));

if (!documentPath) {
  console.error('usage: test_materialized_modulation_grouping.mjs <runtime.json> '
    + '[--plant-order] [--plant-remount] [--plant-scope] [--plant-homograph] '
    + '[--expect-fail]');
  process.exit(2);
}

let html;
try {
  html = JSON.parse(fs.readFileSync(documentPath, 'utf8')).html;
} catch (error) {
  console.error('FAIL: cannot read ' + documentPath + ': ' + error.message);
  process.exit(2);
}
if (typeof html !== 'string' || html.length === 0) {
  console.error('FAIL: ' + documentPath + ' has no usable "html" field');
  process.exit(2);
}

const failures = [];
function fail(message) { failures.push(message); console.log('FAIL  ' + message); }
function pass(message) { console.log('ok    ' + message); }

// ---------------------------------------------------------------- the plants
// A plant that rewrites nothing proves nothing, so each asserts its own hit
// count before the measurement runs.

function replaceExactlyOnce(source, needle, replacement, label) {
  const n = source.split(needle).length - 1;
  if (n !== 1) {
    console.error('FAIL ' + label + ': anchor occurs ' + n + ' times, expected 1');
    process.exit(2);
  }
  return source.replace(needle, replacement);
}

const SEP = ',\n    ';
const TARGET_ROW =
  '/* @__PURE__ */ React.createElement(SpectrSettingsField, { hidden: '
  + '!(value.enabled || value.lfo2Enabled), label: "Target", '
  + 'hint: "Automatable; clears Destinations" }, '
  + '/* @__PURE__ */ React.createElement(SpectrSettingsChips, { value: value.target, '
  + 'onChange: (next) => publish("target", 4004, next), '
  + 'opts: [[0,"Bank"],[1,"A"],[2,"B"],[3,"Morph"] ] }))';
const LFO2_TOGGLE_ROW =
  'React.createElement(SpectrSettingsField, { label: "LFO 2", hint: "Enable second '
  + 'modulation source" }, React.createElement(SpectrSettingsToggle, { value: '
  + 'value.lfo2Enabled || false, onChange: (next) => publish("lfo2Enabled", 4010, next) }))';

if (plantOrder) {
  // The pre-fix arrangement: Target sits inside LFO 1's run, above LFO 2, so
  // the shared destination rows are no longer last and adjacent.
  html = replaceExactlyOnce(html, SEP + TARGET_ROW, '', 'plant-order lift');
  html = replaceExactlyOnce(html, LFO2_TOGGLE_ROW, TARGET_ROW + SEP + LFO2_TOGGLE_ROW,
                            'plant-order drop');
}
if (plantRemount) {
  // The regression this whole suite exists to prevent: put LFO 2's rows back
  // on a conditional mount. In the browser that is invisible -- the rows still
  // appear in the right place -- but in the native runtime the bridge appends
  // a late-mounted widget, so the group re-scrambles the moment the user flips
  // the toggle. Here it shows up as a label sequence that CHANGES with state,
  // which is exactly the property the fix buys.
  for (const label of ['LFO 2 shape', 'LFO 2 rate', 'LFO 2 depth']) {
    html = replaceExactlyOnce(
      html,
      'React.createElement(SpectrSettingsField, { hidden: !value.lfo2Enabled, label: "'
        + label + '"',
      'value.lfo2Enabled && React.createElement(SpectrSettingsField, { label: "'
        + label + '"',
      'plant-remount ' + label);
  }
}
if (plantScope) {
  // Free identifier in an LFO 2 row. Parses clean; only running it catches it.
  html = replaceExactlyOnce(html, 'value: value.lfo2Depth || 0',
                            'value: lfo2DepthUndeclared || 0', 'plant-scope');
}
if (plantHomograph) {
  html = replaceExactlyOnce(html, 'label: "Destinations", hint: "Both LFOs; overrides Target"',
                            'label: "Targets", hint: "Destinations both LFOs modulate"',
                            'plant-homograph');
}

// ---------------------------------------------------------------- extraction

const NEEDLE = 'function SpectrModulationSettings() {';
const start = html.indexOf(NEEDLE);
if (start < 0) { console.error('FAIL: SpectrModulationSettings is absent'); process.exit(2); }
const end = html.indexOf('\nfunction SettingsModal(', start);
if (end < 0) { console.error('FAIL: cannot find the end of SpectrModulationSettings'); process.exit(2); }
const componentSource = html.slice(start, end);

// The whole script block the component lives in, so PARSE covers more than the
// function this patch touched.
const openTag = '<script type="text/javascript">';
const blockStart = html.lastIndexOf(openTag, start) + openTag.length;
const blockEnd = html.indexOf('</script>', start);
const blockSource = html.slice(blockStart, blockEnd);
if (blockSource.length < componentSource.length) {
  console.error('FAIL: script block extraction is smaller than the component');
  process.exit(2);
}

// ------------------------------------------------------------- gate 1: PARSE

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-modgroup-'));
const modulePath = path.join(tmp, 'block.mjs');
fs.writeFileSync(modulePath, blockSource, 'utf8');
try {
  await import(pathToFileURL(modulePath).href);
  pass('PARSE: script block parsed and executed');
} catch (error) {
  if (error instanceof SyntaxError) fail('PARSE: SyntaxError: ' + error.message);
  else pass('PARSE: parsed (ran to ' + error.constructor.name + ', expected without a DOM)');
}

// ------------------------------------------------- gates 2 and 3: SCOPE/ORDER

// A recording createElement. Children are kept verbatim, including the `false`
// a short-circuited row evaluates to, so a row gated OFF stays distinguishable
// from a row that was never emitted.
function makeReact(state) {
  return {
    createElement: (type, props, ...children) => ({type, props: props || {}, children}),
    useState: (initial) => [Object.assign({}, initial, state), () => {}],
    useRef: (initial) => ({current: initial}),
    useCallback: (fn) => fn,
    useEffect: () => {},
  };
}

function renderWith(state) {
  const factory = new Function(
    'React', 'window', 'console',
    'SpectrSettingsGroup', 'SpectrSettingsField', 'SpectrSettingsChips',
    'SpectrSettingsSlider', 'SpectrSettingsToggle',
    componentSource + '\nreturn SpectrModulationSettings;');
  return factory(
    makeReact(state), {pulp: {postMessage: () => Promise.resolve()}}, console,
    'GROUP', 'FIELD', 'CHIPS', 'SLIDER', 'TOGGLE')();
}

function rowsOf(tree) {
  const group = tree.children.find((child) => child && child.type === 'GROUP');
  if (!group) return null;
  return group.children
    .filter((child) => child && child.type === 'FIELD')
    .map((child) => ({label: child.props.label, hidden: child.props.hidden === true}));
}

// Every row, in the one order the group must always have. A row that is not
// mounted at all fails this outright, which is what catches --plant-remount.
const ORDER = ['LFO', 'Shape', 'Rate', 'Depth',
               'LFO 2', 'LFO 2 shape', 'LFO 2 rate', 'LFO 2 depth',
               'Target', 'Destinations'];

// Which rows the user should SEE in each enable state. LFO 1's three follow
// LFO 1, LFO 2's three follow LFO 2, and the two shared destination rows show
// whenever either LFO is on -- they are shared by both (src/spectr.cpp copies
// the whole settings struct into the second LFO's pass).
const visibleFor = (lfo1, lfo2) => (label) => {
  if (label === 'LFO' || label === 'LFO 2') return true;
  if (label === 'Target' || label === 'Destinations') return lfo1 || lfo2;
  return label.startsWith('LFO 2 ') ? lfo2 : lfo1;
};

const CASES = [
  {lfo1: false, lfo2: false},
  {lfo1: true, lfo2: false},
  {lfo1: false, lfo2: true},
  {lfo1: true, lfo2: true},
];

const sequences = [];
for (const {lfo1, lfo2} of CASES) {
  const name = 'lfo1=' + lfo1 + ' lfo2=' + lfo2;
  let rows;
  try {
    rows = rowsOf(renderWith({enabled: lfo1, lfo2Enabled: lfo2}));
  } catch (error) {
    // A free identifier lands here. This is the gate `new Function` alone
    // cannot provide: the component has to RUN for scope to be exercised.
    fail('SCOPE: ' + name + ' threw ' + error.constructor.name + ': ' + error.message);
    continue;
  }
  if (!rows) { fail('ORDER: ' + name + ' rendered no modulation group'); continue; }

  const got = rows.map((row) => row.label);
  sequences.push(got.join(' > '));
  if (got.join(' > ') === ORDER.join(' > ')) pass('ORDER: ' + name + ' matches the fixed row order');
  else fail('ORDER: ' + name + '\n        want ' + ORDER.join(' > ')
            + '\n        got  ' + got.join(' > '));

  const wantVisible = visibleFor(lfo1, lfo2);
  const wrong = rows.filter((row) => row.hidden === wantVisible(row.label));
  if (wrong.length === 0) {
    pass('VISIBILITY: ' + name + '  shown: '
         + rows.filter((r) => !r.hidden).map((r) => r.label).join(', '));
  } else {
    fail('VISIBILITY: ' + name + ' wrong for '
         + wrong.map((r) => r.label + (r.hidden ? ' (hidden, want shown)'
                                                : ' (shown, want hidden)')).join('; '));
  }
}

// THE invariant the fix buys, stated on its own: the row order cannot depend on
// which toggles the user has flipped, because the bridge appends a widget that
// mounts late and cannot move it afterwards.
if (sequences.length === CASES.length && new Set(sequences).size === 1)
  pass('MOUNT-ORDER: the row sequence is identical in all four enable states');
else
  fail('MOUNT-ORDER: the row sequence changes with enable state ('
       + new Set(sequences).size + ' distinct sequences); a late-mounting row '
       + 'is appended by the native bridge, not placed');

// The regrouping invariants, stated independently of the table above so a
// future edit that changes one case cannot quietly satisfy the suite.
let both = [];
try {
  both = (rowsOf(renderWith({enabled: true, lfo2Enabled: true})) || []).map((r) => r.label);
} catch (error) {
  // Already reported by the SCOPE gate above. Swallowing it here keeps the
  // verdict (and --expect-fail) in this script's hands rather than letting an
  // unhandled throw decide the exit code for us.
}
const at = (label) => both.indexOf(label);

if (at('LFO') === 0) pass('GROUPING: LFO 1 opens the group');
else fail('GROUPING: LFO 1 does not open the group');
if (at('Depth') > at('LFO') && at('Depth') < at('LFO 2'))
  pass("GROUPING: LFO 1's settings sit between the two toggles");
else fail("GROUPING: LFO 1's settings are not between the two toggles");
if (at('LFO 2 depth') > at('LFO 2') && at('LFO 2 shape') === at('LFO 2') + 1)
  pass("GROUPING: LFO 2's settings follow its own toggle");
else fail("GROUPING: LFO 2's settings do not follow its own toggle");
if (at('Target') === at('LFO 2 depth') + 1 && at('Destinations') === at('Target') + 1)
  pass('GROUPING: the two shared destination rows sit last, adjacent');
else fail('GROUPING: the shared destination rows are not last and adjacent');
if (at('Targets') === -1) pass('NAMING: the Target/Targets homograph is gone');
else fail('NAMING: a row is still labelled "Targets"');

// Neither control was deleted: both write paths survive. That is the verdict on
// the Target-vs-Targets report, asserted rather than asserted in prose.
if (componentSource.includes('publish("target", 4004, next)'))
  pass('CAPABILITY: Target still writes kParamLfoTarget (4004), the automatable lane');
else fail('CAPABILITY: the Target parameter write is gone');
if (componentSource.includes('modulation_targets_set'))
  pass('CAPABILITY: Destinations still publishes the target mask');
else fail('CAPABILITY: the destination-mask write is gone');

fs.rmSync(tmp, {recursive: true, force: true});

if (expectFail) {
  if (failures.length) {
    console.log('\nOK (control): the planted defect was REJECTED by '
      + failures.length + ' gate(s)');
    process.exit(0);
  }
  console.log('\nFAIL (control): the planted defect passed every gate');
  process.exit(1);
}
if (failures.length) {
  console.log('\n' + failures.length + ' FAILURE(S)');
  process.exit(1);
}
console.log('\nOK: the modulation group is grouped per LFO and its shared '
  + 'destination rows are last');
process.exit(0);
