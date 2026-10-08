#!/usr/bin/env node
// Test-only deterministic analyzer fixture contract shared by browser and native
// Spectr receipts. Production editor code does not import this module.
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';

export const FIXTURE_SCHEMA = 'spectr.deterministic-analyzer-fixture.v1';
export const FRAME_SCHEMA = 'spectr.deterministic-analyzer-frame.v1';
export const VISIBLE_POINTS = 321;
export const OVERVIEW_POINTS = 121;

function fail(message) { throw new Error(`deterministic-analyzer-fixture: ${message}`); }
function finite(value, name) {
  if (!Number.isFinite(value)) fail(`${name} must be finite`);
  return value;
}
function integer(value, name, minimum = 0) {
  if (!Number.isSafeInteger(value) || value < minimum)
    fail(`${name} must be a safe integer >= ${minimum}`);
  return value;
}
function arrayOfFinite(value, name, length) {
  if (!Array.isArray(value) || value.length !== length)
    fail(`${name} must contain exactly ${length} values`);
  for (const [index, item] of value.entries()) finite(item, `${name}[${index}]`);
  return value;
}

export function canonicalFixtureBytes(fixture) {
  // The checked-in JSON is parsed and re-serialized with one stable key order;
  // this digest is the provenance carried by browser/native receipts. It is
  // deliberately independent of capture timing, RAF scheduling, or PNG bytes.
  const ordered = {
    schema: fixture.schema,
    version: fixture.version,
    fixture_id: fixture.fixture_id,
    seed: fixture.seed,
    provenance: {
      mode: fixture.provenance.mode,
      generator: fixture.provenance.generator,
      source: fixture.provenance.source,
    },
    epoch: fixture.epoch,
    sequence_number: fixture.sequence_number,
    dropped_frames: fixture.dropped_frames,
    source_channels: fixture.source_channels,
    fft_size: fixture.fft_size,
    sample_rate: fixture.sample_rate,
    floor_db: fixture.floor_db,
    ceiling_db: fixture.ceiling_db,
    viewport: { min_hz: fixture.viewport.min_hz, max_hz: fixture.viewport.max_hz },
    magnitude_db: fixture.magnitude_db,
  };
  return Buffer.from(JSON.stringify(ordered));
}

export function validateFixture(input) {
  if (!input || typeof input !== 'object' || Array.isArray(input)) fail('root must be an object');
  if (input.schema !== FIXTURE_SCHEMA || input.version !== 1)
    fail(`unsupported schema/version (${input.schema}/${input.version})`);
  if (typeof input.fixture_id !== 'string' || !/^[a-z0-9][a-z0-9._-]*$/.test(input.fixture_id))
    fail('fixture_id must be a stable lowercase identifier');
  integer(input.seed, 'seed');
  if (!input.provenance || input.provenance.mode !== 'test-only')
    fail('provenance.mode must be test-only');
  if (typeof input.provenance.generator !== 'string' || !input.provenance.generator)
    fail('provenance.generator is required');
  if (typeof input.provenance.source !== 'string' || !input.provenance.source)
    fail('provenance.source is required');
  if (input.fixture_sha256 !== undefined
      && !/^[0-9a-f]{64}$/.test(input.fixture_sha256))
    fail('fixture_sha256 must be 64 lowercase hex characters when present');
  integer(input.epoch, 'epoch');
  integer(input.sequence_number, 'sequence_number');
  integer(input.dropped_frames, 'dropped_frames');
  integer(input.source_channels, 'source_channels', 1);
  if (input.source_channels > 64) fail('source_channels exceeds 64');
  integer(input.fft_size, 'fft_size', 2);
  if (input.fft_size > 1048576 || (input.fft_size & (input.fft_size - 1)) !== 0)
    fail('fft_size must be a power of two');
  finite(input.sample_rate, 'sample_rate');
  if (input.sample_rate <= 40) fail('sample_rate must be above 40 Hz for the 20 Hz overview');
  finite(input.floor_db, 'floor_db');
  finite(input.ceiling_db, 'ceiling_db');
  if (!(input.floor_db < 0 && input.ceiling_db > input.floor_db))
    fail('floor_db/ceiling_db range is invalid');
  if (!input.viewport || !(input.viewport.min_hz > 0)
      || !(input.viewport.max_hz > input.viewport.min_hz))
    fail('viewport range is invalid');
  finite(input.viewport.min_hz, 'viewport.min_hz');
  finite(input.viewport.max_hz, 'viewport.max_hz');
  const expectedBins = input.fft_size / 2 + 1;
  arrayOfFinite(input.magnitude_db, 'magnitude_db', expectedBins);
  if (input.magnitude_db.some(value => value < input.floor_db || value > input.ceiling_db))
    fail('magnitude_db contains a value outside floor_db..ceiling_db');
  return input;
}

export function loadFixture(path) {
  const fixture = validateFixture(JSON.parse(readFileSync(path, 'utf8')));
  const fixtureSha256 = createHash('sha256').update(canonicalFixtureBytes(fixture)).digest('hex');
  if (fixture.fixture_sha256 !== undefined && fixture.fixture_sha256 !== fixtureSha256)
    fail(`fixture_sha256 does not match canonical bytes (got ${fixture.fixture_sha256}, expected ${fixtureSha256})`);
  return { fixture, fixtureSha256, path };
}

