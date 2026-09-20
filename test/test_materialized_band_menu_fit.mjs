#!/usr/bin/env node
// THE BAND CONTEXT MENU POSITIONS FROM ITS MEASURED HEIGHT, NOT A GUESS.
//
// NECESSARY, NOT SUFFICIENT -- AND THAT IS NOT HYPOTHETICAL. An earlier
// version of this file passed every case while the real standalone FAILED
// `Spectr-standalone-band-menu-rows`: a `maxHeight` on the panel made Yoga
// shrink the rows (29px pitch to 20.7px), the squash put a header element
// over the first row's centre, and pressing "Mute / Unmute" where its own
// words paint hit nothing and never toggled the mute. Nothing modelled here
// can see that: this file renders the component with a hook shim, so it knows
// what style the component ASKS for and nothing about what Yoga, paint or
// hit-testing then do with it. The gate that can see it drives the shipping
// binary. Keep both.
//
// The menu is a `position: fixed` panel. It used to carry no `maxHeight` and
// no `overflowY`, and it positioned itself from a hardcoded height estimate:
//
//     const H = 420 + (hasBand ? 100 : 0) + (hasSel ? 160 : 0)
//                   + assigned.length * 26;
//     const top = Math.max(8, Math.min(y, vh - H - 8));
//
// Measured against the three shapes it ships, from recorded standalone runs,
// that estimate is wrong by +25 / -15 / -27 px -- and wrong in the DANGEROUS
// direction for both selection cases, understating the height so the clamp
// believed the panel fit when it did not. With nothing capping the panel, a
// menu taller than the viewport is pinned to the top edge by `Math.max(8, …)`
// and keeps its full content height, so its trailing rows are laid out,
// hit-tested and painted outside it, over the app behind.
//
// WHAT THIS SUITE REFUSES TO DO. It does not assert that `maxHeight` is
// present in the document. A presence check passes on a cap wired to the
// wrong number and on a clamp that still uses the guess. Every check below
// RENDERS the component -- the shipping one, extracted from the shipping
// document -- and reads the geometry back out of the element it returns.
//
// THE TWO PLANTS. Each reverses one half of the fix in the source before
// evaluating it, and the run is green only when the matching assertion goes
// RED. A plant that fails to apply aborts rather than measuring a healthy
// component, because that is the one outcome a negative control must never
// produce.
//
// Needs no binary and no Chrome: the component is evaluated in a vm with a
// hook runtime, so this holds even while the build host has no free capacity.
//
// Exit: 0 every case holds, 1 one did not.

import { readFileSync } from "node:fs";
import vm from "node:vm";
import path from "node:path";
import { fileURLToPath } from "node:url";

const REPO = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const DOC = path.join(REPO, "native-ui", "materialized",
                      "materialized-document.runtime.json");

// ── extract the shipping component ───────────────────────────────────────
const ANCHOR = "function ContextMenu({ x, y, band, N, selection";

function menuSource() {
  const doc = JSON.parse(readFileSync(DOC, "utf8"));
  const blocks = [...doc.html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)]
    .map((m) => m[1]);
  const hits = blocks.filter((b) => b.includes(ANCHOR));
  if (hits.length !== 1)
    throw new Error(`the band menu appears in ${hits.length} script blocks, expected 1`);
  const src = hits[0];
  const i = src.indexOf(ANCHOR);
  let b = src.indexOf("{", src.indexOf(")", i));
  let depth = 0, j = b;
  for (; j < src.length; j += 1) {
    if (src[j] === "{") depth += 1;
    else if (src[j] === "}") { depth -= 1; if (!depth) break; }
  }
  return src.slice(i, j + 1);
}

// A plant reverses one half of the fix. It must match exactly once.
function plant(source, from, to, what) {
  const n = source.split(from).length - 1;
  if (n !== 1)
    throw new Error(`plant "${what}": anchor occurs ${n} times, expected 1 — `
                    + "the control would measure a healthy component");
  return source.replace(from, to);
}

const PLANT_GUESSED_H = (s) => plant(
  s,
  "const H = measuredH === null ? estimatedH : measuredH;",
  "const H = estimatedH;",
  "guessed height: the clamp positions from the estimate (pre-fix)");

