// resources/editor.html patches the Claude Design source with JavaScript held
// in String.raw template literals. A backtick written inside one of those
// patches -- in a comment as much as in code -- ends the template early. The
// rest of the patch is then parsed as outer script, the adapter no longer
// compiles, React never mounts, and every browser oracle that loads the mirror
// fails with an unrelated-looking timeout.
//
// This check needs no browser. It proves two things about the file:
//   1. every String.raw template closes on a token that can follow a template
//      literal in an argument list or expression (, ; ) ] } . +), which is
//      exactly what an early close breaks, and it names the offending line;
//   2. the first <script> block (the adapter) compiles.
// Both detectors are run against a planted copy carrying the backtick comment
// that once shipped, and the run fails unless both reject it.
import fs from 'node:fs';
import vm from 'node:vm';

const htmlPath = process.argv[2];
if (!htmlPath) {
  console.error('usage: test_editor_html_raw_templates.mjs <resources/editor.html>');
  process.exit(2);
}
const html = fs.readFileSync(htmlPath, 'utf8');

const OPEN = 'String.raw`';
const FOLLOWERS = new Set([',', ';', ')', ']', '}', '.', '+']);

function lineOf(source, index) {
  let line = 1;
  for (let i = 0; i < index; i += 1) if (source.charCodeAt(i) === 10) line += 1;
  return line;
}

function scanRawTemplates(source) {
  const problems = [];
  let count = 0;
  let from = 0;
  for (;;) {
    const start = source.indexOf(OPEN, from);
    if (start < 0) break;
    count += 1;
    let k = start + OPEN.length;
    let close = -1;
    for (; k < source.length; k += 1) {
      const ch = source[k];
      if (ch === '\\') { k += 1; continue; }
      if (ch === '$' && source[k + 1] === '{') {
        // A deliberate interpolation (the fixed-design stylesheet uses them):
        // skip its expression so a brace or quote inside cannot confuse the scan.
        let depth = 0;
        for (k += 1; k < source.length; k += 1) {
          if (source[k] === '{') depth += 1;
          else if (source[k] === '}' && (depth -= 1) === 0) break;
        }
        continue;
      }
      if (ch === '`') { close = k; break; }
    }
    if (close < 0) {
      problems.push(`line ${lineOf(source, start)}: String.raw template never closes`);
      break;
    }
    let next = close + 1;
    while (next < source.length && /\s/.test(source[next])) next += 1;
    if (!FOLLOWERS.has(source[next])) {
      problems.push(`line ${lineOf(source, close)}: a backtick ends the String.raw template ` +
        `opened at line ${lineOf(source, start)} early (followed by ` +
        `${JSON.stringify(source.slice(next, next + 24))}); write the text without backticks`);
    }
    from = close + 1;
  }
  return { count, problems };
}

function firstScript(source) {
  const open = '<script>';
  const begin = source.indexOf(open);
  if (begin < 0) return null;
  const end = source.indexOf('</script>', begin + open.length);
  if (end < 0) return null;
  return source.slice(begin + open.length, end);
}

function adapterCompileError(source) {
  const adapter = firstScript(source);
  if (adapter === null) return 'no first <script> block';
  try {
    // Compiles without running: proves the adapter parses as a script.
    new vm.Script(adapter, { filename: 'editor.html#adapter' });
    return null;
  } catch (error) {
    return String(error && error.message || error);
  }
}

function check(source) {
  const scan = scanRawTemplates(source);
  return { ...scan, compileError: adapterCompileError(source) };
}

let failed = false;
const real = check(html);
if (real.count === 0) {
  console.error('FAIL: no String.raw templates found; the scan is pointed at the wrong file');
  failed = true;
}
for (const problem of real.problems) {
  console.error(`FAIL: ${problem}`);
  failed = true;
}
if (real.compileError) {
  console.error(`FAIL: the editor.html adapter does not compile: ${real.compileError}`);
  failed = true;
}

// Negative control: plant the comment that once shipped inside the first
// template. Both detectors must reject it or this check proves nothing.
const firstOpen = html.indexOf(OPEN);
const firstNewline = html.indexOf('\n', firstOpen);
const planted = html.slice(0, firstNewline + 1) +
  '  // Normal builds report `available: false` and keep the layout.\n' +
  html.slice(firstNewline + 1);
const control = check(planted);
if (control.problems.length === 0) {
  console.error('FAIL: negative control -- the template scan accepted a planted backtick');
  failed = true;
}
if (!control.compileError) {
  console.error('FAIL: negative control -- the adapter compile accepted a planted backtick');
  failed = true;
}

if (failed) process.exit(1);
console.log(`PASS: ${real.count} String.raw templates close cleanly and the adapter compiles; ` +
  'the planted backtick is rejected by both detectors');
