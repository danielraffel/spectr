// host-shims.ts — bare-minimum stubs of window / document / window.Spectr
// + global React / ReactDOM bindings, for the extracted
// spectr-editor-extracted.js to evaluate inside Pulp's QuickJS
// environment. Real persistence / pattern library will move into
// Spectr's StateStore via the C++ bridge later; for v0 these are
// in-memory so the React app can boot and render structure.
//
// Ordering matters: this file is imported BEFORE
// spectr-editor-extracted.js, and side-effects at module-init time set
// the globals so the extracted code's top-level
//   const { useRef, ... } = React;
// resolves to our @pulp/react-routed implementation.

import { Fragment, useState, useEffect, useRef, useCallback, useMemo } from 'react';
import { render } from '@pulp/react';
import { createElement as adaptedCreateElement } from './dom-adapter.js';

const g = globalThis as unknown as Record<string, unknown>;

// React global — what the extracted code destructures hooks off of.
let _effId = 0;
const _logEffect = (fn: () => unknown, deps?: unknown) => {
    const id = ++_effId;
    const fp = String(fn).slice(0, 80).replace(/\s+/g, ' ');
    return useEffect(() => {
        const lg = (globalThis as unknown as Record<string, unknown>).__spectrLog as
            ((s: string) => void) | undefined;
        if (lg) lg('[useEff#' + id + '] ' + fp);
        try {
            const r = fn();
            return typeof r === 'function' ? r as () => unknown : undefined;
        } catch (e) {
            if (lg) lg('[useEff#' + id + '] THREW: ' +
                ((e as { message?: string })?.message ?? String(e)));
            throw e;
        }
    }, deps as unknown as React.DependencyList);
};
g.React = {
    createElement: adaptedCreateElement,
    Fragment,
    useState,
    useEffect: _logEffect,
    useRef,
    useCallback,
    useMemo,
};
// ReactDOM global — extracted code calls ReactDOM.createRoot(...).render(<App/>).
g.ReactDOM = {
    createRoot: (_container: unknown) => ({
        render: (element: unknown) => render(element as never),
    }),
};
// esbuild compiles JSX to bare createElement(...)/Fragment refs per the
// build script's --jsx-factory / --jsx-fragment flags, so put those on
// the global too.
g.createElement = adaptedCreateElement;
g.Fragment = Fragment;

if (typeof g.window === 'undefined') g.window = g;

// Wrap the bridge-installed `requestAnimationFrame` so user callbacks
// always receive a finite high-resolution timestamp. Pulp v0.52.0 ships
// rAF via web-compat-scheduler.js, but the bridge's `__invokeFrame__(id)`
// path doesn't propagate a timestamp argument — it just calls the stored
// callback with no args. FilterBank's draw(now) does
//   const dt = Math.min(0.05, (now - last) / 1e3);
// so when `now === undefined`, dt becomes NaN, timeRef.current accumulates
// NaN, and every downstream `Math.sin(NaN * ...)` cascades into 337+ NaN
// canvas y-coordinates that silently no-op the spectrum draw.
//
// Fix: intercept rAF, wrap the user callback so it always sees
// performance.now(). Track via diagnostic counter to keep our existing
// __invokeFrame__ probe path useful.
{
    const winAny0 = g.window as Record<string, unknown>;
    const origRaf = winAny0.requestAnimationFrame as
        ((cb: (t: number) => void) => number) | undefined;
    if (typeof origRaf === 'function') {
        let invokeN = 0;
        const wrapped = (cb: (t: number) => void): number => {
            return origRaf((maybe?: number) => {
                invokeN++;
                const lg = (g as Record<string, unknown>).__spectrLog as
                    ((s: string) => void) | undefined;
                const perf = (g as { performance?: { now?: () => number } })
                    .performance;
                const ts = (typeof maybe === 'number' && Number.isFinite(maybe))
                    ? maybe
                    : (perf?.now?.() ?? Date.now());
                if (lg && (invokeN <= 5 || invokeN % 60 === 0)) {
                    lg('[rAF-cb#' + invokeN + '] ts=' + ts.toFixed(2) +
                        ' (orig=' + maybe + ')');
                }
                try { cb(ts); } catch (e) {
                    if (lg) lg('[rAF-cb#' + invokeN + '] THREW: ' +
                        ((e as { message?: string })?.message ?? String(e)));
                    throw e;
                }
            });
        };
        winAny0.requestAnimationFrame = wrapped;
        g.requestAnimationFrame = wrapped;
    }
}

