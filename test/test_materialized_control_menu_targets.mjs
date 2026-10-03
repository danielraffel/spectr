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
import assert from 'node:assert/strict';
import fs from 'node:fs';

const expectFail = process.argv.includes('--expect-fail');
const plantWrongTarget = process.argv.includes('--plant-wrong-target');
const plantNoAccent = process.argv.includes('--plant-no-accent');
const plantEveryAccent = process.argv.includes('--plant-every-accent');

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
if (plantWrongTarget)
  controlMenu = plant(controlMenu, 'freeze: { title: "LIVE / FROZEN", target: 6,',
    'freeze: { title: "LIVE / FROZEN", target: 7,');
if (plantNoAccent)
  bandMenu = plant(bandMenu, 'accent: !!modulationFocus && modulationFocus.target === target,', 'accent: false,');
if (plantEveryAccent)
  bandMenu = plant(bandMenu, 'accent: !!modulationFocus && modulationFocus.target === target,',
    'accent: !!modulationFocus,');

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
  const api = new Function('React', 'window', 'globalThis', 'SpectrSettingsSlider', 'spectrSetFrozen',
    'spectrFreezeStore', 'spectrCommitFreezeLength',
    routeList + '\n' + controlMenu
      + '\nreturn { SPECTR_CONTROL_MENUS, spectrOpenControlMenu, SpectrControlMenu, spectrControlMenuStore };')(
    React, window, g, () => null, () => {}, () => ({}), () => {});
  let tree;
  const render = () => { reset(); tree = expand(api.SpectrControlMenu({ settings: {} })); flush(); };
  return { api, calls, focusCalls, g, render, tree: () => tree };
}

function check() {
  {
    const probe = mountControlMenu({});
    assert.deepEqual(Object.keys(probe.api.SPECTR_CONTROL_MENUS).sort(), Object.keys(EXPECTED).sort(),
      'every control with a menu is covered');
  }

  for (const [control, key] of Object.entries(EXPECTED)) {
    const [target, , label] = routes.find(([, k]) => k === key);
    const modulation = { enabled: true, lfo2Enabled: false };
    const test = mountControlMenu(modulation);
    test.api.spectrOpenControlMenu(control, { currentTarget: { getBoundingClientRect:
      () => ({ left: 200, top: 10, bottom: 40 }) } });
    test.render();
    const rows = () => visible(test.tree());
    const action = (name) => rows().find((n) => n.props['data-spectr-control-action'] === name);
    const menu = rows().find((n) => n.props['data-spectr-control-menu']);
    assert.equal(menu.props['data-spectr-control-menu'], control);
    assert.equal(menu.props['data-spectr-control-menu-target'], key, `${control}: the menu's target`);
    for (const lfo of [1, 2]) {
      const row = action('lfo' + lfo);
      assert(row, `${control}: LFO ${lfo} row`);
      assert.equal(row.props['data-spectr-control-target'], key, `${control}: LFO ${lfo} names ${key}`);
      assert(text(row).includes('LFO ' + lfo + ' → ' + label), `${control}: "${text(row)}" names ${label}`);
      const dot = visible(row).find((n) => n.props['data-spectr-control-lfo-state']);
      assert.equal(dot.props['data-spectr-control-lfo-state'], lfo === 1 ? 'on' : 'off', 'the LFO state shows');
    }
    // Progressive disclosure: no Depth until the route is on.
    assert.equal(action('depth1'), undefined);
    // Toggling writes exactly this target's lane for that LFO, and no other.
    action('lfo1').props.onClick();
    assert.equal(test.calls.length, 1);
    assert.deepEqual(test.calls[0], { key: 'routeOn1_' + target, id: lane(1, target), value: true });
    const others = routes.filter(([t]) => t !== target).map(([t]) => lane(1, t));
    assert(!others.includes(test.calls[0].id), `${control}: the lane is no other target's`);
    modulation['routeOn1_' + target] = true;
    test.render();
    assert(action('depth1'), `${control}: Depth shows under an on route`);
    assert.equal(action('lfo1').props['data-spectr-control-route'], 'on');
    assert.equal(action('lfo1-power'), undefined, 'LFO 1 runs, so no "Turn on" row');
    // LFO 2's route on while LFO 2 is off: says so, and offers to turn it on.
    modulation['routeOn2_' + target] = true;
    test.render();
    assert(text(action('lfo2')).includes('LFO off'));
    action('lfo2-power').props.onClick();
    assert.deepEqual(test.calls.at(-1), { key: 'lfo2Enabled', id: 4010, value: true });
    // "All targets..." opens the full submenu on this target.
    action('all-targets').props.onClick();
    assert.equal(test.focusCalls.length, 1);
    assert.equal(test.focusCalls[0].target, target);
    assert.equal(test.focusCalls[0].key, key);
    assert.equal(test.focusCalls[0].lfo, 1);
    assert.equal(test.api.spectrControlMenuStore().open, null, 'the header menu gives way');
    test.focusCalls[0].back();
    assert.equal(test.api.spectrControlMenuStore().open.control, control, 'Back reopens it');
    // Only LFO 2 drives it: the submenu opens on LFO 2.
    modulation['routeOn1_' + target] = false;
    test.render();
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
      'spectrModulationRouteList', hook + '\n' + bandMenu + '\nreturn ContextMenu;')(React, window,
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
