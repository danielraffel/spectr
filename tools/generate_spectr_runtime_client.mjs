#!/usr/bin/env node
/**
 * Generate the renderer-neutral Spectr runtime client contract.
 *
 * The materialized runtime currently talks to `window.pulp.postMessage` with
 * string command names.  This tool makes that seam inspectable without
 * changing the shipping runtime: it derives the command union from the C++
 * handler registration sites, checks literal service calls against that
 * union, and emits a tiny ESM client plus a TypeScript declaration surface.
 * The checked-in output is staging evidence for a future authored-runtime
 * adoption; it is not a production cutover.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SCHEMA = 'spectr-generated-runtime-client-v1';
const NAME_RE = /^[a-z][a-z0-9_]*$/;
const DIGEST_RE = /^[0-9a-f]{64}$/;
const DEFAULT_BRIDGES = [
  path.join(ROOT, 'src/editor_bridge.cpp'),
  // This is the bridge attached by the shipping native editor. The browser
  // editor_view bridge has adapter-only lifecycle code and is intentionally
  // modeled separately below.
  path.join(ROOT, 'src/ui/native_editor.cpp'),
];
const DEFAULT_SERVICES = path.join(ROOT, 'native-ui/materialized/spectr-native-services.js');
const GENERATED = [
  'spectr-runtime-client.mjs',
  'spectr-runtime-client.d.ts',
  'spectr-runtime-client.inline.js',
  'spectr-runtime-client.manifest.json',
];
// These are publication-only branches emitted by the materialized runtime;
// every other literal branch command must correspond to a native handler.
const OPTIONAL_PUBLICATION_COMMANDS = new Set(['analyzer_frame']);
const ADAPTER_ONLY_COMMANDS = new Set(['editor_ready']);

function fail(message) { throw new Error(`runtime client generation failed: ${message}`); }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function read(file, label = file) {
  try { return fs.readFileSync(file); }
  catch (error) { fail(`cannot read ${label}: ${error.message}`); }
}
function write(file, bytes) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, bytes);
}
function assert(condition, message) { if (!condition) fail(message); }
function assertDigest(value, label) {
  assert(typeof value === 'string' && DIGEST_RE.test(value), `${label} is not a SHA-256 digest`);
}
function escapeRegExp(value) { return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'); }

function parseArgs(argv) {
  const args = { bridges: [], services: DEFAULT_SERVICES, out: path.join(ROOT, 'native-ui/materialized/generated'), verify: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--bridge') args.bridges.push(path.resolve(argv[++i] || ''));
    else if (arg === '--services') args.services = path.resolve(argv[++i] || '');
    else if (arg === '--out') args.out = path.resolve(argv[++i] || '');
    else if (arg === '--verify') args.verify = true;
    else if (arg === '--help') { args.help = true; }
    else fail(`unknown argument ${arg}`);
  }
  if (!args.bridges.length) args.bridges = DEFAULT_BRIDGES.slice();
  return args;
}

function extractHandlers(source, file) {
  const names = [];
  const re = /\b(?:bridge|[A-Za-z_][A-Za-z0-9_]*bridge_)\s*\.\s*add_handler\(\s*["']([^"']+)["']/g;
  let match;
  while ((match = re.exec(source))) {
    const name = match[1];
    assert(NAME_RE.test(name), `${file} contains unsafe handler name ${JSON.stringify(name)}`);
    names.push(name);
  }
  return names;
}

function extractServiceReferences(source) {
  const strict = new Set();
  const optional = new Set();
  const adapterOnly = new Set();
  const direct = /\b(?:dispatch|initial)\(\s*(["'])([a-z][a-z0-9_]*)\1/g;
  let match;
  while ((match = direct.exec(source))) strict.add(match[2]);
  // These are command-routing branches. Only publication-only names are
  // optional; lifecycle and request branches must be backed by C++ handlers.
  const branch = /\btype\s*===\s*(["'])([a-z][a-z0-9_]*)\1/g;
  while ((match = branch.exec(source))) {
    if (OPTIONAL_PUBLICATION_COMMANDS.has(match[2])) optional.add(match[2]);
    else if (ADAPTER_ONLY_COMMANDS.has(match[2])) adapterOnly.add(match[2]);
    else strict.add(match[2]);
  }
  const includes = /\[([^\]]+)\]\s*\.includes\(\s*type\s*\)/g;
  while ((match = includes.exec(source))) {
    const item = /(["'])([a-z][a-z0-9_]*)\1/g;
    let itemMatch;
    while ((itemMatch = item.exec(match[1]))) strict.add(itemMatch[2]);
  }
  return {
    strict: [...strict].sort(),
    optional: [...optional].sort(),
    adapter_only: [...adapterOnly].sort(),
  };
}

function toCamel(name) {
  return name.replace(/_([a-z0-9])/g, (_, letter) => letter.toUpperCase());
}

function makeModule(handlers) {
  const list = handlers.map(name => `  ${JSON.stringify(name)},`).join('\n');
  const methods = handlers.map(name => {
    const method = toCamel(name);
    return `    ${method}(payload = {}, id = '') { return request(${JSON.stringify(name)}, payload, id); },`;
  }).join('\n');
  return `// Generated by tools/generate_spectr_runtime_client.mjs. DO NOT EDIT.\n// Staging-only typed-client seam; editor.html and runtime.js remain unchanged.\nexport const SPECTR_COMMANDS = Object.freeze([\n${list}\n]);\n\nconst COMMAND_SET = new Set(SPECTR_COMMANDS);\n\nexport function createSpectrRuntimeClient(dispatch) {\n  if (typeof dispatch !== 'function') throw new TypeError('dispatch must be a function');\n  const request = (type, payload = {}, id = '') => {\n    if (!COMMAND_SET.has(type)) return Promise.reject(new RangeError('unknown Spectr command: ' + type));\n    try { return Promise.resolve(dispatch(type, payload, id)); }\n    catch (error) { return Promise.reject(error); }\n  };\n  return Object.freeze({\n    request,\n${methods}\n  });\n}\n`;
}

function makeInlineModule(handlers) {
  const list = handlers.map(name => `    ${JSON.stringify(name)},`).join('\n');
  return `// Generated by tools/generate_spectr_runtime_client.mjs. DO NOT EDIT.\n// This inline facade is embedded by tools/sync_generated_runtime_client.py.\n  const __spectrRuntimeCommands = Object.freeze([\n${list}\n  ]);\n  const __spectrRuntimeCommandSet = new Set(__spectrRuntimeCommands);\n  const __spectrCreateRuntimeClient = (dispatch) => {\n    if (typeof dispatch !== 'function') throw new TypeError('dispatch must be a function');\n    return Object.freeze({\n      request(type, payload = {}, id = '') {\n        if (!__spectrRuntimeCommandSet.has(type))\n          return Promise.reject(new RangeError('unknown Spectr command: ' + type));\n        try { return Promise.resolve(dispatch(type, payload, id)); }\n        catch (error) { return Promise.reject(error); }\n      },\n    });\n  };\n`;
}

function makeDeclaration(handlers) {
  const union = handlers.map(name => `  | ${JSON.stringify(name)}`).join('\n');
  const methods = handlers.map(name => `  ${toCamel(name)}<T = unknown>(payload?: SpectrPayload, id?: string): Promise<SpectrResponse<T>>;`).join('\n');
  return `// Generated by tools/generate_spectr_runtime_client.mjs. DO NOT EDIT.\nexport type SpectrCommand =\n${union};\n\nexport type SpectrPayload = Record<string, unknown>;\n\nexport interface SpectrResponse<T = unknown> {\n  ok: boolean;\n  payload?: T;\n  [key: string]: unknown;\n}\n\nexport type SpectrDispatch = (\n  type: SpectrCommand, payload?: SpectrPayload, id?: string,\n) => SpectrResponse<unknown> | PromiseLike<SpectrResponse<unknown>>;\n\nexport interface SpectrRuntimeClient {\n  request<T = unknown>(type: SpectrCommand, payload?: SpectrPayload, id?: string): Promise<SpectrResponse<T>>;\n${methods}\n}\n\nexport declare const SPECTR_COMMANDS: readonly SpectrCommand[];\nexport declare function createSpectrRuntimeClient(dispatch: SpectrDispatch): SpectrRuntimeClient;\n`;
}

function canonicalJson(value) { return `${JSON.stringify(value, null, 2)}\n`; }

function build(args, target) {
  const serviceBytes = read(args.services, 'native service source');
  const bridgeRecords = args.bridges.map(file => ({
    path: path.relative(ROOT, file).split(path.sep).join('/'),
    bytes: read(file, 'bridge source'),
  }));
  const handlersByName = new Map();
  for (const record of bridgeRecords) {
    const names = extractHandlers(record.bytes.toString('utf8'), record.path);
    for (const name of names) {
      const sources = handlersByName.get(name) || [];
      sources.push(record.path);
      handlersByName.set(name, sources);
    }
  }
  assert(handlersByName.size > 0, 'no bridge.add_handler registrations were found');
  const handlers = [...handlersByName.keys()].sort();
  const references = extractServiceReferences(serviceBytes.toString('utf8'));
  const unknown = references.strict.filter(name => !handlersByName.has(name));
  assert(!unknown.length, `native service references unregistered handler(s): ${unknown.join(', ')}`);
  const moduleBytes = Buffer.from(makeModule(handlers));
  const declarationBytes = Buffer.from(makeDeclaration(handlers));
  const outputs = {
    'spectr-runtime-client.mjs': moduleBytes,
    'spectr-runtime-client.d.ts': declarationBytes,
    'spectr-runtime-client.inline.js': Buffer.from(makeInlineModule(handlers)),
  };
  const manifest = {
    schema: SCHEMA,
    version: 1,
    scope: 'staging-runtime-client-contract',
    sources: {
      services: { path: path.relative(ROOT, args.services).split(path.sep).join('/'), sha256: sha256(serviceBytes), bytes: serviceBytes.length },
      bridges: bridgeRecords.map(record => ({ path: record.path, sha256: sha256(record.bytes), bytes: record.bytes.length })),
    },
    handlers: handlers.map(name => ({ name, method: toCamel(name), sources: [...new Set(handlersByName.get(name))].sort() })),
    service_references: {
      strict: references.strict,
      optional: references.optional,
      adapter_only: references.adapter_only,
    },
    generated: Object.fromEntries(Object.entries(outputs).map(([name, bytes]) => [name, { path: name, sha256: sha256(bytes), bytes: bytes.length }])),
  };
  outputs['spectr-runtime-client.manifest.json'] = Buffer.from(canonicalJson(manifest));
  for (const name of GENERATED) write(path.join(target, name), outputs[name]);
  return { manifest, outputs };
}

function readJson(file, label) {
  const bytes = read(file, label);
  try { return { bytes, value: JSON.parse(bytes) }; }
  catch (error) { fail(`${label} is not valid JSON: ${error.message}`); }
}

function verify(args) {
  const { value: supplied } = readJson(path.join(args.out, 'spectr-runtime-client.manifest.json'), 'runtime client manifest');
  assert(supplied.schema === SCHEMA && supplied.version === 1, 'runtime client manifest schema is unsupported');
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-runtime-client-verify-'));
  try {
    const rebuilt = build(args, temp);
    const rebuiltManifest = rebuilt.outputs['spectr-runtime-client.manifest.json'];
    assert(Buffer.compare(rebuiltManifest, read(path.join(args.out, 'spectr-runtime-client.manifest.json'))) === 0, 'runtime client manifest is stale or non-deterministic');
    for (const name of GENERATED.slice(0, 3)) {
      assert(Buffer.compare(rebuilt.outputs[name], read(path.join(args.out, name))) === 0, `${name} is stale or non-deterministic`);
    }
    for (const [name, receipt] of Object.entries(supplied.generated || {})) {
      assert(receipt.path === name, `${name} manifest path is not canonical`);
      assertDigest(receipt.sha256, `${name} manifest digest`);
      const bytes = read(path.join(args.out, name), `${name} output`);
      assert(sha256(bytes) === receipt.sha256 && bytes.length === receipt.bytes, `${name} output identity changed`);
    }
    process.stdout.write(`${JSON.stringify({ verified: true, handlers: supplied.handlers.length, strict_service_references: supplied.service_references.strict.length }, null, 2)}\n`);
  } finally { fs.rmSync(temp, { recursive: true, force: true }); }
}

function main(argv) {
  const args = parseArgs(argv);
  if (args.help) { console.log('usage: node tools/generate_spectr_runtime_client.mjs [--bridge FILE ...] [--services FILE] --out DIR [--verify]'); return; }
  for (const file of [...args.bridges, args.services]) assert(fs.existsSync(file), `input does not exist: ${file}`);
  if (args.verify) verify(args);
  else {
    const result = build(args, args.out);
    process.stdout.write(`${JSON.stringify({ generated: GENERATED, handlers: result.manifest.handlers.length, strict_service_references: result.manifest.service_references.strict.length }, null, 2)}\n`);
  }
}

try { main(process.argv.slice(2)); }
catch (error) { console.error(error.stack || error.message); process.exit(1); }
