// G5 — same chrome content, but boots through host-shims (the way Spectr
// does). If THIS truncates while G3.6 (no host-shims) didn't, host-shims
// or jsx-runtime-shim is the culprit.
import './host-shims.js';
// host-shims sets globalThis.createElement = adaptedCreateElement
// and globalThis.React = { createElement, ... }. Mimicking Spectr's
// extracted-bundle pattern.
const lg = (globalThis as { __spectrLog?: (s: string) => void }).__spectrLog;
const ce = (globalThis as { createElement: (...a: unknown[]) => unknown }).createElement;
const ReactDOM = (globalThis as { ReactDOM: { createRoot: (c: unknown) => { render: (e: unknown) => void } } }).ReactDOM;

if (lg) lg('[G5] starting via host-shims');

function App() {
    if (lg) lg('[G5] App() rendering');
    return ce('div', {
        style: {
            width: 1320, height: 60,
            background: 'rgba(10,14,20,0.95)',
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'center',
            gap: 18, padding: 16,
        },
    },
        ce('span', { style: { fontSize: 16, fontWeight: 600, letterSpacing: 1.5, color: '#ffffff' } }, 'SPECTR'),
        ce('span', { style: { fontSize: 12, color: 'rgba(255,255,255,0.55)' } }, 'ZOOMABLE FILTER BANK'),
    );
}

if (lg) lg('[G5] mounting via ReactDOM.createRoot');
const root = ReactDOM.createRoot({});
root.render(ce(App));
if (lg) lg('[G5] mounted');
