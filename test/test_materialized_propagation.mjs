#!/usr/bin/env node
// Guards the press-propagation contract the materialized document relies on.
//
// Pulp's native bridge treats a handler's RETURN VALUE as the propagation
// marker: a handler stops a press from reaching the nodes behind it only when
// it returns what stopPropagation() returned. A modal panel sits inside a scrim
// whose own press handler closes it, so the panel's guard must be an
// expression-body arrow that returns the marker. A wrapper that calls
// stopPropagation() inside a block body drops the value, the press reaches the
// scrim, and the panel closes on a tap that landed inside it -- toggles then
// look like they vanish, because the panel closed instead of the toggle
// flipping.
//
// Scope is the three modal panels that sit inside a closing scrim. The help
// popover is deliberately excluded: it has no scrim, and its press-outside
// dismissal is Pulp's own overlay handling.
//
// Usage:
//   node test_materialized_propagation.mjs <materialized-document.runtime.json>
//        [--plant-swallow] [--expect-fail]
//
// --plant-swallow reinstates the value-dropping wrappers (the exact pre-fix
// shape). --expect-fail inverts the verdict, so the negative control fails when
// the planted defect is NOT rejected and when anything else goes wrong.

import { readFileSync } from "node:fs";

const args = process.argv.slice(2);
const plantSwallow = args.includes("--plant-swallow");
const expectFail = args.includes("--expect-fail");
const documentPath = args.find((a) => !a.startsWith("--"));

if (!documentPath) {
  console.error("usage: test_materialized_propagation.mjs <runtime.json> [--plant-swallow] [--expect-fail]");
  process.exit(2);
}

const GUARDED_PANELS = [
  "data-spectr-pattern-manager-panel",
  "data-spectr-save-panel",
  "data-spectr-settings-panel",
];

// onClick: (e) => e.stopPropagation()  -- expression body, returns the marker.
const guardExpression = /onClick:\s*\((\w+)\)\s*=>\s*\1\.stopPropagation\(\)/;

let html = JSON.parse(readFileSync(documentPath, "utf8")).html;
if (typeof html !== "string" || html.length === 0) {
  console.error(`FAIL: ${documentPath} has no usable "html" field`);
  process.exit(2);
}

if (plantSwallow) {
  const before = html;
  html = html.replace(
    /onClick:\s*\((\w+)\)\s*=>\s*\1\.stopPropagation\(\)/g,
    (_m, param) => `onClick: (${param}) => { ${param}.stopPropagation(); }`
  );
  const planted = (before.match(/onClick:\s*\((\w+)\)\s*=>\s*\1\.stopPropagation\(\)/g) || []).length;
  if (planted === 0 || html === before) {
    console.error("FAIL: --plant-swallow rewrote nothing; the control cannot prove anything");
    process.exit(2);
  }
  console.log(`planted ${planted} value-dropping wrapper(s)`);
}

// The props of a createElement call run from the panel's own data attribute to
// the style object that follows it, which is where onDismiss/onClick live.
const propsAfter = (attribute) => {
  const at = html.indexOf(`"${attribute}"`);
  if (at < 0) return null;
  const styleAt = html.indexOf("style:", at);
  const end = styleAt > at && styleAt - at < 1200 ? styleAt : at + 900;
  return html.slice(at, end);
};

const failures = [];
for (const panel of GUARDED_PANELS) {
  const props = propsAfter(panel);
  if (props === null) {
    failures.push(`${panel}: not present in the document`);
    continue;
  }
  if (!guardExpression.test(props)) {
    const swallowed = /onClick:\s*\(\w+\)\s*=>\s*\{[^}]*stopPropagation\(\)/.test(props);
    failures.push(
      swallowed
        ? `${panel}: its press guard drops stopPropagation()'s value (block-body wrapper)`
        : `${panel}: no press guard returning stopPropagation()`
    );
  }
}

const passed = failures.length === 0;
if (passed) {
  console.log(`OK: ${GUARDED_PANELS.length} modal panel press guards return the propagation marker`);
} else {
  for (const f of failures) console.error(`FAIL: ${f}`);
}

if (expectFail) {
  if (passed) {
    console.error("FAIL: the planted value-dropping wrappers were NOT rejected");
    process.exit(1);
  }
  console.log("OK: the planted value-dropping wrappers were rejected");
  process.exit(0);
}
process.exit(passed ? 0 : 1);
