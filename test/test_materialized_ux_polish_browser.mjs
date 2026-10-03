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
// The per-LFO target list: one shared list, a switch and a Depth lane per
// target. The both-LFO ALL/NONE Destinations chips are gone.
assert.doesNotMatch(modulationSurface, /data-spectr-modulation-select/,
  'the old both-LFO Destinations ALL/NONE chips are still in Settings');
assert.match(modulationSurface, /spectrModulationRouteList\(\)/,
  'Settings does not list the shared target list');
for (const target of ['bank', 'band-shift', 'band-spread', 'intensity', 'mix', 'morph',
                      'freeze', 'length', 'output']) {
  assert.match(shippingSurface, new RegExp(`['"]${target}['"]`),
    `individual modulation target ${target} missing`);
}
// On/off is lane(t) (the level targets in their own block from 4060), and the
// Depth lane is on/off + 10.
assert.match(modulationSurface, /lane\(t\) \+ 10/, 'target Depth lanes are not written');
assert.match(shippingSurface, /lfo2Enabled|lfo2_enabled/, 'second LFO state is not represented in the bridge surface');
const shortDwell = shippingSurface.replace(
  'const holdMs = /\\b(?:MUTED|UNMUTED)\\b/.test(display) ? 2800 : 2200;',
  'const holdMs = /\\b(?:MUTED|UNMUTED)\\b/.test(display) ? 280 : 220;');
assert.notEqual(shortDwell, shippingSurface, 'status dwell mutation did not plant');

// TEXT SIZE, as Spectr actually ships it. The shipping surface carries the
// scale table and the one native call site, and deliberately exposes NO size
// chooser.
//
// WHY THE CHOOSER IS ABSENT -- this is a PIN constraint, not a product
// decision, and the assertion below should not be read as one. The chooser
// drives WidgetBridge::set_imported_text_scale, which is not in the SDK Spectr
// currently pins (v0.834.0). Shipping the chooser against that SDK ships a
// control that moves nothing, so the materialization recipe strips it:
// tools/patch_materialized_editor.py, add_text_size_setting -- which also
// hard-exits if an unrecognised TYPOGRAPHY group survives. The browser lane
// still authors the group in JSX (resources/editor.html), so the two lanes
// disagree BY DESIGN and this suite asserts the native lane.
//
// TO RESTORE IT: bump the SDK pin to a release that carries
// WidgetBridge::set_imported_text_scale, confirm the knob relayouts a native
// host, then delete add_text_size_setting's strip step and flip the three
// assertions below (chooser present, options reachable, each option's click
// reaching the call site at its table scale). The scale table, spectrTextSize,
// and the spectrApplyTextScale call site are all still wired at the medium
// default precisely so that restoring the group is the only edit needed.
//
// Assert both halves, so restoring an INERT chooser against the current pin
// and dropping the live call site each turn this red.
assert.match(shippingSurface,
  /const SPECTR_TEXT_SCALES = \{ small: 1, medium: 1\.2, large: 1\.45 \};/,
  'text scale table missing from the shipping surface');
