#!/usr/bin/env node
/**
 * Exercise the authored FilterBank through the real Spectr App mount.
 *
 * This is a staging proof.  It injects one compiled authored FilterBank
 * declaration into a copy of resources/editor.html, runs the normal
 * unpack/ReactDOM bootstrap in Chromium, and feeds a deterministic native
 * analyzer frame through window.pulp.  The checked-in editor and materialized
 * runtime are never changed.  The no-ink mutation is deliberately run as a
 * separate page and must fail the same ink gate used by the positive pages.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { spawn, spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import zlib from 'node:zlib';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..');
let ts;
try {
  ts = createRequire(import.meta.url)(
    path.join(repo, 'tools', 'wp1-parser', 'node_modules', 'typescript'));
} catch (error) {
  fail(`pinned WP-1 TypeScript toolchain is missing; run npm ci --ignore-scripts --prefix ${path.join(repo, 'tools', 'wp1-parser')} (${error.message})`);
}

const SCHEMA = 'spectr-authored-reimport-filter-bank-app-mount-v1';
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

function fail(message) { throw new Error(`authored FilterBank App mount failed: ${message}`); }
function assert(condition, message) { if (!condition) fail(message); }
function read(file) { try { return fs.readFileSync(file); } catch (error) { fail(`cannot read ${file}: ${error.message}`); } }
function write(file, value) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, value); }
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (['artifact', 'editor', 'source', 'out', 'chrome'].some(key => arg === `--${key}`))
      args[arg.slice(2)] = argv[++i];
    else if (arg === '--help') args.help = true;
    else fail(`unknown argument ${arg}`);
  }
  return args;
}

function runNode(script, args, label) {
  const result = spawnSync(process.execPath, [script, ...args], {
    cwd: repo, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024,
  });
  if (result.status !== 0)
    fail(`${label}: ${(result.stderr || result.stdout || 'no diagnostic').trim()}`);
  return result;
}

function materializeFilterBankSource(artifact) {
  const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-authored-filter-bank-stage-'));
  const manifest = path.join(stage, 'manifest.json');
  const emission = path.join(stage, 'emitted');
  runNode(path.join(repo, 'tools', 'wp1_dependency_manifest.mjs'),
    ['--artifact', artifact, '--root', 'FilterBank', '--out', manifest], 'dependency manifest');
  runNode(path.join(repo, 'tools', 'wp1_authored_module_emitter.mjs'),
    ['--artifact', artifact, '--manifest', manifest, '--out', emission], 'authored module emission');
  const source = path.join(emission, 'components', 'FilterBank.tsx');
  assert(fs.existsSync(source), `emitted FilterBank source is missing: ${source}`);
  return { stage, source, manifest, emission };
}

function functionSlice(source, name) {
  const marker = `function ${name}`;
  const start = source.lastIndexOf(marker);
  assert(start >= 0, `${name} declaration is missing`);
  const signatureEnd = source.indexOf(')', start);
  assert(signatureEnd >= 0, `${name} signature is incomplete`);
  const open = source.indexOf('{', signatureEnd);
  assert(open >= 0, `${name} body is missing`);
  let depth = 1;
  let quote = null;
  let escaped = false;
  let line = false;
  let block = false;
  for (let i = open + 1; i < source.length; i += 1) {
    const c = source[i];
    const n = source[i + 1] || '';
    if (line) { if (c === '\n') line = false; continue; }
    if (block) {
      if (c === '*' && n === '/') { block = false; i += 1; }
      continue;
    }
    if (quote) {
      if (escaped) escaped = false;
      else if (c === '\\') escaped = true;
      else if (c === quote) quote = null;
      continue;
    }
    if (c === '/' && n === '/') { line = true; i += 1; continue; }
    if (c === '/' && n === '*') { block = true; i += 1; continue; }
    if (c === "'" || c === '"' || c === '`') { quote = c; continue; }
    if (c === '{') depth += 1;
    else if (c === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  fail(`unterminated ${name} declaration`);
}

function templateFromEditor(editor) {
  const input = read(editor);
  let html = input.toString('utf8');
  // The production materialized runtime is a JSON artifact whose `html`
  // member already contains the post-import helper surface.  Keeping this
  // path alongside editor.html lets the browser gate exercise the same
  // runtime that owns the emitted FilterBank, instead of accidentally
  // hiding missing helper bindings behind the older Claude template.
  if (path.extname(editor).toLowerCase() === '.json') {
    let artifact;
    try { artifact = JSON.parse(input); }
    catch (error) { fail(`runtime artifact JSON is invalid: ${error.message}`); }
    assert(typeof artifact?.html === 'string' && artifact.html.includes('function FilterBank'),
      'runtime artifact has no materialized FilterBank HTML');
    html = artifact.html;
  }
  const match = html.match(/<script\s+type="__bundler\/template">([\s\S]*?)<\/script>/i);
  if (!match) {
    assert(html.includes('function FilterBank'), 'editor input has no FilterBank source');
    return { html, template: html, originalFilterBank: functionSlice(html, 'FilterBank'), directRuntime: true };
  }
  let template;
  try { template = JSON.parse(match[1]); }
  catch (error) { fail(`bundler template JSON is invalid: ${error.message}`); }
  assert(typeof template === 'string' && template.includes('function FilterBank'),
    'bundler template has no FilterBank source');
  return { html, template, originalFilterBank: functionSlice(template, 'FilterBank'), directRuntime: false };
}

function compileAuthored(source, sourcePath) {
  const wrapped = source.includes('export function FilterBank')
    ? source : `export ${source}`;
  const result = ts.transpileModule(wrapped, {
    fileName: sourcePath,
    reportDiagnostics: true,
    compilerOptions: {
      target: ts.ScriptTarget.ES2020,
      module: ts.ModuleKind.CommonJS,
      jsx: ts.JsxEmit.React,
      jsxFactory: 'React.createElement',
    },
  });
  const errors = (result.diagnostics || [])
    .filter(item => item.category === ts.DiagnosticCategory.Error);
  assert(!errors.length,
    `authored FilterBank TSX diagnostics: ${errors.map(item => ts.flattenDiagnosticMessageText(item.messageText, '\n')).join('; ')}`);
  assert(result.outputText.includes('exports.FilterBank'),
    'authored FilterBank did not emit an explicit export');
  return result.outputText;
}

// The legacy Claude template predates the materialized runtime helpers used
// by the emitted FilterBank.  This tiny fixture facade is intentionally
// explicit: every name is copied from the corresponding materialized helper
// contract, so a future helper reference cannot silently fall back to an
// ambient global.  The production runtime is exercised without this shim
// when --editor points at materialized-document.runtime.json.
function claudeTemplateRuntimeFacade() {
  return String.raw`
function spectrDrawnBandCount(n) {
  return [32, 40, 48, 56, 64].includes(Number(n)) ? Number(n) : n;
}

function spectrPadBandRow(row, n, fill) {
  if (!row || row.length >= n) return row;
  const out = Array.from(row);
  while (out.length < n) out.push(fill);
  return out;
}
function sameBandSet(a, b) {
  if (a === b) return true;
  if (!a || !b || a.size !== b.size) return false;
  for (const value of a) if (!b.has(value)) return false;
  return true;
}
function spectrBandMenuPortal(menu) {
  const layer = document.querySelector('[data-spectr-global-menu-layer]');
  const canPortal = typeof ReactDOM !== 'undefined' && ReactDOM !== null
    && typeof ReactDOM.createPortal === 'function';
  return (layer && canPortal) ? ReactDOM.createPortal(menu, layer) : menu;
}
function spectrStatusBannerWidth(text) {
  return Math.max(96, Math.min(520, (text ? text.length : 0) * 8 + 28));
}
function spectrPlaceStatusBanner(node, bannerWidth) {
  if (!node || !node.style) return;
  node.style.top = 104;
  node.style.left = '50%';
  node.style.width = bannerWidth;
  node.style.marginLeft = -bannerWidth / 2;
}
const StableContextMenu = React.memo(ContextMenu, (previous, next) =>
  previous.x === next.x && previous.y === next.y && previous.band === next.band
  && previous.N === next.N && previous.selection === next.selection
  && previous.editMode === next.editMode);
`;
}

function reactVendorScripts() {
  const editorPath = path.join(repo, 'resources', 'editor.html');
  const html = read(editorPath).toString('utf8');
  const match = html.match(/<script\s+type="__bundler\/manifest">([\s\S]*?)<\/script>/i);
  assert(match, 'editor vendor manifest is missing');
  let manifest;
  try { manifest = JSON.parse(match[1]); }
  catch (error) { fail(`editor vendor manifest is invalid: ${error.message}`); }
  const vendor = Object.values(manifest).filter(entry => entry.mime === 'text/javascript').slice(0, 2);
  assert(vendor.length === 2, 'editor vendor manifest has no React/ReactDOM pair');
  return vendor.map(entry => {
    let bytes = Buffer.from(entry.data, 'base64');
    if (entry.compressed) bytes = zlib.gunzipSync(bytes);
    // ReactDOM's development build contains a literal <script> in an error
    // string. Escape only script-tag spellings so the HTML parser cannot end
    // the vendor element early; JavaScript string semantics are unchanged.
    const source = bytes.toString('utf8').replace(/<(?=\/?script\b)/gi, '\\x3c');
    return `<script>${source}</script>`;
  }).join('');
}

function patchFilterBank(editorHtml, originalFilterBank, compiled, mutation = false) {
  const source = mutation
    ? compiled.replace('const ctx = canvasRef.current.getContext("2d");',
      'const ctx = canvasRef.current.getContext("2d"); return;')
    : compiled;
  assert(!mutation || source !== compiled, 'planted no-ink mutation did not change compiled output');
  const wrapper = `function __wp1ReimportedFilterBankModule() {\n` +
    `  const module = { exports: {} };\n` +
    `  const exports = module.exports;\n` +
    `  ${source}\n` +
    `  return module.exports.FilterBank;\n` +
    `}\n` +
    `const __wp1ReimportedFilterBank = __wp1ReimportedFilterBankModule();\n` +
    `window.__wp1FilterBankReimported = true;\n` +
    `window.__wp1FilterBankRenderCount = 0;\n` +
    `function FilterBank(props) {\n` +
    `  window.__wp1FilterBankRenderCount += 1;\n` +
    `  return __wp1ReimportedFilterBank(props);\n` +
    `}`;
  const replacement = JSON.stringify(wrapper);
  // A materialized runtime has no bundler bootstrap/template assignment: its
  // scripts are already unpacked. Replace the declaration directly while
  // preserving every helper and sibling component in the same global scope.
  if (!editorHtml.includes('<script type="__bundler/template">')) {
    const start = editorHtml.indexOf(originalFilterBank);
    assert(start >= 0 && editorHtml.indexOf(originalFilterBank, start + 1) < 0,
      'materialized FilterBank declaration is missing or duplicated');
    return editorHtml.slice(0, start) + wrapper + editorHtml.slice(start + originalFilterBank.length);
  }
  const needle = '    // Inject after <head> so the DOCTYPE stays first; prepending the script';
  assert(editorHtml.includes(needle), 'editor bootstrap template assignment is missing');
  // Existing editor patches intentionally run before this fixture injection,
  // so an exact source marker would be stale by the time the replacement is
  // attempted.  Locate the one declaration after those patches and scan its
  // balanced body while respecting comments and quoted/template strings.
  const facade = claudeTemplateRuntimeFacade();
  const injection = `const __wp1FilterBankReplacement = ${replacement};\n` +
    `    const __wp1FilterBankStart = template.indexOf('function FilterBank');\n` +
    `    if (__wp1FilterBankStart < 0 || template.lastIndexOf('function FilterBank') !== __wp1FilterBankStart) throw new Error('FilterBank declaration count mismatch');\n` +
    `    const __wp1FilterBankSignatureClose = template.indexOf(')', __wp1FilterBankStart);\n` +
    `    const __wp1FilterBankOpen = template.indexOf('{', __wp1FilterBankSignatureClose);\n` +
    `    let __wp1FilterBankDepth = 1, __wp1FilterBankQuote = null, __wp1FilterBankEscaped = false, __wp1FilterBankLine = false, __wp1FilterBankBlock = false, __wp1FilterBankClose = -1;\n` +
    `    for (let __i = __wp1FilterBankOpen + 1; __i < template.length; ++__i) {\n` +
    `      const __c = template[__i], __n = template[__i + 1] || '';\n` +
    `      if (__wp1FilterBankLine) { if (__c === String.fromCharCode(10)) __wp1FilterBankLine = false; continue; }\n` +
    `      if (__wp1FilterBankBlock) { if (__c === '*' && __n === '/') { __wp1FilterBankBlock = false; ++__i; } continue; }\n` +
    `      if (__wp1FilterBankQuote) { if (__wp1FilterBankEscaped) __wp1FilterBankEscaped = false; else if (__c === String.fromCharCode(92)) __wp1FilterBankEscaped = true; else if (__c === __wp1FilterBankQuote) __wp1FilterBankQuote = null; continue; }\n` +
    `      if (__c === '/' && __n === '/') { __wp1FilterBankLine = true; ++__i; continue; }\n` +
    `      if (__c === '/' && __n === '*') { __wp1FilterBankBlock = true; ++__i; continue; }\n` +
    `      if (__c === "'" || __c === '"' || __c === String.fromCharCode(96)) { __wp1FilterBankQuote = __c; continue; }\n` +
    `      if (__c === '{') ++__wp1FilterBankDepth;\n` +
    `      else if (__c === '}' && --__wp1FilterBankDepth === 0) { __wp1FilterBankClose = __i; break; }\n` +
    `    }\n` +
    `    if (__wp1FilterBankClose < 0) throw new Error('FilterBank declaration body is unterminated');\n` +
    `    template = template.slice(0, __wp1FilterBankStart) + __wp1FilterBankReplacement + template.slice(__wp1FilterBankClose + 1);\n` +
    `    const __wp1RuntimeFacade = ${JSON.stringify(facade)};\n` +
    `    const __wp1RuntimeFacadeAnchor = template.indexOf('ReactDOM.createRoot');\n` +
    `    if (__wp1RuntimeFacadeAnchor < 0) throw new Error('ReactDOM App mount anchor is missing');\n` +
    `    template = template.slice(0, __wp1RuntimeFacadeAnchor) + __wp1RuntimeFacade + template.slice(__wp1RuntimeFacadeAnchor);`;
  const output = editorHtml.replace(needle, `${needle}\n    ${injection}`);
  assert(output !== editorHtml, 'editor fixture was not changed');
  return output;
}

function clone(value) { return JSON.parse(JSON.stringify(value)); }

function nativeState(n) {
  const gainDb = Array.from({ length: n }, (_, i) => [12, 6, 0, -6][i % 4]);
  return {
    revision: 0,
    n_visible: n,
    gain_db: gainDb,
    muted: new Array(n).fill(false),
    min_hz: 20,
    max_hz: 20000,
    motion_mode: 0,
    analyzer_mode: 2,
    edit_mode: 0,
    visualization_mode: 2,
    snapshots: {
      A: { populated: false, gain_db: [], muted: [] },
      B: { populated: false, gain_db: [], muted: [] },
    },
    patterns_json: JSON.stringify({
      format: 'spectr.patterns', version: 1,
      default_id: 'factory:flat', patterns: [],
    }),
  };
}

function analyzerFrame(sequence, phase = 0) {
  const trace = (length) => Array.from({ length }, (_, i) => {
    const t = i / (length - 1);
    const p1 = Math.exp(-Math.pow((t - (0.21 + phase * 0.08)) / 0.055, 2));
    const p2 = Math.exp(-Math.pow((t - (0.52 - phase * 0.06)) / 0.09, 2));
    const p3 = Math.exp(-Math.pow((t - (0.82 + phase * 0.04)) / 0.045, 2));
    return -92 + 78 * Math.min(1, 0.85 * p1 + 1.0 * p2 + 0.7 * p3);
  });
  return {
    schema_version: 1,
    epoch: 1,
    sequence_number: sequence,
    dropped_frames: 0,
    source_channels: 2,
    fft_size: 1024,
    sample_rate: 48000,
    floor_db: -96,
    ceiling_db: 0,
    visible: { min_hz: 20, max_hz: 20000, magnitude_db: trace(321) },
    overview: { min_hz: 20, max_hz: 20000, magnitude_db: trace(121) },
  };
}

function bridgeScript(n) {
  const state = nativeState(n);
  const frame1 = analyzerFrame(1, 0);
  const frame2 = analyzerFrame(2, 1);
  return `<script>
window.__spectrHandlers = Object.create(null);
window.__spectrTestHooks = window.__spectrTestHooks || {};
window.__spectrPosts = [];
window.__spectrRuntimeErrors = [];
window.__spectrAnalyzerEmissions = 0;
window.__spectrFrameOne = ${JSON.stringify(frame1)};
window.__spectrFrameTwo = ${JSON.stringify(frame2)};
window.addEventListener('error', event => window.__spectrRuntimeErrors.push(String(event.message || event.type)));
window.addEventListener('unhandledrejection', event => window.__spectrRuntimeErrors.push(String(event.reason || 'unhandled rejection')));
window.confirm = () => true;
window.requestAnimationFrame = callback => setTimeout(() => callback(performance.now()), 16);
window.cancelAnimationFrame = handle => clearTimeout(handle);
Element.prototype.setPointerCapture = () => {};
Element.prototype.releasePointerCapture = () => {};
const __spectrState = ${JSON.stringify(state)};
const __spectrClone = value => JSON.parse(JSON.stringify(value));
window.__spectrEmit = (type, payload) => {
  if (type === 'analyzer_frame') window.__spectrAnalyzerEmissions += 1;
  // A materialized runtime already installed its native analyzer facade
  // before this deterministic test transport is injected. Publish analyzer
  // frames through that facade as well as through the mock listeners so the
  // real canvas samples the accepted frame identity and the App wake-up hook
  // still observes the same event.
  if (type === 'analyzer_frame'
      && typeof window.__spectrPublishNativeMessage === 'function')
    window.__spectrPublishNativeMessage(type, payload);
  for (const callback of window.__spectrHandlers[type] || []) callback({ type, payload: __spectrClone(payload) });
};
window.pulp = {
  initial(type) { return type === 'processing_state_get' ? __spectrClone(__spectrState) : null; },
  on(type, callback) {
    (window.__spectrHandlers[type] ||= new Set()).add(callback);
    return () => window.__spectrHandlers[type].delete(callback);
  },
  postMessage(type, payload, id) {
    window.__spectrPosts.push({ type, payload, id });
    if (type === 'editor_ready') {
      setTimeout(() => window.__spectrEmit('processing_state_hydrate', __spectrState), 0);
      setTimeout(() => window.__spectrEmit('analyzer_frame', window.__spectrFrameOne), 120);
      setTimeout(() => window.__spectrEmit('analyzer_frame', window.__spectrFrameTwo), 420);
      return Promise.resolve({ ok: true, payload: { ok: true } });
    }
    if (type === 'spectral_resolution_request') {
      return Promise.resolve({ ok: true, payload: { ok: true, represented_bands: ${n}, active_bands: ${n}, min_hz: 20, max_hz: 20000 } });
    }
    if (type === 'processing_state_set') {
      Object.assign(__spectrState, {
        n_visible: payload.n_visible,
        gain_db: payload.gain_db.slice(), muted: payload.muted.slice(),
        min_hz: payload.min_hz, max_hz: payload.max_hz,
      });
    }
    return Promise.resolve({ ok: true, payload: { ok: true } });
  },
};
</script>`;
}

function instrumentScript() {
  return `<script>
window.__spectrCanvasHash = () => {
  const c = document.querySelector('canvas[data-spectr-filter-canvas]');
  if (!c || !c.width || !c.height) return { hash: 0, colorful: 0, nonzero: 0, width: c?.width || 0, height: c?.height || 0 };
  const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
  let h = 2166136261 >>> 0, colorful = 0, nonzero = 0;
  for (let i = 0; i < d.length; i += 4) {
    const r = d[i], g = d[i + 1], b = d[i + 2], a = d[i + 3];
    if (a > 8 && (r + g + b) > 24) nonzero++;
    if (a > 24 && Math.max(r, g, b) - Math.min(r, g, b) > 12 && (r + g + b) > 40) colorful++;
    h ^= r; h = Math.imul(h, 16777619); h ^= g; h = Math.imul(h, 16777619);
    h ^= b; h = Math.imul(h, 16777619); h ^= a; h = Math.imul(h, 16777619);
  }
  return { hash: h >>> 0, colorful, nonzero, width: c.width, height: c.height };
};
window.__spectrBands = n => {
  const wrap = document.querySelector('[data-spectr-bank-ready="true"]');
  if (!wrap || !window.__spectrTestHooks?.bandAtClientX) return [];
  const rect = wrap.getBoundingClientRect();
  // Probe the real hit-test transform densely instead of assuming the plot's
  // margins or gap policy.  This catches a missing band while remaining
  // independent of responsive layout and native canvas sizing.
  const hits = new Set();
  const samples = Math.max(4096, n * 128);
  for (let i = 0; i < samples; i += 1) {
    const x = rect.left + (i + 0.5) / samples * rect.width;
    const band = window.__spectrTestHooks.bandAtClientX(x);
    if (Number.isInteger(band) && band >= 0 && band < n) hits.add(band);
  }
  return Array.from(hits).sort((a, b) => a - b);
};
</script>`;
}

function makePage(editorHtml, bridge, instrument) {
  // For a direct materialized runtime, the first script is the native bridge;
  // install the deterministic mock immediately after it so the later React
  // scripts see our analyzer/state transport.  The Claude editor has no
  // native bridge and keeps the historical prepend path.
  // Use a function replacement: React's vendor source contains `$'` and `$``
  // spellings that String#replace would otherwise interpret as replacement
  // tokens, duplicating the remainder of the HTML into the vendor script.
  const output = editorHtml.includes('<script type="__bundler/template">')
    ? editorHtml.replace('<script>', () => bridge + instrument + '<script>')
    : editorHtml.replace('</script>', () => `</script>${reactVendorScripts()}${bridge}${instrument}`);
  assert(output !== editorHtml, 'bridge injection point missing');
  return output;
}

function reservePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const port = server.address().port;
      server.close(() => resolve(port));
    });
  });
}

async function browserRun(files, chrome, outDir) {
  const server = http.createServer((request, response) => {
    const name = new URL(request.url, 'http://127.0.0.1').pathname.slice(1);
    const file = files[name];
    if (!file) { response.writeHead(404); response.end(); return; }
    response.writeHead(200, {
      'content-type': 'text/html; charset=utf-8',
      'content-length': file.length,
      connection: 'close',
    });
    response.end(file);
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const debugPort = await reservePort();
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-filter-bank-app-mount-'));
  let child;
  let socket;
  let stderr = '';
  let nextId = 1;
  const pending = new Map();
  try {
    child = spawn(path.resolve(chrome), [
      '--headless=new', '--disable-gpu', '--disable-background-networking',
      '--disable-component-update', '--disable-sync', '--no-first-run',
      '--no-default-browser-check', '--remote-debugging-address=127.0.0.1',
      `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`,
      '--window-size=1320,860', 'about:blank',
    ], { stdio: ['ignore', 'ignore', 'pipe'] });
    child.stderr.on('data', chunk => { stderr += chunk; });
    let websocketUrl;
    const deadline = Date.now() + 15000;
    while (!websocketUrl && Date.now() < deadline) {
      try {
        const pages = await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json();
        websocketUrl = pages.find(page => page.type === 'page')?.webSocketDebuggerUrl;
      } catch {}
      if (!websocketUrl) await new Promise(resolve => setTimeout(resolve, 50));
    }
    assert(websocketUrl, `Chrome DevTools endpoint did not start: ${stderr.trim()}`);
    socket = new WebSocket(websocketUrl);
    await new Promise((resolve, reject) => {
      socket.addEventListener('open', resolve, { once: true });
      socket.addEventListener('error', reject, { once: true });
    });
    socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (message.method === 'Runtime.exceptionThrown' || message.method === 'Runtime.consoleAPICalled') {
        if (!globalThis.__spectrFilterBankDebugEvents) globalThis.__spectrFilterBankDebugEvents = [];
        globalThis.__spectrFilterBankDebugEvents.push(message);
        return;
      }
      if (!message.id || !pending.has(message.id)) return;
      const waiter = pending.get(message.id);
      pending.delete(message.id);
      message.error ? waiter.reject(new Error(JSON.stringify(message.error))) : waiter.resolve(message.result);
    });
    const command = (method, params = {}) => new Promise((resolve, reject) => {
      const id = nextId++;
      pending.set(id, { resolve, reject });
      socket.send(JSON.stringify({ id, method, params }));
    });
    const evaluate = async expression => {
      const result = await command('Runtime.evaluate', {
        expression, awaitPromise: true, returnByValue: true,
      });
      if (result.exceptionDetails)
        throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text || 'browser evaluation failed');
      return result.result.value;
    };
    await command('Page.enable');
    await command('Runtime.enable');
    const debugEvents = [];
    socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (message.method === 'Runtime.exceptionThrown' || message.method === 'Runtime.consoleAPICalled')
        debugEvents.push(message);
    });
    const results = {};
    for (const name of Object.keys(files)) {
      const url = `http://127.0.0.1:${server.address().port}/${name}`;
      await command('Page.navigate', { url });
      const readyDeadline = Date.now() + 30000;
      while (Date.now() < readyDeadline && !(await evaluate(
        `location.href === ${JSON.stringify(url)} && document.readyState === 'complete' && !!document.querySelector('[data-spectr-bank-ready="true"]')`)))
        await new Promise(resolve => setTimeout(resolve, 100));
      const ready = await evaluate(`location.href === ${JSON.stringify(url)} && !!document.querySelector('[data-spectr-bank-ready="true"]')`);
      if (!ready) {
        const diagnostic = await evaluate(`({ error: document.getElementById('__bundler_err')?.textContent || '', body: document.body?.innerText?.slice(-1600) || '', runtime: window.__spectrRuntimeErrors || [], root: document.getElementById('root')?.outerHTML?.slice(0, 800) || '', title: document.title, scripts: Array.from(document.scripts).map(s => ({ type: s.type, src: s.src, length: s.textContent.length })).slice(-12) })`);
        write(path.join(outDir, `${name}.diagnostic.json`), JSON.stringify({ diagnostic, debugEvents }, null, 2));
        fail(`${name} App did not become ready; diagnostic=${JSON.stringify(diagnostic)}`);
      }
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
      const canvasBefore = await evaluate('window.__spectrCanvasHash()');
      const first = await evaluate('window.SpectrAnalyzer.debugSnapshot()');
      const beforeScreenshot = await command('Page.captureScreenshot', { format: 'png' });
      const beforeBytes = Buffer.from(beforeScreenshot.data, 'base64');
      const beforeScreenshotPath = path.join(outDir, `${name}.before.png`);
      write(beforeScreenshotPath, beforeBytes);
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
      const canvasAfter = await evaluate('window.__spectrCanvasHash()');
      const second = await evaluate('window.SpectrAnalyzer.debugSnapshot()');
      const bands = await evaluate(`window.__spectrBands(Number(document.querySelector('[data-spectr-bank-ready="true"]')?.dataset?.spectrBandCount || ${name.includes('32') ? 32 : 64}))`);
      const screenshot = await command('Page.captureScreenshot', { format: 'png' });
      const bytes = Buffer.from(screenshot.data, 'base64');
      const screenshotPath = path.join(outDir, `${name}.png`);
      write(screenshotPath, bytes);
      const runtime = await evaluate('window.__spectrRuntimeErrors || []');
      const emissions = await evaluate('window.__spectrAnalyzerEmissions || 0');
      const renderCount = await evaluate('window.__wp1FilterBankRenderCount || 0');
      results[name] = {
        ready, renderCount, analyzerEmissions: emissions,
        firstFrame: first, secondFrame: second,
        canvasBefore, canvasAfter, canvas: canvasAfter, bands, runtimeErrors: runtime,
        analyzerTrace: {
          first_sha256: sha256(Buffer.from(JSON.stringify(first.visible?.magnitude_db || []))),
          second_sha256: sha256(Buffer.from(JSON.stringify(second.visible?.magnitude_db || []))),
          sequence_delta: second.sequence_number - first.sequence_number,
        },
        screenshots: {
          before: { path: beforeScreenshotPath, sha256: sha256(beforeBytes) },
          after: { path: screenshotPath, sha256: sha256(bytes) },
        },
        screenshot: { path: screenshotPath, sha256: sha256(bytes) },
      };
      assert(runtime.length === 0, `${name} page errors: ${runtime.join('; ')}`);
      assert(emissions >= 2, `${name} did not receive both deterministic analyzer frames`);
      assert(renderCount > 0, `${name} authored FilterBank was not invoked`);
      assert(canvasBefore.colorful > 500, `${name} first analyzer frame central canvas ink is too small: ${JSON.stringify(canvasBefore)}`);
      assert(canvasAfter.colorful > 500, `${name} settled central canvas ink is too small: ${JSON.stringify(canvasAfter)}`);
      assert(results[name].analyzerTrace.first_sha256 !== results[name].analyzerTrace.second_sha256,
        `${name} analyzer trace did not change between deterministic frames`);
      assert(canvasBefore.hash !== canvasAfter.hash,
        `${name} canvas hash did not change between analyzer frames: ${JSON.stringify({ canvasBefore, canvasAfter })}`);
      const expectedN = name.includes('32') ? 32 : 64;
      assert(JSON.stringify(bands) === JSON.stringify(Array.from({ length: expectedN }, (_, i) => i)),
        `${name} band geometry did not expose every expected band: ${JSON.stringify(bands)}`);
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
  if (args.help) {
    console.log('usage: node tools/authored_reimport_filter_bank_app_mount.mjs --editor FILE (--source FILE | --artifact FILE) --out DIR [--chrome PATH]');
    return;
  }
  for (const key of ['editor', 'out']) assert(args[key], `--${key} is required`);
  assert(args.source || args.artifact, 'one of --source or --artifact is required');
  assert(!(args.source && args.artifact), '--source and --artifact are mutually exclusive');
  const editor = path.resolve(args.editor);
  const generated = args.artifact ? materializeFilterBankSource(path.resolve(args.artifact)) : null;
  const sourcePath = path.resolve(args.source || generated.source);
  const outDir = path.resolve(args.out);
  const chrome = path.resolve(args.chrome || CHROME_DEFAULT);
  assert(!fs.existsSync(outDir), `output directory already exists: ${outDir}`);
  assert(fs.existsSync(chrome), `Chrome executable is missing: ${chrome}`);
  const { html, template, originalFilterBank, directRuntime } = templateFromEditor(editor);
  const authored = read(sourcePath).toString('utf8');
  const compiled = compileAuthored(authored, sourcePath);
  const patched32 = makePage(patchFilterBank(html, originalFilterBank, compiled), bridgeScript(32), instrumentScript());
  const patched64 = makePage(patchFilterBank(html, originalFilterBank, compiled), bridgeScript(64), instrumentScript());
  const negative64 = makePage(patchFilterBank(html, originalFilterBank, compiled, true), bridgeScript(64), instrumentScript());
  const files = { 'patched-32.html': Buffer.from(patched32), 'patched-64.html': Buffer.from(patched64) };
  write(path.join(outDir, 'source.tsx'), authored);
  write(path.join(outDir, 'compiled.cjs'), compiled);
  write(path.join(outDir, 'patched-32.html'), patched32);
  write(path.join(outDir, 'patched-64.html'), patched64);
  // The positive lanes are deliberately strict.  Run the no-ink page in its
  // own Chromium pass below so its failure cannot be hidden by a later pass.
  const browser = await browserRun(files, chrome, outDir);
  const negativeOut = path.join(outDir, 'negative-no-ink');
  fs.mkdirSync(negativeOut, { recursive: true });
  const negative = await browserRun({ 'negative-64.html': Buffer.from(negative64) }, chrome, negativeOut)
    .then(result => ({ status: 'unexpected-pass', result }))
    .catch(error => ({ status: 'passed', error: String(error.message) }));
  assert(negative.status === 'passed', 'planted no-ink control unexpectedly passed the positive gate');
  const receipt = {
    schema: SCHEMA,
    version: 1,
    source: {
      editor: path.basename(editor), editor_sha256: sha256(Buffer.from(html)),
      authored: path.basename(sourcePath), authored_sha256: sha256(Buffer.from(authored)),
      compiled_sha256: sha256(Buffer.from(compiled)),
    },
    checks: {
      tsx_compiled: true,
      app_mount: Object.values(browser.results).every(item => item.renderCount > 0),
      analyzer_frame_schema_and_ordering: Object.values(browser.results).every(item => item.analyzerEmissions >= 2),
      canvas_ink: Object.fromEntries(Object.entries(browser.results).map(([name, item]) => [name, item.canvas.colorful])),
      all_bands_geometry: Object.fromEntries(Object.entries(browser.results).map(([name, item]) => [name, item.bands.length])),
      trace_changes_capture: Object.values(browser.results).every(item =>
        item.analyzerTrace.first_sha256 !== item.analyzerTrace.second_sha256
        && item.canvasBefore.hash !== item.canvasAfter.hash),
      negative_control: { status: negative.status, mutation: 'return immediately from renderAll', evidence: negative.error },
    },
    browser,
    scope: { editor_html_unchanged: true, runtime_artifact_changed: false, direct_materialized_runtime: directRuntime, full_native_parity: false, production_cutover: false },
  };
  if (generated) {
    receipt.source.artifact = path.basename(path.resolve(args.artifact));
    receipt.source.artifact_sha256 = sha256(read(path.resolve(args.artifact)));
    receipt.source.dependency_manifest_sha256 = sha256(read(generated.manifest));
    receipt.source.generated_module_manifest_sha256 = sha256(read(path.join(generated.emission, 'authored-modules.manifest.json')));
  }
  write(path.join(outDir, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`);
  process.stdout.write(`${JSON.stringify(receipt, null, 2)}\n`);
  if (generated) fs.rmSync(generated.stage, { recursive: true, force: true });
}

main(process.argv.slice(2)).catch(error => { console.error(error.stack || error.message); process.exit(1); });