// ── render it ────────────────────────────────────────────────────────────
function render(source, { vh, vw = 1320, x = 378, y = 400,
                          hasBand = true, selSize = 0, macrosAssigned = 0,
                          measuredPx = null }) {
  const hooks = { slots: [], index: 0, layout: [] };
  let dirty = false;
  const React = {
    createElement: (type, props, ...children) => ({ type, props, children }),
    Fragment: "fragment",
    useState(initial) {
      const i = hooks.index++;
      if (hooks.slots.length <= i)
        hooks.slots[i] = typeof initial === "function" ? initial() : initial;
      const set = (next) => {
        const resolved = typeof next === "function" ? next(hooks.slots[i]) : next;
        if (Object.is(resolved, hooks.slots[i])) return;
        hooks.slots[i] = resolved;
        dirty = true;
      };
      return [hooks.slots[i], set];
    },
    useRef(initial) {
      const i = hooks.index++;
      if (hooks.slots.length <= i) hooks.slots[i] = { current: initial };
      return hooks.slots[i];
    },
    useEffect() {},
    useLayoutEffect(fn) { hooks.layout.push(fn); },
  };

  // The root the component measures the viewport from.
  const root = { clientWidth: vw, clientHeight: vh };
  const sandbox = {
    React,
    // The shipping component reads its modulation state from a host-provided
    // hook. The sandbox stands in for the app environment, so it has to supply
    // it: without the stub the component throws at module scope and the test
    // fails for a missing global rather than for anything it asserts.
    window: {
      innerWidth: vw,
      innerHeight: vh,
      spectrDismissBandMenu: null,
      // Every field the component actually reads. A stub that omits one
      // does not throw -- it yields `undefined` and the panel renders a
      // shape no user can get -- so the list is exhaustive on purpose.
      useSpectrModulationState: () => ({
        value: {
          enabled: false, shape: 0, rate: 0, depth: 0,
          lfo2Enabled: false, lfo2Shape: 0, lfo2Rate: 0, lfo2Depth: 0,
          targetMask: 0,
        },
        ready: true,
        publish() {},
      }),
    },
    document: { getElementById: (id) => (id === "root" ? root : null) },
    clamp: (v, lo, hi) => Math.max(lo, Math.min(hi, v)),
    spectrShortcutChipStyle: () => ({}),
    Number, Math, Object, Array, String, Boolean, JSON,
  };
  const context = vm.createContext(sandbox);
  vm.runInContext(`${source}; globalThis.__ContextMenu = ContextMenu;`,
                  context, { filename: "spectr-band-context-menu.js" });
  const Menu = context.__ContextMenu;

  const macros = [0, 1, 2, 3].map((i) =>
    ({ slots: i < macrosAssigned ? [1] : [] }));
  const selection = { size: selSize };
  const props = {
    x, y, band: hasBand ? 9 : -1, N: 32, selection, editMode: "sculpt",
    onClose() {}, onEditMode() {}, onMuteBand() {}, onZeroBand() {},
    onSoloBand() {}, onSelectAll() {}, onSelectNone() {}, onZeroSel() {},
    onMuteSel() {}, onFitView() {}, canUndo: true, canRedo: false,
    macros, onUndo() {}, onRedo() {}, onMacroMembers() {},
  };

  // Render, run layout effects, re-render while they changed state — which is
  // how the measured height reaches the position on the second pass.
  let el = null;
  for (let pass = 0; pass < 4; pass += 1) {
    hooks.index = 0;
    hooks.layout = [];
    dirty = false;
    el = Menu(props);
    // Give the container the box a real layout would, so the MEASURED path is
    // exercised rather than only the estimate.
    //
    // Reach the ref through the element the component attached it to, never
    // through a hook slot index. This used to poke `hooks.slots[0]`, which
    // stopped being the container's ref the moment a `useState` was added
    // above it -- three were -- and the guard `slots[0] &&` then saw
    // `modulationOpen === false` and skipped the injection ENTIRELY. Every
    // clamp case still printed a confident number; they were all the estimate.
    if (measuredPx !== null) {
      const container = menuContainer(el);
      const box = container && container.props && container.props.ref;
      if (!box || typeof box !== "object")
        throw new Error("the container's ref was not reachable, so the "
                        + "measured height would never be applied and every "
                        + "clamp case would silently measure the estimate");
      box.current = { offsetHeight: measuredPx, clientHeight: measuredPx };
    }
    hooks.layout.forEach((fn) => fn());
    if (!dirty) break;
  }
  // The component used to return the menu container itself, so `el.props.style`
  // WAS the style under test. Since the submenus became SIBLINGS of that
  // container -- which is what moved them into viewport space instead of their
  // trigger's -- it returns a Fragment wrapping the container and those panels,
  // and the root no longer has a style at all.
  const found = menuContainer(el);
  const style = found && found.props ? found.props.style : undefined;
  if (style === undefined)
    throw new Error("the band menu container was not found in the render; "
                    + "every assertion below would have read `undefined` and "
                    + "passed vacuously");
  return style;
}

// Key on the container's OWN marker. Two nearby choices are both wrong and
// both fail quietly:
//
//   * "the first element with `position: fixed`" -- the submenu panels are
//     fixed too, so this measures a submenu instead of the menu; and
//   * `data-spectr-main-menu-panel` -- that is the inner flex column, which
//     carries no positioned style, so `top`/`maxHeight` read `undefined` and
//     `maxHeight === undefined` PASSES for the wrong reason.
//
// Only the container carries the geometry this file asserts on.
function menuContainer(node) {
  if (!node || typeof node !== "object") return undefined;
  const props = node.props || {};
  if (props["data-spectr-band-context-menu"] !== undefined) return node;
  for (const child of (node.children || []).flat(Infinity)) {
    const found = menuContainer(child);
    if (found !== undefined) return found;
  }
  return undefined;
}

