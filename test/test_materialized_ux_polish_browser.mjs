import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [documentPath, browserHtmlPath, chromePath] = process.argv.slice(2);
assert(documentPath && browserHtmlPath && chromePath,
  'usage: test_materialized_ux_polish_browser.mjs DOCUMENT_JSON BROWSER_HTML CHROME');

const document = JSON.parse(fs.readFileSync(documentPath, 'utf8'));
assert.equal(typeof document.html, 'string', 'materialized document has no HTML');
const surfaceStart = document.html.indexOf('function SpectrSettingsGroup(');
const surfaceEnd = document.html.indexOf('\nfunction SnapBtn(', surfaceStart);
assert(surfaceStart >= 0 && surfaceEnd > surfaceStart,
  'shipping Settings/status surface missing');
const shippingSurface = document.html.slice(surfaceStart, surfaceEnd);
const modulationSurface = document.html.slice(document.html.indexOf('function SpectrModulationSettings'));
// The shipped settings defaults, read out of the document rather than restated
// here. The driven text-size scenario below is handed THESE values, so it
// proves what Spectr actually ships defaults to -- not what this file believes.
const shippedDefaults = JSON.parse(document.html
  .slice(document.html.indexOf('id="tweak-defaults"'))
  .match(/\/\*EDITMODE-BEGIN\*\/([\s\S]*?)\/\*EDITMODE-END\*\//)[1]);
assert.equal(typeof shippedDefaults.textSize, 'string', 'shipped defaults carry no textSize');
assert.match(shippingSurface, /data-spectr-settings-tabs/, 'modulation settings tab surface missing');
assert.match(shippingSurface, /data-spectr-settings-tab[\s\S]*general/, 'General settings tab missing');
assert.match(shippingSurface, /data-spectr-settings-tab[\s\S]*modulation/, 'Modulation settings tab missing');
assert.match(shippingSurface, /data-spectr-settings-body/, 'dedicated Settings body scroll surface missing');
assert.doesNotMatch(shippingSurface, /position: "sticky"/, 'Settings relies on unsupported CSS sticky positioning');
assert.match(shippingSurface, /data-spectr-settings-header[\s\S]*flexShrink: 0/, 'Settings header is not a fixed flex sibling');
assert.match(shippingSurface, /data-spectr-settings-tabs[\s\S]*flexShrink: 0/, 'Settings tabs are not fixed with the header');
assert.match(shippingSurface, /label: "LFO 2"/, 'second internal LFO controls missing');
assert.match(shippingSurface, /data-spectr-modulation-select.*all/, 'modulation select-all control missing');
assert.match(shippingSurface, /data-spectr-modulation-select.*none/, 'modulation select-none control missing');
for (const target of ['bank', 'snapshot-a', 'snapshot-b', 'morph']) {
  assert.match(modulationSurface, new RegExp(`['"]${target}['"]`),
    `individual modulation target ${target} missing`);
}
assert.match(modulationSurface, /targetMask/, 'modulation target mask state is not preserved');
assert.match(shippingSurface, /modulation_targets_set/, 'modulation target selection is not bridge-backed');
assert.match(shippingSurface, /lfo2Enabled|lfo2_enabled/, 'second LFO state is not represented in the bridge surface');
const shortDwell = shippingSurface.replace(
  'const holdMs = /\\b(?:MUTED|UNMUTED)\\b/.test(display) ? 2800 : 2200;',
  'const holdMs = /\\b(?:MUTED|UNMUTED)\\b/.test(display) ? 280 : 220;');
assert.notEqual(shortDwell, shippingSurface, 'status dwell mutation did not plant');

// Negative controls for the driven text-size scenario. The first severs the
// control's handler, so a wiring regression that a static read of the emitted
// source cannot see turns this red. The second leaves the wiring intact and
// only corrupts the Large scale, so a value regression is caught separately
// from a wiring one.
const deadTextSize = shippingSurface.replace(
  'onChange: (v) => persist({ textSize: v })', 'onChange: () => {}');
assert.notEqual(deadTextSize, shippingSurface,
  'text size handler needle did not match; its negative control would be blind');
const flatLargeScale = shippingSurface.replace(
  'large: 1.45 };', 'large: 1 };');
assert.notEqual(flatLargeScale, shippingSurface,
  'text scale table needle did not match; its negative control would be blind');

const oracle = (componentSource, mode, settingsJson) => `<script>
window.__spectrPolishStart = () => {
  if (window.__spectrPolishStarted) return;
  if (!window.React || !window.ReactDOM) {
    setTimeout(window.__spectrPolishStart, 20);
    return;
  }
  window.__spectrPolishStarted = true;
  const useStateChrome = React.useState;
  const useRefChrome = React.useRef;
  const useEffectChrome = React.useEffect;
  ${componentSource}
  const waitFor = async (predicate, label, limit = 250) => {
    for (let attempt = 0; attempt < limit; ++attempt) {
      const value = predicate();
      if (value) return value;
      await new Promise(resolve => setTimeout(resolve, 20));
    }
    throw new Error('timed out waiting for ' + label);
  };
  const result = document.createElement('pre');
  result.id = '__spectr_polish_oracle';
  document.body.appendChild(result);
  const mount = document.createElement('div');
  mount.id = '__spectr_polish_mount';
  document.body.appendChild(mount);
  const assertFinalSurface = () => {
    const unpackedRoot = document.querySelector('#root');
    if (!unpackedRoot || unpackedRoot.children.length === 0)
      throw new Error('shipping unpacked root did not mount');
    if (document.getElementById('__bundler_err'))
      throw new Error('shipping bundle emitted __bundler_err');
  };
  const centered = button => {
    const label = button.querySelector('[aria-live="polite"]');
    if (!label) throw new Error('copy feedback label missing');
    const buttonStyle = getComputedStyle(button);
    const labelStyle = getComputedStyle(label);
    if (buttonStyle.display !== 'flex' || buttonStyle.alignItems !== 'center'
        || buttonStyle.justifyContent !== 'center'
        || labelStyle.display !== 'flex' || labelStyle.alignItems !== 'center'
        || labelStyle.justifyContent !== 'center')
      throw new Error('copy feedback lost flex centering: button='
        + [buttonStyle.display, buttonStyle.alignItems, buttonStyle.justifyContent].join('/')
        + ' label='
        + [labelStyle.display, labelStyle.alignItems, labelStyle.justifyContent].join('/'));
    const outer = button.getBoundingClientRect();
    const inner = label.getBoundingClientRect();
    if (Math.abs((outer.left + outer.right - inner.left - inner.right) / 2) > 0.75
        || Math.abs((outer.top + outer.bottom - inner.top - inner.bottom) / 2) > 0.75)
      throw new Error('copy feedback was not geometrically centered');
  };
  (async () => {
    try {
      if (${JSON.stringify(mode)} === 'status') {
        // Chrome's --virtual-time-budget deliberately advances wall-clock
        // timers out of phase with DOM dumping. Inspect the mounted component's
        // actual scheduling requests instead: this proves the authored normal
        // and mute dwell values without treating virtual time as elapsed time.
        const nativeSetTimeout = window.setTimeout;
        const scheduledDelays = [];
        window.setTimeout = (callback, delay, ...args) => {
          if (delay >= 200) {
            scheduledDelays.push(Number(delay));
            return nativeSetTimeout(() => {}, 60000);
          }
          return nativeSetTimeout(callback, delay, ...args);
        };
        const statusRoot = ReactDOM.createRoot(mount);
        statusRoot.render(React.createElement(
          StatusBanner, { message: 'EDIT → SCULPT|1', disabled: false }));
        await waitFor(() =>
          document.querySelector('#__spectr_polish_mount [data-spectr-status-banner]'),
        'status banner');
        await waitFor(() => scheduledDelays.length === 1, 'normal status timer');
        statusRoot.render(React.createElement(
          StatusBanner, { message: 'BAND 1/32 MUTED|2', disabled: false }));
        await waitFor(() => scheduledDelays.length === 2, 'mute status timer');
        window.setTimeout = nativeSetTimeout;
        if (scheduledDelays[0] !== 2200 || scheduledDelays[1] !== 2800)
          throw new Error('status dwell schedule mismatch: '
            + scheduledDelays.join(','));
        assertFinalSurface();
        result.textContent = 'SPECTR_STATUS_DWELL_OK';
        return;
      }

      if (${JSON.stringify(mode)} === 'textsize') {
        const style = document.createElement('style');
        style.textContent = 'html,body{width:100%!important;height:100%!important;'
          + 'position:fixed!important;inset:0!important;overflow:hidden!important}'
          + '#root{display:none!important}#__spectr_polish_mount{position:fixed;inset:0}';
        document.head.appendChild(style);
        const Harness = () => {
          const [settings, setSettings] = React.useState(${settingsJson});
          return React.createElement(SettingsModal, {
            settings, setSettings, onClose() {},
          });
        };
        // Anything the shipping bundle already published is not this scenario's
        // evidence. Clear immediately before render so the first entry is the
        // mount-time application of the default this harness was handed.
        window.__spectrTextScaleCalls.length = 0;
        ReactDOM.createRoot(mount).render(React.createElement(Harness));
        const panel = await waitFor(() =>
          document.querySelector('#__spectr_polish_mount [data-spectr-settings-panel]'),
        'Settings panel');
        const group = await waitFor(() =>
          panel.querySelector('[data-spectr-settings-group="typography"]'),
        'Text size group');
        const keys = ['small', 'medium', 'large'];
        const chip = key => group.querySelector(
          '[data-spectr-setting-option="' + key + '"]');
        for (const key of keys)
          if (!chip(key)) throw new Error('Text size option missing: ' + key);

        // REACHABILITY. Mounted is not reachable: an ancestor display:none
        // renders every static source assertion vacuous.
        for (let node = chip('large'); node && node !== document.body;
             node = node.parentElement)
          if (getComputedStyle(node).display === 'none')
            throw new Error('Text size control is hidden by an ancestor '
              + 'display:none: ' + node.tagName);
        for (const key of keys) {
          const box = chip(key).getBoundingClientRect();
          if (box.width <= 0 || box.height <= 0)
            throw new Error('Text size option ' + key + ' has no rendered box');
        }

        // CLIPPING. A new group is the case that goes wrong: the row can spill
        // past the scrolling body, or land on top of the group after it.
        const body = panel.querySelector('[data-spectr-settings-body]');
        if (!body) throw new Error('Settings body scroll owner missing');
        const bodyRect = body.getBoundingClientRect();
        for (const key of keys) {
          const box = chip(key).getBoundingClientRect();
          if (box.left < bodyRect.left - 0.5 || box.right > bodyRect.right + 0.5)
            throw new Error('Text size option ' + key
              + ' spilled outside the settings body');
        }
        const feedback = panel.querySelector('[data-spectr-settings-group="feedback"]');
        if (!feedback) throw new Error('FEEDBACK group missing');
        const own = group.getBoundingClientRect();
        const next = feedback.getBoundingClientRect();
        if (own.right > next.left && next.right > own.left
            && own.bottom > next.top && next.bottom > own.top)
          throw new Error('Text size group overlaps the FEEDBACK group');

        // WIRING + VALUE. The stub below stands in for the bridge knob, so
        // this proves the call site fires with the right scale -- it does not
        // and cannot prove a native relayout, which needs the SDK that carries
        // the knob.
        const pressed = () => keys.filter(key =>
          chip(key).getAttribute('aria-pressed') === 'true');
        const calls = window.__spectrTextScaleCalls;
        await waitFor(() => calls.length >= 1, 'mount-time text scale write');
        if (calls[0] !== 1.2)
          throw new Error('default applied scale ' + calls[0] + ' want 1.2');
        if (pressed().join(',') !== 'medium')
          throw new Error('default selection is [' + pressed() + '] want [medium]');

        const select = async (key, want) => {
          const before = calls.length;
          chip(key).click();
          await waitFor(() => calls.length > before, key + ' text scale write');
          const got = calls[calls.length - 1];
          if (got !== want)
            throw new Error(key + ' applied scale ' + got + ' want ' + want);
          if (pressed().join(',') !== key)
            throw new Error(key + ' left the selection at [' + pressed() + ']');
        };
        await select('small', 1);
        await select('large', 1.45);
        await select('medium', 1.2);

        await waitFor(() => {
          const shipped = document.querySelector('#root');
          return shipped && shipped.children.length > 0;
        }, 'shipping root mount');
        assertFinalSurface();
        result.textContent = 'SPECTR_TEXT_SIZE_OK';
        return;
      }

      if (${JSON.stringify(mode)} === 'modulation') {
        const style = document.createElement('style');
        style.textContent = 'html,body{width:100%!important;height:100%!important;'
          + 'position:fixed!important;inset:0!important;overflow:hidden!important}'
          + '#root{display:none!important}#__spectr_polish_mount{position:fixed;inset:0}';
        document.head.appendChild(style);
        const Harness = () => {
          const [settings, setSettings] = React.useState({
            theme: 'spectral', metaphor: 'columns', bloom: 1,
            spectrumIntensity: 1, bandCount: 32, muteStyle: 'cutout',
            showMinimap: true, showRulers: true, motionMode: 'live',
            statusInfo: true, showBuildInfo: true,
          });
          return React.createElement(SettingsModal, {
            settings, setSettings, onClose() {},
          });
        };
        ReactDOM.createRoot(mount).render(React.createElement(Harness));
        const panel = await waitFor(() =>
          document.querySelector('#__spectr_polish_mount [data-spectr-settings-panel]'),
        'Settings panel');
        const names = ['bank', 'snapshot-a', 'snapshot-b', 'morph'];
        const buttonFor = key => panel.querySelector(
          '[data-spectr-modulation-target="' + key + '"]');
        const bank = await waitFor(() => buttonFor('bank'), 'bank target control');

        // REACHABILITY. Mounted is not reachable: an ancestor display:none
        // renders every static source assertion vacuous.
        for (let node = bank; node && node !== document.body; node = node.parentElement) {
          if (getComputedStyle(node).display === 'none')
            throw new Error('modulation controls are hidden by an ancestor '
              + 'display:none: ' + (node.getAttribute('data-spectr-settings-tabs')
                ? 'data-spectr-settings-tabs' : node.tagName));
        }
        const box = bank.getBoundingClientRect();
        if (box.width <= 0 || box.height <= 0)
          throw new Error('modulation target control has no rendered box');

        // WIRING. Assert the delta, not an absolute mask: the mount effect
        // defaults the mask to all-selected when the host reports no targets.
        const pressed = () => names.filter(key =>
          buttonFor(key).getAttribute('aria-pressed') === 'true');
        const before = pressed();
        // The bridge carries unrelated traffic (editor_ready and friends), so
        // filter to the message this control owns. Waiting on raw call count
        // lets an unrelated message satisfy the wait and mask a dead handler.
        const modCalls = () => window.__spectrBridgeCalls.filter(
          entry => entry.type === 'modulation_targets_set');
        window.__spectrBridgeCalls.length = 0;
        bank.click();
        await waitFor(() => modCalls().length >= 1, 'bank click bridge write');
        const call = modCalls()[0];
        const expected = before.includes('bank')
          ? before.filter(key => key !== 'bank')
          : before.concat(['bank']);
        const sortedJoin = list => list.slice().sort().join(',');
        if (sortedJoin(call.payload.targets) !== sortedJoin(expected))
          throw new Error('bank click sent [' + call.payload.targets
            + '] want [' + expected + ']');
        if (sortedJoin(pressed()) !== sortedJoin(expected))
          throw new Error('bank click did not restyle its own control');

        const none = panel.querySelector('[data-spectr-modulation-select="none"]');
        if (!none) throw new Error('NONE control missing');
        none.click();
        await waitFor(() => modCalls().length >= 2, 'NONE bridge write');
        if (modCalls()[1].payload.targets.length !== 0)
          throw new Error('NONE left targets selected');
        const all = panel.querySelector('[data-spectr-modulation-select="all"]');
        if (!all) throw new Error('ALL control missing');
        all.click();
        await waitFor(() => modCalls().length >= 3, 'ALL bridge write');
        if (sortedJoin(modCalls()[2].payload.targets) !== sortedJoin(names))
          throw new Error('ALL selected [' + modCalls()[2].payload.targets + ']');
        // This mode finishes far faster than the copy-feedback scenarios, so
        // the shipping bundle may still be mounting. Wait for it rather than
        // reading a not-yet-mounted root as a failure.
        await waitFor(() => {
          const shipped = document.querySelector('#root');
          return shipped && shipped.children.length > 0;
        }, 'shipping root mount');
        assertFinalSurface();
        result.textContent = 'SPECTR_MODULATION_OK';
        return;
      }

      const style = document.createElement('style');
      style.textContent = 'html,body{width:100%!important;height:100%!important;'
        + 'position:fixed!important;inset:0!important;transform:none!important;overflow:hidden!important}'
        + '#root{display:none!important}#__spectr_polish_mount{position:fixed;inset:0}';
      document.head.appendChild(style);
      const defaults = {
        theme: 'spectral', metaphor: 'columns', bloom: 1,
        spectrumIntensity: 1, bandCount: 32, muteStyle: 'cutout',
        showMinimap: true, showRulers: true, motionMode: 'live',
        statusInfo: true, showBuildInfo: true,
      };
      const Harness = () => {
        const [settings, setSettings] = React.useState(defaults);
        return React.createElement(SettingsModal, {
          settings, setSettings, onClose() {},
        });
      };
      ReactDOM.createRoot(mount).render(React.createElement(Harness));
      const panel = await waitFor(() =>
        document.querySelector('#__spectr_polish_mount [data-spectr-settings-panel]'),
      'Settings panel');
      const body = panel.querySelector('[data-spectr-settings-body]');
      if (!body) throw new Error('Settings body scroll owner missing');
      const shouldOverflow = innerHeight < 1200;
      const actuallyOverflows = body.scrollHeight > body.clientHeight + 1;
      if (actuallyOverflows !== shouldOverflow)
        throw new Error('Settings overflow mismatch: height=' + innerHeight
          + ' scroll=' + body.scrollHeight + ' client=' + body.clientHeight);
      body.scrollTop = 100000;
      await new Promise(resolve => requestAnimationFrame(resolve));
      if (shouldOverflow ? body.scrollTop <= 0 : body.scrollTop !== 0)
        throw new Error('Settings scroll range disagreed with content fit');

      const hint = Array.from(panel.querySelectorAll('div')).find(node =>
        node.children.length === 0
          && node.textContent === 'Hover, mute, and drag feedback');
      if (!hint) throw new Error('complete Status info hint missing');
      const hintRect = hint.getBoundingClientRect();
      const ownerRect = hint.parentElement.getBoundingClientRect();
      if (hint.scrollWidth > hint.clientWidth + 1
          || hintRect.left < ownerRect.left - 0.5
          || hintRect.right > ownerRect.right + 0.5
          || hintRect.top < ownerRect.top - 0.5
          || hintRect.bottom > ownerRect.bottom + 0.5)
        throw new Error('Status info hint was clipped or truncated');

      const button = await waitFor(() =>
        panel.querySelector('[data-spectr-copy-build-info]'), 'Copy button');
      if (button.textContent.trim() !== 'COPY')
        throw new Error('Copy button did not begin at COPY');
      centered(button);
      button.click();
      await waitFor(() => button.textContent.trim() === 'COPYING', 'COPYING feedback');
      if (button.dataset.spectrCopyState !== 'copying')
        throw new Error('COPYING state marker missing');
      centered(button);
      await waitFor(() => button.textContent.trim() === 'COPIED', 'COPIED feedback');
      if (button.dataset.spectrCopyState !== 'copied')
        throw new Error('COPIED state marker missing');
      centered(button);
      await new Promise(resolve => setTimeout(resolve, 1200));
      if (button.textContent.trim() !== 'COPIED')
        throw new Error('COPIED feedback did not persist');
      centered(button);
      await waitFor(() => button.textContent.trim() === 'COPY', 'Copy feedback reset');
      centered(button);
      assertFinalSurface();
      result.textContent = 'SPECTR_SETTINGS_POLISH_OK';
    } catch (error) {
      result.textContent = 'SPECTR_POLISH_ORACLE_ERROR: ' + error.message;
    }
  })();
};
setTimeout(window.__spectrPolishStart, 0);
</script>`;

const run = ({ componentSource, mode, width, height, settings }) => {
  let html = fs.readFileSync(browserHtmlPath, 'utf8');
  const mock = `<script>
window.spectrPublishMode = () => {};
window.__spectrBridgeCalls = [];
// Stand in for WidgetBridge::set_imported_text_scale, which no released SDK
// exposes yet. Recording it here is what lets the driven scenario below read
// the scale Spectr asks the native render for.
window.__spectrTextScaleCalls = [];
globalThis.setImportedTextScale = (scale) => {
  window.__spectrTextScaleCalls.push(scale);
};
window.pulp = {
  on() { return () => {}; },
  postMessage(type, payload) {
    window.__spectrBridgeCalls.push({ type, payload });
    if (type === 'build_info_get') return Promise.resolve({ ok: true, payload: {
      ok: true, product_version: '1.0.0', product_sha: '0123456789abcdef',
      product_provenance_known: true, product_dirty: false,
      sdk_version: '0.829.0', sdk_sha: 'fedcba9876543210',
      sdk_provenance_exact: true, sdk_dirty: false,
      build_type: 'Release', build_time: '2026-09-02T12:00:00Z',
    } });
    if (type === 'build_info_copy') return new Promise(resolve => setTimeout(
      () => resolve({ ok: true, payload: { ok: true } }), 450));
    return Promise.resolve({ ok: true, payload: { ok: true } });
  },
};
</script>`;
  html = html.replace('<script>', mock + '<script>');
  html = html.replace('</body>', oracle(componentSource, mode,
    JSON.stringify(settings || {})) + '</body>');
  html = html.replace('      window.Babel.transformScriptTags();',
    '      window.Babel.transformScriptTags();\n      window.__spectrPolishStart();');
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'spectr-polish-'));
  const htmlPath = path.join(directory, 'oracle.html');
  fs.writeFileSync(htmlPath, html);
  try {
    return spawnSync(chromePath, [
      '--headless=new', '--disable-gpu', '--disable-web-security',
      '--disable-background-networking', '--disable-component-update',
      '--disable-domain-reliability', '--disable-sync', '--incognito',
      '--allow-file-access-from-files', '--no-first-run',
      '--no-default-browser-check', `--window-size=${width},${height}`,
      '--run-all-compositor-stages-before-draw', '--virtual-time-budget=15000',
      '--dump-dom', `file://${htmlPath}`,
    ], { encoding: 'utf8', timeout: 30000, maxBuffer: 64 * 1024 * 1024 });
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
};

const decodeHtmlText = text => text.replace(
  /&(?:#(\d+)|#x([0-9a-f]+)|amp|lt|gt|quot|#39);/gi,
  (entity, decimal, hexadecimal) => {
    if (decimal) return String.fromCodePoint(Number(decimal));
    if (hexadecimal) return String.fromCodePoint(Number.parseInt(hexadecimal, 16));
    return { '&amp;': '&', '&lt;': '<', '&gt;': '>', '&quot;': '"', '&#39;': "'" }[
      entity.toLowerCase()];
  });
const oracleText = run => {
  const match = run.stdout.match(
    /<pre id="__spectr_polish_oracle">([^<]*)<\/pre>/);
  assert(match, 'rendered polish oracle result missing\n' + run.stdout.slice(-3000));
  return decodeHtmlText(match[1]);
};
assert.equal(oracleText({ stdout:
  '<script>SPECTR_STATUS_DWELL_OK</script>'
  + '<pre id="__spectr_polish_oracle">WRONG_RESULT</pre>' }), 'WRONG_RESULT',
'oracle parser must ignore sentinel text embedded in the injected script');

const productionStatus = run({
  componentSource: shippingSurface, mode: 'status', width: 1320, height: 860,
});
assert.equal(productionStatus.error, undefined,
  productionStatus.error && productionStatus.error.message);
assert.equal(productionStatus.status, 0, productionStatus.stderr.slice(-2000));
assert.equal(oracleText(productionStatus), 'SPECTR_STATUS_DWELL_OK');

const negativeStatus = run({
  componentSource: shortDwell, mode: 'status', width: 1320, height: 860,
});
assert.equal(negativeStatus.error, undefined,
  negativeStatus.error && negativeStatus.error.message);
assert.equal(negativeStatus.status, 0, negativeStatus.stderr.slice(-2000));
assert.equal(oracleText(negativeStatus),
  'SPECTR_POLISH_ORACLE_ERROR: status dwell schedule mismatch: 220,280');

for (const [label, height] of [['overflowing', 860], ['fitting', 1800]]) {
  const settings = run({
    componentSource: shippingSurface, mode: 'settings', width: 1320, height,
  });
  assert.equal(settings.error, undefined, settings.error && settings.error.message);
  assert.equal(settings.status, 0, settings.stderr.slice(-2000));
  assert.equal(oracleText(settings), 'SPECTR_SETTINGS_POLISH_OK', label);
}

const modulation = run({
  componentSource: shippingSurface, mode: 'modulation', width: 1320, height: 860,
});
assert.equal(modulation.error, undefined, modulation.error && modulation.error.message);
assert.equal(modulation.status, 0, modulation.stderr.slice(-2000));
assert.equal(oracleText(modulation), 'SPECTR_MODULATION_OK');

// Negative control for the driven check above. The static regex assertions at
// the top of this file cannot tell a wired control from a dead one -- they read
// emitted source text. This severs the target button's handler and requires the
// driven oracle to notice, so a future weakening turns this green->red.
const deadTargets = shippingSurface.replace(
  'onClick: () => publishTargetMask((value.targetMask || 0) ^ bit)',
  'onClick: () => {}');
assert.notEqual(deadTargets, shippingSurface,
  'modulation target handler needle did not match; the negative control would be blind');
const deadModulation = run({
  componentSource: deadTargets, mode: 'modulation', width: 1320, height: 860,
});
assert.equal(deadModulation.status, 0, deadModulation.stderr.slice(-2000));
assert.equal(oracleText(deadModulation),
  'SPECTR_POLISH_ORACLE_ERROR: timed out waiting for bank click bridge write');

// TEXT SIZE. Driven, not read: the panel is mounted with the SHIPPED defaults,
// each option is clicked, and the scale that reaches the bridge knob is the
// assertion. A static read of the emitted source cannot tell a live control
// from a dead one -- MOD-1 was exactly that failure.
const textSize = run({
  componentSource: shippingSurface, mode: 'textsize',
  width: 1320, height: 860, settings: shippedDefaults,
});
assert.equal(textSize.error, undefined, textSize.error && textSize.error.message);
assert.equal(textSize.status, 0, textSize.stderr.slice(-2000));
assert.equal(oracleText(textSize), 'SPECTR_TEXT_SIZE_OK');

// Negative control 1 -- wiring. Sever the handler and the driven scenario must
// stop seeing a scale write; every static assertion still passes.
const deadTextSizeRun = run({
  componentSource: deadTextSize, mode: 'textsize',
  width: 1320, height: 860, settings: shippedDefaults,
});
assert.equal(deadTextSizeRun.status, 0, deadTextSizeRun.stderr.slice(-2000));
assert.equal(oracleText(deadTextSizeRun),
  'SPECTR_POLISH_ORACLE_ERROR: timed out waiting for small text scale write');

// Negative control 2 -- value. Leave the wiring intact and flatten Large back
// to 1.0, the scale the pre-1.0 build shipped at.
const flatLargeRun = run({
  componentSource: flatLargeScale, mode: 'textsize',
  width: 1320, height: 860, settings: shippedDefaults,
});
assert.equal(flatLargeRun.status, 0, flatLargeRun.stderr.slice(-2000));
assert.equal(oracleText(flatLargeRun),
  'SPECTR_POLISH_ORACLE_ERROR: large applied scale 1 want 1.45');

// Negative control 3 -- the default. Hand the same shipping surface the old
// Small default; Medium is the release default and the oracle must say so.
const smallDefaultRun = run({
  componentSource: shippingSurface, mode: 'textsize',
  width: 1320, height: 860,
  settings: { ...shippedDefaults, textSize: 'small' },
});
assert.equal(smallDefaultRun.status, 0, smallDefaultRun.stderr.slice(-2000));
assert.equal(oracleText(smallDefaultRun),
  'SPECTR_POLISH_ORACLE_ERROR: default applied scale 1 want 1.2');

console.log('Spectr UX polish: dwell, modulation, and three text-size negative controls red; copy, hint, overflow, driven modulation, and driven text-size scenarios green');
