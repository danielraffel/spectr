import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const [shippingHtml, canonicalHtml, chromePath, requestedOutput, mutation] = process.argv.slice(2);
assert(shippingHtml && canonicalHtml && chromePath,
  'usage: test_preset_parity_browser.mjs SHIPPING_HTML CANONICAL_HTML|--shipping-only CHROME [OUTPUT_DIR] [--plant-overlap]');
const comparisonEnabled = canonicalHtml !== '--shipping-only';

const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-preset-parity-'));
const output = requestedOutput ? path.resolve(requestedOutput) : scratch;
fs.mkdirSync(output, { recursive: true });
const materializedScratch = path.join(scratch, 'shipping-materialized.html');
if (shippingHtml.endsWith('.json')) {
  const materialized = JSON.parse(fs.readFileSync(shippingHtml, 'utf8'));
  assert.equal(typeof materialized.html, 'string',
    'materialized shipping document has no HTML');
  fs.writeFileSync(materializedScratch, materialized.html);
}
const shippingPath = shippingHtml.endsWith('.json') ? materializedScratch : shippingHtml;
const profile = path.join(scratch, 'chrome-profile');
const port = 19000 + (process.pid % 1000);
const chrome = spawn(chromePath, [
  '--headless=new', '--disable-gpu', '--disable-background-networking',
  '--disable-component-update', '--disable-domain-reliability', '--disable-sync',
  '--no-first-run', '--no-default-browser-check', '--allow-file-access-from-files',
  '--remote-debugging-address=127.0.0.1', `--remote-debugging-port=${port}`,
  `--user-data-dir=${profile}`, '--window-size=1320,860', 'about:blank',
], { stdio: 'ignore' });

const sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const deadline = Date.now() + 15000;
let websocketUrl;
while (!websocketUrl && Date.now() < deadline) {
  try {
    const pages = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
    websocketUrl = pages.find(page => page.type === 'page')?.webSocketDebuggerUrl;
  } catch {}
  if (!websocketUrl) await sleep(50);
}
assert(websocketUrl, 'Chrome DevTools endpoint did not start');

const socket = new WebSocket(websocketUrl);
await new Promise((resolve, reject) => {
  socket.addEventListener('open', resolve, { once: true });
  socket.addEventListener('error', reject, { once: true });
});
let nextId = 1;
const pending = new Map();
socket.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (!message.id || !pending.has(message.id)) return;
  const { resolve, reject } = pending.get(message.id);
  pending.delete(message.id);
  if (message.error) reject(new Error(JSON.stringify(message.error)));
  else resolve(message.result);
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
    throw new Error(result.exceptionDetails.exception?.description
      || result.exceptionDetails.text || 'browser evaluation failed');
  return result.result.value;
};
const waitFor = async (expression, label) => {
  const stop = Date.now() + 12000;
  while (Date.now() < stop) {
    if (await evaluate(`Boolean(${expression})`)) return;
    await sleep(50);
  }
  throw new Error('timed out waiting for ' + label);
};
const navigate = async file => {
  const url = pathToFileURL(path.resolve(file)).href;
  await command('Page.navigate', { url });
  await waitFor(`location.href === ${JSON.stringify(url)}
    && document.readyState === 'complete'
    && document.querySelector('#root')?.children.length`,
    path.basename(file) + ' React mount');
  await sleep(250);
};
const openManager = async () => {
  await evaluate(`(() => {
    const activate = element => {
      const options = { bubbles: true, cancelable: true, button: 0, buttons: 1 };
      element.dispatchEvent(new PointerEvent('pointerdown', options));
      element.dispatchEvent(new MouseEvent('mousedown', options));
      element.dispatchEvent(new PointerEvent('pointerup', { ...options, buttons: 0 }));
      element.dispatchEvent(new MouseEvent('mouseup', { ...options, buttons: 0 }));
      element.dispatchEvent(new MouseEvent('click', { ...options, buttons: 0 }));
    };
    const trigger = Array.from(document.querySelectorAll('button')).find(button =>
      /^PRESET|^PATTERN/.test(button.textContent.trim()));
    if (!trigger) throw new Error('preset dropdown trigger missing: '
      + Array.from(document.querySelectorAll('button'))
        .map(button => button.textContent.trim()).filter(Boolean).join('|'));
    activate(trigger);
  })()`);
  await waitFor("Array.from(document.querySelectorAll('button')).some(button => button.textContent.trim() === 'MANAGE…')",
    'Manage menu item');
  await evaluate(`(() => {
    const element = Array.from(document.querySelectorAll('button')).find(button =>
      button.textContent.trim() === 'MANAGE…');
    const options = { bubbles: true, cancelable: true, button: 0, buttons: 1 };
    element.dispatchEvent(new PointerEvent('pointerdown', options));
    element.dispatchEvent(new MouseEvent('mousedown', options));
    element.dispatchEvent(new PointerEvent('pointerup', { ...options, buttons: 0 }));
    element.dispatchEvent(new MouseEvent('mouseup', { ...options, buttons: 0 }));
    element.dispatchEvent(new MouseEvent('click', { ...options, buttons: 0 }));
  })()`);
  await waitFor("Array.from(document.querySelectorAll('button')).some(button => button.textContent.trim() === 'EXPORT ALL (FILE)')",
    'Pattern Manager');
  await sleep(100);
};
const selectFlat = async () => {
  await evaluate(`(() => {
    const row = document.querySelector(
      '[data-spectr-pattern-source][data-spectr-pattern-id="factory:flat"]')
      || Array.from(document.querySelectorAll('div')).filter(node => {
      const rect = node.getBoundingClientRect();
      return node.textContent.includes('FLAT') && node.querySelector('svg')
        && rect.height > 20 && rect.height < 50;
    }).sort((a, b) => a.getBoundingClientRect().width - b.getBoundingClientRect().width)[0];
    if (!row) throw new Error('Flat pattern row missing');
    row.click();
  })()`);
  await sleep(100);
};
const captureManager = async name => {
  const clip = await evaluate(`(() => {
    const panel = document.querySelector('[aria-label="Pattern manager"] > div')
      || Array.from(document.querySelectorAll('div')).find(node => {
      const rect = node.getBoundingClientRect();
      return Math.abs(rect.width - 780) < 1 && Math.abs(rect.height - 520) < 1
        && node.textContent.includes('SAVE CURRENT')
        && node.textContent.includes('EXPORT ALL (FILE)');
    });
    if (!panel) throw new Error('Pattern Manager panel missing');
    const rect = panel.getBoundingClientRect();
    return { x: rect.x, y: rect.y, width: rect.width, height: rect.height, scale: 1 };
  })()`);
  const screenshot = await command('Page.captureScreenshot', {
    format: 'png', fromSurface: true, captureBeyondViewport: false, clip,
  });
  const destination = path.join(output, name + '.png');
  fs.writeFileSync(destination, Buffer.from(screenshot.data, 'base64'));
  return destination;
};

