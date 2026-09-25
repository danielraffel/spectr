#!/usr/bin/env node
// The band context menu's own Escape and outside-press dismissal, and the
// one-submenu-at-a-time rule, executed against the SHIPPED materialized
// component rather than described.
//
// Why this is not a string check. The defect it guards is a listener
// registered on the wrong OBJECT: `document` receives keydown (the bridge
// calls `document.dispatchEvent` directly) but never receives a pointer
// press, because a press is dispatched on the pressed Element and bubbles the
// `_parentElement` chain, which ends at `document.body`. A grep for
// "addEventListener('pointerdown'" is satisfied by both, and the wrong one is
// what shipped. So the stubs below separate the two objects and only ever
// deliver each event where the real shim delivers it.
//
// Every rule carries a plant that must be REJECTED BY AN ASSERTION. A plant
// that throws anything else died before reaching its rule, and is reported as
// a broken control rather than a pass.
import assert from 'node:assert/strict';
import fs from 'node:fs';

const PLANTS = ['--plant-document-outside', '--plant-menu-only-containment',
                '--plant-stacking-submenus', '--plant-escape-closes-all',
                '--plant-no-escape', '--plant-unanswerable-dismisses'];
// NOT a plant, deliberately: making `< Back` clear only its own flag cannot be
// detected from here, and a control that cannot go red is worse than none.
// Once one-submenu-at-a-time holds, the other flag is already false by the
// time any Back row is reachable, so `closeSubmenus` there is defence in
// depth rather than load-bearing, and this file does not claim otherwise.
const plant = PLANTS.find(name => process.argv.includes(name)) || null;
const expectFail = process.argv.includes('--expect-fail');

const html = JSON.parse(fs.readFileSync(new URL('../native-ui/materialized/materialized-document.runtime.json', import.meta.url), 'utf8')).html;
function extract(from, to) {
  const a = html.indexOf(from), b = html.indexOf(to, a);
  assert(a >= 0 && b > a, `missing source ${from}`);
  return html.slice(a, b);
}
const hook = extract('function useSpectrModulationState()', 'function SpectrModulationSettings()');
let component = extract('function ContextMenu(', 'window.ContextMenu');

// A plant must be a real change to the shipped text. `replaceOnce` refuses a
// needle that is absent or ambiguous, so a plant can never quietly no-op and
// leave the control measuring the unmodified component.
function replaceOnce(from, to) {
  assert.equal(component.split(from).length - 1, 1,
               'plant needle is not unique: ' + from);
  component = component.replace(from, to);
}
if (plant === '--plant-document-outside')
  // The exact defect that shipped: the outside press listened on `document`,
  // which no pointer event ever reaches.
  replaceOnce('body.addEventListener("pointerdown", onOutsidePress, true);',
              'doc.addEventListener("pointerdown", onOutsidePress, true);');
if (plant === '--plant-unanswerable-dismisses')
  // The failure mode that is worse than not dismissing: with no ref able to
  // answer, every press reads as outside and the menu eats its own rows.
  replaceOnce('      return !answered;', '      return false;');
if (plant === '--plant-menu-only-containment')
  replaceOnce('const panels = [ref.current, macrosRef.current, modulationRef.current];',
              'const panels = [ref.current];');
if (plant === '--plant-stacking-submenus')
  replaceOnce('    setMacrosOpen(open => !open);\n    setModulationOpen(false);',
              '    setMacrosOpen(open => !open);');
if (plant === '--plant-stacking-submenus')
  // Two writers now open a submenu -- the hover/ArrowRight helper and the
  // row's own toggle -- and the rule has to hold for both, so the plant
  // breaks both.
  replaceOnce('    setModulationOpen(open);\n    if (open) setMacrosOpen(false);',
              '    setModulationOpen(open);');
if (plant === '--plant-escape-closes-all')
  // The shape that shipped: script ALSO closes the open submenu, so the
  // standalone's overlay route then pops the menu that has become the top and
  // one press retires both layers.
  replaceOnce('        if (!submenu) onClose();',
              '        if (submenu) closeSubmenus(); else onClose();');
