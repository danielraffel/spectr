// Tiny shim that replaces `react/jsx-runtime` for the editor-port bundle.
// The extracted React app's JSX was compiled to `jsx("div", {...})` /
// `jsxs("div", {...children: [...]})` calls against React's automatic
// runtime. Those bypass our dom-adapter's createElement, so HTML tag
// names (div/span/canvas/...) pass through to @pulp/react's host
// config, which throws "unknown intrinsic type: div".
//
// This shim translates jsx/jsxs into createElement-shaped calls that
// hit dom-adapter, where tag-name → bridge-primitive mapping happens.
//
// Wired via `esbuild --alias:react/jsx-runtime=./jsx-runtime-shim.ts`
// in package.json's build:port:dev script.

import { createElement } from './dom-adapter.js';

export { Fragment } from 'react';

let _jsx_count = 0;
function _probe(label: string, type: unknown): void {
    _jsx_count++;
    if (_jsx_count <= 25) {
        const desc = typeof type === 'string' ? type
            : typeof type === 'function' ? '<' + ((type as { name?: string }).name || 'fn') + '>'
            : String(type);
        const lg = (globalThis as { __spectrLog?: (...a: unknown[]) => void }).__spectrLog;
        if (lg) lg('[' + label + '] #' + _jsx_count + ' ' + desc);
    }
}

export function jsx(type: unknown, props: Record<string, unknown>, key?: unknown): unknown {
    _probe('jsx', type);
    const { children, ...rest } = props ?? {};
    if (key !== undefined) (rest as Record<string, unknown>).key = key;
    if (children === undefined) {
        return createElement(type, rest);
    }
    return createElement(type, rest, children as never);
}

export function jsxs(type: unknown, props: Record<string, unknown>, key?: unknown): unknown {
    _probe('jsxs', type);
    const { children, ...rest } = props ?? {};
    if (key !== undefined) (rest as Record<string, unknown>).key = key;
    const kids = Array.isArray(children) ? children : [children];
    return createElement(type, rest, ...(kids as never[]));
}

export const jsxDEV = jsx;
