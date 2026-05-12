// Entry point for the `pulp-css-analyze` CLI. Pure orchestration — all
// real work lives in extract.ts / report.ts / ai-suggest.ts.

import { Command } from 'commander';
import { readFile, writeFile } from 'node:fs/promises';
import { resolve, dirname, basename } from 'node:path';
import { extractStyles } from './extract.js';
import { buildReport } from './report.js';
import { getDefaultBridgeFunctions } from './known-bridge.js';
import { generateAiSuggestions } from './ai-suggest.js';

interface CliOptions {
  out?: string;
  ai: boolean;
  bridgeList?: string;
}

export async function main(argv: string[]): Promise<number> {
  const program = new Command();
  program
    .name('pulp-css-analyze')
    .description(
      'AOT analyzer that reports CSS coverage of a JS bundle against the @pulp/react bridge surface.',
    )
    .argument('<bundle>', 'path to a JS bundle file (e.g. dist/editor.js)')
    .option('-o, --out <file>', 'write the markdown report to <file> (default: stdout)')
    .option('--ai', 'augment unmapped suggestions via the Anthropic API', false)
    .option('--bridge-list <file>', 'path to a JSON array of known bridge setX/createX names')
    .allowExcessArguments(false)
    .helpOption('-h, --help', 'show usage');

  program.parse(argv);
  const opts = program.opts<CliOptions>();
  const [bundlePathRaw] = program.args;
  if (!bundlePathRaw) {
    process.stderr.write('error: bundle path is required\n');
    return 2;
  }
  const bundlePath = resolve(bundlePathRaw);

  let source: string;
  try {
    source = await readFile(bundlePath, 'utf8');
  } catch (err) {
    process.stderr.write(`error: cannot read ${bundlePath}: ${(err as Error).message}\n`);
    return 1;
  }

  const bridge = await loadBridge(opts.bridgeList);

  const extract = extractStyles(source);
  const report = buildReport(extract, { bundlePath, bridgeFunctions: bridge });

  let markdown = report.markdown;
  let aiOut: { ok: boolean; reason?: string; count?: number } = { ok: true, count: 0 };

  if (opts.ai) {
    const unmapped = report.classified.filter(c => c.status === 'unmapped');
    const ai = await generateAiSuggestions({ unmapped, bridgeFunctions: bridge });
    if (ai.ok) {
      aiOut = { ok: true, count: ai.suggestions.length };
      const aiPath = aiSidecarPath(opts.out, bundlePath);
      try {
        await writeFile(aiPath, JSON.stringify(ai.suggestions, null, 2) + '\n', 'utf8');
        process.stderr.write(`pulp-css-analyze: wrote ${ai.suggestions.length} AI suggestions to ${aiPath}\n`);
      } catch (err) {
        process.stderr.write(
          `pulp-css-analyze: failed to write AI sidecar ${aiPath}: ${(err as Error).message}\n`,
        );
      }
      markdown += renderAiSection(ai.suggestions);
    } else {
      aiOut = { ok: false, reason: ai.reason };
      process.stderr.write(`pulp-css-analyze: --ai skipped — ${ai.reason}\n`);
    }
  }

  if (opts.out) {
    const outPath = resolve(opts.out);
    await writeFile(outPath, markdown, 'utf8');
    process.stderr.write(`pulp-css-analyze: wrote ${outPath}\n`);
  } else {
    process.stdout.write(markdown);
  }

  // Exit code: 0 on success regardless of unmapped count — the report
  // is informational. A future --fail-on-unmapped flag could change this.
  void aiOut;
  return 0;
}

function aiSidecarPath(out: string | undefined, bundlePath: string): string {
  if (out) {
    const dir = dirname(resolve(out));
    const name = basename(out).replace(/\.md$/i, '');
    return resolve(dir, `${name || 'mapping-suggestions'}.suggestions.json`);
  }
  const dir = dirname(bundlePath);
  return resolve(dir, 'mapping-suggestions.json');
}

async function loadBridge(path: string | undefined): Promise<readonly string[]> {
  if (!path) return getDefaultBridgeFunctions();
  try {
    const txt = await readFile(resolve(path), 'utf8');
    const parsed: unknown = JSON.parse(txt);
    if (Array.isArray(parsed) && parsed.every(s => typeof s === 'string')) {
      return parsed as string[];
    }
    process.stderr.write(
      `pulp-css-analyze: --bridge-list ${path} is not a JSON array of strings; using bundled snapshot\n`,
    );
  } catch (err) {
    process.stderr.write(
      `pulp-css-analyze: failed to read --bridge-list ${path}: ${(err as Error).message}; using bundled snapshot\n`,
    );
  }
  return getDefaultBridgeFunctions();
}

function renderAiSection(suggestions: { cssProp: string; suggestion: string; detail: string }[]): string {
  if (suggestions.length === 0) return '';
  const lines: string[] = [];
  lines.push('');
  lines.push('## AI-augmented suggestions');
  lines.push('');
  lines.push('| Property | Verdict | Detail |');
  lines.push('|----------|---------|--------|');
  for (const s of suggestions) {
    lines.push(`| \`${s.cssProp}\` | ${s.suggestion} | ${s.detail.replace(/\|/g, '\\|')} |`);
  }
  lines.push('');
  return lines.join('\n');
}

// Direct-execution shim so `tsx src/cli.ts` and the built dist/cli.js
// both invoke main(). Compares the resolved path of the script entry
// (process.argv[1]) against the URL of this module — works under both
// tsx and tsup's ESM bundle.
const isDirect = (() => {
  try {
    const entry = process.argv[1];
    if (!entry) return false;
    const here = new URL(import.meta.url).pathname;
    return resolve(entry) === here;
  } catch {
    return false;
  }
})();

if (isDirect) {
  main(process.argv).then(
    code => process.exit(code),
    err => {
      process.stderr.write(`pulp-css-analyze: fatal: ${(err as Error).stack ?? err}\n`);
      process.exit(1);
    },
  );
}
