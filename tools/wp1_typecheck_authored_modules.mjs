#!/usr/bin/env node
/**
 * Validate the emitted WP-1 authored closure with the pinned TypeScript
 * compiler. This is a read-only build experiment: modules are assembled in a
 * temporary dependency-first script with generated ambient declarations for
 * the external runtime surface, then checked with `tsc --noEmit`.
 *
 * It proves that the current TSX conversion can be parsed and type-checked as
 * one dependency-resolved closure. It does not claim isolated module import
 * contracts, a production React type surface, runtime regeneration, or
 * native/browser parity.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCHEMA = 'spectr-owned-tsx-build-validation-v1';
const EMISSION_SCHEMA = 'spectr-owned-authored-module-emission-v1';
const NAME_RE = /^[A-Za-z_$][\w$]*$/;
const DIGEST_RE = /^[0-9a-f]{64}$/;
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const toolchainDir = path.join(scriptDir, 'wp1-parser');
const tscPath = path.join(toolchainDir, 'node_modules', 'typescript', 'bin', 'tsc');
const STANDARD_BINDINGS = new Set([
  'Array', 'Blob', 'Boolean', 'Date', 'document', 'Error', 'FileReader',
  'Float32Array', 'globalThis', 'Infinity', 'JSON', 'Map', 'Math', 'NaN',
  'navigator', 'Number', 'Object', 'performance', 'Promise', 'Set', 'String',
  'URL', 'URLSearchParams', 'window', 'console', 'undefined', 'arguments',
  'BigInt', 'Symbol', 'WeakMap', 'setTimeout', 'clearTimeout', 'setInterval',
  'clearInterval', 'requestAnimationFrame', 'cancelAnimationFrame',
  'parseInt', 'parseFloat', 'isFinite', 'Intl', 'RegExp', 'queueMicrotask',
]);

function fail(message) { throw new Error(`WP-1 TSX build validation failed: ${message}`); }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function isRecord(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function readBytes(file, label) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${label}: ${error.message}`); } }
function assertDigest(value, label) { if (typeof value !== 'string' || !DIGEST_RE.test(value)) fail(`${label} must be a SHA-256 digest`); }
function assertInteger(value, label) { if (!Number.isInteger(value) || value < 0) fail(`${label} must be a non-negative integer`); }
function assertKeys(value, expected, label) {
  if (!isRecord(value)) fail(`${label} must be an object`);
  const actual = new Set(Object.keys(value));
  const missing = [...expected].filter(key => !actual.has(key));
  const extra = [...actual].filter(key => !expected.has(key));
  if (missing.length || extra.length) fail(`${label} keys changed${missing.length ? `; missing ${missing.join(', ')}` : ''}${extra.length ? `; unexpected ${extra.join(', ')}` : ''}`);
}
function readJson(file, label) {
  const raw = readBytes(file, label);
  try { return { raw, value: JSON.parse(raw) }; } catch (error) { fail(`${label} JSON is invalid: ${error.message}`); }
}

function runEmitterVerify(artifact, manifest, output) {
  const emitter = path.join(scriptDir, 'wp1_authored_module_emitter.mjs');
  const result = spawnSync(process.execPath, [emitter, '--artifact', artifact, '--manifest', manifest, '--out', output, '--verify'], {
    cwd: scriptDir, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) fail(`emitter verification process failed: ${result.error.message}`);
  if (result.status !== 0) fail(`emitted module verification rejected the closure: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
  return result.stdout;
}

function ambientDeclarations(manifest) {
  const names = new Set(['React']);
  for (const component of manifest.components) {
    for (const name of component.external_bindings || []) if (NAME_RE.test(name) && !STANDARD_BINDINGS.has(name)) names.add(name);
  }
  const declarations = [...names].sort().map(name => `declare const ${name}: any;`);
  declarations.push('declare namespace JSX { interface IntrinsicElements { [elemName: string]: any; } }');
  return `${declarations.join('\n')}\n`;
}

function runTsc(stage, semantic) {
  if (!fs.existsSync(tscPath)) fail(`TypeScript compiler unavailable at ${tscPath}; run npm ci --prefix tools/wp1-parser`);
  const flags = ['--noEmit', '--target', 'ES2022', '--module', 'none', '--jsx', 'react', '--skipLibCheck', '--pretty', 'false'];
  if (!semantic) flags.push('--noCheck');
  const result = spawnSync(process.execPath, [tscPath, ...flags, 'globals.d.ts', 'program.tsx'], {
    cwd: stage, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024,
  });
  if (result.error) fail(`TypeScript compiler process failed: ${result.error.message}`);
  const diagnostics = `${result.stdout || ''}${result.stderr || ''}`.trim();
  if (result.status !== 0) fail(`TypeScript reported diagnostics:\n${diagnostics || '(no diagnostic text)'}`);
  return diagnostics;
}

function validateInputs(emissionPath, artifactPath, manifestPath, outputDir) {
  const { raw: emissionRaw, value: emission } = readJson(emissionPath, 'emission manifest');
  assertKeys(emission, new Set(['schema', 'version', 'artifact', 'dependency_manifest', 'converter', 'roots', 'order', 'modules']), 'emission manifest');
  if (emission.schema !== EMISSION_SCHEMA || emission.version !== 1) fail('unsupported emission manifest identity');
  if (!Array.isArray(emission.modules) || !emission.modules.length) fail('emission modules must be non-empty');
  const { value: manifest } = readJson(manifestPath, 'dependency manifest');
  if (!isRecord(manifest) || !Array.isArray(manifest.components)) fail('dependency manifest components are missing');
  const manifestById = new Map(manifest.components.map(component => [component.id, component]));
  const artifact = readBytes(artifactPath, 'artifact');
  if (!emission.artifact || emission.artifact.sha256 !== sha256(artifact)) fail('emission artifact does not match requested artifact');
  if (!emission.dependency_manifest || emission.dependency_manifest.sha256 !== sha256(readBytes(manifestPath, 'dependency manifest'))) fail('emission dependency manifest changed');
  const modulesDir = path.join(outputDir, 'components');
  if (!fs.existsSync(modulesDir) || !fs.statSync(modulesDir).isDirectory()) fail('emission components directory is missing');
  for (const module of emission.modules) {
    if (!isRecord(module) || !NAME_RE.test(module.name) || !manifestById.has(module.id)) fail(`emission module identity is invalid for ${module?.name || '<unknown>'}`);
    assertDigest(module.output_sha256, `${module.name}.output_sha256`); assertInteger(module.output_bytes, `${module.name}.output_bytes`);
    const file = path.join(outputDir, module.path);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) fail(`emission module file is missing for ${module.name}`);
    const bytes = readBytes(file, `emission module ${module.name}`);
    if (bytes.length !== module.output_bytes || sha256(bytes) !== module.output_sha256) fail(`emission module output hash changed for ${module.name}`);
  }
  return { emissionRaw, emission, manifest };
}

function validate({ artifactPath, manifestPath, emissionPath, outputDir, reportPath, semantic }) {
  runEmitterVerify(artifactPath, manifestPath, outputDir);
  const { emissionRaw, emission, manifest } = validateInputs(emissionPath, artifactPath, manifestPath, outputDir);
  const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-wp1-tsc-'));
  try {
    const moduleSources = [];
    for (const module of emission.modules) {
      const bytes = readBytes(path.join(outputDir, module.path), `emission module ${module.name}`);
      moduleSources.push(`// ${module.path} (${module.id})\n${bytes.toString('utf8')}\n`);
    }
    const program = moduleSources.join('\n');
    const globals = ambientDeclarations(manifest);
    fs.writeFileSync(path.join(stage, 'program.tsx'), program);
    fs.writeFileSync(path.join(stage, 'globals.d.ts'), globals);
    const diagnostics = runTsc(stage, semantic);
    const report = {
      schema: SCHEMA, version: 1, mode: semantic ? 'dependency-first-concatenated-script-semantic' : 'dependency-first-concatenated-script-syntax-only',
      artifact: { path: path.basename(artifactPath), sha256: sha256(readBytes(artifactPath, 'artifact')) },
      dependency_manifest: { path: path.basename(manifestPath), sha256: sha256(readBytes(manifestPath, 'dependency manifest')) },
      emission: { path: path.basename(emissionPath), sha256: sha256(emissionRaw), modules: emission.modules.length },
      typescript: { version: '5.9.3', compiler: 'tsc', semantic, diagnostics: diagnostics ? diagnostics.split('\n').length : 0 },
      files: [
        { path: 'program.tsx', bytes: Buffer.byteLength(program), sha256: sha256(Buffer.from(program)) },
        { path: 'globals.d.ts', bytes: Buffer.byteLength(globals), sha256: sha256(Buffer.from(globals)) },
      ],
    };
    fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
    return report;
  } finally {
    fs.rmSync(stage, { recursive: true, force: true });
  }
}

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--artifact' || arg === '--manifest' || arg === '--emission' || arg === '--out-report') args[arg.slice(2).replace('-', '_')] = argv[++i];
    else if (arg === '--semantic') args.semantic = true;
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function usage() { console.log('usage: node tools/wp1_typecheck_authored_modules.mjs --artifact FILE --manifest FILE --emission FILE --out-report FILE [--semantic]'); }
try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  if (!args.artifact || !args.manifest || !args.emission || !args.out_report) fail('--artifact, --manifest, --emission, and --out-report are required');
  const artifactPath = path.resolve(args.artifact), manifestPath = path.resolve(args.manifest), emissionPath = path.resolve(args.emission);
  const outputDir = path.dirname(emissionPath), reportPath = path.resolve(args.out_report);
  for (const [file, label] of [[artifactPath, 'artifact'], [manifestPath, 'dependency manifest'], [emissionPath, 'emission manifest']]) if (!fs.existsSync(file)) fail(`${label} does not exist: ${file}`);
  const report = validate({ artifactPath, manifestPath, emissionPath, outputDir, reportPath, semantic: !!args.semantic });
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
