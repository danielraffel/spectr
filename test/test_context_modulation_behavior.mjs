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
assert.equal(component.includes('sub: "›"'), true);
if (process.argv.includes('--plant-inverted-toggle')) {
  const before = 'publishModulation("enabled", 4000, !modulation.enabled)';
  assert.equal(component.split(before).length - 1, 1);
  component = component.replace(before, 'publishModulation("enabled", 4000, modulation.enabled)');
}

function mount(initial, height = 860, keepSeed = false) {
  // The last native frame is remembered process-wide, so a consumer that
  // mounts later starts live instead of stale. Every case below is a COLD
  // consumer unless it says otherwise -- without this, each case after the
  // first would quietly be exercising the warm path and the disabled-until-
  // hydrated assertions would stop meaning anything.
  if (!keepSeed) delete globalThis.__spectrModulationLast;
  const slots = [], effects = [], pending = [], calls = [], listeners = new Map();
  let cursor = 0, tree, closed = 0;
  const native = { enabled: false, lfo2_enabled: false, target: 0, target_mask: 1, ...initial };
  const React = {
    Fragment: 'fragment',
    createElement: (type, props, ...children) => ({ type, props: props || {}, children }),
    useState(initialValue) {
      const i = cursor++;
      // React calls a function initializer instead of storing it. Storing it
      // does not throw -- the state becomes a FUNCTION, which is truthy, so a
      // `ready`-style flag reads as set and a gated row comes up enabled for
      // entirely the wrong reason.
      if (!(i in slots))
        slots[i] = typeof initialValue === 'function' ? initialValue() : initialValue;
      return [slots[i], value => { slots[i] = typeof value === 'function' ? value(slots[i]) : value; }];
    },
    useRef(value) {
      const i = cursor++;
      return slots[i] || (slots[i] = { current: value });
    },
    useCallback(fn) { cursor++; return fn; },
    // The menu measures its own height in a layout effect. A hook that is
    // absent from this stub does not degrade -- it throws, before a single
    // assertion runs -- and it must still take a slot, or every hook declared
    // after it reads another hook's state.
    useLayoutEffect(fn) { cursor++; fn(); },
    useEffect(fn, deps) {
      const i = cursor++, old = effects[i];
      // No dependency array means "after every render", as in React.
      if (!old || !deps || !old.deps
          || deps.some((dep, index) => dep !== old.deps[index])) {
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
      { getElementById: () => ({ clientWidth: 1320, clientHeight: height }) }, () => ({}));
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
  const menu = () => nodes(tree).find(node => node.props['data-spectr-band-context-menu']);
  const submenu = () => nodes(tree).find(node => node.props['data-spectr-modulation-panel']);
  const click = action => { button(action).props.onClick(); render(); };
  render();
  assert.equal(menu().props.style.zIndex, 2147483001);
  // The menu container is deliberately NOT capped. Pulp does not scroll an
  // overflow container -- `overflow: scroll` is treated like `hidden` with no
  // scrollbar -- so a `maxHeight` does not scroll the rows, it makes Yoga
  // SHRINK them until a section header sits on top of a button and the first
  // row stops being pressable. `tools/patch_materialized_band_menu_no_cap.py`
  // removed the cap for that measured reason, and
  // `test_materialized_band_menu_fit.mjs` holds the same contract; these two
  // lines asserted the opposite and had never once passed.
  //
  // Flip them back to `overflowY: 'auto'` and the menuMaxHeight expression
  // below once Generous-Corp/pulp#8602 lands real scrolling for an overflow
  // container -- at which point capping this panel becomes correct again.
  assert.equal(menu().props.style.overflowY, undefined);
  assert.equal(menu().props.style.maxHeight, undefined);
  return { button, click, calls, native, listeners, get closed() { return closed; },
    menu, submenu,
    async settle() { await Promise.resolve(); await Promise.resolve(); render(); },
    external(value) { Object.assign(native, value); emit(); render(); },
    unmount() { effects.forEach(effect => effect?.cleanup?.()); },
  };
}

for (const enabled of [false, true]) for (const lfo2_enabled of [false, true]) {
  const test = mount({ enabled, lfo2_enabled });
  test.click('modulation-toggle');
  assert.equal(test.closed, 0);
  assert.equal(test.button('modulation-toggle').props['aria-haspopup'], 'menu');
  assert.equal(test.button('modulation-toggle').props['aria-expanded'], true);
  // The submenu IS capped, and short enough that the shrink does not bite.
  // Derived, not the literal 780, so it follows the mount's viewport.
  assert.equal(test.submenu().props.style.maxHeight,
               Math.max(120, Math.max(24, 860 - 64) - 16));
  assert.equal(test.submenu().props.style.overflowY, 'auto');
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
  // Target writes are deliberately non dismissive: Spotify-style submenu
  // navigation lets a user audition several targets without reopening it.
  assert.equal(test.closed, 0);
  assert.equal(test.button('modulation-target-morph').props['aria-checked'], true);
  test.click('modulation-back');
  assert.equal(test.closed, 0);
  test.unmount();
  assert([...test.listeners.values()].every(set => set.size === 0));
  const reopened = mount(test.native);
  await reopened.settle();
  reopened.click('modulation-toggle');
  assert.equal(reopened.button('modulation-target-morph').props['aria-checked'], true);
  reopened.unmount();
}
// A viewport small enough that a cap would certainly bite is where a
// reintroduced one would show up first, so the no-cap contract is worth
// asserting here specifically and not only at 860. Same #8602 condition as
// above: when scrolling lands, this becomes
// `Math.max(120, Math.max(24, 240 - 64) - 16)` again.
// The defect this guards, reported from Logic: the band menu mounts a FRESH
// hook instance every time it opens, so it came up `ready: false` holding a
// default value and its six gated rows stayed disabled until its own round
// trip landed. Tapping LFO 1 straight after opening the menu did nothing,
// while the Settings panel -- same hook, never reads `ready` -- worked.
// Once any frame has been seen, a later consumer must come up live AND
// toggle from the value native last reported, not from a default.
{
  const seen = mount({ enabled: true, lfo2_enabled: false });
  await seen.settle();
  const reopened = mount({ enabled: true, lfo2_enabled: false }, 860, true);
  reopened.click('modulation-toggle');
  assert.equal(reopened.button('lfo1-enable').props.disabled, false);
  assert.equal(reopened.button('lfo1-enable').props['aria-checked'], true);
  reopened.click('lfo1-enable');
  const writes = reopened.calls.filter(call => call.type === 'param_set'
                                            && call.payload.id === 4000);
  assert.equal(writes.length, 1);
  // Toggled from the reported value, not from the default `false`.
  assert.equal(writes[0].payload.value, 0);
  seen.unmount();
  reopened.unmount();
}

const compact = mount({ enabled: false, lfo2_enabled: false }, 240);
assert.equal(compact.menu().props.style.maxHeight, undefined);
assert.equal(compact.menu().props.style.overflowY, undefined);
console.log('PASS: actual shared hook and button handlers; four initial states, disabled hydration, both toggles, live updates, navigation, targets, reopen, cleanup');
