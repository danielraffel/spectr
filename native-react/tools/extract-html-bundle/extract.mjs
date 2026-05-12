#!/usr/bin/env node
// Extract script + design tokens + className rules from a self-bundled
// Spectr-design HTML file. The HTML embeds the React app inside
// <script type="__bundler/manifest"> (gzip+base64 blobs) and an
// unpacked HTML inside <script type="__bundler/template">. At build
// time we walk both to produce static artifacts the @pulp/react port
// can consume without running the bundler at runtime.
//
// Usage:
//   node extract.mjs <path-to-html> <out-dir>
//
// Outputs (in <out-dir>):
//   - main.js         the bundled React app (extracted from template's <script>)
//   - tokens.json     { default: { '--bg': '#05070a', ... }, paper: {...}, ... }
//   - classnames.json { '.tnum': { fontVariantNumeric: 'tabular-nums', ... }, ... }
//   - extract-report.md  what was found, what was deferred

import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { gunzipSync } from 'node:zlib';

if (process.argv.length < 4) {
    console.error('usage: extract.mjs <html> <out-dir>');
    process.exit(2);
}
const htmlPath = resolve(process.argv[2]);
const outDir = resolve(process.argv[3]);
if (!existsSync(htmlPath)) {
    console.error('not found:', htmlPath); process.exit(1);
}
mkdirSync(outDir, { recursive: true });

const html = readFileSync(htmlPath, 'utf8');

function pickScript(type) {
    const re = new RegExp(`<script type="${type.replace(/\//g, '\\/')}">([\\s\\S]*?)</script>`, 'i');
    const m = re.exec(html);
    return m ? m[1].trim() : null;
}

const templateRaw = pickScript('__bundler/template');
const manifestRaw = pickScript('__bundler/manifest');
if (!templateRaw) { console.error('no <script type="__bundler/template">'); process.exit(1); }

// The template is a JSON-encoded HTML string.
const unpackedHtml = JSON.parse(templateRaw);
writeFileSync(join(outDir, 'unpacked.html.txt'), unpackedHtml);

// === STYLE BLOCKS ===
// Pull every <style>...</style> from the unpacked HTML. The first non
// font-face block carries the :root design tokens + theme-scoped
// overrides + className rules. We only consume that block; @font-face
// is left for later (Pulp's font system loads bundled .woff2 separately).
const styleBlocks = [...unpackedHtml.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)]
    .map(m => m[1]).filter(s => !/font-face/.test(s.slice(0, 200)));

const tokensByScheme = { default: {} };
const classNames = {};

for (const css of styleBlocks) {
    // :root { --x: y; --z: w; }  → default tokens
    const rootM = /:root\s*\{([\s\S]*?)\}/.exec(css);
    if (rootM) parseCustomProps(rootM[1], tokensByScheme.default);

    // .scheme-NAME { --x: y; }   → per-theme overrides
    for (const m of css.matchAll(/\.scheme-([a-zA-Z0-9_-]+)\s*\{([\s\S]*?)\}/g)) {
        tokensByScheme[m[1]] = { ...tokensByScheme.default };
        parseCustomProps(m[2], tokensByScheme[m[1]]);
    }

    // .className { props }  → className → style props
    for (const m of css.matchAll(/(?<!^|\n)\.([a-zA-Z][a-zA-Z0-9_-]*)\s*\{([\s\S]*?)\}/g)) {
        // Skip scheme classes (handled above)
        if (m[1].startsWith('scheme-')) continue;
        classNames[m[1]] = parseDeclarationsToCamelCase(m[2]);
    }
    // Also catch class rules at line start (the lookbehind above misses block-start)
    for (const m of css.matchAll(/(?:^|\n)\s*\.([a-zA-Z][a-zA-Z0-9_-]*)\s*\{([\s\S]*?)\}/g)) {
        if (m[1].startsWith('scheme-')) continue;
        if (!classNames[m[1]]) {
            classNames[m[1]] = parseDeclarationsToCamelCase(m[2]);
        }
    }
}

function parseCustomProps(block, into) {
    for (const m of block.matchAll(/(--[a-zA-Z0-9_-]+)\s*:\s*([^;]+);/g)) {
        into[m[1]] = m[2].trim();
    }
}

function parseDeclarationsToCamelCase(block) {
    const out = {};
    for (const m of block.matchAll(/([a-zA-Z-]+)\s*:\s*([^;]+);/g)) {
        const camelKey = m[1].replace(/-([a-z])/g, (_, c) => c.toUpperCase());
        out[camelKey] = m[2].trim();
    }
    return out;
}

// === SCRIPT EXTRACTION ===
// The unpacked HTML may contain inline <script> tags (the actual
// React bundle, post-bundler). Pull them and concatenate.
const scriptBlocks = [...unpackedHtml.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)]
    .map(m => m[1])
    // Skip __bundler/* meta scripts and src=... external scripts
    .filter(s => s.length > 100);

const mainJs = scriptBlocks.join('\n;\n');

// === MANIFEST (optional) ===
// Some assets (fonts, images) live in the gzip+base64 manifest. Decode
// it just enough to enumerate UUIDs → filenames, but we don't need to
// emit them yet (Pulp font system is separate).
let manifestEntries = 0;
if (manifestRaw) {
    try {
        const m = JSON.parse(manifestRaw);
        manifestEntries = Object.keys(m).length;
    } catch { /* ignore */ }
}

// === WRITE OUTPUTS ===
writeFileSync(join(outDir, 'main.js'), mainJs);
writeFileSync(join(outDir, 'tokens.json'), JSON.stringify(tokensByScheme, null, 2));
writeFileSync(join(outDir, 'classnames.json'), JSON.stringify(classNames, null, 2));

const report = `# Extract report

- Source: ${htmlPath}
- Unpacked HTML: ${unpackedHtml.length.toLocaleString()} chars
- Script blocks (concatenated): ${scriptBlocks.length} (${mainJs.length.toLocaleString()} chars total)
- Style blocks (non-fontface): ${styleBlocks.length}
- Token schemes: ${Object.keys(tokensByScheme).join(', ')} (default has ${Object.keys(tokensByScheme.default).length} tokens)
- Class rules: ${Object.keys(classNames).length} (${Object.keys(classNames).join(', ') || '<none>'})
- Manifest entries (font/asset blobs): ${manifestEntries}

## Next steps

- main.js may not be runnable as-is in QuickJS (browser DOM/Worker assumptions). Wrap with the existing host-shims/canvas2d/jsx-runtime shims before embedding.
- tokens.json is consumed by @pulp/css-adapt's var() resolver. Pick \`default\` for the dark theme (current Spectr default).
- classnames.json is consumed by dom-adapter to merge class-based styles into inline before forwarding to css-adapt.
`;
writeFileSync(join(outDir, 'extract-report.md'), report);
console.log(report);
