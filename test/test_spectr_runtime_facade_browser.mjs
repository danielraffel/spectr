#!/usr/bin/env node
/**
 * Exercise the generated facade in a VM and in real headless Chromium.
 *
 * The maintained service source is run once with the generated facade and
 * once with the old direct dispatcher. Valid commands must produce identical
 * C++ envelopes and responses in both realms. An unknown command must be
 * rejected before reaching the processor stub. The browser run is a scoped
 * adapter parity receipt; it does not claim full editor or native pixel
 * parity.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import { spawn } from 'node:child_process';

const argv = process.argv.slice(2);
const servicesPath = argv[0];
const chromePath = argv[1];
if (!servicesPath || !chromePath) {
  console.error('usage: node test/test_spectr_runtime_facade_browser.mjs SERVICES CHROME');
  process.exit(2);
}
const source = fs.readFileSync(servicesPath, 'utf8');
const BEGIN = '  // BEGIN GENERATED SPECTR RUNTIME CLIENT';
const END = '  // END GENERATED SPECTR RUNTIME CLIENT';
const begin = source.indexOf(BEGIN);
const end = source.indexOf(END, begin);
if (begin < 0 || end < begin) throw new Error('generated runtime-client markers are missing');
const inlineFree = source.slice(0, begin) + source.slice(end + END.length + 1);
const route = '  const __spectrRuntimeClient = __spectrCreateRuntimeClient(dispatchCore);\n'
  + '  const dispatch = (type, payload, id) =>\n'
  + '    __spectrRuntimeClient.request(type, payload, id);';
if (!inlineFree.includes(route)) throw new Error('generated runtime-client route is missing');
const baselineSource = inlineFree.replace(route, '  const dispatch = dispatchCore;');
if (baselineSource === source) throw new Error('baseline transform did not change the source');

const COMMANDS = [
  ['processing_state_get', {}, 'state'],
  ['processing_state_set', { revision: 4 }, 'set'],
  ['undo', {}, 'undo'],
  ['redo', {}, 'redo'],
  ['macro_set_members', { index: 2, slots: [1] }, 'macro'],
  ['output_levels_get', {}, 'levels'],
  ['param_set', { id: 4, value: 0.5 }, 'param'],
];

function makeBox() {
  const calls = [];
  const box = {
    Map, Set, Array, Object, Number, String, Boolean, Math, JSON, Promise,
    console: { log() {}, warn() {}, error() {} },
    requestAnimationFrame(fn) { fn(); },
    __spectrEditorDispatch(raw) {
      const message = JSON.parse(raw);
      calls.push(message);
      return JSON.stringify({ ok: true, echo: message });
    },
  };
  box.globalThis = box;
  box.window = box;
  return { box, calls };
}

async function runRealm(code) {
  const { box, calls } = makeBox();
  vm.runInNewContext(code, box, { filename: 'spectr-native-services.js' });
  const results = {};
  for (const [type, payload, id] of COMMANDS)
    results[type] = await box.pulp.postMessage(type, payload, id);
  let rejected = false;
  try { await box.pulp.postMessage('unknown_command', {}, 'bad'); }
  catch (error) { rejected = /unknown Spectr command/.test(String(error.message)); }
  return { calls, trace: box.__spectrNativeDispatchTrace, results, unknown: { rejected, calls: calls.length } };
}

function assertEqual(label, left, right) {
  if (JSON.stringify(left) !== JSON.stringify(right))
    throw new Error(`${label} mismatch:\n${JSON.stringify({ left, right }, null, 2)}`);
}

function withoutUnknown(result) {
  const keep = message => message?.type !== 'unknown_command';
  return { ...result, calls: result.calls.filter(keep), trace: result.trace.filter(keep) };
}

function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
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

function browserPage(service) {
  const probe = `
window.__spectrProbe = async () => {
  const results = {};
  const commands = ${JSON.stringify(COMMANDS)};
  for (const [type, payload, id] of commands)
    results[type] = await window.pulp.postMessage(type, payload, id);
  let rejected = false;
  try { await window.pulp.postMessage('unknown_command', {}, 'bad'); }
  catch (error) { rejected = /unknown Spectr command/.test(String(error.message)); }
  document.body.textContent = 'Spectr runtime facade probe';
  return { calls: window.__spectrBrowserCalls, trace: window.__spectrNativeDispatchTrace,
    results, unknown: { rejected, calls: window.__spectrBrowserCalls.length } };
};`;
  return `<!doctype html><meta charset="utf-8"><body></body><script>
window.__spectrBrowserCalls = [];
window.__spectrEditorDispatch = raw => {
  const message = JSON.parse(raw); window.__spectrBrowserCalls.push(message);
  return JSON.stringify({ ok: true, echo: message });
};
${service}
${probe}
</script>`;
}

async function browserRun(chrome) {
  const pages = { '/baseline.html': browserPage(baselineSource), '/facade.html': browserPage(source) };
  const server = http.createServer((request, response) => {
    const body = pages[new URL(request.url, 'http://127.0.0.1').pathname];
    if (!body) { response.writeHead(404); response.end(); return; }
    response.writeHead(200, { 'content-type': 'text/html; charset=utf-8', connection: 'close' });
    response.end(body);
  });
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const port = await reservePort();
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-runtime-facade-chrome-'));
  const child = spawn(chrome, ['--headless=new', '--disable-gpu', '--disable-background-networking',
    '--disable-component-update', '--disable-sync', '--no-first-run', '--no-default-browser-check',
    '--remote-debugging-address=127.0.0.1', `--remote-debugging-port=${port}`,
    `--user-data-dir=${profile}`, '--window-size=900,300', 'about:blank'],
  { stdio: ['ignore', 'ignore', 'pipe'] });
  let stderr = '';
  child.stderr.on('data', chunk => { stderr += chunk; });
  let socket;
  try {
    let websocketUrl;
    const deadline = Date.now() + 15000;
    while (!websocketUrl && Date.now() < deadline) {
      try { websocketUrl = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(p => p.type === 'page')?.webSocketDebuggerUrl; }
      catch {}
      if (!websocketUrl) await new Promise(resolve => setTimeout(resolve, 50));
    }
    if (!websocketUrl) throw new Error(`Chrome DevTools endpoint did not start: ${stderr.trim()}`);
    socket = new WebSocket(websocketUrl);
    await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
    const pending = new Map(); let nextId = 1;
    socket.addEventListener('message', event => {
      const message = JSON.parse(event.data);
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
      if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text || 'browser evaluation failed');
      return result.result.value;
    };
    await command('Page.enable'); await command('Runtime.enable');
    const receipts = {};
    for (const name of ['baseline', 'facade']) {
      const url = `http://127.0.0.1:${server.address().port}/${name}.html`;
      await command('Page.navigate', { url });
      const ready = Date.now() + 10000;
      while (Date.now() < ready && !(await evaluate(`location.href === ${JSON.stringify(url)} && typeof window.__spectrProbe === 'function'`))) await new Promise(resolve => setTimeout(resolve, 50));
      if (!(await evaluate(`location.href === ${JSON.stringify(url)} && typeof window.__spectrProbe === 'function'`))) throw new Error(`${name} browser service did not load`);
      const result = await evaluate('window.__spectrProbe()');
      const screenshot = await command('Page.captureScreenshot', { format: 'png' });
      const bytes = Buffer.from(screenshot.data, 'base64');
      receipts[name] = { result, screenshot: { sha256: sha256(bytes), bytes: bytes.length } };
    }
    assertEqual('browser baseline/facade result',
      { calls: withoutUnknown(receipts.baseline.result).calls, trace: withoutUnknown(receipts.baseline.result).trace,
        results: receipts.baseline.result.results },
      { calls: withoutUnknown(receipts.facade.result).calls, trace: withoutUnknown(receipts.facade.result).trace,
        results: receipts.facade.result.results });
    if (receipts.baseline.result.unknown.rejected
      || receipts.baseline.result.unknown.calls !== withoutUnknown(receipts.baseline.result).calls.length + 1)
      throw new Error('browser baseline unknown-command forwarding control failed');
    const vmFacade = await runRealm(source);
    assertEqual('VM/browser facade result',
      { calls: withoutUnknown(vmFacade).calls, trace: withoutUnknown(vmFacade).trace, results: vmFacade.results },
      { calls: withoutUnknown(receipts.facade.result).calls, trace: withoutUnknown(receipts.facade.result).trace,
        results: receipts.facade.result.results });
    if (!receipts.facade.result.unknown.rejected || receipts.facade.result.unknown.calls !== receipts.facade.result.calls.length)
      throw new Error('browser unknown-command negative control failed');
    return receipts;
  } finally {
    try { socket?.close(); } catch {}
    if (!child.killed) child.kill('SIGTERM');
    await new Promise(resolve => setTimeout(resolve, 200));
    if (!child.killed) child.kill('SIGKILL');
    if (server.closeAllConnections) server.closeAllConnections();
    await new Promise(resolve => server.close(() => resolve()));
    fs.rmSync(profile, { recursive: true, force: true });
  }
}

const vmBaseline = await runRealm(baselineSource);
const vmFacade = await runRealm(source);
assertEqual('VM baseline/facade result',
  { calls: withoutUnknown(vmBaseline).calls, trace: withoutUnknown(vmBaseline).trace, results: vmBaseline.results },
  { calls: withoutUnknown(vmFacade).calls, trace: withoutUnknown(vmFacade).trace, results: vmFacade.results });
if (vmBaseline.unknown.rejected || vmBaseline.unknown.calls !== withoutUnknown(vmBaseline).calls.length + 1)
  throw new Error('VM baseline unknown-command forwarding control failed');
if (!vmFacade.unknown.rejected || vmFacade.unknown.calls !== vmFacade.calls.length)
  throw new Error('VM unknown-command negative control failed');
const browser = await browserRun(chromePath);
console.log(JSON.stringify({ schema: 'spectr-runtime-facade-browser-parity-v1',
  checks: { vm_results_equal: true, browser_results_equal: true,
    vm_browser_equal: true, unknown_command_rejected: true }, browser }, null, 2));
