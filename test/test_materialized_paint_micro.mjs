#!/usr/bin/env node
// TWO PER-FRAME HELPERS DO THEIR WORK ONCE, AND ANSWER EXACTLY AS BEFORE.
//
//   ANALYZER  `SpectrAnalyzer.sample(logFrequency)` is called ~500 times a
//             frame (the spectrum's steps, one per band for the glow energy,
//             the minimap). Each call turned the log frequency into a
//             frequency with 10^x and straight back with log10, and took both
//             trace bounds' log10 again. The trace's log bounds are fixed per
//             frame and the caller already has the log frequency, so a sample
//             must do no pow/log10 at all.
//   COLOUR    `specColor(pos, alpha, theme)` builds ~450 hsla() strings a
//             frame, re-running the theme branch although hue, saturation and
//             lightness depend only on (theme, pos). They must be computed
//             once per pair.
//
// Both halves are checked for the SAME answers as the previous code, which is
// kept below verbatim as the oracle: every theme at every band position and a
// spread of alphas for the colour (character-identical strings), and every
// trace point plus off-grid points for the analyzer (within 1e-12 of a
// normalised amount -- far below a tenth of a pixel).
//
// Usage:
//   node test_materialized_paint_micro.mjs <runtime.json> <spectr-native-services.js>
//        [--expect-fail]
// --expect-fail inverts the verdict, for a planted document or source.

import { readFileSync } from "node:fs";
import vm from "node:vm";

const args = process.argv.slice(2);
const expectFail = args.includes("--expect-fail");
const [documentPath, servicesPath] = args.filter((a) => !a.startsWith("--"));
if (!documentPath || !servicesPath) {
  console.error("usage: test_materialized_paint_micro.mjs <runtime.json> "
    + "<spectr-native-services.js> [--expect-fail]");
  process.exit(2);
}
const html = JSON.parse(readFileSync(documentPath, "utf8")).html;
const services = readFileSync(servicesPath, "utf8");
const failures = [];
const fail = (m) => failures.push(m);

// A Math whose pow/log10/sin calls are counted.
const counts = { pow: 0, log10: 0, sin: 0 };
const countingMath = new Proxy(Math, {
  get(target, key) {
    if (key in counts) return (...a) => { counts[key]++; return target[key](...a); };
    return target[key];
  },
});
const reset = () => { for (const k of Object.keys(counts)) counts[k] = 0; };

// ------------------------------------------------------------------ colour
{
  const start = html.indexOf("function specColor(");
  const end = html.indexOf("const THEMES = [", start);
  const specialPrefix = html.lastIndexOf("const specColorPrefixes", start);
  const from = specialPrefix >= 0 && start - specialPrefix < 400 ? specialPrefix : start;
  if (start < 0 || end < 0) {
    console.error("FAIL: specColor is not where this suite expects it");
    process.exit(2);
  }
  const shipped = html.slice(from, end);
  const sandbox = { Math: countingMath, Map, String };
  vm.runInNewContext(shipped + "\nglobalThis.__specColor = specColor;", sandbox);
  const specColor = sandbox.__specColor;

  // The previous implementation, verbatim, as the oracle.
  const oracleSrc = String.raw`
function oracle(pos, alpha = 1, theme = "spectral") {
  let hue, sat = 80, light = 62;
  if (theme === "mono") { hue = 210; sat = 10 + pos * 30; light = 55 + pos * 25; }
  else if (theme === "cool") { hue = 260 - pos * 100; sat = 75; }
  else if (theme === "warm") { hue = 50 - pos * 60; sat = 80; light = 58 + pos * 8; }
  else if (theme === "neon") { hue = 300 - pos * 120; sat = 98; light = 58 + Math.sin(pos * Math.PI) * 8; }
  else if (theme === "dusk") { hue = 280 - pos * 250; sat = 55 + pos * 20; light = 50 + pos * 15; }
  else if (theme === "forest") { hue = 140 - pos * 90; sat = 55 + pos * 25; light = 48 + pos * 18; }
  else if (theme === "ember") { hue = 10 + pos * 50; sat = 85; light = 35 + pos * 45; }
  else if (theme === "phosphor") { hue = 115 + pos * 25; sat = 85; light = 45 + pos * 28; }
  else if (theme === "plasma") { hue = 280 - pos * 240; sat = 85 - pos * 20; light = 35 + pos * 40; }
  else if (theme === "ice") { hue = 200 + pos * 20; sat = 45 + pos * 45; light = 88 - pos * 40; }
  else if (theme === "rose") { hue = 340 + pos * 20; sat = 55 + pos * 25; light = 78 - pos * 30; }
  else if (theme === "solar") { hue = 0 + pos * 55; sat = 95 - pos * 30; light = 42 + pos * 48; }
  else if (theme === "oceanic") { hue = 180 + pos * 80; sat = 65; light = 62 - pos * 20; }
  else if (theme === "sodium") { hue = 38; sat = 80 + pos * 15; light = 45 + pos * 30; }
  else { hue = 240 - pos * 300; }
  return ` + "`hsla(${hue}, ${sat}%, ${light}%, ${alpha})`" + String.raw`;
}
globalThis.__oracle = oracle;`;
  const oracleBox = { Math };
  vm.runInNewContext(oracleSrc, oracleBox);
  const oracle = oracleBox.__oracle;

  const themes = ["spectral", "mono", "cool", "warm", "neon", "dusk", "forest",
    "ember", "phosphor", "plasma", "ice", "rose", "solar", "oceanic", "sodium"];
  const alphas = [1, 0, 0.25, 0.42, 0.12 + 0.37 * 0.53, 1 / 3];
  let compared = 0;
  for (const theme of themes)
    for (const n of [32, 40, 48, 56, 64])
      for (let i = 0; i < n; i++) {
        const pos = i / (n - 1);
        for (const alpha of alphas) {
          compared++;
          const got = specColor(pos, alpha, theme);
          const want = oracle(pos, alpha, theme);
          if (got !== want) {
            fail(`specColor(${pos}, ${alpha}, ${theme}) = ${got}, previously ${want}`);
            break;
          }
        }
      }
  // Default arguments too.
  if (specColor(0.5) !== oracle(0.5)) fail("specColor(0.5) changed its default answer");

  // The cost: one neon band re-coloured 100 times computes its lightness once.
  reset();
  for (let k = 0; k < 100; k++) specColor(0.3141, 0.1 + k / 1000, "neon");
  console.log("colour    %d strings compared; neon band x100 -> %d sin() call(s)",
    compared, counts.sin);
  if (counts.sin > 1) {
    fail(`one band recoloured 100 times ran the theme branch ${counts.sin} times; `
      + "hue, saturation and lightness depend only on (theme, pos)");
  }
}

