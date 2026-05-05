// repro-1147-popover-render.tsx — minimal repro for pulp #1147.
//
// Bug: nested flex content + inline <svg> doesn't render inside an
// absolutely-positioned popover. In Spectr's chrome:
//   - AnalyzerPopover (flex-direct-child text only) leaks/escapes panel
//   - EditModePopover (nested flex-1 + inline svg) renders empty
//
// This file boils both shapes down to the simplest <button>+popover
// possible. Default-open both popovers stacked vertically so the
// framework team can see both failure modes in one render.
//
// Build:  npm run build:repro-1147
// Then:   load native-react/dist/editor.js into a Pulp standalone host.

import '../host-shims.js';                // populate React + ReactDOM globals
import { createElement, Fragment } from 'react';

// React + ReactDOM end up on globalThis via host-shims; pull them off.
const g = globalThis as unknown as {
    React: { useState: <T>(v: T) => [T, (v: T) => void] };
    ReactDOM: { createRoot: (c: unknown) => { render: (e: unknown) => void } };
};
const { useState } = g.React;

// (A) AnalyzerPopover pattern — flex-column with direct-child text rows.
function AnalyzerPopoverRepro() {
    return (
        <div style={{
            position: 'absolute', bottom: 34, left: 0,
            background: 'rgba(12,16,22,0.96)',
            border: '1px solid rgba(255,255,255,0.1)',
            borderRadius: 4, padding: 6,
            display: 'flex', flexDirection: 'column', gap: 2,
            width: 260,
        }}>
            <div style={{ fontSize: 8.5, opacity: 0.45, padding: '4px 8px 6px' }}>ANALYZER</div>
            {['SPECTRUM', 'WATERFALL', 'OSCILLOSCOPE'].map(label => (
                <button key={label} style={{
                    background: 'transparent', border: '1px solid transparent',
                    color: 'rgba(255,255,255,0.82)',
                    padding: '7px 10px', borderRadius: 3, cursor: 'pointer',
                    display: 'block', textAlign: 'left',
                    fontSize: 10, letterSpacing: 0.5,
                }}>
                    <span style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                        <span style={{ display: 'inline-block', width: 18, height: 2, background: '#7af', borderRadius: 1 }} />
                        <span style={{ fontWeight: 600 }}>{label}</span>
                    </span>
                    <span style={{ display: 'block', fontSize: 9.5, opacity: 0.6, marginTop: 3 }}>
                        description line
                    </span>
                </button>
            ))}
        </div>
    );
}

// (B) EditModePopover pattern — nested flex-1 spans + inline <svg>.
function EditModePopoverRepro() {
    return (
        <div style={{
            position: 'absolute', bottom: 34, left: 0,
            background: 'rgba(12,16,22,0.96)',
            border: '1px solid rgba(255,255,255,0.1)',
            borderRadius: 4, padding: 6,
            display: 'flex', flexDirection: 'column', gap: 2,
            width: 280,
        }}>
            <div style={{ fontSize: 8.5, opacity: 0.45, padding: '4px 8px 6px' }}>EDIT MODE</div>
            {[{ k: 'peak', label: 'PEAK' }, { k: 'sculpt', label: 'SCULPT' }].map(m => (
                <button key={m.k} style={{
                    background: 'transparent', border: '1px solid transparent',
                    color: 'rgba(255,255,255,0.82)',
                    padding: '8px 10px', borderRadius: 3, cursor: 'pointer',
                    display: 'flex', alignItems: 'flex-start', gap: 10,
                    fontSize: 10, letterSpacing: 0.5, textAlign: 'left',
                }}>
                    <svg width="28" height="20" viewBox="0 0 24 24" style={{ flex: 'none', marginTop: 2 }}>
                        <path d="M2 12 L8 6 L16 18 L22 8" stroke="rgba(255,255,255,0.55)" strokeWidth="1.4" fill="none" />
                    </svg>
                    <span style={{ flex: 1 }}>
                        <span style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 3 }}>
                            <span style={{ fontWeight: 600 }}>{m.label}</span>
                            <span style={{ opacity: 0.5, fontSize: 9 }}>· tagline</span>
                            <span style={{ flex: 1 }} />
                            <span style={{ fontSize: 8.5, opacity: 0.5, padding: '1px 5px', border: '1px solid rgba(255,255,255,0.14)' }}>K</span>
                        </span>
                        <span style={{ display: 'block', fontSize: 9.5, opacity: 0.6, lineHeight: 1.5 }}>
                            description body, multi-word, should wrap inside the flex-1 column
                        </span>
                    </span>
                </button>
            ))}
        </div>
    );
}

function App() {
    return (
        <div style={{ width: 1200, height: 800, background: '#05070a', padding: 40, display: 'flex', flexDirection: 'column', gap: 80 }}>
            <div style={{ position: 'relative', width: 280, height: 200, border: '1px dashed rgba(255,255,255,0.2)' }}>
                <div style={{ color: '#fff', fontSize: 11 }}>(A) AnalyzerPopover pattern — flex column, no svg</div>
                <AnalyzerPopoverRepro />
            </div>
            <div style={{ position: 'relative', width: 280, height: 280, border: '1px dashed rgba(255,255,255,0.2)' }}>
                <div style={{ color: '#fff', fontSize: 11 }}>(B) EditModePopover pattern — nested flex + inline svg</div>
                <EditModePopoverRepro />
            </div>
        </div>
    );
}

void Fragment;
const root = (g.ReactDOM.createRoot as (c: unknown) => { render: (e: unknown) => void })(null);
root.render(createElement(App));
