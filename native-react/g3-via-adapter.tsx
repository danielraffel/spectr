// G3.6 — same content but driven through dom-adapter (HTML JSX style).
// If THIS truncates while G3.5 (@pulp/react direct) didn't, dom-adapter is
// the culprit.
import { createElement } from './dom-adapter';
import { render } from '@pulp/react';

const lg = (globalThis as { __spectrLog?: (s: string) => void }).__spectrLog;

function App() {
    if (lg) lg('[G3.6] rendering via dom-adapter');
    return createElement('div' as never, {
        style: {
            width: 1320, height: 60,
            background: 'rgba(10,14,20,0.95)',
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'center',
            gap: 18, padding: 16,
        },
    },
        createElement('span' as never, {
            style: { fontSize: 16, fontWeight: 600, letterSpacing: 1.5, color: '#ffffff' },
        }, 'SPECTR'),
        createElement('span' as never, {
            style: { fontSize: 12, color: 'rgba(255,255,255,0.55)' },
        }, 'ZOOMABLE FILTER BANK'),
    );
}

if (lg) lg('[G3.6] mounting');
render(createElement(App));
if (lg) lg('[G3.6] mounted');