// ---------------------------------------------------------------- analyzer
{
  const listeners = new Map();
  const sandbox = {
    Math: countingMath, Number, Array, Object, JSON, String, Promise, Map, Set,
    console: { log() {}, warn() {}, error() {} },
    setTimeout() { return 0; }, clearTimeout() {},
  };
  sandbox.globalThis = sandbox;
  sandbox.window = sandbox;
  void listeners;
  vm.runInNewContext(services, sandbox, { filename: "spectr-native-services.js" });
  const publish = sandbox.__spectrPublishNativeMessage;
  const analyzer = sandbox.SpectrAnalyzer;
  if (typeof publish !== "function" || !analyzer) {
    console.error("FAIL: the services source installs no analyzer or publisher");
    process.exit(2);
  }
  const trace = (n, min, max, seed) => ({
    min_hz: min, max_hz: max,
    magnitude_db: Array.from({ length: n }, (_, i) =>
      -100 + 60 * Math.abs(Math.sin(i * 0.37 + seed)) + (i % 7)),
  });
  const frame = {
    schema_version: 1, epoch: 1, sequence_number: 1,
    floor_db: -120, ceiling_db: 24,
    visible: trace(321, 180, 7300, 0.1),
    overview: trace(121, 20, 20000, 0.7),
  };
  publish("analyzer_frame", frame, "");
  if (!analyzer.debugSnapshot()) {
    console.error("FAIL: the analyzer refused a valid frame, so nothing below "
      + "is evidence");
    process.exit(2);
  }
  const oracle = (tr, logFrequency) => {
    const frequency = Math.pow(10, logFrequency);
    const position = Math.max(0, Math.min(1,
      (Math.log10(frequency) - Math.log10(tr.min_hz))
      / (Math.log10(tr.max_hz) - Math.log10(tr.min_hz))));
    const exact = position * (tr.magnitude_db.length - 1);
    const left = Math.floor(exact);
    const right = Math.min(left + 1, tr.magnitude_db.length - 1);
    const mix = exact - left;
    const db = tr.magnitude_db[left]
      + (tr.magnitude_db[right] - tr.magnitude_db[left]) * mix;
    return Math.max(0, Math.min(1,
      (db - frame.floor_db) / (frame.ceiling_db - frame.floor_db)));
  };
  let worst = 0;
  let samples = 0;
  const check = (name, tr, lf) => {
    const got = analyzer.sample(lf, 0, name);
    const want = oracle(tr, lf);
    worst = Math.max(worst, Math.abs(got - want));
    samples++;
  };
  // Exactly the editor's spectrum sweep when the view is the trace's range:
  // steps + 1 == 321 points from log10(min) to log10(max).
  const lmin = Math.log10(frame.visible.min_hz);
  const lmax = Math.log10(frame.visible.max_hz);
  for (let i = 0; i <= 320; i++) check("visible", frame.visible, lmin + i / 320 * (lmax - lmin));
  // Off the grid, outside the range, and the overview trace.
  for (let i = 0; i <= 1000; i++) {
    const lf = 1.0 + i * 0.0035;
    check("visible", frame.visible, lf);
    check("overview", frame.overview, lf);
  }
  console.log("analyzer  %d samples, worst difference %s", samples, worst);
  if (!(worst <= 1e-12)) {
    fail(`sample() moved by ${worst} of a normalised amount from its previous answer`);
  }
  reset();
  for (let i = 0; i <= 320; i++) analyzer.sample(lmin + i / 320 * (lmax - lmin), 0, "visible");
  console.log("analyzer  321 samples -> %d pow(), %d log10()", counts.pow, counts.log10);
  if (counts.pow !== 0 || counts.log10 !== 0) {
    fail(`321 samples called pow ${counts.pow} and log10 ${counts.log10} times; the `
      + "caller already works in log frequency and the trace bounds are fixed "
      + "per frame");
  }
}

if (expectFail) {
  if (failures.length === 0) {
    console.error("FAIL: the planted input PASSED -- the control proves nothing");
    process.exit(1);
  }
  console.log("PASS (inverted): the planted input was rejected.");
  process.exit(0);
}
if (failures.length) {
  for (const f of failures) console.error("FAIL: " + f);
  process.exit(1);
}
console.log("PASS: sample() and specColor() answer exactly as before, with the "
  + "per-call work done once.");
