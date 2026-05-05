// AST-walk a JS bundle to find every `ctx.X` member access where `ctx`
// is bound from `getContext('2d')` or visually appears to be a canvas
// rendering context (heuristic — see notes).
//
// Approach: scan for variable bindings of the form
//   const X = canvas.getContext('2d')
//   const X = el.getContext('2d')
//   X = anything.getContext('2d')
// then track member accesses on those bindings. Cross-bundle bindings
// aren't tracked perfectly — as a fallback we also count every
// `<id>.fillRect` / `<id>.beginPath` style access where the property
// matches a known Canvas 2D API name. Bundles that minify to single-
// letter ctx vars still produce useful counts via the fallback.

import { parse } from '@babel/parser';
import _traverse from '@babel/traverse';
import type { File } from '@babel/types';
import { readFileSync } from 'node:fs';
import { CANVAS_2D_BY_NAME } from './known-canvas2d.js';

const traverse = (_traverse as unknown as { default: typeof _traverse }).default ?? _traverse;

export interface Usage {
    name: string;
    count: number;
    /// First N source-line numbers we saw the access on, for spot-checking.
    sampleLines: number[];
}

export interface ExtractResult {
    bundlePath: string;
    totalCalls: number;
    /// Map of API name → Usage. Includes only names listed in
    /// known-canvas2d.ts (our W3C-spec surface).
    canvas2d: Map<string, Usage>;
    /// API names accessed off a tracked ctx binding but NOT in the W3C
    /// list — these are usually our own methods or vendor extensions.
    /// Surfacing them helps catch typos and proprietary APIs.
    unknown: Map<string, Usage>;
}

export function extractCanvasUsage(bundlePath: string): ExtractResult {
    const src = readFileSync(bundlePath, 'utf8');
    const ast: File = parse(src, {
        sourceType: 'unambiguous',
        plugins: ['typescript', 'jsx'],
        errorRecovery: true,
    });

    // Pass 1 — find ctx bindings: `X = anything.getContext('2d')`.
    const ctxBindings = new Set<string>();
    traverse(ast, {
        VariableDeclarator(path) {
            const init = path.node.init;
            if (!init || init.type !== 'CallExpression') return;
            const callee = init.callee;
            if (callee.type !== 'MemberExpression') return;
            if (callee.property.type !== 'Identifier' || callee.property.name !== 'getContext') return;
            const arg0 = init.arguments[0];
            if (arg0?.type !== 'StringLiteral' || arg0.value !== '2d') return;
            // Capture variable name.
            if (path.node.id.type === 'Identifier') ctxBindings.add(path.node.id.name);
        },
        AssignmentExpression(path) {
            const right = path.node.right;
            if (right.type !== 'CallExpression') return;
            const callee = right.callee;
            if (callee.type !== 'MemberExpression') return;
            if (callee.property.type !== 'Identifier' || callee.property.name !== 'getContext') return;
            const arg0 = right.arguments[0];
            if (arg0?.type !== 'StringLiteral' || arg0.value !== '2d') return;
            if (path.node.left.type === 'Identifier') ctxBindings.add(path.node.left.name);
        },
    });

    // Pass 2 — count member accesses.
    const canvas2d = new Map<string, Usage>();
    const unknown = new Map<string, Usage>();
    let totalCalls = 0;

    function bump(map: Map<string, Usage>, name: string, line: number) {
        let u = map.get(name);
        if (!u) {
            u = { name, count: 0, sampleLines: [] };
            map.set(name, u);
        }
        u.count++;
        if (u.sampleLines.length < 3) u.sampleLines.push(line);
    }

    traverse(ast, {
        MemberExpression(path) {
            const obj = path.node.object;
            const prop = path.node.property;
            if (obj.type !== 'Identifier') return;
            if (prop.type !== 'Identifier') return;
            const onCtx = ctxBindings.has(obj.name);
            // Heuristic fallback: short-name (≤3 chars) ID accessing a
            // canonical canvas2d API name. Bundles minify `ctx` to `c`,
            // `t`, `g`, etc. — without this fallback the W3C-surface
            // table comes back empty for a heavily minified bundle.
            const name = prop.name;
            const isCanonical = CANVAS_2D_BY_NAME.has(name);
            const fallback = !onCtx && obj.name.length <= 3 && isCanonical;
            if (!onCtx && !fallback) return;

            totalCalls++;
            const line = path.node.loc?.start.line ?? 0;
            if (isCanonical) {
                bump(canvas2d, name, line);
            } else {
                bump(unknown, name, line);
            }
        },
    });

    return { bundlePath, totalCalls, canvas2d, unknown };
}
