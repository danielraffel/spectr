#!/usr/bin/env node

/**
 * Capture the checked-in materialized Spectr runtime in an external Chromium
 * process.
 *
 * This is the browser half of the authority-aligned parity experiment.  The
 * runtime JSON is served as an HTTP document together with every embedded
 * asset (especially the font files), and the same deterministic analyzer
 * fixture used by the native shot is delivered through the runtime bridge.
 * The checked-in runtime is never modified.  This receipt is browser
 * evidence; a native join remains a separate, explicit operation.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import zlib from 'node:zlib';
import { stateDigest } from './parity_state_canonical.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '..');
const SCHEMA = 'spectr-materialized-runtime-browser-receipt-v1';
const CHROME_DEFAULT = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

function fail(message) { throw new Error(`materialized runtime Chromium oracle failed: ${message}`); }
function assert(condition, message) { if (!condition) fail(message); }
function read(file) {
  try { return fs.readFileSync(file); }
  catch (error) { fail(`cannot read ${file}: ${error.message}`); }
}
function write(file, value) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, value);
}
function sha256(value) { return crypto.createHash('sha256').update(value).digest('hex'); }
function classifyChromeStderr(stderr) {
  const lines = stderr.split('\n').map(line => line.trim()).filter(Boolean);
  const known = lines.filter(line => line.includes('CVDisplayLinkCreateWithCGDisplay failed')
    || line.includes('Trying to load the allocator multiple times')
    || line.includes('DEPRECATED_ENDPOINT'));
  const unclassified = lines.filter(line => !line.includes('DevTools listening on ')
    && !line.includes('CVDisplayLinkCreateWithCGDisplay failed')
    && !line.includes('Trying to load the allocator multiple times')
    && !line.includes('DEPRECATED_ENDPOINT'));
  return { lines, known_nonfatal: known, unclassified };
}
function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i += 1) {
    const key = argv[i];
    if (['artifact', 'fixture', 'chrome', 'out', 'dpr'].some(name => key === `--${name}`)) {
      const value = argv[++i];
      if (!value || value.startsWith('--')) fail(`${key} requires a value`);
      args[key.slice(2)] = value;
    } else if (key === '--plant-missing-asset') args.plantMissingAsset = true;
    else if (key === '--help') args.help = true;
    else fail(`unknown argument ${key}`);
  }
  return args;
}
function prepareOutputDirectory(outDir) {
  const marker = path.join(outDir, '.harness-schema');
  fs.mkdirSync(outDir, { recursive: true });
  let owned = false;
  if (fs.existsSync(marker)) owned = fs.readFileSync(marker, 'utf8').trim() === SCHEMA;
  if (!owned && fs.existsSync(path.join(outDir, 'receipt.json'))) {
    try { owned = JSON.parse(fs.readFileSync(path.join(outDir, 'receipt.json'))).schema === SCHEMA; }
    catch (error) { fail(`existing output receipt is invalid: ${error.message}`); }
  }
  if (!owned) {
    assert(fs.readdirSync(outDir).length === 0,
      `refusing to clear output directory from another harness: ${outDir}`);
    owned = true;
  }
  for (const entry of fs.readdirSync(outDir))
    if (entry !== '.harness-schema') fs.rmSync(path.join(outDir, entry), { recursive: true, force: true });
  write(marker, `${SCHEMA}\n`);
}
function validateFixture(fixture) {
  const integer = value => Number.isSafeInteger(value);
  const finitePositive = value => Number.isFinite(value) && value > 0;
  const trace = fixture.trace;
  assert(fixture.schema === 'spectr-parity-analyzer-v1' && fixture.version === 1,
    'unsupported analyzer fixture schema/version');
  assert(fixture.epoch === 1 && Array.isArray(fixture.sequences)
    && fixture.sequences.length === 2 && fixture.sequences[0] === 1 && fixture.sequences[1] === 2,
  'analyzer fixture must contain sequences 1 and 2');
  assert(finitePositive(fixture.fftSize) && finitePositive(fixture.sampleRate)
    && finitePositive(fixture.sourceChannels), 'analyzer fixture dimensions are invalid');
  assert(Number.isFinite(fixture.floorDb) && Number.isFinite(fixture.ceilingDb)
    && fixture.ceilingDb > fixture.floorDb, 'analyzer fixture dB range is invalid');
  assert(integer(fixture.visibleSamples) && fixture.visibleSamples >= 2
    && integer(fixture.overviewSamples) && fixture.overviewSamples >= 2,
  'analyzer fixture sample counts are invalid');
  assert(finitePositive(fixture.minHz) && finitePositive(fixture.maxHz)
    && fixture.maxHz > fixture.minHz, 'analyzer fixture frequency range is invalid');
  assert(trace && Number.isFinite(trace.baseDb) && Number.isFinite(trace.peakDb)
    && Number.isFinite(trace.centre) && Number.isFinite(trace.phaseStep)
    && finitePositive(trace.width), 'analyzer fixture trace is invalid');
}

function defaultEditorState() {
  const n = 32;
  return {
    revision: 0, n_visible: n, gain_db: Array(n).fill(0), muted: Array(n).fill(false),
    min_hz: 20, max_hz: 20000, motion_mode: 0, analyzer_mode: 0, edit_mode: 0,
    visualization_mode: 2,
    snapshots: {
      A: { populated: false, gain_db: [], muted: [] },
      B: { populated: false, gain_db: [], muted: [] },
    },
    patterns_json: JSON.stringify({ format: 'spectr.patterns', version: 1,
      default_id: 'factory:flat', patterns: [] }),
    range_db: 24,
    keyboard: { host_kind: 'unknown', in_daw: false, shortcuts_in_daw: false,
      show_tooltips: true, ask_before_override: true },
    modulation: {
      enabled: false, shape: 0, beats_per_cycle: 4, depth: 0, target: 0,
      lfo2_enabled: false, lfo2_shape: 0, lfo2_beats_per_cycle: 4,
      lfo2_depth: 0, target_mask: 0, routes: [], freeze_hold_for_length: false,
      morph_applies_viewport: true,
    },
    freeze: { active: false, has_capture: false, length_preset: 16,
      length_label: '1 BAR', custom_length: false },
    latency: {
      control_label: 'LATENCY', mode: 'zero_latency', samples: 0, ms: 0,
      options: [], gpu_available: false, gpu_processing: false,
      mixing_cpu_samples: 0, mixing_gpu_samples: 0, sample_rate: 48000,
    },
    macros: [], morph: 0,
  };
}

function bridgeScript(fixture) {
  const state = defaultEditorState();
  const frameSource = {
    epoch: fixture.epoch, sourceChannels: fixture.sourceChannels,
    fftSize: fixture.fftSize, sampleRate: fixture.sampleRate,
    floorDb: fixture.floorDb, ceilingDb: fixture.ceilingDb,
    minHz: fixture.minHz, maxHz: fixture.maxHz,
    visibleSamples: fixture.visibleSamples, overviewSamples: fixture.overviewSamples,
    trace: fixture.trace,
  };
  return `<script>
window.__spectrHandlers = Object.create(null);
window.__spectrRuntimeErrors = [];
window.__spectrAnalyzerEmissions = 0;
window.__spectrPosts = [];
window.addEventListener('error', event => window.__spectrRuntimeErrors.push(String(event.message || event.type)));
window.addEventListener('unhandledrejection', event => window.__spectrRuntimeErrors.push(String(event.reason || 'unhandled rejection')));
window.confirm = () => true;
window.requestAnimationFrame = callback => setTimeout(() => callback(performance.now()), 16);
window.cancelAnimationFrame = handle => clearTimeout(handle);
Element.prototype.setPointerCapture = () => {};
Element.prototype.releasePointerCapture = () => {};
const __spectrState = ${JSON.stringify(state)};
const __spectrFixture = ${JSON.stringify(frameSource)};
const __spectrBuildInfo = { ok: true, product_version: '1.0.7-staging',
  sdk_version: 'pinned-staging', sdk_sha: 'materialized-runtime-oracle',
  build_type: 'staging', build_time: '2026-10-11T00:00:00Z' };
const __spectrClone = value => JSON.parse(JSON.stringify(value));
const __spectrFrame = (sequence, phase) => {
  const trace = count => Array.from({ length: count }, (_, index) => {
    const x = index / Math.max(1, count - 1);
    return __spectrFixture.trace.baseDb + __spectrFixture.trace.peakDb
      * Math.exp(-Math.pow((x - (__spectrFixture.trace.centre
        + phase * __spectrFixture.trace.phaseStep)) / __spectrFixture.trace.width, 2));
  });
  return { schema_version: 1, epoch: __spectrFixture.epoch, sequence_number: sequence,
    dropped_frames: 0, source_channels: __spectrFixture.sourceChannels,
    fft_size: __spectrFixture.fftSize, sample_rate: __spectrFixture.sampleRate,
    floor_db: __spectrFixture.floorDb, ceiling_db: __spectrFixture.ceilingDb,
    visible: { min_hz: __spectrFixture.minHz, max_hz: __spectrFixture.maxHz,
      magnitude_db: trace(__spectrFixture.visibleSamples) },
    overview: { min_hz: __spectrFixture.minHz, max_hz: __spectrFixture.maxHz,
      magnitude_db: trace(__spectrFixture.overviewSamples) } };
};
window.__spectrEmit = (type, payload) => {
  if (type === 'analyzer_frame') window.__spectrAnalyzerEmissions += 1;
  if (type === 'analyzer_frame' && typeof window.__spectrPublishNativeMessage === 'function')
    window.__spectrPublishNativeMessage(type, payload);
  for (const callback of window.__spectrHandlers[type] || [])
    callback({ type, payload: __spectrClone(payload) });
};
window.pulp = {
  initial(type) {
    if (type === 'processing_state_get') return __spectrClone(__spectrState);
    if (type === 'build_info_get') return __spectrClone(__spectrBuildInfo);
    return null;
  },
  on(type, callback) {
    (window.__spectrHandlers[type] ||= new Set()).add(callback);
    return () => window.__spectrHandlers[type].delete(callback);
  },
  postMessage(type, payload, id) {
    window.__spectrPosts.push({ type, payload, id });
    if (type === 'editor_ready') {
      setTimeout(() => window.__spectrEmit('processing_state_hydrate', __spectrState), 0);
      setTimeout(() => window.__spectrEmit('analyzer_frame', __spectrFrame(1, 0)), 120);
      setTimeout(() => window.__spectrEmit('analyzer_frame', __spectrFrame(2, 1)), 420);
    }
    if (type === 'processing_state_set' && payload && Array.isArray(payload.gain_db)) {
      Object.assign(__spectrState, { n_visible: payload.n_visible,
        gain_db: payload.gain_db.slice(), muted: payload.muted.slice(),
        min_hz: payload.min_hz, max_hz: payload.max_hz });
    }
    if (type === 'build_info_get')
      return Promise.resolve({ ok: true, payload: __spectrClone(__spectrBuildInfo) });
    return Promise.resolve({ ok: true, payload: { ok: true } });
  },
};
</script>`;
}

function reactVendorScripts() {
  // The retained materialized document deliberately removes browser-vendor
  // assets.  The production WebView supplies React/ReactDOM through its
  // native loader; the external Chromium oracle must inject the exact vendor
  // bytes from the maintained editor manifest while serving the runtime's
  // own embedded fonts separately.
  const editor = read(path.join(REPO, 'resources', 'editor.html')).toString('utf8');
  const match = editor.match(/<script\s+type="__bundler\/manifest">([\s\S]*?)<\/script>/i);
  assert(match, 'editor vendor manifest is missing');
  let manifest;
  try { manifest = JSON.parse(match[1]); }
  catch (error) { fail(`editor vendor manifest is invalid: ${error.message}`); }
  const vendor = Object.values(manifest).filter(entry => entry.mime === 'text/javascript').slice(0, 2);
  assert(vendor.length === 2, 'editor vendor manifest has no React/ReactDOM pair');
  return vendor.map(entry => {
    let bytes = Buffer.from(entry.data, 'base64');
    if (entry.compressed) bytes = zlib.gunzipSync(bytes);
    const source = bytes.toString('utf8').replace(/<(?=\/?script\b)/gi, '\\x3c');
    return `<script>${source}</script>`;
  }).join('');
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

function injectBridge(html, bridge) {
  // The materialized document's first script installs the native host facade.
  // Insert after that script so the deterministic transport intentionally
  // overrides the host only for this external oracle.
  const at = html.search(/<\/script\s*>/i);
  assert(at >= 0, 'materialized runtime has no bridge script boundary');
  const end = html.indexOf('>', at) + 1;
  return html.slice(0, end) + bridge + html.slice(end);
}

async function browserCapture({ html, assets, fixture, chrome, outDir, dpr }) {
  const files = new Map([['runtime.html', Buffer.from(html)] ]);
  const server = http.createServer((request, response) => {
    const pathname = decodeURIComponent(new URL(request.url, 'http://127.0.0.1').pathname).replace(/^\/+/, '');
    const file = files.get(pathname);
    const asset = assets.get(pathname);
    const body = file || asset?.body;
    if (!body) { response.writeHead(404); response.end(); return; }
    response.writeHead(200, {
      'content-type': file ? 'text/html; charset=utf-8' : asset.mime,
      'content-length': body.length, connection: 'close',
    });
    response.end(body);
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const debugPort = await reservePort();
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-materialized-runtime-oracle-'));
  let child; let socket; let stderr = ''; let nextId = 1;
  const pending = new Map(); const consoleErrors = []; const networkFailures = [];
  try {
    child = spawn(path.resolve(chrome), [
      '--headless=new', '--disable-gpu', '--disable-background-networking',
      '--disable-component-update', '--disable-sync', '--no-first-run',
      '--no-default-browser-check', '--remote-debugging-address=127.0.0.1',
      `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`,
      `--window-size=1320,860`, `--force-device-scale-factor=${dpr}`, 'about:blank',
    ], { stdio: ['ignore', 'ignore', 'pipe'] });
    child.stderr.on('data', chunk => { stderr += chunk; });
    let websocketUrl;
    const deadline = Date.now() + 15000;
    while (!websocketUrl && Date.now() < deadline) {
      try {
        websocketUrl = (await (await fetch(`http://127.0.0.1:${debugPort}/json`)).json())
          .find(page => page.type === 'page')?.webSocketDebuggerUrl;
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
      if (message.method === 'Runtime.exceptionThrown')
        consoleErrors.push(message.params.exceptionDetails?.text || 'uncaught exception');
      if (message.method === 'Runtime.consoleAPICalled'
          && ['error', 'assert'].includes(message.params.type)) {
        const text = message.params.args.map(arg => arg.value ?? arg.description ?? '').join(' ');
        // React's development vendor emits non-fatal DOM attribute warnings
        // through console.error. They are retained in the receipt's browser
        // trace but are not runtime failures; exceptions and application
        // diagnostics remain strict errors below.
        if (!text.startsWith('Warning:')) consoleErrors.push(text);
      }
      if (message.method === 'Network.responseReceived' && message.params.response.status >= 400
          && !message.params.response.url.endsWith('/favicon.ico'))
        networkFailures.push(`${message.params.response.status}: ${message.params.response.url}`);
      if (message.method === 'Network.loadingFailed' && !String(message.params.requestId).endsWith('favicon'))
        networkFailures.push(`${message.params.errorText}: ${message.params.requestId}`);
      if (!message.id || !pending.has(message.id)) return;
      const waiter = pending.get(message.id); pending.delete(message.id);
      message.error ? waiter.reject(new Error(JSON.stringify(message.error))) : waiter.resolve(message.result);
    });
    const command = (method, params = {}) => new Promise((resolve, reject) => {
      const id = nextId++; pending.set(id, { resolve, reject });
      socket.send(JSON.stringify({ id, method, params }));
    });
    const evaluate = async expression => {
      const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
      if (result.exceptionDetails)
        throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text || 'browser evaluation failed');
      return result.result.value;
    };
    await command('Page.enable'); await command('Runtime.enable'); await command('Network.enable');
    await command('Emulation.setDeviceMetricsOverride', {
      width: 1320, height: 860, deviceScaleFactor: dpr, mobile: false,
      screenWidth: 1320, screenHeight: 860,
    });
    const url = `http://127.0.0.1:${server.address().port}/runtime.html`;
    await command('Page.navigate', { url });
    const readyDeadline = Date.now() + 45000;
    let ready = false;
    while (Date.now() < readyDeadline && !ready) {
      ready = await evaluate(`location.href === ${JSON.stringify(url)}
        && document.readyState === 'complete'
        && !!document.querySelector('[data-spectr-bank-ready="true"]')`);
      if (!ready) await new Promise(resolve => setTimeout(resolve, 100));
    }
    assert(ready, 'materialized runtime did not expose its readiness marker');
    await evaluate('document.fonts?.ready || Promise.resolve()');
    const firstSequence = await evaluate(`new Promise((resolve, reject) => {
      const deadline = Date.now() + 5000;
      const poll = () => { const frame = window.SpectrAnalyzer?.debugSnapshot?.();
        if (frame && frame.sequence_number >= 1) return resolve(frame.sequence_number);
        if (Date.now() >= deadline) return reject(new Error('first analyzer frame did not arrive'));
        setTimeout(poll, 20); }; poll(); })`);
    assert(firstSequence === 1, `first analyzer sequence was ${firstSequence}, expected 1`);
    const first = await evaluate('window.SpectrAnalyzer.debugSnapshot()');
    const beforeShot = await command('Page.captureScreenshot', { format: 'png', fromSurface: true });
    const beforeBytes = Buffer.from(beforeShot.data, 'base64');
    const beforePath = path.join(outDir, 'before.png'); write(beforePath, beforeBytes);
    const secondSequence = await evaluate(`new Promise((resolve, reject) => {
      const deadline = Date.now() + 5000;
      const poll = () => { const frame = window.SpectrAnalyzer?.debugSnapshot?.();
        if (frame && frame.sequence_number >= 2) return resolve(frame.sequence_number);
        if (Date.now() >= deadline) return reject(new Error('second analyzer frame did not arrive'));
        setTimeout(poll, 20); }; poll(); })`);
    assert(secondSequence === 2, `second analyzer sequence was ${secondSequence}, expected 2`);
    await evaluate('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => setTimeout(resolve, 300))))');
    const settled = await evaluate('window.SpectrAnalyzer.debugSnapshot()');
    const settledShot = await command('Page.captureScreenshot', { format: 'png', fromSurface: true });
    const settledBytes = Buffer.from(settledShot.data, 'base64');
    const settledPath = path.join(outDir, 'settled.png'); write(settledPath, settledBytes);
    const repeatShot = await command('Page.captureScreenshot', { format: 'png', fromSurface: true });
    const repeatBytes = Buffer.from(repeatShot.data, 'base64');
    write(path.join(outDir, 'settled-repeat.png'), repeatBytes);
    const canvas = await evaluate(`Array.from(document.querySelectorAll('canvas')).map(node => ({
      width: node.width, height: node.height, static: node.dataset.spectrStaticCanvas === 'true',
      filter: node.dataset.spectrFilterCanvas === 'true', zIndex: getComputedStyle(node).zIndex,
    }))`);
    assert(Array.isArray(canvas) && canvas.length === 3, `expected three runtime canvas layers, got ${JSON.stringify(canvas)}`);
    const point = await evaluate(`(() => { const node = document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
      if (!node) return null; const box = node.getBoundingClientRect();
      return { x: box.left + box.width / 2, y: box.top + box.height / 2 }; })()`);
    assert(point, 'band dropdown trigger is missing');
    await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: point.x, y: point.y, button: 'none' });
    await command('Input.dispatchMouseEvent', { type: 'mousePressed', x: point.x, y: point.y, button: 'left', clickCount: 1 });
    await command('Input.dispatchMouseEvent', { type: 'mouseReleased', x: point.x, y: point.y, button: 'left', clickCount: 1 });
    const opened = await evaluate(`new Promise((resolve, reject) => { const deadline = Date.now() + 3000;
      const poll = () => { const trigger = document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
        const options = document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-options]');
        if (trigger?.getAttribute('aria-expanded') === 'true' && options) { const box = options.getBoundingClientRect(); return resolve({ x: box.x, y: box.y, width: box.width, height: box.height, count: options.querySelectorAll('button').length }); }
        if (Date.now() >= deadline) return reject(new Error('band dropdown did not open')); setTimeout(poll, 20); }; poll(); })`);
    const menuShot = await command('Page.captureScreenshot', { format: 'png', fromSurface: true });
    const menuBytes = Buffer.from(menuShot.data, 'base64');
    const menuPath = path.join(outDir, 'menu-open.png'); write(menuPath, menuBytes);
    // Toggle the same real trigger a second time. The retained runtime uses a
    // synthetic event bridge for outside dismissal, while trigger clicks are
    // delivered by Chromium end to end and exercise the production handler.
    await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: point.x, y: point.y, button: 'none' });
    await command('Input.dispatchMouseEvent', { type: 'mousePressed', x: point.x, y: point.y, button: 'left', clickCount: 1 });
    await command('Input.dispatchMouseEvent', { type: 'mouseReleased', x: point.x, y: point.y, button: 'left', clickCount: 1 });
    await evaluate(`new Promise((resolve, reject) => { const deadline = Date.now() + 3000; const poll = () => {
      const trigger = document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
      if (trigger?.getAttribute('aria-expanded') === 'false') return resolve(true);
      if (Date.now() >= deadline) return reject(new Error('band dropdown did not close after trigger toggle')); setTimeout(poll, 20); }; poll(); })`);
    assert(sha256(settledBytes) === sha256(repeatBytes), 'settled screenshots were not byte-identical');
    assert(sha256(settledBytes) !== sha256(menuBytes), 'band interaction did not change the screenshot');
    const errors = await evaluate('window.__spectrRuntimeErrors || []');
    assert(errors.length === 0, `runtime errors: ${errors.join('; ')}`);
    assert(consoleErrors.length === 0, `console errors: ${consoleErrors.join('; ')}`);
    assert(networkFailures.length === 0, `network failures: ${networkFailures.join('; ')}`);
    const stderrDisposition = classifyChromeStderr(stderr);
    assert(stderrDisposition.unclassified.length === 0,
      `Chrome emitted unclassified stderr diagnostics: ${JSON.stringify(stderrDisposition.unclassified)}`);
    const viewport = await evaluate('({ width: Math.round(innerWidth), height: Math.round(innerHeight), dpr: devicePixelRatio })');
    assert(viewport.width === 1320 && viewport.height === 860 && viewport.dpr === dpr,
      `unexpected browser viewport ${JSON.stringify(viewport)}`);
    const info = await evaluate(`({ rootChildren: document.querySelector('#root')?.children.length || 0,
      canvases: document.querySelectorAll('canvas').length, analyzerEmissions: window.__spectrAnalyzerEmissions || 0,
      posts: (window.__spectrPosts || []).map(entry => entry.type) })`);
    const output = {
      viewport, info, canvas, first, settled,
      firstSequence, secondSequence,
      consoleErrors, networkFailures,
      interaction: { kind: 'band-dropdown', opened, closedWithTriggerToggle: true,
        menuScreenshot: { path: menuPath, sha256: sha256(menuBytes), bytes: menuBytes.length } },
      screenshots: {
        before: { path: beforePath, sha256: sha256(beforeBytes), bytes: beforeBytes.length },
        settled: { path: settledPath, sha256: sha256(settledBytes), bytes: settledBytes.length },
        settledRepeat: { path: path.join(outDir, 'settled-repeat.png'), sha256: sha256(repeatBytes), bytes: repeatBytes.length },
      },
      stderr, stderrDisposition,
    };
    return output;
  } finally {
    try { socket?.close(); } catch {}
    if (child && child.exitCode === null && child.signalCode === null) child.kill('SIGTERM');
    await new Promise(resolve => setTimeout(resolve, 250));
    if (child && child.exitCode === null && child.signalCode === null) child.kill('SIGKILL');
    if (server.closeAllConnections) server.closeAllConnections();
    await new Promise(resolve => server.close(() => resolve()));
    fs.rmSync(profile, { recursive: true, force: true });
  }
}

async function main(argv) {
  const args = parseArgs(argv);
  if (args.help) {
    console.log('usage: node tools/materialized_runtime_chromium_oracle.mjs --artifact FILE --fixture FILE --chrome PATH --out DIR [--dpr 2]');
    return;
  }
  for (const key of ['artifact', 'fixture', 'out']) assert(args[key], `--${key} is required`);
  const artifactPath = path.resolve(args.artifact);
  const fixturePath = path.resolve(args.fixture);
  const outDir = path.resolve(args.out);
  const chrome = path.resolve(args.chrome || CHROME_DEFAULT);
  const dpr = Number(args.dpr || 2);
  assert(fs.existsSync(chrome), `Chrome executable is missing: ${chrome}`);
  assert(Number.isFinite(dpr) && dpr > 0, '--dpr must be positive');
  prepareOutputDirectory(outDir);
  const artifactBytes = read(artifactPath);
  const fixtureBytes = read(fixturePath);
  let document;
  let fixture;
  try { document = JSON.parse(artifactBytes); } catch (error) { fail(`runtime artifact JSON is invalid: ${error.message}`); }
  try { fixture = JSON.parse(fixtureBytes); } catch (error) { fail(`analyzer fixture JSON is invalid: ${error.message}`); }
  assert(document.schema === 'pulp-materialized-browser-document-v1'
    && typeof document.html === 'string' && document.html.length > 0,
  'unsupported materialized runtime artifact');
  assert(Array.isArray(document.assets) && document.assets.length > 0,
    'materialized runtime has no embedded assets');
  validateFixture(fixture);
  const assets = new Map();
  for (const asset of document.assets) {
    assert(typeof asset.id === 'string' && /^[A-Za-z0-9._-]+$/.test(asset.id),
      `invalid materialized asset id: ${asset.id}`);
    assert(typeof asset.data_base64 === 'string' && asset.data_base64.length > 0,
      `materialized asset ${asset.id} has no bytes`);
    assets.set(asset.id, { body: Buffer.from(asset.data_base64, 'base64'), mime: asset.mime_type || 'application/octet-stream' });
  }
  let missingAsset = null;
  if (args.plantMissingAsset) {
    // The final (latin JetBrains Mono) face is requested by the visible UI,
    // so deleting it proves the HTTP asset envelope is actually guarded by
    // the zero-network-failure assertion rather than by an unused-file check.
    missingAsset = Array.from(assets.keys()).at(-1);
    assets.delete(missingAsset);
  }
  const html = injectBridge(document.html, `${reactVendorScripts()}${bridgeScript(fixture)}`);
  write(path.join(outDir, 'runtime-with-bridge.html'), html);
  const sourceSha256 = sha256(artifactBytes);
  const fixtureSha256 = sha256(fixtureBytes);
  const state = {
    schema: 'spectr-materialized-browser-state-v1', version: 1,
    source: { artifactSha256: sourceSha256, htmlSha256: sha256(Buffer.from(document.html)),
      assetCount: assets.size },
    viewport: { width: 1320, height: 860, deviceScaleFactor: dpr,
      png: { width: Math.round(1320 * dpr), height: Math.round(860 * dpr) } },
    analyzer: { fixtureSha256, contract: 'spectr-parity-v1', epoch: fixture.epoch,
      sequences: fixture.sequences, visibleSamples: fixture.visibleSamples,
      overviewSamples: fixture.overviewSamples },
    editor: { nVisible: 32, minHz: 20, maxHz: 20000, analyzerMode: 0, visualizationMode: 2 },
    authority: { browserArtifact: path.relative(REPO, artifactPath),
      nativeArtifact: path.relative(REPO, artifactPath),
      joinMode: 'same-materialized-runtime-diagnostic' },
  };
  state.stateSha256 = stateDigest(state);
  if (args.plantMissingAsset) {
    try {
      await browserCapture({ html, assets, fixture, chrome, outDir, dpr });
      fail(`planted missing asset unexpectedly passed: ${missingAsset}`);
    } catch (error) {
      const message = String(error.message || error);
      assert(message.includes('network failures'),
        `planted missing asset failed for an unrelated reason: ${message}`);
      process.stdout.write(`PASS: missing materialized asset rejected by network-failure gate (${missingAsset})\n`);
      return;
    }
  }
  const browser = await browserCapture({ html, assets, fixture, chrome, outDir, dpr });
  const receipt = {
    schema: SCHEMA, version: 1,
    source: { artifact: artifactPath, artifactSha256: sourceSha256,
      htmlSha256: state.source.htmlSha256, fixture: fixturePath, fixtureSha256 },
    state, stateSha256: state.stateSha256,
    authority: state.authority,
    checks: { strict: true, readiness: true, analyzerFixture: true,
      embeddedAssetsServed: true, consoleNetworkClean: true,
      realInteraction: true, repeatedSettledCapture: true,
      nativeParity: false },
    browser,
    scope: { runtimeArtifactUnchanged: true, productionCutover: false,
      fullNativeParity: false },
  };
  write(path.join(outDir, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`);
  process.stdout.write(`${JSON.stringify(receipt, null, 2)}\n`);
}

main(process.argv.slice(2)).catch(error => { console.error(error.stack || error.message); process.exit(1); });
