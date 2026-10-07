import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const path = process.argv.find(a => a.endsWith('.json'))
  || new URL('../native-ui/materialized/materialized-document.runtime.json', import.meta.url);
const html = JSON.parse(fs.readFileSync(path, 'utf8')).html;
const start = html.indexOf('    const TIP_DELAY_MS = 500;');
const end = html.indexOf('  }, []);', start);
assert(start >= 0 && end > start, 'shipping tooltip effect absent');
let source = html.slice(start, end);
if (process.argv.includes('--plant-no-press-suppression')) source = source.replace('suppressed = true;', 'suppressed = false;');
if (process.argv.includes('--plant-ignore-popups')) source = source.replace(/popupOpen\(\)/g, 'false');

function rig() {
  let now = 0, next = 1, shown = null, popup = false, cleanup;
  const timers = new Map(), listeners = new Map();
  const box = { left: 400, top: 10, bottom: 34, width: 80 };
  const cluster = { offsetWidth: 1320, getBoundingClientRect: () => ({ left: 500, top: 10 }) };
  const anchor = { getBoundingClientRect: () => box };
  const document = {
    querySelector(selector) {
      if (selector === '[data-spectr-output-cluster]') return cluster;
      if (selector.startsWith('[data-spectr-overlay=')) return popup ? {} : null;
      return anchor;
    },
    addEventListener(type, fn, capture) {
      assert.equal(capture, true);
      listeners.set(type, fn);
    },
    removeEventListener(type, fn, capture) {
      assert.equal(capture, true);
      assert.equal(listeners.get(type), fn);
      listeners.delete(type);
    },
  };
  const context = vm.createContext({ document, tipTimerRef: { current: 0 },
    setTip: value => { shown = value; },
    setTimeout(fn, delay) { const id = next++; timers.set(id, { at: now + delay, fn }); return id; },
    clearTimeout(id) { timers.delete(id); },
  });
  cleanup = vm.runInContext('(function(){' + source + '})()', context);
  return {
    request: () => context.spectrHeaderTip('Length', '[data-spectr-freeze-length]'),
    hide: () => context.spectrHeaderTipHide(),
    event(type, data = {}) { listeners.get(type)?.(data); },
    popup(value) { popup = value; },
    shown: () => shown,
    tick(ms) {
      now += ms;
      for (const [id, timer] of timers) if (timer.at <= now) { timers.delete(id); timer.fn(); }
    },
    cleanup() { cleanup(); assert.equal(listeners.size, 0); assert.equal(timers.size, 0); },
  };
}

const run = () => {
  const r = rig();
  r.event('pointermove', { clientX: 440, clientY: 20 });
  r.request(); r.tick(499); assert.equal(r.shown(), null, 'normal hover delay');
  r.tick(1); assert.equal(r.shown().text, 'Length');
  r.event('pointerdown', { clientX: 440, clientY: 20 });
  assert.equal(r.shown(), null, 'press hides existing tip');
  r.request(); r.tick(1000); assert.equal(r.shown(), null, 'synthetic enter after click stays suppressed');
  r.event('pointermove', { clientX: 441, clientY: 21 });
  r.request(); r.tick(500); assert.equal(r.shown(), null, 'pointer jitter does not rearm');
  r.event('pointermove', { clientX: 450, clientY: 20 });
  r.request(); r.tick(500); assert(r.shown(), 'actual movement rearms normal hover');
  r.hide(); r.request(); r.tick(200);
  r.event('click', { clientX: 450, clientY: 20 });
  r.tick(500); assert.equal(r.shown(), null, 'press cancels pending hover');
  r.event('pointermove', { clientX: 460, clientY: 20 });
  r.popup(true); r.request(); r.tick(500);
  assert.equal(r.shown(), null, 'open dropdown blocks requests');
  r.popup(false); r.request(); r.tick(200); r.popup(true); r.tick(300);
  assert.equal(r.shown(), null, 'dropdown opening during delay blocks stale timer');
  r.popup(false); r.request(); r.tick(500); assert(r.shown());
  r.event('contextmenu', { clientX: 460, clientY: 20 });
  r.request(); r.tick(500); assert.equal(r.shown(), null, 'context interaction suppresses');
  r.event('pointermove', { clientX: 470, clientY: 20 });
  r.request(); r.tick(500); assert(r.shown());
  r.event('wheel'); assert.equal(r.shown(), null, 'scroll dismisses');
  r.request(); r.cleanup();
};
try {
  run();
  if (process.argv.includes('--expect-fail')) throw new Error('negative control passed');
  console.log('PASS: hover delay, press suppression, jitter, popup lifecycle, context click, scroll, teardown');
} catch (error) {
  if (process.argv.includes('--expect-fail') && !String(error).includes('negative control passed')) {
    console.log('PASS: negative control rejected: ' + error.message);
  } else { throw error; }
}
