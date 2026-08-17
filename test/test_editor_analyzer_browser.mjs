import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [htmlPath, chromePath, mode] = process.argv.slice(2);
assert(htmlPath && chromePath, 'usage: test_editor_analyzer_browser.mjs HTML CHROME');
const resizeOnlyMode = mode === '--resize-only';
const jsOnlyMode = mode === '--js-only';

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-analyzer-browser-'));
try {
  let html = fs.readFileSync(htmlPath, 'utf8');
  const mock = `<script>
window.__spectrHandlers = Object.create(null);
window.__spectrPosts = [];
window.__spectrTestHooks = Object.create(null);
window.__spectrCanvasLabels = [];
window.__spectrRuntimeErrors = [];
window.confirm = () => true;
window.addEventListener('error', event => {
  window.__spectrRuntimeErrors.push(String(event.message || event.error || event.type));
});
window.addEventListener('unhandledrejection', event => {
  window.__spectrRuntimeErrors.push(String(event.reason || 'unhandled rejection'));
});
const spectrOriginalFillText = CanvasRenderingContext2D.prototype.fillText;
CanvasRenderingContext2D.prototype.fillText = function(text, ...args) {
  window.__spectrCanvasLabels.push(String(text));
  return spectrOriginalFillText.call(this, text, ...args);
};
// WebKit rejects non-finite Canvas coordinates. Chromium is more permissive
// for several methods, so make the executable oracle enforce the stricter
// cross-engine contract.
for (const method of [
  'arc', 'bezierCurveTo', 'clearRect', 'createLinearGradient',
  'createRadialGradient', 'ellipse', 'fillRect', 'lineTo', 'moveTo',
  'quadraticCurveTo', 'rect', 'rotate', 'scale', 'setTransform',
  'strokeRect', 'transform', 'translate',
]) {
  const original = CanvasRenderingContext2D.prototype[method];
  if (typeof original !== 'function') continue;
  CanvasRenderingContext2D.prototype[method] = function(...args) {
    if (args.some(value => typeof value === 'number' && !Number.isFinite(value)))
      throw new TypeError('non-finite Canvas argument in ' + method);
    return original.apply(this, args);
  };
}
// Headless --dump-dom may throttle RAF after first paint. A timer-backed RAF
// keeps the production animation/effect callbacks ordered and deterministic.
window.requestAnimationFrame = callback => setTimeout(
  () => callback(performance.now()), 16);
window.cancelAnimationFrame = handle => clearTimeout(handle);
// Synthetic PointerEvents are not registered with Chromium's hardware pointer
// tracker, so capture would otherwise throw before React reaches pointerup.
Element.prototype.setPointerCapture = () => {};
Element.prototype.releasePointerCapture = () => {};
const spectrReopened = new URL(location.href).searchParams.has('reopened');
const spectrJsOnly = new URL(location.href).searchParams.has('js-only');
const spectrEmptySnapshots = () => ({
  A: { populated: false, gain_db: [], muted: [] },
  B: { populated: false, gain_db: [], muted: [] },
});
const spectrReopenedSnapshots = () => ({
  A: { populated: true, gain_db: new Array(32).fill(12), muted: new Array(32).fill(false) },
  B: { populated: true, gain_db: new Array(32).fill(-12), muted: Array.from({ length: 32 }, (_, i) => i === 7) },
});
window.__spectrHydration = {
  revision: 0,
  n_visible: 32,
  gain_db: Array.from({ length: 32 }, (_, index) => [-12, 0, 12][index % 3]),
  muted: new Array(32).fill(true),
  min_hz: 100,
  max_hz: 10000,
  snapshots: spectrReopened ? spectrReopenedSnapshots() : spectrEmptySnapshots(),
};
window.__spectrPatternEnvelope = {
  format: 'spectr.patterns', version: 1, default_id: 'factory:flat', patterns: [],
};
window.__spectrHydration.patterns_json = JSON.stringify(window.__spectrPatternEnvelope);
const spectrClone = value => JSON.parse(JSON.stringify(value));
window.__spectrNativeState = spectrClone(window.__spectrHydration);
const spectrReply = revision => ({ ok: true, payload: {
  ok: true, ...spectrClone(window.__spectrNativeState), revision,
} });
if (!spectrJsOnly) window.pulp = {
  on(type, callback) {
    (window.__spectrHandlers[type] ||= new Set()).add(callback);
    return () => window.__spectrHandlers[type].delete(callback);
  },
  postMessage(type, payload, id) {
    window.__spectrPosts.push({ type, payload, id });
    if (type === 'editor_ready') {
      queueMicrotask(() => window.__spectrEmit(
        'processing_state_hydrate', window.__spectrNativeState));
      return Promise.resolve({ ok: true, payload: { ok: true } });
    }
    if (type === 'processing_state_set') {
      window.__spectrNativeState = {
        ...window.__spectrNativeState,
        n_visible: payload.n_visible,
        gain_db: payload.gain_db.slice(), muted: payload.muted.slice(),
        min_hz: payload.min_hz, max_hz: payload.max_hz,
      };
      return Promise.resolve({ ok: true, payload: { ok: true } });
    }
    if (type === 'save_current_pattern') {
      const now = new Date().toISOString();
      window.__spectrPatternEnvelope.patterns.push({
        id: 'user:test-' + window.__spectrPatternEnvelope.patterns.length,
        name: payload.name, source: 'user', created_at: now, updated_at: now,
        gain_db: window.__spectrNativeState.gain_db.slice(),
        muted: window.__spectrNativeState.muted.slice(),
      });
      window.__spectrNativeState.patterns_json = JSON.stringify(window.__spectrPatternEnvelope);
      return Promise.resolve({ ok: true, payload: {
        ok: true, patterns_json: window.__spectrNativeState.patterns_json,
      } });
    }
    if (type === 'rename_pattern') {
      const pattern = window.__spectrPatternEnvelope.patterns.find(item => item.id === payload.id);
      if (pattern) pattern.name = payload.name;
      window.__spectrNativeState.patterns_json = JSON.stringify(window.__spectrPatternEnvelope);
      return Promise.resolve({ ok: true, payload: {
        ok: true, patterns_json: window.__spectrNativeState.patterns_json,
      } });
    }
    if (type === 'delete_pattern') {
      window.__spectrPatternEnvelope.patterns = window.__spectrPatternEnvelope.patterns
        .filter(item => item.id !== payload.id);
      window.__spectrNativeState.patterns_json = JSON.stringify(window.__spectrPatternEnvelope);
      return Promise.resolve({ ok: true, payload: {
        ok: true, patterns_json: window.__spectrNativeState.patterns_json,
      } });
    }
    const revision = Number(payload && payload.revision) || 0;
    if (type === 'capture_snapshot') {
      window.__spectrNativeState.snapshots[payload.slot] = {
        populated: true,
        gain_db: window.__spectrNativeState.gain_db.slice(),
        muted: window.__spectrNativeState.muted.slice(),
      };
      return Promise.resolve(spectrReply(revision));
    }
    if (type === 'recall_snapshot') {
      const snap = window.__spectrNativeState.snapshots[payload.slot];
      if (!snap || !snap.populated) return Promise.resolve({ ok: false, payload: { ok: false } });
      window.__spectrNativeState.gain_db = snap.gain_db.slice();
      window.__spectrNativeState.muted = snap.muted.slice();
      return Promise.resolve(spectrReply(revision));
    }
    if (type === 'morph') {
      const a = window.__spectrNativeState.snapshots.A;
      const b = window.__spectrNativeState.snapshots.B;
      const t = Math.max(0, Math.min(1, Number(payload.t)));
      window.__spectrNativeState.gain_db = a.gain_db.map((gain, i) => gain + (b.gain_db[i] - gain) * t);
      window.__spectrNativeState.muted = (t >= 0.5 ? b : a).muted.slice();
      const reply = spectrReply(revision);
      return t === 0.25
        ? new Promise(resolve => setTimeout(() => resolve(reply), 80))
        : Promise.resolve(reply);
    }
    return Promise.resolve({ ok: true, payload: { ok: true } });
  }
};
window.__spectrEmit = (type, payload) => {
  for (const callback of window.__spectrHandlers[type] || [])
    callback({ type, payload });
};
</script>`;
  html = html.replace('<script>', mock + '<script>');
  const oracle = `<script>
const spectrFrames = (count = 1) => new Promise(resolve => {
  const advance = () => count-- > 0 ? requestAnimationFrame(advance) : resolve();
  requestAnimationFrame(advance);
});
const spectrWaitFor = async (predicate, label, limit = 180) => {
  for (let frame = 0; frame < limit; ++frame) {
    const value = predicate();
    if (value) return value;
    await new Promise(resolve => setTimeout(resolve, 20));
  }
  throw new Error('timed out waiting for ' + label);
};
const spectrStatePosts = () => window.__spectrPosts
  .filter(message => message.type === 'processing_state_set');
const spectrLatestState = () => spectrStatePosts().at(-1)?.payload;
const spectrFiniteState = state => !!state
  && Number.isFinite(state.min_hz) && Number.isFinite(state.max_hz)
  && state.min_hz > 0 && state.max_hz > state.min_hz
  && Array.isArray(state.gain_db) && state.gain_db.length === state.n_visible
  && state.gain_db.every(Number.isFinite)
  && Array.isArray(state.muted) && state.muted.length === state.n_visible
  && state.muted.every(value => typeof value === 'boolean');
const spectrBundleClean = () => !document.getElementById('__bundler_err')
  && window.__spectrRuntimeErrors.length === 0;
const spectrPublishAfter = async (count, label) => spectrWaitFor(
  () => spectrStatePosts().length > count && spectrLatestState(), label);
const spectrPointer = (target, type, x, y, pointerId = 7, modifiers = {}) => {
  const hit = document.elementFromPoint(x, y);
  if (!hit || (hit !== target && !target.contains(hit)))
    throw new Error('pointer coordinate did not hit intended control');
  hit.dispatchEvent(new PointerEvent(type, {
    bubbles: true,
    cancelable: true,
    pointerId,
    pointerType: 'mouse',
    isPrimary: true,
    button: type === 'pointermove' ? -1 : 0,
    buttons: type === 'pointerup' ? 0 : 1,
    clientX: x,
    clientY: y,
    shiftKey: !!modifiers.shiftKey,
  }));
};
const spectrTap = async (target, x, y, jitter = 0) => {
  spectrPointer(target, 'pointerdown', x, y);
  if (jitter) spectrPointer(target, 'pointermove', x + jitter, y + jitter);
  spectrPointer(target, 'pointerup', x + jitter, y + jitter);
  await spectrFrames(2);
};
const spectrModeLabel = () => {
  const button = Array.from(document.querySelectorAll('button')).find(candidate =>
    /^(SCULPT|LEVEL|BOOST|FLARE|GLIDE)(?:\\s|$)/.test(candidate.textContent.trim()));
  return button && button.textContent.trim().split(/\\s+/)[0];
};
const spectrKey = (key, code) => {
  const event = new KeyboardEvent('keydown', {
    key, code, bubbles: true, cancelable: true,
  });
  // Dispatch from the focused document so capture listeners on document and
  // React's bubble listener on window see the same path as a hardware key.
  document.body.dispatchEvent(event);
  return event;
};
const spectrClick = async target => {
  target.dispatchEvent(new MouseEvent('click', {
    bubbles: true, cancelable: true, button: 0,
  }));
  await spectrFrames(2);
};
const spectrOutsideClick = async target => {
  // Popup dismissal is installed on document.mousedown.  Preserve the real
  // browser event order instead of treating a synthetic click as equivalent.
  target.dispatchEvent(new MouseEvent('mousedown', {
    bubbles: true, cancelable: true, button: 0,
  }));
  target.dispatchEvent(new MouseEvent('click', {
    bubbles: true, cancelable: true, button: 0,
  }));
  await spectrFrames(2);
};
const spectrTestNativeResizeSurface = async () => {
  if (document.getElementById('spectr-resize-grip')
      || document.getElementById('spectr-resize-status'))
    throw new Error('product-owned resize affordance is still present');
  if (window.__spectrPosts.some(message => message.type === 'editor_resize_request'))
    throw new Error('product attempted to resize its native host');
  const bodyRect = document.body.getBoundingClientRect();
  const expectedScale = Math.min(innerWidth / 1320, innerHeight / 860);
  const expectedWidth = 1320 * expectedScale;
  const expectedHeight = 860 * expectedScale;
  if (Math.abs(bodyRect.width - expectedWidth) > 0.75
      || Math.abs(bodyRect.height - expectedHeight) > 0.75)
    throw new Error('fixed design did not fit native host viewport');
  if (Math.abs(bodyRect.left + bodyRect.width * 0.5 - innerWidth * 0.5) > 0.75
      || Math.abs(bodyRect.top + bodyRect.height * 0.5 - innerHeight * 0.5) > 0.75)
    throw new Error('fixed design was not centered in native host viewport');
  const root = document.getElementById('root');
  if (!root || root.clientWidth !== 1320 || root.clientHeight !== 860)
    throw new Error('native host resize reflowed the authored design');

  const textInput = document.createElement('input');
  textInput.type = 'text';
  textInput.value = 'selectable pattern name';
  document.body.appendChild(textInput);
  if (getComputedStyle(textInput).userSelect === 'none')
    throw new Error('resize selection guard leaked into text input');
  textInput.focus();
  textInput.setSelectionRange(0, 10);
  if (textInput.selectionStart !== 0 || textInput.selectionEnd !== 10)
    throw new Error('text input selection was disabled');
  textInput.remove();
};
const spectrButton = label => Array.from(document.querySelectorAll('button'))
  .find(candidate => candidate.textContent.trim() === label);

window.spectrStartOracle = () => {
  if (window.__spectrOracleStarted) return;
  if (!window.__spectrTestHooks.renderState) {
    setTimeout(window.spectrStartOracle, 20);
    return;
  }
  window.__spectrOracleStarted = true;
  const result = document.createElement('pre');
  result.id = '__spectr_browser_oracle';
  document.body.appendChild(result);
  (async () => {
    try {
      const step = value => { result.dataset.step = value; };
      const reopened = new URL(location.href).searchParams.has('reopened');
      const jsOnly = new URL(location.href).searchParams.has('js-only');
      const resizeOnly = new URL(location.href).searchParams.has('resize-only');
      step(reopened ? 'reopened-start' : 'initial-start');
      const bodyRect = document.body.getBoundingClientRect();
      if (document.body.clientWidth !== 1320 || document.body.clientHeight !== 860)
        throw new Error('fixed editor design space changed');
      if (Math.abs(bodyRect.width / bodyRect.height - 1320 / 860) > 0.002)
        throw new Error('editor did not scale proportionally');
      if (bodyRect.width > innerWidth + 0.5 || bodyRect.height > innerHeight + 0.5)
        throw new Error('editor overflowed its host viewport');
      if (resizeOnly) {
        await spectrTestNativeResizeSurface();
        result.textContent = 'SPECTR_BROWSER_RESIZE_OK';
        document.documentElement.dataset.spectrOracle = 'RESIZE_OK';
        return;
      }
      if (jsOnly) {
        const canvas = Array.from(document.querySelectorAll('canvas'))
          .find(candidate => getComputedStyle(candidate).pointerEvents !== 'none');
        const target = canvas && canvas.parentElement;
        if (!target) throw new Error('JS-only interactive canvas missing');
        const rect = target.getBoundingClientRect();
        const x = rect.left + rect.width * 0.4, y = rect.top + rect.height * 0.45;
        const captureButton = slot => document.querySelector(
          '[data-spectr-snapshot-action="capture"][data-spectr-snapshot-slot="'
            + slot + '"]');
        const snapA = captureButton('A');
        const snapB = captureButton('B');
        if (!snapA || !snapB) throw new Error('JS-only snapshot controls missing');
        await spectrWaitFor(() => window.__spectrTestHooks.renderState?.()
          .bankReady, 'JS-only bank readiness');
        snapA.click();
        await spectrFrames(2);
        await spectrWaitFor(() => window.__spectrTestHooks.renderState?.()
          .snapshots.A, 'JS-only snapshot A capture');
        await spectrTap(target, x, y);
        captureButton('B').click();
        await spectrFrames(2);
        await spectrWaitFor(() => window.__spectrTestHooks.renderState?.()
          .snapshots.B, 'JS-only snapshot B capture');
        const morph = await spectrWaitFor(() => document.querySelector('[data-spectr-morph]'),
          'JS-only morph slider');
        await spectrWaitFor(() => !morph.disabled, 'JS-only morph enablement');
        morph.value = '0.5';
        morph.dispatchEvent(new Event('input', { bubbles: true }));
        await spectrWaitFor(() => window.__spectrTestHooks.renderState?.()
          .targetGains.some(value => value === -Infinity), 'JS-only midpoint mute parity');
        if (window.__spectrPosts.length !== 0)
          throw new Error('JS-only fallback attempted native publication');
        document.documentElement.dataset.spectrOracle = 'JS_ONLY_OK';
        return;
      }
      const hydrated = await spectrWaitFor(() => {
        const state = window.__spectrTestHooks.renderState?.();
        return state && state.nVisible === 32
          && state.targetGains.length === 32
          && state.targetGains.every(value => value === -Infinity)
          && state.mutedGainDb.every(Number.isFinite) && state;
      }, reopened ? 'finite reopened hydration' : 'finite initial hydration');
      step(reopened ? 'reopened-hydrated' : 'initial-hydrated');
      if (document.querySelector('[data-spectr-status-banner]'))
        throw new Error('empty status banner was mounted');
      Object.defineProperty(document, 'hasFocus', {
        configurable: true, value: () => true,
      });
      spectrKey('1', 'Digit1');
      const statusBanner = await spectrWaitFor(() =>
        document.querySelector('[data-spectr-status-banner]'),
      'non-empty status banner');
      if (!statusBanner.textContent.trim())
        throw new Error('visible status banner had no message');
      await spectrWaitFor(() =>
        !document.querySelector('[data-spectr-status-banner]'),
      'expired status banner removal');
      await spectrTestNativeResizeSurface();
      step('native-resize-surface-complete');
      await spectrFrames(5);
      if (!spectrBundleClean()) throw new Error('bundle error after hydrated RAFs');
      const expectedDbfsLabels = ['-120', '-90', '-60', '-30', '0', '+24'];
      if (!window.__spectrCanvasLabels.includes('dBFS')
          || !expectedDbfsLabels.every(label =>
            window.__spectrCanvasLabels.includes(label)))
        throw new Error('complete calibrated dBFS ruler was not drawn');
      if (!hydrated.mutedGainDb.every((gain, index) =>
        gain === window.__spectrHydration.gain_db[index]))
        throw new Error('hydration changed authored dB');

      if (reopened) {
        if ((window.__spectrHandlers.processing_state_hydrate?.size || 0) !== 1)
          throw new Error('reopened hydration listener count is not one');
        const morph = await spectrWaitFor(() => document.querySelector('[data-spectr-morph]'),
          'reopened morph slider');
        if (morph.disabled) throw new Error('reopened snapshots did not hydrate');
        morph.value = '0.25';
        morph.dispatchEvent(new Event('input', { bubbles: true }));
        morph.value = '0.75';
        morph.dispatchEvent(new Event('input', { bubbles: true }));
        // Host resize can synchronously repaint before the next RAF repairs
        // transition state. This was the real WebKit reopen failure mode.
        window.dispatchEvent(new Event('resize'));
        await spectrWaitFor(() => Math.abs(
          window.__spectrTestHooks.renderState?.().targetGains[0] + 0.25) < 1e-6,
        'latest native morph projection');
        await new Promise(resolve => setTimeout(resolve, 100));
        const afterStale = window.__spectrTestHooks.renderState().targetGains[0];
        if (Math.abs(afterStale + 0.25) > 1e-6)
          throw new Error('stale native morph response overwrote latest revision: ' + afterStale);
        morph.value = '0.5';
        morph.dispatchEvent(new Event('input', { bubbles: true }));
        await spectrWaitFor(() => window.__spectrPosts.some(message =>
          message.type === 'morph' && message.payload.t === 0.5), 'reopened morph command');
        await spectrWaitFor(() => window.__spectrTestHooks.renderState?.().targetGains[7] === -Infinity,
          'reopened authoritative midpoint mute');
        if (!spectrBundleClean())
          throw new Error('bundle error after reopened morph and synchronous paint');
        window.dispatchEvent(new Event('pagehide'));
        if (window.SpectrAnalyzer.debugSnapshot() !== null
            || window.__spectrHandlers.analyzer_frame.size !== 0)
          throw new Error('analyzer survived reopened document teardown');
        result.textContent = 'SPECTR_BROWSER_ORACLE_OK';
        document.documentElement.dataset.spectrOracle = 'OK';
        return;
      }

      const sampleAt1k = () => window.SpectrAnalyzer.sample(3, 0, 'visible');
      if (sampleAt1k() !== 0) throw new Error('native pre-frame signal was not silent');
      const visible = new Array(321).fill(-120);
      const overview = new Array(121).fill(-120);
      const exactPeakIndex = (Math.log10(1000) - Math.log10(20))
        / (Math.log10(20000) - Math.log10(20)) * 320;
      visible[Math.floor(exactPeakIndex)] = 24;
      visible[Math.ceil(exactPeakIndex)] = 24;
      const payload = {
        schema_version: 1, epoch: 2, sequence_number: 4,
        dropped_frames: 0, source_channels: 2,
        fft_size: 8192, sample_rate: 48000, floor_db: -120, ceiling_db: 24,
        visible: { min_hz: 20, max_hz: 20000, magnitude_db: visible },
        overview: { min_hz: 20, max_hz: 20000, magnitude_db: overview },
      };
      window.__spectrEmit('analyzer_frame', payload);
      if (sampleAt1k() < 0.95) throw new Error('valid live peak was not sampled');
      const expectedDbfsMapping = [
        [-120, 0], [-90, 30 / 144], [-60, 60 / 144],
        [-30, 90 / 144], [0, 120 / 144], [24, 1],
      ];
      const deterministicAmplitudes = [
        [1, 0], [10 ** (-6 / 20), -6], [10 ** (-30 / 20), -30],
        [10 ** (-60 / 20), -60], [0, -120],
      ];
      for (const [amplitude, expectedDb] of deterministicAmplitudes) {
        const measuredDb = amplitude > 0
          ? 20 * Math.log10(amplitude) : -Infinity;
        const clampedDb = Math.max(-120, Math.min(24, measuredDb));
        if (Math.abs(clampedDb - expectedDb) > 1e-9)
          throw new Error('incorrect amplitude to dBFS conversion: '
            + amplitude + ' -> ' + clampedDb);
        const expectedAmount = (expectedDb + 120) / 144;
        const actualAmount = window.SpectrAnalyzer.normalizeDb(measuredDb);
        if (Math.abs(actualAmount - expectedAmount) > 1e-9)
          throw new Error('dBFS display mapping diverged for amplitude '
            + amplitude + ': ' + actualAmount);
      }
      for (const [db, expected] of expectedDbfsMapping) {
        const actual = window.SpectrAnalyzer.normalizeDb(db);
        if (Math.abs(actual - expected) > 1e-9)
          throw new Error('incorrect dBFS normalization for ' + db + ': ' + actual);
      }
      const zeroY = 500, halfH = 400;
      for (const [db, expected] of expectedDbfsMapping) {
        const actualY = window.SpectrAnalyzer.project(
          window.SpectrAnalyzer.normalizeDb(db), zeroY, halfH);
        const expectedY = zeroY - expected * halfH * 0.95;
        if (Math.abs(actualY - expectedY) > 1e-9)
          throw new Error('incorrect dBFS projection for ' + db + ': ' + actualY);
      }
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
      step('analyzer-complete');

      const canvas = await spectrWaitFor(() => Array.from(document.querySelectorAll('canvas'))
        .filter(candidate => getComputedStyle(candidate).pointerEvents !== 'none')
        .sort((a, b) => {
          const ar = a.getBoundingClientRect(), br = b.getBoundingClientRect();
          return br.width * br.height - ar.width * ar.height;
        })[0], 'interactive filter canvas');
      const target = canvas.parentElement;
      const rect = target.getBoundingClientRect();
      const x = rect.left + rect.width * 0.43;
      const y = rect.top + rect.height * 0.46;

      const brushX = band => rect.left
        + (56 + (band + 0.5) * (target.clientWidth - 112) / 32)
          * rect.width / target.clientWidth;
      let count = spectrStatePosts().length;
      spectrPointer(target, 'pointerdown', brushX(2), y, 77, { shiftKey: true });
      spectrPointer(target, 'pointermove', brushX(6), y, 77, { shiftKey: true });
      spectrPointer(target, 'pointermove', brushX(4), y, 77, { shiftKey: true });
      spectrPointer(target, 'pointerup', brushX(4), y, 77, { shiftKey: true });
      let state = await spectrPublishAfter(count, 'shift unmute brush publication');
      for (let band = 2; band <= 6; ++band) {
        if (state.muted[band]) throw new Error('shift brush skipped band ' + band);
        if (state.gain_db[band] !== window.__spectrHydration.gain_db[band])
          throw new Error('shift brush lost authored gain at band ' + band);
      }
      count = spectrStatePosts().length;
      spectrPointer(target, 'pointerdown', brushX(2), y, 78, { shiftKey: true });
      spectrPointer(target, 'pointermove', brushX(6), y, 78, { shiftKey: true });
      spectrPointer(target, 'pointerup', brushX(6), y, 78, { shiftKey: true });
      state = await spectrPublishAfter(count, 'shift mute brush publication');
      for (let band = 2; band <= 6; ++band)
        if (!state.muted[band]) throw new Error('shift mute brush skipped band ' + band);
      step('shift-brush-complete');

      count = spectrStatePosts().length;
      await spectrTap(target, x, y, 2);
      state = await spectrPublishAfter(count, '2px jitter tap publication');
      const restoredBand = state.muted.findIndex(value => !value);
      if (restoredBand < 0) throw new Error('2px jitter tap did not restore a band');
      if (state.gain_db[restoredBand]
          !== window.__spectrHydration.gain_db[restoredBand])
        throw new Error('tap did not restore exact authored dB');
      const unmuteRender = window.__spectrTestHooks.renderState?.();
      const normalizedTarget = state.gain_db[restoredBand] / 24;
      if (!unmuteRender || unmuteRender.unmutePulse[restoredBand] <= 0
          || Math.sign(unmuteRender.gains[restoredBand]) !== Math.sign(normalizedTarget)
          || Math.abs(unmuteRender.gains[restoredBand]) > Math.abs(normalizedTarget) + 1e-6)
        throw new Error('unmute did not animate from center toward authored gain');
      if (!spectrFiniteState(state) || !spectrBundleClean())
        throw new Error('tap produced non-finite state or bundle error');
      step('jitter-tap-complete');

      count = spectrStatePosts().length;
      await spectrTap(target, x, y);
      state = await spectrPublishAfter(count, 'second tap publication');
      if (!state.muted[restoredBand]) throw new Error('second tap did not remute band');
      if (state.gain_db[restoredBand]
          !== window.__spectrHydration.gain_db[restoredBand])
        throw new Error('remute did not preserve exact authored dB');
      step('remute-complete');

      for (const [band, sign] of [[3, -1], [4, 0], [5, 1]]) {
        const bandX = rect.left + (56 + (band + 0.5) * (target.clientWidth - 112) / 32)
          * rect.width / target.clientWidth;
        count = spectrStatePosts().length;
        await spectrTap(target, bandX, y);
        state = await spectrPublishAfter(count, 'signed unmute publication ' + sign);
        if (state.muted[band]) throw new Error('signed unmute did not restore band ' + band);
        const rendered = window.__spectrTestHooks.renderState?.();
        if (!rendered || rendered.unmutePulse[band] <= 0)
          throw new Error('signed unmute did not pulse band ' + band);
        if (sign === 0 ? Math.abs(rendered.gains[band]) > 1e-6
          : Math.sign(rendered.gains[band]) !== sign)
          throw new Error('signed unmute originated on wrong side for band ' + band);
        count = spectrStatePosts().length;
        await spectrTap(target, bandX, y);
        state = await spectrPublishAfter(count, 'signed remute publication ' + sign);
        if (!state.muted[band]) throw new Error('signed remute failed for band ' + band);
      }

      count = spectrStatePosts().length;
      await spectrTap(target, x, y);
      state = await spectrPublishAfter(count, 'pre-drag unmute publication');
      if (state.muted[restoredBand]) throw new Error('pre-drag tap did not unmute band');
      const preDragGain = state.gain_db[restoredBand];
      count = spectrStatePosts().length;
      spectrPointer(target, 'pointerdown', x, y);
      spectrPointer(target, 'pointermove', x, y - 24);
      spectrPointer(target, 'pointerup', x, y - 24);
      await spectrFrames(3);
      state = await spectrPublishAfter(count, '>3px drag publication');
      if (state.muted[restoredBand]) throw new Error('drag toggled mute');
      if (state.gain_db[restoredBand] === preDragGain)
        throw new Error('>3px drag did not edit gain');
      if (!spectrFiniteState(state) || !spectrBundleClean())
        throw new Error('drag produced non-finite state or bundle error');
      step('drag-complete');

      Object.defineProperty(document, 'hasFocus', {
        configurable: true, value: () => true,
      });
      target.dispatchEvent(new MouseEvent('contextmenu', {
        bubbles: true, cancelable: true, clientX: x, clientY: y,
      }));
      await spectrWaitFor(() => document.querySelector(
        '[data-spectr-overlay="true"][aria-label="Band actions"]'),
      'band context menu');
      const modeBeforeBlockedKeys = spectrModeLabel();
      for (let digit = 1; digit <= 6; ++digit) {
        const event = spectrKey(String(digit), 'Digit' + digit);
        if (event.defaultPrevented)
          throw new Error('overlay leaked numeric key ownership: ' + digit);
      }
      await spectrFrames(2);
      if (spectrModeLabel() !== modeBeforeBlockedKeys)
        throw new Error('context menu allowed numeric shortcut');
      step('overlay-keys-complete');
      spectrKey('Escape', 'Escape');
      await spectrWaitFor(() => !document.querySelector(
        '[data-spectr-overlay="true"][aria-label="Band actions"]'),
      'context menu dismissal');
      step('context-menu-dismissed');

      const numeric = spectrKey('2', 'Digit2');
      if (!numeric.defaultPrevented) throw new Error('numeric shortcut was not owned');
      await spectrWaitFor(() => spectrModeLabel() === 'LEVEL', 'numeric mode shortcut');
      for (const key of ['a', 's', 'f', 'g', 'l']) {
        const event = spectrKey(key, 'Key' + key.toUpperCase());
        if (event.defaultPrevented)
          throw new Error('musical typing key was cancelled: ' + key.toUpperCase());
      }
      await spectrFrames(3);
      if (spectrModeLabel() !== 'LEVEL')
        throw new Error('musical typing key changed edit mode');
      if (!spectrStatePosts().every(message => spectrFiniteState(message.payload)))
        throw new Error('outbound processing state became non-finite');
      if (!spectrBundleClean()) throw new Error('bundle error before reopen');

      // Every footer popup is exercised through the rendered trigger, not a
      // component-local setter.  Each menu must honor both global dismissal
      // paths before one real option is selected and observed in the same
      // React commit that paints its trigger.
      const menuTrigger = key => {
        const root = document.querySelector('[data-spectr-menu-root="' + key + '"]');
        const host = root && root.querySelector('[data-spectr-menu-trigger]');
        return host && (host.matches('button') ? host : host.querySelector('button'));
      };
      const menuOptions = key => document.querySelector(
        '[data-spectr-menu-root="' + key + '"] [data-spectr-menu-options]');
      const openMenu = async key => {
        const trigger = menuTrigger(key);
        if (!trigger) throw new Error(key + ' menu trigger missing');
        await spectrClick(trigger);
        return spectrWaitFor(() => menuOptions(key), key + ' menu open');
      };
      const exerciseMenuDismissals = async key => {
        await openMenu(key);
        spectrKey('Escape', 'Escape');
        await spectrWaitFor(() => !menuOptions(key), key + ' menu Escape');
        await openMenu(key);
        await spectrOutsideClick(document.querySelector('[data-screen-label="Spectr main"]'));
        await spectrWaitFor(() => !menuOptions(key), key + ' menu outside click');
      };

      step('settings-start');
      const settingsOpen = document.querySelector('[data-spectr-settings-open]');
      if (!settingsOpen) throw new Error('settings open control missing');
      await spectrClick(settingsOpen);
      let settingsPanel = await spectrWaitFor(() => document.querySelector(
        '[data-spectr-settings-panel]'), 'settings panel');
      const option = settingsPanel.querySelector('[data-spectr-setting-option="warm"]')
        || settingsPanel.querySelector('[data-spectr-setting-option]');
      if (!option) throw new Error('settings option missing');
      await spectrClick(option);
      if (option.getAttribute('aria-pressed') !== 'true')
        throw new Error('settings option did not select');
      const toggle = settingsPanel.querySelector('[data-spectr-setting-toggle]');
      if (!toggle) throw new Error('settings toggle missing');
      const toggleBefore = toggle.getAttribute('aria-checked');
      await spectrClick(toggle);
      if (toggle.getAttribute('aria-checked') === toggleBefore)
        throw new Error('settings toggle did not change');
      await spectrClick(toggle);
      if (toggle.getAttribute('aria-checked') !== toggleBefore)
        throw new Error('settings toggle did not restore');
      const slider = settingsPanel.querySelector('[data-spectr-setting-slider]');
      if (!slider) throw new Error('settings slider missing');
      const sliderBefore = Number(slider.value);
      slider.value = sliderBefore > 0.5 ? '0.25' : '0.75';
      slider.dispatchEvent(new Event('input', { bubbles: true }));
      await spectrFrames(2);
      const sliderAfter = settingsPanel.querySelector('[data-spectr-setting-slider]');
      if (sliderAfter !== slider || Number(sliderAfter.value) === sliderBefore)
        throw new Error('settings slider remounted or did not change');
      const settingsClose = settingsPanel.querySelector('[data-spectr-settings-close]');
      if (!settingsClose) throw new Error('settings close control missing');
      await spectrClick(settingsClose);
      await spectrWaitFor(() => !document.querySelector('[data-spectr-settings-panel]'),
        'settings X dismissal');
      await spectrClick(settingsOpen);
      await spectrWaitFor(() => document.querySelector('[data-spectr-settings-panel]'),
        'settings reopen for Escape');
      spectrKey('Escape', 'Escape');
      await spectrWaitFor(() => !document.querySelector('[data-spectr-settings-panel]'),
        'settings Escape dismissal');
      await spectrClick(settingsOpen);
      settingsPanel = await spectrWaitFor(() => document.querySelector(
        '[data-spectr-settings-panel]'), 'settings reopen for outside click');
      await spectrClick(settingsPanel.parentElement);
      await spectrWaitFor(() => !document.querySelector('[data-spectr-settings-panel]'),
        'settings outside dismissal');
      step('settings-complete');

      const visualization = document.querySelector('[data-spectr-visualization]');
      const visualizationButtons = visualization
        ? Array.from(visualization.querySelectorAll('button')) : [];
      if (visualizationButtons.map(button => button.textContent.trim()).join(',')
          !== 'BARS,RESPONSE,BOTH')
        throw new Error('truthful mask visualization choices are missing');
      for (const label of ['BARS', 'RESPONSE', 'BOTH']) {
        await spectrClick(visualizationButtons.find(button =>
          button.textContent.trim() === label));
        if (!spectrBundleClean())
          throw new Error('mask visualization failed: ' + label);
      }

      const presetsButton = spectrButton('PRESETS ▾');
      if (!presetsButton) throw new Error('preset dropdown trigger missing');
      await spectrClick(presetsButton);
      const saveCurrent = await spectrWaitFor(() =>
        document.querySelector('[data-spectr-save-current]'), 'save current menu item');
      await spectrClick(saveCurrent);
      let saveName = await spectrWaitFor(() =>
        document.querySelector('[data-spectr-save-name]'), 'save naming dialog');
      if (saveName.value !== 'PATTERN 01')
        throw new Error('incremented default preset name was wrong: ' + saveName.value);
      saveName.dispatchEvent(new KeyboardEvent('keydown', {
        key: 'Escape', bubbles: true, cancelable: true,
      }));
      await spectrWaitFor(() => !document.querySelector('[data-spectr-save-dialog]'),
        'save dialog Escape');
      await spectrClick(presetsButton);
      await spectrClick(await spectrWaitFor(() =>
        document.querySelector('[data-spectr-save-current]'), 'save current reopen'));
      saveName = await spectrWaitFor(() =>
        document.querySelector('[data-spectr-save-name]'), 'save naming dialog reopen');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')
        .set.call(saveName, 'MY MASK');
      saveName.dispatchEvent(new Event('input', { bubbles: true }));
      await spectrFrames(2);
      saveName.dispatchEvent(new KeyboardEvent('keydown', {
        key: 'Enter', bubbles: true, cancelable: true,
      }));
      await spectrWaitFor(() => window.__spectrPosts.some(message =>
        message.type === 'save_current_pattern' && message.payload.name === 'MY MASK'),
      'native save current command');
      await spectrWaitFor(() => !document.querySelector('[data-spectr-save-dialog]'),
        'save dialog close');

      await spectrClick(presetsButton);
      await spectrClick(await spectrWaitFor(() => spectrButton('MANAGE…'),
        'preset manager menu item'));
      const manager = await spectrWaitFor(() => document.querySelector(
        '[aria-label="Pattern manager"]'), 'pattern manager');
      await spectrClick(await spectrWaitFor(() => manager.querySelector(
        '[data-spectr-pattern-id="user:test-0"]'), 'saved pattern row'));
      await spectrClick(await spectrWaitFor(() => Array.from(manager.querySelectorAll('button'))
        .find(button => button.textContent.trim() === '✎'), 'rename affordance'));
      const renameInput = await spectrWaitFor(() => Array.from(manager.querySelectorAll('input'))
        .find(input => input.value === 'MY MASK'), 'rename input');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')
        .set.call(renameInput, 'RENAMED MASK');
      renameInput.dispatchEvent(new Event('input', { bubbles: true }));
      await spectrFrames(2);
      renameInput.dispatchEvent(new KeyboardEvent('keydown', {
        key: 'Enter', bubbles: true, cancelable: true,
      }));
      await spectrWaitFor(() => window.__spectrPosts.some(message =>
        message.type === 'rename_pattern' && message.payload.name === 'RENAMED MASK'),
      'native rename command');
      await spectrClick(await spectrWaitFor(() => Array.from(manager.querySelectorAll('button'))
        .find(button => button.textContent.trim() === 'DELETE'), 'delete affordance'));
      await spectrWaitFor(() => window.__spectrPosts.some(message =>
        message.type === 'delete_pattern'), 'native delete command');
      await spectrWaitFor(() => !manager.querySelector('[data-spectr-pattern-id^="user:"]'),
        'deleted pattern removal');
      await spectrClick(Array.from(manager.querySelectorAll('button'))
        .find(button => button.textContent.trim() === '×'));
      step('preset-crud-complete');

      step('minimap-start');
      const beforeMinimap = spectrLatestState();
      const designWidth = target.clientWidth, designHeight = target.clientHeight;
      const fullMin = Math.log10(20), fullSpan = Math.log10(20000) - fullMin;
      const leftFraction = (Math.log10(beforeMinimap.min_hz) - fullMin) / fullSpan;
      const rightFraction = (Math.log10(beforeMinimap.max_hz) - fullMin) / fullSpan;
      const mapX = fraction => rect.left
        + (56 + fraction * (designWidth - 112)) * rect.width / designWidth;
      const middleDesignX = 56 + ((leftFraction + rightFraction) * 0.5)
        * (designWidth - 112);
      const miniDesignY = Array.from({ length: designHeight }, (_, index) => index)
        .find(candidate => window.__spectrTestHooks.minimapHit?.(
          middleDesignX, candidate) === 'window');
      if (!Number.isFinite(miniDesignY))
        throw new Error('minimap did not expose an interactive window');
      const miniY = rect.top + miniDesignY * rect.height / designHeight;
      const footerTop = settingsOpen.getBoundingClientRect().top;
      const minimapLabelBottom = miniY + 42 * rect.height / designHeight;
      if (minimapLabelBottom >= footerTop)
        throw new Error('minimap overlaps footer controls');
      count = spectrStatePosts().length;
      const trackX = mapX(leftFraction * 0.45);
      await spectrTap(target, trackX, miniY);
      state = await spectrPublishAfter(count, 'minimap track publication');
      if (state.min_hz === beforeMinimap.min_hz)
        throw new Error('minimap track did not recenter viewport');
      const trackState = state;
      const pannedLeftFraction = (Math.log10(state.min_hz) - fullMin) / fullSpan;
      const pannedRightFraction = (Math.log10(state.max_hz) - fullMin) / fullSpan;
      const dragStartX = mapX((pannedLeftFraction + pannedRightFraction) * 0.5);
      count = spectrStatePosts().length;
      spectrPointer(target, 'pointerdown', dragStartX, miniY, 41);
      spectrPointer(target, 'pointermove', dragStartX + rect.width * 0.04, miniY, 41);
      spectrPointer(target, 'pointerup', dragStartX + rect.width * 0.04, miniY, 41);
      state = await spectrPublishAfter(count, 'minimap pan publication');
      const beforeSpan = Math.log(trackState.max_hz / trackState.min_hz);
      const afterSpan = Math.log(state.max_hz / state.min_hz);
      if (Math.abs(afterSpan - beforeSpan) > 0.03
          || state.min_hz === trackState.min_hz)
        throw new Error('minimap pan did not preserve and move viewport');
      if (window.getSelection().toString() !== '')
        throw new Error('minimap drag selected text');
      const shiftedLeftFraction = (Math.log10(state.min_hz) - fullMin) / fullSpan;
      count = spectrStatePosts().length;
      const leftHandleX = mapX(shiftedLeftFraction);
      spectrPointer(target, 'pointerdown', leftHandleX, miniY, 42);
      spectrPointer(target, 'pointermove', leftHandleX + rect.width * 0.025, miniY, 42);
      spectrPointer(target, 'pointerup', leftHandleX + rect.width * 0.025, miniY, 42);
      const resizedView = await spectrPublishAfter(count, 'minimap resize publication');
      if (Math.abs(Math.log(resizedView.max_hz / resizedView.min_hz) - afterSpan) < 0.02)
        throw new Error('minimap handle did not resize viewport');
      step('minimap-complete');

      step('dropdowns-start');
      await exerciseMenuDismissals('bands');
      await openMenu('bands');
      await spectrClick(document.querySelector('[data-spectr-band-count="40"]'));
      await spectrWaitFor(() => window.__spectrTestHooks.appState?.().settings.bandCount === 40,
        '40-band selection');
      await openMenu('bands');
      await spectrClick(document.querySelector('[data-spectr-band-count="32"]'));
      await spectrWaitFor(() => window.__spectrTestHooks.appState?.().settings.bandCount === 32,
        '32-band restore');

      await exerciseMenuDismissals('edit');
      await openMenu('edit');
      await spectrClick(document.querySelector('[data-spectr-edit-mode="level"]'));
      await spectrWaitFor(() => window.__spectrTestHooks.appState?.().editMode === 'level',
        'edit dropdown selection');
      await openMenu('edit');
      await spectrClick(document.querySelector('[data-spectr-edit-mode="sculpt"]'));

      await exerciseMenuDismissals('analyzer');
      await openMenu('analyzer');
      await spectrClick(document.querySelector('[data-spectr-analyzer-mode="avg"]'));
      await spectrWaitFor(() => window.__spectrTestHooks.appState?.().analyzerMode === 'avg',
        'analyzer dropdown selection');
      await openMenu('analyzer');
      await spectrClick(document.querySelector('[data-spectr-analyzer-mode="peak"]'));

      await exerciseMenuDismissals('overflow');
      const beforeOverflow = spectrClone(spectrLatestState());
      count = spectrStatePosts().length;
      await openMenu('overflow');
      await spectrClick(document.querySelector('[data-spectr-overflow-action="invert"]'));
      const invertedOverflow = await spectrPublishAfter(count, 'overflow invert publication');
      if (JSON.stringify(invertedOverflow.gain_db) === JSON.stringify(beforeOverflow.gain_db))
        throw new Error('overflow selection did not mutate authoritative gains');
      if (invertedOverflow.min_hz !== beforeOverflow.min_hz
          || invertedOverflow.max_hz !== beforeOverflow.max_hz)
        throw new Error('non-view overflow action changed the minimap viewport');
      await spectrWaitFor(() => !menuOptions('overflow'), 'overflow action close');

      count = spectrStatePosts().length;
      await openMenu('overflow');
      await spectrClick(document.querySelector('[data-spectr-overflow-action="mute-all"]'));
      const mutedOverflow = await spectrPublishAfter(count, 'overflow mute-all publication');
      if (!mutedOverflow.muted.slice(0, 32).every(Boolean))
        throw new Error('overflow mute-all did not mute every active band');
      count = spectrStatePosts().length;
      await openMenu('overflow');
      await spectrClick(document.querySelector('[data-spectr-overflow-action="mute-all"]'));
      const unmutedOverflow = await spectrPublishAfter(count,
        'overflow immediate unmute-all publication');
      if (unmutedOverflow.muted.slice(0, 32).some(Boolean))
        throw new Error('overflow immediate second click reused stale mute state');

      await exerciseMenuDismissals('pattern');
      await openMenu('pattern');
      await spectrClick(document.querySelector('[data-spectr-pattern-menu-id="factory:flat"]'));
      await spectrWaitFor(() => !menuOptions('pattern'), 'pattern selection close');

      const helpTrigger = menuTrigger('help');
      if (!helpTrigger) throw new Error('help trigger missing');
      await spectrClick(helpTrigger);
      await spectrWaitFor(() => document.querySelector('[aria-label="Keyboard shortcuts"]'),
        'help open');
      spectrKey('Escape', 'Escape');
      await spectrWaitFor(() => !document.querySelector('[aria-label="Keyboard shortcuts"]'),
        'help Escape');
      await spectrClick(helpTrigger);
      await spectrWaitFor(() => document.querySelector('[aria-label="Keyboard shortcuts"]'),
        'help reopen');
      await spectrOutsideClick(document.querySelector('[data-screen-label="Spectr main"]'));
      await spectrWaitFor(() => !document.querySelector('[aria-label="Keyboard shortcuts"]'),
        'help outside click');
      step('dropdowns-complete');

      step('morph-start');
      const snapshotButtons = Array.from(document.querySelectorAll('button'))
        .filter(candidate => ['A', 'B'].includes(candidate.textContent.trim()))
        .sort((a, b) => b.getBoundingClientRect().top - a.getBoundingClientRect().top);
      const snapA = snapshotButtons.find(button => button.textContent.trim() === 'A');
      const snapB = snapshotButtons.find(button => button.textContent.trim() === 'B');
      if (!snapA || !snapB) throw new Error('snapshot controls missing');
      await spectrClick(snapA);
      count = spectrStatePosts().length;
      spectrPointer(target, 'pointerdown', x, y);
      spectrPointer(target, 'pointermove', x, y - 36);
      spectrPointer(target, 'pointerup', x, y - 36);
      await spectrPublishAfter(count, 'morph B edit publication');
      count = spectrStatePosts().length;
      await spectrTap(target, x, y);
      const beforeMorph = await spectrPublishAfter(count, 'morph B mute publication');
      if (!beforeMorph.muted[restoredBand])
        throw new Error('morph B fixture did not create mute disagreement');
      await spectrClick(snapB);
      const morph = await spectrWaitFor(() => document.querySelector('[data-spectr-morph]'),
        'morph slider');
      if (morph.disabled) throw new Error('morph remained disabled after A and B capture');
      count = spectrStatePosts().length;
      morph.value = '0.5';
      morph.dispatchEvent(new Event('input', { bubbles: true }));
      const morphMessage = await spectrWaitFor(() => window.__spectrPosts.find(message =>
        message.type === 'morph' && Math.abs(message.payload.t - 0.5) < 1e-6),
      'morph midpoint native command');
      if (!morphMessage)
        throw new Error('morph did not reach native bridge');
      await spectrWaitFor(() => window.__spectrTestHooks.renderState?.()
        .targetGains[restoredBand] === -Infinity, 'native midpoint projection');
      if (!window.__spectrNativeState.muted[restoredBand])
        throw new Error('native midpoint parity did not choose B mute');
      if (spectrStatePosts().length !== count)
        throw new Error('JS republished over native-owned morph state');
      step('morph-complete');

      step('initial-complete');
      window.dispatchEvent(new Event('pagehide'));
      if (window.SpectrAnalyzer.debugSnapshot() !== null
          || window.__spectrHandlers.analyzer_frame.size !== 0)
        throw new Error('analyzer survived initial document teardown');
      result.textContent = 'SPECTR_BROWSER_ORACLE_INITIAL_OK';
      document.documentElement.dataset.spectrOracle = 'INITIAL_OK';
    } catch (error) {
      result.textContent = 'SPECTR_BROWSER_ORACLE_FAIL:' + error.message;
      document.documentElement.dataset.spectrOracle = 'FAIL:' + error.message;
    }
  })();
};
setTimeout(window.spectrStartOracle, 0);
</script>`;
  html = html.replace('</body>', oracle + '</body>');
  html = html.replace('      window.Babel.transformScriptTags();',
    '      window.Babel.transformScriptTags();\n'
    + '      window.spectrStartOracle();');
  const instrumented = path.join(temp, 'spectr.html');
  fs.writeFileSync(instrumented, html);

  const runChrome = (url, profile, width, height) => spawnSync(chromePath, [
    '--headless=new', '--disable-gpu', '--disable-web-security',
    '--disable-background-networking', '--disable-component-update',
    '--disable-domain-reliability', '--disable-sync',
    '--allow-file-access-from-files', '--no-first-run', '--no-default-browser-check',
    `--window-size=${width},${height}`,
    '--virtual-time-budget=15000', `--user-data-dir=${profile}`,
    '--dump-dom', url,
  ], { encoding: 'utf8', timeout: 45000 });
  const failure = run => run.stdout.match(/SPECTR_BROWSER_ORACLE_FAIL:[^<]*/)?.[0]
    || run.stdout.match(/data-spectr-oracle="FAIL:[^"]*/)?.[0]
    || run.stdout.match(/<pre id="__spectr_browser_oracle"[^>]*>/)?.[0]
    || run.stderr;

  const initialUrl = `file://${instrumented}`;
  if (jsOnlyMode) {
    const run = runChrome(initialUrl + '?js-only=1',
      path.join(temp, 'profile-js-only'), 792, 516);
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /data-spectr-oracle="JS_ONLY_OK"/, failure(run));
    process.exitCode = 0;
  } else if (resizeOnlyMode) {
    for (const [label, width, height] of [
      ['minimum', 792, 516],
      ['preferred', 990, 645],
      ['authored', 1320, 860],
      ['enlarged', 1980, 1290],
    ]) {
      const run = runChrome(initialUrl + '?resize-only=1',
        path.join(temp, 'profile-resize-' + label), width, height);
      assert.equal(run.status, 0, run.stderr);
      assert.match(run.stdout, /data-spectr-oracle="RESIZE_OK"/, failure(run));
    }
    process.exitCode = 0;
  } else {
  const initial = runChrome(initialUrl, path.join(temp, 'profile-initial'), 1320, 860);
  assert.equal(initial.status, 0, initial.stderr);
  assert.match(initial.stdout, /data-spectr-oracle="INITIAL_OK"/, failure(initial));

  const scaled = runChrome(initialUrl + '?scaled=1',
    path.join(temp, 'profile-scaled'), 792, 516);
  assert.equal(scaled.status, 0, scaled.stderr);
  assert.match(scaled.stdout, /data-spectr-oracle="INITIAL_OK"/, failure(scaled));

  const preferred = runChrome(initialUrl + '?preferred=1',
    path.join(temp, 'profile-preferred'), 990, 645);
  assert.equal(preferred.status, 0, preferred.stderr);
  assert.match(preferred.stdout, /data-spectr-oracle="INITIAL_OK"/, failure(preferred));

  const enlarged = runChrome(initialUrl + '?enlarged=1',
    path.join(temp, 'profile-enlarged'), 1980, 1290);
  assert.equal(enlarged.status, 0, enlarged.stderr);
  assert.match(enlarged.stdout, /data-spectr-oracle="INITIAL_OK"/, failure(enlarged));

  const jsOnly = runChrome(initialUrl + '?js-only=1',
    path.join(temp, 'profile-js-only'), 792, 516);
  assert.equal(jsOnly.status, 0, jsOnly.stderr);
  assert.match(jsOnly.stdout, /data-spectr-oracle="JS_ONLY_OK"/, failure(jsOnly));

  // A second browser document models closing and reopening the native editor:
  // all JS state is gone, and only the finite native hydration may restore it.
  const reopened = runChrome(initialUrl + '?reopened=1',
    path.join(temp, 'profile-reopened'), 792, 516);
  assert.equal(reopened.status, 0, reopened.stderr);
  assert.match(reopened.stdout, /data-spectr-oracle="OK"/, failure(reopened));
  }
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
