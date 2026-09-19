#!/usr/bin/env node
// Focused contract for the shipped band context menu.  This stays against the
// materialized document because that one-line artifact is the runtime shipped
// to the native host; the source JSX alone cannot prove the packaged menu.
import assert from 'node:assert/strict';
import fs from 'node:fs';

const html = JSON.parse(fs.readFileSync(new URL('../native-ui/materialized/materialized-document.runtime.json', import.meta.url), 'utf8')).html;
const start = html.indexOf('function ContextMenu(');
const end = html.indexOf('window.ContextMenu', start);
assert(start >= 0 && end > start, 'missing materialized ContextMenu');
const menu = html.slice(start, end);

// Edit modes belong to the dedicated Edit Mode popover.  Keeping those rows
// out of the band menu prevents duplicate controls and leaves one owner for
// the S/L/B/F/G shortcuts.
assert.equal(menu.includes('label: "EDIT MODE"'), false);
assert.equal(menu.includes('modes.map'), false);
// The callback remains in the component signature for compatibility; no row invokes it.
assert.equal((menu.match(/onEditMode\(/g) || []).length, 0);

// Section captions are structural, noninteractive nodes with enough contrast
// to read against the menu surface.
assert.equal((menu.match(/data-spectr-menu-section/g) || []).length, 1);
assert.match(menu, /fontWeight: 600/);
assert.match(menu, /color: "rgba\(178,200,224,0\.85\)"/);
assert.match(menu, /cursor: "default"/);

// Disabled actions remain legible while clearly inert, and their shortcut
// chips follow the same state instead of implying an available command.
assert.match(menu, /"aria-disabled": disabled \? "true" : "false"/);
assert.match(menu, /background: disabled \? "rgba\(255,255,255,0\.035\)"/);
assert.ok(menu.includes('color: disabled ? "rgba(255,255,255,0.45)"'));
assert.ok(menu.includes('cursor: disabled ? "not-allowed"'));
assert.match(menu, /spectrShortcutChipStyle\(disabled\)/);
assert.match(menu, /Cmd\+Z/);
assert.match(menu, /Cmd\+Shift\+Z/);
assert.match(html, /Cmd\+Shift\+P/);

console.log('PASS: band context menu has one edit-mode owner, readable shortcuts, and explicit section/disabled styling');
