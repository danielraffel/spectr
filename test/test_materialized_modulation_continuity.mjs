#!/usr/bin/env node
import fs from 'node:fs';

const path = process.argv[2] || 'native-ui/materialized/materialized-document.runtime.json';
const html = JSON.parse(fs.readFileSync(path, 'utf8')).html;
const fail = (message) => { console.error(`FAIL: ${message}`); process.exitCode = 1; };
const requireText = (text, message) => { if (!html.includes(text)) fail(message); };

// A band-count change must retain the already painted plot and keep the draw
// loop alive until the short cross-fade has completed.
requireText('__spectrBandLayoutTransition', 'band layout transition marker missing');
requireText('sctx.drawImage(source, 0, 0)', 'old plot is not snapshotted before repacking');
requireText('globalThis.__spectrBeginBandLayoutTransition(previousBands, shown.bands)',
  'freeze_display does not arm the transition for a count change');
requireText('ctx.globalAlpha = 1 - (1 - Math.pow(1 - t, 3))',
  'band transition does not ease the old frame out');
requireText('bandLayoutTransitionRef !== "undefined" && bandLayoutTransitionRef.current',
  'draw loop can park while the band transition is active');

// Preset modulation must have a neighbourhood before a dropdown is opened.
requireText('useAppS("factory:flat")', 'preset modulation still boots with no selected preset');
requireText('spectrSendPresetNeighbourhood(selectedPatternId,',
  'preset neighbourhood publication is missing');
requireText('defaultId !== "factory:flat"',
  'hydrated user default cannot replace the factory bootstrap');

// Guard against the old one-frame reset paths returning.
if (html.includes('setSelectedPatternId] = useAppS(null)'))
  fail('null preset bootstrap returned');
if (!html.includes('store.display = shown;'))
  fail('modulated display state is no longer updated');

if (!process.exitCode) console.log('PASS: band-count continuity and preset modulation bootstrap');
