import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const path = process.argv.find(a => a.endsWith('.json'))
  || new URL('../native-ui/materialized/materialized-document.runtime.json', import.meta.url);
const html = JSON.parse(fs.readFileSync(path, 'utf8')).html;
const start = html.indexOf('  globalThis.spectrRangeGridTicks =');
const end = html.indexOf('  globalThis.spectrRangeDb =', start);
assert(start >= 0 && end > start, 'vertical range grid helper source absent');
const context = vm.createContext({ globalThis: {} });
vm.runInContext(html.slice(start, end), context);
const ticks = context.globalThis.spectrRangeGridTicks;
assert.deepEqual([...ticks(24).major], [-24, -18, -12, -6, 0, 6, 12, 18, 24]);
assert.deepEqual([...ticks(24).minor], [-24, -21, -18, -15, -12, -9, -6, -3, 0, 3, 6, 9, 12, 15, 18, 21, 24]);
assert.deepEqual([...ticks(3).major], [-3, -2.25, -1.5, -0.75, 0, 0.75, 1.5, 2.25, 3]);
assert.deepEqual([...ticks(3).minor], [-3, -2.625, -2.25, -1.875, -1.5, -1.125, -0.75, -0.375, 0, 0.375, 0.75, 1.125, 1.5, 1.875, 2.25, 2.625, 3]);
assert.deepEqual([...ticks(0).major], []);
assert.deepEqual([...ticks(0).minor], []);
assert(html.includes('Minor divisions make the scale visibly travel'), 'minor grid is rendered');
assert(html.includes('Major divisions align with the numeric labels'), 'major grid is rendered');
assert(html.includes('g.inner.x - 5'), 'major dB ticks extend into the numeric gutter');
assert(html.includes('rangeTransitionRef.current'), 'vertical range transition remains animated');
assert(html.includes('g.rulerRange = visualRange'), 'grid receives the animated range');
console.log('PASS: vertical Range major/minor grid ticks, gutter marks, and animated scale contract');
