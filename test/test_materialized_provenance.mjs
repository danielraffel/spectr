#!/usr/bin/env node
import { deepStrictEqual, notDeepStrictEqual, strictEqual, throws } from 'node:assert';
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  BINDING_KINDS,
  assetResolution,
  buildManifest,
  canonicalJson,
  evaluateParity,
  fileRecord,
  normalizeMaterializedDocument,
  repoRoot,
  sha256Bytes,
  validateManifest,
  validateShadowProjection
} from '../tools/validate_materialized_provenance.mjs';

const documentPath = join(repoRoot, 'native-ui/materialized/materialized-document.json');
const runtimeDocument = join(repoRoot, 'native-ui/materialized/materialized-document.runtime.json');
const runtimeJs = join(repoRoot, 'native-ui/materialized/runtime.js');
const editorHtml = join(repoRoot, 'resources/editor.html');
const editorIr = join(repoRoot, 'native-ui/materialized/editor.ir.json');
const stateAtlas = join(repoRoot, 'native-ui/materialized/state-atlas.json');
const source = JSON.parse(readFileSync(documentPath, 'utf8'));
const expectThrows = (fn, needle) => throws(fn, new RegExp(needle));

function reverseObjectKeys(value) {
  if (Array.isArray(value)) return value.map(reverseObjectKeys);
  if (!value || typeof value !== 'object') return value;
  const result = {};
  for (const key of Object.keys(value).reverse()) result[key] = reverseObjectKeys(value[key]);
  return result;
}
function allRowsEqual(original, shadow) {
  for (const kind of BINDING_KINDS) {
    deepStrictEqual(shadow[kind + '_bindings'], original[kind + '_bindings']);
  }
}
function assert(condition, message) {
  if (!condition) throw new Error(message);
}

// Real document: v1 -> shadow v2 is deterministic and lossless.
const first = normalizeMaterializedDocument(source);
const second = normalizeMaterializedDocument(reverseObjectKeys(source));
strictEqual(canonicalJson(first.shadowDocument), canonicalJson(second.shadowDocument),
  'canonical shadow changed when source object key order changed');
allRowsEqual(source, first.shadowDocument);
strictEqual(first.identityMode, 'positional-provisional',
  'Spectr fallback identities must be labeled provisional');
strictEqual(assetResolution(source).status, 'PASS');
const missingAsset = JSON.parse(JSON.stringify(source));
missingAsset.html += '<img src="pulp-materialized-asset-missing">';
strictEqual(assetResolution(missingAsset).status, 'FAIL');
const duplicateAsset = JSON.parse(JSON.stringify(source));
duplicateAsset.assets[1].id = duplicateAsset.assets[0].id;
strictEqual(assetResolution(duplicateAsset).status, 'FAIL');
const malformedAsset = JSON.parse(JSON.stringify(source));
malformedAsset.assets[0].data_base64 = '%%%%';
malformedAsset.assets[0].byte_length = 0;
malformedAsset.assets[0].sha256 = sha256Bytes(Buffer.from(''));
strictEqual(assetResolution(malformedAsset).status, 'FAIL');
validateShadowProjection(source, first);
for (const kind of BINDING_KINDS) {
  strictEqual(Object.keys(first.bindingsById[kind]).length, source[kind + '_bindings'].length);
}

// Reordering a sibling must change a provisional identity. This is the
// regression guard against accidentally treating path-derived IDs as stable.
const reordered = JSON.parse(JSON.stringify(source));
const layout = reordered.layout_bindings;
[layout[0], layout[1]] = [layout[1], layout[0]];
const reorderedNormalized = normalizeMaterializedDocument(reordered);
notDeepStrictEqual(
  first.identitiesByKind.layout[0].id,
  reorderedNormalized.identitiesByKind.layout[0].id,
  'positional identity incorrectly survived sibling reordering'
);

