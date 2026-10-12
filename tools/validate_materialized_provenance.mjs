#!/usr/bin/env node
/*
 * Spectr materialized-document provenance and shadow-v2 gate.
 *
 * The checked-in materialized document remains v1 because current JavaScript
 * and native consumers consume v1. This tool creates a lossless,
 * diagnostic-only v2 shadow projection and provenance manifest. It does not
 * rewrite runtime.js or the shipping document.
 */
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, isAbsolute, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const BINDING_KINDS = Object.freeze([
  'semantic', 'layout', 'text', 'paint', 'canvas'
]);
const BINDING_ARRAYS = Object.freeze(Object.fromEntries(
  BINDING_KINDS.map((kind) => [kind, kind + '_bindings'])
));
export const EXPECTED_RUNTIME_FINGERPRINT_REVISION = 3;
export const MANIFEST_SCHEMA = 'spectr-materialized-shadow-parity-v1';
export const SHADOW_SCHEMA = 'pulp-materialized-browser-document-v2-shadow';
export const EXPECTED_RUNTIME_PATCH_SCRIPTS = Object.freeze([
  'patch_materialized_runtime_batched_host_callbacks.py',
  'patch_materialized_runtime_batched_state_resolution.py',
  'patch_materialized_runtime_commit_scope.py',
  'patch_materialized_runtime_commit_stats.py',
  'patch_materialized_runtime_hit_slop.py',
  'patch_materialized_runtime_insert_index.py',
  'patch_materialized_runtime_linear_scroll_upgrade.py',
  'patch_materialized_runtime_overlay_parent.py',
  'patch_materialized_runtime_path_memo.py',
  'patch_materialized_runtime_pattern_menu_footer.py',
  'patch_materialized_runtime_selector_parse_cache.py',
  'patch_materialized_runtime_single_font_pass.py',
  'patch_materialized_runtime_unread_data_attributes.py'
]);
const scriptDir = dirname(fileURLToPath(import.meta.url));
export const repoRoot = resolve(scriptDir, '..');

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}
export function canonicalValue(value) {
  if (Array.isArray(value)) return value.map(canonicalValue);
  if (isObject(value)) {
    return Object.fromEntries(Object.keys(value).sort().map(
      (key) => [key, canonicalValue(value[key])]
    ));
  }
  if (typeof value === 'number' && !Number.isFinite(value)) {
    throw new Error('canonical JSON cannot contain a non-finite number');
  }
  return value;
}
export function canonicalJson(value) { return JSON.stringify(canonicalValue(value)); }
export function sha256Bytes(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}
export function sha256Text(text) { return sha256Bytes(Buffer.from(text, 'utf8')); }
function clone(value) { return JSON.parse(JSON.stringify(value)); }

function explicitSourceId(row) {
  if (!isObject(row)) return null;
  for (const key of ['data-pulp-id', 'data_pulp_id', 'dataPulpId', 'pulp_id', 'stable_id']) {
    const value = row[key];
    if (typeof value === 'string' && value.trim() !== '') return value.trim();
  }
  return null;
}
function pathDescriptor(row) {
  return {
    anchor: typeof row?.anchor === 'string' ? row.anchor : '',
    path: Array.isArray(row?.path) ? row.path.map((step) => ({
      tag: typeof step?.tag === 'string' ? step.tag : '',
      index: Number.isInteger(step?.index) ? step.index : null
    })) : [],
    backend_node_id: row?.backend_node_id ?? null
  };
}
function provisionalId(kind, row, index) {
  // Index is deliberately part of identity. This is deterministic for one
  // capture, but sibling insertion/reordering changes it.
  const fingerprint = sha256Text(canonicalJson(pathDescriptor(row))).slice(0, 20);
  return kind + ':positional-provisional:' + index + ':' + fingerprint;
}
function rowIdentity(kind, row, index) {
  const explicit = explicitSourceId(row);
  if (explicit) return {
    id: explicit, mode: 'source-explicit', index, descriptor: pathDescriptor(row)
  };
  return {
    id: provisionalId(kind, row, index),
    mode: 'positional-provisional',
    index,
    descriptor: pathDescriptor(row)
  };
}
function normalizeBindingStream(kind, rows, globalIds = null) {
  if (!Array.isArray(rows)) throw new Error(kind + '_bindings must be an array');
  const bindingsById = {};
  const identities = [];
  for (let index = 0; index < rows.length; ++index) {
    const row = rows[index];
    if (!isObject(row)) throw new Error(kind + '_bindings[' + index + '] must be an object');
    const identity = rowIdentity(kind, row, index);
    if (Object.hasOwn(bindingsById, identity.id)) {
      throw new Error(kind + '_bindings has duplicate identity ' + identity.id);
    }
    if (globalIds?.has(identity.id)) {
      throw new Error('cross-stream duplicate identity ' + identity.id);
    }
    if (identity.id === '__proto__' || identity.id === 'constructor' ||
        identity.id === 'prototype') {
      throw new Error(kind + '_bindings rejected prototype identity ' + identity.id);
    }
    bindingsById[identity.id] = clone(row);
    globalIds?.add(identity.id);
    identities.push(identity);
  }
  const allExplicit = identities.every((identity) => identity.mode === 'source-explicit');
  return {
    bindingsById,
    identities,
    identityMode: allExplicit ? 'source-explicit' : 'positional-provisional'
  };
}