assert.match(shippingSurface,
  /React\.useEffect\(\(\) => \{ spectrApplyTextScale\(settings\.textSize\); \}/,
  'mount-time text scale call site missing from the shipping surface');
assert.doesNotMatch(shippingSurface, /marker: "typography"/,
  'shipping Settings carries a text-size chooser the pinned SDK cannot honour');
// Positive control for the absence above, on the same instrument: a surface
// this slice failed to capture would satisfy doesNotMatch vacuously.
assert.match(shippingSurface, /marker: "feedback"/,
  'settings-group needle broke; the TYPOGRAPHY absence above would be vacuous');

// Controls for the driven text-size scenario below. The first severs the one
// effect that carries the setting to the native knob, so a wiring regression
// no static read of the emitted source can see turns this red. The second
// leaves the wiring intact and only flattens the MEDIUM scale every host
// actually renders at, so a value regression is caught separately.
const deadTextSize = shippingSurface.replace(
  'React.useEffect(() => { spectrApplyTextScale(settings.textSize); }, [settings.textSize]);',
  'React.useEffect(() => {}, [settings.textSize]);');
assert.notEqual(deadTextSize, shippingSurface,
  'text scale effect needle did not match; its control would be blind');
const flatMediumScale = shippingSurface.replace(
  'medium: 1.2,', 'medium: 1,');
assert.notEqual(flatMediumScale, shippingSurface,
  'text scale table needle did not match; its control would be blind');

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
  // The shipping bundle mounts on its own schedule, independently of whatever
  // a scenario drives in its own mount point. A scenario that finishes quickly
  // can reach here first, so wait for that root rather than reading a
  // not-yet-mounted one as a failure. The gate is unchanged: a root that never
  // mounts, or a bundle that errors, still fails.
  const assertFinalSurface = async () => {
    await waitFor(() => {
      const unpackedRoot = document.querySelector('#root');
      return unpackedRoot && unpackedRoot.children.length > 0;
    }, 'shipping unpacked root to mount');
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
        await assertFinalSurface();
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
        // The shipping bundle owns its own copy of the scale module and writes
        // through the same stub. Let it finish mounting FIRST, then take a
        // baseline, so what this scenario reads is unambiguously the harness's
        // own mount-time write and not a leftover from #root.
        await waitFor(() => {
          const shipped = document.querySelector('#root');
          return shipped && shipped.children.length > 0;
        }, 'shipping root mount');
        const calls = window.__spectrTextScaleCalls;
        const baseline = calls.length;
        ReactDOM.createRoot(mount).render(React.createElement(Harness));
        const panel = await waitFor(() =>
          document.querySelector('#__spectr_polish_mount [data-spectr-settings-panel]'),
        'Settings panel');

        // POSITIVE CONTROL for the absence checks below, in the same
        // instrument. A panel that rendered no groups at all -- or rendered
        // them under an ancestor display:none -- would satisfy every "no
        // chooser" check vacuously. Prove this query can SEE a shipped group,
        // reachable and with a real box, before trusting it not to find one.
        const feedback = await waitFor(() =>
          panel.querySelector('[data-spectr-settings-group="feedback"]'),
        'FEEDBACK group');
        for (let node = feedback; node && node !== document.body;
             node = node.parentElement)
          if (getComputedStyle(node).display === 'none')
            throw new Error('FEEDBACK group is hidden by an ancestor '
              + 'display:none: ' + node.tagName);
        const feedbackBox = feedback.getBoundingClientRect();
        if (feedbackBox.width <= 0 || feedbackBox.height <= 0)
          throw new Error('FEEDBACK group has no rendered box');

        // ABSENCE. Spectr renders every host at one scale, so shipping
        // Settings offers no size chooser. No chip group in this surface uses
        // these option keys, so the query is specific to the text-size row.
        if (panel.querySelector('[data-spectr-settings-group="typography"]'))
          throw new Error('Settings rendered a TYPOGRAPHY group');
        for (const key of ['small', 'medium', 'large'])
          if (panel.querySelector('[data-spectr-setting-option="' + key + '"]'))
            throw new Error('Settings rendered a text size option: ' + key);

        // WIRING + VALUE. The stub stands in for the bridge knob, so this
        // proves the mount-time call site fires with the scale the settings it
        // was handed name -- it does not and cannot prove a native relayout,
        // which needs the SDK that carries the knob.
        await waitFor(() => calls.length > baseline, 'mount-time text scale write');
        const written = calls.slice(baseline);
        if (written.some(value => value !== written[0]))
          throw new Error('text scale writes disagreed: ' + written.join(','));
        await assertFinalSurface();
        result.textContent = 'SPECTR_TEXT_SIZE_OK:' + written[0];
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
        // Settings > MODULATION lists the band menu's per-LFO targets: a
        // switch row and a Depth row per target, for the LFO the "LFO
        // targets" chips select. Every row is mounted at mount (the native
        // bridge appends a late-mounted widget), and the list is shown
        // whether or not an LFO is on, so targets can be set up first.
        const order = ['bank', 'band-shift', 'band-spread', 'intensity', 'mix',
                       'morph', 'freeze', 'length', 'bands', 'preset', 'output', 'a', 'b'];
        const rowFor = key => panel.querySelector(
          '[data-spectr-settings-target="' + key + '"]');
        const shown = el => {
          if (!el) return null;
          for (let node = el; node && node !== document.body; node = node.parentElement)
            if (getComputedStyle(node).display === 'none') return null;
          const box = el.getBoundingClientRect();
          return box.width > 0 && box.height > 0 ? el : null;
        };
        const got = Array.from(panel.querySelectorAll('[data-spectr-settings-target]'))
          .map(el => el.getAttribute('data-spectr-settings-target'));
        if (got.join(',') !== order.join(','))
          throw new Error('Settings target order [' + got + '] want [' + order + ']');
        if (!shown(rowFor('bank')))
          throw new Error('the target list is not visible with both LFOs off');
        if (panel.querySelector('[data-spectr-modulation-target]')
            || panel.querySelector('[data-spectr-modulation-select]'))
          throw new Error('the old both-LFO Destinations chips are still mounted');
        // A Depth row whose target is off is hidden and inert.
        const bankDepth = panel.querySelector('[data-spectr-settings-target-depth="bank"]');
        if (!bankDepth || bankDepth.getAttribute('data-spectr-settings-target-depth-state') !== 'off')
          throw new Error('the Bank Depth row is not inert while Bank is off');

        // WIRING: a switch writes its own lane as a recordable edit.
        const edits = () => window.__spectrBridgeCalls.filter(
          entry => entry.type === 'param_edit');
        window.__spectrBridgeCalls.length = 0;
        rowFor('bank').querySelector('[data-spectr-setting-toggle]').click();
        await waitFor(() => edits().length >= 1, 'bank switch bridge write');
        if (edits()[0].payload.id !== 4020 || edits()[0].payload.value !== 1)
          throw new Error('bank switch sent ' + JSON.stringify(edits()[0].payload)
            + ', want {id: 4020, value: 1}');
        // LFO 2's rows write LFO 2's lanes.
        const chips = panel.querySelector('[data-spectr-settings-targets-lfo]');
        if (!chips) throw new Error('LFO targets chips missing');
        chips.querySelector('[data-spectr-setting-option="2"]').click();
        await waitFor(() => panel.querySelector(
          '[data-spectr-settings-targets-lfo="2"]'), 'LFO 2 targets selected');
        rowFor('freeze').querySelector('[data-spectr-setting-toggle]').click();
        await waitFor(() => edits().length >= 2, 'LFO 2 freeze switch bridge write');
        if (edits()[1].payload.id !== 4046)
          throw new Error('LFO 2 Freeze switch wrote lane ' + edits()[1].payload.id
            + ', want 4046');
        // A level target lives in its own block: LFO 2 Output is 4082.
        rowFor('output').querySelector('[data-spectr-setting-toggle]').click();
        await waitFor(() => edits().length >= 3, 'LFO 2 output switch bridge write');
        if (edits()[2].payload.id !== 4082)
          throw new Error('LFO 2 Output switch wrote lane ' + edits()[2].payload.id
            + ', want 4082');
        await assertFinalSurface();
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
        statusInfo: true, showBuildInfo: true, showGpuStats: true,
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
      // With the per-LFO target list (a switch and a Depth row per target) the
      // body is taller than the panel's height cap at any window height, so
      // it scrolls at every size; the check is that the scroll range is real.
      const shouldOverflow = true;
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
      const gpuStatus = panel.querySelector('[data-spectr-gpu-audio-status]');
      if (window.__spectrGpuAudioAvailable) {
        if (!gpuStatus || !gpuStatus.textContent.includes('GPU BLOCKS')
            || !gpuStatus.textContent.includes('124'))
          throw new Error('experimental GPU audio status was not visible');
      } else if (gpuStatus) {
        throw new Error('GPU audio status shown although the build reports it unavailable');
      }
      const gpuStatsToggle = panel.querySelector('[data-spectr-gpu-audio-stats-toggle]');
      if (!gpuStatsToggle || gpuStatsToggle.getAttribute('aria-checked') !== 'true')
        throw new Error('GPU stats visibility setting was not enabled');
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
      await assertFinalSurface();
      result.textContent = 'SPECTR_SETTINGS_POLISH_OK';
    } catch (error) {
      result.textContent = 'SPECTR_POLISH_ORACLE_ERROR: ' + error.message;
    }
  })();
};
setTimeout(window.__spectrPolishStart, 0);
</script>`;

const run = ({ componentSource, mode, width, height, settings, gpuAudioAvailable = true }) => {
  let html = fs.readFileSync(browserHtmlPath, 'utf8');
  const mock = `<script>
