// repro-1148-overlay-clicks.tsx — minimal repro for pulp #1148.
//
// Bug: clicks don't dispatch to children inside an absolutely-positioned
// overlay panel. ESC key + outside-click affordances also don't fire,
// so a popover, once open, has no way to close itself.
//
// Layout:
//   - one trigger button at top
//   - on open: a transparent backdrop covering the whole window (should
//     receive a click and call onClose)
//   - on top of that: a small overlay panel with three counter buttons
//   - useEffect adds a keydown listener for Escape to close the panel
//
// Expected:
//   - clicking a counter button bumps the count (visible label)
//   - clicking the backdrop closes the panel
//   - pressing Escape closes the panel
// Observed:
//   - none of the three fire; the overlay sits there inert.
//
// Build:  npm run build:repro-1148

import '../host-shims.js';
import { createElement, Fragment } from 'react';

const g = globalThis as unknown as {
    React: {
        useState: <T>(v: T) => [T, (v: T | ((prev: T) => T)) => void];
        useEffect: (fn: () => void | (() => void), deps?: unknown[]) => void;
    };
    ReactDOM: { createRoot: (c: unknown) => { render: (e: unknown) => void } };
};
const { useState, useEffect } = g.React;

function App() {
    const [open, setOpen] = useState(false);
    const [count, setCount] = useState(0);

    useEffect(() => {
        if (!open) return;
        const onKey = (e: { key?: string }) => {
            if (e?.key === 'Escape') setOpen(false);
        };
        // Document is shimmed; this should still register and fire.
        (document as unknown as { addEventListener: (t: string, f: unknown) => void })
            .addEventListener('keydown', onKey);
        return () => {
            (document as unknown as { removeEventListener: (t: string, f: unknown) => void })
                .removeEventListener('keydown', onKey);
        };
    }, [open]);

    return (
        <div style={{ width: 1200, height: 800, background: '#0a0d12', padding: 40 }}>
            <div style={{ color: '#fff', fontSize: 12, marginBottom: 12 }}>
                count: {String(count)} (target: increments when overlay buttons clicked)
            </div>
            <button
                onClick={() => setOpen(o => !o)}
                style={{
                    background: '#1a2230', color: '#fff', border: '1px solid #355',
                    padding: '8px 14px', cursor: 'pointer', fontSize: 11,
                }}>
                {open ? 'CLOSE' : 'OPEN OVERLAY'}
            </button>

            {open ? (
                <Fragment>
                    {/* Transparent backdrop — click should call onClose */}
                    <div
                        onClick={() => setOpen(false)}
                        style={{
                            position: 'absolute', top: 0, left: 0, right: 0, bottom: 0,
                            background: 'rgba(0,0,0,0.001)',
                        }}
                    />
                    {/* The overlay panel */}
                    <div
                        onClick={(e: { stopPropagation?: () => void }) => e?.stopPropagation?.()}
                        style={{
                            position: 'absolute', top: 120, left: 60,
                            width: 320, padding: 16,
                            background: 'rgba(20,28,40,0.96)',
                            border: '1px solid rgba(255,255,255,0.15)', borderRadius: 6,
                            display: 'flex', flexDirection: 'column', gap: 8,
                        }}>
                        <div style={{ color: '#fff', fontSize: 11, marginBottom: 4 }}>
                            click any button below — count should increment
                        </div>
                        {[1, 2, 3].map(n => (
                            <button
                                key={n}
                                onClick={() => setCount(c => c + 1)}
                                style={{
                                    background: '#22324a', color: '#fff',
                                    border: '1px solid #4a6688', padding: '8px 10px',
                                    cursor: 'pointer', fontSize: 11, textAlign: 'left',
                                }}>
                                button {n} (click me)
                            </button>
                        ))}
                        <div style={{ color: 'rgba(255,255,255,0.5)', fontSize: 10, marginTop: 6 }}>
                            ESC should close · click outside should close
                        </div>
                    </div>
                </Fragment>
            ) : null}
        </div>
    );
}

const root = (g.ReactDOM.createRoot as (c: unknown) => { render: (e: unknown) => void })(null);
root.render(createElement(App));
