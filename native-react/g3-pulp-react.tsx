// G3.5 — same content as G3 but driven through @pulp/react's host config
// (no dom-adapter, no HTML JSX). If chrome labels render correctly here,
// dom-adapter is at fault. If they truncate here, @pulp/react host config is.
import { createElement } from 'react';
import { View, Label, render } from '@pulp/react';

const lg = (globalThis as { __spectrLog?: (s: string) => void }).__spectrLog;

function App() {
    if (lg) lg('[G3.5] App() rendering');
    return createElement(View as never, {
        width: 1320, height: 60,
        background: 'rgba(10,14,20,0.95)',
        direction: 'row' as never,
        alignItems: 'center' as never,
        gap: 18, padding: 16,
    },
        createElement(Label as never, {
            text: 'SPECTR',
            fontSize: 16, fontWeight: 600, letterSpacing: 1.5,
            textColor: '#ffffff',
        }),
        createElement(Label as never, {
            text: 'ZOOMABLE FILTER BANK',
            fontSize: 12,
            textColor: 'rgba(255,255,255,0.55)',
        }),
    );
}

if (lg) lg('[G3.5] mounting...');
render(createElement(App));
if (lg) lg('[G3.5] mounted');
