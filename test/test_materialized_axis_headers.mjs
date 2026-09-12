// The two graph axis headings must be column headers over their own rulers.
//
// The user reported both as "text on the graph": `dB (gain)` was painted LEFT
// aligned at the plot's left edge while the gain ticks it names are RIGHT
// aligned 8px outside it, and `dBFS (analyzer)` was painted RIGHT aligned at
// the plot's right edge while the analyzer ticks are LEFT aligned 8px outside
// that. Each heading therefore sat inside the plot, horizontally divorced from
// the column it names. A third string, `SPECTRAL - mask`, floated inside the
// plot naming a mode the toolbar control already names.
//
// This suite asserts the fixed shape against the SHIPPING artifact -- the
// materialized runtime document the native host loads -- and it asserts it by
// EXECUTION, not by grep. Two reasons that matters:
//
//   * `node --check` is not a syntax checker (it exits 0 on broken input), and
//     a static match cannot tell whether the identifiers a body closes over
//     resolve. So every JavaScript block is really parsed, discriminating on
//     the error CLASS -- a SyntaxError is a parse failure; a ReferenceError
//     for `window` or `React` is a browser module parsed fine in a bare realm.
//   * The alignment claim is about painted geometry, which only running
//     `drawRulers` can produce. It runs against a recording 2D-context stub
//     and the emitted fillText calls are measured directly.
//
// The measurement is deliberately RELATIVE and unit-free: a heading passes
// when it shares BOTH its anchor x and its textAlign with a tick unique to its
// own column. Both halves are load-bearing -- an equal x under a different
// alignment puts the glyphs on opposite sides of the anchor. Nothing here
// pins a pixel constant, so the assertion holds at every window size and band
// count rather than drifting on the next resize; it is checked at four sizes
// to make that concrete.
//
// usage: node test_materialized_axis_headers.mjs <document.json>
//          [--plant syntax|align|caption|nudge] [--expect-fail]
// exit 0 pass | 1 fail | 2 the harness could not run
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

const argv = process.argv.slice(2);
const docPath = argv.find((a) => !a.startsWith('--'));
if (!docPath) {
  console.error('usage: node test_materialized_axis_headers.mjs <document.json>');
  process.exit(2);
}
const plantIndex = argv.indexOf('--plant');
const plant = plantIndex >= 0 ? argv[plantIndex + 1] : null;
const expectFail = argv.includes('--expect-fail');

let html;
try {
  html = JSON.parse(readFileSync(docPath, 'utf8')).html;
} catch (e) {
  console.error(`FAIL: cannot read the document: ${e.message}`);
  process.exit(2);
}
if (typeof html !== 'string') {
  console.error('FAIL: the document carries no html payload');
  process.exit(2);
}

// Each plant restores a specific half of the pre-fix shape, so a failure names
// which assertion is load bearing rather than tripping all of them at once.
// Every plant asserts its needle matched exactly once: a drifted needle must
// fail the control, not quietly re-test the current document.
const PLANTS = {
  syntax: ['function drawRulers(ctx, g) {',
           'function drawRulers(ctx, g) { const ] = 1;'],
  // Restores the whole pre-fix heading: caption text back, left aligned,
  // anchored at the plot edge.
  align: ['ctx.fillText("dB", inner.x - 8, g.inner.y - 8);',
          'ctx.textAlign = "left";\n'
          + '    ctx.fillText("dB (gain)", inner.x, g.inner.y - 8);'],
  // Keeps the heading text and its alignment and moves it 8px. `align` above
  // fails on the label text, so on its own it never exercises the geometry
  // assertion; this plant makes that assertion the only thing that can fire.
  nudge: ['ctx.fillText("dB", inner.x - 8, g.inner.y - 8);',
          'ctx.fillText("dB", inner.x, g.inner.y - 8);'],
  caption: ['    ctx.restore();\n  }\n  function drawSelection(ctx, g) {',
            '    ctx.fillText("SPECTRAL · mask", inner.x + 8, inner.y + 6);\n'
            + '    ctx.restore();\n  }\n  function drawSelection(ctx, g) {'],
};
if (plant) {
  const rule = PLANTS[plant];
  if (!rule) {
    console.error(`FAIL: unknown --plant ${plant}`);
    process.exit(2);
  }
  const hits = html.split(rule[0]).length - 1;
  if (hits !== 1) {
    console.error(`FAIL: plant needle '${plant}' matched ${hits} times, expected 1`
                  + ' -- the control would not test what it claims');
    process.exit(2);
  }
  html = html.replace(rule[0], rule[1]);
}

