// Markdown-report generator for the bridge coverage analyzer.

import type { ExtractResult } from './extract.js';
import type { ShimMember } from './parse-shim.js';
import type { BridgeFn } from './parse-bridge.js';
import { CANVAS_2D_API } from './known-canvas2d.js';

export interface ReportInput {
    bundle: ExtractResult;
    shim: ShimMember[];
    bridge: BridgeFn[];
}

export interface RowVerdict {
    name: string;
    kind: 'method' | 'property';
    used: number;          // call count from bundle
    sampleLines: number[];
    inShim: boolean;
    bridgeCalls: string[]; // bridge fn names the shim dispatches to
    bridgeRegistered: boolean[];  // parallel: which of those exist in widget_bridge.cpp
    verdict: 'supported' | 'shim-gap' | 'bridge-gap' | 'unused';
}

export function buildVerdicts(inp: ReportInput): RowVerdict[] {
    const shimByName = new Map(inp.shim.map(m => [m.name, m]));
    const bridgeNames = new Set(inp.bridge.map(b => b.name));
    const rows: RowVerdict[] = [];
    for (const api of CANVAS_2D_API) {
        const usage = inp.bundle.canvas2d.get(api.name);
        const used = usage?.count ?? 0;
        const sampleLines = usage?.sampleLines ?? [];
        const shim = shimByName.get(api.name);
        const inShim = !!shim;
        const bridgeCalls = shim?.bridgeCalls ?? [];
        const bridgeRegistered = bridgeCalls.map(n => bridgeNames.has(n));
        let verdict: RowVerdict['verdict'];
        if (used === 0) verdict = 'unused';
        else if (!inShim) verdict = 'shim-gap';
        else if (bridgeCalls.length > 0 && bridgeRegistered.some(r => !r)) verdict = 'bridge-gap';
        else verdict = 'supported';
        rows.push({
            name: api.name,
            kind: api.kind,
            used,
            sampleLines,
            inShim,
            bridgeCalls,
            bridgeRegistered,
            verdict,
        });
    }
    return rows;
}

export function renderMarkdown(inp: ReportInput, rows: RowVerdict[]): string {
    const totalAPIs = rows.length;
    const used = rows.filter(r => r.used > 0);
    const supported = used.filter(r => r.verdict === 'supported');
    const shimGap = used.filter(r => r.verdict === 'shim-gap');
    const bridgeGap = used.filter(r => r.verdict === 'bridge-gap');

    const out: string[] = [];
    out.push('# Pulp Bridge Coverage — Canvas 2D');
    out.push('');
    out.push(`Bundle: \`${inp.bundle.bundlePath}\``);
    out.push(`Total ctx.X accesses detected: **${inp.bundle.totalCalls}**`);
    out.push(`W3C Canvas 2D surface entries: **${totalAPIs}**`);
    out.push('');
    out.push('## Summary');
    out.push('');
    out.push(`| Bucket | Count | %  |`);
    out.push(`|---|---|---|`);
    const pct = (n: number) => used.length > 0 ? `${Math.round(n / used.length * 100)}%` : '—';
    out.push(`| Used + supported end-to-end | ${supported.length} | ${pct(supported.length)} |`);
    out.push(`| Used + missing in shim | ${shimGap.length} | ${pct(shimGap.length)} |`);
    out.push(`| Used + shim dispatches to unregistered bridge fn | ${bridgeGap.length} | ${pct(bridgeGap.length)} |`);
    out.push(`| Spec entries not used by this bundle | ${totalAPIs - used.length} | — |`);
    out.push('');
    if (shimGap.length > 0) {
        out.push('## ⚠ Shim gaps (used by bundle, not implemented in canvas2d-shim.ts)');
        out.push('');
        out.push('| API | Kind | Uses | Sample lines |');
        out.push('|---|---|---|---|');
        for (const r of shimGap.sort((a, b) => b.used - a.used)) {
            out.push(`| \`${r.name}\` | ${r.kind} | ${r.used} | ${r.sampleLines.join(', ')} |`);
        }
        out.push('');
    }
    if (bridgeGap.length > 0) {
        out.push('## ⚠ Bridge gaps (shim implements, but the C++ bridge fn isn\'t registered)');
        out.push('');
        out.push('| API | Bridge calls | Missing |');
        out.push('|---|---|---|');
        for (const r of bridgeGap) {
            const missing = r.bridgeCalls
                .filter((_, i) => !r.bridgeRegistered[i])
                .map(n => `\`${n}\``)
                .join(', ');
            out.push(`| \`${r.name}\` | ${r.bridgeCalls.map(n => '`' + n + '`').join(', ')} | ${missing} |`);
        }
        out.push('');
    }
    out.push('## Full coverage table');
    out.push('');
    out.push('| API | Kind | Uses | Verdict | Bridge calls |');
    out.push('|---|---|---|---|---|');
    for (const r of rows) {
        const verdict =
            r.verdict === 'supported' ? '✅' :
            r.verdict === 'shim-gap'  ? '⚠ shim' :
            r.verdict === 'bridge-gap' ? '⚠ bridge' :
            '·';
        const calls = r.bridgeCalls.map(n => '`' + n + '`').join(', ') || '—';
        out.push(`| \`${r.name}\` | ${r.kind} | ${r.used} | ${verdict} | ${calls} |`);
    }
    if (inp.bundle.unknown.size > 0) {
        out.push('');
        out.push('## Unknown accesses (off a ctx-shaped binding but not in the W3C list)');
        out.push('');
        out.push('Likely shim helpers, vendor extensions, or typos — review.');
        out.push('');
        out.push('| Name | Uses |');
        out.push('|---|---|');
        const u = Array.from(inp.bundle.unknown.values()).sort((a, b) => b.count - a.count);
        for (const e of u.slice(0, 30)) {
            out.push(`| \`${e.name}\` | ${e.count} |`);
        }
    }
    return out.join('\n') + '\n';
}
