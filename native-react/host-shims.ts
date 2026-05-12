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

// window.Spectr / SpectrFreq / SpectrSignal / SpectrMetaphors / SpectrThemes
//
// Real implementations, ported from spectr-editor-extracted.js (the
// pre-runtime-import hand-coded path). These were stub fallbacks until
// 2026-05-12 — the runtime-import refactor moved the inline namespace
// out of editor.html's <script> blocks and never ported it back, so
// FilterBank's drawSpectrum() ran against `factoryGains() => []` and
// `SpectrSignal.sample()` returning negative numbers, producing the
// empty-spectrum / flat-band screenshot.
//
// These are not real audio — they're synthesized visual-test voices.
// The C++ side updates the actual analyzer frame via setSpectrumData().

// ---------- pattern system: factory generators ----------
function genFlat(N: number): number[] { return new Array(N).fill(0); }

function genHarmonic(N: number): number[] {
    const out = new Array(N).fill(-Infinity);
    const lmin = Math.log10(20), lmax = Math.log10(20000);
    const base = 110;
    for (let h = 1; h <= 16; h++) {
        const f = base * h;
        if (f > 20000) break;
        const lf = Math.log10(f);
        const pos = (lf - lmin) / (lmax - lmin);
        const idx = Math.round(pos * (N - 1));
        if (idx >= 0 && idx < N) out[idx] = Math.max(out[idx], 1 - (h - 1) * 0.04);
    }
    return out;
}

function genAlternate(N: number): number[] {
    return Array.from({ length: N }, (_, i) => (i % 2 === 0 ? 0.6 : -Infinity));
}

function genComb(N: number): number[] {
    return Array.from({ length: N }, (_, i) => (i % 3 === 0 ? 0.4 : -0.6));
}

function genVocal(N: number): number[] {
    const out = new Array(N).fill(-Infinity);
    const lmin = Math.log10(20), lmax = Math.log10(20000);
    for (const f of [300, 900, 2800]) {
        const lf = Math.log10(f);
        const pos = (lf - lmin) / (lmax - lmin);
        const c = Math.round(pos * (N - 1));
        for (let d = -2; d <= 2; d++) {
            const i = c + d;
            if (i < 0 || i >= N) continue;
            out[i] = d === 0 ? 1 : 0.5;
        }
    }
    return out;
}

function genSubOnly(N: number): number[] {
    const lmin = Math.log10(20), lmax = Math.log10(20000);
    return Array.from({ length: N }, (_, i) => {
        const pos = (i + 0.5) / N;
        const lf = lmin + pos * (lmax - lmin);
        const f = Math.pow(10, lf);
        return f < 160 ? 0.5 : -Infinity;
    });
}

function genTilt(N: number): number[] {
    return Array.from({ length: N }, (_, i) => 0.5 - (i / (N - 1)) * 1.0);
}

function genAirLift(N: number): number[] {
    const lmin = Math.log10(20), lmax = Math.log10(20000);
    return Array.from({ length: N }, (_, i) => {
        const pos = (i + 0.5) / N;
        const lf = lmin + pos * (lmax - lmin);
        const f = Math.pow(10, lf);
        if (f < 4000) return 0;
        return Math.min(0.7, Math.log2(f / 4000) * 0.3);
    });
}

const FACTORY_PATTERNS = [
    { id: 'factory:flat',     name: 'FLAT',                source: 'factory', gen: genFlat,     tags: ['baseline'] },
    { id: 'factory:harmonic', name: 'HARMONIC SERIES',     source: 'factory', gen: genHarmonic, tags: ['musical'] },
    { id: 'factory:alternate',name: 'ALTERNATING',         source: 'factory', gen: genAlternate,tags: ['structural'] },
    { id: 'factory:comb',     name: 'COMB',                source: 'factory', gen: genComb,     tags: ['structural'] },
    { id: 'factory:vocal',    name: 'VOCAL FORMANTS',      source: 'factory', gen: genVocal,    tags: ['tonal'] },
    { id: 'factory:sub',      name: 'SUB ONLY (< 160 Hz)', source: 'factory', gen: genSubOnly,  tags: ['tonal'] },
    { id: 'factory:tilt',     name: 'DOWNWARD TILT',       source: 'factory', gen: genTilt,     tags: ['baseline'] },
    { id: 'factory:air',      name: 'AIR LIFT (4k+)',      source: 'factory', gen: genAirLift,  tags: ['tonal'] },
];

function factoryGains(id: string, N: number): number[] | null {
    const p = FACTORY_PATTERNS.find(p => p.id === id);
    if (!p) return null;
    return p.gen(N);
}

function remapGains(src: number[], N: number): number[] {
    const M = src.length;
    if (M === N) return src.slice();
    const out = new Array(N);
    for (let i = 0; i < N; i++) {
        const pos = (i + 0.5) / N;
        const j = Math.min(M - 1, Math.max(0, Math.floor(pos * M)));
        out[i] = src[j];
    }
    return out;
}

