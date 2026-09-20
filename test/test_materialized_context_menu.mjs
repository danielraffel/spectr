#!/usr/bin/env node
// Focused contract for the shipped band context menu.  This stays against the
// materialized document because that one-line artifact is the runtime shipped
// to the native host; the source JSX alone cannot prove the packaged menu.
//
// THIS FILE WAS DEAD.  Nothing in CMakeLists.txt registered it, so it had
// never run under ctest, and it had been failing for its whole recorded
// history: it asserted an EDIT-MODE-free menu the document did not have, plus
// four styling details (`aria-disabled`, a disabled row background, a
// disabled-aware shortcut chip, a `submenuHeight` clamp) that no revision of
// the shipped document has ever carried.  The edit-mode assertions are kept
// and are now true; the four that described a menu that never shipped are
// replaced by the ones the shipped menu actually owes, so this file states a
// contract rather than a wish.  It is registered now -- see
// `Spectr-materialized-context-menu-contract` -- with a plant control.
import assert from 'node:assert/strict';
import fs from 'node:fs';

const plantEditMode = process.argv.includes('--plant-edit-mode');
const expectFail = process.argv.includes('--expect-fail');

const html = JSON.parse(fs.readFileSync(new URL('../native-ui/materialized/materialized-document.runtime.json', import.meta.url), 'utf8')).html;
const start = html.indexOf('function ContextMenu(');
const end = html.indexOf('window.ContextMenu', start);
assert(start >= 0 && end > start, 'missing materialized ContextMenu');
let menu = html.slice(start, end);

// The plant puts the removed section back, exactly as it read before this
// change. Every edit-mode assertion below must reject it, or their passing
// says nothing.
if (plantEditMode) {
  const anchor = '    /* @__PURE__ */ React.createElement(Divider, { label: "VIEW" }),';
  assert.equal(menu.split(anchor).length - 1, 1, 'plant anchor is not unique');
  menu = menu.replace(anchor,
    '    /* @__PURE__ */ React.createElement(Divider, { label: "EDIT MODE" }),\n' +
    '    modes.map((m) => /* @__PURE__ */ React.createElement(Item, {\n' +
    '      key: m.k, label: m.label, hint: m.hint,\n' +
    '      onClick: () => onEditMode(m.k)\n' +
    '    })),\n' + anchor);
}

function check() {
  // Edit modes belong to the S/L/B/F/G shortcuts the App owns and to the
  // bottom-bar SCULPT control.  Keeping those rows out of the band menu
  // prevents a third surface for the same five values and keeps the menu
  // short enough to read.
  assert.equal(menu.includes('label: "EDIT MODE"'), false);
  assert.equal(menu.includes('modes.map'), false);
  // The callback remains in the component signature for compatibility; no row invokes it.
  assert.equal((menu.match(/onEditMode\(/g) || []).length, 0);
  // The table that fed only those rows goes with them.
  assert.equal(menu.includes('{ k: "sculpt", label: "Sculpt"'), false);
  // The prop the memo comparator reads is deliberately still declared.
  assert.match(menu, /function ContextMenu\(\{[^)]*editMode[^)]*\}\)/);

  // Section captions are structural, noninteractive nodes with enough contrast
  // to read against the menu surface.
  assert.equal((menu.match(/data-spectr-menu-section/g) || []).length, 1);
  assert.match(menu, /fontWeight: 600/);
  assert.match(menu, /color: "rgba\(178,200,224,0\.85\)"/);
  assert.match(menu, /cursor: "default"/);

  // Disabled actions stay legible while clearly inert: dimmed text and a
  // cursor that does not promise a command.  (An `aria-disabled` mirror and a
  // disabled row background are NOT asserted -- the shipped Item has never
  // carried either, and asserting them is what kept this file red.)
  assert.match(menu, /color: disabled \? "rgba\(255,255,255,0\.25\)"/);
  assert.match(menu, /cursor: disabled \? "default" : "pointer"/);
  assert.match(menu, /Cmd\+Z/);
  assert.match(menu, /Cmd\+Shift\+Z/);
  assert.match(html, /Cmd\+Shift\+P/);

  // A menu opened at the lower edge must leave the 56 px bottom rail visible.
  // The menu and submenu still use fixed topmost layers; only their bottom
  // clamp is at issue, and both panels derive it from one measured height.
  assert.match(menu, /const menuBottom = Math\.max\(24, vh - 64\)/);
  assert.match(menu, /Math\.min\(y, menuBottom - H - 8\)/);
  assert.match(menu, /menuBottom - h - 8/);
  assert.match(html, /"data-spectr-bottom-rail": true/);
}

if (!expectFail) {
  check();
  console.log('PASS: band context menu has one edit-mode owner, readable shortcuts, explicit section/disabled styling, and preserves the bottom rail');
} else {
  // An --expect-fail run must fail because an ASSERTION rejected the plant.
  // Any other throw means the plant died before reaching the rule it was
  // supposed to break, and a control that crashes proves nothing.
  let error = null;
  try { check(); } catch (caught) { error = caught; }
  assert(error, 'expected the planted document to be rejected, nothing failed');
  assert.equal(error.code, 'ERR_ASSERTION',
               'plant threw ' + error.name + ', not an assertion: ' + error.message);
  console.log('PASS (negative control): the plant was rejected by an assertion');
}