// Pulp's bridge installs its own `document` polyfill via kDomOpsInit
// before our user script runs (see widget_bridge.cpp::load_script).
// Its getElementById doesn't know about 'tweak-defaults' — the
// extracted App() reads that node's textContent for its initial
// settings. We override getElementById to short-circuit the
// tweak-defaults lookup, falling through to the bridge's polyfill
// for everything else.
const TWEAK_DEFAULTS = JSON.stringify({
    bandCount: 32,
    metaphor: 'spectrum',
    bloom: 0.4,
    spectrumIntensity: 0.7,
    muteStyle: 'cross',
    motionMode: 'precision',
    showMinimap: true,
    showRulers: true,
    theme: 'dark',
});
const _doc = (g.document as { getElementById?: (id: string) => unknown } | undefined) ?? {};
const _origGetById = _doc.getElementById?.bind(_doc);
const docAny = _doc as Record<string, unknown>;
docAny.getElementById = (id: string) => {
    if (id === 'tweak-defaults') {
        return { textContent: TWEAK_DEFAULTS };
    }
    return _origGetById ? _origGetById(id) : null;
};
if (typeof docAny.createElement !== 'function') {
    docAny.createElement = (_tag: string) => ({
        style: {}, classList: { add() {}, remove() {} },
        appendChild() {}, addEventListener() {}, removeEventListener() {},
    });
}
if (typeof docAny.addEventListener !== 'function') {
    docAny.addEventListener = () => { /* no global keyboard yet */ };
    docAny.removeEventListener = () => { /* no global keyboard yet */ };
}
g.document = _doc;

const winAny = g.window as Record<string, unknown>;
if (winAny.devicePixelRatio === undefined) winAny.devicePixelRatio = 2;
if (typeof winAny.addEventListener !== 'function') {
    winAny.addEventListener = () => { /* no global keyboard yet */ };
    winAny.removeEventListener = () => { /* no global keyboard yet */ };
}
winAny.innerWidth = 1320;
winAny.innerHeight = 860;
winAny.parent = winAny;

// requestAnimationFrame: in the standalone Spectr the bridge's
// service_frame_callbacks() pumps these, but pulp-screenshot is
// one-shot and never ticks. Provide a fallback that fires the
// callback synchronously a few times so initial draw passes happen.
// In standalone, the bridge-installed rAF takes precedence (we
// don't override if one already exists).
if (typeof winAny.requestAnimationFrame !== 'function') {
    let _frameId = 0;
    let _ticks = 0;
    winAny.requestAnimationFrame = (cb: (t: number) => void) => {
        _frameId++;
        // Fire a bounded number of "frames" synchronously so any
        // RAF-driven loop completes a few iterations during the
        // one-shot eval. Cap at 3 to avoid infinite loops in
        // self-rearming animations.
        if (_ticks < 3) {
            _ticks++;
            try { cb(performance ? performance.now() : Date.now()); } catch (_e) { /* swallow */ }
        }
        return _frameId;
    };
    winAny.cancelAnimationFrame = () => { /* noop */ };
}
// Mirror onto globalThis so bare references work too. esbuild compiles
// modules to function scopes, and bare `requestAnimationFrame` inside
// extracted code resolves through the global lookup chain — assigning
// only to window.x doesn't put x on globalThis on every engine.
g.requestAnimationFrame = winAny.requestAnimationFrame;
g.cancelAnimationFrame = winAny.cancelAnimationFrame;
g.setTimeout = (winAny.setTimeout ?? g.setTimeout);
g.clearTimeout = (winAny.clearTimeout ?? g.clearTimeout);
g.setInterval = (winAny.setInterval ?? g.setInterval);
g.clearInterval = (winAny.clearInterval ?? g.clearInterval);
if (typeof (g.performance) === 'undefined') {
    g.performance = { now: () => Date.now() };
}