if (plant === '--plant-no-escape')
  replaceOnce('doc.addEventListener("keydown", onEscapeKey, true);', '');

// ── A DOM shim that keeps `document` and `document.body` distinct ─────────
//
// Mirrors web-compat.js: `document.dispatchEvent` walks only listeners
// registered on `document`; an element press runs the CAPTURE phase from the
// outermost ancestor down, so a body capture listener sees it before the
// target does.
function makeDom() {
  const docListeners = new Map(), bodyListeners = new Map();
  const add = (map) => (type, fn) => {
    if (!map.has(type)) map.set(type, []);
    map.get(type).push(fn);
  };
  const remove = (map) => (type, fn) => {
    const list = map.get(type) || [];
    const at = list.lastIndexOf(fn);
    if (at >= 0) list.splice(at, 1);
  };
  const node = (name, children = []) => {
    const self = { name, children };
    self.contains = (other) => {
      if (!other) return false;
      if (other === self) return true;
      return self.children.some(child => child.contains
        ? child.contains(other) : child === other);
    };
    return self;
  };
  const body = node('body');
  body.addEventListener = add(bodyListeners);
  body.removeEventListener = remove(bodyListeners);
  const document = {
    body,
    getElementById: () => ({ clientWidth: 1320, clientHeight: 860 }),
    addEventListener: add(docListeners),
    removeEventListener: remove(docListeners),
  };
  return {
    document, body, node,
    // `document.dispatchEvent` only: this is how a keydown arrives.
    key(key) {
      const event = { key, preventDefault() { this.defaultPrevented = true; } };
      for (const fn of [...(docListeners.get('keydown') || [])]) fn(event);
      return event;
    },
    // An element press: capture phase on body, then the target. Nothing on
    // `document` is ever invoked, because nothing invokes it in the shim.
    press(target) {
      const event = { target, type: 'pointerdown' };
      for (const fn of [...(bodyListeners.get('pointerdown') || [])]) fn(event);
      return event;
    },
    docPointerCount: () => (docListeners.get('pointerdown') || []).length,
    live: () => [...docListeners.values(), ...bodyListeners.values()]
      .reduce((total, list) => total + list.length, 0),
  };
}