let failures = 0;
const fail = (message) => { ++failures; console.log(`  FAIL  ${message}`); };

// ---- 1. every JavaScript block really parses -----------------------------
// Only `text/javascript` (and untyped) blocks are code. The
// `application/json` tweak-defaults block is data; handing it to a JS parser
// yields a SyntaxError that says nothing about the code.
const blocks = [];
for (const m of html.matchAll(/<script([^>]*)>([\s\S]*?)<\/script>/g)) {
  const type = (m[1].match(/type="([^"]*)"/) || [, ''])[1];
  if (type && type !== 'text/javascript') continue;
  blocks.push(m[2]);
}
if (blocks.length === 0) {
  console.error('FAIL: no JavaScript blocks found -- the instrument is aimed wrong');
  process.exit(2);
}
const dir = mkdtempSync(join(tmpdir(), 'spectr-axis-'));
let parsed = 0;
for (let i = 0; i < blocks.length; ++i) {
  const file = join(dir, `block-${String(i).padStart(2, '0')}.mjs`);
  writeFileSync(file, blocks[i]);
  try {
    await import(pathToFileURL(file).href);
    ++parsed;
  } catch (e) {
    if (e instanceof SyntaxError) fail(`block ${i} does not parse :: ${e.message}`);
    else ++parsed;  // parsed fine; threw because this realm has no browser
  }
}
rmSync(dir, { recursive: true, force: true });
console.log(`parse: ${parsed}/${blocks.length} JavaScript blocks parse`);

// ---- 2. run drawRulers and measure what it paints ------------------------
const start = html.indexOf('function drawRulers(ctx, g) {');
if (start < 0) {
  console.error('FAIL: drawRulers is not in the document -- the instrument is aimed wrong');
  process.exit(2);
}
let depth = 0, end = -1;
for (let i = html.indexOf('{', start); i < html.length; ++i) {
  if (html[i] === '{') ++depth;
  else if (html[i] === '}' && --depth === 0) { end = i + 1; break; }
}
const rulerSource = html.slice(start, end);

function paint(width, height) {
  const inner = { x: 56, y: 70, w: width - 112, h: height - 190 };
  const g = { inner, zeroY: inner.y + inner.h / 2, halfH: inner.h / 2 };
  const calls = [];
  let align = 'start';
  const ctx = {
    save() {}, restore() {}, beginPath() {}, moveTo() {}, lineTo() {},
    stroke() {}, set font(_) {}, set fillStyle(_) {}, set strokeStyle(_) {},
    set lineWidth(_) {}, set textBaseline(_) {},
    set textAlign(v) { align = v; }, get textAlign() { return align; },
    fillText(text, x, y) { calls.push({ text, x, y, align }); },
  };
  const view = { lmin: Math.log10(20), lmax: Math.log10(2e4) };
  const window = {
    SpectrFreq: { fmt: (f) => String(f) },
    SpectrAnalyzer: {
      scale: () => ({ floor: -120, ceiling: 24 }),
      normalizeDb: (db) => (db + 120) / 144,
      project: (a, zeroY, halfH) => zeroY - a * halfH * 0.95,
    },
  };
  let drawRulers;
  try {
    drawRulers = new Function('view', 'window', `${rulerSource}; return drawRulers;`)(
      view, window);
  } catch (e) {
    return { error: e };
  }
  try {
    drawRulers(ctx, g);
  } catch (e) {
    return { error: e };
  }
  return { calls };
}

