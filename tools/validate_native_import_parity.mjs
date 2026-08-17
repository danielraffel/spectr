#!/usr/bin/env node

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, isAbsolute, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(scriptDir, '..');
const manifestPath = join(repoRoot, 'native-ui', 'capture-states', 'manifest.json');

function fail(message) {
  console.error(`spectr-native-parity: ${message}`);
  process.exit(1);
}

function parseArgs(argv) {
  const result = { states: [] };
  for (let i = 0; i < argv.length; ++i) {
    const value = argv[i];
    if (value === '--importer') result.importer = argv[++i];
    else if (value === '--browser') result.browser = argv[++i];
    else if (value === '--output') result.output = argv[++i];
    else if (value === '--state') result.states.push(argv[++i]);
    else if (value === '--help') result.help = true;
    else fail(`unknown argument ${value}`);
  }
  return result;
}

const options = parseArgs(process.argv.slice(2));
if (options.help) {
  console.log('Usage: validate_native_import_parity.mjs --importer <pulp-cpp> --browser <Chrome> --output <empty-dir> [--state <id>]');
  process.exit(0);
}
if (!options.importer || !options.browser || !options.output) {
  fail('--importer, --browser, and --output are required');
}

const importer = resolve(options.importer);
const browser = resolve(options.browser);
const output = resolve(options.output);
if (!existsSync(importer)) fail(`importer does not exist: ${importer}`);
if (!existsSync(browser)) fail(`browser does not exist: ${browser}`);
if (existsSync(output) && readdirSync(output).length !== 0) {
  fail(`output directory must be empty: ${output}`);
}
mkdirSync(output, { recursive: true });

const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
if (manifest.schema !== 'spectr-native-parity-matrix-v1' || manifest.version !== 1) {
  fail(`unsupported manifest: ${manifestPath}`);
}
const requested = new Set(options.states);
const states = manifest.states.filter((state) => requested.size === 0 || requested.has(state.id));
if (requested.size !== 0 && states.length !== requested.size) {
  const found = new Set(states.map((state) => state.id));
  fail(`unknown state(s): ${[...requested].filter((id) => !found.has(id)).join(', ')}`);
}

const source = resolve(dirname(manifestPath), manifest.source);
const report = {
  schema: 'spectr-native-parity-report-v1',
  version: 1,
  source,
  importer,
  browser,
  states: []
};

for (const state of states) {
  const stateDir = join(output, state.id);
  mkdirSync(stateDir, { recursive: true });
  const artifact = join(stateDir, `${state.id}.ir.json`);
  const args = [
    'import-design', '--from', 'claude', '--file', source,
    '--mode', 'baked', '--emit', 'ir-json', '--output', artifact,
    '--browser', browser, '--materialized-canvas-composition',
    '--validate', '--screenshot-backend', 'skia',
    '--no-bridge-scaffold'
  ];
  if (state.plan) {
    args.push('--browser-interactions', resolve(dirname(manifestPath), state.plan));
  }

  process.stdout.write(`spectr-native-parity: ${state.id} ... `);
  const run = spawnSync(importer, args, {
    cwd: stateDir,
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024
  });
  const log = `${run.stdout ?? ''}${run.stderr ?? ''}`;
  writeFileSync(join(stateDir, 'import.log'), log);

  // The renderer also reports a premultiplied-alpha mean. Zero differing
  // composited pixels is the exact visible contract; the alpha diagnostic can
  // remain non-zero for fully transparent pixels without changing the frame.
  const exact = /Similarity:\s*100%\s*\(0\/\d+ pixels differ, mean error:\s*[0-9.]+\)/.test(log);
  const validated = /Validation:\s*PASS/.test(log);
  const captureDir = join(stateDir, `${state.id}.ir-browser-capture`);
  let interactionsComplete = true;
  let interactionCount = 0;
  if (state.plan) {
    const interactionPath = join(captureDir, 'interaction-report.json');
    if (!existsSync(interactionPath)) {
      interactionsComplete = false;
    } else {
      const interaction = JSON.parse(readFileSync(interactionPath, 'utf8'));
      const actions = interaction.actions ?? [];
      interactionCount = actions.length;
      interactionsComplete = actions.length > 0 && actions.every((action) => action.status === 'completed');
    }
  }

  const pass = run.status === 0 && exact && validated && interactionsComplete;
  report.states.push({
    id: state.id,
    surface: state.surface,
    behavior: state.behavior,
    pass,
    exit_code: run.status,
    exact_pixels: exact,
    validation_pass: validated,
    interactions_complete: interactionsComplete,
    interaction_count: interactionCount,
    artifact,
    log: join(stateDir, 'import.log')
  });
  console.log(pass ? 'PASS' : 'FAIL');
  if (!pass) {
    writeFileSync(join(output, 'report.json'), `${JSON.stringify(report, null, 2)}\n`);
    fail(`${state.id} failed; see ${join(stateDir, 'import.log')}`);
  }
}

report.pass = report.states.every((state) => state.pass);
writeFileSync(join(output, 'report.json'), `${JSON.stringify(report, null, 2)}\n`);
console.log(`spectr-native-parity: ${report.states.length}/${report.states.length} exact states; report ${join(output, 'report.json')}`);
