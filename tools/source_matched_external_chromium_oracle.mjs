#!/usr/bin/env node

// Capture editor.html in an independent Chromium process.  This deliberately
// does not call pulp-import-design: it is an external browser oracle for
// source-to-pixels and one real DOM interaction.  The importer capture can be
// supplied as --importer-capture; its provenance source SHA must match before
// any screenshot comparison is reported.

import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { pathToFileURL } from 'node:url';

const argv = process.argv.slice(2);
const take = flag => {
  const index = argv.indexOf(flag);
  if (index < 0) return undefined;
  const value = argv[index + 1];
  if (!value || value.startsWith('--')) throw new Error(`${flag} requires a value`);
  return value;
};
const sourcePath = take('--source') || argv[0];
const chromePath = take('--chrome') || argv[1];
const outputPath = take('--output') || argv[2]
  || path.join(process.cwd(), 'external-chromium-oracle-artifacts');
const importerCapturePath = take('--importer-capture');
const skipNegative = argv.includes('--skip-negative');
const plantResizeSensitive = argv.includes('--plant-resize-sensitive');

if (!sourcePath || !chromePath) {
  console.error('usage: source_matched_external_chromium_oracle.mjs --source editor.html --chrome CHROME [--output DIR] [--importer-capture DIR] [--skip-negative] [--plant-resize-sensitive]');
  process.exit(2);
}

const source = fs.readFileSync(sourcePath);
const sourceText = source.toString('utf8');
const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');
const sourceSha256 = sha256(source);
const output = path.resolve(outputPath);
fs.mkdirSync(output, { recursive: true });

// The source expects the same minimal host surface as the browser-fidelity
// receipt.  It is prepended in a temporary file; sourceSha256 always refers to
// the unmodified bytes supplied by the caller.
const bridge = `<script>
window.__spectrBrowserPosts = [];
window.__spectrBrowserListeners = Object.create(null);
window.pulp = {
  on(type, callback) {
    (window.__spectrBrowserListeners[type] ||= new Set()).add(callback);
    return () => window.__spectrBrowserListeners[type].delete(callback);
  },
  postMessage(type, payload) {
    window.__spectrBrowserPosts.push({ type, payload });
    if (type === 'editor_ready') {
      queueMicrotask(() => {
        const n = 32;
        const trace = count => Array.from({ length: count }, (_, index) => {
          const x = index / Math.max(1, count - 1);
          const left = Math.exp(-((x - 0.22) ** 2) / 0.008) * 28;
          const middle = Math.exp(-((x - 0.52) ** 2) / 0.018) * 38;
          const right = Math.exp(-((x - 0.82) ** 2) / 0.012) * 23;
          return -96 + left + middle + right;
        });
        const state = {
          n_visible: n, gain_db: new Array(n).fill(0), muted: new Array(n).fill(false),
          min_hz: 20, max_hz: 20000, motion_mode: 0, analyzer_mode: 0,
          edit_mode: 0, visualization_mode: 2, revision: 1,
          snapshots: { A: { populated: false }, B: { populated: false } },
          patterns_json: JSON.stringify({ format: 'spectr.patterns', version: 1,
            default_id: 'factory:flat', patterns: [] }),
        };
        for (const callback of window.__spectrBrowserListeners.processing_state_hydrate || [])
          callback({ type: 'processing_state_hydrate', payload: state });
        const analyzer = {
          schema_version: 1, epoch: 0, sequence_number: 0, dropped_frames: 0,
          source_channels: 2, fft_size: 512, sample_rate: 48000,
          floor_db: -120, ceiling_db: 24,
          visible: { min_hz: 20, max_hz: 20000, magnitude_db: trace(321) },
          overview: { min_hz: 20, max_hz: 20000, magnitude_db: trace(121) },
        };
        for (const callback of window.__spectrBrowserListeners.analyzer_frame || [])
          callback({ type: 'analyzer_frame', payload: analyzer });
      });
    }
    return Promise.resolve({ ok: true, payload: { ok: true } });
  },
};
window.confirm = () => true;
</script>`;

