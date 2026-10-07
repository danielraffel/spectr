#!/usr/bin/env node
/**
 * Prove a small authored module closure can consume typed runtime bindings.
 *
 * This is a fixture-only follow-up to wp1_module_import_contract.mjs. It
 * replaces ambient React/helper declarations with a real typed .mts facade,
 * compiles that facade to .mjs, and imports the implementation in Node. It
 * deliberately does not claim the full App closure or browser runtime parity.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const SCHEMA = 'spectr-owned-runtime-facade-contract-v1';
const MODULE_CONTRACT = 'spectr-owned-module-import-contract-v1';
const NAME_RE = /^[A-Za-z_$][\w$]*$/;
const DIGEST_RE = /^[0-9a-f]{64}$/;
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const toolchainDir = path.join(scriptDir, 'wp1-parser');
const tscPath = path.join(toolchainDir, 'node_modules', 'typescript', 'bin', 'tsc');
const parserPath = path.join(toolchainDir, 'node_modules', '@babel', 'parser');
const babel = createRequire(import.meta.url)(parserPath);
const parse = babel.parse || babel.default?.parse;
const moduleContractTool = path.join(scriptDir, 'wp1_module_import_contract.mjs');

const FACADE_DEFINITIONS = new Map([
  ['React', 'React'],
  ['claimDocumentNavigationFocus', 'claimDocumentNavigationFocus'],
]);

function fail(message) { throw new Error(`WP-1 runtime facade contract failed: ${message}`); }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function isRecord(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function readBytes(file, label) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${label}: ${error.message}`); } }
function readJson(file, label) {
  const raw = readBytes(file, label);
  try { return { raw, value: JSON.parse(raw) }; } catch (error) { fail(`${label} JSON is invalid: ${error.message}`); }
}
function assertDigest(value, label) { if (typeof value !== 'string' || !DIGEST_RE.test(value)) fail(`${label} must be a lowercase SHA-256 digest`); }
function assertInteger(value, label) { if (!Number.isInteger(value) || value < 0) fail(`${label} must be a non-negative integer`); }
function assertKeys(value, expected, label) {
  if (!isRecord(value)) fail(`${label} must be an object`);
  const actual = new Set(Object.keys(value));
  const missing = [...expected].filter((key) => !actual.has(key)).sort();
  const extra = [...actual].filter((key) => !expected.has(key)).sort();
  if (missing.length || extra.length)
    fail(`${label} keys changed${missing.length ? `; missing ${missing.join(', ')}` : ''}${extra.length ? `; unexpected ${extra.join(', ')}` : ''}`);
}

function runModuleContract(artifactPath, manifestPath, emissionDir, outputDir) {
  const result = spawnSync(process.execPath, [moduleContractTool, '--artifact', artifactPath, '--manifest', manifestPath, '--emission', emissionDir, '--out', outputDir], {
    cwd: scriptDir, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) fail(`module contract process failed: ${result.error.message}`);
  if (result.status !== 0) fail(`module import contract rejected the facade fixture: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
}

function runtimeFacadeSource() {
  return `export interface ReactElement {\n  type: unknown;\n  props: Record<string, unknown> | null;\n  children: unknown[];\n}\n\nexport interface ReactRuntime {\n  Fragment: symbol;\n  createElement(type: unknown, props: Record<string, unknown> | null, ...children: unknown[]): ReactElement;\n}\n\nexport const React: ReactRuntime = {\n  Fragment: Symbol.for('react.fragment'),\n  createElement(type, props, ...children) { return { type, props, children }; },\n};\n\nexport type ClaimDocumentNavigationFocus = (...args: unknown[]) => boolean;\nexport const claimDocumentNavigationFocus: ClaimDocumentNavigationFocus = () => true;\n`;
}

function facadeExportNames(source) {
  if (typeof parse !== 'function') fail('WP-1 parser toolchain has no parse() API');
  let ast;
  try { ast = parse(source, { sourceType: 'module', plugins: ['typescript'], errorRecovery: false }); }
  catch (error) { fail(`runtime facade is not parseable: ${error.message}`); }
  const names = new Set();
  for (const statement of ast.program.body) {
    if (statement.type !== 'ExportNamedDeclaration') continue;
    if (statement.declaration?.id?.name) names.add(statement.declaration.id.name);
    for (const declaration of statement.declaration?.declarations || []) {
      if (declaration.id?.name) names.add(declaration.id.name);
    }
    for (const specifier of statement.specifiers || []) {
      const name = specifier.exported?.name || specifier.exported?.value;
      if (NAME_RE.test(name || '')) names.add(name);
    }
  }
  return names;
}

function facadeImports(component, manifestById) {
  const names = new Set(component.external_bindings || []);
  for (const name of names) if (!FACADE_DEFINITIONS.has(name))
    fail(`fixture component ${component.name} needs an unproven runtime binding ${name}`);
  return [...names].sort().map((name) => FACADE_DEFINITIONS.get(name));
}

function runTypeScript(stage) {
  const files = ['globals.d.ts', 'runtime-bindings.mts', ...fs.readdirSync(path.join(stage, 'components')).filter((name) => name.endsWith('.tsx')).sort().map((name) => path.join('components', name))];
  const result = spawnSync(process.execPath, [tscPath, '--noEmit', '--target', 'ES2022', '--module', 'ESNext', '--moduleResolution', 'Bundler', '--jsx', 'react', '--skipLibCheck', '--pretty', 'false', ...files], {
    cwd: stage, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  const diagnostics = `${result.stdout || ''}${result.stderr || ''}`.trim();
  if (result.error) fail(`TypeScript process failed: ${result.error.message}`);
  if (result.status !== 0) fail(`typed runtime facade rejected the closure:\n${diagnostics || '(no diagnostics)'}`);
  return diagnostics;
}

function compileFacade(stage, componentNames) {
  const compiledDir = path.join(stage, 'compiled');
  fs.mkdirSync(compiledDir);
  const componentFiles = componentNames.map((name) => path.join('components', `${name}.tsx`));
  const result = spawnSync(process.execPath, [tscPath, 'globals.d.ts', 'runtime-bindings.mts', ...componentFiles,
    '--target', 'ES2022', '--module', 'ESNext', '--moduleResolution', 'Bundler', '--jsx', 'react',
    '--skipLibCheck', '--declaration', 'false', '--pretty', 'false', '--outDir', 'compiled'], {
    cwd: stage, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) fail(`facade/component compilation process failed: ${result.error.message}`);
  if (result.status !== 0) fail(`runtime facade/component compilation failed:\n${(result.stdout || '') + (result.stderr || '')}`);
  const facadePath = path.join(compiledDir, 'runtime-bindings.mjs');
  if (!fs.existsSync(facadePath)) fail('compiled runtime facade is missing');
  const componentPaths = new Map();
  for (const name of componentNames) {
    const componentPath = path.join(compiledDir, 'components', `${name}.js`);
    if (!fs.existsSync(componentPath)) fail(`compiled component ${name} is missing`);
    componentPaths.set(name, componentPath);
  }
  // TypeScript emits .js for TSX under ESNext. Mark this isolated compilation
  // tree as ESM so Node imports the compiled component closure with the same
  // semantics as the facade's .mjs output.
  fs.writeFileSync(path.join(compiledDir, 'package.json'), '{"type":"module"}\n');
  return { facadePath, componentPaths, packagePath: path.join(compiledDir, 'package.json') };
}

function runNodeSmoke(compiledPath, rootComponentPath, rootComponentName) {
  const script = `const facade = await import(process.env.WP1_FACADE_URL);\nif (typeof facade.React?.createElement !== 'function') throw new Error('React.createElement export is not callable');\nif (typeof facade.claimDocumentNavigationFocus !== 'function') throw new Error('claimDocumentNavigationFocus export is not callable');\nconst element = facade.React.createElement('button', { id: 'smoke' }, 'ok');\nif (element.type !== 'button' || element.props.id !== 'smoke' || element.children[0] !== 'ok') throw new Error('React facade behavior mismatch');\nif (facade.claimDocumentNavigationFocus() !== true) throw new Error('navigation helper behavior mismatch');\nconst component = await import(process.env.WP1_COMPONENT_URL);\nif (typeof component[process.env.WP1_COMPONENT_NAME] !== 'function') throw new Error('compiled root component export is not callable');\nconst root = component[process.env.WP1_COMPONENT_NAME]();\nif (!root || typeof root.type !== 'function' || !root.props || root.props.label !== 'ok') throw new Error('compiled root component did not produce the expected child element');\nconst child = root.type(root.props);\nif (child?.type !== 'button' || child?.children?.[0] !== 'ok' || typeof child?.props?.onClick !== 'function') throw new Error('compiled component closure did not render through React facade');\nif (child.props.onClick() !== true) throw new Error('compiled component helper behavior mismatch');`;
  const result = spawnSync(process.execPath, ['--input-type=module', '--eval', script], {
    cwd: scriptDir, env: {
      ...process.env,
      WP1_FACADE_URL: pathToFileURL(compiledPath).href,
      WP1_COMPONENT_URL: pathToFileURL(rootComponentPath).href,
      WP1_COMPONENT_NAME: rootComponentName,
    }, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) fail(`Node runtime smoke failed: ${result.error.message}`);
  if (result.status !== 0) fail(`Node runtime smoke rejected facade: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
}

function build({ artifactPath, manifestPath, emissionDir, outDir }) {
  if (fs.existsSync(outDir)) fail(`output directory already exists: ${outDir}`);
  const workspace = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-wp1-facade-'));
  const baseDir = path.join(workspace, 'module-contract');
  const stage = path.join(workspace, 'facade');
  try {
    runModuleContract(artifactPath, manifestPath, emissionDir, baseDir);
    const { raw: baseRaw, value: base } = readJson(path.join(baseDir, 'module-import-contract.json'), 'base module import contract');
    if (base.schema !== MODULE_CONTRACT || base.version !== 1) fail('unsupported base module contract');
    const artifactBytes = readBytes(artifactPath, 'artifact');
    const manifestBytes = readBytes(manifestPath, 'dependency manifest');
    const { value: manifest } = readJson(manifestPath, 'dependency manifest');
    const byId = new Map(manifest.components.map((component) => [component.id, component]));
    fs.mkdirSync(path.join(stage, 'components'), { recursive: true });
    const modules = [];
    for (const record of base.modules) {
      const component = byId.get(record.id);
      if (!component) fail(`base module ${record.name} is absent from dependency manifest`);
      const runtimeImports = facadeImports(component, byId);
      const source = readBytes(path.join(baseDir, record.path), `base module ${record.name}`);
      const prefix = runtimeImports.length ? `import { ${runtimeImports.join(', ')} } from '../runtime-bindings.mjs';\n` : '';
      let sourceText = source.toString('utf8');
      // The staging contract uses extensionless TypeScript imports so the
      // checker can resolve TSX sources. Node's ESM loader requires an
      // explicit extension after compilation; rewrite only the dependency
      // edges proven by this manifest, leaving authored strings untouched.
      for (const dependencyId of component.dependencies) {
        const dependency = byId.get(dependencyId);
        if (!dependency) fail(`component ${component.name} dependency ${dependencyId} is unknown`);
        sourceText = sourceText.replaceAll(`from './${dependency.name}'`, `from './${dependency.name}.js'`);
      }
      const output = Buffer.from(`${prefix}${sourceText}`);
      const outputPath = path.join(stage, record.path);
      fs.mkdirSync(path.dirname(outputPath), { recursive: true });
      fs.writeFileSync(outputPath, output);
      modules.push({
        id: record.id, name: record.name, path: record.path,
        base_output_sha256: record.output_sha256, base_output_bytes: record.output_bytes,
        runtime_imports: runtimeImports, output_sha256: sha256(output), output_bytes: output.length,
      });
    }
    const facadeSource = Buffer.from(runtimeFacadeSource());
    fs.writeFileSync(path.join(stage, 'runtime-bindings.mts'), facadeSource);
    fs.writeFileSync(path.join(stage, 'globals.d.ts'), 'declare namespace JSX { interface IntrinsicElements { [elemName: string]: any; } }\n');
    const diagnostics = runTypeScript(stage);
    const componentNames = modules.map((module) => module.name);
    const compiled = compileFacade(stage, componentNames);
    const compiledBytes = readBytes(compiled.facadePath, 'compiled runtime facade');
    const rootId = manifest.roots?.[0];
    const rootComponent = byId.get(rootId);
    if (!rootComponent) fail('runtime facade fixture has no resolvable root component');
    const rootComponentPath = compiled.componentPaths.get(rootComponent.name);
    if (!rootComponentPath) fail(`compiled root component ${rootComponent.name} is missing`);
    runNodeSmoke(compiled.facadePath, rootComponentPath, rootComponent.name);
    for (const module of modules) {
      const componentPath = compiled.componentPaths.get(module.name);
      const componentBytes = readBytes(componentPath, `compiled component ${module.name}`);
      module.compiled = {
        path: path.relative(stage, componentPath),
        sha256: sha256(componentBytes),
        bytes: componentBytes.length,
      };
    }
    const packageBytes = readBytes(compiled.packagePath, 'compiled ESM package marker');
    const contract = {
      schema: SCHEMA, version: 1,
      artifact: { path: path.basename(artifactPath), sha256: sha256(artifactBytes), bytes: artifactBytes.length },
      dependency_manifest: { path: path.basename(manifestPath), sha256: sha256(manifestBytes) },
      base_module_contract: { path: path.basename(baseDir), sha256: sha256(Buffer.from(baseRaw)), modules: base.modules.length },
      typescript: { version: '5.9.3', module: 'ESNext', module_resolution: 'Bundler', diagnostics: diagnostics ? diagnostics.split('\n').length : 0 },
      runtime_facade: {
        source: { path: 'runtime-bindings.mts', sha256: sha256(facadeSource), bytes: facadeSource.length, exports: [...facadeExportNames(facadeSource.toString('utf8'))].sort() },
        compiled: { path: 'compiled/runtime-bindings.mjs', sha256: sha256(compiledBytes), bytes: compiledBytes.length },
        smoke: {
          runner: 'node', status: 'passed',
          component: rootComponent.name,
          component_path: path.relative(stage, rootComponentPath),
        },
      },
      modules,
      files: [
        { path: 'globals.d.ts', sha256: sha256(readBytes(path.join(stage, 'globals.d.ts')),), bytes: fs.statSync(path.join(stage, 'globals.d.ts')).size },
        { path: 'compiled/package.json', sha256: sha256(packageBytes), bytes: packageBytes.length },
      ],
    };
    fs.writeFileSync(path.join(stage, 'runtime-facade-contract.json'), `${JSON.stringify(contract, null, 2)}\n`);
    fs.renameSync(stage, outDir);
    return contract;
  } catch (error) {
    fs.rmSync(workspace, { recursive: true, force: true });
    throw error;
  }
}

function verify({ artifactPath, manifestPath, emissionDir, outDir }) {
  const contractPath = path.join(outDir, 'runtime-facade-contract.json');
  const { value: contract } = readJson(contractPath, 'runtime facade contract');
  assertKeys(contract, new Set(['schema', 'version', 'artifact', 'dependency_manifest', 'base_module_contract', 'typescript', 'runtime_facade', 'modules', 'files']), 'runtime facade contract');
  if (contract.schema !== SCHEMA || contract.version !== 1) fail('unsupported runtime facade contract');
  assertKeys(contract.runtime_facade, new Set(['source', 'compiled', 'smoke']), 'runtime facade receipt');
  assertKeys(contract.runtime_facade.source, new Set(['path', 'sha256', 'bytes', 'exports']), 'runtime facade source receipt');
  assertKeys(contract.runtime_facade.compiled, new Set(['path', 'sha256', 'bytes']), 'runtime facade compiled receipt');
  assertInteger(contract.runtime_facade.source.bytes, 'runtime facade source bytes');
  assertInteger(contract.runtime_facade.compiled.bytes, 'runtime facade compiled bytes');
  assertDigest(contract.runtime_facade.source.sha256, 'runtime facade source sha256');
  assertDigest(contract.runtime_facade.compiled.sha256, 'runtime facade compiled sha256');
  assertKeys(contract.runtime_facade.smoke, new Set(['runner', 'status', 'component', 'component_path']), 'runtime facade smoke receipt');
  if (contract.runtime_facade.smoke.runner !== 'node' || contract.runtime_facade.smoke.status !== 'passed') fail('runtime facade smoke receipt is not passed');
  if (!NAME_RE.test(contract.runtime_facade.smoke.component) || contract.runtime_facade.smoke.component_path !== path.join('compiled', 'components', `${contract.runtime_facade.smoke.component}.js`))
    fail('runtime facade component smoke identity is invalid');
  const facadePath = path.join(outDir, contract.runtime_facade.source.path);
  const facadeSource = readBytes(facadePath, 'runtime facade source').toString('utf8');
  const actualExports = facadeExportNames(facadeSource);
  for (const expected of contract.runtime_facade.source.exports) if (!actualExports.has(expected)) fail(`runtime facade missing export ${expected}`);
  if (sha256(Buffer.from(facadeSource)) !== contract.runtime_facade.source.sha256) fail('runtime facade source identity changed');
  const compiledPath = path.join(outDir, contract.runtime_facade.compiled.path);
  const compiled = readBytes(compiledPath, 'compiled runtime facade');
  if (sha256(compiled) !== contract.runtime_facade.compiled.sha256 || compiled.length !== contract.runtime_facade.compiled.bytes)
    fail('compiled runtime facade identity changed');
  if (!Array.isArray(contract.files) || contract.files.length !== 2) fail('runtime facade files receipt changed');
  for (const file of contract.files) {
    assertKeys(file, new Set(['path', 'sha256', 'bytes']), 'runtime facade file receipt');
    assertDigest(file.sha256, `${file.path} sha256`);
    assertInteger(file.bytes, `${file.path} bytes`);
    const bytes = readBytes(path.join(outDir, file.path), `runtime facade file ${file.path}`);
    if (sha256(bytes) !== file.sha256 || bytes.length !== file.bytes) fail(`runtime facade file identity changed for ${file.path}`);
  }
  if (contract.files[0].path !== 'globals.d.ts' || contract.files[1].path !== 'compiled/package.json') fail('runtime facade files are not canonical');
  if (!Array.isArray(contract.modules)) fail('runtime facade module receipt is missing');
  const moduleIds = new Set();
  for (const record of contract.modules) {
    assertKeys(record, new Set(['id', 'name', 'path', 'base_output_sha256', 'base_output_bytes', 'runtime_imports', 'output_sha256', 'output_bytes', 'compiled']), `runtime facade module ${record.name || '<unknown>'}`);
    if (!NAME_RE.test(record.name) || record.path !== path.join('components', `${record.name}.tsx`)) fail(`runtime facade module path is unsafe for ${record.name}`);
    if (moduleIds.has(record.id)) fail(`runtime facade module identity is duplicated for ${record.name}`);
    moduleIds.add(record.id);
    assertDigest(record.output_sha256, `${record.name}.output_sha256`);
    assertInteger(record.output_bytes, `${record.name}.output_bytes`);
    assertKeys(record.compiled, new Set(['path', 'sha256', 'bytes']), `${record.name}.compiled receipt`);
    if (record.compiled.path !== path.join('compiled', 'components', `${record.name}.js`)) fail(`runtime facade compiled module path is unsafe for ${record.name}`);
    assertDigest(record.compiled.sha256, `${record.name}.compiled.sha256`);
    assertInteger(record.compiled.bytes, `${record.name}.compiled.bytes`);
    if (!Array.isArray(record.runtime_imports) || record.runtime_imports.some((name) => !FACADE_DEFINITIONS.has(name)))
      fail(`runtime facade imports are invalid for ${record.name}`);
    const moduleBytes = readBytes(path.join(outDir, record.path), `runtime facade module ${record.name}`);
    if (sha256(moduleBytes) !== record.output_sha256 || moduleBytes.length !== record.output_bytes)
      fail(`runtime facade module output identity changed for ${record.name}`);
    const compiledModuleBytes = readBytes(path.join(outDir, record.compiled.path), `compiled runtime facade module ${record.name}`);
    if (sha256(compiledModuleBytes) !== record.compiled.sha256 || compiledModuleBytes.length !== record.compiled.bytes)
      fail(`compiled runtime facade module identity changed for ${record.name}`);
  }
  const smoke = contract.runtime_facade.smoke;
  const result = runNodeSmoke(compiledPath, path.join(outDir, smoke.component_path), smoke.component);
  if (result !== undefined) fail('unexpected runtime smoke result');
  const workspace = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-wp1-facade-verify-'));
  try {
    const rebuilt = path.join(workspace, 'rebuilt');
    build({ artifactPath, manifestPath, emissionDir, outDir: rebuilt });
    const rebuiltContract = readBytes(path.join(rebuilt, 'runtime-facade-contract.json'));
    const currentContract = readBytes(contractPath);
    if (sha256(rebuiltContract) !== sha256(currentContract)) fail('runtime facade regeneration differs from supplied receipt');
  } finally { fs.rmSync(workspace, { recursive: true, force: true }); }
  process.stdout.write(`${JSON.stringify({ verified: true, exports: [...actualExports].sort(), runtime_smoke: 'passed' }, null, 2)}\n`);
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
function usage() { console.log('usage: node tools/wp1_runtime_facade_contract.mjs --artifact FILE --manifest FILE --emission DIR --out DIR [--verify]'); }

try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  if (!args.artifact || !args.manifest || !args.emission || !args.out) fail('--artifact, --manifest, --emission, and --out are required');
  const artifactPath = path.resolve(args.artifact), manifestPath = path.resolve(args.manifest), emissionDir = path.resolve(args.emission), outDir = path.resolve(args.out);
  if (!fs.existsSync(artifactPath)) fail(`artifact does not exist: ${artifactPath}`);
  if (!fs.existsSync(manifestPath)) fail(`dependency manifest does not exist: ${manifestPath}`);
  if (!fs.existsSync(emissionDir) || !fs.statSync(emissionDir).isDirectory()) fail(`emission directory does not exist: ${emissionDir}`);
  if (args.verify) verify({ artifactPath, manifestPath, emissionDir, outDir });
  else process.stdout.write(`${JSON.stringify(build({ artifactPath, manifestPath, emissionDir, outDir }), null, 2)}\n`);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
