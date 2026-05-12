// Markdown report generator. Pure: no I/O, no globals.

import { sampleValues, type ExtractResult } from './extract.js';
import { lookupCssMapping, type MappingStatus } from './known-css.js';

export interface ReportOptions {
  bundlePath: string;
  bridgeFunctions: readonly string[];
}

export interface ReportSummary {
  totalUniqueProps: number;
  mapped: number;
  intentionalDrop: number;
  unmapped: number;
}

export interface ClassifiedProp {
  prop: string;
  status: MappingStatus;
  occurrences: number;
  values: Set<string>;
  bridgeSetters?: string[];
  note?: string;
}

export interface ReportResult {
  summary: ReportSummary;
  classified: ClassifiedProp[];
  markdown: string;
}

export function buildReport(extract: ExtractResult, opts: ReportOptions): ReportResult {
  const classified: ClassifiedProp[] = [];
  for (const [prop, valSet] of extract.values.entries()) {
    const mapping = lookupCssMapping(prop);
    classified.push({
      prop,
      status: mapping.status,
      occurrences: extract.occurrences.get(prop) ?? 0,
      values: valSet,
      bridgeSetters: mapping.bridgeSetters,
      note: mapping.note,
    });
  }
  classified.sort((a, b) => b.occurrences - a.occurrences || a.prop.localeCompare(b.prop));

  const summary: ReportSummary = {
    totalUniqueProps: classified.length,
    mapped: classified.filter(c => c.status === 'mapped').length,
    intentionalDrop: classified.filter(c => c.status === 'intentional-drop').length,
    unmapped: classified.filter(c => c.status === 'unmapped').length,
  };

  const markdown = renderMarkdown(extract, classified, summary, opts);
  return { summary, classified, markdown };
}

function pct(n: number, total: number): string {
  if (total === 0) return '0%';
  return `${Math.round((n / total) * 100)}%`;
}

function renderMarkdown(
  extract: ExtractResult,
  classified: ClassifiedProp[],
  summary: ReportSummary,
  opts: ReportOptions,
): string {
  const lines: string[] = [];
  lines.push(`# Style coverage for ${opts.bundlePath}`);
  lines.push('');
  lines.push('## Summary');
  lines.push('');
  lines.push(`- Style objects parsed: ${extract.styleObjectCount}`);
  lines.push(`- Dynamic style props skipped (non-literal values): ${extract.dynamicSkippedCount}`);
  lines.push(`- Unique CSS properties used: ${summary.totalUniqueProps}`);
  lines.push(`- Mapped to bridge: ${summary.mapped} (${pct(summary.mapped, summary.totalUniqueProps)})`);
  lines.push(`- Intentionally dropped: ${summary.intentionalDrop} (${pct(summary.intentionalDrop, summary.totalUniqueProps)})`);
  lines.push(`- Unmapped (silently dropped): ${summary.unmapped} (${pct(summary.unmapped, summary.totalUniqueProps)})`);
  lines.push(`- Bridge surface size (setX + createX): ${opts.bridgeFunctions.length}`);
  lines.push('');

  // Top unmapped props
  const unmapped = classified.filter(c => c.status === 'unmapped');
  lines.push('## Top unmapped properties (by occurrence)');
  lines.push('');
  if (unmapped.length === 0) {
    lines.push('_No unmapped properties detected — every CSS prop in the bundle has a known status._');
    lines.push('');
  } else {
    lines.push('| Property | Occurrences | Sample values |');
    lines.push('|----------|-------------|----------------|');
    for (const c of unmapped.slice(0, 25)) {
      lines.push(`| \`${c.prop}\` | ${c.occurrences} | ${escapePipes(sampleValues(c.values))} |`);
    }
    lines.push('');
  }

  // Mapped (just for visibility, top-N)
  const mapped = classified.filter(c => c.status === 'mapped');
  lines.push('## Mapped properties');
  lines.push('');
  if (mapped.length === 0) {
    lines.push('_No mapped properties found._');
    lines.push('');
  } else {
    lines.push('| Property | Occurrences | Bridge setter(s) |');
    lines.push('|----------|-------------|-------------------|');
    for (const c of mapped.slice(0, 40)) {
      lines.push(`| \`${c.prop}\` | ${c.occurrences} | ${(c.bridgeSetters ?? []).map(s => `\`${s}\``).join(', ') || '—'} |`);
    }
    lines.push('');
  }

  // Intentional drops
  const drops = classified.filter(c => c.status === 'intentional-drop');
  lines.push('## Intentionally dropped properties');
  lines.push('');
  if (drops.length === 0) {
    lines.push('_None — bundle does not exercise the intentional-drop set._');
    lines.push('');
  } else {
    lines.push('| Property | Occurrences | Reason |');
    lines.push('|----------|-------------|--------|');
    for (const c of drops) {
      lines.push(`| \`${c.prop}\` | ${c.occurrences} | ${escapePipes(c.note ?? '—')} |`);
    }
    lines.push('');
  }

  // Heuristic suggestions for unmapped props (deterministic; AI mode adds detail)
  lines.push('## Suggested fixes');
  lines.push('');
  if (unmapped.length === 0) {
    lines.push('_No suggested fixes — every observed prop is either mapped or intentionally dropped._');
    lines.push('');
  } else {
    for (const c of unmapped.slice(0, 20)) {
      const guess = suggestFix(c.prop);
      lines.push(`- **\`${c.prop}\`** (${c.occurrences}×) — ${guess}`);
    }
    lines.push('');
  }

  // Footer
  lines.push('---');
  lines.push('');
  lines.push('_Generated by `pulp-css-analyze`. Run with `--ai` to enrich the suggestions section using the Anthropic API (requires `ANTHROPIC_API_KEY`)._');
  lines.push('');
  return lines.join('\n');
}

function escapePipes(s: string): string {
  return s.replace(/\|/g, '\\|');
}

/**
 * Deterministic heuristic suggestion for a single unmapped CSS prop.
 * Used in the non-AI report; the --ai flow replaces these with model-
 * generated `mapping-suggestions.json` entries.
 */
export function suggestFix(prop: string): string {
  const p = prop.toLowerCase();
  if (p.startsWith('webkit') || p.startsWith('moz')) {
    return 'Vendor-prefixed property — strip in adapter (intentional drop).';
  }
  if (p.includes('shadow')) return 'Pulp framework gap — file an issue: bridge has no shadow primitive yet.';
  if (p.includes('filter')) return 'Pulp framework gap — could be lowered to a Skia ImageFilter post-effect.';
  if (p.includes('mask')) return 'Pulp framework gap — bridge has no mask primitive; lower to clip + alpha.';
  if (p.includes('grid')) return 'Adapter gap — could be lowered onto the Yoga flex engine via a polyfill.';
  if (p.includes('transform')) return 'Pulp framework gap — needs setTransform / setTranslate / setRotate on the bridge.';
  if (p.includes('content')) return 'Adapter gap — pseudo-element-style content; consider rendering as a label child.';
  if (p.startsWith('aspect')) return 'Adapter gap — can be lowered to width/height via Yoga aspectRatio.';
  if (p.includes('scroll')) return 'Pulp framework gap — scroll containers are widget-driven, not CSS-driven.';
  return 'Adapter gap — unknown prop, add to known-css.ts as either mapped or intentional-drop.';
}