const CANONICAL_RES = 128;
function round3(v: number): number { return Math.round(v * 1000) / 1000; }
function toCanonical(gains: number[]): Array<number | null> {
    return remapGains(gains, CANONICAL_RES).map(v => (v === -Infinity ? null : round3(v)));
}
function fromCanonical(arr: Array<number | null>): number[] {
    return arr.map(v => (v === null ? -Infinity : v));
}

function makeUserPattern(name: string, gains: number[]) {
    const now = new Date().toISOString();
    return {
        id: 'user:' + Math.random().toString(36).slice(2, 10) + Date.now().toString(36),
        name: name || 'Untitled',
        source: 'user',
        version: 1,
        createdAt: now,
        updatedAt: now,
        tags: [] as string[],
        gains: toCanonical(gains),
    };
}

function resolveGains(pattern: { source?: string; id?: string; gains?: Array<number | null> } | null,
                      N: number): number[] {
    if (!pattern) return genFlat(N);
    if (pattern.source === 'factory') {
        return factoryGains(pattern.id ?? '', N) ?? genFlat(N);
    }
    const src = fromCanonical(pattern.gains ?? []);
    return remapGains(src, N);
}

function exportEnvelope(patterns: Array<{ id: string; name: string; version?: number; createdAt: string; updatedAt: string; tags?: string[]; gains: Array<number | null> }>) {
    return {
        format: 'spectr.patterns',
        version: 1,
        exportedAt: new Date().toISOString(),
        count: patterns.length,
        patterns: patterns.map(p => ({
            id: p.id, name: p.name, source: 'user',
            version: p.version || 1,
            createdAt: p.createdAt, updatedAt: p.updatedAt,
            tags: p.tags || [], gains: p.gains,
        })),
    };
}

function parseEnvelope(obj: unknown): { patterns: unknown[]; errors: string[] } {
    const errors: string[] = [];
    if (!obj || typeof obj !== 'object') return { patterns: [], errors: ['not a JSON object'] };
    const o = obj as Record<string, unknown>;
    let list: Array<Record<string, unknown>>;
    if (o.format === 'spectr.patterns' && Array.isArray(o.patterns)) list = o.patterns as Array<Record<string, unknown>>;
    else if (Array.isArray(obj)) list = obj as Array<Record<string, unknown>>;
    else if (o.gains) list = [o];
    else return { patterns: [], errors: ['unrecognized structure'] };

    const out: unknown[] = [];
    for (const p of list) {
        if (!p || !Array.isArray(p.gains)) { errors.push('skipped: missing gains'); continue; }
        let gains = (p.gains as Array<number | null>).map(v => (v === null ? -Infinity : Number(v)));
        if (gains.length !== CANONICAL_RES) gains = remapGains(gains, CANONICAL_RES);
        out.push({
            id: 'user:' + Math.random().toString(36).slice(2, 10) + Date.now().toString(36),
            name: String(p.name || 'Imported').slice(0, 64),
            source: 'user', version: 1,
            createdAt: p.createdAt || new Date().toISOString(),
            updatedAt: new Date().toISOString(),
            tags: Array.isArray(p.tags) ? p.tags : [],
            gains: gains.map(v => (v === -Infinity ? null : round3(v))),
        });
    }
    return { patterns: out, errors };
}

// ---------- store: in-memory (no localStorage in QuickJS) ----------
// The real version uses localStorage('spectr.patterns.v1'). When Spectr
// wires its C++ StateStore through a binding, this becomes real
// persistence; for now patterns survive the session, not the process.
let _patternStore: unknown[] = [];
let _defaultId = 'factory:flat';
function loadStore(): unknown[] { return _patternStore.slice(); }
function saveStore(list: unknown[]): void { _patternStore = list.slice(); }
function loadDefaultId(): string { return _defaultId; }
function saveDefaultId(id: string): void { _defaultId = id; }

winAny.Spectr = winAny.Spectr ?? {
    FACTORY_PATTERNS,
    CANONICAL_RES,
    PATTERN_SCHEMA_VERSION: 1,
    makeUserPattern,
    resolveGains,
    remapGains,
    exportEnvelope,
    parseEnvelope,
    loadStore, saveStore,
    loadDefaultId, saveDefaultId,
    factoryGains,
    toCanonical, fromCanonical,
};

