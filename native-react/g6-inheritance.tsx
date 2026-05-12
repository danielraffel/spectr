// G6 — replicate Spectr's exact pattern: parent sets fontSize, children
// don't. If THIS truncates, CSS inheritance is the missing piece.
import './host-shims.js';
const lg = (globalThis as { __spectrLog?: (s: string) => void }).__spectrLog;
const ce = (globalThis as { createElement: (...a: unknown[]) => unknown }).createElement;
const ReactDOM = (globalThis as { ReactDOM: { createRoot: (c: unknown) => { render: (e: unknown) => void } } }).ReactDOM;

function App() {
    return ce('div', {
        style: {
            width: 1320, height: 60,
            background: 'rgba(10,14,20,0.95)',
            display: 'flex', flexDirection: 'row',
            alignItems: 'center', gap: 18, padding: 16,
            // Parent sets typography — children should inherit
            fontSize: 12,
            color: 'rgba(255,255,255,0.75)',
            letterSpacing: 0.5,
        },
    },
        // SPECTR — sets fontWeight + color but NOT fontSize (relies on inherit)
        ce('span', { style: { fontWeight: 600, letterSpacing: 1.5, color: '#fff' } }, 'SPECTR'),
        // ZOOMABLE FILTER BANK — sets opacity but NOT fontSize/color
        ce('span', { style: { opacity: 0.55 } }, 'ZOOMABLE FILTER BANK'),
    );
}

if (lg) lg('[G6] mounting');
ReactDOM.createRoot({}).render(ce(App));
if (lg) lg('[G6] mounted');
