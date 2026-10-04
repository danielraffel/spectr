#!/usr/bin/env node
// A header control's context menu names its own LFO target, toggles exactly
// that target's lane, and opens the full Modulation submenu marked on it.
//
// Executes the shipped components out of the materialized document under a
// minimal React stub, rather than grepping them: a row that names the wrong
// target, or an accent that lands on every row, is invisible to a text search.
//
// Plants (each must be REJECTED by an assertion; --expect-fail inverts the
// verdict so the ctest row is green only when that happens):
//   --plant-wrong-target  LIVE / FROZEN's menu points at Length's target.
//   --plant-no-accent     the full submenu marks no target row.
//   --plant-every-accent  the full submenu marks every target row.
//   --plant-loose-hold    Hold for Length is a loose row, not nested under the route.
//   --plant-lfo2-shape    the shared head writes LFO 2's Shape to LFO 1's lane.
//   --plant-head-copy     the header menu builds its own head instead of the shared one.
import assert from 'node:assert/strict';
import fs from 'node:fs';

const expectFail = process.argv.includes('--expect-fail');
const plantWrongTarget = process.argv.includes('--plant-wrong-target');
const plantNoAccent = process.argv.includes('--plant-no-accent');
const plantEveryAccent = process.argv.includes('--plant-every-accent');
const plantLooseHold = process.argv.includes('--plant-loose-hold');
const plantLfo2Shape = process.argv.includes('--plant-lfo2-shape');
const plantHeadCopy = process.argv.includes('--plant-head-copy');

const documentPath = process.argv.find((arg) => arg.endsWith('.json'))
  || new URL('../native-ui/materialized/materialized-document.runtime.json', import.meta.url);
const html = JSON.parse(fs.readFileSync(documentPath, 'utf8')).html;
function extract(start, end) {
  const a = html.indexOf(start), b = html.indexOf(end, a);
  assert(a >= 0 && b > a, `missing source ${start}`);
  return html.slice(a, b);
}
function plant(source, before, after) {
  assert.equal(source.split(before).length - 1, 1, `plant anchor is not unique: ${before}`);
  return source.replace(before, after);
}

let controlMenu = extract('const SPECTR_CONTROL_MENUS = {', 'function SpectrModulationOverrideDialog(');
const routeList = extract('function spectrModulationRouteList()', '// ── __spectrStructuralTargets');
const routeLane = extract('globalThis.spectrRouteLane = ', '\n  globalThis.__spectrNativeDispatchTrace');
const hook = extract('function useSpectrModulationState()', 'function SpectrModulationSettings()');
let bandMenu = extract('function ContextMenu(', 'window.ContextMenu');
let shared = extract('function spectrMenuKit(', 'window.ContextMenu');
if (plantWrongTarget)
  controlMenu = plant(controlMenu, 'freeze: { title: "LIVE / FROZEN", target: 6,',
    'freeze: { title: "LIVE / FROZEN", target: 7,');
if (plantNoAccent)
  bandMenu = plant(bandMenu, 'accent: !!modulationFocus && modulationFocus.target === target,', 'accent: false,');
if (plantEveryAccent)
  bandMenu = plant(bandMenu, 'accent: !!modulationFocus && modulationFocus.target === target,',
    'accent: !!modulationFocus,');
if (plantLooseHold) {
  // The Hold row moved out of the route's children, to sit after the route.
  controlMenu = plant(controlMenu, '            onClick: () => publish("holdForLength", 4140, modulation.holdForLength !== true) }))));',
    '            onClick: () => publish("holdForLength", 4140, modulation.holdForLength !== true) }) && null)),\n'
    + '        open.control === "freeze" && Item({ action: lfo === 1 ? "hold-for-length" : "hold-for-length-2", label: "Hold for Length", keepOpen: true, onClick: () => {} }));');
}
if (plantLfo2Shape)
  shared = plant(shared, 'modulationSource === 1 ? 4001 : 4011, index)', 'modulationSource === 1 ? 4001 : 4001, index)');