// Matches Spectr's native peak_bucket_log_spectrum implementation. Keeping the
// projection here makes the exact same source bins feed browser and native;
// neither side gets a separately authored trace that could drift.
export function peakBucketLogSpectrum(fixture, minHz, maxHz, pointCount) {
  const result = new Array(pointCount).fill(fixture.floor_db);
  const binHz = fixture.sample_rate / fixture.fft_size;
  const lastBin = fixture.magnitude_db.length - 1;
  const logMin = Math.log(minHz);
  const logSpan = Math.log(maxHz) - logMin;
  for (let point = 0; point < pointCount; ++point) {
    const center = (point + 0.5) / pointCount;
    const lower = point / pointCount;
    const upper = (point + 1) / pointCount;
    const centerHz = Math.exp(logMin + center * logSpan);
    const lowerHz = Math.exp(logMin + lower * logSpan);
    const upperHz = Math.exp(logMin + upper * logSpan);
    const first = Math.max(0, Math.min(lastBin, Math.ceil(lowerHz / binHz)));
    const last = Math.max(0, Math.min(lastBin, Math.floor(upperHz / binHz)));
    let peak = fixture.floor_db;
    if (first <= last) {
      for (let bin = first; bin <= last; ++bin)
        peak = Math.max(peak, fixture.magnitude_db[bin]);
    } else {
      const position = Math.max(0, Math.min(lastBin, centerHz / binHz));
      const left = Math.floor(position);
      const right = Math.min(left + 1, lastBin);
      const mix = position - left;
      peak = fixture.magnitude_db[left]
        + (fixture.magnitude_db[right] - fixture.magnitude_db[left]) * mix;
    }
    result[point] = Math.max(fixture.floor_db, Math.min(fixture.ceiling_db, peak));
  }
  return result;
}

export function browserFramePayload(loaded) {
  const { fixture, fixtureSha256 } = loaded;
  const minHz = fixture.viewport.min_hz;
  const maxHz = fixture.viewport.max_hz;
  return {
    schema_version: 1,
    epoch: fixture.epoch,
    sequence_number: fixture.sequence_number,
    dropped_frames: fixture.dropped_frames,
    source_channels: fixture.source_channels,
    fft_size: fixture.fft_size,
    sample_rate: fixture.sample_rate,
    floor_db: fixture.floor_db,
    ceiling_db: fixture.ceiling_db,
    visible: { min_hz: minHz, max_hz: maxHz,
      magnitude_db: peakBucketLogSpectrum(fixture, minHz, maxHz, VISIBLE_POINTS) },
    overview: { min_hz: 20, max_hz: Math.min(20000, fixture.sample_rate * 0.5),
      magnitude_db: peakBucketLogSpectrum(fixture, 20, Math.min(20000, fixture.sample_rate * 0.5), OVERVIEW_POINTS) },
    provenance: { fixture_id: fixture.fixture_id, seed: fixture.seed, fixture_sha256: fixtureSha256 },
  };
}

export function receiptFor(loaded, consumer, extra = {}) {
  if (!['browser', 'native'].includes(consumer)) fail(`unknown consumer ${consumer}`);
  const { fixture, fixtureSha256, path } = loaded;
  return {
    schema: 'spectr.deterministic-capture-receipt.v1',
    consumer,
    fixture_id: fixture.fixture_id,
    seed: fixture.seed,
    fixture_sha256: fixtureSha256,
    fixture_path: path,
    frame: { epoch: fixture.epoch, sequence_number: fixture.sequence_number,
      fft_size: fixture.fft_size, sample_rate: fixture.sample_rate,
      source_channels: fixture.source_channels },
    provenance: fixture.provenance,
    ...extra,
  };
}

function main(argv) {
  const fixtureArg = argv.indexOf('--fixture');
  const consumerArg = argv.indexOf('--consumer');
  const plant = argv.includes('--plant-invalid');
  if (fixtureArg < 0 || !argv[fixtureArg + 1]) fail('usage: --fixture PATH [--consumer browser|native]');
  const loaded = loadFixture(argv[fixtureArg + 1]);
  if (plant) {
    const broken = { ...loaded.fixture, sequence_number: -1 };
    try { validateFixture(broken); }
    catch (error) {
      process.stdout.write(JSON.stringify({ schema: 'spectr.deterministic-capture-negative-control.v1', caught: true, message: error.message }) + '\n');
      return;
    }
    fail('planted invalid sequence_number was accepted');
  }
  const consumer = consumerArg >= 0 ? argv[consumerArg + 1] : 'browser';
  const payload = browserFramePayload(loaded);
  process.stdout.write(JSON.stringify(receiptFor(loaded, consumer, {
    payload_sha256: createHash('sha256').update(JSON.stringify(payload)).digest('hex'),
  }) , null, 2) + '\n');
}

if (import.meta.url === `file://${process.argv[1]}`) main(process.argv.slice(2));
