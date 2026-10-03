import assert from 'node:assert/strict';
import fs from 'node:fs';

const path = process.argv[2];
assert(path, 'usage: test_gpu_audio_materialized_ui.mjs <runtime.json>');
const document = JSON.parse(fs.readFileSync(path, 'utf8'));
const html = document.html;
assert.match(html, /data-spectr-gpu-mode-indicator/);
assert.match(html, /data-spectr-gpu-audio-status-pill/);
assert.match(html, /render_mode_set/);
assert.match(html, /build_info_get/);
assert.match(html, /gpu_audio/);
assert.match(html, /Convolving on the CPU/);
assert.match(html, /CPU fallback/);
assert.match(html, /freeze_available === false/);
assert.match(html, /Freeze unavailable in this mode/);
console.log('PASS: native materialized editor contains the GPU audio controls and status contract');