if (plantHeadCopy)
  controlMenu = plant(controlMenu, 'items.push(...spectrModulationHeadRows({', 'items.push(...spectrHeaderOwnHead({');
function makeReact() {
  const slots = [], effects = [], pending = [];
  let cursor = 0;
  const React = {
    Fragment: 'fragment',
    createElement: (type, props, ...children) => ({ type, props: props || {}, children }),
    useState(initialValue) {
      const i = cursor++;
      if (!(i in slots)) slots[i] = typeof initialValue === 'function' ? initialValue() : initialValue;
      return [slots[i], (value) => { slots[i] = typeof value === 'function' ? value(slots[i]) : value; }];
    },
    useRef(value) { const i = cursor++; return slots[i] || (slots[i] = { current: value }); },
    useCallback(fn) { const i = cursor++; return slots[i] || (slots[i] = fn); },
    useLayoutEffect(fn) { cursor++; fn(); },
    useEffect(fn, deps) {
      const i = cursor++, old = effects[i];
      if (!old || !deps || !old.deps || deps.some((dep, index) => dep !== old.deps[index])) {
        old?.cleanup?.();
        effects[i] = { deps };
        pending.push(() => { effects[i].cleanup = fn(); });
      }
    },
  };
  return { React, reset: () => { cursor = 0; }, flush: () => pending.splice(0).forEach((fn) => fn()) };
}
function expand(node) {
  if (Array.isArray(node)) return node.flatMap(expand);
  if (!node || typeof node !== 'object') return node;
  if (typeof node.type === 'function') return expand(node.type(node.props));
  return { ...node, children: (node.children || []).flatMap(expand) };
}
function visible(node, output = [], shown = true) {
  if (!node || typeof node !== 'object') return output;
  shown = shown && node.props?.style?.display !== 'none';
  if (shown) output.push(node);
  for (const child of node.children || []) visible(child, output, shown);
  return output;
}
const text = (node) => (node && typeof node === 'object')
  ? (node.children || []).map(text).join('') : (node === null || node === undefined || node === false ? '' : String(node));

const routes = new Function(routeList + '\nreturn spectrModulationRouteList();')();
const lane = (() => { const g = {}; new Function('globalThis', routeLane)(g); return g.spectrRouteLane; })();

// ── The header menu ──────────────────────────────────────────────────────
// Every control that has a menu, and the target it must name.
const EXPECTED = { freeze: 'freeze', mix: 'mix', intensity: 'intensity', output: 'output',
                   length: 'length', bands: 'bands', morph: 'morph', preset: 'preset' };

function mountControlMenu(modulation) {
  const { React, reset, flush } = makeReact();
  const calls = [], focusCalls = [];
  const g = { spectrRouteLane: lane, spectrOpenModulationFocus: (f) => focusCalls.push(f) };
  const window = {
    useSpectrModulationState: () => ({ value: modulation, ready: true,
      publish: (key, id, value) => calls.push({ key, id, value }) }),
  };
  // The plant's private head: the same rows, built without the shared helper.
  const ownHead = 'function spectrHeaderOwnHead(o) { return spectrModulationHeadRows(o).slice(0, 3); }\n';
  const api = new Function('React', 'window', 'globalThis', 'SpectrSettingsSlider', 'spectrSetFrozen',
    'spectrFreezeStore', 'spectrCommitFreezeLength', 'spectrShortcutChipStyle', 'document',
    routeList + '\n' + shared + '\n' + ownHead + controlMenu
      + '\nreturn { SPECTR_CONTROL_MENUS, spectrOpenControlMenu, SpectrControlMenu, spectrControlMenuStore };')(
    React, window, g, () => null, () => {}, () => ({}), () => {}, () => ({}), undefined);
  let tree;
  const render = () => { reset(); tree = expand(api.SpectrControlMenu({ settings: {} })); flush(); };
  return { api, calls, focusCalls, g, render, tree: () => tree };
}

