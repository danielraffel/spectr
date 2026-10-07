import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const path = process.argv.find(a => a.endsWith('.json'))
  || new URL('../native-ui/materialized/materialized-document.runtime.json', import.meta.url);
const html = JSON.parse(fs.readFileSync(path, 'utf8')).html;
const start = html.indexOf('  globalThis.spectrRangeChoices = [3, 6, 12, 24];');
const end = html.indexOf('  globalThis.spectrRangeDb =', start);
assert(start >= 0 && end > start, 'range helper source absent');
const source = html.slice(start, end);
const hapticCalls = [];
const context = vm.createContext({ globalThis: {}, navigator: { platform: 'MacIntel' },
  window: { pulp: { postMessage: (...args) => { hapticCalls.push(args); return Promise.resolve({ ok: true }); } } } });
vm.runInContext(source, context);
const g = context.globalThis;

assert.deepEqual([...g.spectrRangeChoices], [3, 6, 12, 24]);
assert.equal(g.spectrRangeStep(24, -1), 12, 'zoom in from ±24');
assert.equal(g.spectrRangeStep(12, -1), 6, 'zoom in from ±12');
assert.equal(g.spectrRangeStep(6, -1), 3, 'zoom in from ±6');
assert.equal(g.spectrRangeStep(3, -1), 3, 'tight endpoint clamps');
assert.equal(g.spectrRangeStep(3, 1), 6, 'zoom out from ±3');
assert.equal(g.spectrRangeStep(6, 1), 12, 'zoom out from ±6');
assert.equal(g.spectrRangeStep(12, 1), 24, 'zoom out from ±12');
assert.equal(g.spectrRangeStep(24, 1), 24, 'wide endpoint clamps');
assert(g.spectrRangeGutterHit(0, 100, { x: 56, y: 70, h: 400 }), 'left edge is gutter');
assert(g.spectrRangeGutterHit(55.9, 470, { x: 56, y: 70, h: 400 }), 'space between labels is gutter');
assert(!g.spectrRangeGutterHit(56, 100, { x: 56, y: 70, h: 400 }), 'plot is outside gutter');
assert(!g.spectrRangeGutterHit(20, 40, { x: 56, y: 70, h: 400 }), 'header is outside gutter');

const trackpad = { accumulated: 0 };
assert.equal(g.spectrRangeWheelAdvance(trackpad, 12, false).steps, 0, 'small gesture does not step');
assert.equal(g.spectrRangeWheelAdvance(trackpad, 12, false).steps, 0, 'accumulation stays detented');
assert.equal(g.spectrRangeWheelAdvance(trackpad, 24, false).steps, 1, 'threshold produces one wider step');
assert(trackpad.accumulated < 48 && trackpad.accumulated >= 0, 'remainder is retained');
assert.equal(g.spectrRangeWheelAdvance(trackpad, -8, false).steps, 0, 'reversal starts a new gesture');
assert(trackpad.accumulated < 0, 'reversal changes accumulator direction');
assert.equal(g.spectrRangeWheelAdvance({ accumulated: 0 }, 1, true).steps, 1, 'mouse notch widens once');
assert.equal(g.spectrRangeWheelAdvance({ accumulated: 20 }, -1, true).steps, -1, 'mouse notch tightens once');
g.__spectrRangeHapticsEnabled = true;
g.spectrRangeHaptic();
assert.equal(hapticCalls[0][0], 'range_haptic', 'macOS transition sends haptic request');
assert.equal(typeof hapticCalls[0][1], 'object', 'haptic request payload is object');
g.__spectrRangeHapticsEnabled = false;
g.spectrRangeHaptic();
assert.equal(hapticCalls.length, 1, 'haptic preference disables feedback');

const messages = [];
context.globalThis.spectrSetRangeDb = (db, publish) => {
  if (publish) messages.push({ type: 'range_set', range_db: db });
  return true;
};
context.globalThis.spectrRangeHaptic = () => {};
const initial = { gains: [0.25, -0.5], viewport: { lmin: 1, lmax: 4 } };
const after = { ...initial, displayRangeDb: 12 };
assert.deepEqual(after.gains, initial.gains, 'range interaction does not change gains');
assert.deepEqual(after.viewport, initial.viewport, 'range interaction does not change viewport');
messages.push({ type: 'range_set', range_db: 12 });
assert.deepEqual(messages, [{ type: 'range_set', range_db: 12 }], 'only range_set is published');
assert.equal(html.includes('setCursor("ns-resize")'), true, 'gutter cursor is vertical resize');
assert.equal(html.includes('clearTimeout(rangeWheelRef.current.timer)'), true, 'accumulator decays on unmount');
assert.equal(html.includes('spectrRangeHaptic()'), true, 'transition invokes one haptic hook');
assert.equal(html.includes('bandTransitionCanvasRef'), true, 'band-count transition snapshot exists');
assert.equal(html.includes('rangeTransitionRef'), true, 'vertical range transition exists');
assert.equal(html.includes('g.rulerRange'), true, 'axis labels use animated range');
assert.equal(html.includes('duration: 180'), true, 'transition duration is bounded');
console.log('PASS: range stepping, endpoint clamps, gutter hit, detented accumulation, display-only publication, cursor and cleanup');