class OracleError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

const pngSize = bytes => {
  // PNG IHDR stores big-endian width and height at offsets 16 and 20.
  if (bytes.length < 24 || bytes.readUInt32BE(0) !== 0x89504e47)
    throw new Error('not a PNG');
  return { width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20) };
};

async function launch(htmlPath, outputPrefix) {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-external-oracle-'));
  const chrome = spawn(chromePath, [
    '--headless=new', '--disable-gpu', '--disable-background-networking',
    '--disable-component-update', '--disable-domain-reliability', '--disable-sync',
    '--no-first-run', '--no-default-browser-check', '--allow-file-access-from-files',
    '--run-all-compositor-stages-before-draw', '--window-size=1320,860',
    '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=0',
    `--user-data-dir=${profile}`, 'about:blank',
  ], { stdio: 'ignore' });
  let socket;
  try {
    let page;
    const deadline = Date.now() + 15000;
    while (!page && Date.now() < deadline) {
      try {
        const active = fs.readFileSync(path.join(profile, 'DevToolsActivePort'), 'utf8').split('\n');
        const port = Number(active[0]);
        if (Number.isInteger(port) && port > 0) {
          const pages = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
          page = pages.find(item => item.type === 'page');
        }
      } catch {}
      if (!page) await delay(50);
    }
    if (!page?.webSocketDebuggerUrl)
      throw new OracleError('chrome-start', 'Chrome DevTools endpoint did not start');
    socket = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Chrome DevTools WebSocket did not open')), 10000);
      socket.addEventListener('open', () => { clearTimeout(timer); resolve(); }, { once: true });
      socket.addEventListener('error', error => { clearTimeout(timer); reject(error); }, { once: true });
    });
    let nextId = 1;
    const pending = new Map();
    const consoleErrors = [];
    const networkFailures = [];
    socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (message.method === 'Runtime.consoleAPICalled'
          && ['error', 'assert'].includes(message.params.type))
        consoleErrors.push(message.params.args.map(arg => arg.value ?? arg.description ?? '').join(' '));
      if (message.method === 'Runtime.exceptionThrown')
        consoleErrors.push(message.params.exceptionDetails?.text || 'uncaught exception');
      if (message.method === 'Network.loadingFailed')
        networkFailures.push(`${message.params.errorText}: ${message.params.url}`);
      if (!message.id || !pending.has(message.id)) return;
      const item = pending.get(message.id);
      pending.delete(message.id);
      if (message.error) item.reject(new Error(JSON.stringify(message.error)));
      else item.resolve(message.result);
    });
    const command = (method, params = {}) => new Promise((resolve, reject) => {
      const id = nextId++;
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(new Error(`Chrome DevTools command timed out: ${method}`));
      }, 10000);
      pending.set(id, {
        resolve: result => { clearTimeout(timer); resolve(result); },
        reject: error => { clearTimeout(timer); reject(error); },
      });
      try { socket.send(JSON.stringify({ id, method, params })); }
      catch (error) { pending.delete(id); clearTimeout(timer); reject(error); }
    });
    const evaluate = async expression => {
      const result = await command('Runtime.evaluate', {
        expression, awaitPromise: true, returnByValue: true,
      });
      if (result.exceptionDetails)
        throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
      return result.result.value;
    };
    const waitFor = async (expression, description) => {
      const stop = Date.now() + 20000;
      while (Date.now() < stop) {
        const value = await evaluate(expression);
        if (value) return value;
        await delay(50);
      }
      throw new OracleError('state-timeout', `${description} did not become true`);
    };
    const viewportSize = () => evaluate(`JSON.stringify({
      width: Math.round(window.innerWidth), height: Math.round(window.innerHeight),
    })`).then(JSON.parse);
    const settleResize = async (width, height) => {
      await command('Emulation.setDeviceMetricsOverride', {
        width, height, deviceScaleFactor: 1, mobile: false,
      });
      await waitFor(`window.innerWidth === ${width} && window.innerHeight === ${height}`,
        `viewport resize ${width}x${height}`);
      // Layout and canvas effects consume the new bounds on the next frames.
      await evaluate(`new Promise(resolve => requestAnimationFrame(() =>
        requestAnimationFrame(resolve)))`);
    };
    const capture = async (name, staleClip = false) => {
      const viewport = await viewportSize();
      const clip = staleClip
        ? { x: 0, y: 0, width: viewport.width + 330, height: viewport.height + 215, scale: 1 }
        : { x: 0, y: 0, width: viewport.width, height: viewport.height, scale: 1 };
      if (clip.width > viewport.width || clip.height > viewport.height)
        throw new OracleError('resize-sensitive',
          `capture clip ${clip.width}x${clip.height} exceeds viewport ${viewport.width}x${viewport.height}`);
      const result = await command('Page.captureScreenshot', {
        format: 'png', fromSurface: true, captureBeyondViewport: false, clip,
      });
      const file = path.join(output, `${outputPrefix}-${name}.png`);
      fs.writeFileSync(file, Buffer.from(result.data, 'base64'));
      return { path: file, sha256: sha256(fs.readFileSync(file)), size: pngSize(fs.readFileSync(file)) };
    };
    await command('Runtime.enable');
    await command('Network.enable');
    await command('Page.enable');
    await command('Page.navigate', { url: pathToFileURL(path.resolve(htmlPath)).href });
    await waitFor(
      `Boolean(document.querySelector('[data-spectr-bank-ready="true"]'))`,
      'editor readiness marker',
    );
    const initial = await evaluate(`JSON.stringify({
      title: document.title,
      ready: document.querySelectorAll('[data-spectr-bank-ready="true"]').length,
      rootChildren: document.getElementById('root')?.children.length || 0,
      menu: document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]')?.getAttribute('aria-expanded'),
    })`).then(JSON.parse);
    const initialViewport = await viewportSize();
    await settleResize(1000, 600);
    await settleResize(initialViewport.width, initialViewport.height);
    if (plantResizeSensitive) {
      await settleResize(990, 645);
      await capture('resize-sensitive', true);
    }
    const before = await capture('before');
    if (before.size.width !== initialViewport.width || before.size.height !== initialViewport.height)
      throw new OracleError('capture-size',
        `capture ${before.size.width}x${before.size.height} != viewport ${initialViewport.width}x${initialViewport.height}`);

    // Use actual CDP mouse input.  Calling HTMLElement.click() would prove
    // only the JS handler, while this also exercises hit testing and dispatch.
    const point = await evaluate(`(() => {
      const node = document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
      if (!node) return null;
      const box = node.getBoundingClientRect();
      return { x: box.left + box.width / 2, y: box.top + box.height / 2,
        expanded: node.getAttribute('aria-expanded') };
    })()`);
    if (!point) throw new OracleError('interaction-target-missing', 'band menu trigger was not found');
    await command('Input.dispatchMouseEvent', { type: 'mouseMoved', x: point.x, y: point.y, button: 'none' });
    await command('Input.dispatchMouseEvent', { type: 'mousePressed', x: point.x, y: point.y, button: 'left', clickCount: 1 });
    await command('Input.dispatchMouseEvent', { type: 'mouseReleased', x: point.x, y: point.y, button: 'left', clickCount: 1 });
    const opened = await waitFor(`(() => {
      const node = document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
      return node?.getAttribute('aria-expanded') === 'true'
        && document.querySelectorAll('[data-spectr-menu-root="bands"] [data-spectr-menu-options] button').length > 0;
    })()`, 'band menu open state');
    const openDom = await evaluate(`JSON.stringify({
      expanded: document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]')?.getAttribute('aria-expanded'),
      optionCount: document.querySelectorAll('[data-spectr-menu-root="bands"] [data-spectr-menu-options] button').length,
      rect: (() => {
        const node = document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-options]');
        if (!node) return null;
        const box = node.getBoundingClientRect();
        return { x: box.x, y: box.y, width: box.width, height: box.height };
      })(),
    })`).then(JSON.parse);
    const menuOpen = await capture('menu-open');

    const pixelDiff = spawnSync('python3', ['-c', String.raw`
from PIL import Image, ImageChops
import json, sys
a = Image.open(sys.argv[1]).convert('RGB')
b = Image.open(sys.argv[2]).convert('RGB')
if a.size != b.size:
    raise SystemExit('before/menu-open dimensions differ')
d = ImageChops.difference(a, b)
pixels = list(d.getdata())
changed = [index for index, pixel in enumerate(pixels) if pixel != (0, 0, 0)]
rect = json.loads(sys.argv[3])
inside = 0
for index in changed:
    x = index % a.width
    y = index // a.width
    if rect['x'] <= x < rect['x'] + rect['width'] and rect['y'] <= y < rect['y'] + rect['height']:
        inside += 1
print(json.dumps({
  'changed_pixels': len(changed),
  'changed_fraction': len(changed) / (a.width * a.height),
  'changed_inside_menu': inside,
  'changed_inside_fraction': inside / (a.width * a.height),
}))
`, before.path, menuOpen.path, JSON.stringify(openDom.rect)], { encoding: 'utf8' });
    if (pixelDiff.status !== 0) throw new Error(pixelDiff.stderr || 'before/menu-open pixel diff failed');
    const visualStateChanged = JSON.parse(pixelDiff.stdout);
    assert(visualStateChanged.changed_inside_menu > 100,
      'menu interaction did not change pixels inside the opened menu bounds');

    await command('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27 });
    await command('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27, nativeVirtualKeyCode: 27 });
    const closed = await waitFor(`(() => {
      const node = document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]');
      return node?.getAttribute('aria-expanded') === 'false'
        && document.querySelectorAll('[data-spectr-menu-root="bands"] [data-spectr-menu-options]').length === 0;
    })()`, 'band menu closed state');
    const closedDom = await evaluate(`JSON.stringify({
      expanded: document.querySelector('[data-spectr-menu-root="bands"] [data-spectr-menu-trigger]')?.getAttribute('aria-expanded'),
      optionCount: document.querySelectorAll('[data-spectr-menu-root="bands"] [data-spectr-menu-options] button').length,
    })`).then(JSON.parse);
    const after = await capture('after-close');
    await command('Browser.close').catch(() => {});
    return {
      initial, before, menuOpen, after, openDom, closedDom,
      interactionCount: 2,
      visualStateChanged,
      consoleErrors, networkFailures,
      stateChanged: initial.menu !== openDom.expanded && openDom.expanded === 'true'
        && closedDom.expanded === 'false' && openDom.optionCount > 0 && closedDom.optionCount === 0,
    };
  } finally {
    try { socket?.close(); } catch {}
    chrome.kill('SIGTERM');
    try { fs.rmSync(profile, { recursive: true, force: true }); } catch {}
  }
}

