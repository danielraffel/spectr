import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const path = process.argv[2];
assert(path, 'usage: test_tracking_gpu_ui.mjs <resources/editor.html|runtime.json>');
const raw = fs.readFileSync(path, 'utf8');
const materialized = path.endsWith('.json');
const html = materialized ? JSON.parse(raw).html : raw;
const occurs = re => [...html.matchAll(re)].length;
assert.equal(occurs(/function SpectrTrackingGpuSettings\(\)/g), 1);
assert.equal(occurs(/data-spectr-tracking-gpu-toggle/g), 1);
assert.equal(occurs(materialized ? /React\.createElement\(SpectrTrackingGpuSettings, null\)/g
  : /<SpectrTrackingGpuSettings \/>/g), 1);
assert.equal(occurs(/function spectrTrackingGpuDisplay\(tracking\)/g), 1);
assert.doesNotMatch(html, /Tracking always (?:uses|runs on) the CPU|GPU runs in Mixing only|Tracking uses the CPU path/);
assert.match(html, /tracking_gpu_processing_set/);
assert.match(html, /tracking\.gpu_samples/);
assert.match(html, /tracking\.cpu_samples/);
assert.match(html, /Tracking GPU refusal refresh failed/);
assert.match(html, /data-spectr-tracking-gpu-status/);
assert.match(html, /enabled: (?:enabled === true|!!next)/);

const fn = html.match(/function spectrTrackingGpuDisplay\(tracking\) \{[\s\S]*?^\}/m)[0];
const display = vm.runInNewContext('(' + fn + ')');
const cases = [
  [null, 'unknown', '—', /unavailable/],
  [{processing:false}, 'cpu', 'CPU', /Tracking on the CPU/],
  [{processing:true, eligibility:'unprobed'}, 'selected', 'GPU set', /waiting for provider/],
  [{processing:true, eligibility:'ready', actual:{gpu_selected:'0',cpu_fallback:'42'}}, 'ready', 'Ready', /waiting for delivery.*42 CPU fallback/],
  [{processing:true, eligibility:'active', actual:{gpu_selected:'18446744073709551614',cpu_fallback:'19'}}, 'delivered','GPU used', /18446744073709551614 GPU blocks delivered · 19 CPU fallback/],
  [{processing:true, eligibility:'unprobed', provider_state:'degraded', actual:{gpu_selected:'4',cpu_fallback:'55'}}, 'fallback','CPU', /degraded · CPU active.*4 GPU blocks delivered/],
  [{processing:false, eligibility:'provider_refused'}, 'refused','CPU', /refused · CPU active/],
  [{processing:true, eligibility:'unsupported_platform'}, 'unsupported','CPU', /unavailable on this platform/],
];
for (const [input, state, chip, text] of cases) {
  const result = display(input);
  assert.equal(result.state, state);
  assert.equal(result.chip, chip);
  assert.match(result.text, text);
}
if (materialized) {
  assert.match(html, /const chip = mixing \? \(gpuOn \? "GPU" : "CPU"\) : trackingView\.chip/);
  assert.match(html, /let text = !mixing \? trackingView\.text/);
  assert.match(html, /if \(!gpuOn && !trackingSelected\) return undefined/);
  assert.match(html, /toggleable \? \(\) => spectrSetGpuProcessing\(!gpuOn\)/);
} else {
  assert.match(html, /const label = mixing \? .* : tracking\.chip/);
  assert.match(html, /let text = mixing \? 'GPU unavailable' : tracking\.text/);
}
console.log('PASS Tracking GPU readout, counters, independent control and single panel');
