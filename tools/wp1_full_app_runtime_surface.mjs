#!/usr/bin/env node
/**
 * Freeze the host-facing runtime surface used by the dependency-resolved App
 * closure.
 *
 * WP-1 already proves that the 59-module App closure is parser-backed and can
 * be assembled as TSX.  This gate adds the missing seam between authored
 * modules and the browser host: every static window/globalThis/document member
 * used by a module must be present in an artifact-pinned allowlist.  The gate
 * emits a small, typed declaration facade for those names and records exact
 * input/module/output hashes.  It never edits the canonical runtime artifact.
 *
 * A property is deliberately declared as an explicit `any` at this staging
 * boundary.  The name is typed and reviewable while payload types remain a
 * follow-up bridge-contract concern.  An index signature is intentionally not
 * emitted: a typo such as `window.pulpz` must fail the scanner.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCHEMA = 'spectr-owned-app-runtime-surface-v1';
const MANIFEST_SCHEMA = 'spectr-owned-component-dependency-v1';
const EMISSION_SCHEMA = 'spectr-owned-authored-module-emission-v1';
const NAME_RE = /^[A-Za-z_$][\w$]*$/;
const DIGEST_RE = /^[0-9a-f]{64}$/;
const OBJECTS = ['window', 'globalThis', 'document'];
const STANDARD = {
  window: new Set(['addEventListener', 'removeEventListener', 'devicePixelRatio', 'parent']),
  globalThis: new Set(),
  document: new Set(['activeElement', 'addEventListener', 'createElement', 'getElementById', 'hasFocus', 'querySelector', 'querySelectorAll', 'removeEventListener']),
};
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const parserPath = path.join(scriptDir, 'wp1-parser', 'node_modules', '@babel', 'parser');
const tscPath = path.join(scriptDir, 'wp1-parser', 'node_modules', 'typescript', 'bin', 'tsc');
let parse;
try {
  const babel = createRequire(import.meta.url)(parserPath);
  parse = babel.parse || babel.default?.parse;
} catch (error) {
  fail(`WP-1 parser unavailable at ${parserPath}: ${error.message}; run npm ci --ignore-scripts --prefix tools/wp1-parser`);
}
if (typeof parse !== 'function') fail('WP-1 parser toolchain has no parse() API');

function fail(message) { throw new Error(`WP-1 full-App runtime surface failed: ${message}`); }
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function isRecord(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function readBytes(file, label) {
  try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${label}: ${error.message}`); }
}
function readJson(file, label) {
  const raw = readBytes(file, label);
  try { return { raw, value: JSON.parse(raw) }; }
  catch (error) { fail(`${label} JSON is invalid: ${error.message}`); }
}
function assertDigest(value, label) { if (typeof value !== 'string' || !DIGEST_RE.test(value)) fail(`${label} must be a lowercase SHA-256 digest`); }
function assertInteger(value, label) { if (!Number.isInteger(value) || value < 0) fail(`${label} must be a non-negative integer`); }
function assertKeys(value, expected, label) {
  if (!isRecord(value)) fail(`${label} must be an object`);
  const actual = new Set(Object.keys(value));
  const missing = [...expected].filter(key => !actual.has(key)).sort();
  const extra = [...actual].filter(key => !expected.has(key)).sort();
  if (missing.length || extra.length)
    fail(`${label} keys changed${missing.length ? `; missing ${missing.join(', ')}` : ''}${extra.length ? `; unexpected ${extra.join(', ')}` : ''}`);
}
function sorted(values) { return [...values].sort(); }

function validateAllowlist(value, artifactBytes) {
  assertKeys(value, new Set(['schema', 'version', 'artifact', 'objects']), 'runtime surface allowlist');
  if (value.schema !== 'spectr-owned-app-runtime-surface-allowlist-v1' || value.version !== 1)
    fail('unsupported runtime surface allowlist identity');
  assertKeys(value.artifact, new Set(['sha256', 'bytes']), 'runtime surface allowlist artifact');
  assertDigest(value.artifact.sha256, 'runtime surface allowlist artifact.sha256');
  assertInteger(value.artifact.bytes, 'runtime surface allowlist artifact.bytes');
  if (value.artifact.sha256 !== sha256(artifactBytes) || value.artifact.bytes !== artifactBytes.length)
    fail('runtime surface allowlist is pinned to a different artifact');
  const objects = {};
  for (const object of OBJECTS) {
    const entry = value.objects?.[object];
    assertKeys(entry, new Set(['properties']), `runtime surface allowlist ${object}`);
    if (!Array.isArray(entry.properties)) fail(`runtime surface allowlist ${object}.properties must be an array`);
    const names = new Set();
    const properties = [];
    for (const [index, property] of entry.properties.entries()) {
      assertKeys(property, new Set(['name', 'classification']), `runtime surface allowlist ${object}[${index}]`);
      if (typeof property.name !== 'string' || !NAME_RE.test(property.name)) fail(`invalid ${object} property name at index ${index}`);
      if (!['standard-dom', 'plugin-runtime'].includes(property.classification)) fail(`invalid ${object}.${property.name} classification`);
      if (STANDARD[object].has(property.name) !== (property.classification === 'standard-dom'))
        fail(`${object}.${property.name} has an invalid standard/plugin classification`);
      if (names.has(property.name)) fail(`duplicate ${object}.${property.name}`);
      names.add(property.name); properties.push({ name: property.name, classification: property.classification });
    }
    if (JSON.stringify(properties.map(property => property.name)) !== JSON.stringify(sorted(names)))
      fail(`${object} runtime surface properties must be in canonical name order`);
    objects[object] = { names, properties };
  }
  return objects;
}

function parseModule(source, label) {
  try {
    return parse(source, { sourceType: 'module', plugins: ['jsx', 'typescript'], errorRecovery: false, attachComment: false });
  } catch (error) { fail(`module ${label} is not parseable TSX: ${error.message}`); }
}

function memberName(node, object, label) {
  if (!node.computed && node.property?.type === 'Identifier') return node.property.name;
  if (node.computed && ['StringLiteral', 'Literal'].includes(node.property?.type) && typeof node.property.value === 'string') return node.property.value;
  fail(`${label} uses a dynamic ${object} member; stable runtime-surface names must be static`);
}

function scanModule(source, label) {
  const ast = parseModule(source, label);
  const used = Object.fromEntries(OBJECTS.map(object => [object, new Set()]));
  const aliases = new Map();
  // Resolve the simple alias form (`const host = window`) before scanning
  // uses.  Without this pre-pass an authored module could hide a host access
  // behind an alias and evade the allowlist.  Destructuring and computed alias
  // forms stay fail-closed until the facade can model them explicitly.
  const bindings = new Map();
  const rootAliasSources = new Map();
  function bindingNames(pattern, names = []) {
    if (!pattern) return names;
    if (pattern.type === 'Identifier') names.push(pattern.name);
    else if (pattern.type === 'RestElement') bindingNames(pattern.argument, names);
    else if (pattern.type === 'AssignmentPattern') bindingNames(pattern.left, names);
    else if (pattern.type === 'ArrayPattern') pattern.elements.forEach(item => bindingNames(item, names));
    else if (pattern.type === 'ObjectPattern') pattern.properties.forEach(property => {
      if (property.type === 'RestElement') bindingNames(property.argument, names);
      else bindingNames(property.value || property.argument, names);
    });
    return names;
  }
  function recordBinding(name) {
    if (OBJECTS.includes(name)) fail(`${label} shadows the runtime object ${name}`);
    bindings.set(name, (bindings.get(name) || 0) + 1);
  }
  function collectAliases(node) {
    if (!node || typeof node !== 'object') return;
    if (node.type === 'VariableDeclarator') {
      for (const name of bindingNames(node.id)) recordBinding(name);
      if (node.init?.type === 'Identifier' && OBJECTS.includes(node.init.name)) {
        if (node.id?.type === 'Identifier') {
          const roots = rootAliasSources.get(node.id.name) || new Set();
          roots.add(node.init.name); rootAliasSources.set(node.id.name, roots);
        } else if (node.id?.type === 'ObjectPattern') {
          fail(`${label} destructures ${node.init.name}; runtime-surface aliasing is unsupported`);
        }
      }
    }
    if (['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(node.type)) {
      for (const parameter of node.params || []) for (const name of bindingNames(parameter)) recordBinding(name);
    }
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) value.forEach(collectAliases);
      else if (value && typeof value === 'object') collectAliases(value);
    }
  }
  collectAliases(ast);
  for (const [name, roots] of rootAliasSources) {
    if (OBJECTS.includes(name)) fail(`${label} shadows the runtime object ${name}`);
    if (roots.size !== 1 || bindings.get(name) !== 1)
      fail(`${label} rebinds runtime-surface alias ${name}; lexical shadowing is unsupported`);
    aliases.set(name, [...roots][0]);
  }
  function rootObject(node) {
    if (node?.type !== 'Identifier') return null;
    if (OBJECTS.includes(node.name)) return node.name;
    const seen = new Set(); let name = node.name;
    while (aliases.has(name) && !seen.has(name)) { seen.add(name); name = aliases.get(name); }
    return OBJECTS.includes(name) ? name : null;
  }
  function visit(node) {
    if (!node || typeof node !== 'object') return;
    if (['MemberExpression', 'OptionalMemberExpression'].includes(node.type)) {
      const object = rootObject(node.object);
      if (object) used[object].add(memberName(node, object, label));
    }
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) value.forEach(visit);
      else if (value && typeof value === 'object') visit(value);
    }
  }
  visit(ast);
  return used;
}

function validateArtifactManifest(artifactBytes, manifestBytes) {
  const artifact = (() => { try { return JSON.parse(artifactBytes); } catch (error) { fail(`artifact JSON is invalid: ${error.message}`); } })();
  if (!isRecord(artifact) || typeof artifact.html !== 'string' || !artifact.html) fail('artifact html must be a non-empty string');
  const { value: manifest } = (() => { try { return { value: JSON.parse(manifestBytes) }; } catch (error) { fail(`dependency manifest JSON is invalid: ${error.message}`); } })();
  if (!isRecord(manifest) || manifest.schema !== MANIFEST_SCHEMA || !Array.isArray(manifest.components)) fail('dependency manifest schema/components are invalid');
  if (!isRecord(manifest.source) || manifest.source.sha256 !== sha256(artifactBytes) || manifest.source.bytes !== artifactBytes.length)
    fail('dependency manifest does not identify the requested artifact');
  if (!Array.isArray(manifest.roots) || manifest.roots.length !== 1) fail('full-App gate requires exactly one root');
  const byId = new Map();
  const byName = new Map();
  for (const component of manifest.components) {
    if (!isRecord(component) || typeof component.id !== 'string' || !NAME_RE.test(component.name)) fail('dependency manifest component identity is invalid');
    if (byId.has(component.id) || byName.has(component.name)) fail(`duplicate dependency component ${component.name}`);
    byId.set(component.id, component); byName.set(component.name, component);
  }
  const rootNames = manifest.roots.map(id => {
    const component = byId.get(id);
    if (!component) fail(`dependency manifest root references unknown component ${id}`);
    return component.name;
  });
  if (rootNames[0] !== 'App') fail('full-App gate requires an App root');
  for (const component of manifest.components) {
    if (!Array.isArray(component.dependencies)) fail(`dependency list is missing for ${component.name}`);
    for (const dependency of component.dependencies) if (!byId.has(dependency)) fail(`unknown dependency ${dependency} for ${component.name}`);
  }
  return { artifact, manifest, byId };
}

function validateEmission(emissionDir, artifactBytes, manifestBytes, manifest, byId) {
  const emissionPath = path.join(emissionDir, 'authored-modules.manifest.json');
  const { raw, value: emission } = readJson(emissionPath, 'authored module emission manifest');
  assertKeys(emission, new Set(['schema', 'version', 'artifact', 'dependency_manifest', 'converter', 'roots', 'order', 'modules']), 'authored module emission');
  if (emission.schema !== EMISSION_SCHEMA || emission.version !== 1) fail('unsupported authored module emission identity');
  if (emission.artifact?.sha256 !== sha256(artifactBytes) || emission.artifact?.bytes !== artifactBytes.length) fail('emission artifact identity changed');
  if (emission.dependency_manifest?.sha256 !== sha256(manifestBytes)) fail('emission dependency manifest identity changed');
  if (!Array.isArray(emission.modules) || !emission.modules.length) fail('emission contains no modules');
  if (JSON.stringify(emission.roots) !== JSON.stringify(manifest.roots)) fail('emission roots do not match the App manifest root');
  if (!Array.isArray(emission.order) || emission.order.length !== emission.modules.length)
    fail('emission dependency order is missing or incomplete');
  const ids = new Set(); const modules = [];
  for (const module of emission.modules) {
    const component = byId.get(module.id);
    if (!component || module.name !== component.name) fail(`emission module identity is invalid for ${module?.name || '<unknown>'}`);
    if (module.path !== path.join('components', `${component.name}.tsx`)) fail(`unsafe emitted module path for ${component.name}`);
    if (ids.has(module.id)) fail(`duplicate emitted module ${component.name}`); ids.add(module.id);
    const file = path.join(emissionDir, module.path);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile() || fs.lstatSync(file).isSymbolicLink()) fail(`emitted module missing or symlinked for ${component.name}`);
    const bytes = readBytes(file, `emitted module ${component.name}`);
    if (bytes.length !== module.output_bytes || sha256(bytes) !== module.output_sha256) fail(`emitted module identity changed for ${component.name}`);
    if (emission.order[modules.length] !== module.id) fail(`emission dependency order disagrees at ${component.name}`);
    modules.push({ component, module, file, bytes });
  }
  for (const component of manifest.components) if (!ids.has(component.id)) fail(`manifest component ${component.name} is absent from emitted App closure`);
  return { emission, raw, modules };
}

function runEmitterVerify(artifactPath, manifestPath, emissionDir) {
  const emitter = path.join(scriptDir, 'wp1_authored_module_emitter.mjs');
  const result = spawnSync(process.execPath, [emitter, '--artifact', artifactPath, '--manifest', manifestPath, '--out', emissionDir, '--verify'], {
    cwd: scriptDir, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) fail(`authored module verifier could not run: ${result.error.message}`);
  if (result.status !== 0) fail(`authored module verifier rejected the App closure: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
}

function surfaceDeclaration(objects) {
  const lines = [
    '// Generated by tools/wp1_full_app_runtime_surface.mjs; do not edit.',
    'type SpectrWp1RuntimeValue = any;',
  ];
  for (const object of OBJECTS) {
    const custom = objects[object].properties.filter(property => property.classification === 'plugin-runtime');
    if (!custom.length) continue;
    const interfaceName = object === 'window' ? 'Window' : object === 'globalThis' ? 'GlobalThis' : 'Document';
    lines.push(`interface ${interfaceName} {`);
    for (const property of custom) lines.push(`  ${property.name}?: SpectrWp1RuntimeValue;`);
    lines.push('}');
  }
  return `${lines.join('\n')}\n`;
}

function surfaceProbe(objects) {
  const lines = ['// Generated probe: every declared runtime member is type-visible.'];
  for (const object of OBJECTS) {
    for (const property of objects[object].properties.filter(item => item.classification === 'plugin-runtime'))
      lines.push(`void ${object}.${property.name};`);
  }
  return `${lines.join('\n')}\n`;
}

function runTypeScript(stage) {
  if (!fs.existsSync(tscPath)) fail(`TypeScript compiler unavailable at ${tscPath}; run npm ci --ignore-scripts --prefix tools/wp1-parser`);
  const result = spawnSync(process.execPath, [tscPath, '--noEmit', '--target', 'ES2022', '--module', 'None', '--skipLibCheck', '--pretty', 'false', 'runtime-surface.d.ts', 'runtime-surface.probe.ts'], { cwd: stage, encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 });
  const diagnostics = `${result.stdout || ''}${result.stderr || ''}`.trim();
  if (result.error) fail(`TypeScript facade process failed: ${result.error.message}`);
  if (result.status !== 0) fail(`generated runtime facade is not type-checkable:\n${diagnostics || '(no diagnostics)'}`);
  return diagnostics;
}

function build({ artifactPath, manifestPath, emissionDir, allowlistPath, outDir }) {
  if (fs.existsSync(outDir)) fail(`output directory already exists: ${outDir}`);
  const artifactBytes = readBytes(artifactPath, 'artifact');
  const manifestBytes = readBytes(manifestPath, 'dependency manifest');
  const { raw: allowlistRaw, value: allowlist } = readJson(allowlistPath, 'runtime surface allowlist');
  const objects = validateAllowlist(allowlist, artifactBytes);
  const { manifest, byId } = validateArtifactManifest(artifactBytes, manifestBytes);
  runEmitterVerify(artifactPath, manifestPath, emissionDir);
  const { raw: emissionRaw, emission, modules } = validateEmission(emissionDir, artifactBytes, manifestBytes, manifest, byId);
  const used = Object.fromEntries(OBJECTS.map(object => [object, new Set()]));
  const moduleRecords = [];
  for (const { component, module, bytes } of modules) {
    const moduleUsed = scanModule(bytes.toString('utf8'), component.name);
    for (const object of OBJECTS) for (const name of moduleUsed[object]) {
      if (!objects[object].names.has(name)) fail(`${component.name} uses undeclared ${object}.${name}`);
      used[object].add(name);
    }
    moduleRecords.push({
      id: component.id, name: component.name, path: module.path,
      source_sha256: module.source_sha256, source_bytes: module.source_bytes,
      emitted_sha256: module.output_sha256, emitted_bytes: module.output_bytes,
      used: Object.fromEntries(OBJECTS.map(object => [object, sorted(moduleUsed[object])])),
    });
  }
  const stage = fs.mkdtempSync(path.join(path.dirname(outDir), `.${path.basename(outDir)}.staging-`));
  try {
    const facade = Buffer.from(surfaceDeclaration(objects));
    const probe = Buffer.from(surfaceProbe(objects));
    fs.writeFileSync(path.join(stage, 'runtime-surface.d.ts'), facade);
    fs.writeFileSync(path.join(stage, 'runtime-surface.probe.ts'), probe);
    const diagnostics = runTypeScript(stage);
    const contract = {
      schema: SCHEMA, version: 1, root: 'App', module_count: modules.length,
      artifact: { path: path.basename(artifactPath), sha256: sha256(artifactBytes), bytes: artifactBytes.length },
      dependency_manifest: { path: path.basename(manifestPath), sha256: sha256(manifestBytes), bytes: manifestBytes.length },
      emission: { path: path.basename(emissionDir), sha256: sha256(emissionRaw), modules: emission.modules.length },
      allowlist: { path: path.basename(allowlistPath), sha256: sha256(allowlistRaw), bytes: allowlistRaw.length },
      typescript: { version: '5.9.3', diagnostics: diagnostics ? diagnostics.split('\n').length : 0 },
      runtime_surface: Object.fromEntries(OBJECTS.map(object => [object, {
        declared: sorted(objects[object].names), used: sorted(used[object]),
        unused: sorted([...objects[object].names].filter(name => !used[object].has(name))),
      }])),
      files: [
        { path: 'runtime-surface.d.ts', sha256: sha256(facade), bytes: facade.length },
        { path: 'runtime-surface.probe.ts', sha256: sha256(probe), bytes: probe.length },
      ],
      modules: moduleRecords,
    };
    fs.writeFileSync(path.join(stage, 'runtime-surface-contract.json'), `${JSON.stringify(contract, null, 2)}\n`);
    fs.mkdirSync(path.dirname(outDir), { recursive: true });
    fs.renameSync(stage, outDir);
    return contract;
  } catch (error) {
    fs.rmSync(stage, { recursive: true, force: true });
    throw error;
  }
}

function verify({ artifactPath, manifestPath, emissionDir, allowlistPath, outDir }) {
  const contractPath = path.join(outDir, 'runtime-surface-contract.json');
  const { raw: suppliedRaw, value: supplied } = readJson(contractPath, 'runtime surface contract');
  assertKeys(supplied, new Set(['schema', 'version', 'root', 'module_count', 'artifact', 'dependency_manifest', 'emission', 'allowlist', 'typescript', 'runtime_surface', 'files', 'modules']), 'runtime surface contract');
  if (supplied.schema !== SCHEMA || supplied.version !== 1 || supplied.root !== 'App') fail('unsupported runtime surface contract');
  for (const file of supplied.files) {
    assertKeys(file, new Set(['path', 'sha256', 'bytes']), 'runtime surface contract file'); assertDigest(file.sha256, `${file.path}.sha256`); assertInteger(file.bytes, `${file.path}.bytes`);
    const bytes = readBytes(path.join(outDir, file.path), `runtime surface output ${file.path}`);
    if (bytes.length !== file.bytes || sha256(bytes) !== file.sha256) fail(`runtime surface output identity changed for ${file.path}`);
  }
  const stage = fs.mkdtempSync(path.join(path.dirname(outDir), `.${path.basename(outDir)}.verify-`));
  try {
    const rebuilt = path.join(stage, 'rebuilt');
    build({ artifactPath, manifestPath, emissionDir, allowlistPath, outDir: rebuilt });
    const rebuiltRaw = readBytes(path.join(rebuilt, 'runtime-surface-contract.json'), 'rebuilt runtime surface contract');
    if (sha256(rebuiltRaw) !== sha256(Buffer.from(suppliedRaw))) fail('runtime surface regeneration differs from supplied receipt');
  } finally { fs.rmSync(stage, { recursive: true, force: true }); }
  process.stdout.write(`${JSON.stringify({ verified: true, modules: supplied.module_count, contract_sha256: sha256(Buffer.from(suppliedRaw)) }, null, 2)}\n`);
}

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (['--artifact', '--manifest', '--emission', '--allowlist', '--out'].includes(arg)) args[arg.slice(2)] = argv[++index];
    else if (arg === '--verify') args.verify = true;
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function usage() { console.log('usage: node tools/wp1_full_app_runtime_surface.mjs --artifact FILE --manifest FILE --emission DIR --allowlist FILE --out DIR [--verify]'); }

try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  for (const name of ['artifact', 'manifest', 'emission', 'allowlist', 'out']) if (!args[name]) fail(`--${name} is required`);
  const inputNames = { artifact: 'artifactPath', manifest: 'manifestPath', emission: 'emissionDir', allowlist: 'allowlistPath', out: 'outDir' };
  const input = Object.fromEntries(Object.entries(inputNames).map(([name, key]) => [key, path.resolve(args[name])]));
  if (args.verify) verify(input); else process.stdout.write(`${JSON.stringify(build(input), null, 2)}\n`);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
