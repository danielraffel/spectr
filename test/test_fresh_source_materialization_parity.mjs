#!/usr/bin/env node
// Keep the canonical fresh-import adapter and checked-in materialized runtime
// aligned on native processing-state provenance. This is intentionally a
// source/artifact contract, not a browser-only smoke test.
import fs from 'node:fs';
import assert from 'node:assert/strict';

const source = fs.readFileSync('resources/editor.html', 'utf8');
const artifact = JSON.parse(fs.readFileSync('native-ui/materialized/materialized-document.runtime.json', 'utf8')).html;

assert.equal((source.match(/spectrPublicationProvenance/g) || []).length, 3,
  'source must define and use one canonical provenance helper at both publication sites');
assert.ok(source.includes('drawn_revision: live.drawn'),
  'source publications must carry drawn_revision');
assert.ok(source.includes('lmin: viewRef.current.lmin'),
  'source helper must read the live viewport ref');
assert.ok(!source.includes('N, gainDb, muted, view.lmin, view.lmax'),
  'source must not publish render-time viewport state');
assert.ok(artifact.includes('spectrPublicationProvenance'),
  'materialized runtime must contain the canonical provenance helper');
assert.ok((artifact.match(/drawn_revision/g) || []).length >= 2,
  'materialized runtime must carry drawn_revision at both publication sites');
assert.ok(artifact.includes('lmin: viewRef.current.lmin'),
  'materialized runtime must read the live viewport ref');

const plantedNoDrawn = source.replaceAll('drawn_revision: live.drawn', '');
assert.notEqual((plantedNoDrawn.match(/drawn_revision/g) || []).length,
  (source.match(/drawn_revision/g) || []).length,
  'planted missing-drawn mutation must be detectable');
const plantedRenderView = source.replaceAll('live.lmin', 'view.lmin').replaceAll('live.lmax', 'view.lmax');
assert.ok(plantedRenderView.includes('N, gainDb, muted, view.lmin, view.lmax'),
  'planted render-view mutation must be detectable');
console.log('PASS: fresh source/materialized provenance parity and planted negatives');