// Explicit source IDs survive sibling insertion; the fallback row does not.
const explicitFixture = {
  schema: source.schema, version: source.version,
  html: source.html, mime_type: source.mime_type,
  assets: source.assets, font_bindings: source.font_bindings,
  surface_style: source.surface_style, coordinate_space: source.coordinate_space,
  presentation_time_ms: source.presentation_time_ms,
  semantic_bindings: [{ ...source.semantic_bindings[0], 'data-pulp-id': 'semantic:live' }],
  layout_bindings: [{ ...source.layout_bindings[0], 'data-pulp-id': 'layout:root' }, source.layout_bindings[1]],
  text_bindings: source.text_bindings.slice(0, 2),
  paint_bindings: source.paint_bindings.slice(0, 2),
  canvas_bindings: source.canvas_bindings.slice(0, 2)
};
const explicit = normalizeMaterializedDocument(explicitFixture);
strictEqual(explicit.identityModesByKind.semantic, 'source-explicit');
const explicitInserted = JSON.parse(JSON.stringify(explicitFixture));
explicitInserted.layout_bindings.unshift({
  anchor: '#root', path: [{ tag: 'div', index: 99 }], box: { left: 0, top: 0, width: 1, height: 1 }
});
const explicitAfterInsert = normalizeMaterializedDocument(explicitInserted);
strictEqual(explicit.identitiesByKind.layout[0].id, 'layout:root');
strictEqual(explicitAfterInsert.identitiesByKind.layout[1].id, 'layout:root');
notDeepStrictEqual(
  explicit.identitiesByKind.layout[1]?.id,
  explicitAfterInsert.identitiesByKind.layout[0]?.id
);

// Collision, map/list mismatch, and prototype-pollution controls fail closed.
const duplicate = JSON.parse(JSON.stringify(explicitFixture));
duplicate.semantic_bindings.push({ ...duplicate.semantic_bindings[0], index: 999 });
expectThrows(() => normalizeMaterializedDocument(duplicate), 'duplicate identity');
const tampered = normalizeMaterializedDocument(source);
delete tampered.bindingsById.layout[Object.keys(tampered.bindingsById.layout)[0]];
expectThrows(() => validateShadowProjection(source, tampered), 'map/list count mismatch');
const prototypeFixture = JSON.parse(JSON.stringify(explicitFixture));
prototypeFixture.semantic_bindings[0]['data-pulp-id'] = '__proto__';
expectThrows(() => normalizeMaterializedDocument(prototypeFixture), 'prototype identity');

// Manifest positive and exact file hash controls.
const manifest = buildManifest({
  root: repoRoot, document: documentPath, runtimeDocument, runtimeJs,
  editorHtml, editorIr, stateAtlas
});
validateManifest(manifest, repoRoot);
strictEqual(manifest.status.parity, 'NOT_RUN');
strictEqual(manifest.status.overall, 'OPEN');
const hashTampered = JSON.parse(JSON.stringify(manifest));
hashTampered.source.sha256 = '0'.repeat(64);
expectThrows(() => validateManifest(hashTampered, repoRoot), 'source SHA-256 mismatch');
const runtimeTampered = JSON.parse(JSON.stringify(manifest));
runtimeTampered.runtime_fingerprint.signatures.batched_host_callbacks.present = false;
expectThrows(() => validateManifest(runtimeTampered, repoRoot), 'runtime');
const sourceMetadataTampered = JSON.parse(JSON.stringify(manifest));
sourceMetadataTampered.source.bytes += 1;
expectThrows(() => validateManifest(sourceMetadataTampered, repoRoot), 'byte length');
const shadowMetadataTampered = JSON.parse(JSON.stringify(manifest));
shadowMetadataTampered.shadow.binding_counts.layout += 1;
expectThrows(() => validateManifest(shadowMetadataTampered, repoRoot), 'binding counts');
const artifactRecordTampered = JSON.parse(JSON.stringify(manifest));
artifactRecordTampered.files.editor_ir.present = false;
expectThrows(() => validateManifest(artifactRecordTampered, repoRoot), 'artifact record');
const allowlistTampered = JSON.parse(JSON.stringify(manifest));
allowlistTampered.runtime_patch_allowlist.scripts.pop();
expectThrows(() => validateManifest(allowlistTampered, repoRoot), 'allowlist');

