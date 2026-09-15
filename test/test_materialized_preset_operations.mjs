// Preset-manager operations, asserted against the shipping document.
//
// Six rules, one per defect, each with its own plant. THE PLANT CONTRACT IS
// STRICTER HERE THAN ELSEWHERE IN THIS REPO, on purpose: `--expect-fail` alone
// only requires the suite to redden, which a plant tripping four rules at once
// satisfies while proving nothing about any of them. That exact hole was found
// twice in one day -- once where a label assertion failed first and the
// wire-value check behind it never ran. So a planted run must fail its OWN
// rule and NO OTHER, and the harness checks the failing set, not the count.
//
//   node test/test_materialized_preset_operations.mjs <document.runtime.json>
//   node ... <doc> --plant delete-confirm --expect-fail
//
// Exit codes: 0 pass (or, under --expect-fail, the plant reddened exactly its
// own rule), 1 a rule failed, 2 NO VERDICT -- the document is unreadable, a
// plant needle did not match exactly once, or a planted run failed the wrong
// rules. 2 is never a pass.

import fs from "fs";

const argv = process.argv.slice(2);
const docPath = argv.find((a) => !a.startsWith("--"));
const plantIndex = argv.indexOf("--plant");
const plant = plantIndex >= 0 ? argv[plantIndex + 1] : null;
const expectFail = argv.includes("--expect-fail");

if (!docPath) {
  console.error("FAIL: no document path given");
  process.exit(2);
}

let html;
try {
  const doc = JSON.parse(fs.readFileSync(docPath, "utf8"));
  html = doc.html;
} catch (e) {
  console.error(`FAIL: could not read ${docPath}: ${e.message}`);
  process.exit(2);
}
if (typeof html !== "string" || html.length < 100000) {
  console.error("FAIL: that file carries no editor page");
  process.exit(2);
}

// A control on the instrument itself. If the page does not contain the
// component every rule below is about, every "absent" finding is a statement
// about a document this harness cannot read -- not about the product.
for (const [what, needle] of [
  ["the preset manager", "function PatternManager({"],
  ["its detail pane", "function PatternDetail({"],
  ["React", "React.createElement"],
]) {
  if (!html.includes(needle)) {
    console.error(`FAIL: no verdict -- ${what} is not in this document`);
    process.exit(2);
  }
}

// Each plant restores EXACTLY the pre-fix shape of one rule, and nothing else.
const PLANTS = {
  "delete-confirm": [
    "onDelete: () => setPendingDelete(selected),",
    'onDelete: () => {\n        if (confirm(`Delete "${selected.name}"?`)) '
      + "del(selected.id);\n      },",
  ],
  "empty-state": [
    '"data-spectr-user-empty": userPatterns.length === 0 ? "library" : '
      + '"no-match", ',
    "",
  ],
  "export-file-sink": [
    "} else if (!canDownloadFile()) {\n"
      + '      await exportToClipboard(json, "EXPORTED TO CLIPBOARD");\n'
      + "    } else {",
    "} else {",
  ],
  "duplicate-library": [
    "if (onDuplicatePattern) {\n      onDuplicatePattern(id);\n      return;\n"
      + "    }\n",
    "",
  ],
  "set-default-library": [
    "if (onSetDefaultPattern) {\n          onSetDefaultPattern(selected.id);\n"
      + "          return;\n        }\n",
    "",
  ],
  "search-value": [
    'onChange: (e) => setQuery(String(spectrInputValue(e) ?? "")),',
    "onChange: (e) => setQuery(e.target.value),",
  ],
};

if (plant) {
  const rule = PLANTS[plant];
  if (!rule) {
    console.error(`FAIL: unknown --plant ${plant}`);
    process.exit(2);
  }
  const hits = html.split(rule[0]).length - 1;
  if (hits !== 1) {
    console.error(`FAIL: plant needle '${plant}' matched ${hits} times, `
      + "expected 1 -- the control would not test what it claims");
    process.exit(2);
  }
  html = html.replace(rule[0], rule[1]);
  console.log(`planted ${plant}`);
}

const failed = [];
const check = (rule, ok, why) => {
  if (ok) {
    console.log(`  ok    ${rule}`);
    return;
  }
  failed.push(rule);
  console.log(`  FAIL  ${rule}: ${why}`);
};

