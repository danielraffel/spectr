#!/usr/bin/env node
/**
 * Build a deterministic dependency-first module plan from a WP-1 parser
 * manifest. This is a plan/verification seam only: it never rewrites the
 * materialized runtime and never guesses dependencies from source text.
 *
 * The parser manifest already carries exact component IDs, source slices and
 * unresolved-reference diagnostics. This tool turns that closure into a
 * reviewable order for a future TSX builder and optionally verifies extracted
 * component files against the same hashes.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const PLAN_SCHEMA = 'spectr-owned-tsx-module-plan-v1';
const MANIFEST_SCHEMA = 'spectr-owned-component-dependency-v1';
const ID_RE = /^component:([A-Za-z_$][\w$]*):([0-9a-f]{64})$/;
const MANIFEST_KEYS = new Set([
  'schema', 'parser', 'source', 'roots', 'component_count', 'components',
]);
const COMPONENT_KEYS = new Set([
  'id', 'name', 'kind', 'owner', 'script_index', 'start', 'end', 'bytes',
  'sha256', 'source_sha256', 'dependencies', 'captures',
  'external_bindings', 'unresolved',
]);

function fail(message) { throw new Error(`WP-1 module plan failed: ${message}`); }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function isRecord(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function assertKeys(value, expected, label) {
  if (!isRecord(value)) fail(`${label} must be an object`);
  const actual = new Set(Object.keys(value));
  const missing = [...expected].filter(key => !actual.has(key)).sort();
  const extra = [...actual].filter(key => !expected.has(key)).sort();
  if (missing.length || extra.length)
    fail(`${label} keys changed${missing.length ? `; missing ${missing.join(', ')}` : ''}${extra.length ? `; unexpected ${extra.join(', ')}` : ''}`);
}
function assertDigest(value, label) {
  if (typeof value !== 'string' || !/^[0-9a-f]{64}$/.test(value)) fail(`${label} must be a lowercase SHA-256 digest`);
}
function assertNonNegativeInteger(value, label) {
  if (!Number.isInteger(value) || value < 0) fail(`${label} must be a non-negative integer`);
}
function validateManifest(raw, manifestPath) {
  let manifest;
  try { manifest = JSON.parse(raw); } catch (error) { fail(`manifest JSON is invalid: ${error.message}`); }
  assertKeys(manifest, MANIFEST_KEYS, 'manifest');
  if (manifest.schema !== MANIFEST_SCHEMA) fail(`unexpected manifest schema ${JSON.stringify(manifest.schema)}`);
  if (!isRecord(manifest.parser)) fail('manifest parser must be an object');
  if (!isRecord(manifest.source)) fail('manifest source must be an object');
  assertDigest(manifest.source.sha256, 'manifest source.sha256');
  assertNonNegativeInteger(manifest.source.bytes, 'manifest source.bytes');
  if (!Array.isArray(manifest.roots) || !manifest.roots.length) fail('manifest roots must be a non-empty array');
  assertNonNegativeInteger(manifest.component_count, 'manifest component_count');
  if (!Array.isArray(manifest.components)) fail('manifest components must be an array');
  if (manifest.component_count !== manifest.components.length) fail('manifest component_count does not match components');

  const byId = new Map();
  const byName = new Map();
  for (const [index, component] of manifest.components.entries()) {
    assertKeys(component, COMPONENT_KEYS, `component[${index}]`);
    if (typeof component.id !== 'string' || !ID_RE.test(component.id)) fail(`component[${index}] has invalid id`);
    if (typeof component.name !== 'string' || !/^[A-Za-z_$][\w$]*$/.test(component.name)) fail(`component[${index}] has invalid name`);
    if (byId.has(component.id)) fail(`duplicate component id ${component.id}`);
    if (byName.has(component.name)) fail(`duplicate component name ${component.name}`);
    byId.set(component.id, component);
    byName.set(component.name, component);
    for (const key of ['script_index', 'start', 'end', 'bytes']) assertNonNegativeInteger(component[key], `component ${component.name}.${key}`);
    if (component.end < component.start) fail(`component ${component.name} end precedes start`);
    assertDigest(component.sha256, `component ${component.name}.sha256`);
    assertDigest(component.source_sha256, `component ${component.name}.source_sha256`);
    if (component.sha256 !== component.source_sha256) fail(`component ${component.name} source/hash identity diverges`);
    if (component.owner !== null && (typeof component.owner !== 'string' || !ID_RE.test(component.owner))) fail(`component ${component.name} owner is invalid`);
    for (const key of ['dependencies', 'captures', 'external_bindings', 'unresolved']) {
      if (!Array.isArray(component[key]) || component[key].some(value => typeof value !== 'string')) fail(`component ${component.name}.${key} must be a string array`);
    }
    if (component.unresolved.length) fail(`unresolved identifiers for ${component.name}: ${component.unresolved.join(', ')}`);
  }
  for (const root of manifest.roots) {
    if (typeof root !== 'string' || !byId.has(root)) fail(`root references unknown component ${JSON.stringify(root)}`);
  }
  for (const component of manifest.components) {
    if (component.owner !== null && !byId.has(component.owner)) fail(`component ${component.name} owner references unknown id ${component.owner}`);
    for (const dependency of component.dependencies) {
      if (!byId.has(dependency)) fail(`component ${component.name} dependency references unknown id ${dependency}`);
      if (dependency === component.id) fail(`component ${component.name} depends on itself`);
    }
  }
  return { manifest, byId, byName, manifestPath };
}

function authoredKey(component) {
  // Source location is the semantic tie-break; id closes ties for nested
  // components that share a declaration start in a parser fixture.
  return [component.script_index, component.start, component.id];
}
function compareComponents(left, right) {
  const a = authoredKey(left); const b = authoredKey(right);
  for (let i = 0; i < a.length; i++) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}
function dependencyOrder(manifest, byId) {
  const state = new Map();
  const stack = [];
  const ordered = [];
  const visit = id => {
    const mark = state.get(id) || 0;
    if (mark === 2) return;
    if (mark === 1) {
      const index = stack.indexOf(id);
      const cycle = [...stack.slice(index), id].map(item => byId.get(item)?.name || item);
      fail(`dependency cycle: ${cycle.join(' -> ')}`);
    }
    const component = byId.get(id);
    if (!component) fail(`dependency references unknown id ${id}`);
    state.set(id, 1); stack.push(id);
    [...component.dependencies].map(dep => byId.get(dep)).sort(compareComponents).forEach(dep => visit(dep.id));
    stack.pop(); state.set(id, 2); ordered.push(component);
  };
  // Walk roots in authored order and include only their transitive closure.
  [...manifest.roots].map(id => byId.get(id)).sort(compareComponents).forEach(root => visit(root.id));
  return ordered;
}
function moduleRecord(component, modulesDir, manifestPath) {
  const modulePath = modulesDir ? path.join(modulesDir, `${component.name}.tsx`) : null;
  const record = {
    id: component.id,
    name: component.name,
    kind: component.kind,
    owner: component.owner,
    dependencies: [...component.dependencies].sort(),
    captures: [...component.captures].sort(),
    source_sha256: component.source_sha256,
    source_bytes: component.bytes,
    module: modulePath ? path.relative(path.dirname(manifestPath), modulePath) : null,
  };
  if (modulePath) {
    if (!fs.existsSync(modulePath) || !fs.statSync(modulePath).isFile()) fail(`module missing for ${component.name}: ${modulePath}`);
    const bytes = fs.readFileSync(modulePath);
    if (bytes.length !== component.bytes || sha256(bytes) !== component.source_sha256)
      fail(`module bytes/hash changed for ${component.name}: ${modulePath}`);
  }
  return record;
}
function makePlan(manifestPath, modulesDir) {
  const raw = fs.readFileSync(manifestPath);
  const { manifest, byId } = validateManifest(raw, manifestPath);
  const ordered = dependencyOrder(manifest, byId);
  const reachable = new Set(ordered.map(component => component.id));
  for (const component of manifest.components) {
    if (!reachable.has(component.id)) fail(`component ${component.name} is not reachable from manifest roots`);
  }
  return {
    schema: PLAN_SCHEMA,
    version: 1,
    manifest: {
      path: path.basename(manifestPath),
      sha256: sha256(raw),
      source: manifest.source,
    },
    roots: [...manifest.roots],
    order: ordered.map(component => component.id),
    modules: ordered.map(component => moduleRecord(component, modulesDir, manifestPath)),
  };
}
function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--manifest' || arg === '--modules' || arg === '--out') args[arg.slice(2)] = argv[++i];
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function usage() { console.log('usage: node tools/wp1_module_plan.mjs --manifest FILE [--modules DIR] [--out FILE]'); }
try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  if (!args.manifest) fail('--manifest is required');
  const manifestPath = path.resolve(args.manifest);
  if (!fs.existsSync(manifestPath)) fail(`manifest does not exist: ${manifestPath}`);
  const modulesDir = args.modules ? path.resolve(args.modules) : null;
  if (modulesDir && (!fs.existsSync(modulesDir) || !fs.statSync(modulesDir).isDirectory())) fail(`modules directory does not exist: ${modulesDir}`);
  const plan = makePlan(manifestPath, modulesDir);
  const output = `${JSON.stringify(plan, null, 2)}\n`;
  if (args.out) fs.writeFileSync(path.resolve(args.out), output);
  process.stdout.write(output);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
