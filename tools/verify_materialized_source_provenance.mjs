#!/usr/bin/env node
/**
 * Verify the exact authored HTML and materialized runtime bytes recorded by
 * the staging provenance manifest. This gate records identity only; it does
 * not regenerate either file or claim browser/native parity.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const TOOL_DIR = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(TOOL_DIR, '..');
const DEFAULT_MANIFEST = path.join(
  REPO_ROOT,
  'native-ui/materialized/materialized-document.provenance.json',
);
const SCHEMA = 'spectr-materialized-source-provenance-v1';
const DIGEST_RE = /^[0-9a-f]{64}$/;

function fail(message) {
  throw new Error(`materialized source provenance failed: ${message}`);
}

function isRecord(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function exactKeys(value, expected, label) {
  if (!isRecord(value)) fail(`${label} must be an object`);
  const actual = Object.keys(value).sort();
  const required = [...expected].sort();
  if (actual.join('\0') !== required.join('\0')) {
    const missing = required.filter((key) => !actual.includes(key));
    const extra = actual.filter((key) => !required.includes(key));
    fail(`${label} keys changed${missing.length ? `; missing ${missing.join(', ')}` : ''}${extra.length ? `; unexpected ${extra.join(', ')}` : ''}`);
  }
}

function nonNegativeInteger(value, label) {
  if (!Number.isSafeInteger(value) || value < 0) fail(`${label} must be a non-negative integer`);
}

function digest(value, label) {
  if (typeof value !== 'string' || !DIGEST_RE.test(value))
    fail(`${label} must be a lowercase SHA-256 digest`);
}

function relativePath(value, label) {
  if (typeof value !== 'string' || !value || path.isAbsolute(value) || value.includes('\0'))
    fail(`${label} must be a non-empty relative path`);
  const parts = value.split(/[\\/]+/);
  if (parts.includes('..') || parts.includes('')) fail(`${label} must not contain traversal or empty path segments`);
  return value;
}

function readJson(file, label) {
  let bytes;
  try { bytes = fs.readFileSync(file); }
  catch (error) { fail(`cannot read ${label}: ${error.message}`); }
  let value;
  try { value = JSON.parse(bytes); }
  catch (error) { fail(`${label} is not valid JSON: ${error.message}`); }
  return { bytes, value };
}

function insideRoot(root, file, label) {
  let rootReal;
  let fileReal;
  try {
    rootReal = fs.realpathSync(root);
    fileReal = fs.realpathSync(file);
  } catch (error) {
    fail(`cannot resolve ${label}: ${error.message}`);
  }
  const prefix = rootReal.endsWith(path.sep) ? rootReal : `${rootReal}${path.sep}`;
  if (fileReal !== rootReal && !fileReal.startsWith(prefix))
    fail(`${label} resolves outside the repository root`);
}

function verifyFile(root, record, label) {
  exactKeys(record, new Set(['path', 'sha256', 'bytes']), label);
  const rel = relativePath(record.path, `${label}.path`);
  digest(record.sha256, `${label}.sha256`);
  nonNegativeInteger(record.bytes, `${label}.bytes`);
  const file = path.resolve(root, rel);
  insideRoot(root, file, label);
  let bytes;
  try { bytes = fs.readFileSync(file); }
  catch (error) { fail(`cannot read ${label} ${rel}: ${error.message}`); }
  const actualDigest = crypto.createHash('sha256').update(bytes).digest('hex');
  if (bytes.length !== record.bytes)
    fail(`${label} byte count mismatch for ${rel}: expected ${record.bytes}, got ${bytes.length}`);
  if (actualDigest !== record.sha256)
    fail(`${label} SHA-256 mismatch for ${rel}: expected ${record.sha256}, got ${actualDigest}`);
  return { path: rel, bytes: bytes.length, sha256: actualDigest };
}

function parseArgs(argv) {
  const args = { root: REPO_ROOT, manifest: DEFAULT_MANIFEST };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--root' || arg === '--manifest') {
      const value = argv[++i];
      if (!value) fail(`${arg} requires a value`);
      args[arg.slice(2)] = path.resolve(value);
    } else if (arg === '--help' || arg === '-h') {
      console.log('Usage: node tools/verify_materialized_source_provenance.mjs [--root DIR] [--manifest FILE]');
      process.exit(0);
    } else {
      fail(`unknown argument ${arg}`);
    }
  }
  return args;
}

function verify({ root, manifest }) {
  let rootReal;
  try { rootReal = fs.realpathSync(root); }
  catch (error) { fail(`cannot resolve repository root: ${error.message}`); }
  insideRoot(rootReal, manifest, 'manifest');
  const { value } = readJson(manifest, 'provenance manifest');
  exactKeys(value, new Set(['schema', 'version', 'scope', 'source', 'artifact']), 'provenance manifest');
  if (value.schema !== SCHEMA || value.version !== 1)
    fail(`unsupported manifest schema/version: ${value.schema}/${value.version}`);
  exactKeys(value.scope, new Set(['staging_only', 'production_cutover']), 'manifest scope');
  if (value.scope.staging_only !== true || value.scope.production_cutover !== false)
    fail('manifest scope must remain staging_only=true and production_cutover=false');
  const source = verifyFile(rootReal, value.source, 'source');
  const artifact = verifyFile(rootReal, value.artifact, 'artifact');
  return {
    schema: SCHEMA,
    version: 1,
    verified: true,
    scope: value.scope,
    source,
    artifact,
  };
}

try {
  console.log(JSON.stringify(verify(parseArgs(process.argv.slice(2))), null, 2));
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}
