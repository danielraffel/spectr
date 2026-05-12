// Optional AI-augmented suggestions for unmapped CSS props.
// Calls the Anthropic Messages API; falls back gracefully when the
// ANTHROPIC_API_KEY env var is missing.

import Anthropic from '@anthropic-ai/sdk';
import type { ClassifiedProp } from './report.js';

export type AiSuggestionKind = 'mappable' | 'lower-to' | 'framework-gap' | 'unknown';

export interface AiSuggestion {
  cssProp: string;
  suggestion: AiSuggestionKind;
  detail: string;
}

export interface AiSuggestOptions {
  unmapped: ClassifiedProp[];
  bridgeFunctions: readonly string[];
  /** Override for tests; defaults to env var. */
  apiKey?: string;
  /** Override the model id for tests; defaults to a recent Sonnet. */
  model?: string;
}

const DEFAULT_MODEL = 'claude-sonnet-4-5';

export async function generateAiSuggestions(
  opts: AiSuggestOptions,
): Promise<{ ok: true; suggestions: AiSuggestion[] } | { ok: false; reason: string }> {
  const key = opts.apiKey ?? process.env.ANTHROPIC_API_KEY;
  if (!key) {
    return { ok: false, reason: 'ANTHROPIC_API_KEY not set; skipping --ai step.' };
  }
  if (opts.unmapped.length === 0) {
    return { ok: true, suggestions: [] };
  }

  const client = new Anthropic({ apiKey: key });
  const prompt = buildPrompt(opts.unmapped, opts.bridgeFunctions);

  try {
    const resp = await client.messages.create({
      model: opts.model ?? DEFAULT_MODEL,
      max_tokens: 2048,
      messages: [{ role: 'user', content: prompt }],
      system:
        'You analyze CSS properties for a GPU-rendered native UI runtime. Return strict JSON only.',
    });
    const text = resp.content
      .filter((c): c is Anthropic.TextBlock => c.type === 'text')
      .map(c => c.text)
      .join('\n');
    const suggestions = parseSuggestions(text);
    return { ok: true, suggestions };
  } catch (err) {
    return {
      ok: false,
      reason: `Anthropic API error: ${(err as Error).message ?? String(err)}`,
    };
  }
}

function buildPrompt(unmapped: ClassifiedProp[], bridge: readonly string[]): string {
  const propsBlock = unmapped
    .slice(0, 60)
    .map(c => `  - ${c.prop} (×${c.occurrences})`)
    .join('\n');
  const bridgeBlock = JSON.stringify([...bridge]);
  return [
    'You are reviewing a bundle that runs on Pulp\'s @pulp/react bridge.',
    'The bridge exposes only the following setX / createX functions:',
    bridgeBlock,
    '',
    'These CSS properties were used in style-prop literals but have no',
    'known mapping. Classify each one as exactly one of:',
    '  - "mappable"      → could be wired to an existing bridge setter',
    '  - "lower-to"      → the adapter could lower it onto another bridge primitive',
    '  - "framework-gap" → genuinely missing in the runtime; needs a new bridge setter',
    '',
    'Unmapped properties:',
    propsBlock,
    '',
    'Return ONLY a JSON array of objects with shape:',
    '  { "cssProp": string, "suggestion": "mappable"|"lower-to"|"framework-gap", "detail": string }',
    'No prose, no markdown fences.',
  ].join('\n');
}

export function parseSuggestions(raw: string): AiSuggestion[] {
  // Strip code fences if the model added any.
  const cleaned = raw
    .replace(/^```(?:json)?\s*/i, '')
    .replace(/```\s*$/i, '')
    .trim();
  let parsed: unknown;
  try {
    parsed = JSON.parse(cleaned);
  } catch {
    // Try to find the first JSON array substring.
    const m = cleaned.match(/\[[\s\S]*\]/);
    if (!m) return [];
    try {
      parsed = JSON.parse(m[0]);
    } catch {
      return [];
    }
  }
  if (!Array.isArray(parsed)) return [];
  const out: AiSuggestion[] = [];
  for (const item of parsed) {
    if (!item || typeof item !== 'object') continue;
    const o = item as Record<string, unknown>;
    const cssProp = typeof o.cssProp === 'string' ? o.cssProp : null;
    const detail = typeof o.detail === 'string' ? o.detail : '';
    const sugRaw = typeof o.suggestion === 'string' ? o.suggestion : '';
    const suggestion: AiSuggestionKind =
      sugRaw === 'mappable' || sugRaw === 'lower-to' || sugRaw === 'framework-gap'
        ? sugRaw
        : 'unknown';
    if (!cssProp) continue;
    out.push({ cssProp, suggestion, detail });
  }
  return out;
}
