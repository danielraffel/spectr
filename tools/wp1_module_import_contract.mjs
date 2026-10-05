#!/usr/bin/env node
/**
 * Prove a dependency-first authored TSX closure can be represented as real ES
 * modules without changing the frozen runtime artifact.
 *
 * This is a staging-only WP-1 experiment. It adds deterministic imports and
 * exports around already-emitted modules, then asks the pinned TypeScript
 * compiler to resolve and type-check those files. Components with captures or
 * owners are rejected: an import cannot reproduce a lexical closure, and
 * silently lifting one would change runtime semantics.
 * Runtime/browser binding imports are deliberately not claimed here: globals.d.ts
 * is an ambient compile-time facade only. A later runtime-facade slice must prove
 * those bindings as actual module exports before isolated browser execution.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCHEMA = 'spectr-owned-module-import-contract-v1';
const EMISSION_SCHEMA = 'spectr-owned-authored-module-emission-v1';
const MANIFEST_SCHEMA = 'spectr-owned-component-dependency-v1';
const NAME_RE = /^[A-Za-z_$][\w$]*$/;
const DIGEST_RE = /^[0-9a-f]{64}$/;
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const toolchainDir = path.join(scriptDir, 'wp1-parser');
const tscPath = path.join(toolchainDir, 'node_modules', 'typescript', 'bin', 'tsc');
const parserPath = path.join(toolchainDir, 'node_modules', '@babel', 'parser');
const babel = createRequire(import.meta.url)(parserPath);
const parse = babel.parse || babel.default?.parse;

const STANDARD_BINDINGS = new Set([
  'React', 'Array', 'Blob', 'Boolean', 'Date', 'document', 'Error', 'FileReader',
  'Float32Array', 'globalThis', 'Infinity', 'JSON', 'Map', 'Math', 'NaN',
  'navigator', 'Number', 'Object', 'performance', 'Promise', 'Set', 'String',
  'URL', 'URLSearchParams', 'window', 'console', 'undefined', 'arguments',
  'BigInt', 'Symbol', 'WeakMap', 'setTimeout', 'clearTimeout', 'setInterval',
  'clearInterval', 'requestAnimationFrame', 'cancelAnimationFrame',
  'parseInt', 'parseFloat', 'isFinite', 'Intl', 'RegExp', 'queueMicrotask',
  'claimDocumentNavigationFocus', 'releaseDocumentNavigationFocus',
]);

function fail(message) { throw new Error(`WP-1 module import contract failed: ${message}`); }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function isRecord(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function readBytes(file, label) {
  try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${label}: ${error.message}`); }
}
function readJson(file, label) {
  const raw = readBytes(file, label);
  try { return { raw, value: JSON.parse(raw) }; } catch (error) { fail(`${label} JSON is invalid: ${error.message}`); }
}
function assertDigest(value, label) {
  if (typeof value !== 'string' || !DIGEST_RE.test(value)) fail(`${label} must be a lowercase SHA-256 digest`);
}
function assertInteger(value, label) {
  if (!Number.isInteger(value) || value < 0) fail(`${label} must be a non-negative integer`);
}
function assertKeys(value, expected, label) {
  if (!isRecord(value)) fail(`${label} must be an object`);
  const actual = new Set(Object.keys(value));
  const missing = [...expected].filter((key) => !actual.has(key)).sort();
  const extra = [...actual].filter((key) => !expected.has(key)).sort();
  if (missing.length || extra.length)
    fail(`${label} keys changed${missing.length ? `; missing ${missing.join(', ')}` : ''}${extra.length ? `; unexpected ${extra.join(', ')}` : ''}`);
}

function namesFromPattern(node, out = new Set()) {
  if (!node) return out;
  if (node.type === 'Identifier') out.add(node.name);
  else if (node.type === 'RestElement') namesFromPattern(node.argument, out);
  else if (node.type === 'AssignmentPattern') namesFromPattern(node.left, out);
  else if (node.type === 'ArrayPattern') node.elements.forEach((item) => namesFromPattern(item, out));
  else if (node.type === 'ObjectPattern') node.properties.forEach((property) => {
    if (property.type === 'RestElement') namesFromPattern(property.argument, out);
    else namesFromPattern(property.value || property.argument, out);
  });
  return out;
}

function artifactScripts(artifact) {
  if (!isRecord(artifact) || typeof artifact.html !== 'string' || !artifact.html)
    fail('artifact html must be a non-empty string');
  const scripts = [];
  const re = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;
  for (const match of artifact.html.matchAll(re)) {
    const attrs = match[1] || '';
    const typeMatch = attrs.match(/\btype\s*=\s*["']([^"']+)["']/i);
    const type = (typeMatch?.[1] || 'text/javascript').toLowerCase();
    if (type === 'application/json' || type === 'importmap') continue;
    if (!/^(?:text|application)\/(?:java|ecma)script$/.test(type) && type !== 'module') continue;
    scripts.push(match[2]);
  }
  if (!scripts.length) fail('artifact contains no JavaScript scripts');
  return scripts;
}

function provenAmbientBindings(artifact) {
  if (typeof parse !== 'function') fail('WP-1 parser toolchain has no parse() API');
  const allowed = new Set(STANDARD_BINDINGS);
  artifactScripts(artifact).forEach((source, index) => {
    let ast;
    try {
      ast = parse(source, { sourceType: 'script', plugins: ['jsx', 'typescript'], errorRecovery: false });
    } catch (error) { fail(`artifact script ${index} parser rejected source: ${error.message}`); }
    for (const statement of ast.program.body) {
      if (statement.type === 'FunctionDeclaration' && statement.id) allowed.add(statement.id.name);
      if (statement.type === 'ClassDeclaration' && statement.id) allowed.add(statement.id.name);
      if (statement.type === 'VariableDeclaration')
        statement.declarations.forEach((declaration) => namesFromPattern(declaration.id, allowed));
    }
  });
  return allowed;
}

function ambientDeclarations(manifest, artifact) {
  const allowed = provenAmbientBindings(artifact);
  const names = new Set(['React']);
  for (const component of manifest.components) {
    if (new Set(component.external_bindings || []).size !== (component.external_bindings || []).length)
      fail(`component ${component.name} external_bindings contain duplicates`);
    for (const name of component.external_bindings || []) {
      if (!NAME_RE.test(name)) fail(`component ${component.name} external binding is not an identifier: ${name}`);
      if (!allowed.has(name)) fail(`component ${component.name} external binding ${name} is not proven by artifact scope`);
      if (!STANDARD_BINDINGS.has(name)) names.add(name);
    }
  }
  const declarations = [...names].sort().map((name) => `declare const ${name}: any;`);
  declarations.push('declare namespace JSX { interface IntrinsicElements { [elemName: string]: any; } }');
  return `${declarations.join('\n')}\n`;
}

function validateManifest(manifest, artifactBytes) {
  if (!isRecord(manifest) || manifest.schema !== MANIFEST_SCHEMA || !Array.isArray(manifest.components))
    fail('dependency manifest schema/components are invalid');
  if (!isRecord(manifest.source) || manifest.source.kind !== 'artifact') fail('manifest source must be an artifact');
  if (manifest.source.sha256 !== sha256(artifactBytes)) fail('manifest artifact identity changed');
  const byId = new Map();
  const byName = new Map();
  for (const component of manifest.components) {
    if (!isRecord(component) || !NAME_RE.test(component.name) || typeof component.id !== 'string')
      fail('manifest component identity is invalid');
    if (byId.has(component.id) || byName.has(component.name)) fail(`duplicate manifest component ${component.name}`);
    byId.set(component.id, component); byName.set(component.name, component);
    if (!Array.isArray(component.dependencies) || !Array.isArray(component.captures))
      fail(`manifest dependency metadata is invalid for ${component.name}`);
    for (const dependency of component.dependencies) if (!byId.has(dependency)) {
      // Forward references are valid; resolve them after the complete index is built.
    }
  }
  for (const component of manifest.components) {
    for (const dependency of component.dependencies) {
      if (!byId.has(dependency)) fail(`dependency references unknown id ${dependency}`);
      if (dependency === component.id) fail(`component ${component.name} depends on itself`);
    }
  }
  return { byId, byName };
}

function validateEmission(emissionDir, artifactBytes, manifestBytes, manifest) {
  const emissionPath = path.join(emissionDir, 'authored-modules.manifest.json');
  const { raw, value: emission } = readJson(emissionPath, 'authored module emission manifest');
  assertKeys(emission, new Set(['schema', 'version', 'artifact', 'dependency_manifest', 'converter', 'roots', 'order', 'modules']), 'emission manifest');
  if (emission.schema !== EMISSION_SCHEMA || emission.version !== 1) fail('unsupported emission manifest');
  if (emission.artifact?.sha256 !== sha256(artifactBytes)) fail('emission artifact identity changed');
  if (emission.dependency_manifest?.sha256 !== sha256(manifestBytes)) fail('emission dependency manifest identity changed');
  if (!Array.isArray(emission.modules) || !emission.modules.length) fail('emission modules are empty');
  const byId = new Map(manifest.components.map((component) => [component.id, component]));
  const modules = [];
  const moduleNames = new Set();
  for (const module of emission.modules) {
    const component = byId.get(module.id);
    if (!component || module.name !== component.name) fail(`emission module identity is invalid for ${module.name || '<unknown>'}`);
    if (module.path !== path.join('components', `${component.name}.tsx`)) fail(`unsafe emitted module path for ${component.name}`);
    if (component.owner !== null || component.captures.length)
      fail(`cannot create a standalone ES module for captured component ${component.name}`);
    if (moduleNames.has(module.name)) fail(`duplicate emitted module ${module.name}`);
    moduleNames.add(module.name);
    const sourcePath = path.join(emissionDir, module.path);
    const source = readBytes(sourcePath, `emitted module ${component.name}`);
    if (sha256(source) !== module.output_sha256 || source.length !== module.output_bytes)
      fail(`emitted module source identity changed for ${component.name}`);
    modules.push({ component, module, source });
  }
  const ids = new Set(modules.map(({ component }) => component.id));
  for (const { component } of modules) for (const dependency of component.dependencies) {
    if (!ids.has(dependency)) fail(`dependency ${dependency} for ${component.name} is not in emitted closure`);
  }
  return { emission, emissionRaw: raw, modules };
}

function transformedModule(component, source, byId) {
  const imports = [...component.dependencies].sort().map((id) => {
    const dependency = byId.get(id);
    if (!dependency) fail(`dependency references unknown id ${id}`);
    return `import { ${dependency.name} } from './${dependency.name}';`;
  });
  const body = source.toString('utf8');
  let ast;
  try {
    ast = parse(body, { sourceType: 'module', plugins: ['jsx', 'typescript'], errorRecovery: false });
  } catch (error) { fail(`authored module ${component.name} is not parseable as TSX: ${error.message}`); }
  if (ast.program.body.some((statement) => statement.type === 'ImportDeclaration' || statement.type.startsWith('Export')))
    fail(`authored module ${component.name} already contains module syntax`);
  const prefix = imports.length ? `${imports.join('\n')}\n\n` : '';
  return `${prefix}${body}\nexport { ${component.name} };\n`;
}

function runEmitterVerify(artifactPath, manifestPath, emissionDir) {
  const emitter = path.join(scriptDir, 'wp1_authored_module_emitter.mjs');
  const result = spawnSync(process.execPath, [emitter, '--artifact', artifactPath, '--manifest', manifestPath, '--out', emissionDir, '--verify'], {
    cwd: scriptDir, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) fail(`emitter verification process failed: ${result.error.message}`);
  if (result.status !== 0)
    fail(`emitted module closure verification rejected the contract: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
}

function runEmitterRegenerationCheck(artifactPath, manifestPath, emissionDir) {
  const regenerationRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-wp1-import-emission-'));
  const regenerated = path.join(regenerationRoot, 'emitted');
  try {
    const emitter = path.join(scriptDir, 'wp1_authored_module_emitter.mjs');
    const result = spawnSync(process.execPath, [emitter, '--artifact', artifactPath, '--manifest', manifestPath, '--out', regenerated], {
      cwd: scriptDir, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
    });
    if (result.error) fail(`emitter regeneration process failed: ${result.error.message}`);
    if (result.status !== 0)
      fail(`emitted module regeneration rejected the contract: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
    const expectedManifest = readBytes(path.join(regenerated, 'authored-modules.manifest.json'), 'regenerated emission manifest');
    const actualManifest = readBytes(path.join(emissionDir, 'authored-modules.manifest.json'), 'emission manifest');
    if (sha256(expectedManifest) !== sha256(actualManifest)) fail('emission regeneration differs from the supplied receipt');
    const expectedComponents = path.join(regenerated, 'components');
    const actualComponents = path.join(emissionDir, 'components');
    for (const name of fs.readdirSync(expectedComponents)) {
      const expected = readBytes(path.join(expectedComponents, name), `regenerated module ${name}`);
      const actual = readBytes(path.join(actualComponents, name), `emission module ${name}`);
      if (sha256(expected) !== sha256(actual)) fail(`emission regeneration differs for module ${name}`);
    }
  } finally {
    fs.rmSync(regenerationRoot, { recursive: true, force: true });
  }
}

function moduleEdges(source, label) {
  let ast;
  try {
    ast = parse(source, { sourceType: 'module', plugins: ['jsx', 'typescript'], errorRecovery: false });
  } catch (error) { fail(`module ${label} is not parseable as ES module: ${error.message}`); }
  const exports = new Set();
  const imports = [];
  for (const statement of ast.program.body) {
    if (statement.type === 'ExportNamedDeclaration') {
      if (statement.declaration?.id?.name) exports.add(statement.declaration.id.name);
      for (const specifier of statement.specifiers || []) {
        const name = specifier.exported?.name || specifier.exported?.value;
        if (NAME_RE.test(name || '')) exports.add(name);
      }
    }
    if (statement.type === 'ImportDeclaration') {
      for (const specifier of statement.specifiers || []) {
        const name = specifier.imported?.name || specifier.imported?.value;
        if (specifier.type === 'ImportSpecifier' && NAME_RE.test(name || '')) imports.push({ name, source: statement.source.value });
      }
    }
  }
  return { exports, imports };
}

function validateModuleEdges(stage, records) {
  const exportsByPath = new Map();
  for (const record of records) {
    const file = path.join(stage, record.path);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) fail(`module import output missing for ${record.name}`);
    exportsByPath.set(record.path, moduleEdges(readBytes(file, `module import output ${record.name}`).toString('utf8'), record.name));
  }
  for (const record of records) {
    const edges = exportsByPath.get(record.path);
    for (const imported of edges.imports) {
      if (typeof imported.source !== 'string' || !imported.source.startsWith('./'))
        fail(`module import ${record.name} has unsafe source ${imported.source}`);
      const dependencyPath = path.join('components', `${path.basename(imported.source)}.tsx`);
      const dependencyEdges = exportsByPath.get(dependencyPath);
      if (!dependencyEdges) fail(`module import ${record.name} references missing module ${imported.source}`);
      if (!dependencyEdges.exports.has(imported.name)) fail(`module import ${record.name} references missing export ${imported.name} from ${imported.source}`);
    }
    if (!edges.exports.has(record.export)) fail(`module ${record.name} is missing export ${record.export}`);
  }
}

function runTypeScript(stage) {
  if (!fs.existsSync(tscPath)) fail(`TypeScript compiler unavailable at ${tscPath}`);
  const files = ['globals.d.ts', ...fs.readdirSync(path.join(stage, 'components')).filter((name) => name.endsWith('.tsx')).sort().map((name) => path.join('components', name))];
  const result = spawnSync(process.execPath, [tscPath, '--noEmit', '--target', 'ES2022', '--module', 'ESNext', '--moduleResolution', 'Bundler', '--jsx', 'react', '--skipLibCheck', '--pretty', 'false', ...files], {
    cwd: stage, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  const diagnostics = `${result.stdout || ''}${result.stderr || ''}`.trim();
  if (result.error) fail(`TypeScript process failed: ${result.error.message}`);
  if (result.status !== 0) fail(`TypeScript module contract rejected the closure:\n${diagnostics || '(no diagnostics)'}`);
  return diagnostics;
}

function build({ artifactPath, manifestPath, emissionDir, outDir }) {
  if (fs.existsSync(outDir)) fail(`output directory already exists: ${outDir}`);
  const artifactBytes = readBytes(artifactPath, 'artifact');
  const manifestBytes = readBytes(manifestPath, 'dependency manifest');
  const { value: artifact } = readJson(artifactPath, 'artifact');
  const { value: manifest } = readJson(manifestPath, 'dependency manifest');
  runEmitterVerify(artifactPath, manifestPath, emissionDir);
  runEmitterRegenerationCheck(artifactPath, manifestPath, emissionDir);
  const { byId } = validateManifest(manifest, artifactBytes);
  const { emission, emissionRaw, modules } = validateEmission(emissionDir, artifactBytes, manifestBytes, manifest);
  const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-wp1-import-contract-'));
  try {
    const componentsDir = path.join(stage, 'components'); fs.mkdirSync(componentsDir, { recursive: true });
    const records = [];
    for (const { component, module, source } of modules) {
      const output = Buffer.from(transformedModule(component, source, byId));
      const outputPath = path.join(componentsDir, `${component.name}.tsx`);
      fs.writeFileSync(outputPath, output);
      records.push({
        id: component.id, name: component.name,
        authored_source_sha256: module.source_sha256, authored_source_bytes: module.source_bytes,
        emitted_module_sha256: sha256(source), emitted_module_bytes: source.length,
        imports: [...component.dependencies].sort().map((id) => ({ id, name: byId.get(id).name })),
        export: component.name, output_sha256: sha256(output), output_bytes: output.length,
        path: path.join('components', `${component.name}.tsx`),
      });
    }
    const globals = ambientDeclarations(manifest, artifact);
    fs.writeFileSync(path.join(stage, 'globals.d.ts'), globals);
    validateModuleEdges(stage, records);
    const diagnostics = runTypeScript(stage);
    const contract = {
      schema: SCHEMA, version: 1,
      artifact: { path: path.basename(artifactPath), sha256: sha256(artifactBytes), bytes: artifactBytes.length },
      dependency_manifest: { path: path.basename(manifestPath), sha256: sha256(manifestBytes) },
      emission: { path: path.basename(emissionDir), sha256: sha256(Buffer.from(emissionRaw)), modules: modules.length },
      typescript: { version: '5.9.3', module: 'ESNext', module_resolution: 'Bundler', diagnostics: diagnostics ? diagnostics.split('\n').length : 0 },
      runtime_bindings: { mode: 'ambient-declarations-only', runtime_facade: false },
      modules: records,
      files: [{ path: 'globals.d.ts', bytes: globals.length, sha256: sha256(Buffer.from(globals)) }],
    };
    fs.writeFileSync(path.join(stage, 'module-import-contract.json'), `${JSON.stringify(contract, null, 2)}\n`);
    fs.renameSync(stage, outDir);
    return contract;
  } catch (error) {
    fs.rmSync(stage, { recursive: true, force: true });
    throw error;
  }
}

function verify({ artifactPath, manifestPath, emissionDir, outDir }) {
  const contractPath = path.join(outDir, 'module-import-contract.json');
  const { raw: contractRaw, value: contract } = readJson(contractPath, 'module import contract');
  assertKeys(contract, new Set(['schema', 'version', 'artifact', 'dependency_manifest', 'emission', 'typescript', 'runtime_bindings', 'modules', 'files']), 'module import contract');
  if (contract.schema !== SCHEMA || contract.version !== 1) fail('unsupported module import contract');
  assertKeys(contract.typescript, new Set(['version', 'module', 'module_resolution', 'diagnostics']), 'module import TypeScript receipt');
  if (contract.typescript.version !== '5.9.3' || contract.typescript.module !== 'ESNext'
      || contract.typescript.module_resolution !== 'Bundler' || contract.typescript.diagnostics !== 0)
    fail('module import TypeScript receipt changed');
  assertKeys(contract.runtime_bindings, new Set(['mode', 'runtime_facade']), 'module import runtime binding receipt');
  if (contract.runtime_bindings.mode !== 'ambient-declarations-only' || contract.runtime_bindings.runtime_facade !== false)
    fail('module import runtime binding receipt changed');
  const artifactBytes = readBytes(artifactPath, 'artifact');
  const manifestBytes = readBytes(manifestPath, 'dependency manifest');
  if (contract.artifact?.sha256 !== sha256(artifactBytes)) fail('module import contract artifact identity changed');
  if (contract.dependency_manifest?.sha256 !== sha256(manifestBytes)) fail('module import contract dependency identity changed');
  const { value: manifest } = readJson(manifestPath, 'dependency manifest');
  runEmitterVerify(artifactPath, manifestPath, emissionDir);
  runEmitterRegenerationCheck(artifactPath, manifestPath, emissionDir);
  const { byId } = validateManifest(manifest, artifactBytes);
  const { emission, emissionRaw, modules } = validateEmission(emissionDir, artifactBytes, manifestBytes, manifest);
  if (contract.emission?.sha256 !== sha256(Buffer.from(emissionRaw))) fail('module import contract emission identity changed');
  if (!Array.isArray(contract.modules) || contract.modules.length !== modules.length) fail('module import contract module count changed');
  if (!Array.isArray(contract.files) || contract.files.length !== 1 || contract.files[0].path !== 'globals.d.ts')
    fail('module import contract globals receipt changed');
  assertKeys(contract.files[0], new Set(['path', 'bytes', 'sha256']), 'module import globals receipt');
  assertInteger(contract.files[0].bytes, 'module import globals bytes');
  assertDigest(contract.files[0].sha256, 'module import globals sha256');
  const expectedGlobals = Buffer.from(ambientDeclarations(manifest, JSON.parse(artifactBytes)));
  const globalsPath = path.join(outDir, 'globals.d.ts');
  if (!fs.existsSync(globalsPath) || !fs.statSync(globalsPath).isFile()) fail('module import globals.d.ts is missing');
  const actualGlobals = readBytes(globalsPath, 'module import globals.d.ts');
  const globalsReceipt = contract.files[0];
  if (globalsReceipt.bytes !== expectedGlobals.length || globalsReceipt.sha256 !== sha256(expectedGlobals))
    fail('module import contract globals identity changed');
  if (actualGlobals.length !== globalsReceipt.bytes || sha256(actualGlobals) !== globalsReceipt.sha256)
    fail('module import globals.d.ts bytes changed');
  const moduleById = new Map(contract.modules.map((module) => [module.id, module]));
  if (moduleById.size !== contract.modules.length) fail('module import contract contains duplicate module identity');
  for (const record of contract.modules) {
    assertKeys(record, new Set(['id', 'name', 'authored_source_sha256', 'authored_source_bytes', 'emitted_module_sha256', 'emitted_module_bytes', 'imports', 'export', 'output_sha256', 'output_bytes', 'path']), `module import record ${record.name || '<unknown>'}`);
    if (!NAME_RE.test(record.name) || record.path !== path.join('components', `${record.name}.tsx`)) fail(`unsafe module import path for ${record.name}`);
    assertDigest(record.authored_source_sha256, `${record.name}.authored_source_sha256`);
    assertDigest(record.emitted_module_sha256, `${record.name}.emitted_module_sha256`);
    assertDigest(record.output_sha256, `${record.name}.output_sha256`);
    for (const key of ['authored_source_bytes', 'emitted_module_bytes', 'output_bytes']) assertInteger(record[key], `${record.name}.${key}`);
    if (!Array.isArray(record.imports) || record.imports.some((item) => !isRecord(item) || typeof item.id !== 'string' || !NAME_RE.test(item.name)))
      fail(`module import metadata is invalid for ${record.name}`);
  }
  validateModuleEdges(outDir, contract.modules);
  for (const { component, module, source } of modules) {
    const record = moduleById.get(component.id);
    if (!record || record.name !== component.name) fail(`module import contract identity missing for ${component.name}`);
    if (record.authored_source_sha256 !== module.source_sha256 || record.authored_source_bytes !== module.source_bytes)
      fail(`authored source identity changed for ${component.name}`);
    if (record.emitted_module_sha256 !== sha256(source) || record.emitted_module_bytes !== source.length)
      fail(`emitted source identity changed for ${component.name}`);
    const expectedImports = [...component.dependencies].sort().map((id) => ({ id, name: byId.get(id).name }));
    if (JSON.stringify(record.imports) !== JSON.stringify(expectedImports) || record.export !== component.name)
      fail(`module import/export contract changed for ${component.name}`);
    const output = Buffer.from(transformedModule(component, source, byId));
    if (record.output_sha256 !== sha256(output) || record.output_bytes !== output.length)
      fail(`module import output hash changed for ${component.name}`);
    const file = path.join(outDir, record.path);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) fail(`module import output missing for ${component.name}`);
    const actual = readBytes(file, `module import output ${component.name}`);
    if (sha256(actual) !== record.output_sha256 || actual.length !== record.output_bytes)
      fail(`module import output bytes changed for ${component.name}`);
  }
  if (!emission || !contractRaw) fail('module import receipt is empty');
  runTypeScript(outDir);
  process.stdout.write(`${JSON.stringify({ verified: true, modules: modules.length, contract_sha256: sha256(Buffer.from(contractRaw)) }, null, 2)}\n`);
}

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '--artifact' || arg === '--manifest' || arg === '--emission' || arg === '--out') args[arg.slice(2)] = argv[++index];
    else if (arg === '--verify') args.verify = true;
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function usage() { console.log('usage: node tools/wp1_module_import_contract.mjs --artifact FILE --manifest FILE --emission DIR --out DIR [--verify]'); }

try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  if (!args.artifact || !args.manifest || !args.emission || !args.out) fail('--artifact, --manifest, --emission, and --out are required');
  const artifactPath = path.resolve(args.artifact), manifestPath = path.resolve(args.manifest);
  const emissionDir = path.resolve(args.emission), outDir = path.resolve(args.out);
  if (!fs.existsSync(artifactPath)) fail(`artifact does not exist: ${artifactPath}`);
  if (!fs.existsSync(manifestPath)) fail(`dependency manifest does not exist: ${manifestPath}`);
  if (!fs.existsSync(emissionDir) || !fs.statSync(emissionDir).isDirectory()) fail(`emission directory does not exist: ${emissionDir}`);
  if (args.verify) verify({ artifactPath, manifestPath, emissionDir, outDir });
  else process.stdout.write(`${JSON.stringify(build({ artifactPath, manifestPath, emissionDir, outDir }), null, 2)}\n`);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
