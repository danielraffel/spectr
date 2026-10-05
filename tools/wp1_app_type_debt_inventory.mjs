#!/usr/bin/env node
/**
 * Inventory semantic debt in the full App authored ESM staging graph.
 *
 * This tool intentionally preserves TypeScript diagnostics instead of making
 * the App appear green. It is a read-only planning probe: no runtime artifact
 * is rewritten and no staged module is published. The optional planted-name
 * control proves that an unknown binding cannot disappear silently.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCHEMA = 'spectr-owned-app-type-debt-inventory-v1';
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
const emitterPath = path.join(scriptDir, 'wp1_authored_module_emitter.mjs');

const FACADE_BINDINGS = new Set(['React', 'claimDocumentNavigationFocus', 'releaseDocumentNavigationFocus']);
const BROWSER_RUNTIME_BINDINGS = new Set([
  'window', 'document', 'navigator', 'globalThis', 'performance', 'console', 'Blob', 'FileReader',
  'URL', 'URLSearchParams', 'requestAnimationFrame', 'cancelAnimationFrame', 'setTimeout',
  'clearTimeout', 'setInterval', 'clearInterval', 'queueMicrotask', 'HTMLElement', 'KeyboardEvent',
  'MouseEvent', 'PointerEvent', 'WheelEvent', 'ResizeObserver', 'DOMRect', 'CSS', 'getComputedStyle',
]);
const JS_RUNTIME_BINDINGS = new Set([
  'Array', 'Boolean', 'Date', 'Error', 'Float32Array', 'Infinity', 'JSON', 'Map', 'Math', 'NaN',
  'Number', 'Object', 'Promise', 'RegExp', 'Set', 'String', 'Symbol', 'WeakMap', 'BigInt',
  'undefined', 'arguments', 'parseInt', 'parseFloat', 'isFinite', 'Intl',
]);

function fail(message) { throw new Error(`WP-1 App type debt inventory failed: ${message}`); }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function isRecord(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function readBytes(file, label) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${label}: ${error.message}`); } }
function readJson(file, label) {
  const raw = readBytes(file, label);
  try { return { raw, value: JSON.parse(raw) }; } catch (error) { fail(`${label} JSON is invalid: ${error.message}`); }
}
function assertDigest(value, label) { if (typeof value !== 'string' || !DIGEST_RE.test(value)) fail(`${label} must be a SHA-256 digest`); }
function assertKeys(value, expected, label) {
  if (!isRecord(value)) fail(`${label} must be an object`);
  const actual = new Set(Object.keys(value));
  const missing = [...expected].filter((key) => !actual.has(key));
  const extra = [...actual].filter((key) => !expected.has(key));
  if (missing.length || extra.length) fail(`${label} keys changed`);
}
function namesFromPattern(node, out = new Set()) {
  if (!node) return out;
  if (node.type === 'Identifier') out.add(node.name);
  else if (node.type === 'RestElement') namesFromPattern(node.argument, out);
  else if (node.type === 'AssignmentPattern') namesFromPattern(node.left, out);
  else if (node.type === 'ArrayPattern') node.elements.forEach((item) => namesFromPattern(item, out));
  else if (node.type === 'ObjectPattern') node.properties.forEach((property) => namesFromPattern(property.value || property.argument, out));
  return out;
}

function scriptSources(artifact) {
  if (!isRecord(artifact) || typeof artifact.html !== 'string' || !artifact.html) fail('artifact html must be a non-empty string');
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

function authoredScriptBindings(artifact) {
  if (typeof parse !== 'function') fail('WP-1 parser toolchain has no parse() API');
  const names = new Set();
  scriptSources(artifact).forEach((source, index) => {
    let ast;
    try { ast = parse(source, { sourceType: 'script', plugins: ['jsx', 'typescript'], errorRecovery: false }); }
    catch (error) { fail(`artifact script ${index} parser rejected source: ${error.message}`); }
    for (const statement of ast.program.body) {
      if (statement.type === 'FunctionDeclaration' && statement.id) names.add(statement.id.name);
      if (statement.type === 'ClassDeclaration' && statement.id) names.add(statement.id.name);
      if (statement.type === 'VariableDeclaration') statement.declarations.forEach((declaration) => namesFromPattern(declaration.id, names));
    }
  });
  return names;
}

function validateExternalBindings(manifest, artifact) {
  const allowed = new Set([
    ...FACADE_BINDINGS,
    ...BROWSER_RUNTIME_BINDINGS,
    ...JS_RUNTIME_BINDINGS,
    ...authoredScriptBindings(artifact),
  ]);
  for (const component of manifest.components) {
    if (!Array.isArray(component.external_bindings)
        || component.external_bindings.some((name) => typeof name !== 'string'))
      fail(`external bindings are invalid for ${component.name}`);
    if (new Set(component.external_bindings).size !== component.external_bindings.length)
      fail(`external bindings contain duplicates for ${component.name}`);
    for (const name of component.external_bindings) {
      if (!NAME_RE.test(name)) fail(`external binding is not an identifier: ${name}`);
      if (!allowed.has(name)) fail(`external binding ${name} is not proven by artifact scope`);
    }
  }
}

function runEmitterVerify(artifactPath, manifestPath, emissionDir) {
  const result = spawnSync(process.execPath, [emitterPath, '--artifact', artifactPath, '--manifest', manifestPath, '--out', emissionDir, '--verify'], {
    cwd: scriptDir, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024,
  });
  if (result.error) fail(`emitter verification process failed: ${result.error.message}`);
  if (result.status !== 0) fail(`emission verification rejected the App closure: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
}

function ambientDeclarations(manifest) {
  const names = new Set(['React', 'claimDocumentNavigationFocus', 'releaseDocumentNavigationFocus']);
  for (const component of manifest.components) {
    for (const name of component.external_bindings || []) {
      if (!NAME_RE.test(name)) fail(`external binding is not an identifier: ${name}`);
      if (!JS_RUNTIME_BINDINGS.has(name) && !BROWSER_RUNTIME_BINDINGS.has(name)) names.add(name);
    }
  }
  const declarations = [...names].sort().map((name) => `declare const ${name}: any;`);
  declarations.push('declare namespace JSX { interface IntrinsicElements { [elemName: string]: any; } interface IntrinsicAttributes { key?: unknown; } }');
  return `${declarations.join('\n')}\n`;
}

function parseDiagnostics(text) {
  const diagnostics = [];
  for (const line of text.split('\n')) {
    const match = line.match(/^(.*)\((\d+),(\d+)\): error (TS\d+): (.*)$/);
    if (!match) continue;
    const message = match[5];
    let category = 'other-semantic';
    if (match[4] === 'TS2304' || /Cannot find name/.test(message)) category = 'unknown-binding';
    else if (/does not exist on type 'Window|Property .* does not exist on type 'Window/.test(message)) category = 'browser-window';
    else if (/not assignable|missing the following properties|Expected \d+ arguments?/.test(message)) category = 'prop-or-type';
    else if (match[4] === 'TS2307') category = 'module-resolution';
    const unknown = message.match(/^Cannot find name '([^']+)'\.?$/);
    diagnostics.push({ file: match[1], line: Number(match[2]), column: Number(match[3]), code: match[4], message, category, ...(unknown ? { unknown_name: unknown[1] } : {}) });
  }
  return diagnostics;
}

function runTypeScript(stage) {
  const files = ['globals.d.ts', ...fs.readdirSync(path.join(stage, 'components')).filter((name) => name.endsWith('.tsx')).sort().map((name) => path.join('components', name))];
  const result = spawnSync(process.execPath, [tscPath, '--noEmit', '--target', 'ES2022', '--module', 'ESNext', '--moduleResolution', 'Bundler', '--jsx', 'react', '--skipLibCheck', '--pretty', 'false', ...files], {
    cwd: stage, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024,
  });
  if (result.error) fail(`TypeScript process failed: ${result.error.message}`);
  const output = `${result.stdout || ''}${result.stderr || ''}`.trim();
  return { status: result.status ?? 1, output, diagnostics: parseDiagnostics(output) };
}

function stageModules(manifest, emission, emissionDir, stage, { applyPropContracts = true } = {}) {
  const byId = new Map(manifest.components.map((component) => [component.id, component]));
  const components = [];
  fs.mkdirSync(path.join(stage, 'components'), { recursive: true });
  for (const module of emission.modules) {
    const component = byId.get(module.id);
    if (!component) fail(`emitted module ${module.name} is absent from manifest`);
    const source = readBytes(path.join(emissionDir, module.path), `emitted module ${module.name}`);
    const imports = [...component.dependencies].sort().map((id) => {
      const dependency = byId.get(id);
      if (!dependency) fail(`dependency ${id} for ${component.name} is unknown`);
      return `import { ${dependency.name} } from './${dependency.name}';`;
    });
    let authored = source.toString('utf8');
    let propContract = null;
    if (applyPropContracts && component.name === 'SpectrSettingsField') {
      const marker = 'function SpectrSettingsField({ label, hint, children, hidden }) {';
      if (!authored.includes(marker)) fail('SpectrSettingsField prop contract marker is missing');
      propContract = {
        name: component.name,
        type: 'label required; hint/children/hidden optional',
        value_type: 'label:string; hint?:string; children?:SpectrSettingsFieldChild; hidden?:boolean',
      };
      authored = authored.replace(marker, `type SpectrSettingsFieldChild = string | number | boolean | null | { readonly type: unknown; readonly props: Record<string, unknown> | null; readonly children: ReadonlyArray<SpectrSettingsFieldChild> } | ReadonlyArray<SpectrSettingsFieldChild>;\ntype SpectrSettingsFieldProps = { label: string; hint?: string; children?: SpectrSettingsFieldChild; hidden?: boolean };\nfunction SpectrSettingsField({ label, hint, children, hidden }: SpectrSettingsFieldProps) {`);
    }
    if (applyPropContracts && component.name === 'MBtn') {
      const marker = 'function MBtn({ children, onClick, primary, danger, action }) {';
      if (!authored.includes(marker)) fail('MBtn prop contract marker is missing');
      propContract = {
        name: component.name,
        type: 'all props optional; action:string; primary/danger:boolean; onClick:callable',
        value_type: 'children?:MBtnChild; onClick?:(...args:any[])=>unknown; primary?:boolean; danger?:boolean; action?:string',
      };
      authored = authored.replace(marker, `type MBtnChild = string | number | boolean | null | { readonly type: unknown; readonly props: Record<string, unknown> | null; readonly children: ReadonlyArray<MBtnChild> } | ReadonlyArray<MBtnChild>;\ntype MBtnProps = { children?: MBtnChild; onClick?: (...args: any[]) => unknown; primary?: boolean; danger?: boolean; action?: string };\nfunction MBtn({ children, onClick, primary, danger, action }: MBtnProps) {`);
    }
    const output = Buffer.from(`${imports.length ? `${imports.join('\n')}\n\n` : ''}${authored}\nexport { ${component.name} };\n`);
    fs.writeFileSync(path.join(stage, module.path), output);
    components.push({ id: component.id, name: component.name, path: module.path, authored_source_sha256: module.source_sha256, emitted_module_sha256: sha256(source), output_sha256: sha256(output), output_bytes: output.length, imports: [...component.dependencies].sort().map((id) => byId.get(id).name), ...(propContract ? { prop_contract: propContract } : {}) });
  }
  return components;
}

function classifyBindings(manifest, artifact) {
  const authored = authoredScriptBindings(artifact);
  const all = new Set();
  for (const component of manifest.components) for (const name of component.external_bindings || []) all.add(name);
  const groups = { facade_provided: [], browser_runtime: [], authored_script_scope: [], JavaScript_runtime: [], unclassified_external: [] };
  for (const name of [...all].sort()) {
    if (FACADE_BINDINGS.has(name)) groups.facade_provided.push(name);
    else if (BROWSER_RUNTIME_BINDINGS.has(name)) groups.browser_runtime.push(name);
    else if (authored.has(name)) groups.authored_script_scope.push(name);
    else if (JS_RUNTIME_BINDINGS.has(name)) groups.JavaScript_runtime.push(name);
    else groups.unclassified_external.push(name);
  }
  return { groups, authored_script_bindings: [...authored].sort() };
}

function build({ artifactPath, manifestPath, emissionDir, outReport, plantUnknown, plantProp, plantPropType, plantMbtnPropType }) {
  const artifactBytes = readBytes(artifactPath, 'artifact');
  const manifestBytes = readBytes(manifestPath, 'dependency manifest');
  const { value: artifact } = readJson(artifactPath, 'artifact');
  const { value: manifest } = readJson(manifestPath, 'dependency manifest');
  if (manifest.schema !== MANIFEST_SCHEMA || !Array.isArray(manifest.components)) fail('dependency manifest schema/components are invalid');
  validateExternalBindings(manifest, artifact);
  runEmitterVerify(artifactPath, manifestPath, emissionDir);
  const { raw: emissionRaw, value: emission } = readJson(path.join(emissionDir, 'authored-modules.manifest.json'), 'emission manifest');
  if (emission.schema !== EMISSION_SCHEMA || !Array.isArray(emission.modules)) fail('unsupported emission manifest');
  const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-wp1-app-debt-'));
  const controlStage = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-wp1-app-debt-control-'));
  try {
    const modules = stageModules(manifest, emission, emissionDir, stage);
    fs.writeFileSync(path.join(stage, 'globals.d.ts'), ambientDeclarations(manifest));
    const baseline = runTypeScript(stage);
    stageModules(manifest, emission, emissionDir, controlStage, { applyPropContracts: false });
    fs.writeFileSync(path.join(controlStage, 'globals.d.ts'), ambientDeclarations(manifest));
    const controlBaseline = runTypeScript(controlStage);
    let negativeControl = { status: 'not-run' };
    if (plantUnknown) {
      if (!NAME_RE.test(plantUnknown)) fail(`planted name is not an identifier: ${plantUnknown}`);
      const target = path.join(stage, 'components', `${modules[0].name}.tsx`);
      fs.appendFileSync(target, `\nconst __wp1_planted_unknown__: ${plantUnknown} = null;\n`);
      const planted = runTypeScript(stage);
      const found = planted.diagnostics.some((diagnostic) => diagnostic.code === 'TS2304' && diagnostic.message.includes(plantUnknown));
      if (!found) fail(`planted unknown binding ${plantUnknown} did not produce TS2304`);
      negativeControl = { status: 'passed', name: plantUnknown, diagnostic: planted.diagnostics.find((diagnostic) => diagnostic.message.includes(plantUnknown)), planted_diagnostic_count: planted.diagnostics.length };
    }
    let propNegativeControl = { status: 'not-run' };
    if (plantProp) {
      if (!NAME_RE.test(plantProp)) fail(`planted prop component is not an identifier: ${plantProp}`);
      const target = path.join(stage, 'components', `${plantProp}.tsx`);
      if (!fs.existsSync(target)) fail(`planted prop component ${plantProp} is not staged`);
      fs.appendFileSync(target, `\nconst __wp1_planted_missing_props__: SpectrSettingsFieldProps = { children: \"planted\" };\n`);
      const planted = runTypeScript(stage);
      const targetSuffix = `components/${plantProp}.tsx`;
      const found = planted.diagnostics.some((diagnostic) => ['TS2739', 'TS2741'].includes(diagnostic.code) && diagnostic.file.endsWith(targetSuffix) && /label/.test(diagnostic.message));
      if (!found) fail(`planted missing props for ${plantProp} did not produce a required-label diagnostic: ${JSON.stringify(planted.diagnostics.filter((diagnostic) => diagnostic.file.endsWith(targetSuffix)))}`);
      propNegativeControl = { status: 'passed', name: plantProp, diagnostic: planted.diagnostics.find((diagnostic) => ['TS2739', 'TS2741'].includes(diagnostic.code) && diagnostic.file.endsWith(targetSuffix) && /label/.test(diagnostic.message)), planted_diagnostic_count: planted.diagnostics.length };
    }
    let propTypeNegativeControl = { status: 'not-run' };
    if (plantPropType) {
      if (!NAME_RE.test(plantPropType)) fail(`planted prop-type component is not an identifier: ${plantPropType}`);
      const target = path.join(stage, 'components', `${plantPropType}.tsx`);
      if (!fs.existsSync(target)) fail(`planted prop-type component ${plantPropType} is not staged`);
      fs.appendFileSync(target, `\nconst __wp1_planted_wrong_label__ = <${plantPropType} label={1} />;\nconst __wp1_planted_wrong_hidden__ = <${plantPropType} label={\"ok\"} hidden={\"yes\"} />;\n`);
      const planted = runTypeScript(stage);
      const targetSuffix = `components/${plantPropType}.tsx`;
      const typeDiagnostics = planted.diagnostics.filter((diagnostic) => diagnostic.code === 'TS2322' && diagnostic.file.endsWith(targetSuffix));
      const typedControls = typeDiagnostics.filter((diagnostic) => /number/.test(diagnostic.message) && /string/.test(diagnostic.message)).concat(typeDiagnostics.filter((diagnostic) => /string/.test(diagnostic.message) && /boolean/.test(diagnostic.message)));
      if (typedControls.length !== 2) fail(`planted wrong prop types for ${plantPropType} did not produce exact label/hidden TS2322 controls: ${JSON.stringify(typeDiagnostics)}`);
      propTypeNegativeControl = { status: 'passed', name: plantPropType, properties: ['label', 'hidden'], diagnostics: typedControls, planted_diagnostic_count: planted.diagnostics.length };
    }
    let mbtnPropTypeNegativeControl = { status: 'not-run' };
    if (plantMbtnPropType) {
      if (plantMbtnPropType !== 'MBtn') fail(`MBtn prop-type control must target MBtn, got ${plantMbtnPropType}`);
      const target = path.join(stage, 'components', 'MBtn.tsx');
      if (!fs.existsSync(target)) fail('MBtn prop-type control target is not staged');
      fs.appendFileSync(target, '\nconst __wp1_planted_mbtn_wrong_primary__ = <MBtn primary={"yes"} />;\nconst __wp1_planted_mbtn_wrong_action__ = <MBtn action={1} />;\n');
      const planted = runTypeScript(stage);
      const targetSuffix = 'components/MBtn.tsx';
      const typeDiagnostics = planted.diagnostics.filter((diagnostic) => diagnostic.code === 'TS2322' && diagnostic.file.endsWith(targetSuffix));
      const typedControls = typeDiagnostics.filter((diagnostic) => /string/.test(diagnostic.message) && /boolean/.test(diagnostic.message)).concat(typeDiagnostics.filter((diagnostic) => /number/.test(diagnostic.message) && /string/.test(diagnostic.message)));
      if (typedControls.length !== 2) fail(`planted wrong MBtn prop types did not produce exact primary/action TS2322 controls: ${JSON.stringify(typeDiagnostics)}`);
      mbtnPropTypeNegativeControl = { status: 'passed', name: 'MBtn', properties: ['primary', 'action'], diagnostics: typedControls, planted_diagnostic_count: planted.diagnostics.length };
    }
    const bindingClass = classifyBindings(manifest, artifact);
    const report = {
      schema: SCHEMA, version: 1,
      artifact: { path: path.basename(artifactPath), sha256: sha256(artifactBytes), bytes: artifactBytes.length },
      dependency_manifest: { path: path.basename(manifestPath), sha256: sha256(manifestBytes) },
      emission: { path: path.basename(emissionDir), sha256: sha256(Buffer.from(emissionRaw)), modules: emission.modules.length },
      graph: { root: 'App', modules: modules.length, module_names: modules.map((module) => module.name), graph_sha256: sha256(Buffer.from(JSON.stringify(modules))) },
      bindings: bindingClass,
      baseline: {
        exit_status: baseline.status,
        diagnostics: baseline.diagnostics,
        counts: Object.fromEntries(Object.entries(Object.groupBy(baseline.diagnostics, (diagnostic) => diagnostic.category)).map(([key, values]) => [key, values.length])),
        counts_by_code: Object.fromEntries(Object.entries(Object.groupBy(baseline.diagnostics, (diagnostic) => diagnostic.code)).map(([key, values]) => [key, values.length])),
        unknown_binding_names: [...new Set(baseline.diagnostics.filter((diagnostic) => diagnostic.unknown_name).map((diagnostic) => diagnostic.unknown_name))].sort(),
      },
      prop_contract_effect: {
        before_diagnostics: controlBaseline.diagnostics.length,
        after_diagnostics: baseline.diagnostics.length,
        delta: baseline.diagnostics.length - controlBaseline.diagnostics.length,
        control_counts: Object.fromEntries(Object.entries(Object.groupBy(controlBaseline.diagnostics, (diagnostic) => diagnostic.category)).map(([key, values]) => [key, values.length])),
      },
      negative_control: negativeControl,
      prop_contracts: modules.filter((module) => module.prop_contract).map((module) => module.prop_contract),
      prop_negative_control: propNegativeControl,
      prop_type_negative_control: propTypeNegativeControl,
      mbtn_prop_type_negative_control: mbtnPropTypeNegativeControl,
      scope: { runtime_artifact_changed: false, semantic_full_app: baseline.diagnostics.length === 0, runtime_facade: 'not-applied', staging_only: true },
    };
    fs.writeFileSync(outReport, `${JSON.stringify(report, null, 2)}\n`);
    return report;
  } finally {
    fs.rmSync(stage, { recursive: true, force: true });
    fs.rmSync(controlStage, { recursive: true, force: true });
  }
}

function parseArgs(argv) {
  const args = {};
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === '--artifact' || arg === '--manifest' || arg === '--emission' || arg === '--out-report' || arg === '--plant-unknown' || arg === '--plant-prop' || arg === '--plant-prop-type' || arg === '--plant-mbtn-prop-type') args[arg.slice(2).replaceAll('-', '_')] = argv[++index];
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function usage() { console.log('usage: node tools/wp1_app_type_debt_inventory.mjs --artifact FILE --manifest FILE --emission DIR --out-report FILE [--plant-unknown NAME] [--plant-prop COMPONENT] [--plant-prop-type COMPONENT] [--plant-mbtn-prop-type MBtn]'); }

try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  if (!args.artifact || !args.manifest || !args.emission || !args.out_report) fail('--artifact, --manifest, --emission, and --out-report are required');
  const artifactPath = path.resolve(args.artifact), manifestPath = path.resolve(args.manifest), emissionDir = path.resolve(args.emission), outReport = path.resolve(args.out_report);
  if (!fs.existsSync(artifactPath)) fail(`artifact does not exist: ${artifactPath}`);
  if (!fs.existsSync(manifestPath)) fail(`dependency manifest does not exist: ${manifestPath}`);
  if (!fs.existsSync(emissionDir) || !fs.statSync(emissionDir).isDirectory()) fail(`emission directory does not exist: ${emissionDir}`);
  process.stdout.write(`${JSON.stringify(build({ artifactPath, manifestPath, emissionDir, outReport, plantUnknown: args.plant_unknown, plantProp: args.plant_prop, plantPropType: args.plant_prop_type, plantMbtnPropType: args.plant_mbtn_prop_type }), null, 2)}\n`);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