// Exact before/after parity positive and changed-artifact negative.
const temp = mkdtempSync(join(tmpdir(), 'spectr-provenance-'));
const beforeBrowser = join(temp, 'browser-before.png');
const afterBrowser = join(temp, 'browser-after.png');
const beforeNative = join(temp, 'native-before.png');
const afterNative = join(temp, 'native-after.png');
for (const path of [beforeBrowser, afterBrowser, beforeNative, afterNative]) {
  writeFileSync(path, Buffer.from('same-image'));
}
const sameBrowser = fileRecord(beforeBrowser, repoRoot);
const sameBrowserAfter = fileRecord(afterBrowser, repoRoot);
const sameNative = fileRecord(beforeNative, repoRoot);
const sameNativeAfter = fileRecord(afterNative, repoRoot);
const positiveParity = evaluateParity({
  sourceBeforeSha256: 'a'.repeat(64), sourceAfterSha256: 'a'.repeat(64),
  browserBefore: sameBrowser, browserAfter: sameBrowserAfter,
  nativeBefore: sameNative, nativeAfter: sameNativeAfter
});
strictEqual(positiveParity.status, 'PASS');
writeFileSync(afterNative, Buffer.from('changed-image'));
const negativeParity = evaluateParity({
  sourceBeforeSha256: 'a'.repeat(64), sourceAfterSha256: 'a'.repeat(64),
  browserBefore: sameBrowser, browserAfter: sameBrowserAfter,
  nativeBefore: sameNative, nativeAfter: fileRecord(afterNative, repoRoot)
});
strictEqual(negativeParity.status, 'FAIL');
strictEqual(negativeParity.mismatch.includes('native_screenshot_sha256'), true);
const malformedSourceParity = evaluateParity({
  sourceBeforeSha256: 'bad', sourceAfterSha256: 'bad',
  browserBefore: sameBrowser, browserAfter: sameBrowserAfter,
  nativeBefore: sameNative, nativeAfter: sameNativeAfter
});
strictEqual(malformedSourceParity.status, 'FAIL');

// A legacy receipt lacking strict fields is explicitly incompatible.
writeFileSync(join(temp, 'legacy.json'), JSON.stringify({
  schema: 'legacy-receipt', sourceSha256: sha256Bytes(readFileSync(documentPath))
}));
const receiptMissing = buildManifest({
  root: repoRoot, document: documentPath, runtimeDocument, runtimeJs,
  editorHtml, editorIr, stateAtlas, parityReceipt: join(temp, 'legacy.json'),
  browserBefore: beforeBrowser, browserAfter: afterBrowser,
  nativeBefore: beforeNative, nativeAfter: afterNative
});
strictEqual(receiptMissing.fidelity_receipt.status, 'INCOMPATIBLE');
strictEqual(receiptMissing.status.parity, 'FAIL');
const malformedReceipt = JSON.parse(JSON.stringify(receiptMissing));
malformedReceipt.fidelity_receipt = {
  status: 'COMPATIBLE', missing: [], invalid: []
};
const malformedReceiptInput = join(temp, 'malformed.json');
writeFileSync(malformedReceiptInput, JSON.stringify({
  schema: 'legacy-receipt',
  sourceSha256: sha256Bytes(readFileSync(documentPath)),
  screenshotSha256: 'not-a-sha',
  repeat: {},
  deterministicDomMarker: 'yes',
  negativeControl: false
}));
const malformed = buildManifest({
  root: repoRoot, document: documentPath, runtimeDocument, runtimeJs,
  editorHtml, editorIr, stateAtlas, parityReceipt: malformedReceiptInput,
  browserBefore: beforeBrowser, browserAfter: afterBrowser,
  nativeBefore: beforeNative, nativeAfter: afterNative
});
strictEqual(malformed.fidelity_receipt.status, 'INCOMPATIBLE');
strictEqual(malformed.status.parity, 'FAIL');

console.log('spectr-materialized-provenance: PASS');
console.log('spectr-materialized-provenance: deterministic/lossless shadow, identity and map/list controls PASS');
console.log('spectr-materialized-provenance: parity positive + changed-artifact negative controls PASS');
console.log('spectr-materialized-provenance: strict receipt incompatibility control PASS');
