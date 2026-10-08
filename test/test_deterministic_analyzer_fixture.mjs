import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  browserFramePayload, canonicalFixtureBytes, loadFixture, receiptFor, validateFixture,
} from '../tools/deterministic_analyzer_fixture.mjs';

const fixturePath = fileURLToPath(new URL('./fixtures/deterministic-analyzer-v1.json', import.meta.url));
const loaded = loadFixture(fixturePath);
const payload = browserFramePayload(loaded);
assert.equal(payload.visible.magnitude_db.length, 321);
assert.equal(payload.overview.magnitude_db.length, 121);
assert(payload.visible.magnitude_db.some(value => value > 20));
assert(payload.overview.magnitude_db.some(value => value > 20));
for (let repeat = 0; repeat < 5; ++repeat)
  assert.deepEqual(browserFramePayload(loadFixture(fixturePath)), payload);
assert.equal(loaded.fixtureSha256, 'a313f479591699997285ad47bd97d4a57494d50f8b7a83486613982c44c4db55');
const reordered = structuredClone(loaded.fixture);
reordered.viewport = { max_hz: reordered.viewport.max_hz, min_hz: reordered.viewport.min_hz };
reordered.provenance = { source: reordered.provenance.source, generator: reordered.provenance.generator, mode: 'test-only' };
assert.equal(createHash('sha256').update(canonicalFixtureBytes(reordered)).digest('hex'), loaded.fixtureSha256);
const browserReceipt = receiptFor(loaded, 'browser');
const nativeReceipt = receiptFor(loaded, 'native');
assert.deepEqual({ ...browserReceipt, consumer: 'native' }, nativeReceipt);
for (const mutate of [
  f => { f.sequence_number = -1; },
  f => { f.epoch = 1.5; },
  f => { f.sample_rate = Infinity; },
  f => { f.sample_rate = 20; },
  f => { f.fft_size = 513; },
  f => { f.magnitude_db[0] = NaN; },
  f => { f.magnitude_db[0] = 25; },
  f => { f.provenance.mode = 'production'; },
]) {
  const fixture = structuredClone(loaded.fixture);
  mutate(fixture);
  assert.throws(() => validateFixture(fixture), /deterministic-analyzer-fixture:/);
}
const temp = mkdtempSync(path.join(os.tmpdir(), 'spectr-fixture-negative-'));
try {
  const tampered = structuredClone(loaded.fixture);
  tampered.magnitude_db[0] += 1;
  const tamperedPath = path.join(temp, 'tampered.json');
  writeFileSync(tamperedPath, JSON.stringify(tampered));
  assert.throws(() => loadFixture(tamperedPath), /fixture_sha256 does not match/);
} finally {
  rmSync(temp, { recursive: true, force: true });
}
console.log('SPECTR_DETERMINISTIC_FIXTURE_OK: projection, canonical identity, receipts, and planted negatives');
