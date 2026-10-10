#!/usr/bin/env node
/**
 * Staging proof for replacing the materialized App declaration with the
 * current authored App module.  The checked-in runtime is never modified.
 * This deliberately uses the materialized document as the baseline because
 * the older Claude editor template does not contain the complete helper
 * closure required by today's App (for example SpectrControlMenu).
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import zlib from 'node:zlib';

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(ROOT, '..');
const SCHEMA = 'spectr-authored-reimport-full-app-materialized-browser-v1';
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const TSC = path.join(REPO, 'tools', 'wp1-parser', 'node_modules', 'typescript');
const CHROME_WINDOW_SIZE = process.env.SPECTR_BROWSER_WINDOW_SIZE || '1320,860';
const EXPECTED_CANVAS_WIDTH = Number(process.env.SPECTR_EXPECTED_CANVAS_WIDTH || 1320);
const EXPECTED_CANVAS_HEIGHT = Number(process.env.SPECTR_EXPECTED_CANVAS_HEIGHT || 773);
let ts;
try { ts = createRequire(import.meta.url)(TSC); }
catch (error) { fail(`pinned TypeScript is missing at ${TSC}; run npm ci --ignore-scripts --prefix tools/wp1-parser (${error.message})`); }

function fail(message) { throw new Error(`authored full-App materialized browser proof failed: ${message}`); }
function assert(value, message) { if (!value) fail(message); }
function read(file) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${file}: ${error.message}`); } }
function write(file, value) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, value); }
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function classifyChromeStderr(stderr) {
  const lines = stderr.split('\n').map(line => line.trim()).filter(Boolean);
  const known = lines.filter(line => line.includes('CVDisplayLinkCreateWithCGDisplay failed')
    || line.includes('Trying to load the allocator multiple times'));
  const unclassified = lines.filter(line => !line.includes('DevTools listening on ')
    && !line.includes('CVDisplayLinkCreateWithCGDisplay failed')
    && !line.includes('Trying to load the allocator multiple times'));
  return { lines, known_nonfatal: known, unclassified };
}
function prepareOutputDirectory(outDir) {
  const markerPath = path.join(outDir, '.harness-schema');
  if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });
  let owned = false;
  if (fs.existsSync(markerPath)) {
    try { owned = fs.readFileSync(markerPath, 'utf8').trim() === SCHEMA; }
    catch (error) { fail(`existing output directory has an unreadable harness marker: ${error.message}`); }
  }
  const receiptPath = path.join(outDir, 'receipt.json');
  if (!owned && !fs.existsSync(receiptPath)) {
    assert(fs.readdirSync(outDir).length === 0,
      `output directory already exists without a prior receipt or harness marker: ${outDir}`);
    owned = true;
  }
  if (!owned && fs.existsSync(receiptPath)) {
    let receipt;
    try { receipt = JSON.parse(fs.readFileSync(receiptPath, 'utf8')); }
    catch (error) { fail(`existing output directory has an invalid receipt: ${error.message}`); }
    owned = receipt.schema === SCHEMA;
  }
  assert(owned, `refusing to clear an output directory from another harness: ${outDir}`);
  for (const entry of fs.readdirSync(outDir))
    if (entry !== '.harness-schema') fs.rmSync(path.join(outDir, entry), { recursive: true, force: true });
  write(markerPath, `${SCHEMA}\n`);
}
function runNode(script, args, label) {
  const result = spawnSync(process.execPath, [script, ...args], { cwd: REPO, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  if (result.status !== 0) fail(`${label}: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
  return result;
}
function parseArgs(argv) {
  const out = {};
  for (let i = 0; i < argv.length; i += 1) {
    if (['artifact', 'chrome', 'out'].some(key => argv[i] === `--${key}`)) out[argv[i].slice(2)] = argv[++i];
    else if (argv[i] === '--help') out.help = true;
    else fail(`unknown argument ${argv[i]}`);
  }
  return out;
}
function sliceFunction(source, name) {
  const start = source.lastIndexOf(`function ${name}(`);
  assert(start >= 0, `${name} declaration is missing`);
  const open = source.indexOf('{', source.indexOf(')', start));
  assert(open >= 0, `${name} body is missing`);
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
function stage(artifact) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-authored-full-app-materialized-'));
  try {
    const manifest = path.join(dir, 'manifest.json'); const emission = path.join(dir, 'emitted');
    runNode(path.join(ROOT, 'wp1_dependency_manifest.mjs'), ['--artifact', artifact, '--root', 'App', '--out', manifest], 'dependency manifest');
    runNode(path.join(ROOT, 'wp1_authored_module_emitter.mjs'), ['--artifact', artifact, '--manifest', manifest, '--out', emission], 'authored module emission');
    const data = JSON.parse(read(path.join(emission, 'authored-modules.manifest.json')));
    const app = data.modules.find(item => item.name === 'App');
    assert(app, 'emitted closure has no App module');
    return { dir, manifest, emission, app, source: path.join(emission, 'components', 'App.tsx') };
  } catch (error) {
    fs.rmSync(dir, { recursive: true, force: true });
    throw error;
  }
}
function compile(sourcePath) {
  const source = read(sourcePath).toString('utf8');
  const result = ts.transpileModule(`${source}\nexport { App };\n`, {
    fileName: sourcePath, reportDiagnostics: true,
    compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.React, jsxFactory: 'React.createElement', jsxFragmentFactory: 'React.Fragment' },
  });
  const errors = (result.diagnostics || []).filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length, errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; '));
  assert(result.outputText.includes('exports.App'), 'compiled App has no explicit export');
  return { source, compiled: result.outputText };
}
function patchApp(html, compiled) {
  const original = sliceFunction(html, 'App');
  const wrapper = `function App() {\n  globalThis.__spectrAuthoredAppInvocations = (globalThis.__spectrAuthoredAppInvocations || 0) + 1;\n  const module = { exports: {} };\n  const exports = module.exports;\n  ${compiled}\n  return module.exports.App.apply(this, arguments);\n}`;
  const at = html.indexOf(original);
  assert(at >= 0 && html.indexOf(original, at + 1) < 0, 'materialized App declaration is missing or duplicated');
  return html.slice(0, at) + wrapper + html.slice(at + original.length);
}
function mutatePatchedApp(html) {
  const token = 'return module.exports.App.apply(this, arguments);';
  assert(html.includes(token), 'authored App invocation marker is missing from patched App');
  return html.replace(token,
    "const rendered = module.exports.App.apply(this, arguments); return React.createElement(React.Fragment, null, rendered, React.createElement('div', { 'data-spectr-negative-app': 'true', style: { display: 'none' } }));");
}
function state(n = process.env.SPECTR_BROWSER_NATIVE_DEFAULTS === '1' ? 32 : 64) {
  const nativeDefaults = process.env.SPECTR_BROWSER_NATIVE_DEFAULTS === '1';
  return { revision: 0, n_visible: n,
    gain_db: nativeDefaults ? Array(n).fill(0) : Array.from({ length: n }, (_, i) => [12, 6, 0, -6][i % 4]),
    muted: Array(n).fill(false), min_hz: 20, max_hz: 20000, motion_mode: 0,
    analyzer_mode: nativeDefaults ? 0 : 2, edit_mode: 0, visualization_mode: 2,
    snapshots: { A: { populated: false, gain_db: [], muted: [] }, B: { populated: false, gain_db: [], muted: [] } },
    patterns_json: JSON.stringify({ format: 'spectr.patterns', version: 1, default_id: 'factory:flat', patterns: [] }) };
}
function bridgeScript() {
  const native = state();
  const frame = (sequence, phase) => {
    const trace = count => Array.from({ length: count }, (_, i) =>
      -92 + 78 * Math.exp(-Math.pow((i / (count - 1) - (0.25 + phase * 0.1)) / 0.06, 2)));
    return { schema_version: 1, epoch: 1, sequence_number: sequence, dropped_frames: 0, source_channels: 2, fft_size: 1024, sample_rate: 48000, floor_db: -96, ceiling_db: 0, visible: { min_hz: 20, max_hz: 20000, magnitude_db: trace(321) }, overview: { min_hz: 20, max_hz: 20000, magnitude_db: trace(121) } };
  };
  return `<script>\nwindow.__spectrHandlers = Object.create(null); window.__spectrRuntimeErrors = []; window.__spectrAnalyzerEmissions = 0; window.__spectrAuthoredAppInvocations = 0; window.__spectrFrameOne = ${JSON.stringify(frame(1, 0))}; window.__spectrFrameTwo = ${JSON.stringify(frame(2, 1))}; window.addEventListener('error', e => window.__spectrRuntimeErrors.push(String(e.message || e.type))); window.addEventListener('unhandledrejection', e => window.__spectrRuntimeErrors.push(String(e.reason || 'unhandled rejection'))); window.requestAnimationFrame = cb => setTimeout(() => cb(performance.now()), 16); window.cancelAnimationFrame = id => clearTimeout(id); Element.prototype.setPointerCapture = () => {}; Element.prototype.releasePointerCapture = () => {}; const state = ${JSON.stringify(native)}; const clone = v => JSON.parse(JSON.stringify(v)); const buildInfo = { ok: true, product_version: '1.0.7-staging', sdk_version: 'pinned-staging', sdk_sha: 'authored-reimport', build_type: 'staging', build_time: '2026-10-09T00:00:00Z' }; window.__spectrEmit = (type, payload) => { if (type === 'analyzer_frame') window.__spectrAnalyzerEmissions++; if (type === 'analyzer_frame' && typeof window.__spectrPublishNativeMessage === 'function') window.__spectrPublishNativeMessage(type, payload); for (const cb of window.__spectrHandlers[type] || []) cb({ type, payload: clone(payload) }); }; window.pulp = { initial: type => type === 'processing_state_get' ? clone(state) : (type === 'build_info_get' ? buildInfo : null), on: (type, cb) => { (window.__spectrHandlers[type] ||= new Set()).add(cb); return () => window.__spectrHandlers[type].delete(cb); }, postMessage: (type, payload) => { if (type === 'editor_ready') { setTimeout(() => window.__spectrEmit('processing_state_hydrate', state), 0); setTimeout(() => window.__spectrEmit('analyzer_frame', window.__spectrFrameOne), 120); setTimeout(() => window.__spectrEmit('analyzer_frame', window.__spectrFrameTwo), 420); } if (type === 'build_info_get') return Promise.resolve({ ok: true, payload: buildInfo }); return Promise.resolve({ ok: true, payload: { ok: true } }); } };\n</script>`;
}
function reactVendorScripts() {
  const editor = read(path.join(REPO, 'resources', 'editor.html')).toString('utf8');
  const match = editor.match(/<script\s+type="__bundler\/manifest">([\s\S]*?)<\/script>/i);
  assert(match, 'editor vendor manifest is missing');
  const manifest = JSON.parse(match[1]);
  const vendor = Object.values(manifest).filter(entry => entry.mime === 'text/javascript').slice(0, 2);
  assert(vendor.length === 2, 'editor vendor manifest has no React/ReactDOM pair');
  return vendor.map(entry => {
    let bytes = Buffer.from(entry.data, 'base64');
    if (entry.compressed) bytes = zlib.gunzipSync(bytes);
    return `<script>${bytes.toString('utf8').replace(/<(?=\/?script\b)/gi, '\\x3c')}</script>`;
  }).join('');
}
function instrumentScript() { return `<script>window.__spectrCanvasSummary = () => Array.from(document.querySelectorAll('canvas')).map(c => { try { const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let ink = 0; for (let i = 0; i < d.length; i += 4) if (d[i + 3] > 8 && d[i] + d[i + 1] + d[i + 2] > 24) ink++; return { width: c.width, height: c.height, ink }; } catch { return { width: c.width, height: c.height, ink: 0 }; } });</script>`; }
function injected(html) {
  // React's vendor source contains `$'`/`$`` spellings.  A string replacement
  // would interpret those as replacement tokens and duplicate the remainder
  // of the document into the vendor script, producing a misleading blank
  // mount.  A function replacement preserves the bytes exactly.
  return html.replace('</script>', () => `</script>${reactVendorScripts()}${bridgeScript()}${instrumentScript()}`);
}
function reservePort() { return new Promise((resolve, reject) => { const server = net.createServer(); server.once('error', reject); server.listen(0, '127.0.0.1', () => { const port = server.address().port; server.close(() => resolve(port)); }); }); }
async function browser(files, chrome, outDir, assets = new Map()) {
  const server = http.createServer((req, res) => {
    const key = new URL(req.url, 'http://127.0.0.1').pathname.slice(1);
    const body = files[key] || assets.get(key)?.body;
    if (!body) { res.writeHead(404); res.end(); return; }
    const contentType = files[key] ? 'text/html; charset=utf-8' : (assets.get(key)?.mime || 'application/octet-stream');
    res.writeHead(200, { 'content-type': contentType, 'content-length': body.length });
    res.end(body);
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const debugPort = await reservePort(); const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-authored-full-app-materialized-chrome-')); let child; let socket; let nextId = 1; let stderr = ''; const pending = new Map(); const errors = [];
  try {
    child = spawn(path.resolve(chrome), ['--headless=new', '--disable-gpu', '--disable-background-networking', '--disable-component-update', '--disable-sync', '--no-first-run', '--no-default-browser-check', '--remote-debugging-address=127.0.0.1', `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`, `--window-size=${CHROME_WINDOW_SIZE}`, 'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] }); child.stderr.on('data', c => { stderr += c; });
    let ws; const deadline = Date.now() + 15000; while (!ws && Date.now() < deadline) { try { ws = (await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json()).find(p => p.type === 'page')?.webSocketDebuggerUrl; } catch {} if (!ws) await new Promise(r => setTimeout(r, 50)); } assert(ws, `Chrome did not start: ${stderr}`);
    const networkFailures = [];
    const requestUrls = new Map();
    socket = new WebSocket(ws); await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); }); socket.addEventListener('message', event => { const msg = JSON.parse(event.data); if (msg.method === 'Runtime.exceptionThrown') errors.push(msg.params.exceptionDetails?.text || 'exception'); if (msg.method === 'Runtime.consoleAPICalled' && ['error', 'assert'].includes(msg.params.type)) { const text = (msg.params.args || []).map(arg => arg.value ?? arg.description ?? '').join(' '); if (!text.startsWith('Warning:')) errors.push(`console.${msg.params.type}:${text}`); } if (msg.method === 'Network.requestWillBeSent') requestUrls.set(msg.params.requestId, msg.params.request.url); if (msg.method === 'Network.responseReceived' && msg.params.response.status >= 400 && !msg.params.response.url.endsWith('/favicon.ico')) networkFailures.push({ kind: 'http', status: msg.params.response.status, url: msg.params.response.url }); if (msg.method === 'Network.loadingFailed' && !String(requestUrls.get(msg.params.requestId) || '').endsWith('/favicon.ico')) networkFailures.push({ kind: 'load', error: msg.params.errorText, url: requestUrls.get(msg.params.requestId) || '' }); if (!msg.id || !pending.has(msg.id)) return; const waiter = pending.get(msg.id); pending.delete(msg.id); msg.error ? waiter.reject(new Error(JSON.stringify(msg.error))) : waiter.resolve(msg.result); });
    const command = (method, params = {}) => new Promise((resolve, reject) => { const id = nextId++; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); }); const evaluate = async expr => { const result = await command('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text || 'evaluation failed'); return result.result.value; }; await command('Page.enable'); await command('Runtime.enable'); await command('Network.enable');
    const results = {};
    for (const name of Object.keys(files)) {
      errors.length = 0;
      networkFailures.length = 0;
      const url = `http://127.0.0.1:${server.address().port}/${name}`;
      await command('Page.navigate', { url });
      const readyDeadline = Date.now() + 45000;
      while (Date.now() < readyDeadline && !(await evaluate(
        `location.href === ${JSON.stringify(url)} && document.readyState === 'complete' && document.querySelector('#root')?.children.length > 0`)))
        await new Promise(r => setTimeout(r, 100));
      const ready = await evaluate(
        `location.href === ${JSON.stringify(url)} && document.querySelector('#root')?.children.length > 0`);
      if (!ready) {
        const diagnostic = await evaluate(`({ body: document.body?.innerText?.slice(-1400) || '', root: document.querySelector('#root')?.outerHTML?.slice(0, 700) || '', error: document.getElementById('__bundler_err')?.textContent || '', runtime: window.__spectrRuntimeErrors || [], title: document.title })`);
        fail(`${name} did not mount the root: ${JSON.stringify(diagnostic)}`);
      }
      await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
      const firstSequence = await evaluate(`new Promise((resolve, reject) => {
        const deadline = Date.now() + 5000;
        const poll = () => {
          const frame = window.SpectrAnalyzer?.debugSnapshot?.();
          if (frame && frame.sequence_number >= 1) { resolve(frame.sequence_number); return; }
          if (Date.now() >= deadline) { reject(new Error('first analyzer frame did not arrive')); return; }
          setTimeout(poll, 20);
        };
        poll();
      })`);
      assert(firstSequence === 1, `${name} first analyzer sequence was ${firstSequence}, expected 1`);
      const first = await evaluate('window.SpectrAnalyzer.debugSnapshot()');
      const canvasBefore = await evaluate('window.__spectrCanvasSummary()');
      const beforeShot = await command('Page.captureScreenshot', { format: 'png' });
      const beforeBytes = Buffer.from(beforeShot.data, 'base64');
      const beforePath = path.join(outDir, `${name}.before.png`);
      write(beforePath, beforeBytes);
      const secondSequence = await evaluate(`new Promise((resolve, reject) => {
        const deadline = Date.now() + 5000;
        const poll = () => {
          const frame = window.SpectrAnalyzer?.debugSnapshot?.();
          if (frame && frame.sequence_number >= 2) { resolve(frame.sequence_number); return; }
          if (Date.now() >= deadline) { reject(new Error('second analyzer frame did not arrive')); return; }
          setTimeout(poll, 20);
        };
        poll();
      })`);
      assert(secondSequence >= 2, `${name} second analyzer sequence was ${secondSequence}, expected >= 2`);
      await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(resolve, 300))))');
      const second = await evaluate('window.SpectrAnalyzer.debugSnapshot()');
      const canvasAfter = await evaluate('window.__spectrCanvasSummary()');
      const summary = await evaluate('({ root: document.querySelector("#root")?.outerHTML || "", canvases: document.querySelectorAll("canvas").length, ready: document.querySelector("#root")?.children.length > 0, analyzer: window.__spectrAnalyzerEmissions, authoredAppInvocations: window.__spectrAuthoredAppInvocations || 0, errors: window.__spectrRuntimeErrors || [] })');
      const shot = await command('Page.captureScreenshot', { format: 'png' });
      const bytes = Buffer.from(shot.data, 'base64');
      const screenshotPath = path.join(outDir, `${name}.png`);
      write(screenshotPath, bytes);
      const analyzerTrace = {
        first_sha256: sha256(Buffer.from(JSON.stringify(first?.visible?.magnitude_db || []))),
        second_sha256: sha256(Buffer.from(JSON.stringify(second?.visible?.magnitude_db || []))),
        first_sequence: first?.sequence_number,
        second_sequence: second?.sequence_number,
        sequence_delta: (second?.sequence_number || 0) - (first?.sequence_number || 0),
      };
      results[name] = {
        ...summary,
        browser_errors: [...errors],
        networkFailures: [...networkFailures],
        canvasBefore,
        canvasAfter,
        analyzerSnapshot: second,
        analyzerTrace,
        screenshot: { path: screenshotPath, sha256: sha256(bytes), bytes: bytes.length },
        screenshots: {
          before: { path: beforePath, sha256: sha256(beforeBytes), bytes: beforeBytes.length },
          after: { path: screenshotPath, sha256: sha256(bytes), bytes: bytes.length },
        },
      };
      assert(summary.errors.length === 0, `${name} page errors: ${summary.errors.join('; ')}`);
      assert(networkFailures.length === 0,
        `${name} network failures: ${JSON.stringify(networkFailures)}`);
      assert(summary.analyzer >= 2, `${name} did not receive both deterministic analyzer frames`);
      assert(name === 'baseline' ? summary.authoredAppInvocations === 0 : summary.authoredAppInvocations > 0,
        `${name} authored App invocation count was ${summary.authoredAppInvocations}`);
      assert(summary.canvases === 3, `${name} rendered ${summary.canvases} canvas layers; expected 3`);
      const assertCanvasLayers = (layers, phase) => {
        assert(Array.isArray(layers) && layers.length === 3,
          `${name} ${phase} canvas layer summary is incomplete: ${JSON.stringify(layers)}`);
        for (const [index, layer] of layers.entries()) {
          assert(layer.width === EXPECTED_CANVAS_WIDTH && layer.height === EXPECTED_CANVAS_HEIGHT,
            `${name} ${phase} canvas ${index} dimensions were ${layer.width}x${layer.height}; expected 1320x860`);
          assert(layer.ink > 0,
            `${name} ${phase} canvas ${index} has no painted pixels: ${JSON.stringify(layer)}`);
        }
      };
      assertCanvasLayers(canvasBefore, 'first-frame');
      assertCanvasLayers(canvasAfter, 'settled');
      assert(analyzerTrace.first_sha256 !== analyzerTrace.second_sha256,
        `${name} analyzer trace did not change between deterministic frames`);
      assert(analyzerTrace.second_sequence >= 2,
        `${name} analyzer snapshot did not retain sequence >= 2: ${JSON.stringify(analyzerTrace)}`);
    }
    assert(results.baseline.root === results.reimported.root, 'baseline and authored App DOM differ'); assert(results.baseline.canvases === 3 && results.reimported.canvases === 3, 'App did not render exactly three canvas layers'); assert(results.baseline.browser_errors.length === 0 && results.reimported.browser_errors.length === 0, `browser errors: ${JSON.stringify(results)}`); assert(results.baseline.networkFailures.length === 0 && results.reimported.networkFailures.length === 0, `network failures: ${JSON.stringify(results)}`); assert(results.baseline.analyzer >= 2 && results.reimported.analyzer >= 2, `analyzer frames were not observed: ${JSON.stringify(Object.fromEntries(Object.entries(results).map(([name, value]) => [name, { analyzer: value.analyzer, runtime: value.errors, canvases: value.canvases }])))} `); assert(results.baseline.screenshot.sha256 === results.reimported.screenshot.sha256, `settled screenshots differ: ${results.baseline.screenshot.sha256} vs ${results.reimported.screenshot.sha256}`);
    const stderrDisposition = classifyChromeStderr(stderr.trim());
    assert(stderrDisposition.unclassified.length === 0,
      `Chrome emitted unclassified stderr diagnostics: ${JSON.stringify(stderrDisposition.unclassified)}`);
    return { results, transport: 'cdp-http-loopback', stderr: stderr.trim(), stderrDisposition };
  } finally { try { socket?.close(); } catch {} if (child && child.exitCode === null && child.signalCode === null) child.kill('SIGTERM'); await new Promise(r => setTimeout(r, 250)); if (child && child.exitCode === null && child.signalCode === null) child.kill('SIGKILL'); if (server.closeAllConnections) server.closeAllConnections(); await new Promise(r => server.close(() => r())); fs.rmSync(profile, { recursive: true, force: true }); }
}
async function main(argv) {
  const args = parseArgs(argv);
  if (args.help) {
    console.log('usage: node tools/authored_reimport_full_app_materialized_browser.mjs --artifact FILE --chrome PATH --out DIR');
    return;
  }
  for (const key of ['artifact', 'out']) assert(args[key], `--${key} is required`);
  const artifact = path.resolve(args.artifact);
  const editorHtml = path.join(REPO, 'resources', 'editor.html');
  const artifactBefore = sha256(read(artifact));
  const editorBefore = sha256(read(editorHtml));
  const outDir = path.resolve(args.out);
  const chrome = path.resolve(args.chrome || CHROME_DEFAULT);
  assert(fs.existsSync(chrome), `Chrome executable is missing: ${chrome}`);
  prepareOutputDirectory(outDir);
  const staged = stage(artifact);
  try {
    const document = JSON.parse(read(artifact));
    const html = document.html;
    const assets = new Map((document.assets || []).map(asset => [
      asset.id,
      { body: Buffer.from(asset.data_base64, 'base64'), mime: asset.mime_type },
    ]));
    const compiled = compile(staged.source);
    const authored = JSON.parse(read(staged.manifest)).components.find(item => item.name === 'App');
    assert(authored, 'App source provenance component missing');
    assert(sha256(Buffer.from(sliceFunction(html, 'App'))) === authored.source_sha256,
      'App source provenance hash mismatch');
    const patched = patchApp(html, compiled.compiled);
    const negativePatched = mutatePatchedApp(patched);
    const baseline = Buffer.from(injected(html));
    const reimported = Buffer.from(injected(patched));
    write(path.join(outDir, 'baseline.html'), baseline);
    write(path.join(outDir, 'reimported.html'), reimported);
    const run = await browser({ baseline, reimported }, chrome, outDir, assets);
    const negativeOut = path.join(outDir, 'negative-dom-mutation');
    const negative = await browser({
      baseline: Buffer.from(injected(html)),
      reimported: Buffer.from(injected(negativePatched)),
    }, chrome, negativeOut, assets)
      .then(result => ({ status: 'unexpected-pass', result }))
      .catch(error => {
        const message = String(error.message);
        assert(message.includes('baseline and authored App DOM differ')
          || message.includes('settled screenshots differ'),
        `planted authored App mutation failed for an unrelated reason: ${message}`);
        return { status: 'passed', error: message };
      });
    assert(negative.status === 'passed', 'planted authored App mutation unexpectedly passed parity');
    const appManifest = JSON.parse(read(path.join(staged.emission, 'authored-modules.manifest.json')));
    const appModule = appManifest.modules.find(item => item.name === 'App');
    assert(appModule, 'emitted closure manifest has no App module');
    const artifactAfter = sha256(read(artifact));
    const editorAfter = sha256(read(editorHtml));
    assert(artifactAfter === artifactBefore, `materialized artifact changed during harness run: ${artifactBefore} -> ${artifactAfter}`);
    assert(editorAfter === editorBefore, `editor.html changed during harness run: ${editorBefore} -> ${editorAfter}`);
    const receipt = {
      schema: SCHEMA,
      version: 1,
      source: {
        artifact: path.basename(artifact),
        artifact_sha256: artifactBefore,
        app_source_sha256: authored.source_sha256,
        app_emitted_sha256: appModule.output_sha256,
      },
      checks: {
        closure_verified: true,
        authored_app_compiled: true,
        app_source_provenance: true,
        authored_app_invocation: true,
        browser_ready: true,
        root_dom_parity: true,
        canvas_layers: true,
        analyzer_frames: true,
        console_network_clean: true,
        chrome_stderr_classified: true,
        editor_html_unchanged: editorBefore === editorAfter,
        runtime_artifact_unchanged: artifactBefore === artifactAfter,
        settled_screenshot_parity: true,
        negative_control: {
          status: negative.status,
          mutation: 'append a hidden data-spectr-negative-app node from the authored App wrapper',
          evidence: negative.error,
        },
      },
      browser: run,
      scope: {
        materialized_runtime_baseline: true,
        editor_html_sha256_before: editorBefore,
        editor_html_sha256_after: editorAfter,
        runtime_artifact_sha256_before: artifactBefore,
        runtime_artifact_sha256_after: artifactAfter,
        editor_html_unchanged: editorBefore === editorAfter,
        runtime_artifact_changed: artifactBefore !== artifactAfter,
        full_native_parity: false,
        production_cutover: false,
      },
    };
    write(path.join(outDir, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`);
    process.stdout.write(`${JSON.stringify(receipt, null, 2)}\n`);
  } finally {
    fs.rmSync(staged.dir, { recursive: true, force: true });
  }
}
main(process.argv.slice(2)).catch(error => { console.error(error.stack || error.message); process.exit(1); });