// 990x645 is the shipping design size (SET-9). The rest are sizes the user
// actually drags to; the assertion must not know the difference.
for (const [w, h] of [[990, 645], [1320, 860], [1600, 1000], [760, 520]]) {
  const { calls, error } = paint(w, h);
  if (error) {
    // A scope defect -- a declaration in the wrong body, a free identifier
    // that does not resolve -- parses clean and only shows up here.
    fail(`${w}x${h}: drawRulers threw :: ${error.constructor.name}: ${error.message}`);
    continue;
  }
  const only = (text) => {
    const hits = calls.filter((c) => c.text === text);
    if (hits.length !== 1) {
      fail(`${w}x${h}: expected exactly one '${text}' label, found ${hits.length}`);
      return null;
    }
    return hits[0];
  };
  // "+18" is unique to the gain column and "-120" to the analyzer column --
  // the analyzer ruler only emits -120/-90/-60/-30/0/+24, the gain ruler only
  // spans +/-24 -- so neither anchor can be confused for the other's.
  const dB = only('dB'), gainTick = only('+18');
  const dBFS = only('dBFS'), analyzerTick = only('-120');
  const topTick = calls.find((c) => c.text === '+24');
  if (!dB || !gainTick || !dBFS || !analyzerTick || !topTick) continue;

  const report = (name, head, tick) => {
    const dx = head.x - tick.x;
    const aligned = dx === 0 && head.align === tick.align;
    console.log(`  ${w}x${h} ${name}: heading x=${head.x} align=${head.align}`
      + ` | tick '${tick.text}' x=${tick.x} align=${tick.align}`
      + ` | dx=${dx.toFixed(1)} => ${aligned ? 'ALIGNED' : 'MISALIGNED'}`);
    if (!aligned)
      fail(`${w}x${h}: ${name} does not share its column's edge`
        + ` (dx=${dx.toFixed(1)}, align ${head.align} vs ${tick.align})`);
  };
  report('dB  ', dB, gainTick);
  report('dBFS', dBFS, analyzerTick);

  // Opposite gutters, so opposite alignment. Equal alignment would lean both
  // headings the same way across the plot.
  if (dB.align === dBFS.align)
    fail(`${w}x${h}: both headings are ${dB.align} aligned; they name opposite edges`);
  // One row, above the top tick rather than on it.
  if (dB.y !== dBFS.y)
    fail(`${w}x${h}: headings are on different rows (${dB.y} vs ${dBFS.y})`);
  if (!(dB.y < topTick.y))
    fail(`${w}x${h}: heading y=${dB.y} is not above the +24 tick at y=${topTick.y}`);
  // Neither heading may carry a parenthetical caption any more.
  for (const c of calls)
    if (/\(gain\)|\(analyzer\)/.test(c.text))
      fail(`${w}x${h}: a parenthetical heading is still painted: ${c.text}`);
}

// ---- 3. nothing paints a mode caption over the plot ----------------------
// drawBands owns that string, so scan the whole payload rather than only the
// ruler body.
if (/SPECTRAL/.test(html))
  fail('a SPECTRAL caption is still painted inside the plot');

const passed = failures === 0;
console.log(`\n${passed ? 'PASS' : 'FAIL'}: ${failures} failure(s)`
  + (plant ? ` (plant=${plant})` : '')
  + (expectFail ? ' [--expect-fail: this row is green only when the suite FAILS]' : ''));
if (expectFail) {
  if (passed) {
    console.log('the planted regression did NOT redden the suite -- it can no'
      + ' longer fail, so its clean run proves nothing');
    process.exit(1);
  }
  process.exit(0);
}
process.exit(passed ? 0 : 1);
