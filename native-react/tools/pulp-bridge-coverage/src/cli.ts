#!/usr/bin/env node
// pulp-bridge-coverage — AST-walks a JS bundle and reports W3C-spec
// coverage of canvas2d (later: DOM, SVG, form controls) against the
// Pulp WidgetBridge surface. Sibling of pulp-css-analyze.

import { Command } from 'commander';
import { writeFileSync } from 'node:fs';
import { extractCanvasUsage } from './extract.js';
import { parseShim } from './parse-shim.js';
import { parseBridge } from './parse-bridge.js';
import { buildVerdicts, renderMarkdown } from './report.js';

const program = new Command();
program
    .name('pulp-bridge-coverage')
    .description('Spec-driven Pulp bridge coverage report (Canvas 2D, …).')
    .requiredOption('--bundle <path>', 'JS bundle to analyze (e.g. native-react/dist/editor.js)')
    .requiredOption('--shim <path>', 'canvas2d-shim source (e.g. native-react/canvas2d-shim.ts)')
    .requiredOption('--bridge <path>', 'Pulp widget_bridge.cpp (e.g. ~/Code/pulp/core/view/src/widget_bridge.cpp)')
    .option('-o, --output <path>', 'Markdown report path (defaults to stdout)')
    .parse(process.argv);

const opts = program.opts() as { bundle: string; shim: string; bridge: string; output?: string };

const bundle = extractCanvasUsage(opts.bundle);
const shim = parseShim(opts.shim);
const bridge = parseBridge(opts.bridge);

const rows = buildVerdicts({ bundle, shim, bridge });
const md = renderMarkdown({ bundle, shim, bridge }, rows);

if (opts.output) {
    writeFileSync(opts.output, md);
    process.stderr.write(`Wrote ${opts.output}\n`);
} else {
    process.stdout.write(md);
}
