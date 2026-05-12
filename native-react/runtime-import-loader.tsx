// runtime-import-loader.tsx — Spectr's SPECTR_RUNTIME_IMPORT entry point.
//
// Bundled with esbuild into dist/runtime-import-bundle.js and embedded
// alongside resources/editor.html. Invoked from NativeEditorView's
// constructor once the WidgetBridge has registered the runtime-import
// native handlers (install_runtime_import_handlers()).
//
// Flow:
//   1. host-shims.ts side-effects install window.Spectr / SpectrFreq /
//      SpectrSignal / SpectrMetaphors / SpectrThemes globals + the
//      React + ReactDOM globals (so legacy `const { useState } = React`
//      destructuring inside editor.html resolves). We re-import these
//      shims for their side-effects, identical to editor.tsx's prelude.
//   2. Pull the editor.html source via the C++-registered native
//      `__spectrLoadEditorHtml()`.
//   3. Call `renderFromDesign(html, undefined, { hostReact, bindings })`
//      from @pulp/react/runtime-import. That helper:
//        - parses the Claude bundle envelope from the HTML,
//        - captures ReactDOM.createRoot().render() into a Pulp container,
//        - installs our `bindings` on globalThis + window,
//        - settles useEffect / rAF / timer queues.
//
// On error the renderFromDesign() promise resolves with status='failed'
// and `lastError` set; we log via __spectrLog so the C++ side can see it.

import React from 'react';
import { renderFromDesign } from '@pulp/react/runtime-import';

// Re-use the same shim setup as the offline path. host-shims.ts is
// side-effect-only at module init — importing it wires the window.Spectr.*
// helpers + the React/ReactDOM globals editor.html legacy code expects.
// Side note: renderFromDesign installs its own ReactDOM capture shim
// AFTER host-shims runs, so the renderer-DOM is intercepted correctly.
import './host-shims';

const g = globalThis as Record<string, unknown>;

function logViaSpectr(msg: string): void {
    const log = g.__spectrLog as ((s: string) => void) | undefined;
    if (typeof log === 'function') log(msg);
    else if (typeof console !== 'undefined' && console.log) console.log(msg);
}

async function boot(): Promise<void> {
    const loadHtml = g.__spectrLoadEditorHtml as (() => string) | undefined;
    if (typeof loadHtml !== 'function') {
        logViaSpectr('[runtime-import-loader] __spectrLoadEditorHtml not registered');
        return;
    }
    const html = loadHtml();
    if (!html || typeof html !== 'string') {
        logViaSpectr('[runtime-import-loader] editor.html missing or empty');
        return;
    }
    logViaSpectr(`[runtime-import-loader] loaded editor.html (${html.length} bytes)`);

    try {
        // host-shims has already deposited Spectr.* on globalThis/window;
        // we pass them explicitly through `bindings` so renderFromDesign
        // snapshots + restores them around the bundle eval. Same surface,
        // explicit ownership.
        const bindings: Record<string, unknown> = {};
        for (const key of [
            'Spectr', 'SpectrFreq', 'SpectrSignal',
            'SpectrMetaphors', 'SpectrThemes',
            '__spectrLog', '__spectrumTick',
        ]) {
            if (key in g) bindings[key] = g[key];
        }

        // Enable JS-side trace when the C++ side did. PULP_RUNTIME_IMPORT_TRACE=1
        // seeds globalThis.__pulpRuntimeTrace__ from C++; we mirror via
        // opts.trace so the JS-side pulpTrace() calls also fire.
        const traceOn = Array.isArray(
            (g as Record<string, unknown>).__pulpRuntimeTrace__);

        const handle = await renderFromDesign(html, undefined, {
            hostReact: React,
            bindings,
            // Keep our host-shims globals around after the design
            // bundle returns — Spectr's bridge keeps calling them
            // from the analyzer-tick path.
            persistBindings: true,
            source: 'claude',
            trace: traceOn,
            // Bump settle rounds to the C++-side cap (64). The default
            // (8) was enough for the initial mount but not for the
            // bundle's post-mount useEffect → setState → larger
            // re-render seen in the live test 2026-05-11.
            settleRounds: 1,
            onError: (e: unknown) => {
                logViaSpectr('[runtime-import-loader] renderFromDesign error: ' +
                    ((e as { message?: string })?.message ?? String(e)));
            },
        });
        logViaSpectr('[runtime-import-loader] status=' + handle.status +
            (handle.lastError ? ' err=' + handle.lastError : ''));

        // Phase 7 — dump the structured trace so external tools can
        // consume it without depending on this stderr format.
        if (traceOn) {
            const tr = (g as Record<string, unknown>).__pulpRuntimeTrace__;
            if (Array.isArray(tr)) {
                const dropped = (g as Record<string, unknown>).__pulpRuntimeTrace_dropped__ as number | undefined;
                logViaSpectr('[runtime-import-trace] events=' + tr.length +
                    (dropped ? ' dropped=' + dropped : ''));
                // Emit each event on its own line so grep can locate them.
                for (let i = 0; i < tr.length; i++) {
                    try {
                        logViaSpectr('[trace] ' + JSON.stringify(tr[i]));
                    } catch {
                        logViaSpectr('[trace] ' + i + ' (unserializable)');
                    }
                }
            }
        }
    } catch (e) {
        logViaSpectr('[runtime-import-loader] threw: ' +
            ((e as { message?: string })?.message ?? String(e)));
    }
}

// Kick off the boot. QuickJS resolves the microtask queue when
// service_frame_callbacks pumps the loop, which happens immediately
// after this script evaluates via __pulpRuntimeSettle__ inside
// renderFromDesign itself.
void boot();