// ---------- SpectrSignal: synthesized visual-test voices ----------
// drawSpectrum() in FilterBank calls SpectrSignal.sample(logF, t) for
// every bin every frame. Returns 0..1. Multiple overlapping voices
// (bass, mids, vocal formants, air, transient bursts) so the ghosted
// spectrum behind the bands has character even with no real audio.
{
    const FMIN = 20;
    const FMAX = 20000;

    const bump = (logF: number, center: number, width: number, amp: number): number => {
        const d = (logF - center) / width;
        return amp * Math.exp(-d * d);
    };
    const bassVoice = (logF: number, t: number): number => {
        const beat = 0.5 + 0.5 * Math.sin(t * Math.PI * 2 * 1.6);
        const kick = Math.max(0, 1 - ((t * 1.6) % 1) * 4);
        return bump(logF, Math.log10(70),  0.18, 0.85 * (0.35 + 0.65 * beat))
             + bump(logF, Math.log10(55),  0.12, 0.95 * kick)
             + bump(logF, Math.log10(110), 0.15, 0.55 * beat);
    };
    const midVoice = (logF: number, t: number): number => {
        const sweep = Math.log10(300 + 400 * (0.5 + 0.5 * Math.sin(t * 0.25)));
        return bump(logF, sweep,       0.22, 0.55)
             + bump(logF, sweep + 0.3, 0.18, 0.40)
             + bump(logF, sweep + 0.6, 0.14, 0.28);
    };
    const vocalVoice = (logF: number, t: number): number => {
        const wob = 0.04 * Math.sin(t * 1.3);
        return bump(logF, Math.log10(500)  + wob,       0.10, 0.70)
             + bump(logF, Math.log10(1500) + wob * 1.4, 0.12, 0.60)
             + bump(logF, Math.log10(2800) + wob * 0.8, 0.14, 0.50);
    };
    const airVoice = (logF: number, t: number): number => {
        const shimmer = 0.5 + 0.5 * Math.sin(t * 2.2 + logF * 6);
        return bump(logF, Math.log10(8000),  0.35, 0.45 * shimmer)
             + bump(logF, Math.log10(14000), 0.30, 0.30 * shimmer);
    };
    const hiss = (logF: number, t: number, seed: number): number => {
        const v  = Math.sin(logF * 137.9 + t * 3.1 + seed * 11.7) * 0.5 + 0.5;
        const v2 = Math.sin(logF * 71.3  + t * 5.7 + seed * 3.4)  * 0.5 + 0.5;
        return v * v2 * 0.35;
    };

    winAny.SpectrSignal = winAny.SpectrSignal ?? {
        sample(logF: number, t: number, scenarioMix = 1): number {
            const base =
                bassVoice(logF, t) +
                midVoice(logF, t) +
                vocalVoice(logF, t) * 0.9 +
                airVoice(logF, t) +
                hiss(logF, t, 0.7) * 0.6;
            const lf = Math.pow(10, logF);
            let shape = 1;
            if (lf < 40)    shape *= Math.max(0, (lf - 20) / 20);
            if (lf > 14000) shape *= Math.max(0, 1 - (lf - 14000) / 8000);
            const burstPhase = (t % 2.0) / 2.0;
            const burst = burstPhase < 0.08 ? (1 - burstPhase / 0.08) * 0.8 : 0;
            const burstSpec = burst * (0.3 + 0.7 * Math.exp(-Math.pow((logF - 3.3) / 0.5, 2)));
            const v = (base + burstSpec) * shape * scenarioMix;
            return Math.max(0, Math.min(1, v * 0.55));
        },
        makePeakHold(n: number, decay = 0.92) {
            const peaks = new Float32Array(n);
            return function(arr: ArrayLike<number>): Float32Array {
                for (let i = 0; i < n; i++) peaks[i] = Math.max(arr[i], peaks[i] * decay);
                return peaks;
            };
        },
    };

    winAny.SpectrFreq = winAny.SpectrFreq ?? {
        FMIN, FMAX,
        logMin: Math.log10(FMIN),
        logMax: Math.log10(FMAX),
        posToFreq(pos: number, lmin: number, lmax: number): number {
            return Math.pow(10, lmin + (lmax - lmin) * pos);
        },
        freqToPos(f: number, lmin: number, lmax: number): number {
            return (Math.log10(f) - lmin) / (lmax - lmin);
        },
        // FilterBank/Chrome both read `bandFrequencyHz(i, n)` — keep
        // the legacy entry point too so consumers using either survive.
        bandFrequencyHz(i: number, n: number): number {
            const t = i / Math.max(1, n - 1);
            return FMIN * Math.pow(FMAX / FMIN, t);
        },
        fmt(f: number): string {
            if (f >= 1000) return (f / 1000).toFixed(f >= 10000 ? 1 : 2).replace(/\.?0+$/, '') + 'k';
            return f.toFixed(f < 100 ? 1 : 0);
        },
    };
}

winAny.SpectrMetaphors = winAny.SpectrMetaphors ?? {
    spectrum: { label: 'spectrum' },
};
winAny.SpectrThemes = winAny.SpectrThemes ?? {
    dark: { bg: '#05070a', fg: '#e8edf2', dim: '#6b7380' },
};
