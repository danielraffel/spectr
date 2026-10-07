#!/usr/bin/env node
/**
 * Exercise authored ContextMenu through the real Spectr App mount.
 *
 * The checked-in editor.html and materialized runtime remain untouched.  The
 * fixture replaces only the ContextMenu declaration in a copy of the unpacked
 * Claude template with the compiled authored module.  It proves the browser
 * event path (open, action, Escape, and outside-pointer dismissal), compares
 * the baseline and patched menu DOM, and rejects a planted interaction change
 * before Chromium is started.
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

const SCHEMA = 'spectr-authored-reimport-context-menu-app-mount-v1';

function fail(message) { throw new Error(`authored ContextMenu App mount failed: ${message}`); }
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
  assert(typeof template === 'string' && template.includes('function ContextMenu'), 'bundler template has no ContextMenu source');
  return { html, template, originalContextMenu: functionSlice(template, 'ContextMenu') };
}

function compileModule(source, name) {
  const result = ts.transpileModule(source, {
    fileName: `${name}.tsx`, reportDiagnostics: true,
    compilerOptions: {
      target: ts.ScriptTarget.ES2020,
      module: ts.ModuleKind.CommonJS,
      jsx: ts.JsxEmit.React,
      jsxFactory: 'React.createElement',
      jsxFragmentFactory: 'React.Fragment',
    },
  });
  const errors = (result.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length, `${name} diagnostics: ${errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
  assert(result.outputText.includes(`exports.${name}`), `${name} did not emit an explicit export`);
  return result.outputText;
}

function reactMock() {
  return {
    createElement(type, props, ...children) {
      return { type, props: { ...(props || {}), ...(children.length ? { children } : {}) } };
    },
    Fragment: Symbol('Fragment'),
    useRef: initial => ({ current: initial }),
    useEffect: () => {},
  };
}

function context() {
  const listeners = new Map();
  const document = {
    getElementById(id) { return id === 'root' ? { clientWidth: 1320, clientHeight: 860 } : null; },
    addEventListener(type, fn) { listeners.set(`document:${type}`, fn); },
    removeEventListener(type) { listeners.delete(`document:${type}`); },
  };
  const window = {
    innerWidth: 1320,
    innerHeight: 860,
    addEventListener(type, fn) { listeners.set(`window:${type}`, fn); },
    removeEventListener(type) { listeners.delete(`window:${type}`); },
  };
  return { React: reactMock(), window, document, setTimeout: () => 1, clearTimeout: () => {}, globalThis: null };
}

function runCommonJs(code, globals) {
  const module = { exports: {} };
  vm.runInNewContext(code, { ...globals, module, exports: module.exports });
  return module.exports;
}

function loadContextMenu(code, globals) {
  const module = runCommonJs(code, globals);
  assert(typeof module.ContextMenu === 'function', 'ContextMenu export missing');
  return module.ContextMenu;
}

function expand(value, seen = new Set()) {
  if (value === null || value === undefined || typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') return value;
  if (Array.isArray(value)) return value.map(child => expand(child, seen));
  if (typeof value !== 'object') return value;
  if (typeof value.type === 'function') {
    assert(!seen.has(value.type), `component recursion in ${value.type.name}`);
    const next = new Set(seen); next.add(value.type);
    return expand(value.type(value.props || {}), next);
  }
  const props = {};
  for (const [key, item] of Object.entries(value.props || {})) props[key] = key === 'children' ? expand(item, seen) : item;
  return { type: typeof value.type === 'symbol' ? '[fragment]' : value.type, props };
}

function normalize(value) {
  if (typeof value === 'function') return '[function]';
  if (Array.isArray(value)) return value.map(normalize);
  if (value && typeof value === 'object') {
    const output = {};
    for (const key of Object.keys(value).sort()) output[key] = normalize(value[key]);
    return output;
  }
  return value;
}

function menuProps(calls) {
  return {
    x: 1300, y: 850, band: 4, N: 32, selection: new Set([3, 4, 5]), editMode: 'level',
    onClose: () => calls.push('close'),
    onEditMode: mode => calls.push(`mode:${mode}`),
    onMuteBand: band => calls.push(`mute:${band}`),
    onZeroBand: band => calls.push(`zero:${band}`),
    onSoloBand: band => calls.push(`solo:${band}`),
    onSelectAround: (band, radius) => calls.push(`select:${band}:${radius}`),
    onClearSel: () => calls.push('clear-selection'),
    onZeroSel: () => calls.push('zero-selection'),
    onMuteSel: () => calls.push('mute-selection'),
    onFitView: () => calls.push('fit-view'),
  };
}

function textOf(node) {
  if (node === null || node === undefined) return '';
  if (typeof node === 'string' || typeof node === 'number') return String(node);
  if (Array.isArray(node)) return node.map(textOf).join('');
  return textOf(node.props?.children);
}

function buttons(node, result = []) {
  if (!node || typeof node !== 'object') return result;
  if (node.type === 'button') result.push(node);
  if (Array.isArray(node)) node.forEach(child => buttons(child, result));
  else buttons(node.props?.children, result);
  return result;
}

function nodeRenderAndInteraction(originalCode, authoredCode) {
  const originalCalls = [];
  const authoredCalls = [];
  const original = expand(loadContextMenu(originalCode, context())(menuProps(originalCalls)));
  const authored = expand(loadContextMenu(authoredCode, context())(menuProps(authoredCalls)));
  assert(JSON.stringify(normalize(original)) === JSON.stringify(normalize(authored)), 'authored ContextMenu render tree diverges from template');
  const labels = buttons(authored).map(button => textOf(button).trim());
  const semanticLabels = labels.map(label => label.replace(/^[●\s]+/, '').trim());
  for (const label of ['Mute / Unmute', 'Reset to 0 dB', 'Sculpt', 'Fit full range']) assert(semanticLabels.some(value => value === label || value.startsWith(label)), `authored menu is missing ${label}`);
  const mute = buttons(authored).find(button => textOf(button).trim() === 'Mute / Unmute');
  assert(mute && typeof mute.props.onClick === 'function', 'Mute / Unmute interaction is not callable');
  mute.props.onClick();
  assert(authoredCalls.join('|') === 'mute:4|close', `authored Mute / Unmute action sequence was ${authoredCalls.join('|')}`);
  const originalMute = buttons(original).find(button => textOf(button).trim() === 'Mute / Unmute');
  originalMute.props.onClick();
  assert(originalCalls.join('|') === 'mute:4|close', `template Mute / Unmute action sequence was ${originalCalls.join('|')}`);
  return { labels, semantic_labels: semanticLabels, button_count: labels.length, action_sequence: authoredCalls };
}

function replaceContextSource(source, needle, replacement, label) {
  const count = source.split(needle).length - 1;
  assert(count === 1, `ContextMenu transform ${label} matched ${count} times`);
  return source.replace(needle, replacement);
}

function transformContextMenuSource(original) {
  let source = original;
  source = replaceContextSource(source,
    String.raw`    const onDown = (e) => { if (ref.current && !ref.current.contains(e.target)) onClose(); };
    const onKey = (e) => { if (e.key === 'Escape') onClose(); };
    // Schedule so the click that opened it doesn't immediately close it.
    const t = setTimeout(() => document.addEventListener('pointerdown', onDown), 0);
    window.addEventListener('keydown', onKey);
    return () => { clearTimeout(t); document.removeEventListener('pointerdown', onDown); window.removeEventListener('keydown', onKey); };`,
    String.raw`    const consume = event => {
      event.preventDefault();
      event.stopPropagation();
      if (event.stopImmediatePropagation) event.stopImmediatePropagation();
    };
    const armOutsideActivationShield = () => {
      const types = ['mousedown', 'pointerup', 'mouseup', 'click'];
      let timer = 0;
      const release = () => {
        clearTimeout(timer);
        for (const type of types) document.removeEventListener(type, shield, true);
      };
      const shield = event => {
        consume(event);
        if (event.type === 'click') release();
      };
      for (const type of types) document.addEventListener(type, shield, true);
      timer = setTimeout(release, 1000);
    };
    const onDown = event => {
      if (!ref.current || ref.current.contains(event.target)) return;
      consume(event);
      armOutsideActivationShield();
      onClose();
    };
    const onKey = event => {
      if (event.key !== 'Escape') return;
      consume(event);
      onClose();
    };
    // Schedule so the click that opened it doesn't immediately close it.
    const t = setTimeout(() => document.addEventListener('pointerdown', onDown, true), 0);
    window.addEventListener('keydown', onKey, true);
    return () => {
      clearTimeout(t);
      document.removeEventListener('pointerdown', onDown, true);
      window.removeEventListener('keydown', onKey, true);
    };`,
    'dismissal ownership');
  source = replaceContextSource(source, '    <div ref={ref}\n      style={{', '    <div ref={ref} data-spectr-overlay="true" data-spectr-band-context-menu="true" role="menu" aria-label="Band actions"\n      style={{', 'root overlay semantics');
  source = replaceContextSource(source, '  const Item = ({ label, hint, onClick, disabled, danger, sub }) => (\n    <button\n      onClick=', '  const Item = ({ action, label, hint, onClick, disabled, danger, sub }) => (\n    <button role="menuitem" data-spectr-band-action={action}\n      onClick=', 'item semantics');
  source = replaceContextSource(source, '<Item label="Mute / Unmute" onClick={() => onMuteBand(band)} />', '<Item action="mute-band" label="Mute / Unmute" onClick={() => onMuteBand(band)} />', 'mute action selector');
  source = replaceContextSource(source, '<Item label="Reset to 0 dB" onClick={() => onZeroBand(band)} />', '<Item action="reset-band" label="Reset to 0 dB" onClick={() => onZeroBand(band)} />', 'reset action selector');
  source = replaceContextSource(source, '<Divider label={`Band ${band + 1}`} />', '<Divider label={`BAND ${band + 1}`} />', 'band divider text');
  source = replaceContextSource(source, '<Divider label="Edit Mode" />', '<Divider label="EDIT MODE" />', 'edit divider text');
  source = replaceContextSource(source, '<Divider label="View" />', '<Divider label="VIEW" />', 'view divider text');
  source = replaceContextSource(source, 'const vw = window.innerWidth, vh = window.innerHeight;', "const root = document.getElementById('root');\n  const vw = root ? root.clientWidth : window.innerWidth, vh = root ? root.clientHeight : window.innerHeight;", 'viewport contract');
  for (const [from, to, label] of [['hint: \'S\'', "hint: '1'", 'Sculpt hint'], ['hint: \'L\'', "hint: '2'", 'Level hint'], ['hint: \'B\'', "hint: '3'", 'Boost hint'], ['hint: \'F\'', "hint: '4'", 'Flare hint'], ['hint: \'G\'', "hint: '5'", 'Glide hint']]) source = replaceContextSource(source, from, to, label);
  return source;
}

function patchEditor(editorHtml, originalContextMenu, compiled, mutation = false) {
  const moduleCode = mutation ? compiled.replace('label: "Mute / Unmute"', 'label: "Mute only"') : compiled;
  assert(moduleCode !== compiled || !mutation, 'planted ContextMenu mutation did not change compiled output');
  const wrapper = `function __wp1ReimportedContextMenuModule() {\n  const module = { exports: {} };\n  const exports = module.exports;\n  ${moduleCode}\n  return module.exports.ContextMenu;\n}\nconst __wp1ReimportedContextMenu = __wp1ReimportedContextMenuModule();\nwindow.__wp1ContextMenuReimported = true;\nwindow.__wp1ContextMenuRenderCount = 0;\nfunction ContextMenu(props) { window.__wp1ContextMenuRenderCount += 1; return __wp1ReimportedContextMenu(props); }`;
  // Earlier bootstrap transforms rewrite ContextMenu before this injection.
  // Use exact declaration/end delimiters and keep both count guards fail closed.
  const startMarker = JSON.stringify('function ContextMenu(');
  const endMarker = JSON.stringify('\nwindow.ContextMenu = ContextMenu;');
  const replacement = JSON.stringify(wrapper);
  const injection = `const __contextStartMarker = ${startMarker};\n    const __contextEndMarker = ${endMarker};\n    const __contextStart = template.indexOf(__contextStartMarker);\n    const __contextStartCount = template.split(__contextStartMarker).length - 1;\n    const __contextEnd = __contextStart < 0 ? -1 : template.indexOf(__contextEndMarker, __contextStart);\n    const __contextEndCount = template.split(__contextEndMarker).length - 1;\n    if (__contextStartCount !== 1 || __contextEndCount !== 1 || __contextEnd < __contextStart) throw new Error('ContextMenu source replacement delimiter mismatch: start_matches=' + __contextStartCount + ' end_matches=' + __contextEndCount + ' start=' + __contextStart + ' end=' + __contextEnd);\n    window.__wp1ContextMenuTransformedSource = template.slice(__contextStart, __contextEnd + __contextEndMarker.length);\n    template = template.slice(0, __contextStart) + ${replacement} + template.slice(__contextEnd);`;
  const needle = '    // Inject after <head> so the DOCTYPE stays first; prepending the script';
  assert(editorHtml.includes(needle), 'editor bootstrap template assignment is missing');
  return editorHtml.replace(needle, `${needle}\n    ${injection}`);
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
  const debugPort = await reservePort();
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-context-menu-app-mount-'));
  let child; let socket; let stderr = ''; let nextId = 1; const pending = new Map();
  try {
    child = spawn(path.resolve(chrome), [
      '--headless=new', '--disable-gpu', '--disable-background-networking', '--disable-component-update',
      '--disable-sync', '--no-first-run', '--no-default-browser-check', '--remote-debugging-address=127.0.0.1',
      `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`, '--window-size=1320,860', 'about:blank',
    ], { stdio: ['ignore', 'ignore', 'pipe'] });
    child.stderr.on('data', chunk => { stderr += chunk; });
    let websocketUrl; const deadline = Date.now() + 15000;
    while (!websocketUrl && Date.now() < deadline) {
      try { const pages = await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json(); websocketUrl = pages.find(page => page.type === 'page')?.webSocketDebuggerUrl; } catch {}
      if (!websocketUrl) await new Promise(resolve => setTimeout(resolve, 50));
    }
    assert(websocketUrl, `Chrome DevTools endpoint did not start: ${stderr.trim()}`);
    socket = new WebSocket(websocketUrl);
    await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
    socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (!message.id || !pending.has(message.id)) return;
      const waiter = pending.get(message.id); pending.delete(message.id);
      message.error ? waiter.reject(new Error(JSON.stringify(message.error))) : waiter.resolve(message.result);
    });
    const command = (method, params = {}) => new Promise((resolve, reject) => { const id = nextId++; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); });
    const evaluate = async expression => {
      const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
      if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text || 'browser evaluation failed');
      return result.result.value;
    };
    const waitFor = async (expression, timeout = 30000) => {
      const until = Date.now() + timeout;
      while (Date.now() < until) {
        if (await evaluate(expression)) return true;
        await new Promise(resolve => setTimeout(resolve, 100));
      }
      return false;
    };
    await command('Page.enable'); await command('Runtime.enable');
    const results = {};
    for (const name of ['patched', 'baseline']) {
      const url = `http://127.0.0.1:${server.address().port}/${name}.html`;
      await command('Page.navigate', { url });
      const ready = await waitFor(`location.href === ${JSON.stringify(url)} && document.readyState === 'complete' && !!document.querySelector('[data-spectr-bank-ready="true"]')`);
      if (!ready) {
        const diagnostic = await evaluate(`({ error: document.getElementById('__bundler_err')?.textContent || '', body: document.body?.innerText?.slice(-1800) || '', consoleErrors: window.__wp1BrowserErrors || [] })`);
        fail(`${name} App did not become ready; diagnostic=${JSON.stringify(diagnostic)}`);
      }
      assert(await evaluate(`!!document.querySelector('[data-spectr-filter-surface]')`), `${name} filter surface is missing`);
      const open = await evaluate(`(() => { const target = document.querySelector('[data-spectr-filter-surface]'); const event = new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 640, clientY: 420 }); target.dispatchEvent(event); return event.defaultPrevented || true; })()`);
      assert(open, `${name} contextmenu dispatch failed`);
      const menuVisible = `Array.from(document.querySelectorAll('div')).some(x => x.style.position === 'fixed' && x.textContent.includes('Fit full range'))`;
      assert(await waitFor(menuVisible, 10000), `${name} ContextMenu did not open`);
      const menuSummary = await evaluate(`(() => { const menu = Array.from(document.querySelectorAll('div')).find(x => x.style.position === 'fixed' && x.textContent.includes('Fit full range')); const rect = menu?.getBoundingClientRect(); const buttons = Array.from(menu?.querySelectorAll('button') || []).map(x => x.textContent.trim()); const html = menu?.outerHTML || ''; return { html, normalized_html: html.replace(/ data-spectr-context-menu=""/, ''), text: menu?.innerText || '', buttons, semantic_buttons: buttons.map(x => x.replace(/^[●\\s]+/, '').trim()), renderCount: window.__wp1ContextMenuRenderCount || 0, marker: !!menu?.matches('[data-spectr-context-menu]'), clamped: !!rect && rect.right <= innerWidth && rect.bottom <= innerHeight, left: rect?.left ?? null, top: rect?.top ?? null }; })()`);
      const transformedSource = await evaluate('window.__wp1ContextMenuTransformedSource || ""');
      if (transformedSource) write(path.join(outDir, 'transformed-context-menu.js'), transformedSource);
      for (const label of ['Mute / Unmute', 'Reset to 0 dB', 'Sculpt', 'Fit full range']) assert(menuSummary.semantic_buttons.some(value => value === label || value.startsWith(label)), `${name} menu is missing ${label}`);
      assert(menuSummary.clamped, `${name} menu escaped viewport`);
      assert(await evaluate(`(() => { const menu = Array.from(document.querySelectorAll('div')).find(x => x.style.position === 'fixed' && x.textContent.includes('Fit full range')); const button = Array.from(menu?.querySelectorAll('button') || []).find(x => x.textContent.trim() === 'Mute / Unmute'); if (!button) return false; button.click(); return true; })()`), `${name} Mute / Unmute button is missing`);
      assert(await waitFor(`!${menuVisible}`, 5000), `${name} Mute / Unmute did not dismiss menu`);
      assert(await evaluate(`(() => { const target = document.querySelector('[data-spectr-filter-surface]'); target.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 640, clientY: 420 })); return true; })()`), `${name} menu reopen failed`);
      assert(await waitFor(menuVisible, 5000), `${name} menu did not reopen for Escape`);
      await evaluate(`window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))`);
      assert(await waitFor(`!${menuVisible}`, 5000), `${name} Escape did not dismiss menu`);
      assert(await evaluate(`(() => { const target = document.querySelector('[data-spectr-filter-surface]'); target.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: 640, clientY: 420 })); return true; })()`), `${name} menu reopen outside failed`);
      assert(await waitFor(menuVisible, 5000), `${name} menu did not reopen for outside dismissal`);
      await new Promise(resolve => setTimeout(resolve, 80));
      await evaluate(`document.body.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 2, clientY: 2 }))`);
      assert(await waitFor(`!${menuVisible}`, 5000), `${name} outside pointer did not dismiss menu`);
      const screenshot = await command('Page.captureScreenshot', { format: 'png' });
      const bytes = Buffer.from(screenshot.data, 'base64'); const screenshotPath = path.join(outDir, `${name}.png`); write(screenshotPath, bytes);
      results[name] = { ...menuSummary, screenshot: { path: screenshotPath, sha256: sha256(bytes) }, interactions: { opened: true, mute_dismissed: true, escape_dismissed: true, outside_dismissed: true } };
    }
    assert(results.patched.renderCount > 0, 'authored ContextMenu was never invoked by mounted App');
    if (results.baseline.normalized_html !== results.patched.normalized_html) {
      const a = results.baseline.normalized_html;
      const b = results.patched.normalized_html;
      let i = 0; while (i < a.length && i < b.length && a[i] === b[i]) i += 1;
      fail(`App menu DOM changed after authored ContextMenu re-import: first_diff=${i} baseline_len=${a.length} patched_len=${b.length} baseline=${JSON.stringify(a.slice(Math.max(0, i - 120), i + 240))} patched=${JSON.stringify(b.slice(Math.max(0, i - 120), i + 240))}`);
    }
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
  if (args.help) { console.log('usage: node tools/authored_reimport_context_menu_app_mount.mjs --editor FILE --source FILE --out DIR --chrome PATH'); return; }
  for (const key of ['editor', 'source', 'out', 'chrome']) assert(args[key], `--${key} is required`);
  const editor = path.resolve(args.editor); const sourcePath = path.resolve(args.source); const outDir = path.resolve(args.out);
  assert(!fs.existsSync(outDir), `output directory already exists: ${outDir}`);
  const { html, originalContextMenu } = templateFromEditor(editor);
  const authored = read(sourcePath).toString('utf8');
  const compiled = compileModule(authored, 'ContextMenu');
  const transformedOriginal = transformContextMenuSource(originalContextMenu);
  const originalCompiled = compileModule(`export ${transformedOriginal}`, 'ContextMenu');
  const interaction = nodeRenderAndInteraction(originalCompiled, compiled);
  const patched = patchEditor(html, originalContextMenu, compiled);
  const negative = patchEditor(html, originalContextMenu, compiled, true);
  assert(negative.includes('Mute only'), 'planted negative control was not applied');
  const mutatedCompiled = compiled.replace('label: "Mute / Unmute"', 'label: "Mute only"');
  assert(mutatedCompiled !== compiled, 'planted source mutation did not change compiled output');
  let negativeRejected = false;
  try { nodeRenderAndInteraction(originalCompiled, mutatedCompiled); } catch (error) { negativeRejected = /render tree diverges|missing Mute \/ Unmute|action sequence/.test(String(error.message)); }
  assert(negativeRejected, 'planted ContextMenu interaction mutation was not detected before browser execution');
  write(path.join(outDir, 'baseline.html'), html); write(path.join(outDir, 'patched.html'), patched);
  write(path.join(outDir, 'source.tsx'), authored); write(path.join(outDir, 'compiled.cjs'), compiled);
  const browser = await browserRun({ 'baseline.html': Buffer.from(html), 'patched.html': Buffer.from(patched) }, args.chrome, outDir);
  const receipt = {
    schema: SCHEMA, version: 1,
    source: { editor: path.basename(editor), editor_sha256: sha256(Buffer.from(html)), authored: path.basename(sourcePath), authored_sha256: sha256(Buffer.from(authored)), compiled_sha256: sha256(Buffer.from(compiled)) },
    checks: {
      tsx_compiled: true, node_render_tree_parity: true, node_interaction_parity: true,
      app_mount: true, patched_context_menu_invoked: browser.results.patched.renderCount > 0,
      menu_dom_parity: browser.results.baseline.normalized_html === browser.results.patched.normalized_html,
      open_action_escape_outside: true,
      negative_control: { status: 'passed', mutation: 'Mute / Unmute -> Mute only', detected_before_browser: negativeRejected },
    },
    interaction,
    browser,
    scope: { editor_html_unchanged: true, runtime_artifact_changed: false, full_native_parity: false, production_cutover: false },
  };
  write(path.join(outDir, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`);
  process.stdout.write(`${JSON.stringify(receipt, null, 2)}\n`);
}

main(process.argv.slice(2)).catch(error => { console.error(error.stack || error.message); process.exit(1); });
