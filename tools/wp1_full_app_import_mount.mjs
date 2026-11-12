#!/usr/bin/env node
/**
 * Stage and execute the complete authored App closure.
 *
 * This is deliberately a staging gate.  It never edits editor.html or a
 * materialized runtime.  The emitted TSX modules are assembled as CommonJS
 * modules, resolved only through the dependency manifest, and evaluated in a
 * VM with an explicit browser/runtime facade.  The receipt is an integrity
 * boundary for the artifact, manifest, emission, runtime-surface contract,
 * module source, and facade shape; it is not a native-rendering claim.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCHEMA = 'spectr-owned-full-app-import-mount-v1';
const MANIFEST_SCHEMA = 'spectr-owned-component-dependency-v1';
const EMISSION_SCHEMA = 'spectr-owned-authored-module-emission-v1';
const SURFACE_SCHEMA = 'spectr-owned-app-runtime-surface-v1';
const NAME_RE = /^[A-Za-z_$][\w$]*$/;
const DIGEST_RE = /^[0-9a-f]{64}$/;
const ROOT = path.dirname(fileURLToPath(import.meta.url));
const TSC = path.join(ROOT, 'wp1-parser', 'node_modules', 'typescript');
let ts;
try { ts = createRequire(import.meta.url)(TSC); }
catch (error) { fail(`pinned TypeScript toolchain is missing at ${TSC}; run npm ci --ignore-scripts --prefix tools/wp1-parser (${error.message})`); }

function fail(message) { throw new Error(`WP-1 full-App import mount failed: ${message}`); }
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function isRecord(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function readBytes(file, label) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${label}: ${error.message}`); } }
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
  if (missing.length || extra.length) fail(`${label} keys changed${missing.length ? `; missing ${missing.join(', ')}` : ''}${extra.length ? `; unexpected ${extra.join(', ')}` : ''}`);
}
function sorted(values) { return [...values].sort(); }

function surfacePath(input) {
  const resolved = path.resolve(input);
  if (fs.existsSync(resolved) && fs.statSync(resolved).isDirectory()) return path.join(resolved, 'runtime-surface-contract.json');
  return resolved;
}

function validateSurface(file, artifactBytes, manifestBytes, emissionDir) {
  const { raw, value } = readJson(file, 'runtime surface contract');
  assertKeys(value, new Set(['schema', 'version', 'root', 'module_count', 'artifact', 'dependency_manifest', 'emission', 'allowlist', 'typescript', 'runtime_surface', 'files', 'modules']), 'runtime surface contract');
  if (value.schema !== SURFACE_SCHEMA || value.version !== 1 || value.root !== 'App') fail('unsupported runtime surface contract');
  if (value.module_count !== value.modules.length) fail('runtime surface module count is inconsistent');
  if (value.artifact?.sha256 !== sha256(artifactBytes) || value.artifact?.bytes !== artifactBytes.length) fail('runtime surface artifact identity changed');
  if (value.dependency_manifest?.sha256 !== sha256(manifestBytes)) fail('runtime surface dependency identity changed');
  if (value.emission?.path !== path.basename(emissionDir)) fail('runtime surface emission identity changed');
  const emissionBytes = readBytes(path.join(emissionDir, 'authored-modules.manifest.json'), 'emission manifest');
  if (value.emission?.sha256 !== sha256(emissionBytes)) fail('runtime surface emission identity changed');
  for (const fileRecord of value.files) {
    assertKeys(fileRecord, new Set(['path', 'sha256', 'bytes']), 'runtime surface file');
    assertDigest(fileRecord.sha256, `${fileRecord.path}.sha256`); assertInteger(fileRecord.bytes, `${fileRecord.path}.bytes`);
    const sibling = path.join(path.dirname(file), fileRecord.path);
    const bytes = readBytes(sibling, `runtime surface file ${fileRecord.path}`);
    if (bytes.length !== fileRecord.bytes || sha256(bytes) !== fileRecord.sha256) fail(`runtime surface file identity changed for ${fileRecord.path}`);
  }
  return { raw, value };
}

function validateInputs(artifactPath, manifestPath, emissionDir, surfaceContractPath) {
  const artifactBytes = readBytes(artifactPath, 'artifact');
  const manifestBytes = readBytes(manifestPath, 'dependency manifest');
  const { value: artifact } = readJson(artifactPath, 'artifact');
  const { value: manifest } = readJson(manifestPath, 'dependency manifest');
  if (!isRecord(artifact) || typeof artifact.html !== 'string' || !artifact.html) fail('artifact html must be a non-empty string');
  if (!isRecord(manifest) || manifest.schema !== MANIFEST_SCHEMA || !Array.isArray(manifest.components)) fail('dependency manifest schema/components are invalid');
  if (manifest.source?.sha256 !== sha256(artifactBytes) || manifest.source?.bytes !== artifactBytes.length) fail('dependency manifest artifact identity changed');
  if (!Array.isArray(manifest.roots) || manifest.roots.length !== 1) fail('full-App mount requires exactly one root');
  const byId = new Map(); const byName = new Map();
  for (const component of manifest.components) {
    if (!isRecord(component) || typeof component.id !== 'string' || !NAME_RE.test(component.name)) fail('manifest component identity is invalid');
    if (byId.has(component.id) || byName.has(component.name)) fail(`duplicate component ${component.name}`);
    if (!Array.isArray(component.dependencies)) fail(`dependency list is missing for ${component.name}`);
    byId.set(component.id, component); byName.set(component.name, component);
  }
  const root = byId.get(manifest.roots[0]);
  if (!root || root.name !== 'App') fail('full-App mount requires an App root');
  for (const component of manifest.components) for (const dependency of component.dependencies) if (!byId.has(dependency)) fail(`unknown dependency ${dependency} for ${component.name}`);
  const surface = validateSurface(surfaceContractPath, artifactBytes, manifestBytes, emissionDir);
  const emissionFile = path.join(emissionDir, 'authored-modules.manifest.json');
  const { raw: emissionRaw, value: emission } = readJson(emissionFile, 'authored module emission');
  assertKeys(emission, new Set(['schema', 'version', 'artifact', 'dependency_manifest', 'converter', 'roots', 'order', 'modules']), 'authored module emission');
  if (emission.schema !== EMISSION_SCHEMA || emission.version !== 1) fail('unsupported authored module emission identity');
  if (emission.artifact?.sha256 !== sha256(artifactBytes) || emission.artifact?.bytes !== artifactBytes.length) fail('emission artifact identity changed');
  if (emission.dependency_manifest?.sha256 !== sha256(manifestBytes)) fail('emission dependency identity changed');
  if (JSON.stringify(emission.roots) !== JSON.stringify(manifest.roots)) fail('emission roots changed');
  if (!Array.isArray(emission.modules) || emission.modules.length !== manifest.components.length) fail('emission does not cover the full App closure');
  if (!Array.isArray(emission.order) || emission.order.length !== emission.modules.length) fail('emission dependency order is incomplete');
  const modules = []; const ids = new Set();
  for (const [index, module] of emission.modules.entries()) {
    const component = byId.get(module.id);
    if (!component || module.name !== component.name) fail(`emission module identity is invalid for ${module?.name || '<unknown>'}`);
    if (module.path !== path.join('components', `${component.name}.tsx`)) fail(`unsafe emitted module path for ${component.name}`);
    if (module.id !== emission.order[index]) fail(`emission dependency order disagrees at ${component.name}`);
    if (ids.has(module.id)) fail(`duplicate emitted module ${component.name}`); ids.add(module.id);
    const file = path.join(emissionDir, module.path);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile() || fs.lstatSync(file).isSymbolicLink()) fail(`emitted module missing or symlinked for ${component.name}`);
    const bytes = readBytes(file, `emitted module ${component.name}`);
    if (bytes.length !== module.output_bytes || sha256(bytes) !== module.output_sha256) fail(`emitted module identity changed for ${component.name}`);
    modules.push({ component, module, file, bytes });
  }
  if (ids.size !== manifest.components.length) fail('emission is missing one or more App modules');
  for (const component of manifest.components) if (!ids.has(component.id)) fail(`emission is missing ${component.name}`);
  return { artifactBytes, manifestBytes, emissionRaw, artifact, manifest, byId, modules, surface };
}

function transformedSource(component, source, byId) {
  const imports = [...component.dependencies].sort().map(id => {
    const dependency = byId.get(id); if (!dependency) fail(`unknown dependency ${id}`);
    return `import { ${dependency.name} } from './${dependency.name}';`;
  });
  return `${imports.join('\n')}${imports.length ? '\n\n' : ''}${source.toString('utf8')}\nexport { ${component.name} };\n`;
}

function reactFacade() {
  const state = value => [typeof value === 'function' ? value() : value, () => {}];
  return {
    Fragment: Symbol.for('spectr.fragment'),
    createElement: (type, props, ...children) => ({ type, props: { ...(props || {}), ...(children.length ? { children } : {}) } }),
    useState: state, useRef: value => ({ current: value }), useEffect: () => {}, useLayoutEffect: () => {},
    useCallback: fn => fn, useMemo: fn => fn(), memo: value => value,
  };
}

function namedFacade(names) {
  const facade = {};
  const state = value => [typeof value === 'function' ? value() : value, () => {}];
  const ref = value => ({ current: value });
  for (const name of names) {
    if (name === 'React') continue;
    if (name === 'window' || name === 'globalThis' || name === 'document') continue;
    if (name === 'undefined') { facade[name] = undefined; continue; }
    if (new Set(['useAppS', 'usePM', 'useTS', 'useStateChrome']).has(name) || /^use.*State$/.test(name)) facade[name] = state;
    else if (new Set(['useAppE', 'usePE', 'useTE', 'useEffectChrome']).has(name)) facade[name] = () => {};
    else if (new Set(['useAppR', 'usePR', 'useRefChrome']).has(name)) facade[name] = ref;
    else if (/^(use.*(?:Callback|C)$|use(?:App|.*Chrome)C)$/.test(name)) facade[name] = fn => fn;
    else if (/^use.*Memo/.test(name)) facade[name] = fn => fn();
    else if (/^use[A-Z]/.test(name) || /^spectr[A-Z]/.test(name) || /^(clamp|lerp|smooth|sameBandSet|isMuted|specColor|parse[A-Z]|claimDocument|releaseDocument|iconBtn)$/.test(name)) facade[name] = (...args) => args[0];
    else if (/^[A-Z][A-Z0-9_]*$/.test(name)) facade[name] = [];
    else facade[name] = (...args) => args[0];
  }
  return facade;
}

function runtimeFacade(manifest, surface) {
  const names = new Set(['React']);
  for (const component of manifest.components) for (const name of component.external_bindings || []) names.add(name);
  const facade = namedFacade(names);
  facade.React = reactFacade();
  const document = {
    activeElement: null, hasFocus: () => true, addEventListener: () => {}, removeEventListener: () => {},
    createElement: tag => ({ tagName: String(tag).toUpperCase(), style: {}, setAttribute: () => {}, appendChild: () => {}, remove: () => {} }),
    getElementById: id => id === 'tweak-defaults' ? { textContent: '{}' } : null,
    querySelector: () => null, querySelectorAll: () => [],
  };
  const window = { devicePixelRatio: 1, parent: null, addEventListener: () => {}, removeEventListener: () => {}, pulp: null };
  for (const name of surface.value.runtime_surface.window.used) window[name] = facade[name] ?? null;
  for (const name of surface.value.runtime_surface.globalThis.used) facade[name] = facade[name] ?? (() => undefined);
  window.Spectr = {
    FACTORY_PATTERNS: [], loadStore: () => [], loadDefaultId: () => 'factory:flat', saveStore: () => {}, saveDefaultId: () => {},
    resolveGains: (_pattern, count) => new Array(Number(count) || 0).fill(0), factoryGains: (_id, count) => new Array(Number(count) || 0).fill(0),
    fromCanonical: value => Array.isArray(value) ? value.slice() : [], toCanonical: value => Array.isArray(value) ? value.slice() : [],
    makeUserPattern: (name, gains) => ({ id: `user:${String(name)}`, name: String(name), source: 'user', gains: Array.isArray(gains) ? gains.slice() : [] }),
    parseEnvelope: () => ({ patterns: [], errors: [] }), exportEnvelope: value => value,
  };
  window.SpectrNativeState = { parse: () => null };
  window.SpectrNativePatterns = { parse: () => null };
  facade.window = window; facade.document = document; facade.globalThis = facade;
  const context = {
    ...facade, window, document, globalThis: facade,
    Array, Blob, Boolean, Date, Error, FileReader: class {}, Float32Array, Infinity, JSON, Map, Math, NaN,
    Number, Object, performance: { now: () => 0 }, Promise, Set, String, URL, URLSearchParams,
    console: { log: () => {}, warn: () => {}, error: () => {} }, navigator: {},
    setTimeout: () => 0, clearTimeout: () => {}, setInterval: () => 0, clearInterval: () => {},
    requestAnimationFrame: callback => { if (typeof callback === 'function') callback(0); return 0; }, cancelAnimationFrame: () => {},
    parseInt, parseFloat, isFinite, Intl, RegExp, queueMicrotask: callback => callback(), Symbol,
  };
  return context;
}

function mount(inputs) {
  const modulesByName = new Map(inputs.modules.map(item => [item.component.name, item]));
  const context = vm.createContext(runtimeFacade(inputs.manifest, inputs.surface));
  const cache = new Map();
  const load = name => {
    if (cache.has(name)) return cache.get(name).exports;
    const item = modulesByName.get(name); if (!item) fail(`missing dependency/helper module ${name}`);
    const transformed = transformedSource(item.component, item.bytes, inputs.byId);
    const output = ts.transpileModule(transformed, { fileName: item.file, reportDiagnostics: true, compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.React, jsxFactory: 'React.createElement' } });
    const diagnostics = (output.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
    if (diagnostics.length) fail(`TypeScript mount diagnostics for ${name}: ${diagnostics.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
    const module = { exports: {} }; cache.set(name, module);
    const requireModule = specifier => {
      if (!specifier.startsWith('./')) fail(`module ${name} requested unsafe dependency ${specifier}`);
      const dependency = specifier.slice(2); if (!modulesByName.has(dependency)) fail(`module ${name} requested missing dependency ${dependency}`);
      return load(dependency);
    };
    const wrapper = `(function(require,module,exports){\n${output.outputText}\n})`;
    const fn = vm.runInContext(wrapper, context, { filename: item.file });
    fn(requireModule, module, module.exports);
    cache.set(name, module); return module.exports;
  };
  const app = load('App').App;
  if (typeof app !== 'function') fail('App module did not export a callable App');
  let rendered;
  try { rendered = app({}); } catch (error) { fail(`authored App execution failed: ${error.stack || error.message}`); }
  if (!rendered || typeof rendered !== 'object') fail('authored App execution returned no render tree');
  return { context, moduleCount: cache.size, exports: [...cache.entries()].map(([name, module]) => ({ name, exports: Object.keys(module.exports).sort() })), rendered };
}

function canonicalReceipt(inputs, mounted) {
  const surfaceValue = inputs.surface.value;
  return {
    schema: SCHEMA, version: 1,
    artifact: { path: path.basename(inputs.artifactPath), sha256: sha256(inputs.artifactBytes), bytes: inputs.artifactBytes.length },
    dependency_manifest: { path: path.basename(inputs.manifestPath), sha256: sha256(inputs.manifestBytes), bytes: inputs.manifestBytes.length },
    emission: { path: path.basename(inputs.emissionDir), sha256: sha256(inputs.emissionRaw), modules: inputs.modules.length },
    runtime_surface: { path: path.basename(inputs.surfacePath), sha256: sha256(inputs.surface.raw), modules: surfaceValue.module_count },
    mount: { root: 'App', modules: mounted.moduleCount, exports: mounted.exports, render_tree: { type: typeof mounted.rendered.type === 'string' ? mounted.rendered.type : 'symbol', child_count: Array.isArray(mounted.rendered.props?.children) ? mounted.rendered.props.children.length : 0 } },
    modules: inputs.modules.map(({ component, module, bytes }) => ({ id: component.id, name: component.name, authored_source_sha256: module.source_sha256, emitted_sha256: sha256(bytes), emitted_bytes: bytes.length })),
    checks: { artifact_manifest_identity: true, emission_identity: true, runtime_surface_identity: true, module_import_resolution: true, explicit_runtime_facade: true, authored_app_root_invoked: true, runtime_command_facade: false, chromium_parity: false, native_parity: false, production_cutover: false },
  };
}

function runEmitterVerify(inputs) {
  const emitter = path.join(ROOT, 'wp1_authored_module_emitter.mjs');
  const result = spawnSync(process.execPath, [emitter, '--artifact', inputs.artifactPath, '--manifest', inputs.manifestPath, '--out', inputs.emissionDir, '--verify'], { cwd: ROOT, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 });
  if (result.status !== 0) fail(`emitted module verification rejected the closure: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
}

function runSurfaceVerify(args) {
  if (!args.allowlistPath) fail('runtime surface allowlist is required; refusing unverifiable mount');
  const surfaceTool = path.join(ROOT, 'wp1_full_app_runtime_surface.mjs');
  const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-wp1-surface-verify-'));
  try {
    const result = spawnSync(process.execPath, [surfaceTool, '--artifact', args.artifactPath, '--manifest', args.manifestPath, '--emission', args.emissionDir, '--allowlist', args.allowlistPath, '--out', path.dirname(args.surfacePath), '--verify'], { cwd: ROOT, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 });
    if (result.status !== 0) fail(`runtime surface verification rejected the contract: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
  } finally { fs.rmSync(stage, { recursive: true, force: true }); }
}

function build(args) {
  if (fs.existsSync(args.outDir)) fail(`output directory already exists: ${args.outDir}`);
  const inputs = validateInputs(args.artifactPath, args.manifestPath, args.emissionDir, args.surfacePath);
  inputs.artifactPath = args.artifactPath; inputs.manifestPath = args.manifestPath; inputs.emissionDir = args.emissionDir; inputs.surfacePath = args.surfacePath;
  runEmitterVerify(inputs);
  runSurfaceVerify(args);
  const mounted = mount(inputs);
  const receipt = canonicalReceipt(inputs, mounted);
  const stage = fs.mkdtempSync(path.join(path.dirname(args.outDir), `.${path.basename(args.outDir)}.staging-`));
  try { fs.writeFileSync(path.join(stage, 'full-app-import-mount.json'), `${JSON.stringify(receipt, null, 2)}\n`); fs.mkdirSync(path.dirname(args.outDir), { recursive: true }); fs.renameSync(stage, args.outDir); return receipt; }
  catch (error) { fs.rmSync(stage, { recursive: true, force: true }); throw error; }
}

function verify(args) {
  const receiptPath = path.join(args.outDir, 'full-app-import-mount.json');
  const { raw, value } = readJson(receiptPath, 'full-App mount receipt');
  if (value.schema !== SCHEMA || value.version !== 1) fail('unsupported full-App mount receipt');
  const stage = fs.mkdtempSync(path.join(path.dirname(args.outDir), `.${path.basename(args.outDir)}.verify-`));
  try {
    const rebuilt = build({ ...args, outDir: path.join(stage, 'rebuilt') });
    if (sha256(Buffer.from(JSON.stringify(rebuilt, null, 2) + '\n')) !== sha256(raw)) fail('mount receipt regeneration differs from supplied receipt');
    process.stdout.write(`${JSON.stringify({ verified: true, modules: rebuilt.mount.modules, receipt_sha256: sha256(raw) }, null, 2)}\n`);
  } finally { fs.rmSync(stage, { recursive: true, force: true }); }
}

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (['artifact', 'manifest', 'emission', 'surface', 'allowlist', 'out'].includes(arg.slice(2)) && arg.startsWith('--')) args[arg.slice(2) + 'Path'] = argv[++index];
    else if (arg === '--verify') args.verify = true;
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function usage() { console.log('usage: node tools/wp1_full_app_import_mount.mjs --artifact FILE --manifest FILE --emission DIR --surface FILE|DIR --allowlist FILE --out DIR [--verify]'); }

try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  for (const name of ['artifactPath', 'manifestPath', 'emissionPath', 'surfacePath', 'outPath']) if (!args[name]) fail(`--${name.slice(0, -4)} is required`);
  args.emissionDir = path.resolve(args.emissionPath); args.outDir = path.resolve(args.outPath); args.artifactPath = path.resolve(args.artifactPath); args.manifestPath = path.resolve(args.manifestPath); args.surfacePath = surfacePath(args.surfacePath);
  if (!args.allowlistPath) fail('--allowlist is required');
  args.allowlistPath = path.resolve(args.allowlistPath);
  if (args.verify) verify(args); else process.stdout.write(`${JSON.stringify(build(args), null, 2)}\n`);
} catch (error) { console.error(error.message); process.exit(1); }
