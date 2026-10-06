import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const [sourcePath, chromePath, outputArg] = process.argv.slice(2);
assert(sourcePath && chromePath,
  'usage: test_browser_fidelity.mjs resources/editor.html CHROME [OUTPUT_DIR]');
const source = fs.readFileSync(sourcePath, 'utf8');
const output = path.resolve(outputArg || path.join(process.cwd(), 'browser-fidelity-artifacts'));
fs.mkdirSync(output, { recursive: true });

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
      });
    }
    return Promise.resolve({ ok: true, payload: { ok: true } });
  },
};
window.confirm = () => true;
</script>`;

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const launch = async (htmlPath, screenshotPath) => {
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-browser-fidelity-profile-'));
  const port = 19000 + (process.pid % 1000);
  const chrome = spawn(chromePath, [
    '--headless=new', '--disable-gpu', '--disable-background-networking',
    '--disable-component-update', '--disable-domain-reliability', '--disable-sync',
    '--no-first-run', '--no-default-browser-check', '--allow-file-access-from-files',
    '--run-all-compositor-stages-before-draw', '--window-size=1320,860',
    '--remote-debugging-address=127.0.0.1', `--remote-debugging-port=${port}`,
    `--user-data-dir=${profile}`, 'about:blank',
  ], { stdio: 'ignore' });
  try {
    const deadline = Date.now() + 15000;
    let page;
    while (!page && Date.now() < deadline) {
      try {
        const pages = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
        page = pages.find(item => item.type === 'page');
      } catch {}
      if (!page) await sleep(50);
    }
    assert(page?.webSocketDebuggerUrl, 'Chrome DevTools endpoint did not start');
    const socket = new WebSocket(page.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
      socket.addEventListener('open', resolve, { once: true });
      socket.addEventListener('error', reject, { once: true });
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
      pending.set(id, { resolve, reject });
      socket.send(JSON.stringify({ id, method, params }));
    });
    const evaluate = async expression => {
      const result = await command('Runtime.evaluate', {
        expression, awaitPromise: true, returnByValue: true,
      });
      if (result.exceptionDetails)
        throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
      return result.result.value;
    };
    await command('Runtime.enable');
    await command('Network.enable');
    await command('Page.enable');
    const url = pathToFileURL(path.resolve(htmlPath)).href;
    await command('Page.navigate', { url });
    const stop = Date.now() + 20000;
    let marker;
    while (Date.now() < stop) {
      marker = await evaluate(`(() => {
        const node = document.querySelector('[data-spectr-bank-ready="true"]');
        return node ? { text: node.textContent || '', count: document.querySelectorAll('[data-spectr-bank-ready="true"]').length } : null;
      })()`);
      if (marker) break;
      await sleep(50);
    }
    if (!marker) {
      socket.close();
      throw new Error('editor did not expose data-spectr-bank-ready="true"');
    }
    const dom = await evaluate(`JSON.stringify({
      title: document.title,
      ready: document.querySelectorAll('[data-spectr-bank-ready="true"]').length,
      bodyText: document.body.innerText.slice(0, 2000),
      rootChildren: document.getElementById('root')?.children.length || 0,
    })`);
    const screenshot = await command('Page.captureScreenshot', { format: 'png', fromSurface: true });
    fs.writeFileSync(screenshotPath, Buffer.from(screenshot.data, 'base64'));
    await command('Browser.close').catch(() => {});
    socket.close();
    return { marker, dom: JSON.parse(dom), consoleErrors, networkFailures, screenshotSha256: crypto.createHash('sha256').update(fs.readFileSync(screenshotPath)).digest('hex') };
  } finally {
    chrome.kill('SIGTERM');
    try { fs.rmSync(profile, { recursive: true, force: true }); } catch {}
  }
};

const run = async () => {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-browser-fidelity-'));
  try {
    const goodPath = path.join(temp, 'editor.html');
    fs.writeFileSync(goodPath, bridge + source);
    const first = await launch(goodPath, path.join(output, 'editor.png'));
    const second = await launch(goodPath, path.join(output, 'editor-repeat.png'));
    assert.deepEqual(first.marker, second.marker, 'DOM readiness marker is not deterministic');
    assert.equal(first.dom.title, 'Spectr — zoomable filter bank');
    assert.equal(first.dom.ready, 1);
    assert.equal(first.consoleErrors.length, 0, `console errors: ${first.consoleErrors.join('; ')}`);
    assert.equal(first.networkFailures.length, 0, `network failures: ${first.networkFailures.join('; ')}`);
    // Break the actual React mount inside the embedded editor bundle. A
    // mutation that matches only the outer wait helper would make this
    // control blind.
    const broken = source.replaceAll('ReactDOM.createRoot', 'ReactDOM.brokenRoot');
    assert.notEqual(broken, source, 'broken-editor mutation matched nothing');
    const brokenPath = path.join(temp, 'broken-editor.html');
    fs.writeFileSync(brokenPath, bridge + broken);
    let rejected = false;
    try { await launch(brokenPath, path.join(output, 'broken-editor.png')); }
    catch (error) { rejected = true; console.log(`negative control rejected: ${error.message}`); }
    assert(rejected, 'broken-editor negative control was accepted');
    const receipt = { source: path.resolve(sourcePath), positive: first, repeat: second,
      deterministicDomMarker: true, negativeControl: 'rejected' };
    fs.writeFileSync(path.join(output, 'receipt.json'), JSON.stringify(receipt, null, 2) + '\n');
    console.log(JSON.stringify(receipt, null, 2));
  } finally { fs.rmSync(temp, { recursive: true, force: true }); }
};
await run();
