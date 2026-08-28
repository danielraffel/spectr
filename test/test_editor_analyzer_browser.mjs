import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [htmlPath, chromePath, mode, modeArg] = process.argv.slice(2);
// --emit-instrumented DIR writes the instrumented oracle page and stops, so
// it can be driven in a real browser when a headless run cannot be trusted
// (a Chrome that will not settle fails every lane here identically).
const emitInstrumentedDir = mode === '--emit-instrumented' ? modeArg : null;
assert(htmlPath && chromePath, 'usage: test_editor_analyzer_browser.mjs HTML CHROME');
const resizeOnlyMode = mode === '--resize-only';
const jsOnlyMode = mode === '--js-only';
const muteModesMode = mode === '--mute-modes';
const responsivenessMode = mode === '--responsiveness';
const cursorsMode = mode === '--cursors';
const dropdownsMode = mode === '--dropdowns';
const bannerMode = mode === '--banner';

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
  window.__spectrRuntimeErrors.push({
    message: String(event.message || event.error || event.type),
    source: String(event.filename || ''),
    line: Number(event.lineno) || 0,
    column: Number(event.colno) || 0,
    stack: String(event.error && event.error.stack || ''),
  });
});
window.addEventListener('unhandledrejection', event => {
  window.__spectrRuntimeErrors.push({
    message: String(event.reason || 'unhandled rejection'),
    stack: String(event.reason && event.reason.stack || ''),
  });
});
const spectrOriginalFillText = CanvasRenderingContext2D.prototype.fillText;
CanvasRenderingContext2D.prototype.fillText = function(text, ...args) {
  window.__spectrCanvasLabels.push(String(text));
  return spectrOriginalFillText.call(this, text, ...args);
};
// WebKit rejects non-finite Canvas coordinates. Chromium is more permissive
// for several methods, so make the executable oracle enforce the stricter
// cross-engine contract.
// arcTo is how the imported design builds every rounded rect (mute chip, hover
// readout, minimap handles), so it belongs in the same net as the other path ops.
for (const method of [
  'arc', 'arcTo', 'bezierCurveTo', 'clearRect', 'createLinearGradient',
  'createRadialGradient', 'ellipse', 'fillRect', 'lineTo', 'moveTo',
  'quadraticCurveTo', 'rect', 'rotate', 'roundRect', 'scale', 'setTransform',
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
      const muteModes = new URL(location.href).searchParams.has('mute-modes');
      const responsiveness = new URL(location.href).searchParams.has('responsiveness');
      const cursors = new URL(location.href).searchParams.has('cursors');
      const dropdowns = new URL(location.href).searchParams.has('dropdowns');
      const banner = new URL(location.href).searchParams.has('banner');
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
      const hydrationLabel = reopened
        ? 'finite reopened hydration' : 'finite initial hydration';
      const hydrated = await spectrWaitFor(() => {
        const state = window.__spectrTestHooks.renderState?.();
        return state && state.nVisible === 32
          && state.targetGains.length === 32
          && state.targetGains.every(value => value === -Infinity)
          && state.mutedGainDb.every(Number.isFinite) && state;
      }, hydrationLabel).catch(error => {
        const state = window.__spectrTestHooks.renderState?.();
        throw new Error(error.message
          + '; state=' + JSON.stringify(state)
          + '; posts=' + JSON.stringify(window.__spectrPosts)
          + '; runtimeErrors=' + JSON.stringify(window.__spectrRuntimeErrors));
      });
      step(reopened ? 'reopened-hydrated' : 'initial-hydrated');
      if (cursors) {
        const canvas = Array.from(document.querySelectorAll('canvas'))
          .filter(candidate => getComputedStyle(candidate).pointerEvents !== 'none')
          .sort((a, b) => b.width * b.height - a.width * a.height)[0];
        const target = canvas && canvas.parentElement;
        if (!target) throw new Error('cursor canvas missing');
        const rect = target.getBoundingClientRect();
        const state = window.__spectrTestHooks.renderState();
        const fullMin = Math.log10(20);
        const fullSpan = Math.log10(20000) - fullMin;
        const left = (state.view.lmin - fullMin) / fullSpan;
        const right = (state.view.lmax - fullMin) / fullSpan;
        const mapX = fraction => rect.left
          + (56 + fraction * (target.clientWidth - 112))
            * rect.width / target.clientWidth;
        const centerX = mapX((left + right) * 0.5);
        const miniDesignY = Array.from({length: target.clientHeight}, (_, y) => y)
          .find(y => window.__spectrTestHooks.minimapHit(
            56 + (left + right) * 0.5 * (target.clientWidth - 112), y) === 'window');
        if (!Number.isFinite(miniDesignY)) throw new Error('cursor minimap y missing');
        const miniY = rect.top + miniDesignY * rect.height / target.clientHeight;
        const expectCursor = (x, expected, label) => {
          spectrPointer(target, 'pointermove', x, miniY, 82);
          const actual = getComputedStyle(target).cursor;
          if (actual !== expected) throw new Error(label + ' cursor was ' + actual);
        };
        for (const [x, label] of [[mapX(left), 'left'], [mapX(right), 'right']]) {
          expectCursor(x, 'ew-resize', label + ' handle hover');
          spectrPointer(target, 'pointerdown', x, miniY, 82);
          if (getComputedStyle(target).cursor !== 'ew-resize')
            throw new Error(label + ' handle press changed cursor');
          spectrPointer(target, 'pointerup', x, miniY, 82);
          if (getComputedStyle(target).cursor !== 'ew-resize')
            throw new Error(label + ' handle release changed cursor');
        }
        expectCursor(centerX, 'grab', 'viewport center hover');
        await spectrFrames(1);
        if (getComputedStyle(target).cursor !== 'grab')
          throw new Error('viewport center hover did not settle as grab');
        spectrPointer(target, 'pointerdown', centerX, miniY, 82);
        if (getComputedStyle(target).cursor !== 'grabbing')
          throw new Error('viewport center press was not grabbing');
        spectrPointer(target, 'pointermove', centerX + 8, miniY, 82);
        if (getComputedStyle(target).cursor !== 'grabbing')
          throw new Error('viewport center drag was not grabbing');
        spectrPointer(target, 'pointerup', centerX + 8, miniY, 82);
        const bandX = rect.left + rect.width * 0.5;
        const bandY = rect.top + rect.height * 0.45;
        spectrPointer(target, 'pointermove', bandX, bandY, 84);
        if (getComputedStyle(target).cursor !== 'crosshair')
          throw new Error('band draw surface was not crosshair');
        result.textContent = 'SPECTR_BROWSER_CURSORS_OK';
        document.documentElement.dataset.spectrOracle = 'CURSORS_OK';
        return;
      }
      if (dropdowns) {
        const root = document.querySelector('[data-spectr-menu-root="bands"]');
        const triggerHost = root && root.querySelector('[data-spectr-menu-trigger]');
        const trigger = triggerHost && (triggerHost.matches('button')
          ? triggerHost : triggerHost.querySelector('button'));
        if (!trigger) throw new Error('band-count trigger missing');
        const triggerStyle = getComputedStyle(trigger);
        if (triggerStyle.display !== 'inline-flex' || triggerStyle.alignItems !== 'center')
          throw new Error('band-count trigger lost its centered inline-flex layout');
        const number = trigger.querySelector('.tnum');
        if (!number) throw new Error('band-count number missing');
        const numberRect = number.getBoundingClientRect();
        const triggerRect = trigger.getBoundingClientRect();
        if (Math.abs((numberRect.top + numberRect.bottom)
            - (triggerRect.top + triggerRect.bottom)) > 1.5)
          throw new Error('band-count number is not vertically centered');
        await spectrClick(trigger);
        const popup = await spectrWaitFor(() => root.querySelector(
          '[data-spectr-menu-options]'), 'band-count popup');
        const options = Array.from(popup.querySelectorAll('button:not([disabled])'));
        if (options.length !== 5) throw new Error('band-count options changed');
        for (const option of options) {
          if (getComputedStyle(option).backgroundColor === 'rgba(0, 0, 0, 0)')
            throw new Error('dropdown item has no visible surface');
        }
        // Browser mode proves Spectr's semantic presentation and selection
        // callback. Pulp's owning-root N1 suite proves the framework-owned
        // hover/keyboard/outside-dismiss behavior on the shipping native path.
        await spectrClick(options[1]);
        await spectrWaitFor(() => !root.querySelector('[data-spectr-menu-options]'),
          'band-count selection dismissal');
        await spectrWaitFor(() => trigger.textContent.includes('40') && trigger,
          'band-count selected value projection');
        result.textContent = 'SPECTR_BROWSER_DROPDOWNS_OK';
        document.documentElement.dataset.spectrOracle = 'DROPDOWNS_OK';
        return;
      }
      if (banner) {
        Object.defineProperty(document, 'hasFocus', {
          configurable: true, value: () => true,
        });
        const editRoot = document.querySelector('[data-spectr-menu-root="edit"]');
        const editTriggerHost = editRoot.querySelector('[data-spectr-menu-trigger]');
        const editTrigger = editTriggerHost.querySelector('button');
        const chooseEdit = async mode => {
          editTrigger.click();
          await Promise.resolve();
          const option = await spectrWaitFor(() => editRoot.querySelector(
            '[data-spectr-edit-mode="' + mode + '"]'), mode + ' edit option');
          option.click();
          await Promise.resolve();
        };
        await chooseEdit('level');
        let status = await spectrWaitFor(() => document.querySelector(
          '[data-spectr-status-banner]'), 'initial action status');
        if (!/EDIT/.test(status.textContent))
          throw new Error('action status text was not truthful');
        const firstWidth = status.getBoundingClientRect().width;
        const replacementStarted = performance.now();
        await chooseEdit('boost');
        status = await spectrWaitFor(() => {
          const candidate = document.querySelector('[data-spectr-status-banner]');
          return candidate && /BOOST/.test(candidate.textContent) ? candidate : null;
        }, 'truthful replacement status');
        if (performance.now() - replacementStarted > 100)
          throw new Error('replacement status delayed the newest value');
        const motion = getComputedStyle(status);
        const durations = motion.transitionDuration.split(',').map(value =>
          value.trim().endsWith('ms') ? parseFloat(value) : parseFloat(value) * 1000);
        if (!motion.transitionProperty.split(',').map(value => value.trim()).includes('transform')
            || Math.max(...durations) > 200)
          throw new Error('status replacement motion exceeded its subtle bound');
        const transform = motion.transform;
        if (transform !== 'none') {
          const values = transform.match(/matrix\\(([^)]+)\\)/)?.[1].split(',').map(Number);
          if (values && (values[0] < 0.98 || values[0] > 1.001))
            throw new Error('status replacement local scale step was an outlier');
        }
        await spectrFrames(2);
        status = document.querySelector('[data-spectr-status-banner]');
        if (!status || getComputedStyle(status).transform.includes('0.985'))
          throw new Error('status replacement did not settle');
        const canvas = Array.from(document.querySelectorAll('canvas'))
          .filter(candidate => getComputedStyle(candidate).pointerEvents !== 'none')
          .sort((a, b) => b.width * b.height - a.width * a.height)[0];
        const target = canvas && canvas.parentElement;
        if (!target) throw new Error('status hover canvas missing');
        const rect = target.getBoundingClientRect();
        spectrPointer(target, 'pointermove', rect.left + rect.width * 0.25,
          rect.top + rect.height * 0.45, 91);
        await spectrFrames(1);
        status = document.querySelector('[data-spectr-status-banner]');
        if (!status || !/BAND \\d+\\/32/.test(status.textContent)
            || /BOOST/.test(status.textContent))
          throw new Error('hover did not immediately replace action status');
        if (status.getBoundingClientRect().top <= rect.top + 60 * rect.height / target.clientHeight)
          throw new Error('status banner overlaps the graph top rule');
        const hoverWidth = status.getBoundingClientRect().width;
        const statusStyle = getComputedStyle(status);
        const leftPadding = parseFloat(statusStyle.paddingLeft);
        const rightPadding = parseFloat(statusStyle.paddingRight);
        if (firstWidth === hoverWidth || Math.abs(leftPadding - rightPadding) > 0.01)
          throw new Error('status width/padding did not follow content symmetrically: '
            + JSON.stringify({ firstWidth, hoverWidth, leftPadding, rightPadding,
              text: status.textContent }));
        const settingsOpen = document.querySelector('[data-spectr-settings-open]');
        await spectrClick(settingsOpen);
        const setting = await spectrWaitFor(() => document.querySelector(
          '[data-spectr-status-info-setting] [role="switch"]'), 'status info setting');
        if (setting.getAttribute('aria-checked') !== 'true')
          throw new Error('status info setting was not default-on');
        await spectrClick(setting);
        await spectrWaitFor(() => !document.querySelector('[data-spectr-status-banner]'),
          'status info disablement');
        const close = document.querySelector('[data-spectr-settings-close]');
        await spectrClick(close);
        spectrPointer(target, 'pointermove', rect.left + rect.width * 0.55,
          rect.top + rect.height * 0.45, 91);
        await chooseEdit('level');
        await spectrFrames(3);
        if (document.querySelector('[data-spectr-status-banner]'))
          throw new Error('disabled status info still showed hover/action text');
        await spectrClick(settingsOpen);
        const disabledSetting = await spectrWaitFor(() => document.querySelector(
          '[data-spectr-status-info-setting] [role="switch"]'), 'disabled status setting');
        await spectrClick(disabledSetting);
        await spectrClick(document.querySelector('[data-spectr-settings-close]'));
        const originalMatchMedia = window.matchMedia;
        window.matchMedia = query => query.includes('prefers-reduced-motion')
          ? { matches: true } : originalMatchMedia(query);
        await chooseEdit('boost');
        status = await spectrWaitFor(() => document.querySelector(
          '[data-spectr-status-banner]'), 'reduced-motion status');
        if (getComputedStyle(status).transitionDuration !== '0s')
          throw new Error('status banner ignored reduced motion');
        result.textContent = 'SPECTR_BROWSER_BANNER_OK';
        document.documentElement.dataset.spectrOracle = 'BANNER_OK';
        return;
      }
      if (responsiveness) {
        const canvas = Array.from(document.querySelectorAll('canvas'))
          .filter(candidate => getComputedStyle(candidate).pointerEvents !== 'none')
          .sort((a, b) => {
            const ar = a.getBoundingClientRect(), br = b.getBoundingClientRect();
            return br.width * br.height - ar.width * ar.height;
          })[0];
        const target = canvas && canvas.parentElement;
        if (!target) throw new Error('responsive-drag canvas missing');
        const perfDiagnostics = window.SpectrPerfDiagnostics;
        if (!perfDiagnostics || typeof perfDiagnostics.enable !== 'function')
          throw new Error('opt-in performance diagnostics were unavailable');
        perfDiagnostics.enable();
        const perfVisible = Array.from({ length: 321 }, (_, index) => -120 + index * 144 / 320);
        const perfOverview = Array.from({ length: 121 }, (_, index) => -120 + index * 144 / 120);
        window.__spectrEmit('analyzer_frame', {
          schema_version: 1, epoch: 1, sequence_number: 1,
          dropped_frames: 0, source_channels: 2, fft_size: 8192,
          sample_rate: 48000, floor_db: -120, ceiling_db: 24,
          visible: { min_hz: 100, max_hz: 10000, magnitude_db: perfVisible },
          overview: { min_hz: 20, max_hz: 20000, magnitude_db: perfOverview },
        });
        const assertGridEquivalent = (name, minHz, maxHz, values) => {
          const grid = window.SpectrAnalyzer.grid(name, minHz, maxHz, values.length);
          if (!grid || !Object.isFrozen(grid))
            throw new Error(name + ' exact analyzer grid was unavailable or mutable');
          for (const index of [0, 1, Math.floor((grid.length - 1) / 2),
            grid.length - 2, grid.length - 1]) {
            const fraction = index / (grid.length - 1);
            const logFrequency = Math.log10(minHz)
              + fraction * (Math.log10(maxHz) - Math.log10(minHz));
            const scalar = window.SpectrAnalyzer.sample(logFrequency, 0, name);
            const bulk = window.SpectrAnalyzer.normalizeDb(grid[index]);
            if (Math.abs(scalar - bulk) > 1e-7)
              throw new Error(name + ' analyzer bulk/scalar mismatch at ' + index);
          }
        };
        assertGridEquivalent('visible', 100, 10000, perfVisible);
        assertGridEquivalent('overview', 20, 20000, perfOverview);
        if (window.SpectrAnalyzer.grid('visible', 100.00005, 10000, 321) !== null
            || window.SpectrAnalyzer.grid('visible', 20, 20000, 321) !== null
            || window.SpectrAnalyzer.grid('visible', 100, 10000, 320) !== null)
          throw new Error('analyzer grid accepted a range/count mismatch');
        await spectrFrames(3);
        const rect = target.getBoundingClientRect();
        const designX = value => rect.left + value * rect.width / target.clientWidth;
        const designY = value => rect.top + value * rect.height / target.clientHeight;
        const startX = designX(56 + 2.5 * (target.clientWidth - 112) / 32);
        const endX = designX(56 + 25.5 * (target.clientWidth - 112) / 32);
        const startY = designY(target.clientHeight * 0.62);
        const endY = designY(target.clientHeight * 0.34);
        const before = spectrStatePosts().length;
        const rawSamples = 96;
        spectrPointer(target, 'pointerdown', startX, startY, 73);
        for (let sample = 1; sample <= rawSamples; ++sample) {
          const t = sample / rawSamples;
          spectrPointer(target, 'pointermove',
            startX + (endX - startX) * t,
            startY + (endY - startY) * t, 73);
        }
        spectrPointer(target, 'pointerup', endX, endY, 73);
        if (spectrStatePosts().length !== before)
          throw new Error('raw drag samples crossed the native bridge before a frame');
        const publication = await spectrPublishAfter(before,
          'frame-coalesced responsive drag');
        if (spectrStatePosts().length - before !== 1)
          throw new Error('one drag frame produced '
            + (spectrStatePosts().length - before) + ' native publications');
        await spectrFrames(2);
        if (spectrStatePosts().length - before !== 1)
          throw new Error('responsive drag left a growing publication backlog');
        // The endpoint-only framework coalescer is safe because SCULPT fills
        // every band between the last delivered band and the newest one. This
        // pins that reconstruction: no gaps may appear across the collapsed
        // 23-band span.
        for (let band = 2; band <= 25; ++band) {
          if (publication.muted[band] || !Number.isFinite(publication.gain_db[band]))
            throw new Error('coalesced drag left a gap at band ' + band);
        }
        const perf = perfDiagnostics.snapshot();
        if (!perf || perf.rawPointerSamples !== rawSamples
            || perf.statePublications !== 1)
          throw new Error('diagnostics did not preserve raw/publication counts: '
            + JSON.stringify(perf));
        if (perf.animationFrames < 3 || perf.canvasRedraws !== perf.animationFrames
            || perf.analyzerSamples <= 0
            || perf.analyzerGridHits < perf.animationFrames
            || perf.analyzerGridMisses < 2)
          throw new Error('diagnostics did not cover frame/render/analyzer work');
        if (perf.inputToPublicationMs.length !== 1
            || !perf.inputToPublicationMs.every(value => Number.isFinite(value) && value >= 0))
          throw new Error('diagnostics did not record bounded input-to-publication latency');
        if (perf.frameIntervalsMs.length > 512 || perf.drawDurationsMs.length > 512)
          throw new Error('diagnostic sample buffers exceeded their bound');
        result.textContent = 'SPECTR_BROWSER_RESPONSIVENESS_OK';
        document.documentElement.dataset.spectrOracle = 'RESPONSIVENESS_OK';
        return;
      }
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
      const bannerStyle = getComputedStyle(statusBanner);
      if (bannerStyle.display !== 'flex'
          || bannerStyle.alignItems !== 'center'
          || bannerStyle.justifyContent !== 'center')
        throw new Error('status banner content was not centered');
      if (bannerStyle.paddingLeft !== bannerStyle.paddingRight
          || parseFloat(bannerStyle.paddingLeft) <= 0)
        throw new Error('status banner padding was not symmetric');
      if (!bannerStyle.transitionProperty.split(',').map(value => value.trim()).includes('width'))
        throw new Error('status banner width did not animate');
      if (parseFloat(bannerStyle.top) < 70)
        throw new Error('status banner remained above the plot top line');
      await spectrWaitFor(() =>
        !document.querySelector('[data-spectr-status-banner]'),
      'expired status banner removal');

      if (muteModes) {
        // Drawing over a muted band used to mean five different things: SCULPT
        // and LEVEL cleared the mute, BOOST, FLARE and GLIDE preserved it. One
        // setting now decides for all of them. Asserting the SHARED DECISION
        // POINT would prove nothing about that, so this asserts the published
        // band state after a real drag through each mode's real handler.
        Object.defineProperty(document, 'hasFocus', {
          configurable: true, value: () => true,
        });
        const canvas = await spectrWaitFor(() => Array.from(
          document.querySelectorAll('canvas'))
          .filter(candidate => getComputedStyle(candidate).pointerEvents !== 'none')
          .sort((a, b) => {
            const ar = a.getBoundingClientRect(), br = b.getBoundingClientRect();
            return br.width * br.height - ar.width * ar.height;
          })[0], 'interactive filter canvas');
        const target = canvas.parentElement;
        const rect = target.getBoundingClientRect();
        const bandX = band => rect.left
          + (56 + (band + 0.5) * (target.clientWidth - 112) / 32)
            * rect.width / target.clientWidth;
        const midY = rect.top + rect.height * 0.46;

        const setRedrawUnmutes = async enabled => {
          // The toggle lives in the overflow menu, not beside its siblings in
          // the settings panel: the native materialized runtime paints an
          // ADDED child at its container's first child position, so a new
          // settings row overlapped an existing one. See
          // tools/patch_materialized_editor.py.
          const root = document.querySelector('[data-spectr-menu-root="overflow"]');
          if (!root) throw new Error('overflow menu root missing');
          const host = root.querySelector('[data-spectr-menu-trigger]');
          const trigger = host.matches('button') ? host : host.querySelector('button');
          if (!trigger) throw new Error('overflow menu trigger missing');
          await spectrClick(trigger);
          const item = await spectrWaitFor(
            () => document.querySelector('[data-spectr-redraw-unmutes]'),
            'redraw-unmutes overflow item');
          if ((item.getAttribute('data-spectr-redraw-unmutes') === 'on') !== enabled)
            await spectrClick(item);
          else
            await spectrClick(trigger);
          // Absent means on, the same reading every call site uses, so compare
          // through that rather than against a literal true.
          await spectrWaitFor(
            () => (window.__spectrTestHooks.appState?.().settings.unmuteOnDraw
              !== false) === enabled,
            'redraw-unmutes reaching settings state');
          await spectrWaitFor(
            () => !document.querySelector('[data-spectr-redraw-unmutes]'),
            'overflow menu dismissal');
        };

        const MODES = [
          ['sculpt', '1'], ['level', '2'], ['boost', '3'],
          ['flare', '4'], ['glide', '5'],
        ];
        // Hydration starts every band muted, so the fixture re-mutes only
        // when a previous mode's edit cleared it. GLIDE reaches four bands
        // either side, which is exactly why each check re-establishes its own
        // precondition instead of trusting the band it was handed.
        const nativeNow = () => spectrLatestState() || window.__spectrNativeState;
        const ensureMuted = async (x, label) => {
          let state = nativeNow();
          const band = Number(label.band);
          if (!state.muted[band]) {
            const count = spectrStatePosts().length;
            await spectrTap(target, x, midY);
            state = await spectrPublishAfter(count, label.name + ' mute fixture');
          }
          if (!state.muted[band])
            throw new Error(label.name + ': fixture band was not muted');
          return state;
        };

        let band = 2;
        for (const enabled of [true, false]) {
          await setRedrawUnmutes(enabled);
          for (const [modeName, digit] of MODES) {
            spectrKey(digit, 'Digit' + digit);
            await spectrWaitFor(
              () => window.__spectrTestHooks.appState?.().editMode === modeName,
              modeName + ' mode selection');
            const x = bandX(band);
            const label = { name: modeName + (enabled ? ' (ON)' : ' (OFF)'), band };

            let state = await ensureMuted(x, label);
            const mutedDb = state.gain_db[band];

            // A real >3px drag through this mode's own pointer handler.
            const count = spectrStatePosts().length;
            spectrPointer(target, 'pointerdown', x, midY);
            spectrPointer(target, 'pointermove', x, midY - 30);
            spectrPointer(target, 'pointermove', x, midY - 60);
            spectrPointer(target, 'pointerup', x, midY - 60);
            await spectrFrames(3);
            state = await spectrPublishAfter(count, label.name + ' draw publication');

            if (enabled) {
              if (state.muted[band])
                throw new Error(label.name + ': left the band muted');
              if (!Number.isFinite(state.gain_db[band]))
                throw new Error(label.name + ': unmuted band has non-finite dB');
              if (state.gain_db[band] === mutedDb)
                throw new Error(label.name + ': did not take the drawn gain');
            } else {
              if (!state.muted[band])
                throw new Error(label.name + ': cleared the mute');
              if (state.gain_db[band] !== mutedDb)
                throw new Error(label.name + ': disturbed the stashed gain');
            }
            if (!spectrFiniteState(state) || !spectrBundleClean())
              throw new Error(label.name + ': draw produced non-finite state');
            band += 2;
          }
        }

        // An explicit mute command stays a command under either setting: the
        // policy governs drawing, it is not a veto on saying "mute".
        for (const enabled of [false, true]) {
          await setRedrawUnmutes(enabled);
          const x = bandX(30);
          const before = nativeNow().muted[30];
          const count = spectrStatePosts().length;
          await spectrTap(target, x, midY);
          const after = await spectrPublishAfter(count, 'single-tap toggle');
          if (after.muted[30] === before)
            throw new Error('single-tap mute toggle stopped working with '
              + 'redraw-unmutes ' + enabled);
        }
        result.textContent = 'SPECTR_BROWSER_MUTE_MODES_OK';
        document.documentElement.dataset.spectrOracle = 'MUTE_MODES_OK';
        return;
      }
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
        // spectr#35 / spectr#44 regression. Control state legitimately carries
        // the -Infinity mute sentinel; projected canvas coordinates never may,
        // and a muted band belongs ON the 0 dB line beside its mute badge, not
        // at the axis floor. A snapshot hydrate followed by a morph is the
        // exact combination that produced both symptoms, and it is the one the
        // close/reconstruct rig never reached.
        await spectrWaitFor(() =>
          window.__spectrTestHooks.renderState?.().bandGeometry?.responseY?.length,
        'projected band geometry after reopened morph');
        const projected = window.__spectrTestHooks.renderState();
        const geometry = projected.bandGeometry;
        if (!Number.isFinite(geometry.zeroY) || !Number.isFinite(geometry.halfH)
            || geometry.halfH <= 0)
          throw new Error('projected band geometry frame was not finite: '
            + JSON.stringify({ zeroY: geometry.zeroY, halfH: geometry.halfH }));
        for (const key of ['responseY', 'topY', 'botY']) {
          const coordinates = geometry[key];
          if (coordinates.length !== projected.nVisible)
            throw new Error('projected ' + key + ' did not cover every band: '
              + coordinates.length + ' of ' + projected.nVisible);
          const offending = coordinates.findIndex(value => !Number.isFinite(value));
          if (offending !== -1)
            throw new Error('non-finite projected ' + key + ' at band '
              + offending + ': ' + coordinates[offending]);
          // The band body legitimately animates between 0 dB and the collapse
          // floor, so only bound it to the plot. The mask response curve has no
          // transition and must sit exactly on 0 dB for a muted band.
          const bound = geometry.halfH * 1.05;
          const strayed = coordinates.findIndex(value =>
            value < geometry.zeroY - bound || value > geometry.zeroY + bound);
          if (strayed !== -1)
            throw new Error('projected ' + key + ' left the plot at band '
              + strayed + ': ' + coordinates[strayed]);
          if (key !== 'responseY') continue;
          const floored = projected.targetGains.findIndex((gain, index) =>
            gain === -Infinity
            && Math.abs(coordinates[index] - geometry.zeroY) > 1e-6);
          if (floored !== -1)
            throw new Error('muted band ' + floored
              + ' left the 0 dB line in the mask response: '
              + coordinates[floored] + ' (0 dB at ' + geometry.zeroY
              + ', axis floor at ' + (geometry.zeroY + geometry.halfH) + ')');
        }
        if (!projected.targetGains.includes(-Infinity))
          throw new Error('mask response 0 dB contract was not exercised: '
            + 'no muted band survived the reopened morph');
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
      const assertExactGrid = (name, trace, minHz, maxHz) => {
        const grid = window.SpectrAnalyzer.grid(
          name, minHz, maxHz, trace.magnitude_db.length);
        if (!grid || !Object.isFrozen(grid))
          throw new Error(name + ' exact bulk analyzer grid was unavailable or mutable');
        for (const index of [0, 1, Math.floor((grid.length - 1) / 2),
          grid.length - 2, grid.length - 1]) {
          const fraction = index / (grid.length - 1);
          const logFrequency = Math.log10(minHz)
            + fraction * (Math.log10(maxHz) - Math.log10(minHz));
          const scalar = window.SpectrAnalyzer.sample(logFrequency, 0, name);
          const bulk = window.SpectrAnalyzer.normalizeDb(grid[index]);
          if (Math.abs(scalar - bulk) > 1e-7)
            throw new Error(name + ' bulk/scalar mismatch at ' + index);
        }
      };
      assertExactGrid('visible', payload.visible, 20, 20000);
      assertExactGrid('overview', payload.overview, 20, 20000);
      if (window.SpectrAnalyzer.grid('visible', 100, 10000, 321) !== null
          || window.SpectrAnalyzer.grid('visible', 20, 20000, 320) !== null)
        throw new Error('bulk analyzer grid accepted a zoom/count mismatch');
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

      spectrPointer(target, 'pointermove', x, y);
      const hoverBanner = await spectrWaitFor(() => {
        const candidate = document.querySelector('[data-spectr-status-banner]');
        return candidate && /BAND \\d+\\/32/.test(candidate.textContent) && candidate;
      }, 'hover readout in unified status banner');
      if (window.__spectrCanvasLabels.some(label => /band \\d+\\/32/i.test(label)))
        throw new Error('hover readout still painted a floating canvas tooltip');
      if (!hoverBanner.textContent.trim())
        throw new Error('unified hover banner was empty');
      // React synthesizes onPointerLeave from the bubbling pointerout event;
      // dispatching native pointerleave directly bypasses its delegated handler.
      spectrPointer(target, 'pointerout', x, y);
      await spectrWaitFor(() => !document.querySelector('[data-spectr-status-banner]'),
        'hover banner clears on pointer leave');

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
      for (const key of ['edit', 'analyzer', 'pattern']) {
        const trigger = menuTrigger(key);
        const style = trigger && getComputedStyle(trigger);
        if (!style || style.display !== 'inline-flex' || style.alignItems !== 'center')
          throw new Error(key + ' bottom-rail trigger was not vertically centered');
      }
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
      const cursorAt = async (cursorX, expected, label) => {
        spectrPointer(target, 'pointermove', cursorX, miniY, 40);
        await spectrFrames(1);
        if (getComputedStyle(target).cursor !== expected)
          throw new Error(label + ' cursor was ' + getComputedStyle(target).cursor
            + ', expected ' + expected);
      };
      const rightHandleX = mapX(rightFraction);
      const windowX = mapX((leftFraction + rightFraction) * 0.5);
      const trackX = mapX(leftFraction * 0.45);
      await cursorAt(mapX(leftFraction), 'ew-resize', 'left minimap handle');
      await cursorAt(rightHandleX, 'ew-resize', 'right minimap handle');
      await cursorAt(windowX, 'grab', 'minimap window');
      await cursorAt(trackX, 'pointer', 'minimap track');
      spectrPointer(target, 'pointerdown', windowX, miniY, 40);
      if (getComputedStyle(target).cursor !== 'grabbing')
        throw new Error('active minimap cursor was not grabbing');
      spectrPointer(target, 'pointerup', windowX, miniY, 40);
      if (getComputedStyle(target).cursor !== 'grab')
        throw new Error('released minimap cursor did not restore grab');
      spectrPointer(target, 'pointermove', x, y, 40);
      if (getComputedStyle(target).cursor !== 'crosshair')
        throw new Error('band adjustment cursor was not preserved');
      count = spectrStatePosts().length;
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
      if (getComputedStyle(target).cursor !== 'ew-resize')
        throw new Error('active minimap resize cursor was not horizontal resize');
      spectrPointer(target, 'pointermove', leftHandleX + rect.width * 0.025, miniY, 42);
      if (getComputedStyle(target).cursor !== 'ew-resize')
        throw new Error('dragged minimap resize cursor changed role');
      spectrPointer(target, 'pointerup', leftHandleX + rect.width * 0.025, miniY, 42);
      if (getComputedStyle(target).cursor !== 'ew-resize')
        throw new Error('released minimap resize cursor did not remain horizontal resize');
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
  if (emitInstrumentedDir) {
    const emitted = path.join(emitInstrumentedDir, 'spectr-instrumented.html');
    fs.copyFileSync(instrumented, emitted);
    console.log(emitted);
  } else {

  // Chrome 151 for Testing hangs before navigation when native headless is
  // given a brand-new --user-data-dir on macOS. Let native headless create its
  // own ephemeral profile and add incognito explicitly: each call remains an
  // isolated process, while avoiding the broken explicit-profile startup path.
  const runChrome = (url, width, height) => spawnSync(chromePath, [
    '--headless=new', '--disable-gpu', '--disable-web-security',
    '--disable-background-networking', '--disable-component-update',
    '--disable-domain-reliability', '--disable-sync', '--incognito',
    '--allow-file-access-from-files', '--no-first-run', '--no-default-browser-check',
    `--window-size=${width},${height}`,
    '--virtual-time-budget=15000', '--dump-dom', url,
  ], { encoding: 'utf8', timeout: 45000, maxBuffer: 64 * 1024 * 1024 });
  const failure = run => run.stdout.match(/data-spectr-oracle="FAIL:[^"]*/)?.[0]
    || run.stdout.match(/<pre id="__spectr_browser_oracle"[^>]*>([^<]*)<\/pre>/)?.[1]
    || run.error?.stack || run.stderr || 'Chrome exited without diagnostics';

  const initialUrl = `file://${instrumented}`;
  if (cursorsMode) {
    const run = runChrome(initialUrl + '?cursors=1', 1320, 860);
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /data-spectr-oracle="CURSORS_OK"/, failure(run));
    process.exitCode = 0;
  } else if (dropdownsMode) {
    const run = runChrome(initialUrl + '?dropdowns=1', 1320, 860);
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /data-spectr-oracle="DROPDOWNS_OK"/, failure(run));
    process.exitCode = 0;
  } else if (bannerMode) {
    const run = runChrome(initialUrl + '?banner=1', 1320, 860);
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /data-spectr-oracle="BANNER_OK"/, failure(run));
    process.exitCode = 0;
  } else if (responsivenessMode) {
    const run = runChrome(initialUrl + '?responsiveness=1', 1320, 860);
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /data-spectr-oracle="RESPONSIVENESS_OK"/, failure(run));
    process.exitCode = 0;
  } else if (muteModesMode) {
    const run = runChrome(initialUrl + '?mute-modes=1', 1320, 860);
    assert.equal(run.status, 0, run.stderr);
    assert.match(run.stdout, /data-spectr-oracle="MUTE_MODES_OK"/, failure(run));
    process.exitCode = 0;
  } else if (jsOnlyMode) {
    const run = runChrome(initialUrl + '?js-only=1', 792, 516);
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
      const run = runChrome(initialUrl + '?resize-only=1', width, height);
      assert.equal(run.status, 0, run.stderr);
      assert.match(run.stdout, /data-spectr-oracle="RESIZE_OK"/, failure(run));
    }
    process.exitCode = 0;
  } else {
  const initial = runChrome(initialUrl, 1320, 860);
  assert.equal(initial.status, 0, initial.stderr);
  assert.match(initial.stdout, /data-spectr-oracle="INITIAL_OK"/, failure(initial));

  const scaled = runChrome(initialUrl + '?scaled=1', 792, 516);
  assert.equal(scaled.status, 0, scaled.stderr);
  assert.match(scaled.stdout, /data-spectr-oracle="INITIAL_OK"/, failure(scaled));

  const preferred = runChrome(initialUrl + '?preferred=1', 990, 645);
  assert.equal(preferred.status, 0, preferred.stderr);
  assert.match(preferred.stdout, /data-spectr-oracle="INITIAL_OK"/, failure(preferred));

  const enlarged = runChrome(initialUrl + '?enlarged=1', 1980, 1290);
  assert.equal(enlarged.status, 0, enlarged.stderr);
  assert.match(enlarged.stdout, /data-spectr-oracle="INITIAL_OK"/, failure(enlarged));

  const jsOnly = runChrome(initialUrl + '?js-only=1', 792, 516);
  assert.equal(jsOnly.status, 0, jsOnly.stderr);
  assert.match(jsOnly.stdout, /data-spectr-oracle="JS_ONLY_OK"/, failure(jsOnly));

  // A second browser document models closing and reopening the native editor:
  // all JS state is gone, and only the finite native hydration may restore it.
  const reopened = runChrome(initialUrl + '?reopened=1', 792, 516);
  assert.equal(reopened.status, 0, reopened.stderr);
  assert.match(reopened.stdout, /data-spectr-oracle="OK"/, failure(reopened));
  }
  }
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