// 1. DELETE must not depend on a global this runtime does not define.
//    `confirm` is a browser dialog; the Pulp scripted-UI runtime defines no
//    such global, so the handler threw before deleting anything -- on every
//    press, by every route, since the action shipped.
check(
  "delete-confirm",
  !html.includes("confirm(`Delete")
    && html.includes("onDelete: () => setPendingDelete(selected),")
    && html.includes('"data-spectr-delete-dialog"'),
  "the delete handler must open the in-panel confirmation, not call the "
    + "browser `confirm` this runtime does not define",
);

// 2. An empty USER library and a query matching none of a full one are the
//    same length and are not the same fact.
check(
  "empty-state",
  html.includes('"data-spectr-user-empty": userPatterns.length === 0 ? '
    + '"library" : "no-match"')
    && html.includes("no user preset matches"),
  "the USER empty state must say which of the two states it is reporting",
);

// 3. Blob, URL and document.createElement all exist in this runtime, so the
//    browser export body runs clean and announces EXPORTED while delivering
//    nowhere. It must check for a file sink first.
check(
  "export-file-sink",
  html.includes("const canDownloadFile = ()")
    && html.includes("typeof FileReader === \"function\"")
    && (html.match(/if \(!canDownloadFile\(\)\)/g) || []).length === 2,
  "both EXPORT (FILE) and EXPORT ALL (FILE) must check for a file sink "
    + "before claiming a file was written",
);

// 4-5. Both of these wrote React state alone, which the next pattern
//      command's response replaces wholesale from a library they never
//      reached. Invisible to a row count.
check(
  "duplicate-library",
  html.includes("onDuplicatePattern(id);")
    && html.includes('"duplicate_pattern"'),
  "DUPLICATE must send the copy to the pattern library",
);
check(
  "set-default-library",
  html.includes("onSetDefaultPattern(selected.id);")
    && html.includes('"set_default_pattern"'),
  "SET AS DEFAULT must send the default to the pattern library",
);

// 6. The search field was the only input in this panel reading its value
//    unguarded; its three siblings all go through spectrInputValue. A value
//    that does not survive the raw read makes `query` undefined, and the next
//    line of the render is `query.toLowerCase()` inside `userPatterns.filter`.
check(
  "search-value",
  !html.includes("onChange: (e) => setQuery(e.target.value)")
    && html.includes('setQuery(String(spectrInputValue(e) ?? ""))'),
  "the search field must read its value the same guarded way its siblings do",
);

// Two known-good details this change must not undo. Both were bought by an
// earlier round and neither is otherwise guarded from THIS file's edits.
check(
  "no-row-chip",
  !html.includes('} }, isFactory ? "F" : "U")')
    && html.includes("No trailing chip."),
  "the preset row must not regrow the F / U chip that read as a shortcut",
);
check(
  "detail-name-not-truncated",
  html.includes('"data-spectr-manager-title"')
    && html.includes("textOverflow: \"ellipsis\", flexShrink: 0, maxWidth: 190"),
  "the detail heading must keep the width that stopped the FACTORY badge "
    + "overlapping the name",
);

const passed = failed.length === 0;
console.log(`\n${passed ? "PASS" : "FAIL"}: ${failed.length} rule(s) failed`
  + (plant ? ` (plant=${plant})` : "")
  + (expectFail ? " [--expect-fail]" : ""));

if (!expectFail) process.exit(passed ? 0 : 1);

// Inverted. A plant must redden its OWN rule and no other: a plant that trips
// several rules proves nothing about any of them, and a bare "it failed" would
// accept exactly that.
if (passed) {
  console.error(`FAIL: the planted '${plant}' regression did NOT redden any `
    + "rule -- that rule can no longer fail, so its clean run proves nothing");
  process.exit(1);
}
if (failed.length !== 1 || failed[0] !== plant) {
  console.error(`FAIL: planting '${plant}' reddened [${failed.join(", ")}] `
    + "-- a plant must fail its own rule and no other, or neither rule is "
    + "shown to be load bearing");
  process.exit(2);
}
console.log(`OK: planting '${plant}' reddened exactly '${plant}'`);
process.exit(0);
