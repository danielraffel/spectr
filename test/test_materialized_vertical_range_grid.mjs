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
// Reference lines are anchored to absolute dB values, not fractions of Range.
for (const range of [24, 12, 6, 3, 9]) {
  const { major, minor } = ticks(range);
  assert([...minor].includes(1.5), 'the same +1.5 dB reference survives every scale');
  assert([...minor].includes(-1.5), 'grid remains symmetric about zero');
  assert([...major, ...minor].every(db => Math.abs(db) <= range));
}
// Execute the actual painter and inspect screen-space strokes rather than
// trusting helper values or source markers. This catches range cancellation.
const gridStart = html.indexOf('  function drawGrid(ctx, g) {');
const gridEnd = html.indexOf('  function drawSpectrum(', gridStart);
assert(gridStart >= 0 && gridEnd > gridStart);
context.globalThis.spectrRangeDb = () => 24;
context.modulationViewportRef = { current: { lmin: 1, lmax: 4 } };
context.viewRef = { current: context.modulationViewportRef.current };
vm.runInContext(html.slice(gridStart, gridEnd) + '\nglobalThis.paintGrid = drawGrid;', context);
function horizontalLines(range) {
  const lines = [];
  let from, to;
  const ctx = {
    save() {}, restore() {}, beginPath() { from = to = null; },
    rect() {}, clip() {}, setLineDash() {},
    moveTo(x, y) { from = [x, y]; }, lineTo(x, y) { to = [x, y]; },
    stroke() {
      if (from && to && from[1] === to[1] && to[0] - from[0] === 600)
        lines.push(from[1]);
    }
  };
  context.globalThis.paintGrid(ctx, {
    inner: { x: 50, y: 20, w: 600, h: 400 },
    zeroY: 220, plotHalfH: 200, halfH: 200, rulerRange: range
  });
  return lines;
}
let previousDistance = 0;
let previousLines;
for (const range of [24, 12, 6, 3]) {
  const lines = horizontalLines(range);
  const distance = Math.min(...lines.filter(y => y < 220).map(y => 220.5 - y));
  assert(distance > previousDistance, `grid spreads outward at ±${range}`);
  if (previousLines) assert.notDeepEqual(lines, previousLines, 'settled grid positions change');
  previousDistance = distance;
  previousLines = lines;
}
// During the existing ease transition, a retained reference travels smoothly
// outward with tighter scale instead of being regenerated at a fixed fraction.
let previousY = Infinity;
for (const range of [12, 11, 10, 9, 8, 7, 6]) {
  const expectedY = 220 - 1.5 / range * 200 + 0.5;
  assert(horizontalLines(range).some(y => Math.abs(y - expectedY) < 1e-6));
  assert(expectedY < previousY);
  previousY = expectedY;
}
assert.deepEqual([...ticks(0).major], []);
assert.deepEqual([...ticks(0).minor], []);
assert(html.includes('Minor divisions make the scale visibly travel'), 'minor grid is rendered');
assert(html.includes('Fixed dB reference divisions'), 'major grid is rendered');
assert(html.includes('g.inner.x - 5'), 'major dB ticks extend into the numeric gutter');
assert(html.includes('rangeTransitionRef.current'), 'vertical range transition remains animated');
assert(html.includes('g.rulerRange = visualRange'), 'grid receives the animated range');
console.log('PASS: vertical Range major/minor grid ticks, gutter marks, and animated scale contract');
