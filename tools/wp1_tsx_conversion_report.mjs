#!/usr/bin/env node
/**
 * Produce a deterministic report for the bounded WP-1 TSX leaf codemod.
 *
 * The report is intentionally read-only: it inventories every top-level
 * function component in the frozen materialized artifact, asks the existing
 * fail-closed converter to process it, and records exact source hashes plus
 * structured rejection classes. It does not rewrite the runtime artifact.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import process from 'node:process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const REPORT_SCHEMA = 'spectr-owned-tsx-conversion-report-v1';
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const parserPath = path.join(scriptDir, 'wp1-parser', 'node_modules', '@babel', 'parser');
const converterPath = path.join(scriptDir, 'tsx_leaf_experiment.mjs');

function fail(message) { throw new Error(`WP-1 TSX conversion report failed: ${message}`); }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function isNode(value) { return value && typeof value === 'object' && typeof value.type === 'string'; }
function walk(node, fn) {
  if (!isNode(node)) return;
  fn(node);
  for (const [key, value] of Object.entries(node)) {
    if (['loc', 'start', 'end', 'extra', 'tokens', 'comments'].includes(key)) continue;
    if (Array.isArray(value)) value.forEach(child => walk(child, fn));
    else if (isNode(value)) walk(value, fn);
  }
}
function parseScript(parse, source, label) {
  try {
    return parse(source, { sourceType: 'script', sourceFilename: label,
      plugins: ['jsx', 'typescript'], allowReturnOutsideFunction: true });
  } catch (error) { fail(`parser rejected ${label}: ${error.message}`); }
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
    byte += Buffer.byteLength(source.slice(index, index + width));
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
    scripts.push(match[2]);
  }
  if (!scripts.length) fail('artifact contains no JavaScript script blocks');
  return scripts;
}
function discoverComponents(parse, html) {
  const candidates = [];
  htmlScripts(html).forEach((source, scriptIndex) => {
    const ast = parseScript(parse, source, `materialized script ${scriptIndex}`);
    const bytes = utf8Offsets(source);
    walk(ast, node => {
      if (node.type !== 'FunctionDeclaration' || !/^[A-Z][A-Za-z0-9_$]*$/.test(node.id?.name || '')) return;
      const body = source.slice(node.body.start, node.body.end);
      if (!body.includes('React.createElement')) return;
      const raw = source.slice(node.start, node.end);
      candidates.push({
        name: node.id.name, script_index: scriptIndex, start: bytes[node.start], end: bytes[node.end],
        bytes: Buffer.byteLength(raw), source_sha256: sha256(Buffer.from(raw)),
      });
    });
  });
  candidates.sort((a, b) => a.script_index - b.script_index || a.start - b.start || a.name.localeCompare(b.name));
  const names = new Set();
  for (const candidate of candidates) {
    if (names.has(candidate.name)) fail(`duplicate top-level component ${candidate.name}`);
    names.add(candidate.name);
  }
  if (!candidates.length) fail('artifact contains no top-level function components');
  return candidates;
}
function classify(stderr) {
  const text = stderr.trim().split('\n').filter(Boolean).at(-1) || 'converter failed without a diagnostic';
  if (text.includes('expected one returned createElement tree')) return ['multiple-return-trees', text];
  if (text.includes('dynamic element tag')) return ['dynamic-element-tag', text];
  if (text.includes('unsupported prop shape')) return ['unsupported-prop-shape', text];
  if (text.includes('spread props') || text.includes('spread createElement argument')) return ['spread-props', text];
  if (text.includes('non-object props')) return ['non-object-props', text];
  if (text.includes('parser rejected')) return ['parser-rejected', text];
  fail(`unclassified converter rejection: ${text}`);
}
function convert(artifactPath, candidate) {
  const result = spawnSync(process.execPath, [converterPath, '--artifact', artifactPath, '--component', candidate.name], {
    cwd: path.dirname(scriptDir), encoding: 'utf8', maxBuffer: 8 * 1024 * 1024,
  });
  if (result.error) fail(`converter process failed for ${candidate.name}: ${result.error.message}`);
  if (result.status === 0) {
    let report;
    try { report = JSON.parse(result.stdout); } catch (error) { fail(`converter emitted invalid JSON for ${candidate.name}: ${error.message}`); }
    if (report.component !== candidate.name)
      fail(`converter component identity mismatch for ${candidate.name}`);
    return {
      ...candidate, status: 'converted', output_sha256: report.output_sha256,
      output_bytes: report.output_bytes, rejection: null,
    };
  }
  const [category, message] = classify(result.stderr);
  return { ...candidate, status: 'blocked', output_sha256: null, output_bytes: null,
    rejection: { category, message } };
}
function makeReport(artifactPath) {
  let raw;
  try { raw = fs.readFileSync(artifactPath); } catch (error) { fail(`cannot read artifact: ${error.message}`); }
  let artifact;
  try { artifact = JSON.parse(raw); } catch (error) { fail(`artifact JSON is invalid: ${error.message}`); }
  if (!artifact || typeof artifact.html !== 'string' || !artifact.html.length) fail('artifact html must be a non-empty string');
  let babel;
  try { babel = createRequire(import.meta.url)(parserPath); }
  catch (error) { fail(`parser unavailable at ${parserPath}: ${error.message}`); }
  const parse = babel.parse || babel.default?.parse;
  if (typeof parse !== 'function') fail('parser has no parse() API');
  const parserPackage = JSON.parse(fs.readFileSync(path.join(parserPath, 'package.json'), 'utf8'));
  if (parserPackage.version !== '7.28.4') fail(`unsupported @babel/parser ${parserPackage.version}`);
  const htmlBytes = Buffer.from(artifact.html);
  const candidates = discoverComponents(parse, artifact.html);
  const components = candidates.map(candidate => convert(artifactPath, candidate));
  const blocked = components.filter(component => component.status === 'blocked');
  const categories = {};
  blocked.forEach(component => { const key = component.rejection.category; categories[key] = (categories[key] || 0) + 1; });
  return {
    schema: REPORT_SCHEMA, version: 1,
    artifact: { path: path.basename(artifactPath), sha256: sha256(raw), html_sha256: sha256(htmlBytes), html_bytes: htmlBytes.length },
    converter: { path: path.basename(converterPath), parser: '@babel/parser', parser_version: parserPackage.version },
    counts: { components: components.length, converted: components.length - blocked.length, blocked: blocked.length, blocked_by_category: Object.fromEntries(Object.entries(categories).sort(([a], [b]) => a.localeCompare(b))) },
    components,
  };
}
function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--artifact' || argv[i] === '--out') args[argv[i].slice(2)] = argv[++i];
    else if (argv[i] === '--help') args.help = true;
    else fail(`unknown argument ${argv[i]}`);
  }
  return args;
}
function usage() { console.log('usage: node tools/wp1_tsx_conversion_report.mjs --artifact FILE [--out FILE]'); }
try {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  if (!args.artifact) fail('--artifact is required');
  const artifactPath = path.resolve(args.artifact);
  if (!fs.existsSync(artifactPath)) fail(`artifact does not exist: ${artifactPath}`);
  const report = makeReport(artifactPath);
  const output = `${JSON.stringify(report, null, 2)}\n`;
  if (args.out) fs.writeFileSync(path.resolve(args.out), output);
  process.stdout.write(output);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
