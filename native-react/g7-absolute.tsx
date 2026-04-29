// G7 — Spectr's exact outer-div CSS: position:absolute, top:0, left:0, right:0, height:44
import './host-shims.js';
const lg = (globalThis as { __spectrLog?: (s: string) => void }).__spectrLog;
const ce = (globalThis as { createElement: (...a: unknown[]) => unknown }).createElement;
const ReactDOM = (globalThis as { ReactDOM: { createRoot: (c: unknown) => { render: (e: unknown) => void } } }).ReactDOM;

function App() {
    return ce('div', { style: { width: 1320, height: 860, background: 'rgb(40,40,60)', position: 'relative' } },
        // Outer chrome bar — Spectr's exact pattern
        ce('div', {
            style: {
                position: 'absolute', top: 0, left: 0, right: 0, height: 44,
                display: 'flex', alignItems: 'center', gap: 18,
                padding: '0 20px',
                background: 'linear-gradient(to bottom, rgba(10,14,20,0.8), rgba(10,14,20,0.0))',
                fontFamily: 'var(--mono)', fontSize: 11, letterSpacing: 0.5,
                color: 'rgba(255,255,255,0.75)',
            },
        },
            // Inner flex container with logo + labels (Spectr's pattern)
            ce('div', { style: { display: 'flex', alignItems: 'center', gap: 10 } },
                ce('span', { style: { fontWeight: 600, letterSpacing: 1.5, color: '#fff' } }, 'SPECTR'),
                ce('span', { style: { opacity: 0.4 } }, '·'),
                ce('span', { style: { opacity: 0.55 } }, 'ZOOMABLE FILTER BANK'),
            ),
        ),
    );
}

if (lg) lg('[G7] mounting');
ReactDOM.createRoot({}).render(ce(App));
if (lg) lg('[G7] mounted');