// In-memory stand-ins for window.Spectr, .SpectrFreq, .SpectrSignal,
// .SpectrMetaphors, .SpectrThemes. Used by the extracted code's pattern
// helpers and themed visuals. Real implementations will follow once the
// native lane reaches feature parity worth persisting.
winAny.Spectr = winAny.Spectr ?? {
    FACTORY_PATTERNS: [
        { id: 'factory:flat',       name: 'FLAT',       gains: [] },
        { id: 'factory:gentle-low', name: 'GENTLE LOW', gains: [] },
        { id: 'factory:bright',     name: 'BRIGHT',     gains: [] },
    ],
    PATTERN_SCHEMA_VERSION: 1,
    CANONICAL_RES: 64,
    factoryGains() { return []; },
    resolveGains(_pattern: unknown, n: number) { return new Array(n).fill(0); },
    remapGains(arr: number[], n: number) {
        const out = new Array(n).fill(0);
        for (let i = 0; i < n; i++) {
            const j = Math.floor(i / n * arr.length);
            out[i] = arr[j] ?? 0;
        }
        return out;
    },
    toCanonical(_arr: number[]) { return _arr; },
    fromCanonical(_arr: number[], _n: number) { return _arr; },
    makeUserPattern(name: string, gains: number[]) {
        return { id: 'user:' + Date.now(), name, gains };
    },
    exportEnvelope(p: unknown) { return JSON.stringify(p); },
    parseEnvelope(s: string) { try { return JSON.parse(s); } catch { return null; } },
    loadStore() { return []; },
    saveStore(_s: unknown[]) { /* no-op */ },
    loadDefaultId() { return null; },
    saveDefaultId(_id: unknown) { /* no-op */ },
};
winAny.SpectrFreq = winAny.SpectrFreq ?? {
    bandFrequencyHz(i: number, n: number) {
        // log-spaced 20 Hz → 20 kHz
        const fmin = 20, fmax = 20000;
        const t = i / Math.max(1, n - 1);
        return fmin * Math.pow(fmax / fmin, t);
    },
};
winAny.SpectrSignal = winAny.SpectrSignal ?? {
    // Stub spectrum sampler. drawSpectrum() in the bundle calls this
    // for each frequency bin. Return a synthesized envelope that gives
    // a visible spectrum shape (mid-bumped, decaying at extremes) so
    // the analyzer renders something instead of NaN-everywhere. Real
    // implementation hooks Spectr's analyzer ring-buffer (Phase 4).
    sample: (lf: number, t: number) => {
        // lf is log-frequency (typically 0..14ish for 20Hz..20kHz log scale).
        // Return value in roughly [-1, 1] dB-ish normalized.
        const f = (lf - 4) / 6;            // center mid-band
        const env = Math.exp(-f * f * 0.6); // gaussian bump
        const lfo = 0.05 * Math.sin(t * 1.3 + lf * 0.7);
        return Math.max(-1, Math.min(1, env * 0.6 + lfo - 0.2));
    },
};
winAny.SpectrMetaphors = winAny.SpectrMetaphors ?? {
    spectrum: { label: 'spectrum' },
};
winAny.SpectrThemes = winAny.SpectrThemes ?? {
    dark: { bg: '#05070a', fg: '#e8edf2', dim: '#6b7380' },
};
