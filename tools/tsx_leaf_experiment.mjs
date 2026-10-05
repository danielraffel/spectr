#!/usr/bin/env node
/**
 * Convert one small React.createElement component to readable TSX.
 *
 * This is intentionally a bounded WP-1 experiment, not the production
 * compiler: it accepts exactly one function component with one returned
 * React.createElement tree and fails closed on unsupported props/tags or
 * multiple return trees. The output is deterministic and remains valid JSX
 * only when every source expression is preserved verbatim in braces.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const parserPath = path.join(
  fileURLToPath(new URL('.', import.meta.url)),
  'wp1-parser', 'node_modules', '@babel', 'parser');
let babel;
try {
  babel = createRequire(import.meta.url)(parserPath);
} catch (error) {
  throw new Error(`parser unavailable at ${parserPath}: ${error.message}`);
}
const parse = babel.parse || babel.default?.parse;
if (typeof parse !== 'function') throw new Error('parser has no parse() API');
const parserPackage = JSON.parse(fs.readFileSync(path.join(parserPath, 'package.json'), 'utf8'));
if (parserPackage.version !== '7.28.4')
  throw new Error(`unsupported @babel/parser ${parserPackage.version}; expected 7.28.4`);

function sha256(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}
function fail(message) { throw new Error(`TSX leaf conversion failed: ${message}`); }
function isNode(value) { return value && typeof value === 'object' && typeof value.type === 'string'; }
function children(node, fn) {
  for (const [key, value] of Object.entries(node)) {
    if (['loc', 'start', 'end', 'extra', 'tokens', 'comments'].includes(key)) continue;
    if (Array.isArray(value)) value.forEach((child) => { if (isNode(child)) fn(child, key); });
    else if (isNode(value)) fn(value, key);
  }
}
function parseScript(source, label) {
  try {
    return parse(source, { sourceType: 'script', sourceFilename: label,
      plugins: ['jsx', 'typescript'], allowReturnOutsideFunction: true });
  } catch (error) { fail(`parser rejected ${label}: ${error.message}`); }
}
function sourceFor(input) {
  if (input.kind === 'source') return { source: fs.readFileSync(input.path, 'utf8'), label: input.path };
  const artifact = JSON.parse(fs.readFileSync(input.path, 'utf8'));
  if (!artifact || typeof artifact.html !== 'string' || artifact.html.length === 0)
    fail('artifact html must be a non-empty string');
  // The native artifact is a captured HTML shell. Parse every inline script and
  // preserve script index in diagnostics; component source offsets are local to
  // the selected script and are never presented as global artifact offsets.
  const scripts = [];
  const scriptRe = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;
  for (const match of artifact.html.matchAll(scriptRe)) {
    const attrs = match[1] || '';
    const typeMatch = attrs.match(/\btype\s*=\s*["']([^"']+)["']/i);
    const type = (typeMatch?.[1] || 'text/javascript').toLowerCase();
    if (type === 'application/json' || type === 'importmap') continue;
    if (!/^(?:text|application)\/(?:java|ecma)script$/.test(type) && type !== 'module') continue;
    scripts.push(match[2]);
  }
  if (!scripts.length) fail('artifact contains no script blocks');
  return { scripts, label: input.path };
}
function componentNodes(ast, out = []) {
  function visit(node) {
    if (!isNode(node)) return;
    if (node.type === 'FunctionDeclaration' && /^[A-Z][A-Za-z0-9_$]*$/.test(node.id?.name || '')) out.push(node);
    if (node.type === 'VariableDeclarator' && /^[A-Z][A-Za-z0-9_$]*$/.test(node.id?.name || '')
        && node.init?.type === 'ArrowFunctionExpression') out.push(node);
    children(node, visit);
  }
  visit(ast);
  return out;
}
function componentName(node) { return node.type === 'FunctionDeclaration' ? node.id.name : node.id.name; }
function functionBody(node) { return node.type === 'FunctionDeclaration' ? node.body : node.init.body; }
function raw(source, node) { return source.slice(node.start, node.end); }
function isCreateElement(node) {
  return node?.type === 'CallExpression' && node.callee?.type === 'MemberExpression'
    && node.callee.object?.type === 'Identifier' && node.callee.object.name === 'React'
    && node.callee.property?.type === 'Identifier' && node.callee.property.name === 'createElement';
}
function jsxName(node, source) {
  if (node?.type === 'StringLiteral' || node?.type === 'Identifier') return node.value ?? node.name;
  fail(`dynamic element tag at byte ${node?.start ?? 'unknown'}`);
}
function attrName(node, source) {
  if (node.type === 'Identifier') return node.name;
  if (node.type === 'StringLiteral' && /^[A-Za-z_$][\w$:-]*$/.test(node.value)) return node.value;
  fail(`unsupported property key at byte ${node.start}`);
}
function expressionAttribute(name, value, source) {
  if (value?.type === 'BooleanLiteral' && value.value === true) return name;
  // Keep source expressions exact. In particular, object expressions become
  // style={{ ... }} and shorthand props stay equivalent to their JS source.
  return `${name}={${raw(source, value)}}`;
}
function createElementToJsx(call, source) {
  if (!isCreateElement(call)) fail(`nested expression is not React.createElement at byte ${call.start}`);
  if (call.arguments.length < 2) fail(`createElement without props at byte ${call.start}`);
  const [tag, props, ...childrenArgs] = call.arguments;
  if (tag?.type === 'SpreadElement' || props?.type === 'SpreadElement')
    fail(`spread createElement argument at byte ${call.start}`);
  const tagText = jsxName(tag, source);
  if (props && props.type !== 'ObjectExpression' && props.type !== 'NullLiteral')
    fail(`non-object props for ${tagText} at byte ${props.start}`);
  const attrs = [];
  if (props?.type === 'ObjectExpression') {
    for (const property of props.properties) {
      if (property.type === 'SpreadElement') fail(`spread props for ${tagText} at byte ${property.start}`);
      if (property.type !== 'ObjectProperty' || property.computed || property.method)
        fail(`unsupported prop shape for ${tagText} at byte ${property.start}`);
      attrs.push(expressionAttribute(attrName(property.key, source), property.value, source));
    }
  }
  const opening = `<${tagText}${attrs.length ? ` ${attrs.join(' ')}` : ''}`;
  if (!childrenArgs.length) return `${opening} />`;
  const body = childrenArgs.map((child) => {
    if (isCreateElement(child)) return createElementToJsx(child, source);
    if (child?.type === 'JSXElement') return raw(source, child);
    if (child?.type === 'JSXText') return raw(source, child);
    return `{${raw(source, child)}}`;
  }).join('');
  return `${opening}>${body}</${tagText}>`;
}
function returnTrees(body) {
  const trees = [];
  function visit(node) {
    if (!isNode(node)) return;
    if (node.type === 'ReturnStatement' && isCreateElement(node.argument)) trees.push(node.argument);
    children(node, visit);
  }
  visit(body);
  return trees;
}
function convertOne(source, component) {
  const ast = parseScript(source, `${component} input`);
  const nodes = componentNodes(ast).filter((node) => componentName(node) === component);
  if (nodes.length !== 1) fail(`expected one component ${component}, found ${nodes.length}`);
  const node = nodes[0];
  const trees = returnTrees(functionBody(node));
  if (trees.length !== 1) fail(`expected one returned createElement tree for ${component}, found ${trees.length}`);
  const replacement = createElementToJsx(trees[0], source);
  const declaration = raw(source, node);
  const relativeStart = trees[0].start - node.start;
  const relativeEnd = trees[0].end - node.start;
  const converted = declaration.slice(0, relativeStart) + replacement + declaration.slice(relativeEnd);
  // Parse the result as TSX before emitting it. This catches invalid attribute
  // names/braces while leaving semantic compilation to the later build step.
  parse(converted, { sourceType: 'module', sourceFilename: `${component}.tsx`, plugins: ['jsx', 'typescript'] });
  return {
    schema: 'spectr-owned-tsx-leaf-v1', component, source_sha256: sha256(Buffer.from(source)),
    source_bytes: Buffer.byteLength(source), output_sha256: sha256(Buffer.from(converted)),
    output_bytes: Buffer.byteLength(converted), converted,
  };
}
function args(argv) {
  const out = { roots: [] };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--source' || arg === '--artifact' || arg === '--component' || arg === '--out') out[arg.slice(2)] = argv[++i];
    else if (arg === '--help') out.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return out;
}
function usage() { console.log('usage: node tools/tsx_leaf_experiment.mjs (--source FILE | --artifact FILE) --component NAME [--out FILE]'); }
function main(argv) {
  const input = args(argv);
  if (input.help) { usage(); return; }
  if (!!input.source === !!input.artifact) fail('choose exactly one of --source or --artifact');
  if (!input.component) fail('--component is required');
  const data = sourceFor({ kind: input.source ? 'source' : 'artifact', path: input.source || input.artifact });
  const scripts = data.scripts || [data.source];
  const candidates = [];
  scripts.forEach((script, index) => {
    const ast = parseScript(script, `${data.label} script ${index}`);
    for (const node of componentNodes(ast)) if (componentName(node) === input.component) candidates.push({ script, index });
  });
  if (candidates.length !== 1) fail(`expected one ${input.component} declaration across scripts, found ${candidates.length}`);
  const result = convertOne(candidates[0].script, input.component);
  const outputText = result.converted + '\n';
  result.output_sha256 = sha256(Buffer.from(outputText));
  result.output_bytes = Buffer.byteLength(outputText);
  delete result.converted;
  if (input.out) fs.writeFileSync(input.out, outputText);
  process.stdout.write(JSON.stringify({ ...result, script_index: candidates[0].index, output: input.out || null }, null, 2) + '\n');
}
try { main(process.argv.slice(2)); } catch (error) { console.error(error.message); process.exit(1); }
