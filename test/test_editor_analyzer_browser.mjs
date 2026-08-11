import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [htmlPath, chromePath] = process.argv.slice(2);
assert(htmlPath && chromePath, 'usage: test_editor_analyzer_browser.mjs HTML CHROME');

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-analyzer-browser-'));
try {
  let html = fs.readFileSync(htmlPath, 'utf8');
  const mock = `<script>
window.__spectrHandlers = Object.create(null);
window.pulp = {
  on(type, callback) {
    (window.__spectrHandlers[type] ||= new Set()).add(callback);
    return () => window.__spectrHandlers[type].delete(callback);
  },
  postMessage() { return Promise.resolve({ ok: true, payload: { ok: true } }); }
};
window.__spectrEmit = (type, payload) => {
  for (const callback of window.__spectrHandlers[type] || [])
    callback({ type, payload });
};
</script>`;
  html = html.replace('<script>', mock + '<script>');
  const oracle = `<script>
setTimeout(() => {
  const result = document.createElement('pre');
  result.id = '__spectr_browser_oracle';
  try {
    const sampleAt1k = () => window.SpectrAnalyzer.sample(3, 0, 'visible');
    if (sampleAt1k() !== 0) throw new Error('native pre-frame signal was not silent');
    const visible = new Array(321).fill(-120);
    const overview = new Array(121).fill(-120);
    const exactPeakIndex = (Math.log10(1000) - Math.log10(20))
      / (Math.log10(20000) - Math.log10(20)) * 320;
    visible[Math.floor(exactPeakIndex)] = 0;
    visible[Math.ceil(exactPeakIndex)] = 0;
    const payload = {
      schema_version: 1, epoch: 2, sequence_number: 4,
      dropped_frames: 0, source_channels: 2,
      fft_size: 8192, sample_rate: 48000, floor_db: -120, ceiling_db: 0,
      visible: { min_hz: 20, max_hz: 20000, magnitude_db: visible },
      overview: { min_hz: 20, max_hz: 20000, magnitude_db: overview },
    };
    window.__spectrEmit('analyzer_frame', payload);
    if (sampleAt1k() < 0.95) throw new Error('valid live peak was not sampled');
    const accepted = window.SpectrAnalyzer.debugSnapshot();
    window.__spectrEmit('analyzer_frame', { ...payload, sequence_number: 3 });
    if (window.SpectrAnalyzer.debugSnapshot() !== accepted)
      throw new Error('stale frame replaced live state');
    window.__spectrEmit('analyzer_frame', {
      ...payload, sequence_number: 5,
      visible: { ...payload.visible, magnitude_db: [NaN] },
    });
    if (window.SpectrAnalyzer.debugSnapshot() !== accepted)
      throw new Error('malformed frame replaced live state');
    window.__spectrEmit('analyzer_frame', {
      ...payload, sequence_number: 6,
      visible: { ...payload.visible, magnitude_db: new Array(321).fill(-120) },
    });
    if (sampleAt1k() !== 0) throw new Error('finite floor did not produce silence');
    window.dispatchEvent(new Event('pagehide'));
    if (window.SpectrAnalyzer.debugSnapshot() !== null
        || window.__spectrHandlers.analyzer_frame.size !== 0)
      throw new Error('analyzer listener survived document teardown');
    if (document.getElementById('__bundler_err'))
      throw new Error('bundle error sink is not empty');
    result.textContent = 'SPECTR_BROWSER_ORACLE_OK';
  } catch (error) {
    result.textContent = 'SPECTR_BROWSER_ORACLE_FAIL:' + error.message;
  }
  document.body.appendChild(result);
}, 1500);
</script>`;
  html = html.replace('</body>', oracle + '</body>');
  const instrumented = path.join(temp, 'spectr.html');
  fs.writeFileSync(instrumented, html);

  const run = spawnSync(chromePath, [
    '--headless=new', '--disable-gpu', '--disable-web-security',
    '--disable-background-networking', '--disable-component-update',
    '--disable-domain-reliability', '--disable-sync',
    '--allow-file-access-from-files', '--no-first-run', '--no-default-browser-check',
    '--virtual-time-budget=5000', `--user-data-dir=${path.join(temp, 'profile')}`,
    '--dump-dom', `file://${instrumented}`,
  ], { encoding: 'utf8', timeout: 30000 });
  assert.equal(run.status, 0, run.stderr);
  assert.match(run.stdout, /SPECTR_BROWSER_ORACLE_OK/,
    run.stdout.match(/SPECTR_BROWSER_ORACLE_FAIL:[^<]*/)?.[0] || run.stderr);
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