window.spectrPublishMode = () => {};
window.__spectrBridgeCalls = [];
window.__spectrGpuAudioAvailable = ${gpuAudioAvailable ? 'true' : 'false'};
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
      gpu_audio: { available: window.__spectrGpuAudioAvailable, provider_state: 'shared_ready',
        gpu_selected: '124', cpu_fallback: '0', cancelled: '0',
        lost_terminal_records: '0' },
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
      // Headless macOS may have no CVDisplayLink. Keep rAF advancing without
      // display vsync; otherwise the scroll assertion never resumes.
      '--disable-frame-rate-limit',
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

// The tall probe is a normal build: its GPU audio status reports
// unavailable, so Settings keeps the release layout. Since the MODULATION
// group lists every target for the selected LFO, the body passes the panel's
// 1500px cap at any window height and scrolls at both sizes.
for (const [label, height, gpuAudioAvailable] of [
  ['overflowing', 860, true], ['tall', 1800, false]]) {
  const settings = run({
    componentSource: shippingSurface, mode: 'settings', width: 1320, height,
    gpuAudioAvailable,
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
  'onChange: (next) => publish("routeOn" + lfo + "_" + t, lane(t), next)',
  'onChange: () => {}');
assert.notEqual(deadTargets, shippingSurface,
  'modulation target handler needle did not match; the negative control would be blind');
const deadModulation = run({
  componentSource: deadTargets, mode: 'modulation', width: 1320, height: 860,
});
assert.equal(deadModulation.status, 0, deadModulation.stderr.slice(-2000));
assert.equal(oracleText(deadModulation),
  'SPECTR_POLISH_ORACLE_ERROR: timed out waiting for bank switch bridge write');

// TEXT SIZE. Driven, not read: the panel is mounted with the SHIPPED defaults
// and the assertion is the scale that reaches the native call site, plus the
// deliberate ABSENCE of a chooser. A static read of the emitted source cannot
// tell a live call site from a dead one -- MOD-1 was exactly that failure.
// The shipped default is Medium, read out of the document rather than restated
// here, so this run's expected scale is the table's medium entry.
assert.equal(shippedDefaults.textSize, 'medium',
  'shipped text size default is no longer Medium');
const textSize = run({
  componentSource: shippingSurface, mode: 'textsize',
  width: 1320, height: 860, settings: shippedDefaults,
});
assert.equal(textSize.error, undefined, textSize.error && textSize.error.message);
assert.equal(textSize.status, 0, textSize.stderr.slice(-2000));
assert.equal(oracleText(textSize), 'SPECTR_TEXT_SIZE_OK:1.2');

// Control 1 -- wiring. Sever the mount-time effect and the driven scenario
// must stop seeing a scale write; every static assertion still passes.
const deadTextSizeRun = run({
  componentSource: deadTextSize, mode: 'textsize',
  width: 1320, height: 860, settings: shippedDefaults,
});
assert.equal(deadTextSizeRun.status, 0, deadTextSizeRun.stderr.slice(-2000));
assert.equal(oracleText(deadTextSizeRun),
  'SPECTR_POLISH_ORACLE_ERROR: timed out waiting for mount-time text scale write');

// Control 2 -- value. Leave the wiring intact and flatten MEDIUM back to 1.0,
// the scale the pre-1.0 build shipped at. The oracle reports what it read
// rather than a verdict, so this reads 1 where the shipping table reads 1.2:
// the scenario is measuring the real table, not a constant.
const flatMediumRun = run({
  componentSource: flatMediumScale, mode: 'textsize',
  width: 1320, height: 860, settings: shippedDefaults,
});
assert.equal(flatMediumRun.status, 0, flatMediumRun.stderr.slice(-2000));
assert.equal(oracleText(flatMediumRun), 'SPECTR_TEXT_SIZE_OK:1');

// Control 3 -- the non-default entries stay live even with no chooser. A host
// that persisted Large before the chooser was withdrawn must still reach the
// call site at 1.45, which is what keeps that table row meaningful.
const largeSettingRun = run({
  componentSource: shippingSurface, mode: 'textsize',
  width: 1320, height: 860,
  settings: { ...shippedDefaults, textSize: 'large' },
});
assert.equal(largeSettingRun.status, 0, largeSettingRun.stderr.slice(-2000));
assert.equal(oracleText(largeSettingRun), 'SPECTR_TEXT_SIZE_OK:1.45');

console.log('Spectr UX polish: dwell and modulation negative controls red; copy, hint, overflow, driven modulation, and driven text-size (medium default, no chooser, flattened-table and Large controls) scenarios green');