function check() {
  // One head, two menus: both build it with the shared helper.
  assert.match(bandMenu, /\.\.\.spectrModulationHeadRows\(\{/, 'the band menu builds the shared head');
  assert.match(controlMenu, /items\.push\(\.\.\.spectrModulationHeadRows\(\{/, 'the header menu builds the shared head');
  {
    const probe = mountControlMenu({});
    assert.deepEqual(Object.keys(probe.api.SPECTR_CONTROL_MENUS).sort(), Object.keys(EXPECTED).sort(),
      'every control with a menu is covered');
  }

  for (const [control, key] of Object.entries(EXPECTED)) {
    const [target, , label] = routes.find(([, k]) => k === key);
    const modulation = { enabled: true, lfo2Enabled: false, shape: 0, lfo2Shape: 0, rate: 4, lfo2Rate: 4 };
    const test = mountControlMenu(modulation);
    test.api.spectrOpenControlMenu(control, { currentTarget: { getBoundingClientRect:
      () => ({ left: 200, top: 10, bottom: 40 }) } });
    test.render();
    const rows = () => visible(test.tree());
    const all = () => { const out = []; const walk = (n) => { if (!n || typeof n !== 'object') return; out.push(n); (n.children || []).forEach(walk); }; walk(test.tree()); return out; };
    const action = (name) => rows().find((n) => n.props['data-spectr-control-action'] === name);
    const mounted = (name) => all().find((n) => n.props && n.props['data-spectr-control-action'] === name);
    const menu = rows().find((n) => n.props['data-spectr-control-menu']);
    assert.equal(menu.props['data-spectr-control-menu'], control);
    assert.equal(menu.props['data-spectr-control-menu-target'], key, `${control}: the menu's target`);
    // The Modulation head: both LFO switches, the EDIT tabs, Shape and Rate.
    assert(action('lfo1-enable') && action('lfo2-enable'), `${control}: the LFO switches`);
    action('lfo1-enable').props.onClick();
    assert.deepEqual(test.calls.at(-1), { key: 'enabled', id: 4000, value: false });
    action('lfo2-enable').props.onClick();
    assert.deepEqual(test.calls.at(-1), { key: 'lfo2Enabled', id: 4010, value: true });
    const tab = (n) => rows().find((node) => node.props['data-spectr-modulation-source-action'] === n);
    assert.equal(tab(1).props['aria-pressed'], true, `${control}: nothing drives it, so EDIT LFO 1`);
    // Shape and Rate write the edited LFO's lanes.
    const shapeNode = {}; action('lfo1-shape').props.ref(shapeNode); shapeNode.__spectrSliderStep(1);
    assert.deepEqual(test.calls.at(-1), { key: 'shape', id: 4001, value: 1 });
    const rateNode = {}; action('lfo1-rate').props.ref(rateNode); rateNode.__spectrSliderStep(1);
    assert.deepEqual(test.calls.at(-1), { key: 'rate', id: 4002, value: 8 });
    tab(2).props.onClick(); test.render();
    assert.equal(tab(2).props['aria-pressed'], true);
    assert.equal(action('lfo1-shape'), undefined);
    const shape2 = {}; action('lfo2-shape').props.ref(shape2); shape2.__spectrSliderStep(1);
    assert.deepEqual(test.calls.at(-1), { key: 'lfo2Shape', id: 4011, value: 1 }, `${control}: LFO 2 Shape`);
    const rate2 = {}; action('lfo2-rate').props.ref(rate2); rate2.__spectrSliderStep(-1);
    assert.deepEqual(test.calls.at(-1), { key: 'lfo2Rate', id: 4012, value: 2 }, `${control}: LFO 2 Rate`);
    // Only this control's own target: two route rows, both naming it.
    const routeRows = rows().filter((n) => /^lfo[12]$/.test(String(n.props['data-spectr-control-action'] || '')));
    assert.equal(routeRows.length, 2, `${control}: one route row per LFO and no other target`);
    assert.equal(rows().filter((n) => n.props['data-spectr-control-target']).length, 2);
    for (const lfo of [1, 2]) {
      const wrap = rows().find((n) => n.props['data-spectr-control-route-lfo'] === lfo);
      assert.equal(wrap.props['data-spectr-control-target'], key, `${control}: LFO ${lfo} names ${key}`);
      assert(text(action('lfo' + lfo)).includes('LFO ' + lfo + ' → ' + label), `${control}: "${text(action('lfo' + lfo))}" names ${label}`);
    }
    assert.equal(mounted('lfo1-power'), undefined, 'no "Turn on" row: the LFO switches cover it');
    assert.equal(mounted('ask-before-override'), undefined, 'no OPTIONS: Ask before overriding is a Setting');
    assert(!all().some((n) => n.props && n.props['data-spectr-menu-section'] && text(n) === 'OPTIONS'),
      'no OPTIONS section');
    // Progressive disclosure: the route's children are mounted, hidden while off.
    assert(mounted('depth1'), 'Depth is mounted');
    assert.equal(action('depth1'), undefined, 'and hidden while the route is off');
    // Toggling writes exactly this target's lane for that LFO, and no other.
    const before = test.calls.length;
    action('lfo1').props.onClick();
    assert.equal(test.calls.length, before + 1);
    assert.deepEqual(test.calls.at(-1), { key: 'routeOn1_' + target, id: lane(1, target), value: true });
    const others = routes.filter(([t]) => t !== target).map(([t]) => lane(1, t));
    assert(!others.includes(test.calls.at(-1).id), `${control}: the lane is no other target's`);
    modulation['routeOn1_' + target] = true;
    test.render();
    // The children sit under the route, behind the guide line.
    const children = rows().find((n) => n.props['data-spectr-control-route-children'] === 1);
    assert(children, `${control}: LFO 1's route children are disclosed`);
    assert.match(String(children.props.style.borderLeft), /1px solid/);
    assert(children.props.style.marginLeft > 0);
    assert(visible(children).includes(action('depth1')), 'Depth is nested under its route');
    if (control === 'freeze') {
      assert(action('hold-for-length'), 'Hold for Length under the Freeze route');
      assert(visible(children).includes(action('hold-for-length')), 'Hold for Length is nested under the route');
      action('hold-for-length').props.onClick();
      assert.deepEqual(test.calls.at(-1), { key: 'holdForLength', id: 4140, value: true });
    } else {
      assert.equal(mounted('hold-for-length'), undefined, `${control}: Hold for Length is Freeze's alone`);
    }
    // The depth writes this route's amount lane.
    const depthNode = {}; action('depth1').props.ref(depthNode); depthNode.__spectrSliderStep(1);
    assert.deepEqual(test.calls.at(-1), { key: 'routeAmt1_' + target, id: lane(1, target) + 10, value: 0.51 });
    // LFO 2's route on while LFO 2 is off: the row says so.
    modulation.lfo2Enabled = false;
    modulation['routeOn2_' + target] = true;
    test.render();
    assert(text(action('lfo2')).includes('LFO off'));
    // "All targets..." opens the full submenu on this target.
    action('all-targets').props.onClick();
    assert.equal(test.focusCalls.length, 1);
    assert.equal(test.focusCalls[0].target, target);
    assert.equal(test.focusCalls[0].key, key);
    assert.equal(test.focusCalls[0].lfo, 1);
    assert.equal(test.api.spectrControlMenuStore().open, null, 'the header menu gives way');
    test.focusCalls[0].back();
    assert.equal(test.api.spectrControlMenuStore().open.control, control, 'Back reopens it');
    // Only LFO 2 drives it: the EDIT tab and the full submenu open on LFO 2.
    modulation['routeOn1_' + target] = false;
    test.render();
    assert.equal(tab(2).props['aria-pressed'], true, `${control}: LFO 2 drives it, so EDIT LFO 2`);
    action('all-targets').props.onClick();
    assert.equal(test.focusCalls.at(-1).lfo, 2);
  }

  // ── The full submenu, opened on a target ─────────────────────────────────
  function mountBandMenu(modulationFocus) {
    const { React, reset, flush } = makeReact();
    let closed = 0;
    globalThis.__spectrModulationLast = { enabled: true };
    const window = { pulp: { on: () => () => {}, postMessage: () => Promise.resolve({}) } };
    const ContextMenu = new Function('React', 'window', 'document', 'spectrShortcutChipStyle',
      'spectrModulationRouteList', hook + '\n' + shared + '\n' + bandMenu + '\nreturn ContextMenu;')(React, window,
      { getElementById: () => ({ clientWidth: 1320, clientHeight: 860 }) }, () => ({}),
      () => routes);
    const noop = () => {};
    const props = { x: 300, y: 300, band: -1, N: 64, selection: new Set(), macros: [],
      onClose: () => closed++, onMuteBand: noop, onZeroBand: noop, onSoloBand: noop,
      onSelectAll: noop, onSelectNone: noop, onZeroSel: noop, onMuteSel: noop,
      onFitView: noop, onUndo: noop, onRedo: noop, onMacroMembers: noop, modulationFocus };
    let tree;
    const render = () => { reset(); tree = expand(ContextMenu(props)); flush(); };
    render();
    render();  // the commit after mount, where a focused menu opens its submenu
    return { tree: () => tree, render, get closed() { return closed; } };
  }

  for (const [, key, label] of routes) {
    const target = routes.find(([, k]) => k === key)[0];
    let backs = 0;
    const test = mountBandMenu({ target, key, lfo: 2, left: 180, top: 52, title: 'TITLE',
                                 back: () => backs++ });
    const rows = visible(test.tree());
    const panel = rows.find((n) => n.props['data-spectr-modulation-panel']);
    assert(panel, `${key}: the Modulation submenu opens as the menu mounts`);
    assert.equal(panel.props.style.left, 180, 'it sits where the header menu was');
    const root = rows.find((n) => n.props['data-spectr-band-context-menu']);
    assert.equal(root.props.style.left, -4000, 'the band menu itself is parked off-screen');
    const source = rows.find((n) => n.props['data-spectr-modulation-source']);
    assert.equal(source.props['data-spectr-modulation-source'], 2, 'on the LFO that drives it');
    const marked = rows.filter((n) => n.props['data-spectr-modulation-focus'] === 'true');
    assert.equal(marked.length, 1, `${key}: exactly one row is marked`);
    assert.equal(marked[0].props['data-spectr-band-action'], 'modulation-target-' + key);
    assert(text(marked[0]).startsWith(label));
    assert.match(String(marked[0].props.style.borderLeft), /2px solid/);
    const back = rows.find((n) => n.props['data-spectr-band-action'] === 'modulation-back');
    assert(text(back).includes('TITLE'));
    back.props.onClick();
    assert.equal(test.closed, 1);
    assert.equal(backs, 1, 'Back returns to the header menu');
  }
  {
    // Opened from the band menu, nothing is marked and the menu is on screen.
    const test = mountBandMenu(null);
    const rows = visible(test.tree());
    assert.equal(rows.filter((n) => n.props['data-spectr-modulation-focus'] === 'true').length, 0);
    assert.equal(rows.find((n) => n.props['data-spectr-modulation-panel']), undefined);
    assert.notEqual(rows.find((n) => n.props['data-spectr-band-context-menu']).props.style.left, -4000);
  }
}

try {
  check();
} catch (error) {
  if (expectFail && error instanceof assert.AssertionError) {
    console.log('plant rejected:', error.message.split('\n')[0]);
    process.exit(0);
  }
  throw error;
}
if (expectFail) {
  console.error('FAIL: the plant was not rejected');
  process.exit(1);
}
console.log('control menu targets: ok');