export function normalizeMaterializedDocument(document) {
  if (!isObject(document)) throw new Error('materialized document must be an object');
  if (document.version !== 1 ||
      document.schema !== 'pulp-materialized-browser-document-v1') {
    throw new Error('expected pulp-materialized-browser-document-v1/version 1, got ' +
      document.schema + '/version ' + document.version);
  }
  const bindingsById = {};
  const identitiesByKind = {};
  const identityModesByKind = {};
  const counts = {};
  const globalIds = new Set();
  for (const kind of BINDING_KINDS) {
    const field = BINDING_ARRAYS[kind];
    const result = normalizeBindingStream(kind, document[field], globalIds);
    bindingsById[kind] = result.bindingsById;
    identitiesByKind[kind] = result.identities;
    identityModesByKind[kind] = result.identityMode;
    counts[kind] = document[field].length;
  }
  const identityMode = Object.values(identityModesByKind).every(
    (mode) => mode === 'source-explicit'
  ) ? 'source-explicit' : 'positional-provisional';
  const diagnosticShadow = {
    schema: 'pulp-materialized-binding-diagnostic-shadow-v1',
    version: 1,
    identity_mode: identityMode,
    identity_modes_by_kind: identityModesByKind,
    binding_counts: counts,
    bindings_by_id: bindingsById,
    identities_by_kind: identitiesByKind,
    authoritative: false,
    consumer_compatibility: {
      source_schema: document.schema,
      source_version: document.version,
      runtime_and_native_consumers: 'v1-only'
    }
  };
  const shadowDocument = {
    ...clone(document),
    schema: SHADOW_SCHEMA,
    version: 2,
    source_schema: document.schema,
    source_version: document.version,
    diagnostic_shadow: diagnosticShadow
  };
  return {
    shadowDocument, diagnosticShadow, bindingsById, identitiesByKind,
    identityModesByKind, identityMode, counts
  };
}

