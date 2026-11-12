// The window an LFO moves is drawn, paint only.
//
// Checks the materialized runtime's modulation-frame parser accepts the
// optional drawn window and refuses a malformed one, and that `renderAll`
// swaps the drawn window into the live view for exactly one paint pass and
// restores the user's window on the way out. Each textual contract carries a
// planted negative so a green run proves the check can fail.
//
// usage: test_materialized_modulated_viewport.mjs <runtime.json> [--plant-no-restore] [--expect-fail]
import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantNoRestore = args.includes("--plant-no-restore");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));
if (!documentPath) {
  console.error("usage: test_materialized_modulated_viewport.mjs <runtime.json> [--plant-no-restore] [--expect-fail]");
  process.exit(2);
}
let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}
const failures = [];
const check = (ok, message) => { if (!ok) failures.push(message); };

const plant = (label, from, to) => {
  const hits = html.split(from).length - 1;
  if (hits !== 1) {
    console.error(`FAIL: plant "${label}" found ${hits} sites, expected exactly 1`);
    process.exit(2);
  }
  html = html.replace(from, to);
};
if (plantNoRestore) {
  plant("no-restore", "if (paintView) { view.lmin = userLmin; view.lmax = userLmax; }", "/* restore removed */");
}

// 1. The parser, lifted out of the document and run as written.
const start = html.indexOf("function parseSpectrModulationFrame(payload) {");
check(start >= 0, "parseSpectrModulationFrame is missing");
if (start >= 0) {
  const end = html.indexOf("\nfunction parseSpectrNativeLiveState", start);
  check(end > start, "parseSpectrModulationFrame has no terminator");
  const parse = new Function(`${html.slice(start, end)}; return parseSpectrModulationFrame;`)();
  const n = 32;
  const base = { active: true, n_visible: n, gain_db: new Array(n).fill(0), muted: new Array(n).fill(false) };
  const plain = parse(base);
  check(plain && plain.viewport === null, "a frame without a drawn window parses with viewport null");
  const moved = parse({ ...base, viewport_driven: true, min_hz: 100, max_hz: 10000 });
  check(moved && moved.viewport && Math.abs(moved.viewport.lmin - 2) < 1e-9 && Math.abs(moved.viewport.lmax - 4) < 1e-9,
        "a driven frame carries log10 bounds");
  check(parse({ ...base, viewport_driven: false, min_hz: 100, max_hz: 10000 }).viewport === null,
        "an undriven frame ignores the bounds it carries");
  check(parse({ ...base, viewport_driven: true, min_hz: 10000, max_hz: 100 }) === null,
        "an inverted window is refused");
  check(parse({ ...base, viewport_driven: true, min_hz: 0, max_hz: 100 }) === null,
        "a zero lower bound is refused");
}

// 2. The paint-only swap in renderAll: set before the geometry is read,
//    restored in a finally, never written to viewRef.
const renderStart = html.indexOf("const renderAll = useCallback(() => {");
const renderEnd = html.indexOf("}, [view, N, bloom,", renderStart);
check(renderStart >= 0 && renderEnd > renderStart, "renderAll is missing");
const renderAll = html.slice(renderStart, renderEnd);
const swapAt = renderAll.indexOf("if (paintView) { view.lmin = paintView.lmin; view.lmax = paintView.lmax; }");
const geomAt = renderAll.indexOf("const g = getGeom();");
check(swapAt >= 0 && geomAt > swapAt, "the drawn window is swapped in before the geometry is read");
check(renderAll.includes("} finally {") && renderAll.includes("if (paintView) { view.lmin = userLmin; view.lmax = userLmax; }"),
      "the user's window is restored in a finally block");
check(!renderAll.includes("viewRef.current.lmin ="), "renderAll never writes the drawn window into viewRef");

// 3. The bank clears the drawn window on the falling edge and takes it from
//    every active frame.
const applyAt = html.indexOf("applyModulationFrame: (state) => {");
check(applyAt >= 0, "applyModulationFrame is missing");
const apply = html.slice(applyAt, applyAt + 2500);
check(apply.indexOf("modulationViewRef.current = null;") >= 0 &&
      apply.indexOf("modulationViewRef.current = null;") < apply.indexOf("modulationActiveRef.current = false;"),
      "the falling edge clears the drawn window before releasing the overlay");
check(apply.includes("modulationViewRef.current = state.viewport || null;"), "an active frame installs the drawn window");

const failed = failures.length > 0;
if (expectFail) {
  if (!failed) { console.error("FAIL: planted defect was not detected"); process.exit(1); }
  console.log(`PASS: planted defect detected (${failures[0]})`);
  process.exit(0);
}
if (failed) { for (const f of failures) console.error(`FAIL: ${f}`); process.exit(1); }
console.log("PASS: modulated viewport is parsed, painted through, and restored");
