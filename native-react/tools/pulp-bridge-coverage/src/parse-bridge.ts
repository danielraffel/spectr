// Parse Pulp's `core/view/src/widget_bridge.cpp` to discover which JS
// bridge functions are registered. The result is the authoritative
// "supported by the bridge" set.
//
// We grep-extract `engine_.register_function("X", ...)` patterns. The
// C++ source is the canonical truth — registering a JS-callable fn is
// the only way to add a bridge entry, so this captures the full surface.

import { readFileSync } from 'node:fs';

export interface BridgeFn {
    name: string;
    line: number;
}

export function parseBridge(cppPath: string): BridgeFn[] {
    const src = readFileSync(cppPath, 'utf8');
    const lines = src.split('\n');
    const fns: BridgeFn[] = [];
    const re = /engine_\.register_function\(\s*"([^"]+)"/;
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (!line) continue;
        const m = line.match(re);
        if (m && m[1]) fns.push({ name: m[1], line: i + 1 });
    }
    return fns;
}
