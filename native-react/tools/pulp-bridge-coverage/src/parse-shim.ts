// Parse `canvas2d-shim.ts` to discover which CanvasRenderingContext2D
// methods/properties the shim has implementations for. The result is the
// authoritative "supported by Pulp's canvas2d-shim" set, cross-referenced
// against the W3C surface defined in known-canvas2d.ts.
//
// We deliberately parse the SHIM SOURCE, not a hard-coded list, so a
// developer who adds a method to the shim doesn't also have to remember
// to register it here. The single source of truth is the implementation.

import { parse } from '@babel/parser';
import _traverse from '@babel/traverse';
import type { File } from '@babel/types';
import { readFileSync } from 'node:fs';

const traverse = (_traverse as unknown as { default: typeof _traverse }).default ?? _traverse;

export interface ShimMember {
    name: string;
    kind: 'method' | 'getter' | 'setter';
    /// What bridge functions this member dispatches to (best-effort —
    /// extracted from `call('X', ...)` patterns inside the body).
    bridgeCalls: string[];
}

export function parseShim(shimPath: string): ShimMember[] {
    const src = readFileSync(shimPath, 'utf8');
    const ast: File = parse(src, {
        sourceType: 'module',
        plugins: ['typescript'],
    });

    const members: ShimMember[] = [];

    traverse(ast, {
        // Class body: scan ClassMethod and ClassMethod kind=getter/setter
        ClassMethod(path) {
            const node = path.node;
            if (node.key.type !== 'Identifier') return;
            const name = node.key.name;
            const kind: ShimMember['kind'] =
                node.kind === 'get' ? 'getter' :
                node.kind === 'set' ? 'setter' : 'method';

            // Walk the method body for `call('X', ...)` invocations.
            const bridgeCalls: string[] = [];
            path.traverse({
                CallExpression(inner) {
                    const callee = inner.node.callee;
                    if (callee.type !== 'Identifier' || callee.name !== 'call') return;
                    const first = inner.node.arguments[0];
                    if (first?.type === 'StringLiteral') {
                        bridgeCalls.push(first.value);
                    }
                },
            });

            members.push({ name, kind, bridgeCalls });
        },
    });

    return members;
}

/// Group shim members by API name (collapses paired getter/setter).
export function shimSupportedNames(members: ShimMember[]): Set<string> {
    return new Set(members.map(m => m.name));
}
