import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [documentPath, browserHtmlPath, chromePath] = process.argv.slice(2);
assert(documentPath && browserHtmlPath && chromePath,
  'usage: test_materialized_build_info_browser.mjs DOCUMENT_JSON BROWSER_HTML CHROME');

const document = JSON.parse(fs.readFileSync(documentPath, 'utf8'));
assert.equal(typeof document.html, 'string', 'materialized document has no HTML');

const timeoutBlock = `    const loadTimer = setTimeout(() => {
      if (live) setLoadFailed(true);
    }, 1500);`;
assert(document.html.includes(timeoutBlock), 'build-info timeout contract missing');
const surfaceStart = document.html.indexOf('function SpectrSettingsGroup(');
const surfaceEnd = document.html.indexOf('/* materialized-build-info-owner */');
assert(surfaceStart >= 0 && surfaceEnd > surfaceStart,
  'shipping build-info component surface missing');
const shippingSurface = document.html.slice(surfaceStart, surfaceEnd);

const oracle = (negativeControl, componentSource) => `<script>
${componentSource}
window.spectrStartBuildInfoOracle = () => {
  if (window.__spectrBuildInfoOracleStarted) return;
  if (!window.React || !window.ReactDOM) {
    setTimeout(window.spectrStartBuildInfoOracle, 20);
    return;
  }
  window.__spectrBuildInfoOracleStarted = true;
  const waitFor = async (predicate, label, limit = 200) => {
    for (let attempt = 0; attempt < limit; ++attempt) {
      const value = predicate();
      if (value) return value;
      await new Promise(resolve => setTimeout(resolve, 20));
    }
    throw new Error('timed out waiting for ' + label);
  };
  const result = document.createElement('pre');
  result.id = '__spectr_build_info_oracle';
  document.body.appendChild(result);
  const mount = document.createElement('div');
  mount.id = '__spectr_build_info_mount';
  document.body.appendChild(mount);
  ReactDOM.createRoot(mount).render(React.createElement(SpectrBuildInfo, null));
  (async () => {
    try {
      const state = await waitFor(
        () => document.querySelector('[data-spectr-build-info-state]'),
        'build-info state');
      if (state.dataset.spectrBuildInfoState !== 'loading')
        throw new Error('build info did not begin in loading state');
      await new Promise(resolve => setTimeout(resolve, 1800));
      const finalState = document.querySelector('[data-spectr-build-info-state]');
      const expected = ${negativeControl ? JSON.stringify('loading') : JSON.stringify('unavailable')};
      if (!finalState || finalState.dataset.spectrBuildInfoState !== expected)
        throw new Error('expected ' + expected + ', got '
          + (finalState && finalState.dataset.spectrBuildInfoState));
      result.textContent = ${negativeControl
        ? JSON.stringify('SPECTR_BUILD_INFO_NEGATIVE_CONTROL_OK')
        : JSON.stringify('SPECTR_BUILD_INFO_TIMEOUT_OK')};
    } catch (error) {
      result.textContent = 'SPECTR_BUILD_INFO_ORACLE_ERROR: ' + error.stack;
    }
  })();
};
setTimeout(window.spectrStartBuildInfoOracle, 0);
</script>`;

const run = negativeControl => {
  let componentSource = shippingSurface;
  if (negativeControl) componentSource = componentSource.replace(timeoutBlock,
    '    const loadTimer = null; // planted negative control: no timeout');
  let html = fs.readFileSync(browserHtmlPath, 'utf8');
  const mock = `<script>
window.pulp = {
  on() { return () => {}; },
  postMessage(type) {
    return type === 'build_info_get' ? new Promise(() => {})
      : Promise.resolve({ ok: true, payload: { ok: true } });
  },
};
</script>`;
  html = html.replace('<script>', mock + '<script>');
  html = html.replace('</body>', `${oracle(negativeControl, componentSource)}</body>`);
  html = html.replace('      window.Babel.transformScriptTags();',
    '      window.Babel.transformScriptTags();\n'
      + '      window.spectrStartBuildInfoOracle();');

  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-build-info-'));
  const htmlPath = path.join(directory, 'oracle.html');
  fs.writeFileSync(htmlPath, html);
  try {
    const result = spawnSync(chromePath, [
      '--headless=new', '--disable-gpu', '--disable-web-security',
      '--disable-background-networking', '--disable-component-update',
      '--disable-domain-reliability', '--disable-sync', '--incognito',
      '--allow-file-access-from-files', '--no-first-run',
      '--no-default-browser-check', '--window-size=1320,860',
      '--run-all-compositor-stages-before-draw', '--virtual-time-budget=12000',
      '--dump-dom', `file://${htmlPath}`,
    ], { encoding: 'utf8', timeout: 30000, maxBuffer: 64 * 1024 * 1024 });
    assert.equal(result.error, undefined, result.error && result.error.message);
    assert.equal(result.status, 0, result.stderr.slice(-2000));
    const marker = negativeControl
      ? 'SPECTR_BUILD_INFO_NEGATIVE_CONTROL_OK'
      : 'SPECTR_BUILD_INFO_TIMEOUT_OK';
    assert(result.stdout.includes(marker),
      `missing ${marker}: ${result.stdout.slice(-3000)}`);
    assert(!result.stdout.includes('SPECTR_BUILD_INFO_ORACLE_ERROR'),
      result.stdout.slice(-3000));
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
};

// The planted control proves the mounted oracle sees the old indefinite
// loading state. The shipping pass then proves the same unresolved host
// request transitions to a terminal, user-readable state.
run(true);
run(false);
console.log('Spectr materialized build-info timeout: negative control red, production green');
