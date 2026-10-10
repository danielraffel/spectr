import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [comparatorPath, chromePath] = process.argv.slice(2);
assert(comparatorPath && chromePath,
  'usage: test_compare_source_html_chromium.mjs COMPARATOR CHROME');

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-compare-doctype-'));
const cases = [
  {
    name: 'doctype',
    source: '<!doctype html>\n<html><head><title>standards</title></head>' +
      '<body><div id="root"><span>fixture</span><canvas width="8" height="8"></canvas>' +
      '</div></body></html>\n',
    expectedCompatMode: 'CSS1Compat',
  },
  {
    name: 'no-doctype',
    source: '<html><head><title>quirks</title></head>' +
      '<body><div id="root"><span>fixture</span><canvas width="8" height="8"></canvas>' +
      '</div></body></html>\n',
    expectedCompatMode: 'BackCompat',
  },
];

try {
  for (const testCase of cases) {
    const sourcePath = path.join(temp, `${testCase.name}.html`);
    const outputPath = path.join(temp, testCase.name);
    fs.writeFileSync(sourcePath, testCase.source);
    const run = spawnSync(process.execPath, [
      comparatorPath,
      '--source', sourcePath,
      '--output', outputPath,
      '--chrome', chromePath,
      '--strict',
    ], {encoding: 'utf8', timeout: 45_000, maxBuffer: 4 * 1024 * 1024});
    assert.equal(run.error, undefined, run.error && run.error.message);
    assert.equal(run.status, 0,
      `${testCase.name} comparator failed:\n${run.stdout}\n${run.stderr}`);

    const receipt = JSON.parse(fs.readFileSync(path.join(outputPath, 'receipt.json'), 'utf8'));
    assert.equal(receipt.positive.info.compatMode, testCase.expectedCompatMode,
      `${testCase.name} compatMode`);

    const injected = fs.readFileSync(path.join(outputPath, 'source-with-bridge.html'), 'utf8');
    if (testCase.expectedCompatMode === 'CSS1Compat') {
      assert.match(injected, /^<!doctype html>\s*<script>/i,
        'the bridge must follow a leading doctype');
    } else {
      assert.match(injected, /^<script>/,
        'a source without a doctype keeps the bridge at the document start');
    }
  }
} finally {
  fs.rmSync(temp, {recursive: true, force: true});
}

console.log('PASS: doctype source stays CSS1Compat and no-doctype control stays BackCompat');
