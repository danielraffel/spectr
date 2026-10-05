#!/usr/bin/env node
/**
 * Emit a dependency-first, read-only authored TSX module closure.
 *
 * This is the next WP-1 build seam after the parser and bounded converter
 * experiments. It never edits the frozen materialized artifact. The input
 * dependency manifest is an integrity boundary: the artifact bytes, exact
 * component declaration slices, dependency closure, converter reports, and
 * emitted module bytes are all checked before a staging directory is moved
 * into place. The resulting TSX modules are intentionally not compiled or
 * installed; this tool proves deterministic authored-module emission only.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCHEMA = 'spectr-owned-authored-module-emission-v1';
const MANIFEST_SCHEMA = 'spectr-owned-component-dependency-v1';
const ID_RE = /^component:([A-Za-z_$][\w$]*):([0-9a-f]{64})$/;
const NAME_RE = /^[A-Za-z_$][\w$]*$/;
const DIGEST_RE = /^[0-9a-f]{64}$/;
const COMPONENT_KEYS = new Set([
  'id', 'name', 'kind', 'owner', 'script_index', 'start', 'end', 'bytes',
  'sha256', 'source_sha256', 'dependencies', 'captures',
  'external_bindings', 'unresolved',
]);
const MANIFEST_KEYS = new Set([
  'schema', 'parser', 'source', 'roots', 'component_count', 'components',
]);
const EMISSION_KEYS = new Set([
  'schema', 'version', 'artifact', 'dependency_manifest', 'converter',
  'roots', 'order', 'modules',
]);
const MODULE_KEYS = new Set([
  'id', 'name', 'kind', 'owner', 'dependencies', 'captures',
  'source_sha256', 'source_bytes', 'output_sha256', 'output_bytes', 'path',
]);
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const defaultConverter = path.join(scriptDir, 'tsx_leaf_experiment.mjs');

function fail(message) { throw new Error(`WP-1 authored module emitter failed: ${message}`); }
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
  if (typeof value !== 'string' || !DIGEST_RE.test(value)) fail(`${label} must be a lowercase SHA-256 digest`);
}
function assertInteger(value, label) {
  if (!Number.isInteger(value) || value < 0) fail(`${label} must be a non-negative integer`);
}
function readBytes(file, label) {
  try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${label}: ${error.message}`); }
}
function utf8Offsets(source) {
  const offsets = new Array(source.length + 1);
  let byte = 0;
  let index = 0;
  while (index < source.length) {
    const codePoint = source.codePointAt(index);
    const width = codePoint > 0xffff ? 2 : 1;
    offsets[index] = byte;
    if (width === 2) offsets[index + 1] = byte;
    byte += Buffer.byteLength(source.slice(index, index + width), 'utf8');
    index += width;
  }
  offsets[source.length] = byte;
  return offsets;
}

function htmlScripts(html) {
  const scripts = [];
  const re = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;
  for (const match of html.matchAll(re)) {
    const attrs = match[1] || '';
    const typeMatch = attrs.match(/\btype\s*=\s*["']([^"']+)["']/i);
    const type = (typeMatch?.[1] || 'text/javascript').toLowerCase();
    if (type === 'application/json' || type === 'importmap') continue;
    if (!/^(?:text|application)\/(?:java|ecma)script$/.test(type) && type !== 'module') continue;
    scripts.push({ source: match[2], offsets: utf8Offsets(match[2]) });
  }
  return scripts;
}

function validateManifest(raw, artifactBytes) {
  let manifest;
  try { manifest = JSON.parse(raw); } catch (error) { fail(`dependency manifest JSON is invalid: ${error.message}`); }
  assertKeys(manifest, MANIFEST_KEYS, 'dependency manifest');
  if (manifest.schema !== MANIFEST_SCHEMA) fail(`unexpected dependency manifest schema ${JSON.stringify(manifest.schema)}`);
  assertKeys(manifest.parser, new Set(['name', 'version', 'plugins', 'source_type']), 'dependency manifest parser');
  if (manifest.parser.name !== '@babel/parser' || manifest.parser.version !== '7.28.4'
      || JSON.stringify(manifest.parser.plugins) !== JSON.stringify(['jsx', 'typescript'])
      || manifest.parser.source_type !== 'script') fail('unsupported dependency parser identity');
  assertKeys(manifest.source, new Set(['kind', 'path', 'sha256', 'bytes']), 'dependency manifest source');
  if (manifest.source.kind !== 'artifact') fail(`dependency manifest source kind must be artifact, got ${JSON.stringify(manifest.source.kind)}`);
  assertDigest(manifest.source.sha256, 'dependency manifest source.sha256');
  assertInteger(manifest.source.bytes, 'dependency manifest source.bytes');
  if (manifest.source.sha256 !== sha256(artifactBytes)) fail('artifact digest does not match dependency manifest source.sha256');
  if (manifest.source.bytes !== artifactBytes.length) fail('artifact byte count does not match dependency manifest source.bytes');
  if (!Array.isArray(manifest.roots) || !manifest.roots.length) fail('dependency manifest roots must be non-empty');
  if (new Set(manifest.roots).size !== manifest.roots.length) fail('dependency manifest roots contain duplicates');
  assertInteger(manifest.component_count, 'dependency manifest component_count');
  if (!Array.isArray(manifest.components) || manifest.components.length !== manifest.component_count)
    fail('dependency manifest component_count does not match components');

  const byId = new Map();
  const byName = new Map();
  for (const [index, component] of manifest.components.entries()) {
    assertKeys(component, COMPONENT_KEYS, `dependency component[${index}]`);
    if (typeof component.id !== 'string' || !ID_RE.test(component.id)) fail(`dependency component[${index}] has invalid id`);
    if (typeof component.name !== 'string' || !NAME_RE.test(component.name)) fail(`dependency component[${index}] has invalid name`);
    const parts = component.id.match(ID_RE);
    if (parts[1] !== component.name || parts[2] !== component.sha256 || component.sha256 !== component.source_sha256)
      fail(`dependency component ${component.name} id/hash identity diverges`);
    if (byId.has(component.id) || byName.has(component.name)) fail(`duplicate dependency component ${component.name}`);
    byId.set(component.id, component); byName.set(component.name, component);
    if (!['function', 'arrow'].includes(component.kind)) fail(`unsupported component kind for ${component.name}`);
    for (const key of ['script_index', 'start', 'end', 'bytes']) assertInteger(component[key], `component ${component.name}.${key}`);
    if (component.end < component.start) fail(`component ${component.name} end precedes start`);
    assertDigest(component.sha256, `component ${component.name}.sha256`);
    if (component.owner !== null && (typeof component.owner !== 'string' || !ID_RE.test(component.owner))) fail(`component ${component.name} owner is invalid`);
    for (const key of ['dependencies', 'captures', 'external_bindings', 'unresolved']) {
      if (!Array.isArray(component[key]) || component[key].some(item => typeof item !== 'string')) fail(`component ${component.name}.${key} must be a string array`);
    }
    if (component.unresolved.length) fail(`unresolved identifiers for ${component.name}: ${component.unresolved.join(', ')}`);
    if (new Set(component.dependencies).size !== component.dependencies.length) fail(`component ${component.name} dependencies contain duplicates`);
  }
  for (const root of manifest.roots) if (typeof root !== 'string' || !byId.has(root)) fail(`root references unknown component ${JSON.stringify(root)}`);
  for (const component of manifest.components) {
    if (component.owner !== null && !byId.has(component.owner)) fail(`component ${component.name} owner references unknown id`);
    for (const dependency of component.dependencies) {
      if (!byId.has(dependency)) fail(`component ${component.name} dependency references unknown id ${dependency}`);
      if (dependency === component.id) fail(`component ${component.name} depends on itself`);
    }
  }
  const document = (() => {
    try { return JSON.parse(artifactBytes); } catch (error) { fail(`artifact JSON is invalid: ${error.message}`); }
  })();
  if (!isRecord(document) || typeof document.html !== 'string' || !document.html) fail('artifact html must be a non-empty string');
  const scripts = htmlScripts(document.html);
  if (!scripts.length) fail('artifact contains no JavaScript scripts');
  for (const component of manifest.components) {
    const script = scripts[component.script_index];
    if (!script) fail(`component ${component.name} references missing script ${component.script_index}`);
    const start = script.offsets[component.start];
    const end = script.offsets[component.end];
    if (start === undefined || end === undefined || end < start) fail(`component ${component.name} has invalid UTF-8 source range`);
    const source = Buffer.from(script.source, 'utf8').subarray(start, end);
    if (source.length !== component.bytes || sha256(source) !== component.source_sha256)
      fail(`component ${component.name} declaration bytes/hash do not match artifact`);
  }
  return { manifest, byId, scripts };
}

function authoredKey(component) { return [component.script_index, component.start, component.id]; }
function compareComponents(left, right) {
  const a = authoredKey(left), b = authoredKey(right);
  for (let i = 0; i < a.length; i++) { if (a[i] < b[i]) return -1; if (a[i] > b[i]) return 1; }
  return 0;
}
function dependencyOrder(manifest, byId) {
  const state = new Map(); const stack = []; const ordered = [];
  const visit = id => {
    const mark = state.get(id) || 0;
    if (mark === 2) return;
    if (mark === 1) {
      const index = stack.indexOf(id);
      fail(`dependency cycle: ${[...stack.slice(index), id].map(item => byId.get(item)?.name || item).join(' -> ')}`);
    }
    const component = byId.get(id); if (!component) fail(`dependency references unknown id ${id}`);
    state.set(id, 1); stack.push(id);
    [...component.dependencies].map(dep => byId.get(dep)).sort(compareComponents).forEach(dep => visit(dep.id));
    stack.pop(); state.set(id, 2); ordered.push(component);
  };
  [...manifest.roots].map(id => byId.get(id)).sort(compareComponents).forEach(root => visit(root.id));
  const reachable = new Set(ordered.map(component => component.id));
  for (const component of manifest.components) if (!reachable.has(component.id)) fail(`component ${component.name} is not reachable from roots`);
  return ordered;
}

function modulePath(name) { return path.join('components', `${name}.tsx`); }
function runConverter(converter, artifactPath, component, outputPath) {
  const result = spawnSync(process.execPath, [converter, '--artifact', artifactPath, '--component', component.name, '--out', outputPath], {
    cwd: scriptDir, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) fail(`converter process failed for ${component.name}: ${result.error.message}`);
  if (result.status !== 0) fail(`converter rejected ${component.name}: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
  let report;
  try { report = JSON.parse(result.stdout); } catch (error) { fail(`converter report for ${component.name} is invalid JSON: ${error.message}`); }
  if (report.component !== component.name || report.script_index !== component.script_index)
    fail(`converter identity mismatch for ${component.name}`);
  const output = readBytes(outputPath, `converted module ${component.name}`);
  if (report.output_bytes !== output.length || report.output_sha256 !== sha256(output))
    fail(`converter output hash mismatch for ${component.name}`);
  return { output, report };
}

function emissionRecord(component, output, relativePath) {
  return {
    id: component.id, name: component.name, kind: component.kind, owner: component.owner,
    dependencies: [...component.dependencies].sort(), captures: [...component.captures].sort(),
    source_sha256: component.source_sha256, source_bytes: component.bytes,
    output_sha256: sha256(output), output_bytes: output.length, path: relativePath,
  };
}

function validateEmission(raw, emissionPath, artifactPath, manifestPath, outDir) {
  const { value: emission } = (() => { try { return { value: JSON.parse(raw) }; } catch (error) { fail(`emission report JSON is invalid: ${error.message}`); } })();
  assertKeys(emission, EMISSION_KEYS, 'emission report');
  if (emission.schema !== SCHEMA || emission.version !== 1) fail('unsupported emission report identity');
  for (const [name, value] of [['artifact', emission.artifact], ['dependency_manifest', emission.dependency_manifest], ['converter', emission.converter]]) if (!isRecord(value)) fail(`emission ${name} must be an object`);
  assertKeys(emission.artifact, new Set(['path', 'sha256', 'bytes']), 'emission artifact');
  assertKeys(emission.dependency_manifest, new Set(['path', 'sha256']), 'emission dependency_manifest');
  assertKeys(emission.converter, new Set(['path', 'parser', 'parser_version']), 'emission converter');
  assertDigest(emission.artifact.sha256, 'emission artifact.sha256'); assertInteger(emission.artifact.bytes, 'emission artifact.bytes');
  assertDigest(emission.dependency_manifest.sha256, 'emission dependency_manifest.sha256');
  if (emission.converter.parser !== '@babel/parser' || emission.converter.parser_version !== '7.28.4') fail('unsupported emission converter parser');
  if (!Array.isArray(emission.roots) || !Array.isArray(emission.order) || !Array.isArray(emission.modules)) fail('emission roots/order/modules must be arrays');
  const artifactBytes = readBytes(artifactPath, 'artifact');
  const manifestBytes = readBytes(manifestPath, 'dependency manifest');
  if (emission.artifact.sha256 !== sha256(artifactBytes) || emission.artifact.bytes !== artifactBytes.length) fail('emission artifact identity changed');
  if (emission.dependency_manifest.sha256 !== sha256(manifestBytes)) fail('emission dependency manifest identity changed');
  const { manifest, byId } = validateManifest(manifestBytes, artifactBytes);
  const ordered = dependencyOrder(manifest, byId);
  if (JSON.stringify(emission.roots) !== JSON.stringify(manifest.roots)) fail('emission roots changed');
  if (JSON.stringify(emission.order) !== JSON.stringify(ordered.map(component => component.id))) fail('emission dependency order changed');
  if (emission.modules.length !== ordered.length) fail('emission module count changed');
  const modulesDir = path.join(outDir, 'components');
  if (!fs.existsSync(modulesDir) || !fs.statSync(modulesDir).isDirectory() || fs.lstatSync(modulesDir).isSymbolicLink()) fail('emission components directory is missing or symlinked');
  const expectedNames = new Set(ordered.map(component => `${component.name}.tsx`));
  for (const entry of fs.readdirSync(modulesDir)) if (entry.endsWith('.tsx') && !expectedNames.has(entry)) fail(`unexpected emitted module ${entry}`);
  for (let index = 0; index < ordered.length; index++) {
    const component = ordered[index], module = emission.modules[index];
    assertKeys(module, MODULE_KEYS, `emission module[${index}]`);
    if (module.id !== component.id || module.name !== component.name || module.kind !== component.kind || module.owner !== component.owner) fail(`emission component identity changed for ${component.name}`);
    if (JSON.stringify(module.dependencies) !== JSON.stringify([...component.dependencies].sort()) || JSON.stringify(module.captures) !== JSON.stringify([...component.captures].sort())) fail(`emission dependency metadata changed for ${component.name}`);
    if (module.source_sha256 !== component.source_sha256 || module.source_bytes !== component.bytes) fail(`emission source identity changed for ${component.name}`);
    if (module.path !== modulePath(component.name)) fail(`emission module path changed for ${component.name}`);
    assertDigest(module.output_sha256, `emission ${component.name}.output_sha256`); assertInteger(module.output_bytes, `emission ${component.name}.output_bytes`);
    const file = path.join(outDir, module.path);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile() || fs.lstatSync(file).isSymbolicLink()) fail(`emitted module missing or symlinked for ${component.name}`);
    const output = readBytes(file, `emitted module ${component.name}`);
    if (output.length !== module.output_bytes || sha256(output) !== module.output_sha256) fail(`emitted module output hash changed for ${component.name}`);
  }
  return { verified: true, component_count: ordered.length, output_sha256: sha256(Buffer.from(JSON.stringify(emission))) };
}

function emit({ artifactPath, manifestPath, outDir, converter }) {
  if (fs.existsSync(outDir)) fail(`output directory already exists: ${outDir}`);
  // Stage beside the destination so the final atomic rename remains on one
  // filesystem. A system temp directory can be a different volume from a
  // checkout (for example /var/folders versus /Volumes/Workshop), which turns
  // renameSync into EXDEV and breaks otherwise valid imports.
  fs.mkdirSync(path.dirname(outDir), { recursive: true });
  const artifactBytes = readBytes(artifactPath, 'artifact');
  const manifestBytes = readBytes(manifestPath, 'dependency manifest');
  const { manifest, byId } = validateManifest(manifestBytes, artifactBytes);
  const ordered = dependencyOrder(manifest, byId);
  const stage = fs.mkdtempSync(path.join(path.dirname(outDir), `.${path.basename(outDir)}.staging-`));
  try {
    const componentsDir = path.join(stage, 'components'); fs.mkdirSync(componentsDir, { recursive: true });
    const modules = [];
    for (const component of ordered) {
      const outputPath = path.join(componentsDir, `${component.name}.tsx`);
      const { output } = runConverter(converter, artifactPath, component, outputPath);
      modules.push(emissionRecord(component, output, modulePath(component.name)));
    }
    const emission = {
      schema: SCHEMA, version: 1,
      artifact: { path: path.basename(artifactPath), sha256: sha256(artifactBytes), bytes: artifactBytes.length },
      dependency_manifest: { path: path.basename(manifestPath), sha256: sha256(manifestBytes) },
      converter: { path: path.basename(converter), parser: '@babel/parser', parser_version: '7.28.4' },
      roots: [...manifest.roots], order: ordered.map(component => component.id), modules,
    };
    const emissionPath = path.join(stage, 'authored-modules.manifest.json');
    fs.writeFileSync(emissionPath, `${JSON.stringify(emission, null, 2)}\n`);
    validateEmission(fs.readFileSync(emissionPath), emissionPath, artifactPath, manifestPath, stage);
    fs.mkdirSync(path.dirname(outDir), { recursive: true });
    fs.renameSync(stage, outDir);
    return { output: path.resolve(outDir), manifest: path.resolve(outDir, 'authored-modules.manifest.json'), component_count: ordered.length };
  } catch (error) {
    fs.rmSync(stage, { recursive: true, force: true });
    throw error;
  }
}

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--artifact' || arg === '--manifest' || arg === '--out' || arg === '--converter') args[arg.slice(2)] = argv[++i];
    else if (arg === '--verify') args.verify = true;
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function usage() { console.log('usage: node tools/wp1_authored_module_emitter.mjs --artifact FILE --manifest FILE --out DIR [--verify] [--converter FILE]'); }
try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  if (!args.artifact || !args.manifest || !args.out) fail('--artifact, --manifest, and --out are required');
  const artifactPath = path.resolve(args.artifact), manifestPath = path.resolve(args.manifest), outDir = path.resolve(args.out);
  const converter = path.resolve(args.converter || defaultConverter);
  if (!fs.existsSync(artifactPath)) fail(`artifact does not exist: ${artifactPath}`);
  if (!fs.existsSync(manifestPath)) fail(`dependency manifest does not exist: ${manifestPath}`);
  if (!fs.existsSync(converter)) fail(`converter does not exist: ${converter}`);
  if (args.verify) {
    if (!fs.existsSync(outDir) || !fs.statSync(outDir).isDirectory()) fail(`output directory does not exist: ${outDir}`);
    const emissionPath = path.join(outDir, 'authored-modules.manifest.json');
    if (!fs.existsSync(emissionPath)) fail(`emission manifest does not exist: ${emissionPath}`);
    const result = validateEmission(fs.readFileSync(emissionPath), emissionPath, artifactPath, manifestPath, outDir);
    process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
  } else {
    process.stdout.write(`${JSON.stringify(emit({ artifactPath, manifestPath, outDir, converter }), null, 2)}\n`);
  }
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