function writeSourceFile(file, bytes) {
  fs.writeFileSync(file, Buffer.concat([Buffer.from(bridge), bytes]));
}

async function main() {
  const chromeVersion = spawnSync(chromePath, ['--version'], { encoding: 'utf8' }).stdout?.trim() || 'unknown';
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-external-oracle-source-'));
  try {
    const good = path.join(temp, 'editor.html');
    writeSourceFile(good, source);
    if (plantResizeSensitive) {
      try {
        await launch(good, 'external-resize-negative');
        throw new Error('resize-sensitive source unexpectedly captured');
      } catch (error) {
        if (!(error instanceof OracleError) || error.code !== 'resize-sensitive') throw error;
        const receipt = {
          schema: 'spectr-source-matched-external-chromium-oracle-v1',
          source: path.resolve(sourcePath), sourceSha256, chrome: chromeVersion,
          capturePolicy: { clip: 'current CSS viewport', captureBeyondViewport: false,
            resizeSettleFrames: 2 },
          negative: { status: 'rejected', errorCode: error.code, message: error.message },
        };
        fs.writeFileSync(path.join(output, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`);
        console.log(JSON.stringify(receipt, null, 2));
        return;
      }
    }
    const positive = await launch(good, 'external');
    assert.equal(positive.initial.ready, 1);
    assert.equal(positive.initial.rootChildren, 1);
    assert.equal(positive.consoleErrors.length, 0, `console errors: ${positive.consoleErrors.join('; ')}`);
    assert.equal(positive.networkFailures.length, 0, `network failures: ${positive.networkFailures.join('; ')}`);
    assert.equal(positive.interactionCount, 2);
    assert.equal(positive.stateChanged, true, 'menu interaction did not produce the expected open/closed state transition');
    assert.notEqual(positive.before.sha256, positive.menuOpen.sha256, 'menu interaction did not change the screenshot');

    let reference;
    if (importerCapturePath) {
      const captureFile = path.join(path.resolve(importerCapturePath), 'capture.json');
      const capture = JSON.parse(fs.readFileSync(captureFile, 'utf8'));
      const importerSha = capture.provenance?.source?.sha256;
      assert.equal(importerSha, sourceSha256,
        `importer capture source SHA ${importerSha} does not match external source ${sourceSha256}`);
      const browserFile = path.join(path.resolve(importerCapturePath), 'browser.png');
      const browserBytes = fs.readFileSync(browserFile);
      reference = {
        capture: captureFile,
        sourceSha256: importerSha,
        browser: browserFile,
        browserSha256: sha256(browserBytes),
        browserSize: pngSize(browserBytes),
        sameDimensions: JSON.stringify(pngSize(browserBytes)) === JSON.stringify(positive.before.size),
        exactBeforeBytes: sha256(browserBytes) === positive.before.sha256,
      };
      // The importer may capture the full logical document at DPR 2 while
      // this oracle captures the visible viewport at DPR 1.  Record that
      // distinction; source SHA equality is the hard compatibility gate.
    }

    let negative;
    if (!skipNegative) {
      const brokenSource = sourceText.replaceAll('ReactDOM.createRoot', 'ReactDOM.brokenRoot');
      assert.notEqual(brokenSource, sourceText, 'mount mutation matched nothing');
      const broken = path.join(temp, 'broken-editor.html');
      writeSourceFile(broken, Buffer.from(brokenSource));
      try {
        await launch(broken, 'external-broken');
        throw new Error('broken source unexpectedly became ready');
      } catch (error) {
        if (!(error instanceof OracleError) || error.code !== 'state-timeout') throw error;
        negative = { status: 'rejected', errorCode: error.code, mutatedSourceSha256: sha256(Buffer.from(brokenSource)) };
      }
    }
    const receipt = {
      schema: 'spectr-source-matched-external-chromium-oracle-v1',
      source: path.resolve(sourcePath), sourceSha256, chrome: chromeVersion,
      capturePolicy: { clip: 'current CSS viewport', captureBeyondViewport: false,
        resizeSettleFrames: 2 },
      hostFixture: 'deterministic-analyzer-frame-v1',
      positive, reference, negative,
    };
    fs.writeFileSync(path.join(output, 'receipt.json'), `${JSON.stringify(receipt, null, 2)}\n`);
    console.log(JSON.stringify(receipt, null, 2));
  } finally {
    fs.rmSync(temp, { recursive: true, force: true });
  }
}

await main();