try {
  await command('Page.enable');
  await command('Runtime.enable');

  let canonicalPng;
  if (comparisonEnabled) {
    await navigate(canonicalHtml);
    await openManager();
    await selectFlat();
    canonicalPng = await captureManager('preset-manager-canonical');
  }

  await navigate(shippingPath);
  await openManager();
  await evaluate(`(() => {
    const save = Array.from(document.querySelectorAll('button')).find(button =>
      button.textContent.trim() === 'SAVE CURRENT');
    if (!save) throw new Error('Save Current missing');
    save.click();
  })()`);
  await waitFor("document.querySelector('[data-spectr-save-name]')", 'save name input');
  await evaluate(`(() => {
    const input = document.querySelector('[data-spectr-save-name]');
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')
      .set.call(input, 'USER ALIGNMENT PROOF');
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new KeyboardEvent('keydown', {
      key: 'Enter', bubbles: true, cancelable: true,
    }));
  })()`);
  await waitFor("document.querySelector('[data-spectr-pattern-source=\"user\"]')",
    'saved user preset row');
  const receipt = await evaluate(`(async () => {
    const manager = document.querySelector('[aria-label="Pattern manager"] > div')
      || Array.from(document.querySelectorAll('div')).find(node => {
      const rect = node.getBoundingClientRect();
      return Math.abs(rect.width - 780) < 1 && Math.abs(rect.height - 520) < 1
        && node.textContent.includes('SAVE CURRENT')
        && node.textContent.includes('EXPORT ALL (FILE)');
    });
    if (!manager) throw new Error('shipping Pattern Manager missing');
    const rows = Array.from(manager.querySelectorAll(
      '[data-spectr-pattern-source][data-spectr-pattern-id]'));
    if (rows.length !== 9)
      throw new Error('expected 8 factory and 1 user preset, saw ' + rows.length);
    const frames = () => new Promise(resolve => requestAnimationFrame(() =>
      requestAnimationFrame(resolve)));
    const overlap = (a, b) => Math.min(a.right, b.right) - Math.max(a.left, b.left) > 0.5
      && Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top) > 0.5;
    const inspect = (id, editing = false) => {
      const title = editing ? manager.querySelector('[data-spectr-manager-rename]')
        : manager.querySelector('[data-spectr-manager-title]');
      const source = manager.querySelector('[data-spectr-manager-source]');
      const heading = source?.parentElement;
      const detail = heading?.parentElement;
      const rename = manager.querySelector('[data-spectr-manager-action="rename-start"]');
      if (!title || !source || !heading || !detail)
        throw new Error(id + ' selected detail subjects missing');
      const centered = [title, source, ...(rename ? [rename] : [])]
        .map(node => node.getBoundingClientRect());
      const headingRect = heading.getBoundingClientRect();
      const center = (headingRect.top + headingRect.bottom) * 0.5;
      if (centered.some(rect => Math.abs((rect.top + rect.bottom) * 0.5 - center) > 3))
        throw new Error(id + (editing ? ' rename' : '') + ' vertical alignment diverged');
      const actions = Array.from(detail.querySelectorAll('button')).filter(button =>
        button !== rename && button.offsetWidth > 0 && button.offsetHeight > 0);
      const expected = id.startsWith('user:') ? 7 : 5;
      if (actions.length !== expected)
        throw new Error(id + ' expected ' + expected + ' actions, saw ' + actions.length);
      const rects = actions.map(button => button.getBoundingClientRect());
      if (${JSON.stringify(mutation)} === '--plant-overlap' && id === 'factory:flat')
        rects[1] = rects[0];
      for (let a = 0; a < rects.length; ++a)
        for (let b = a + 1; b < rects.length; ++b)
          if (overlap(rects[a], rects[b]))
            throw new Error(id + ' action overlap: '
              + actions[a].textContent.trim() + '/' + actions[b].textContent.trim());
      return actions.map(button => button.textContent.trim());
    };
    const result = {};
    for (const row of rows) {
      row.click();
      await frames();
      const id = row.dataset.spectrPatternId;
      const selected = manager.querySelector('[data-spectr-manager-title]');
      if (selected?.dataset.spectrPatternId !== id)
        throw new Error(id + ' selected title identity did not update');
      result[id] = inspect(id);
    }
    const user = rows.find(row => row.dataset.spectrPatternSource === 'user');
    user.click();
    await frames();
    manager.querySelector('[data-spectr-manager-action="rename-start"]').click();
    await frames();
    inspect(user.dataset.spectrPatternId, true);
    return result;
  })()`);
  await evaluate(`(() => {
    const input = document.querySelector('[data-spectr-manager-rename]');
    input?.dispatchEvent(new KeyboardEvent('keydown', {
      key: 'Escape', bubbles: true, cancelable: true,
    }));
  })()`);
  await sleep(100);
  if (!await evaluate("Array.from(document.querySelectorAll('button')).some(button => button.textContent.trim() === 'EXPORT ALL (FILE)')"))
    await openManager();
  await selectFlat();
  const shippingPng = await captureManager('preset-manager-shipping');

  const comparison = comparisonEnabled ? spawnSync('python3', ['-c', String.raw`
from PIL import Image, ImageChops
import json, sys
a = Image.open(sys.argv[1]).convert('RGB')
b = Image.open(sys.argv[2]).convert('RGB')
shipping_size = b.size
if a.size != b.size:
    b = b.resize(a.size, Image.Resampling.LANCZOS)
d = ImageChops.difference(a, b)
h = d.histogram()
total = sum(value * (index % 256) for index, value in enumerate(h))
mean = total / (a.width * a.height * 3)
changed = sum(1 for pixel in d.getdata() if pixel != (0, 0, 0))
sheet = Image.new('RGB', (a.width * 2, a.height))
sheet.paste(a, (0, 0)); sheet.paste(b, (a.width, 0))
sheet.save(sys.argv[3])
print(json.dumps({'canonical_size': list(a.size), 'shipping_size': list(shipping_size),
  'mean_channel_delta': mean,
  'changed_pixel_fraction': changed / (a.width * a.height)}))
`, canonicalPng, shippingPng, path.join(output, 'preset-manager-ab.png')], {
    encoding: 'utf8',
  }) : null;
  if (comparison) assert.equal(comparison.status, 0, comparison.stderr);
  const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  console.log(JSON.stringify({
    ...(comparisonEnabled ? {
      canonical_html_sha256: digest(canonicalHtml),
      canonical_pattern_manager_sha256:
        '4650bcb8effd65e0fd1cbd001a8a032205f6937285776830a6255f59a48cb3a1',
    } : {}),
      shipping_html_sha256: digest(shippingPath), receipt,
    ...(comparison ? { comparison: { ...JSON.parse(comparison.stdout), comparison_only: true,
      note: 'No parity threshold is asserted; human review of the A/B sheet remains required.' } } : {}),
    output,
  }, null, 2));
} finally {
  socket.close();
  chrome.kill('SIGTERM');
  if (!requestedOutput) fs.rmSync(scratch, { recursive: true, force: true });
}
