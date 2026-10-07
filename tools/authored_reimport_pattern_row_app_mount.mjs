#!/usr/bin/env node
/**
 * Exercise authored PatternRow + MiniPreview through the real Spectr App mount.
 *
 * This is a staging proof: editor.html and the materialized runtime are never
 * modified. It proves SVG band previews survive authored TSX re-import in the
 * manager opened by the normal ReactDOM App, with a planted style mutation
 * rejected before Chromium execution.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import vm from 'node:vm';
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..');
let ts;
try {
  ts = createRequire(import.meta.url)(path.join(repo, 'tools', 'wp1-parser', 'node_modules', 'typescript'));
} catch (error) {
  fail(`pinned WP-1 TypeScript toolchain is missing; run npm ci --ignore-scripts --prefix ${path.join(repo, 'tools', 'wp1-parser')} (${error.message})`);
}
const SCHEMA = 'spectr-authored-reimport-pattern-row-app-mount-v1';
function fail(message) { throw new Error(`authored PatternRow App mount failed: ${message}`); }
function assert(condition, message) { if (!condition) fail(message); }
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function read(file) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${file}: ${error.message}`); } }
function write(file, value) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, value); }
function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (['editor', 'row', 'preview', 'out', 'chrome'].some(key => arg === `--${key}`)) args[arg.slice(2)] = argv[++i];
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}
function functionSlice(source, name) {
  const start = source.lastIndexOf(`function ${name}`); assert(start >= 0, `${name} declaration is missing`);
  const signatureEnd = source.indexOf(')', start); assert(signatureEnd >= 0, `${name} signature is incomplete`);
  const open = source.indexOf('{', signatureEnd); assert(open >= 0, `${name} body is missing`);
  let depth = 1; let quote = null; let escaped = false; let line = false; let block = false;
  for (let i = open + 1; i < source.length; i += 1) {
    const c = source[i], n = source[i + 1] || '';
    if (line) { if (c === '\n') line = false; continue; }
    if (block) { if (c === '*' && n === '/') { block = false; i += 1; } continue; }
    if (quote) { if (escaped) escaped = false; else if (c === '\\') escaped = true; else if (c === quote) quote = null; continue; }
    if (c === '/' && n === '/') { line = true; i += 1; continue; }
    if (c === '/' && n === '*') { block = true; i += 1; continue; }
    if (c === "'" || c === '"' || c === '`') { quote = c; continue; }
    if (c === '{') depth += 1;
    else if (c === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  fail(`unterminated ${name} declaration`);
}
function templateFromEditor(editor) {
  const html = read(editor).toString('utf8');
  const match = html.match(/<script\s+type="__bundler\/template">([\s\S]*?)<\/script>/i);
  assert(match, 'editor.html has no bundler template');
  let template; try { template = JSON.parse(match[1]); } catch (error) { fail(`template JSON is invalid: ${error.message}`); }
  return { html, template, originalRow: functionSlice(template, 'PatternRow'), originalPreview: functionSlice(template, 'MiniPreview') };
}
function compileModule(source, name, dependencies = []) {
  const imports = dependencies.map(dep => `import { ${dep} } from './${dep}.cjs';`).join('\n');
  const result = ts.transpileModule(`${imports}\nexport ${source}`, {
    fileName: `${name}.tsx`, reportDiagnostics: true,
    compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.React, jsxFactory: 'React.createElement', jsxFragmentFactory: 'React.Fragment' },
  });
  const errors = (result.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length, `${name} diagnostics: ${errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
  assert(result.outputText.includes(`exports.${name}`), `${name} did not emit an explicit export`);
  for (const dep of dependencies) assert(result.outputText.includes(`require("./${dep}.cjs")`), `${name} lost import ${dep}`);
  return result.outputText;
}
function reactMock() {
  return { createElement(type, props, ...children) { return { type, props: { ...(props || {}), ...(children.length ? { children } : {}) } }; }, Fragment: Symbol('Fragment') };
}
function context() {
  return { React: reactMock(), useMemoPM: fn => fn(), window: { Spectr: { resolveGains: (pattern, n) => (pattern.gains || []).slice(0, n) } }, globalThis: null };
}
function runCommonJs(code, globals, requireImpl) {
  const module = { exports: {} };
  vm.runInNewContext(code, { ...globals, module, exports: module.exports, require: requireImpl });
  return module.exports;
}
function loadPair(previewCode, rowCode, globals) {
  const preview = runCommonJs(previewCode, globals, () => fail('MiniPreview has unexpected imports'));
  const row = runCommonJs(rowCode, globals, id => id === './MiniPreview.cjs' ? preview : fail(`unexpected import ${id}`));
  assert(typeof preview.MiniPreview === 'function', 'MiniPreview export missing');
  assert(typeof row.PatternRow === 'function', 'PatternRow export missing');
  return { preview: preview.MiniPreview, row: row.PatternRow };
}
function expand(value, seen = new Set()) {
  if (value === null || value === undefined || typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') return value;
  if (Array.isArray(value)) return value.map(child => expand(child, seen));
  if (typeof value !== 'object') return value;
  if (typeof value.type === 'function') {
    assert(!seen.has(value.type), `component recursion in ${value.type.name}`);
    const next = new Set(seen); next.add(value.type); return expand(value.type(value.props || {}), next);
  }
  const props = {};
  for (const [key, item] of Object.entries(value.props || {})) props[key] = key === 'children' ? expand(item, seen) : item;
  return { type: typeof value.type === 'symbol' ? '[fragment]' : value.type, props };
}
function normalize(value) {
  if (typeof value === 'function') return '[function]';
  if (Array.isArray(value)) return value.map(normalize);
  if (value && typeof value === 'object') { const output = {}; for (const key of Object.keys(value).sort()) output[key] = normalize(value[key]); return output; }
  return value;
}
function samplePattern() { return { id: 'factory:alternating', name: 'ALTERNATING', source: 'factory', gains: [0.25, -0.5, -Infinity, 0.75, 0.15, -0.25] }; }
function renderParity(originalPreview, originalRow, authoredPreview, authoredRow) {
  const original = loadPair(originalPreview, originalRow, context());
  const imported = loadPair(authoredPreview, authoredRow, context());
  const props = { pattern: samplePattern(), selected: true, isDefault: true, onClick: () => {}, onDblClick: () => {}, N: 6 };
  assert(JSON.stringify(normalize(expand(original.row(props)))) === JSON.stringify(normalize(expand(imported.row(props)))), 'authored PatternRow render tree diverges from template');
  const svg = expand(imported.row(props));
  const count = node => !node || typeof node !== 'object' ? 0 : (node.type === 'rect' ? 1 : 0) + (Array.isArray(node) ? node.reduce((sum, child) => sum + count(child), 0) : count(node.props?.children));
  assert(count(svg) === 6, `authored MiniPreview rendered ${count(svg)} band bars, expected 6`);
}
function durablePatternRow(source) {
  const adapted = source.replace('<div onClick={onClick} onDoubleClick={onDblClick}', '<div data-spectr-pattern-id={pattern.id} data-spectr-pattern-source={pattern.source} onClick={onClick} onDoubleClick={onDblClick}');
  assert(adapted !== source, 'editor PatternRow durable selector adapter is missing'); return adapted;
}
function patchEditor(editorHtml, originalRow, compiledPreview, compiledRow, mutation = false) {
  const adapted = durablePatternRow(originalRow);
  const expected = mutation ? adapted.replace("gap: 10", "gap: 11") : adapted;
  const rowCode = mutation ? compiledRow.replace("gap: 10", "gap: 11") : compiledRow;
  assert(rowCode !== compiledRow || !mutation, 'planted PatternRow mutation did not change compiled output');
  const wrapper = `function __wp1ReimportedPatternRowModule() {\n  const previewModule = { exports: {} };\n  { const exports = previewModule.exports; ${compiledPreview} }\n  const rowModule = { exports: {} };\n  { const exports = rowModule.exports; const require = id => id === './MiniPreview.cjs' ? previewModule.exports : (() => { throw new Error('unexpected authored import ' + id); })(); ${rowCode} }\n  return rowModule.exports.PatternRow;\n}\nconst __wp1ReimportedPatternRow = __wp1ReimportedPatternRowModule();\nwindow.__wp1PatternRowReimported = true;\nwindow.__wp1PatternRowRenderCount = 0;\nfunction PatternRow(props) { window.__wp1PatternRowRenderCount += 1; return __wp1ReimportedPatternRow(props); }`;
  const marker = JSON.stringify(expected); const replacement = JSON.stringify(wrapper);
  const injection = `if (template.split(${marker}).length - 1 !== 1) throw new Error('PatternRow source replacement count mismatch');\n    template = template.split(${marker}).join(${replacement});`;
  const needle = '    // Inject after <head> so the DOCTYPE stays first; prepending the script';
  assert(editorHtml.includes(needle), 'editor bootstrap template assignment is missing');
  return editorHtml.replace(needle, `${needle}\n    ${injection}`);
}
function reservePort() { return new Promise((resolve, reject) => { const server = net.createServer(); server.once('error', reject); server.listen(0, '127.0.0.1', () => { const port = server.address().port; server.close(() => resolve(port)); }); }); }
async function browserRun(files, chrome, outDir) {
  const server = http.createServer((request, response) => { const file = files[new URL(request.url, 'http://127.0.0.1').pathname.slice(1)]; if (!file) { response.writeHead(404); response.end(); return; } response.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'content-length': file.length, connection: 'close' }); response.end(file); });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const debugPort = await reservePort(); const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-pattern-row-app-mount-')); let child; let socket; let stderr = ''; let nextId = 1; const pending = new Map();
  try {
    child = spawn(path.resolve(chrome), ['--headless=new', '--disable-gpu', '--disable-background-networking', '--disable-component-update', '--disable-sync', '--no-first-run', '--no-default-browser-check', '--remote-debugging-address=127.0.0.1', `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`, '--window-size=1320,860', 'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] });
    child.stderr.on('data', chunk => { stderr += chunk; }); let websocketUrl; const deadline = Date.now() + 15000;
    while (!websocketUrl && Date.now() < deadline) { try { const pages = await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json(); websocketUrl = pages.find(page => page.type === 'page')?.webSocketDebuggerUrl; } catch {} if (!websocketUrl) await new Promise(resolve => setTimeout(resolve, 50)); }
    assert(websocketUrl, `Chrome DevTools endpoint did not start: ${stderr.trim()}`); socket = new WebSocket(websocketUrl);
    await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
    socket.addEventListener('message', event => { const message = JSON.parse(event.data); if (!message.id || !pending.has(message.id)) return; const waiter = pending.get(message.id); pending.delete(message.id); message.error ? waiter.reject(new Error(JSON.stringify(message.error))) : waiter.resolve(message.result); });
    const command = (method, params = {}) => new Promise((resolve, reject) => { const id = nextId++; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); });
    const evaluate = async expression => { const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }); if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text || 'browser evaluation failed'); return result.result.value; };
    await command('Page.enable'); await command('Runtime.enable'); const results = {};
    for (const name of ['patched', 'baseline']) {
      const url = `http://127.0.0.1:${server.address().port}/${name}.html`; await command('Page.navigate', { url }); const pageDeadline = Date.now() + 30000;
      while (Date.now() < pageDeadline && !(await evaluate(`location.href === ${JSON.stringify(url)} && document.readyState === 'complete' && !!document.querySelector('[data-spectr-bank-ready="true"]')`))) await new Promise(resolve => setTimeout(resolve, 100));
      assert(await evaluate(`location.href === ${JSON.stringify(url)} && !!document.querySelector('[data-spectr-bank-ready="true"]')`), `${name} App did not become ready`);
      assert(await evaluate(`(() => { const b = Array.from(document.querySelectorAll('button')).find(x => x.textContent.trim().includes('PRESETS')); if (!b) return false; b.click(); return true; })()`), `${name} PRESETS control is missing`);
      assert(await evaluate(`(() => { const b = Array.from(document.querySelectorAll('button')).find(x => x.textContent.trim().includes('MANAGE')); if (!b) return false; b.click(); return true; })()`), `${name} MANAGE control is missing`);
      const panelDeadline = Date.now() + 10000; while (Date.now() < panelDeadline && !(await evaluate(`document.body.innerText.includes('PRESET MANAGER')`))) await new Promise(resolve => setTimeout(resolve, 50));
      assert(await evaluate(`document.body.innerText.includes('PRESET MANAGER')`), `${name} preset manager did not mount through App`);
      const summary = await evaluate(`(() => { const root = Array.from(document.querySelectorAll('div')).find(x => x.textContent.includes('PRESET MANAGER') && x.querySelectorAll('button').length >= 6); return { patternRows: document.querySelectorAll('[data-spectr-pattern-id]').length, miniPreviewBars: root ? root.querySelectorAll('svg rect').length : 0, rowRenders: window.__wp1PatternRowRenderCount || 0, manager: root?.outerHTML || '', ready: !!document.querySelector('[data-spectr-bank-ready="true"]'), canvases: document.querySelectorAll('canvas').length }; })()`);
      const screenshot = await command('Page.captureScreenshot', { format: 'png' }); const bytes = Buffer.from(screenshot.data, 'base64'); const screenshotPath = path.join(outDir, `${name}.png`); write(screenshotPath, bytes); results[name] = { ...summary, screenshot: { path: screenshotPath, sha256: sha256(bytes) } };
    }
    assert(results.patched.rowRenders > 0, 'authored PatternRow was never invoked by mounted PatternManager'); assert(results.patched.patternRows >= 8, 'mounted manager did not expose factory pattern rows'); assert(results.patched.miniPreviewBars >= 6, 'mounted manager did not render SVG band bars'); assert(results.baseline.manager === results.patched.manager, 'App manager DOM changed after authored PatternRow re-import');
    return { results, transport: 'cdp-http-loopback', stderr: stderr.trim() };
  } finally { try { socket?.close(); } catch {} if (child && !child.killed) child.kill('SIGTERM'); await new Promise(resolve => setTimeout(resolve, 250)); if (child && !child.killed) child.kill('SIGKILL'); if (server.closeAllConnections) server.closeAllConnections(); await new Promise(resolve => server.close(() => resolve())); fs.rmSync(profile, { recursive: true, force: true }); }
}
async function main(argv) {
  const args = parseArgs(argv); if (args.help) { console.log('usage: node tools/authored_reimport_pattern_row_app_mount.mjs --editor FILE --row FILE --preview FILE --out DIR --chrome PATH'); return; }
  for (const key of ['editor', 'row', 'preview', 'out', 'chrome']) assert(args[key], `--${key} is required`); const editor = path.resolve(args.editor); const rowPath = path.resolve(args.row); const previewPath = path.resolve(args.preview); const outDir = path.resolve(args.out); assert(!fs.existsSync(outDir), `output directory already exists: ${outDir}`);
  const { html, template, originalRow, originalPreview } = templateFromEditor(editor); const authoredRow = read(rowPath).toString('utf8'); const authoredPreview = read(previewPath).toString('utf8');
  const originalPreviewCode = compileModule(`export ${originalPreview}`, 'MiniPreview'); const originalRowCode = compileModule(`export ${durablePatternRow(originalRow)}`, 'PatternRow', ['MiniPreview']); const compiledPreview = compileModule(authoredPreview, 'MiniPreview'); const compiledRow = compileModule(authoredRow, 'PatternRow', ['MiniPreview']);
  renderParity(originalPreviewCode, originalRowCode, compiledPreview, compiledRow);
  const patched = patchEditor(html, originalRow, compiledPreview, compiledRow); const negative = patchEditor(html, originalRow, compiledPreview, compiledRow, true); assert(negative.includes('gap: 11'), 'planted negative control was not applied'); let negativeRejected = false; try { renderParity(originalPreviewCode, originalRowCode, compiledPreview, compiledRow.replace("gap: 10", "gap: 11")); } catch (error) { negativeRejected = /render tree diverges|band bars/.test(String(error.message)); } assert(negativeRejected, 'planted PatternRow style mutation was not detected before browser execution');
  write(path.join(outDir, 'baseline.html'), html); write(path.join(outDir, 'patched.html'), patched); write(path.join(outDir, 'row.tsx'), authoredRow); write(path.join(outDir, 'preview.tsx'), authoredPreview); write(path.join(outDir, 'compiled-row.cjs'), compiledRow); write(path.join(outDir, 'compiled-preview.cjs'), compiledPreview);
  const browser = await browserRun({ 'baseline.html': Buffer.from(html), 'patched.html': Buffer.from(patched) }, args.chrome, outDir); const receipt = { schema: SCHEMA, version: 1, source: { editor: path.basename(editor), editor_sha256: sha256(Buffer.from(html)), row: path.basename(rowPath), row_sha256: sha256(Buffer.from(authoredRow)), preview: path.basename(previewPath), preview_sha256: sha256(Buffer.from(authoredPreview)), compiled_row_sha256: sha256(Buffer.from(compiledRow)), compiled_preview_sha256: sha256(Buffer.from(compiledPreview)) }, checks: { tsx_compiled: true, node_render_tree_parity: true, app_mount: true, patched_pattern_row_invoked: browser.results.patched.rowRenders > 0, manager_dom_parity: browser.results.baseline.manager === browser.results.patched.manager, band_preview_svg: browser.results.patched.miniPreviewBars >= 6, negative_control: { status: 'passed', mutation: 'gap: 10 -> gap: 11', detected_before_browser: negativeRejected } }, browser, scope: { editor_html_unchanged: true, runtime_artifact_changed: false, full_native_parity: false, production_cutover: false } };
  write(path.join(outDir, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`); process.stdout.write(`${JSON.stringify(receipt, null, 2)}\n`);
}
main(process.argv.slice(2)).catch(error => { console.error(error.stack || error.message); process.exit(1); });
