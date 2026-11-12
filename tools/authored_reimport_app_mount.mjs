#!/usr/bin/env node
/**
 * Exercise one authored TSX leaf through the real Spectr App mount.
 *
 * This is a staging proof: the checked-in editor.html is never modified and
 * the materialized runtime is never rewritten.  The fixture replaces only the
 * source MBtn declaration in the unpacked Claude template with the compiled
 * authored module, then loads the normal ReactDOM App and opens PRESETS.  The
 * baseline and patched manager DOM are compared, while a planted source
 * mutation must be rejected before Chromium runs.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import vm from 'node:vm';
import { spawn, spawnSync } from 'node:child_process';
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
const SCHEMA = 'spectr-authored-reimport-app-mount-v1';

function fail(message) { throw new Error(`authored App mount failed: ${message}`); }
function assert(condition, message) { if (!condition) fail(message); }
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function read(file) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${file}: ${error.message}`); } }
function write(file, value) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, value); }

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (['editor', 'source', 'out', 'chrome'].some(key => arg === `--${key}`)) args[arg.slice(2)] = argv[++i];
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}

function functionSlice(source, name) {
  const marker = `function ${name}`;
  const start = source.indexOf(marker);
  assert(start >= 0, `${name} declaration is missing`);
  const signatureEnd = source.indexOf(')', start);
  assert(signatureEnd >= 0, `${name} declaration has no parameter close`);
  const open = source.indexOf('{', signatureEnd);
  assert(open >= 0, `${name} declaration has no body`);
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
  let template;
  try { template = JSON.parse(match[1]); } catch (error) { fail(`bundler template JSON is invalid: ${error.message}`); }
  assert(typeof template === 'string' && template.includes('function MBtn'), 'bundler template has no MBtn source');
  return { html, template, originalMBtn: functionSlice(template, 'MBtn') };
}

function compileAuthored(source, sourcePath) {
  const result = ts.transpileModule(source, {
    fileName: sourcePath, reportDiagnostics: true,
    compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.React, jsxFactory: 'React.createElement' },
  });
  const errors = (result.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length, `authored MBtn TSX diagnostics: ${errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
  assert(result.outputText.includes('exports.MBtn'), 'authored MBtn did not emit an explicit export');
  return result.outputText;
}

function durableMBtnSource(originalMBtn) {
  const adapted = originalMBtn
    .replace('function MBtn({ children, onClick, primary, danger }) {\n  return (\n    <button onClick={onClick} style={{',
      'function MBtn({ children, onClick, primary, danger, action }) {\n  return (\n    <button data-spectr-manager-action={action} onClick={onClick} style={{');
  assert(adapted !== originalMBtn, 'editor MBtn durable adapter shape is missing');
  return adapted;
}

function reactMock() {
  return { createElement(type, props, ...children) { return { type, props: { ...(props || {}), ...(children.length ? { children } : {}) } }; }, Fragment: Symbol('Fragment') };
}

function renderWith(originalCompiled, compiled) {
  const context = { React: reactMock(), require: () => fail('MBtn has unexpected imports') };
  const originalModule = { exports: {} };
  vm.runInNewContext(originalCompiled, { ...context, module: originalModule, exports: originalModule.exports });
  const original = originalModule.exports.MBtn({ children: 'SAVE CURRENT', onClick: () => {}, primary: true });
  const module = { exports: {} };
  vm.runInNewContext(compiled, { ...context, module, exports: module.exports });
  const imported = module.exports.MBtn({ children: 'SAVE CURRENT', onClick: () => {}, primary: true });
  assert(JSON.stringify(original) === JSON.stringify(imported), 'authored MBtn render tree diverges from template source');
}

function patchEditor(editorHtml, originalMBtn, compiled, mutation = null) {
  const adaptedMBtn = durableMBtnSource(originalMBtn);
  const expected = mutation ? adaptedMBtn.replace("height: 26", "height: 27") : adaptedMBtn;
  const moduleCode = mutation ? compiled.replace("height: 26", "height: 27") : compiled;
  if (mutation) assert(moduleCode !== compiled, 'planted mutation did not change authored module');
  const wrapper = `function __wp1ReimportedMBtnModule() {\n  const module = { exports: {} };\n  const exports = module.exports;\n  ${moduleCode}\n  return module.exports.MBtn;\n}\nconst __wp1ReimportedMBtn = __wp1ReimportedMBtnModule();\nwindow.__wp1MBtnReimported = true;\nwindow.__wp1MBtnRenderCount = 0;\nfunction MBtn(props) { window.__wp1MBtnRenderCount += 1; return __wp1ReimportedMBtn(props); }`;
  const marker = JSON.stringify(expected);
  const replacement = JSON.stringify(wrapper);
  const injection = `if (template.split(${marker}).length - 1 !== 1) throw new Error('MBtn source replacement count mismatch');\n    template = template.split(${marker}).join(${replacement});`;
  // The editor bootstrap applies its durable source adapters after template
  // unpacking. Inject after those adapters so the MBtn replacement does not
  // hide a required adapter needle (and so the replacement sees the final
  // action-marker form used by the real App).
  // Match the complete source line. The editor keeps "prepending the script"
  // on the same comment line; inserting after only the semicolon would split
  // that comment and emit invalid JavaScript in the patched fixture.
  const needle = '    // Inject after <head> so the DOCTYPE stays first; prepending the script';
  assert(editorHtml.includes(needle), 'editor bootstrap template assignment is missing');
  const output = editorHtml.replace(needle, `${needle}\n    ${injection}`);
  assert(output !== editorHtml, 'editor fixture was not changed');
  return output;
}

function reservePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer(); server.once('error', reject);
    server.listen(0, '127.0.0.1', () => { const port = server.address().port; server.close(() => resolve(port)); });
  });
}

async function browserRun(files, chrome, outDir) {
  const server = http.createServer((request, response) => {
    const file = files[new URL(request.url, 'http://127.0.0.1').pathname.slice(1)];
    if (!file) { response.writeHead(404); response.end(); return; }
    response.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'content-length': file.length, connection: 'close' }); response.end(file);
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const debugPort = await reservePort(); const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-authored-app-mount-'));
  let child; let socket; let stderr = ''; let nextId = 1; const pending = new Map();
  try {
    child = spawn(path.resolve(chrome), ['--headless=new', '--disable-gpu', '--disable-background-networking', '--disable-component-update', '--disable-sync', '--no-first-run', '--no-default-browser-check', '--remote-debugging-address=127.0.0.1', `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`, '--window-size=1320,860', 'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] });
    child.stderr.on('data', chunk => { stderr += chunk; });
    let websocketUrl; const deadline = Date.now() + 15000;
    while (!websocketUrl && Date.now() < deadline) { try { const pages = await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json(); websocketUrl = pages.find(page => page.type === 'page')?.webSocketDebuggerUrl; } catch {} if (!websocketUrl) await new Promise(resolve => setTimeout(resolve, 50)); }
    assert(websocketUrl, `Chrome DevTools endpoint did not start: ${stderr.trim()}`); socket = new WebSocket(websocketUrl);
    await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
    socket.addEventListener('message', event => { const message = JSON.parse(event.data); if (!message.id || !pending.has(message.id)) return; const waiter = pending.get(message.id); pending.delete(message.id); message.error ? waiter.reject(new Error(JSON.stringify(message.error))) : waiter.resolve(message.result); });
    const command = (method, params = {}) => new Promise((resolve, reject) => { const id = nextId++; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); });
    const evaluate = async expression => { const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }); if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text || 'browser evaluation failed'); return result.result.value; };
    await command('Page.enable'); await command('Runtime.enable');
    const results = {};
    for (const name of ['patched', 'baseline']) {
      const url = `http://127.0.0.1:${server.address().port}/${name}.html`;
      await command('Page.navigate', { url });
      const pageDeadline = Date.now() + 30000;
      while (Date.now() < pageDeadline && !(await evaluate(`location.href === ${JSON.stringify(url)} && document.readyState === 'complete' && !!document.querySelector('[data-spectr-bank-ready="true"]')`))) await new Promise(resolve => setTimeout(resolve, 100));
      if (!await evaluate(`location.href === ${JSON.stringify(url)} && !!document.querySelector('[data-spectr-bank-ready="true"]')`)) {
        const diagnostic = await evaluate(`({ error: document.getElementById('__bundler_err')?.textContent || '', body: document.body?.innerText?.slice(-1500) || '', consoleErrors: window.__wp1BrowserErrors || [] })`);
        fail(`${name} App did not become ready; diagnostic=${JSON.stringify(diagnostic)}`);
      }
      const before = await evaluate(`Array.from(document.querySelectorAll('button')).map(b => b.textContent.trim()).filter(Boolean)`);
      const opened = await evaluate(`(() => { const b = Array.from(document.querySelectorAll('button')).find(x => x.textContent.trim().includes('PRESETS')); if (!b) return false; b.click(); return true; })()`);
      assert(opened, `${name} PRESETS control is missing`);
      const managed = await evaluate(`(() => { const b = Array.from(document.querySelectorAll('button')).find(x => x.textContent.trim().includes('MANAGE')); if (!b) return false; b.click(); return true; })()`);
      if (!managed) {
        const afterPresets = await evaluate(`Array.from(document.querySelectorAll('button')).map(b => b.textContent.trim()).filter(Boolean)`);
        fail(`${name} MANAGE control is missing after opening PRESETS; buttons=${JSON.stringify(afterPresets)}`);
      }
      const panelDeadline = Date.now() + 10000;
      while (Date.now() < panelDeadline && !(await evaluate(`document.body.innerText.includes('PRESET MANAGER')`))) await new Promise(resolve => setTimeout(resolve, 50));
      assert(await evaluate(`document.body.innerText.includes('PRESET MANAGER')`), `${name} preset manager did not mount through App`);
      const summary = await evaluate(`(() => { const root = Array.from(document.querySelectorAll('div')).find(x => x.textContent.includes('PRESET MANAGER') && x.querySelectorAll('button').length >= 6); return { mbtn: window.__wp1MBtnRenderCount || 0, manager: root?.outerHTML || '', ready: !!document.querySelector('[data-spectr-bank-ready="true"]'), bands: document.querySelectorAll('canvas').length }; })()`);
      const screenshot = await command('Page.captureScreenshot', { format: 'png' });
      const screenshotPath = path.join(outDir, `${name}.png`); write(screenshotPath, Buffer.from(screenshot.data, 'base64'));
      results[name] = { ...summary, buttons_before: before, screenshot: { path: screenshotPath, sha256: sha256(Buffer.from(screenshot.data, 'base64')) } };
    }
    assert(results.patched.mbtn > 0, 'patched authored MBtn was never invoked by mounted PatternManager');
    assert(results.baseline.manager.length > 0 && results.patched.manager.length > 0, 'manager DOM evidence is missing');
    assert(results.baseline.manager === results.patched.manager, 'App manager DOM changed after authored MBtn re-import');
    return { results, transport: 'cdp-http-loopback', stderr: stderr.trim() };
  } finally {
    try { socket?.close(); } catch {}
    if (child && !child.killed) child.kill('SIGTERM');
    await new Promise(resolve => setTimeout(resolve, 250));
    if (child && !child.killed) child.kill('SIGKILL');
    if (server.closeAllConnections) server.closeAllConnections();
    await new Promise(resolve => server.close(() => resolve()));
    fs.rmSync(profile, { recursive: true, force: true });
  }
}

async function main(argv) {
  const args = parseArgs(argv);
  if (args.help) { console.log('usage: node tools/authored_reimport_app_mount.mjs --editor FILE --source FILE --out DIR --chrome PATH'); return; }
  for (const key of ['editor', 'source', 'out', 'chrome']) assert(args[key], `--${key} is required`);
  const editor = path.resolve(args.editor), sourcePath = path.resolve(args.source), outDir = path.resolve(args.out);
  assert(!fs.existsSync(outDir), `output directory already exists: ${outDir}`);
  const { html, template, originalMBtn } = templateFromEditor(editor);
  const authored = read(sourcePath).toString('utf8');
  const compiled = compileAuthored(authored, sourcePath);
  const adaptedMBtn = durableMBtnSource(originalMBtn);
  const originalCompiled = compileAuthored(`export ${adaptedMBtn}`, path.join(repo, 'experiment', 'app-mount', 'TemplateMBtn.tsx'));
  renderWith(originalCompiled, compiled);
  const patched = patchEditor(html, originalMBtn, compiled);
  const negative = patchEditor(html, originalMBtn, compiled, true);
  assert(negative.includes('height: 27'), 'planted negative control was not applied');
  const mutatedCompiled = compiled.replace("height: 26", "height: 27");
  assert(mutatedCompiled !== compiled, 'planted source mutation did not change compiled output');
  let negativeRejected = false;
  try { renderWith(originalCompiled, mutatedCompiled); } catch (error) {
    negativeRejected = /render tree diverges/.test(String(error.message));
  }
  assert(negativeRejected, 'planted MBtn height mutation was not detected before browser execution');
  const patchedPath = path.join(outDir, 'patched.html'); const baselinePath = path.join(outDir, 'baseline.html');
  write(baselinePath, html); write(patchedPath, patched);
  write(path.join(outDir, 'source.tsx'), authored); write(path.join(outDir, 'compiled.cjs'), compiled);
  const browser = await browserRun({ 'baseline.html': Buffer.from(html), 'patched.html': Buffer.from(patched) }, args.chrome, outDir);
  const receipt = {
    schema: SCHEMA, version: 1,
    source: { editor: path.basename(editor), editor_sha256: sha256(Buffer.from(html)), authored: path.basename(sourcePath), authored_sha256: sha256(Buffer.from(authored)), compiled_sha256: sha256(Buffer.from(compiled)) },
    checks: { tsx_compiled: true, node_render_tree_parity: true, app_mount: true, patched_mbtn_invoked: browser.results.patched.mbtn > 0, manager_dom_parity: browser.results.baseline.manager === browser.results.patched.manager, negative_control: { status: 'passed', mutation: 'height: 26 -> height: 27', detected_before_browser: negativeRejected } },
    browser,
    scope: { editor_html_unchanged: true, runtime_artifact_changed: false, full_native_parity: false, production_cutover: false },
  };
  write(path.join(outDir, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`);
  process.stdout.write(`${JSON.stringify(receipt, null, 2)}\n`);
}

main(process.argv.slice(2)).catch(error => { console.error(error.stack || error.message); process.exit(1); });
