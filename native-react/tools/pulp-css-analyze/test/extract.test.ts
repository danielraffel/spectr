import { describe, expect, it } from 'vitest';
import { extractStyles } from '../src/extract.js';
import { lookupCssMapping } from '../src/known-css.js';
import { buildReport } from '../src/report.js';
import { parseSuggestions } from '../src/ai-suggest.js';

describe('extractStyles — JSX runtime', () => {
  it('finds a single jsx() style object', () => {
    const src = `
      import { jsx } from "react/jsx-runtime";
      jsx("div", { style: { color: "red", fontSize: 14 } });
    `;
    const r = extractStyles(src);
    expect(r.styleObjectCount).toBe(1);
    expect(r.values.get('color')).toEqual(new Set(['red']));
    expect(r.values.get('fontSize')).toEqual(new Set(['14']));
    expect(r.occurrences.get('color')).toBe(1);
    expect(r.occurrences.get('fontSize')).toBe(1);
  });

  it('handles jsxs() with multi-style children', () => {
    const src = `
      jsxs("div", { style: { background: "#000", borderRadius: 4 }, children: [
        jsx("span", { style: { color: "white" } }),
        jsx("span", { style: { color: "white" } })
      ] });
    `;
    const r = extractStyles(src);
    expect(r.styleObjectCount).toBe(3);
    expect(r.occurrences.get('color')).toBe(2);
    expect(r.values.get('background')).toEqual(new Set(['#000']));
    expect(r.values.get('borderRadius')).toEqual(new Set(['4']));
  });

  it('handles classic React.createElement', () => {
    const src = `
      React.createElement("div", { style: { display: "flex", padding: 8 } });
      createElement("span", { style: { display: "block" } });
    `;
    const r = extractStyles(src);
    expect(r.styleObjectCount).toBe(2);
    expect(r.values.get('display')).toEqual(new Set(['flex', 'block']));
  });

  it('skips style: variable references but still parses surrounding bundle', () => {
    const src = `
      const s = { color: "red" };
      jsx("div", { style: s });
      jsx("div", { style: { color: "blue" } });
    `;
    const r = extractStyles(src);
    expect(r.styleObjectCount).toBe(1);
    expect(r.values.get('color')).toEqual(new Set(['blue']));
  });

  it('records dynamic values with key but no value', () => {
    const src = `
      jsx("div", { style: { width: someExpr, height: 100 } });
    `;
    const r = extractStyles(src);
    expect(r.values.get('width')?.size ?? 0).toBe(0);
    expect(r.values.get('height')).toEqual(new Set(['100']));
    expect(r.dynamicSkippedCount).toBeGreaterThan(0);
    expect(r.occurrences.get('width')).toBe(1);
  });

  it('handles negative numeric literals', () => {
    const src = `jsx("div", { style: { marginTop: -8 } });`;
    const r = extractStyles(src);
    expect(r.values.get('marginTop')).toEqual(new Set(['-8']));
  });

  it('does not crash on malformed code (errorRecovery)', () => {
    const src = `jsx("div", { style: { color: "red", `;
    expect(() => extractStyles(src)).not.toThrow();
  });

  it('parses TypeScript annotations in the bundle', () => {
    const src = `
      const x: number = 1;
      jsx("div", { style: { color: "red" } });
    `;
    const r = extractStyles(src);
    expect(r.styleObjectCount).toBe(1);
  });

  it('captures static template literals', () => {
    const src = "jsx('div', { style: { background: `red` } });";
    const r = extractStyles(src);
    expect(r.values.get('background')).toEqual(new Set(['red']));
  });

  it('ignores non-jsx CallExpressions', () => {
    const src = `console.log({ style: { color: "red" } });`;
    const r = extractStyles(src);
    expect(r.styleObjectCount).toBe(0);
  });
});

describe('known-css — mapping classification', () => {
  it('classifies known mapped props', () => {
    expect(lookupCssMapping('color').status).toBe('mapped');
    expect(lookupCssMapping('background').status).toBe('mapped');
    expect(lookupCssMapping('zIndex').status).toBe('mapped');
  });

  it('classifies intentional drops', () => {
    expect(lookupCssMapping('cursor').status).toBe('intentional-drop');
    expect(lookupCssMapping('transition').status).toBe('intentional-drop');
  });

  it('classifies unknown props as unmapped', () => {
    expect(lookupCssMapping('boxShadow').status).toBe('unmapped');
    expect(lookupCssMapping('backdropFilter').status).toBe('unmapped');
  });

  it('accepts kebab-case input', () => {
    expect(lookupCssMapping('font-size').status).toBe('mapped');
    expect(lookupCssMapping('background-color').status).toBe('mapped');
  });
});

describe('buildReport', () => {
  it('produces a markdown report with summary and tables', () => {
    const src = `
      jsx("div", { style: { color: "red", boxShadow: "0 0 4px black" } });
      jsx("div", { style: { color: "blue", cursor: "pointer" } });
    `;
    const ex = extractStyles(src);
    const r = buildReport(ex, { bundlePath: '/tmp/test.js', bridgeFunctions: ['setBackground'] });
    expect(r.summary.totalUniqueProps).toBe(3);
    expect(r.summary.mapped).toBe(1); // color
    expect(r.summary.intentionalDrop).toBe(1); // cursor
    expect(r.summary.unmapped).toBe(1); // boxShadow
    expect(r.markdown).toContain('# Style coverage for /tmp/test.js');
    expect(r.markdown).toContain('boxShadow');
  });
});

describe('parseSuggestions', () => {
  it('parses a clean JSON array', () => {
    const raw =
      '[{"cssProp":"boxShadow","suggestion":"framework-gap","detail":"add setShadow"}]';
    const r = parseSuggestions(raw);
    expect(r).toHaveLength(1);
    expect(r[0]?.cssProp).toBe('boxShadow');
    expect(r[0]?.suggestion).toBe('framework-gap');
  });

  it('strips fenced code blocks', () => {
    const raw =
      '```json\n[{"cssProp":"x","suggestion":"mappable","detail":"y"}]\n```';
    const r = parseSuggestions(raw);
    expect(r).toHaveLength(1);
    expect(r[0]?.suggestion).toBe('mappable');
  });

  it('returns [] on garbage', () => {
    expect(parseSuggestions('not json')).toEqual([]);
  });

  it('falls back to substring extraction', () => {
    const raw = 'noise [{"cssProp":"a","suggestion":"lower-to","detail":"b"}] tail';
    const r = parseSuggestions(raw);
    expect(r).toHaveLength(1);
    expect(r[0]?.suggestion).toBe('lower-to');
  });

  it('coerces unknown suggestion kinds', () => {
    const raw = '[{"cssProp":"a","suggestion":"weird","detail":""}]';
    const r = parseSuggestions(raw);
    expect(r[0]?.suggestion).toBe('unknown');
  });
});