function mount() {
  const slots = [], effects = [], pending = [];
  let cursor = 0, tree, closed = 0;
  const native = { enabled: false, lfo2_enabled: false, target: 0, target_mask: 1 };
  const listeners = new Map();
  const React = {
    Fragment: 'fragment',
    createElement: (type, props, ...children) => ({ type, props: props || {}, children }),
    useState(initial) {
      const i = cursor++;
      if (!(i in slots)) slots[i] = typeof initial === 'function' ? initial() : initial;
      return [slots[i], value => { slots[i] = typeof value === 'function' ? value(slots[i]) : value; }];
    },
    useRef(value) { const i = cursor++; return slots[i] || (slots[i] = { current: value }); },
    useCallback(fn) { const i = cursor++; return slots[i] || (slots[i] = fn); },
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
  const dom = makeDom();
  const window = { pulp: {
    on(type, callback) {
      if (!listeners.has(type)) listeners.set(type, new Set());
      listeners.get(type).add(callback);
      return () => listeners.get(type).delete(callback);
    },
    postMessage(type) {
      if (type === 'processing_state_get')
        return Promise.resolve({ payload: { modulation: { ...native } } });
      return Promise.resolve({});
    },
  } };
  delete globalThis.__spectrModulationLast;
  // Timers the test advances itself. A hover onto another entry while a
  // submenu is open switches after a grace period; `hover()` models the
  // pointer staying there by running what is due, so the rule under test is
  // the switch itself rather than wall-clock timing.
  const timers = new Map();
  let nextTimer = 1;
  const fakeSetTimeout = (fn) => { const id = nextTimer++; timers.set(id, fn); return id; };
  const fakeClearTimeout = (id) => { timers.delete(id); };
  const flushTimers = () => {
    for (const [id, fn] of [...timers]) { timers.delete(id); fn(); }
  };
  const ContextMenu = new Function('React', 'window', 'document', 'spectrShortcutChipStyle',
    'setTimeout', 'clearTimeout',
    hook + '\n' + component + '\nreturn ContextMenu;')(
      React, window, dom.document, () => ({}), fakeSetTimeout, fakeClearTimeout);
  const noop = () => {};
  const props = { x: 300, y: 300, band: 8, N: 64, selection: new Set([1, 2]),
    macros: [], editMode: 'sculpt', onClose: () => closed++, onEditMode: noop,
    onMuteBand: noop, onZeroBand: noop, onSoloBand: noop, onSelectAll: noop,
    onSelectNone: noop, onZeroSel: noop, onMuteSel: noop, onFitView: noop,
    onUndo: noop, onRedo: noop, onMacroMembers: noop };
  function expand(node) {
    if (Array.isArray(node)) return node.flatMap(expand);
    if (!node || typeof node !== 'object') return node;
    if (typeof node.type === 'function') return expand(node.type(node.props));
    return { ...node, children: node.children.flatMap(expand) };
  }
  function nodes(node, out = []) {
    if (!node || typeof node !== 'object') return out;
    out.push(node);
    for (const child of node.children || []) nodes(child, out);
    return out;
  }
  // Attach a fake DOM node to every panel ref the render produced, and hang
  // it under body, so `contains` answers the way it will in the host. Without
  // this every ref reads null, `inside()` is vacuously false, and the
  // containment rules below would pass no matter what they tested.
  function attachRefs() {
    for (const node of nodes(tree)) {
      const ref = node.props?.ref;
      if (!ref || typeof ref !== 'object') continue;
      if (!ref.current) {
        ref.current = dom.node(node.props['data-spectr-macros-panel'] ? 'macros'
          : node.props['data-spectr-modulation-panel'] ? 'modulation' : 'menu',
          [dom.node('row')]);
        dom.body.children.push(ref.current);
      }
    }
  }
  function render() {
    cursor = 0;
    tree = expand(ContextMenu(props));
    attachRefs();
    pending.splice(0).forEach(fn => fn());
  }
  const button = action => {
    const found = nodes(tree).find(n => n.props['data-spectr-band-action'] === action);
    assert(found, 'visible button ' + action);
    return found;
  };
  const panel = marker => nodes(tree).find(n => n.props[marker]);
  render();
  return {
    dom, button, panel, render,
    get closed() { return closed; },
    click(action) { button(action).props.onClick(); render(); },
    // `onHover` belongs to the Item component; what survives into the tree
    // is the button's own onMouseEnter, which is what a real hover fires.
    hover(action) {
      // The hovered node sits inside the band menu, as a real row does: the
      // menu decides what a hover means from which panel contains the row.
      const inside = panel('data-spectr-band-context-menu')?.props?.ref?.current
        ?.children?.[0] ?? { style: {} };
      if (!inside.style) inside.style = {};
      button(action).props.onMouseEnter({ currentTarget: inside });
      render();
      flushTimers();
      render();
    },
    refOf(marker) { return panel(marker)?.props?.ref?.current ?? null; },
    // Escape as each host delivers it. Both route it to the overlay stack,
    // which pops the TOP claim and fires that claim's onDismiss; an open
    // submenu stacks on the menu, so it is the top. The standalone offers the
    // key to script FIRST (-keyDown: fans out, then routes Escape), and a
    // script state change commits before the route runs; the plugin editor
    // routes Escape to the stack INSTEAD of script.
    escape(host) {
      let event = null;
      if (host === 'standalone') { event = dom.key('Escape'); render(); }
      const top = panel('data-spectr-modulation-panel')
        || panel('data-spectr-macros-panel')
        || panel('data-spectr-band-context-menu');
      if (top && typeof top.props.onDismiss === 'function') top.props.onDismiss();
      render();
      return event;
    },
    unmount() { effects.forEach(effect => effect?.cleanup?.()); },
  };
}

function check() {
  // ── The outside press must arrive where the host delivers it ───────────
  {
    const menu = mount();
    // Nothing is registered on `document` for a pointer press. The shipped
    // defect was exactly that, and it is invisible to a grep.
    assert.equal(menu.dom.docPointerCount(), 0,
                 'the outside-press listener is on `document`, which never receives a press');
    // Control: a press on a node that is NOT the menu closes it. If this
    // reads 0 the instrument is dead and every "did not close" below is
    // meaningless.
    const elsewhere = menu.dom.node('canvas');
    menu.dom.body.children.push(elsewhere);
    menu.dom.press(elsewhere);
    assert.equal(menu.closed, 1, 'a press outside the menu must dismiss it');
    menu.unmount();
  }

  // ── A press inside the menu, or inside an open submenu, is not outside ──
  {
    const menu = mount();
    menu.dom.press(menu.refOf('data-spectr-band-context-menu'));
    assert.equal(menu.closed, 0, 'a press on the menu itself must not dismiss it');
    menu.click('macros-toggle');
    const macros = menu.refOf('data-spectr-macros-panel');
    assert(macros, 'the macros panel mounted');
    menu.dom.press(macros.children[0]);
    assert.equal(menu.closed, 0,
                 'a press inside the open submenu must not dismiss the menu');
    menu.unmount();
  }

  // ── An unanswerable containment question must not dismiss ─────────────
  {
    // Before the refs attach -- the tree not linked to the shim yet -- inside
    // and outside are indistinguishable. Guessing "outside" there closes the
    // menu on its own first row.
    const menu = mount();
    for (const marker of ['data-spectr-band-context-menu'])
      menu.panel(marker).props.ref.current = null;
    const anything = menu.dom.node('unknown');
    menu.dom.press(anything);
    assert.equal(menu.closed, 0,
                 'with nothing able to answer, a press must not dismiss');
    // Control: restore one answerable ref and the same press DOES dismiss,
    // so the rule above is not just a listener that never fires.
    menu.panel('data-spectr-band-context-menu').props.ref.current =
      menu.dom.node('menu', [menu.dom.node('row')]);
    menu.dom.press(anything);
    assert.equal(menu.closed, 1,
                 'control: once a ref can answer, an outside press dismisses');
    menu.unmount();
  }

  // ── Escape retires one layer per press, in both hosts ──────────────────
  for (const host of ['standalone', 'plugin']) {
    for (const toggle of ['modulation-toggle', 'macros-toggle']) {
      const menu = mount();
      const marker = toggle === 'macros-toggle'
        ? 'data-spectr-macros-panel' : 'data-spectr-modulation-panel';
      menu.click(toggle);
      assert(menu.panel(marker), 'submenu open');
      // Every submenu must own its dismissal: the stack pops its claim, and a
      // panel with no onDismiss would stay painted with nothing behind it.
      assert.equal(typeof menu.panel(marker).props.onDismiss, 'function',
                   host + ': the submenu handles its own dismissal');
      const first = menu.escape(host);
      if (first) assert.equal(first.defaultPrevented, true, 'Escape is consumed');
      assert.equal(menu.closed, 0,
                   host + ': the first Escape closes the submenu, not the menu');
      assert.equal(menu.panel(marker), undefined,
                   host + ': the submenu is gone after one Escape');
      assert(menu.panel('data-spectr-band-context-menu'),
             host + ': the band menu is still open');
      menu.escape(host);
      assert(menu.closed >= 1, host + ': the second Escape closes the menu');
      menu.unmount();
    }
  }
  {
    // With no submenu open, one Escape is enough -- from script alone, which
    // is what an Escape that never reaches the overlay stack still needs.
    const menu = mount();
    menu.dom.key('Escape');
    assert.equal(menu.closed, 1, 'Escape closes a menu with no submenu open');
    // A key that is not Escape must not.
    const other = mount();
    other.dom.key('s');
    assert.equal(other.closed, 0, 'only Escape dismisses');
    menu.unmount(); other.unmount();
  }

  // ── One submenu at a time, and Back leaves neither open ────────────────
  {
    const menu = mount();
    menu.click('macros-toggle');
    assert(menu.panel('data-spectr-macros-panel'), 'macros open');
    menu.hover('modulation-toggle');
    assert(menu.panel('data-spectr-modulation-panel'), 'modulation open');
    assert.equal(menu.panel('data-spectr-macros-panel'), undefined,
                 'opening one submenu must replace the other, not stack on it');
    menu.click('modulation-back');
    assert.equal(menu.panel('data-spectr-modulation-panel'), undefined,
                 'Back returns to the parent');
    assert.equal(menu.panel('data-spectr-macros-panel'), undefined,
                 'Back leaves no other panel open');
    assert.equal(menu.closed, 0, 'Back does not dismiss the whole menu');
    // And the reverse order, because the two rows are separate code paths.
    menu.click('modulation-toggle');
    menu.hover('macros-toggle');
    assert(menu.panel('data-spectr-macros-panel'), 'macros open');
    assert.equal(menu.panel('data-spectr-modulation-panel'), undefined,
                 'the other panel closed');
    // The two rows have TWO writers each -- the hover/ArrowRight helper and
    // the row's own toggle -- and the exclusivity rule has to hold for both.
    // Only the hover writer was covered above; this is the toggle one.
    menu.click('modulation-toggle');
    assert(menu.panel('data-spectr-modulation-panel'),
           'the modulation toggle opens its panel');
    menu.click('macros-toggle');
    assert(menu.panel('data-spectr-macros-panel'),
           'the macros toggle opens its panel');
    assert.equal(menu.panel('data-spectr-modulation-panel'), undefined,
                 'a toggle must close the sibling panel too');
    menu.click('macros-back');
    assert.equal(menu.panel('data-spectr-macros-panel'), undefined, 'macros Back returns');
    assert.equal(menu.panel('data-spectr-modulation-panel'), undefined,
                 'macros Back leaves no other panel open');
    menu.unmount();
  }

  // ── The EDIT MODE rows are gone from the executed tree ─────────────────
  {
    const menu = mount();
    // Labels reach the tree as TEXT CHILDREN of the row's span, not as a
    // `label` prop: `expand` has already replaced each Item with the button
    // it renders. Reading `props.label` here finds nothing at all, which is a
    // control that passes for the wrong reason.
    const text = [];
    (function walk(node) {
      if (typeof node === 'string') { text.push(node); return; }
      if (!node || typeof node !== 'object') return;
      for (const child of node.children || []) walk(child);
    })(menu.panel('data-spectr-band-context-menu'));
    // Control: the rows that must still be there.
    assert(text.includes('Undo'), 'control: the Undo row is still rendered');
    assert(text.includes('Macros'), 'control: the Macros row is still rendered');
    for (const gone of ['EDIT MODE', 'Sculpt', 'Level', 'Boost', 'Flare', 'Glide'])
      assert.equal(text.includes(gone), false, gone + ' must not be a menu row');
    menu.unmount();
  }

  // ── Unmounting takes every listener with it ────────────────────────────
  {
    const menu = mount();
    assert(menu.dom.live() > 0, 'control: listeners were registered');
    menu.unmount();
    assert.equal(menu.dom.live(), 0, 'no listener outlives the menu');
  }
}

if (!expectFail) {
  check();
  console.log('PASS: the band menu owns Escape and the outside press, keeps one submenu at a time, and carries no edit-mode rows');
} else {
  assert(plant, '--expect-fail needs a --plant-* to reject');
  let error = null;
  try { check(); } catch (caught) { error = caught; }
  assert(error, 'expected ' + plant + ' to be rejected, nothing failed');
  assert.equal(error.code, 'ERR_ASSERTION',
               plant + ' threw ' + error.name + ', not an assertion: ' + error.message);
  console.log('PASS (negative control): ' + plant + ' rejected by an assertion -- ' + error.message.split('\n')[0]);
}