// ── cases ────────────────────────────────────────────────────────────────
const SHAPES = [
  ["no selection, band", { hasBand: true, selSize: 0, macrosAssigned: 0 }, 495],
  ["selection + band", { hasBand: true, selSize: 5, macrosAssigned: 0 }, 695],
  ["selection + 4 macros", { hasBand: true, selSize: 5, macrosAssigned: 4 }, 811],
];

let failures = [];
function check(what, ok, reading) {
  console.log(`  ${what.padEnd(52)} ${ok ? "PASS" : "FAIL"}  ${reading}`);
  if (!ok) failures.push(what);
}

// `vh` is 575 in the standalone, derived from the clamp across six recorded
// (H, top) pairs. 860 is the design root height. The panel must fit EITHER,
// because which one is correct is a separate open question.
const VIEWPORTS = [575, 860];

const shipped = menuSource();

// Where the clamp is allowed to put the panel's top.
//
// The menu reserves the bottom rail: `menuBottom = max(24, vh - 64)`, so the
// panel is clamped to sit ABOVE the transport row rather than across it. This
// expectation used to be `vh - H - 8`, which was right before the rail was
// reserved and is why every clamp case here read 64px low. It is derived
// independently of the component -- and the plant at the bottom of this file
// is what proves these assertions can still fail.
const RAIL = 64;
function expectedTop(vh, h) {
  return Math.max(8, Math.min(400, Math.max(24, vh - RAIL) - h - 8));
}

console.log("== NO CAP: the panel must not ask to be capped ==");
// Measured on the shipping standalone: Pulp does not scroll an overflow
// container (`overflow: scroll` is treated like `hidden`, pulp
// view.hpp:1655; a wheel over the panel moves nothing), so `maxHeight` does
// not scroll the rows -- it shrinks them, and the squash breaks the first
// row's hit target. Capping this panel needs scroll support in core Pulp.
for (const vh of VIEWPORTS) {
  const s = render(shipped, { vh, ...SHAPES[2][1], measuredPx: 811 });
  check(`vh=${vh} no maxHeight is declared`, s.maxHeight === undefined,
        `maxHeight=${s.maxHeight}`);
  check(`vh=${vh} no overflowY is declared`, s.overflowY === undefined,
        `overflowY=${JSON.stringify(s.overflowY)}`);
}

console.log("\n== CLAMP: it positions from the measured height ==");
for (const vh of VIEWPORTS) {
  for (const [name, shape, measured] of SHAPES) {
    const s = render(shipped, { vh, ...shape, measuredPx: measured });
    const want = expectedTop(vh, measured);
    check(`vh=${vh} ${name}`, s.top === want,
          `top=${s.top} expected=${want} (measured H=${measured})`);
  }
}

console.log("\n== MEASURED: the real height beats the estimate ==");
{
  // The estimate for this shape is 784; the real panel measures 811. The
  // viewport has to be chosen so the HEIGHT actually drives `top`: with the
  // anchor at y=400, `top` only follows the height while
  // (vh - 64) - H - 8 < 400, i.e. vh < 1283. At 4000 both heights clamp to
  // the anchor and the case cannot fail — it was written that way first,
  // and said so.
  const big = 1000;
  const est = render(shipped, { vh: big, ...SHAPES[2][1], measuredPx: null });
  const mes = render(shipped, { vh: big, ...SHAPES[2][1], measuredPx: 811 });
  check("an unmeasured first frame falls back to the estimate",
        est.top === expectedTop(big, 784),
        `top=${est.top} (estimate 784)`);
  check("a measured panel positions from 811, not 784",
        mes.top === expectedTop(big, 811)
        && mes.top !== est.top,
        `estimate top=${est.top} measured top=${mes.top}`);
}

console.log("\n== NEGATIVE CONTROL: the guessed height must break the clamp ==");
{
  // Compare two real RENDERS, never a render against a hand-computed number.
  // This used to recompute `honest` from a formula of its own, which drifted
  // the moment the clamp reserved the rail: it was asserting 144 !== 181 when
  // the honest render was actually 117, so the control would have passed even
  // if the plant had done nothing at all. A control whose baseline can be
  // wrong is not a control.
  const vh = 1000;
  const planted = render(PLANT_GUESSED_H(shipped),
                         { vh, ...SHAPES[2][1], measuredPx: 811 });
  const honest = render(shipped, { vh, ...SHAPES[2][1], measuredPx: 811 });
  check("PLANT guessed-H: the clamp positions from 784, not 811",
        planted.top !== honest.top,
        `planted top=${planted.top} honest top=${honest.top} — `
        + `off by ${Math.abs(planted.top - honest.top)}px`);
}

console.log();
if (failures.length) {
  console.log(`${failures.length} failure(s): ${failures.join("; ")}`);
  process.exit(1);
}
console.log("all cases hold");