function pathForManifest(file, root) {
  const absolute = resolve(file);
  const rel = relative(root, absolute);
  return rel && !rel.startsWith('..') && !isAbsolute(rel) ? rel : absolute;
}
export function fileRecord(file, root = repoRoot) {
  const absolute = resolve(file);
  if (!existsSync(absolute)) {
    return { path: pathForManifest(absolute, root), present: false };
  }
  const bytes = readFileSync(absolute);
  return {
    path: pathForManifest(absolute, root),
    present: true,
    bytes: bytes.byteLength,
    sha256: sha256Bytes(bytes)
  };
}
function readOptionalJson(file) {
  if (!file || !existsSync(file)) return null;
  try { return JSON.parse(readFileSync(file, 'utf8')); } catch { return null; }
}
export function assetResolution(document) {
  const assets = Array.isArray(document.assets) ? document.assets : [];
  const ids = assets.map((asset) => String(asset?.id ?? ''));
  const duplicateIds = [...new Set(ids.filter(
    (id, index) => ids.indexOf(id) !== index
  ))].sort();
  const byId = new Map(assets.map((asset) => [String(asset?.id ?? ''), asset]));
  const references = [...String(document.html ?? '').matchAll(
    /pulp-materialized-asset-[A-Za-z0-9]+/g
  )].map((match) => match[0]);
  const referenced = [...new Set(references)];
  const missing = referenced.filter((id) => !byId.has(id));
  const invalid = [];
  const hashes = {};
  for (const asset of assets) {
    const id = String(asset?.id ?? '');
    const raw = asset?.data_base64;
    let digest = null;
    let bytes = null;
    try {
      const validBase64 = typeof raw === 'string' &&
        /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(raw);
      bytes = validBase64 ? Buffer.from(raw, 'base64') : null;
      const canonicalBase64 = bytes ? bytes.toString('base64') : '';
      digest = bytes ? sha256Bytes(bytes) : null;
      hashes[id] = digest;
      const expectedId = 'pulp-materialized-asset-' + String(asset?.sha256 ?? '');
      if (!validBase64 || !bytes || canonicalBase64 !== raw ||
          !Number.isInteger(asset?.byte_length) || asset.byte_length < 0 ||
          asset.byte_length !== bytes.byteLength ||
          typeof asset?.mime_type !== 'string' || asset.mime_type.length === 0 ||
          !validSha(asset?.sha256) || asset.sha256 !== digest ||
          id !== expectedId) {
        invalid.push(id);
      }
    } catch {
      invalid.push(id);
    }
  }
  return {
    status: missing.length || invalid.length || duplicateIds.length ? 'FAIL' : 'PASS',
    referenced_count: referenced.length,
    asset_count: assets.length,
    missing_ids: missing,
    duplicate_ids: duplicateIds,
    invalid_assets: [...new Set(invalid)].sort(),
    hashes
  };
}
function runtimePatchAllowlist(root) {
  const toolsDir = join(root, 'tools');
  const discovered = existsSync(toolsDir) ? readdirSync(toolsDir).filter(
    (name) => /^patch_materialized_runtime_.*\.py$/.test(name)
  ).sort() : [];
  const expected = [...EXPECTED_RUNTIME_PATCH_SCRIPTS];
  const missing = expected.filter((name) => !discovered.includes(name));
  const unexpected = discovered.filter((name) => !expected.includes(name));
  return {
    expected_count: expected.length,
    status: missing.length === 0 && unexpected.length === 0 ? 'PASS' : 'FAIL',
    expected_scripts: expected,
    discovered_scripts: discovered,
    missing_scripts: missing,
    unexpected_scripts: unexpected,
    scripts: expected.map((name) => fileRecord(join(toolsDir, name), root))
  };
}
export function runtimeFingerprint(runtimePath, root = repoRoot) {
  const record = fileRecord(runtimePath, root);
  if (!record.present) return {
    expected_revision: EXPECTED_RUNTIME_FINGERPRINT_REVISION,
    status: 'MISSING', file: record, signatures: {}
  };
  const source = readFileSync(resolve(runtimePath), 'utf8');
  const signatures = {
    batched_host_callbacks: {
      needles: ['__pulpBatchUpdates__'],
      present: source.includes('__pulpBatchUpdates__')
    },
    scoped_metadata_reapply: {
      needles: ['markMaterializedTreeDirty', '__pulpMaterializedTreeEpoch__'],
      present: source.includes('markMaterializedTreeDirty') &&
        source.includes('__pulpMaterializedTreeEpoch__')
    },
    selector_parse_cache: {
      needles: ['selectorAttributeNames', 'missCacheable'],
      present: source.includes('selectorAttributeNames') &&
        source.includes('missCacheable')
    },
    paint_reconcile: {
      needles: ['__pulpReconcileMaterializedPaint__'],
      present: source.includes('__pulpReconcileMaterializedPaint__')
    },
    indexed_children: {
      needles: ['materializedIndexedChildren'],
      present: source.includes('materializedIndexedChildren')
    }
  };
  const legacyProductPatched = signatures.batched_host_callbacks.present &&
    signatures.scoped_metadata_reapply.present &&
    signatures.selector_parse_cache.present &&
    !signatures.paint_reconcile.present;
  return {
    expected_revision: EXPECTED_RUNTIME_FINGERPRINT_REVISION,
    status: legacyProductPatched ? 'LEGACY_PRODUCT_PATCHED' : 'INSPECTED',
    wholesale_regeneration: false,
    file: record,
    signatures,
    missing_expected_signatures: Object.values(signatures)
      .filter((signature) => !signature.present)
      .flatMap((signature) => signature.needles),
    note: legacyProductPatched
      ? 'Product-patched vendored runtime retained; no wholesale regeneration performed.'
      : 'Runtime signature set differs from the known product-patched baseline.'
  };
}
function artifactRecord(path, root) { return path ? fileRecord(path, root) : null; }
function validSha(value) {
  return typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);
}
function validEvidence(value) {
  if (value === true) return true;
  if (Number.isInteger(value) && value >= 2) return true;
  return isObject(value) && (
    value.pass === true || value.ok === true || value.equal === true ||
    value.completed === true || value.status === 'PASS'
  );
}
function strictFidelityReceipt(receipt) {
  if (!receipt) return { status: 'NOT_PROVIDED', missing: [], invalid: [] };
  const required = [
    'sourceSha256', 'screenshotSha256', 'repeat',
    'deterministicDomMarker', 'negativeControl'
  ];
  const missing = required.filter((key) => receipt[key] === undefined);
  const invalid = [];
  if (receipt.sourceSha256 !== undefined && !validSha(receipt.sourceSha256)) {
    invalid.push('sourceSha256');
  }
  if (receipt.screenshotSha256 !== undefined && !validSha(receipt.screenshotSha256)) {
    invalid.push('screenshotSha256');
  }
  for (const key of ['repeat', 'deterministicDomMarker', 'negativeControl']) {
    if (receipt[key] !== undefined && !validEvidence(receipt[key])) invalid.push(key);
  }
  return {
    status: missing.length === 0 && invalid.length === 0 ? 'COMPATIBLE' : 'INCOMPATIBLE',
    missing, invalid, schema: receipt.schema ?? null
  };
}
export function evaluateParity(options = {}) {
  const missing = [];
  const invalid = [];
  if (!options.sourceBeforeSha256 || !options.sourceAfterSha256) {
    missing.push('source_sha256_before_after');
  } else {
    if (!validSha(options.sourceBeforeSha256)) invalid.push('source_sha256_before');
    if (!validSha(options.sourceAfterSha256)) invalid.push('source_sha256_after');
  }
  if (!options.browserBefore?.present || !options.browserAfter?.present ||
      options.browserBefore.path === options.browserAfter.path) {
    missing.push('browser_screenshot_before_after_distinct');
  }
  if (!options.nativeBefore?.present || !options.nativeAfter?.present ||
      options.nativeBefore.path === options.nativeAfter.path) {
    missing.push('native_screenshot_before_after_distinct');
  }
  if (invalid.length) return {
    status: 'FAIL', comparator: 'exact-sha256', mismatch: invalid,
    reason: 'Source provenance must be an explicit lowercase SHA-256 digest.'
  };
  if (missing.length) return {
    status: 'NOT_RUN', comparator: 'exact-sha256', missing,
    reason: 'Same-source before/after browser and native artifacts are required.'
  };
  const mismatch = [];
  if (options.sourceBeforeSha256 !== options.sourceAfterSha256) {
    mismatch.push('source_sha256');
  }
  if (options.browserBefore.sha256 !== options.browserAfter.sha256) {
    mismatch.push('browser_screenshot_sha256');
  }
  if (options.nativeBefore.sha256 !== options.nativeAfter.sha256) {
    mismatch.push('native_screenshot_sha256');
  }
  return {
    status: mismatch.length ? 'FAIL' : 'PASS',
    comparator: 'exact-sha256', mismatch,
    source_sha256_before: options.sourceBeforeSha256,
    source_sha256_after: options.sourceAfterSha256,
    browser_screenshot_sha256_before: options.browserBefore.sha256,
    browser_screenshot_sha256_after: options.browserAfter.sha256,
    native_screenshot_sha256_before: options.nativeBefore.sha256,
    native_screenshot_sha256_after: options.nativeAfter.sha256
  };
}
function defaultFiles(root, options) {
  return {
    materialized_document: fileRecord(options.document, root),
    materialized_runtime_document: fileRecord(options.runtimeDocument, root),
    runtime_js: fileRecord(options.runtimeJs, root),
    editor_html: fileRecord(options.editorHtml, root),
    editor_ir: fileRecord(options.editorIr, root),
    state_atlas: fileRecord(options.stateAtlas, root)
  };
}
export function buildManifest(options = {}) {
  const root = resolve(options.root ?? repoRoot);
  const documentPath = resolve(options.document);
  const documentBytes = readFileSync(documentPath);
  const document = JSON.parse(documentBytes.toString('utf8'));
  const normalized = normalizeMaterializedDocument(document);
  const runtimeFingerprintResult = runtimeFingerprint(resolve(options.runtimeJs), root);
  const sourceSha256 = sha256Bytes(documentBytes);
  const browserBefore = artifactRecord(options.browserBefore, root);
  const browserAfter = artifactRecord(options.browserAfter, root);
  const nativeBefore = artifactRecord(options.nativeBefore, root);
  const nativeAfter = artifactRecord(options.nativeAfter, root);
  const receipt = readOptionalJson(options.parityReceipt);
  const parity = evaluateParity({
    sourceBeforeSha256: options.sourceBeforeSha256 ?? sourceSha256,
    sourceAfterSha256: options.sourceAfterSha256 ?? sourceSha256,
    browserBefore, browserAfter, nativeBefore, nativeAfter
  });
  const strictReceipt = strictFidelityReceipt(receipt);
  const artifactsSupplied = [browserBefore, browserAfter, nativeBefore, nativeAfter]
    .some((artifact) => artifact !== null);
  if (artifactsSupplied && strictReceipt.status !== 'COMPATIBLE') {
    parity.status = 'FAIL';
    parity.mismatch = [
      strictReceipt.status === 'NOT_PROVIDED'
        ? 'fidelity_receipt_missing' : 'fidelity_receipt_incompatible'
    ];
  }
  const shadowCanonical = canonicalJson(normalized.shadowDocument);
  return {
    schema: MANIFEST_SCHEMA,
    version: 1,
    generated_by: 'tools/validate_materialized_provenance.mjs',
    source: {
      path: pathForManifest(documentPath, root),
      bytes: documentBytes.byteLength,
      sha256: sourceSha256,
      schema: document.schema,
      version: document.version
    },
    shadow: {
      schema: SHADOW_SCHEMA, version: 2,
      canonical_sha256: sha256Text(shadowCanonical),
      bytes: Buffer.byteLength(shadowCanonical),
      authoritative: false, source_arrays_preserved: true,
      binding_counts: normalized.counts,
      identity_mode: normalized.identityMode,
      identity_modes_by_kind: normalized.identityModesByKind,
      output_path: options.shadowOutput
        ? pathForManifest(options.shadowOutput, root) : null
    },
    asset_resolution: assetResolution(document),
    runtime_patch_allowlist: runtimePatchAllowlist(root),
    provenance: {
      sdk_source_git_sha: options.sdkSourceGitSha ?? null,
      dependency_hashes: {
        materialized_document: sourceSha256,
        materialized_runtime_document: fileRecord(resolve(options.runtimeDocument), root).sha256 ?? null,
        runtime_js: fileRecord(resolve(options.runtimeJs), root).sha256 ?? null
      }
    },
    files: defaultFiles(root, {
      document: documentPath,
      runtimeDocument: resolve(options.runtimeDocument),
      runtimeJs: resolve(options.runtimeJs),
      editorHtml: resolve(options.editorHtml),
      editorIr: resolve(options.editorIr),
      stateAtlas: resolve(options.stateAtlas)
    }),
    runtime_fingerprint: runtimeFingerprintResult,
    fidelity_receipt: strictReceipt,
    parity: {
      ...parity, browser_before: browserBefore, browser_after: browserAfter,
      native_before: nativeBefore, native_after: nativeAfter
    },
    status: {
      shadow_projection: 'PASS', provenance_hashes: 'PASS',
      runtime: runtimeFingerprintResult.status, parity: parity.status,
      overall: parity.status === 'PASS' ? 'PASS' : 'OPEN'
    }
  };
}
function assert(condition, message) { if (!condition) throw new Error(message); }
export function validateShadowProjection(document, normalized) {
  for (const kind of BINDING_KINDS) {
    const field = BINDING_ARRAYS[kind];
    const rows = document[field];
    const map = normalized.bindingsById[kind];
    assert(Object.keys(map).length === rows.length,
      kind + ' map/list count mismatch');
    for (const identity of normalized.identitiesByKind[kind]) {
      assert(Object.hasOwn(map, identity.id), kind + ' identity missing from map');
      assert(canonicalJson(map[identity.id]) === canonicalJson(rows[identity.index]),
        kind + ' map/list row mismatch for ' + identity.id);
    }
  }
  return true;
}
export function validateManifest(manifest, root = repoRoot) {
  assert(manifest?.schema === MANIFEST_SCHEMA && manifest.version === 1,
    'unsupported provenance manifest schema/version');
  const sourcePath = resolve(root, manifest.source.path);
  assert(existsSync(sourcePath), 'source file missing: ' + sourcePath);
  const sourceBytes = readFileSync(sourcePath);
  assert(sha256Bytes(sourceBytes) === manifest.source.sha256,
    'source SHA-256 mismatch');
  const sourceDocument = JSON.parse(sourceBytes.toString('utf8'));
  assert(manifest.source.bytes === sourceBytes.byteLength,
    'source byte length mismatch');
  assert(manifest.source.schema === sourceDocument.schema &&
    manifest.source.version === sourceDocument.version,
    'source schema/version mismatch');
  const normalized = normalizeMaterializedDocument(sourceDocument);
  assert(canonicalJson(assetResolution(sourceDocument)) ===
    canonicalJson(manifest.asset_resolution), 'asset resolution mismatch');
  assert(canonicalJson(runtimePatchAllowlist(root)) ===
    canonicalJson(manifest.runtime_patch_allowlist), 'runtime patch allowlist mismatch');
  const shadowCanonical = canonicalJson(normalized.shadowDocument);
  assert(sha256Text(shadowCanonical) === manifest.shadow.canonical_sha256,
    'shadow canonical SHA-256 mismatch');
  assert(manifest.shadow.bytes === Buffer.byteLength(shadowCanonical),
    'shadow byte length mismatch');
  assert(manifest.shadow.schema === SHADOW_SCHEMA && manifest.shadow.version === 2,
    'shadow schema/version mismatch');
  assert(canonicalJson(manifest.shadow.binding_counts) ===
    canonicalJson(normalized.counts),
    'shadow binding counts mismatch');
  assert(manifest.shadow.identity_mode === normalized.identityMode &&
    canonicalJson(manifest.shadow.identity_modes_by_kind) ===
      canonicalJson(normalized.identityModesByKind),
    'shadow identity mode mismatch');
  assert(manifest.shadow.source_arrays_preserved === true,
    'shadow does not prove source arrays were preserved');
  validateShadowProjection(sourceDocument, normalized);
  for (const [name, record] of Object.entries(manifest.files ?? {})) {
    assert(record?.present === true, name + ' artifact record is missing');
    const path = resolve(root, record.path);
    assert(existsSync(path), name + ' artifact missing: ' + path);
    assert(sha256Bytes(readFileSync(path)) === record.sha256,
      name + ' artifact SHA-256 mismatch');
  }
  if (manifest.shadow.output_path) {
    const shadowPath = resolve(root, manifest.shadow.output_path);
    assert(existsSync(shadowPath), 'shadow output missing: ' + shadowPath);
    const shadowBytes = readFileSync(shadowPath);
    const shadowDocument = JSON.parse(shadowBytes.toString('utf8'));
    assert(sha256Text(canonicalJson(shadowDocument)) ===
      manifest.shadow.canonical_sha256, 'shadow output SHA-256 mismatch');
  }
  const runtime = manifest.runtime_fingerprint;
  assert(runtime?.expected_revision === EXPECTED_RUNTIME_FINGERPRINT_REVISION,
    'unexpected runtime fingerprint revision');
  if (runtime.file?.present) {
    const runtimePath = resolve(root, runtime.file.path);
    const actualRuntime = runtimeFingerprint(runtimePath, root);
    assert(canonicalJson(actualRuntime.signatures) === canonicalJson(runtime.signatures),
      'runtime fingerprint signatures mismatch');
    assert(actualRuntime.file.sha256 === runtime.file.sha256,
      'runtime bundle SHA-256 mismatch');
  }
  if (runtime.status === 'LEGACY_PRODUCT_PATCHED') {
    assert(runtime.wholesale_regeneration === false,
      'legacy runtime cannot claim wholesale regeneration');
  }
  if (manifest.parity?.status === 'PASS') {
    assert(!manifest.parity.mismatch || manifest.parity.mismatch.length === 0,
      'passing parity has mismatches');
  }
  assert(manifest.status.shadow_projection === 'PASS' &&
    manifest.status.provenance_hashes === 'PASS',
    'manifest status claims must remain successful');
  assert(manifest.status.runtime === runtime.status,
    'manifest runtime status mismatch');
  assert(manifest.status.parity === manifest.parity.status,
    'manifest parity status mismatch');
  assert(manifest.status.overall ===
    (manifest.parity.status === 'PASS' ? 'PASS' : 'OPEN'),
    'manifest overall status mismatch');
  if (manifest.fidelity_receipt?.status !== 'COMPATIBLE') {
    assert(manifest.status.parity !== 'PASS',
      'missing or incompatible fidelity receipt cannot pass parity');
  }
  return true;
}
function parseArgs(argv) {
  const result = {
    root: repoRoot,
    document: join(repoRoot, 'native-ui/materialized/materialized-document.json'),
    runtimeDocument: join(repoRoot, 'native-ui/materialized/materialized-document.runtime.json'),
    runtimeJs: join(repoRoot, 'native-ui/materialized/runtime.js'),
    editorHtml: join(repoRoot, 'resources/editor.html'),
    editorIr: join(repoRoot, 'native-ui/materialized/editor.ir.json'),
    stateAtlas: join(repoRoot, 'native-ui/materialized/state-atlas.json'),
    shadowOutput: join(repoRoot, 'native-ui/materialized/materialized-document.v2-shadow.json'),
    output: join(repoRoot, 'native-ui/materialized/materialized-provenance.json')
  };
  for (let i = 0; i < argv.length; ++i) {
    const value = argv[i];
    const next = () => {
      if (++i >= argv.length) throw new Error(value + ' requires a value');
      return argv[i];
    };
    if (value === '--root') result.root = resolve(next());
    else if (value === '--document') result.document = resolve(next());
    else if (value === '--runtime-document') result.runtimeDocument = resolve(next());
    else if (value === '--runtime-js') result.runtimeJs = resolve(next());
    else if (value === '--editor-html') result.editorHtml = resolve(next());
    else if (value === '--editor-ir') result.editorIr = resolve(next());
    else if (value === '--state-atlas') result.stateAtlas = resolve(next());
    else if (value === '--shadow-output') result.shadowOutput = resolve(next());
    else if (value === '--output') result.output = resolve(next());
    else if (value === '--browser-before') result.browserBefore = resolve(next());
    else if (value === '--browser-after') result.browserAfter = resolve(next());
    else if (value === '--native-before') result.nativeBefore = resolve(next());
    else if (value === '--native-after') result.nativeAfter = resolve(next());
    else if (value === '--parity-receipt') result.parityReceipt = resolve(next());
    else if (value === '--source-before-sha256') result.sourceBeforeSha256 = next();
    else if (value === '--source-after-sha256') result.sourceAfterSha256 = next();
    else if (value === '--sdk-source-git-sha') result.sdkSourceGitSha = next();
    else if (value === '--help') result.help = true;
    else throw new Error('unknown argument ' + value);
  }
  return result;
}
export async function main(argv = process.argv.slice(2)) {
  const options = parseArgs(argv);
  if (options.help) {
    console.log('Usage: validate_materialized_provenance.mjs [options]');
    return 0;
  }
  mkdirSync(dirname(options.output), { recursive: true });
  mkdirSync(dirname(options.shadowOutput), { recursive: true });
  const source = JSON.parse(readFileSync(options.document, 'utf8'));
  writeFileSync(options.shadowOutput,
    canonicalJson(normalizeMaterializedDocument(source).shadowDocument) + '\n');
  const manifest = buildManifest(options);
  validateManifest(manifest, options.root);
  writeFileSync(options.output, JSON.stringify(manifest, null, 2) + '\n');
  console.log('spectr-materialized-provenance: ' + manifest.status.overall);
  console.log('spectr-materialized-provenance: manifest ' + options.output);
  console.log('spectr-materialized-provenance: shadow ' + options.shadowOutput);
  console.log('spectr-materialized-provenance: parity ' + manifest.parity.status);
  return 0;
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error('spectr-materialized-provenance: ' + error.message);
    process.exitCode = 1;
  });
}
