#!/usr/bin/env node
/**
 * Produce a small, parser-backed owner/capture closure for imported UI source.
 *
 * This is deliberately an experiment for WP-1.  It parses JavaScript/JSX with
 * @babel/parser and fails closed on syntax errors, missing roots, or unresolved
 * identifiers in the requested closure.  It does not rewrite the materialized
 * runtime and does not infer dependencies from regular expressions.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const TOOLCHAIN = path.join(fileURLToPath(new URL('.', import.meta.url)), 'wp1-parser', 'node_modules', '@babel', 'parser');
let babel;
try {
  babel = createRequire(import.meta.url)(TOOLCHAIN);
} catch (error) {
  console.error(`WP-1 parser unavailable at ${TOOLCHAIN}: ${error.message}; run npm ci --prefix ${path.resolve(TOOLCHAIN, '../../..')}`);
  process.exit(2);
}
const parse = babel.parse || babel.default?.parse;
if (typeof parse !== 'function') throw new Error('WP-1 parser toolchain has no parse() API');
const parserPackage = JSON.parse(fs.readFileSync(path.join(TOOLCHAIN, 'package.json'), 'utf8'));
const EXPECTED_PARSER_VERSION = '7.28.4';
if (parserPackage.version !== EXPECTED_PARSER_VERSION)
  throw new Error(`unsupported @babel/parser version ${parserPackage.version}; expected ${EXPECTED_PARSER_VERSION}`);

const SCHEMA = 'spectr-owned-component-dependency-v1';
const COMPONENT = /^[A-Z][A-Za-z0-9_$]*$/;
const RUNTIME_GLOBALS = new Set([
  'React', 'window', 'document', 'globalThis', 'console', 'JSON', 'Math', 'Date',
  'Number', 'String', 'Boolean', 'Array', 'Object', 'RegExp', 'Promise', 'Error',
  'Infinity', 'NaN', 'undefined', 'setTimeout', 'clearTimeout', 'requestAnimationFrame',
  'cancelAnimationFrame', 'Intl', 'performance', 'parseInt', 'parseFloat', 'isFinite',
  'BigInt', 'Symbol', 'Map', 'Set', 'WeakMap', 'URL', 'URLSearchParams',
  // JavaScript's implicit function binding and browser globals used by the
  // frozen editor. These are runtime-provided names, not authored modules.
  'arguments', 'Blob', 'FileReader', 'navigator',
  // Optional host navigation hooks are intentionally feature-tested by the
  // authored UI and are supplied by Pulp when available.
  'claimDocumentNavigationFocus', 'releaseDocumentNavigationFocus',
]);

function fail(message) {
  throw new Error(message);
}
function sha256(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}
function namesFromPattern(node, out = new Set()) {
  if (!node) return out;
  if (node.type === 'Identifier') out.add(node.name);
  else if (node.type === 'RestElement') namesFromPattern(node.argument, out);
  else if (node.type === 'AssignmentPattern') namesFromPattern(node.left, out);
  else if (node.type === 'ArrayPattern') node.elements.forEach((x) => namesFromPattern(x, out));
  else if (node.type === 'ObjectPattern') node.properties.forEach((p) => {
    if (p.type === 'RestElement') namesFromPattern(p.argument, out);
    else namesFromPattern(p.value || p.argument, out);
  });
  return out;
}
function isComponentNode(node) {
  return (node.type === 'FunctionDeclaration' && node.id?.type === 'Identifier' && COMPONENT.test(node.id.name))
    || (node.type === 'VariableDeclarator' && node.id?.type === 'Identifier'
      && COMPONENT.test(node.id.name) && node.init?.type === 'ArrowFunctionExpression');
}
function componentName(node) {
  return node.id.name;
}
function isNode(value) {
  return value && typeof value === 'object' && typeof value.type === 'string';
}
function forEachChild(node, fn) {
  for (const [key, value] of Object.entries(node)) {
    if (key === 'loc' || key === 'start' || key === 'end' || key === 'extra' || key === 'tokens' || key === 'comments') continue;
    if (Array.isArray(value)) value.forEach((child) => { if (isNode(child)) fn(child, key); });
    else if (isNode(value)) fn(value, key);
  }
}
function collectComponents(ast, scriptIndex, source) {
  const found = [];
  function visit(node, componentStack) {
    if (!isNode(node)) return;
    let stack = componentStack;
    if (isComponentNode(node)) {
      const name = componentName(node);
      const raw = source.slice(node.start, node.end);
      const parent = componentStack.at(-1) || null;
      const entry = {
        name,
        kind: node.type === 'FunctionDeclaration' ? 'function' : 'arrow',
        script_index: scriptIndex,
        start: node.start,
        end: node.end,
        bytes: Buffer.byteLength(raw, 'utf8'),
        sha256: sha256(raw),
        node,
        parent,
      };
      found.push(entry);
      stack = [...componentStack, entry];
    }
    forEachChild(node, (child) => visit(child, stack));
  }
  visit(ast, []);
  return found;
}
function declarationBinding(node, parent, key) {
  if (!parent) return false;
  if (key === 'id' && [
    'FunctionDeclaration', 'FunctionExpression', 'ClassDeclaration', 'ClassExpression',
    'VariableDeclarator', 'ClassMethod', 'ObjectMethod',
  ].includes(parent.type)) return true;
  if (['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression', 'ObjectMethod', 'ClassMethod'].includes(parent.type)
      && key === 'params') return true;
  if (['CatchClause'].includes(parent.type) && key === 'param') return true;
  if (['VariableDeclarator'].includes(parent.type) && key === 'id') return true;
  if (['RestElement', 'AssignmentPattern', 'ArrayPattern', 'ObjectPattern', 'ObjectProperty'].includes(parent.type)
      && (key === 'argument' || key === 'left' || key === 'value' || key === 'properties' || key === 'elements')) return true;
  return false;
}
function isNonReferenceIdentifier(node, parent, key) {
  if (declarationBinding(node, parent, key)) return true;
  if (parent?.type === 'MemberExpression' && key === 'property' && !parent.computed) return true;
  if (parent?.type === 'OptionalMemberExpression' && key === 'property' && !parent.computed) return true;
  if (parent?.type === 'ObjectProperty' && key === 'key' && !parent.computed && !parent.shorthand) return true;
  if (parent?.type === 'ObjectMethod' && key === 'key' && !parent.computed) return true;
  if (parent?.type === 'ClassMethod' && key === 'key' && !parent.computed) return true;
  if (parent?.type === 'LabeledStatement' || parent?.type === 'BreakStatement' || parent?.type === 'ContinueStatement') return true;
  if (parent?.type === 'ImportSpecifier' || parent?.type === 'ImportDefaultSpecifier' || parent?.type === 'ImportNamespaceSpecifier') return true;
  return false;
}
function componentFunctionNode(component) {
  return component.node.type === 'VariableDeclarator' ? component.node.init : component.node;
}
function directScopeBindings(component) {
  const root = componentFunctionNode(component);
  const bindings = new Set();
  root.params.forEach((p) => namesFromPattern(p, bindings));
  function visit(node) {
    if (!isNode(node)) return;
    if (node !== root && isComponentNode(node)) {
      // A nested component has its own scope. Its name is still a binding in
      // the owner, so capture it from the declaration without walking its body.
      bindings.add(componentName(node));
      return;
    }
    if (node !== root && ['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(node.type)) {
      // Nested non-component closures can declare names visible only within
      // themselves. Do not make those names owner bindings.
      if (node.type === 'FunctionDeclaration' && node.id) bindings.add(node.id.name);
      return;
    }
    if (node.type === 'VariableDeclarator') namesFromPattern(node.id, bindings);
    if (node.type === 'ClassDeclaration' && node.id) bindings.add(node.id.name);
    if (node.type === 'CatchClause') namesFromPattern(node.param, bindings);
    forEachChild(node, (child) => visit(child));
  }
  visit(root);
  return bindings;
}
function nestedFunctionBindings(node) {
  const bindings = new Set();
  if (node.type === 'FunctionDeclaration' && node.id) bindings.add(node.id.name);
  node.params?.forEach((param) => namesFromPattern(param, bindings));
  function visit(child) {
    if (!isNode(child)) return;
    if (child !== node && isComponentNode(child)) return;
    if (child !== node && ['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(child.type)) {
      if (child.type === 'FunctionDeclaration' && child.id) bindings.add(child.id.name);
      return;
    }
    if (child.type === 'VariableDeclarator') namesFromPattern(child.id, bindings);
    if (child.type === 'ClassDeclaration' && child.id) bindings.add(child.id.name);
    if (child.type === 'CatchClause') namesFromPattern(child.param, bindings);
    forEachChild(child, visit);
  }
  visit(node.body);
  return bindings;
}
function jsxMemberText(node) {
  if (!node) return '<unknown>';
  if (node.type === 'JSXIdentifier') return node.name;
  if (node.type === 'JSXMemberExpression') return `${jsxMemberText(node.object)}.${jsxMemberText(node.property)}`;
  if (node.type === 'JSXNamespacedName') return `${jsxMemberText(node.namespace)}:${jsxMemberText(node.name)}`;
  return '<unknown>';
}
function directReferences(component) {
  const refs = [];
  const root = componentFunctionNode(component);
  const rootBindings = directScopeBindings(component);
  function visit(node, parent, key, scopes) {
    if (!isNode(node)) return;
    if (node !== root && isComponentNode(node)) return;
    let nextScopes = scopes;
    if (node !== root && ['FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression'].includes(node.type)) {
      nextScopes = [...scopes, nestedFunctionBindings(node)];
    }
    if (node.type === 'Identifier' && !isNonReferenceIdentifier(node, parent, key)) {
      refs.push({ name: node.name, resolved: [...nextScopes].some((scope) => scope.has(node.name)) });
    }
    // Babel represents JSX tag names separately from JavaScript identifiers.
    // Count direct uppercase tags as component references. JSX member and
    // namespaced tags are explicitly unsupported and become unresolved so a
    // future TSX conversion cannot silently omit them.
    if (node.type === 'JSXIdentifier' && parent
        && (parent.type === 'JSXOpeningElement' || parent.type === 'JSXClosingElement')
        && key === 'name' && COMPONENT.test(node.name)) {
      refs.push({ name: node.name, resolved: [...nextScopes].some((scope) => scope.has(node.name)) });
    }
    if ((node.type === 'JSXMemberExpression' || node.type === 'JSXNamespacedName')
        && parent && (parent.type === 'JSXOpeningElement' || parent.type === 'JSXClosingElement')
        && key === 'name') {
      refs.push({ name: `JSX tag ${jsxMemberText(node)}`, resolved: false });
    }
    forEachChild(node, (child, childKey) => visit(child, node, childKey, nextScopes));
  }
  visit(root, null, null, [rootBindings]);
  return refs;
}
function parseJavaScript(source, label, scriptIndex = 0) {
  try {
    return parse(source, {
      sourceType: 'script',
      plugins: ['jsx', 'typescript'],
      errorRecovery: false,
      ranges: false,
      tokens: false,
      attachComment: false,
    });
  } catch (error) {
    fail(`parser rejected ${label}: ${error.message}`);
  }
}
function htmlScripts(html) {
  const scripts = [];
  const re = /<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi;
  let match;
  while ((match = re.exec(html))) {
    const attrs = match[1] || '';
    const typeMatch = attrs.match(/\btype\s*=\s*["']([^"']+)["']/i);
    const type = (typeMatch?.[1] || 'text/javascript').toLowerCase();
    if (type === 'application/json' || type === 'importmap') continue;
    if (!/^(?:text|application)\/(?:java|ecma)script$/.test(type) && type !== 'module') continue;
    scripts.push({ index: scripts.length, source: match[2], html_start: match.index + match[0].indexOf(match[2]) });
  }
  return scripts;
}
function readInput(args) {
  if (args.artifact && args.source) fail('choose exactly one of --artifact or --source');
  if (!args.artifact && !args.source) fail('one of --artifact or --source is required');
  if (args.artifact) {
    const raw = fs.readFileSync(args.artifact);
    let doc;
    try { doc = JSON.parse(raw); } catch (error) { fail(`invalid artifact JSON: ${error.message}`); }
    if (typeof doc.html !== 'string' || !doc.html) fail('artifact html must be a non-empty string');
    return { source_kind: 'artifact', source_path: path.resolve(args.artifact), source_raw: raw, scripts: htmlScripts(doc.html) };
  }
  const sourceRaw = fs.readFileSync(args.source);
  return { source_kind: 'source', source_path: path.resolve(args.source), source_raw: sourceRaw, scripts: [{ index: 0, source: sourceRaw.toString('utf8'), html_start: 0 }] };
}
function argMap(argv) {
  const out = { roots: [] };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--artifact' || arg === '--source' || arg === '--out') out[arg.slice(2).replace('-', '_')] = argv[++i];
    else if (arg === '--root') out.roots.push(argv[++i]);
    else if (arg === '--help') out.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return out;
}
function usage() {
  console.log('usage: node tools/wp1_dependency_manifest.mjs (--artifact FILE | --source FILE) --root NAME [--root NAME ...] [--out FILE]');
}
function makeManifest(args) {
  const input = readInput(args);
  if (!args.roots.length) fail('at least one --root is required');
  const all = [];
  for (const script of input.scripts) {
    const ast = parseJavaScript(script.source, `${input.source_path} script ${script.index}`, script.index);
    all.push(...collectComponents(ast, script.index, script.source));
  }
  const byName = new Map();
  for (const component of all) {
    if (byName.has(component.name)) fail(`duplicate component name: ${component.name}`);
    byName.set(component.name, component);
  }
  const scriptBindings = new Set();
  for (const script of input.scripts) {
    const ast = parseJavaScript(script.source, `${input.source_path} script ${script.index}`, script.index);
    // Only program-scope declarations are external bindings. Recursing into
    // component bodies here would incorrectly make one component's locals
    // resolve an unrelated component's typo.
    for (const statement of ast.program.body) {
      if (statement.type === 'FunctionDeclaration' && statement.id)
        scriptBindings.add(statement.id.name);
      if (statement.type === 'ClassDeclaration' && statement.id)
        scriptBindings.add(statement.id.name);
      if (statement.type === 'VariableDeclaration')
        statement.declarations.forEach((decl) => namesFromPattern(decl.id, scriptBindings));
    }
  }
  const analyzed = new Map();
  for (const component of all) {
    const ownerBindings = new Set();
    let parent = component.parent;
    while (parent) {
      for (const name of directScopeBindings(parent)) ownerBindings.add(name);
      parent = parent.parent;
    }
    const ownBindings = directScopeBindings(component);
    const refs = directReferences(component);
    const dependencies = [];
    const captures = [];
    const externalBindings = [];
    const unresolved = [];
    const orderedRefs = [...refs].sort((a, b) => a.name.localeCompare(b.name));
    for (const { name, resolved } of orderedRefs) {
      const dependency = byName.get(name);
      // A local binding wins over the global component index unless that
      // index entry is the component's direct nested declaration. Without
      // this check a helper returned by `spectrMenuKit` named `Item` was
      // incorrectly attached to an unrelated top-level component named
      // `Item`, producing a false unresolved-capture failure for ContextMenu.
      const localBinding = resolved || ownBindings.has(name);
      const directNestedComponent = dependency && dependency.parent === component;
      if (dependency && dependency !== component && (!ownBindings.has(name) || directNestedComponent)) {
        dependencies.push(dependency);
        continue;
      }
      if (localBinding) continue;
      if (ownerBindings.has(name)) { captures.push(name); continue; }
      if (scriptBindings.has(name) || RUNTIME_GLOBALS.has(name)) { externalBindings.push(name); continue; }
      unresolved.push(name);
    }
    const raw = input.scripts[component.script_index].source.slice(component.start, component.end);
    analyzed.set(component, {
      id: `component:${component.name}:${component.sha256}`,
      name: component.name,
      kind: component.kind,
      owner: component.parent ? `component:${component.parent.name}:${component.parent.sha256}` : null,
      script_index: component.script_index,
      start: component.start,
      end: component.end,
      bytes: component.bytes,
      sha256: component.sha256,
      source_sha256: sha256(raw),
      dependencies: [...new Map(dependencies.map((x) => [x.sha256, x])).values()].sort((a, b) => a.name.localeCompare(b.name)).map((x) => `component:${x.name}:${x.sha256}`),
      captures: [...new Set(captures)],
      external_bindings: [...new Set(externalBindings)],
      unresolved: [...new Set(unresolved)],
    });
  }
  const roots = [...new Set(args.roots)].sort();
  for (const root of roots) if (!byName.has(root)) fail(`requested root is not a component: ${root}`);
  const closure = new Set();
  function add(component) {
    if (closure.has(component)) return;
    closure.add(component);
    const entry = analyzed.get(component);
    if (entry.unresolved.length) fail(`unresolved identifiers for ${component.name}: ${entry.unresolved.join(', ')}`);
    for (const id of entry.dependencies) {
      const name = id.split(':')[1];
      add(byName.get(name));
    }
  }
  roots.forEach((root) => add(byName.get(root)));
  const entries = [...closure].map((component) => analyzed.get(component)).sort((a, b) => a.id.localeCompare(b.id));
  const rootEntries = roots.map((root) => analyzed.get(byName.get(root)).id);
  return {
    schema: SCHEMA,
    parser: { name: '@babel/parser', version: parserPackage.version, plugins: ['jsx', 'typescript'], source_type: 'script' },
    source: { kind: input.source_kind, path: path.basename(input.source_path), sha256: sha256(input.source_raw), bytes: input.source_raw.length },
    roots: rootEntries,
    component_count: entries.length,
    components: entries,
  };
}

try {
  const args = argMap(process.argv.slice(2));
  if (args.help) { usage(); process.exit(0); }
  const manifest = makeManifest(args);
  const output = JSON.stringify(manifest, null, 2) + '\n';
  if (args.out) fs.writeFileSync(args.out, output);
  else process.stdout.write(output);
} catch (error) {
  console.error(`WP-1 dependency manifest failed: ${error.message}`);
  process.exit(1);
}
