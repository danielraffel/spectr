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
window.__spectrPosts = [];
window.__spectrTestHooks = Object.create(null);
window.__spectrCanvasLabels = [];
const spectrOriginalFillText = CanvasRenderingContext2D.prototype.fillText;
CanvasRenderingContext2D.prototype.fillText = function(text, ...args) {
  window.__spectrCanvasLabels.push(String(text));
  return spectrOriginalFillText.call(this, text, ...args);
};
// Headless --dump-dom may throttle RAF after first paint. A timer-backed RAF
// keeps the production animation/effect callbacks ordered and deterministic.
window.requestAnimationFrame = callback => setTimeout(
  () => callback(performance.now()), 16);
window.cancelAnimationFrame = handle => clearTimeout(handle);
// Synthetic PointerEvents are not registered with Chromium's hardware pointer
// tracker, so capture would otherwise throw before React reaches pointerup.
Element.prototype.setPointerCapture = () => {};
Element.prototype.releasePointerCapture = () => {};
window.__spectrHydration = {
  n_visible: 32,
  gain_db: Array.from({ length: 32 }, (_, index) => -15 + index * 0.5),
  muted: new Array(32).fill(true),
  min_hz: 100,
  max_hz: 10000,
};
window.pulp = {
  on(type, callback) {
    (window.__spectrHandlers[type] ||= new Set()).add(callback);
    return () => window.__spectrHandlers[type].delete(callback);
  },
  postMessage(type, payload, id) {
    window.__spectrPosts.push({ type, payload, id });
    if (type === 'editor_ready') {
      queueMicrotask(() => window.__spectrEmit(
        'processing_state_hydrate', window.__spectrHydration));
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
const spectrBundleClean = () => !document.getElementById('__bundler_err');
const spectrPublishAfter = async (count, label) => spectrWaitFor(
  () => spectrStatePosts().length > count && spectrLatestState(), label);
const spectrPointer = (target, type, x, y, pointerId = 7) => {
  target.dispatchEvent(new PointerEvent(type, {
    bubbles: true,
    cancelable: true,
    pointerId,
    pointerType: 'mouse',
    isPrimary: true,
    button: type === 'pointermove' ? -1 : 0,
    buttons: type === 'pointerup' ? 0 : 1,
    clientX: x,
    clientY: y,
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
const spectrButton = label => Array.from(document.querySelectorAll('button'))
  .find(candidate => candidate.textContent.trim() === label);

setTimeout(() => {
  const result = document.createElement('pre');
  result.id = '__spectr_browser_oracle';
  document.body.appendChild(result);
  (async () => {
    try {
      const step = value => { result.dataset.step = value; };
      const reopened = new URL(location.href).searchParams.has('reopened');
      step(reopened ? 'reopened-start' : 'initial-start');
      const bodyRect = document.body.getBoundingClientRect();
      if (document.body.clientWidth !== 1320 || document.body.clientHeight !== 860)
        throw new Error('fixed editor design space changed');
      if (Math.abs(bodyRect.width / bodyRect.height - 1320 / 860) > 0.002)
        throw new Error('editor did not scale proportionally');
      if (bodyRect.width > innerWidth + 0.5 || bodyRect.height > innerHeight + 0.5)
        throw new Error('editor overflowed its host viewport');
      const hydrated = await spectrWaitFor(() => {
        const state = spectrLatestState();
        return spectrFiniteState(state) && state.muted.every(Boolean) && state;
      }, reopened ? 'finite reopened hydration' : 'finite initial hydration');
      step(reopened ? 'reopened-hydrated' : 'initial-hydrated');
      await spectrFrames(5);
      if (!spectrBundleClean()) throw new Error('bundle error after hydrated RAFs');
      if (!window.__spectrCanvasLabels.includes('dBFS')
          || !window.__spectrCanvasLabels.some(label => /^-(30|60|90|120)$/.test(label)))
        throw new Error('independent negative dBFS ruler was not drawn');
      if (!hydrated.gain_db.every((gain, index) =>
        gain === window.__spectrHydration.gain_db[index]))
        throw new Error('hydration changed authored dB');

      if (reopened) {
        if ((window.__spectrHandlers.processing_state_hydrate?.size || 0) !== 1)
          throw new Error('reopened hydration listener count is not one');
        if (!spectrFiniteState(spectrLatestState()))
          throw new Error('reopened publication is non-finite');
        window.dispatchEvent(new Event('pagehide'));
        if (window.SpectrAnalyzer.debugSnapshot() !== null
            || window.__spectrHandlers.analyzer_frame.size !== 0)
          throw new Error('analyzer survived reopened document teardown');
        result.textContent = 'SPECTR_BROWSER_ORACLE_OK';
        return;
      }

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

      let count = spectrStatePosts().length;
      await spectrTap(target, x, y, 2);
      let state = await spectrPublishAfter(count, '2px jitter tap publication');
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

      const engineBadge = document.querySelector('[aria-label="Spectral mask engine"]');
      if (!engineBadge || engineBadge.tagName === 'BUTTON'
          || engineBadge.textContent.trim() !== 'SPECTRAL')
        throw new Error('engine identity is not a static truthful badge');

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
      await spectrClick(snapB);
      const morph = await spectrWaitFor(() => document.querySelector('[data-spectr-morph]'),
        'morph slider');
      if (morph.disabled) throw new Error('morph remained disabled after A and B capture');
      count = spectrStatePosts().length;
      morph.value = '0.5';
      morph.dispatchEvent(new Event('input', { bubbles: true }));
      await spectrPublishAfter(count, 'morph midpoint publication');
      if (!window.__spectrPosts.some(message => message.type === 'morph'
          && Math.abs(message.payload.t - 0.5) < 1e-6))
        throw new Error('morph did not reach native bridge');
      step('morph-complete');

      step('initial-complete');
      window.dispatchEvent(new Event('pagehide'));
      if (window.SpectrAnalyzer.debugSnapshot() !== null
          || window.__spectrHandlers.analyzer_frame.size !== 0)
        throw new Error('analyzer survived initial document teardown');
      result.textContent = 'SPECTR_BROWSER_ORACLE_INITIAL_OK';
    } catch (error) {
      result.textContent = 'SPECTR_BROWSER_ORACLE_FAIL:' + error.message;
    }
  })();
}, 0);
</script>`;
  html = html.replace('</body>', oracle + '</body>');
  const instrumented = path.join(temp, 'spectr.html');
  fs.writeFileSync(instrumented, html);

  const runChrome = (url, profile, width, height) => spawnSync(chromePath, [
    '--headless=new', '--disable-gpu', '--disable-web-security',
    '--disable-background-networking', '--disable-component-update',
    '--disable-domain-reliability', '--disable-sync',
    '--allow-file-access-from-files', '--no-first-run', '--no-default-browser-check',
    `--window-size=${width},${height}`,
    '--virtual-time-budget=5000', `--user-data-dir=${profile}`,
    '--dump-dom', url,
  ], { encoding: 'utf8', timeout: 45000 });
  const failure = run => run.stdout.match(/SPECTR_BROWSER_ORACLE_FAIL:[^<]*/)?.[0]
    || run.stdout.match(/<pre id="__spectr_browser_oracle"[^>]*>/)?.[0]
    || run.stderr;

  const initialUrl = `file://${instrumented}`;
  const initial = runChrome(initialUrl, path.join(temp, 'profile-initial'), 1320, 860);
  assert.equal(initial.status, 0, initial.stderr);
  assert.match(initial.stdout, /SPECTR_BROWSER_ORACLE_INITIAL_OK/, failure(initial));

  // A second browser document models closing and reopening the native editor:
  // all JS state is gone, and only the finite native hydration may restore it.
  const reopened = runChrome(initialUrl + '?reopened=1',
    path.join(temp, 'profile-reopened'), 800, 521);
  assert.equal(reopened.status, 0, reopened.stderr);
  assert.match(reopened.stdout, /SPECTR_BROWSER_ORACLE_OK/, failure(reopened));
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
