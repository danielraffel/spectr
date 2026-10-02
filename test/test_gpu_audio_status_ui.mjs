// Source-level contract for the experimental GPU audio status surface.
// The browser lane exercises the mounted document; this quick check catches
// an incomplete materialization recipe before an expensive plugin build.
import assert from 'node:assert/strict';
import fs from 'node:fs';

const htmlPath = process.argv[2];
assert(htmlPath, 'usage: test_gpu_audio_status_ui.mjs <resources/editor.html>');
const source = fs.readFileSync(htmlPath, 'utf8');

assert.match(source, /data-spectr-gpu-mode-indicator/);
assert.match(source, /data-spectr-gpu-audio-status-pill/);
assert.match(source, /render_mode_set/);
assert.match(source, /postMessage\('render_mode_set', \{ mode: next \}/);
assert.match(source, /setRenderMode\(mode\)/);
assert.match(source, /gpu_audio/);
assert.match(source, /onRenderModeChange=\{setRenderModeFromUi\}/);
assert.match(source, /renderMode === 'linear_phase'/);
assert.match(source, /Convolving on the CPU/);
assert.match(source, /CPU fallback/);
// The UI must never silently call the old unimplemented toggle component.
assert.doesNotMatch(source, /function GpuModeControl\(/);
console.log('PASS: GPU audio status indicator, pill, native mode command, and CPU fallback contract are authored');
