import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = JSON.parse(fs.readFileSync(new URL('../native-ui/materialized/materialized-document.runtime.json', import.meta.url), 'utf8')).html;
function extract(start, end) {
  const a = html.indexOf(start), b = html.indexOf(end, a);
  assert(a >= 0 && b > a, `missing source ${start}`);
  return html.slice(a, b);
}
const hook = extract('function useSpectrModulationState()', 'function SpectrModulationSettings()');
let component = extract('function ContextMenu(', 'window.ContextMenu');
if (process.argv.includes('--plant-inverted-toggle')) {
  const before = 'publishModulation("enabled", 4000, !modulation.enabled)';
  assert.equal(component.split(before).length - 1, 1);
  component = component.replace(before, 'publishModulation("enabled", 4000, modulation.enabled)');
}

function mount(initial) {
  const slots = [], effects = [], pending = [], calls = [], listeners = new Map();
  let cursor = 0, tree, closed = 0;
  const native = { enabled: false, lfo2_enabled: false, target: 0, target_mask: 1, ...initial };
  const React = {
    Fragment: 'fragment',
    createElement: (type, props, ...children) => ({ type, props: props || {}, children }),
    useState(initialValue) {
      const i = cursor++;
      if (!(i in slots)) slots[i] = initialValue;
      return [slots[i], value => { slots[i] = typeof value === 'function' ? value(slots[i]) : value; }];
    },
    useRef(value) {
      const i = cursor++;
      return slots[i] || (slots[i] = { current: value });
    },
    useCallback(fn) { cursor++; return fn; },
    useEffect(fn, deps) {
      const i = cursor++, old = effects[i];
      if (!old || deps.some((dep, index) => dep !== old.deps[index])) {
        old?.cleanup?.();
        effects[i] = { deps };
        pending.push(() => { effects[i].cleanup = fn(); });
      }
    },
  };
  // Callback identities are stable, as React.useCallback(..., []) promises.
  React.useCallback = fn => {
    const i = cursor++;
    return slots[i] || (slots[i] = fn);
  };
  const emit = () => {
    for (const callback of listeners.get('processing_state_live') || []) callback({ payload: { modulation: { ...native } } });
  };
  const window = { pulp: {
    on(type, callback) {
      if (!listeners.has(type)) listeners.set(type, new Set());
      listeners.get(type).add(callback);
      return () => listeners.get(type).delete(callback);
    },
    postMessage(type, payload) {
      calls.push({ type, payload });
      if (type === 'processing_state_get') return Promise.resolve({ payload: { modulation: { ...native } } });
      if (type === 'param_set') {
        if (payload.id === 4000) native.enabled = payload.value === 1;
        if (payload.id === 4010) native.lfo2_enabled = payload.value === 1;
        if (payload.id === 4004) { native.target = payload.value; native.target_mask = 1 << payload.value; }
        emit();
      }
      return Promise.resolve({});
    },
  } };
  const ContextMenu = new Function('React', 'window', 'document', 'spectrShortcutChipStyle',
    hook + '\n' + component + '\nreturn ContextMenu;')(React, window,
      { getElementById: () => ({ clientWidth: 1320, clientHeight: 860 }) }, () => ({}));
  const noop = () => {};
  const props = { x: 300, y: 300, band: 8, N: 64, selection: new Set(), macros: [],
    onClose: () => closed++, onMuteBand: noop, onZeroBand: noop, onSoloBand: noop,
    onSelectAll: noop, onSelectNone: noop, onZeroSel: noop, onMuteSel: noop,
    onFitView: noop, onUndo: noop, onRedo: noop, onMacroMembers: noop };
  function expand(node) {
    if (Array.isArray(node)) return node.flatMap(expand);
    if (!node || typeof node !== 'object') return node;
    if (typeof node.type === 'function') return expand(node.type(node.props));
    return { ...node, children: node.children.flatMap(expand) };
  }
  function render() { cursor = 0; tree = expand(ContextMenu(props)); pending.splice(0).forEach(fn => fn()); }
  function nodes(node, visible = true, output = []) {
    if (!node || typeof node !== 'object') return output;
    visible = visible && node.props?.style?.display !== 'none';
    if (visible) output.push(node);
    for (const child of node.children || []) nodes(child, visible, output);
    return output;
  }
  const button = action => {
    const result = nodes(tree).find(node => node.props['data-spectr-band-action'] === action);
    assert(result, `visible button ${action}`);
    return result;
  };
  const click = action => { button(action).props.onClick(); render(); };
  render();
  return { button, click, calls, native, listeners, get closed() { return closed; },
    async settle() { await Promise.resolve(); await Promise.resolve(); render(); },
    external(value) { Object.assign(native, value); emit(); render(); },
    unmount() { effects.forEach(effect => effect?.cleanup?.()); },
  };
}

for (const enabled of [false, true]) for (const lfo2_enabled of [false, true]) {
  const test = mount({ enabled, lfo2_enabled });
  test.click('modulation-toggle');
  assert.equal(test.closed, 0);
  assert.equal(test.button('lfo1-enable').props.disabled, true);
  test.click('lfo1-enable');
  assert.equal(test.calls.filter(call => call.type === 'param_set').length, 0);
  await test.settle();
  assert.equal(test.button('lfo1-enable').props['aria-checked'], enabled);
  assert.equal(test.button('lfo2-enable').props['aria-checked'], lfo2_enabled);
  test.click('lfo1-enable');
  assert.deepEqual(test.calls.at(-1), { type: 'param_set', payload: { id: 4000, value: enabled ? 0 : 1 } });
  test.click('lfo2-enable');
  assert.deepEqual(test.calls.at(-1), { type: 'param_set', payload: { id: 4010, value: lfo2_enabled ? 0 : 1 } });
  assert.equal(test.closed, 0);
  test.external({ enabled, lfo2_enabled, target: 2, target_mask: 4 });
  assert.equal(test.button('lfo1-enable').props['aria-checked'], enabled);
  assert.equal(test.button('lfo2-enable').props['aria-checked'], lfo2_enabled);
  assert.equal(test.button('modulation-target-b').props['aria-checked'], true);
  test.click('modulation-back');
  test.button('modulation-toggle');
  assert.equal(test.closed, 0);
  test.click('modulation-toggle');
  test.click('modulation-target-morph');
  assert.deepEqual(test.calls.at(-1), { type: 'param_set', payload: { id: 4004, value: 3 } });
  assert.equal(test.closed, 1);
  test.unmount();
  assert([...test.listeners.values()].every(set => set.size === 0));
  const reopened = mount(test.native);
  await reopened.settle();
  reopened.click('modulation-toggle');
  assert.equal(reopened.button('modulation-target-morph').props['aria-checked'], true);
  reopened.unmount();
}
console.log('PASS: actual shared hook and button handlers; four initial states, disabled hydration, both toggles, live updates, navigation, targets, reopen, cleanup');
