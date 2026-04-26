// QuickJS shim — runs FIRST (prepended to dist/editor.js by build.sh).
// Pulp's bridge installs Element.prototype.* via kDomOpsInit but does
// NOT define `document` itself. The bundle IIFE that follows references
// `document` directly (e.g. `document.getElementById('tweak-defaults')`)
// and QuickJS's strict bare-reference resolution wants the global to
// exist before the IIFE evaluates. So we set up window/document AT THE
// TOP LEVEL here, not inside an IIFE.

var __spectrTweakDefaults = JSON.stringify({
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

(function () {
    var g = globalThis;
    if (typeof g.setTimeout !== 'function') g.setTimeout = function () { return -1; };
    if (typeof g.clearTimeout !== 'function') g.clearTimeout = function () {};
    if (typeof g.queueMicrotask !== 'function') {
        g.queueMicrotask = function (fn) { try { fn(); } catch (e) {} };
    }
    if (typeof g.console === 'undefined') {
        g.console = { log: function () {}, error: function () {}, warn: function () {} };
    }
    if (typeof g.performance === 'undefined') g.performance = { now: function () { return Date.now(); } };
    if (typeof g.window === 'undefined') g.window = g;

    // Document polyfill — wrap any existing one and short-circuit
    // 'tweak-defaults' to a known JSON blob so App() can boot.
    var origDoc = g.document;
    var origGetById = origDoc && origDoc.getElementById && origDoc.getElementById.bind(origDoc);
    var doc = origDoc || {};
    doc.getElementById = function (id) {
        if (id === 'tweak-defaults') return { textContent: __spectrTweakDefaults };
        if (origGetById) return origGetById(id);
        return null;
    };
    if (typeof doc.createElement !== 'function') {
        doc.createElement = function () {
            return {
                style: {}, classList: { add: function () {}, remove: function () {} },
                appendChild: function () {}, addEventListener: function () {},
                removeEventListener: function () {},
            };
        };
    }
    if (typeof doc.addEventListener !== 'function') {
        doc.addEventListener = function () {};
        doc.removeEventListener = function () {};
    }
    g.document = doc;

    // Window-level extras
    var w = g.window;
    if (w.devicePixelRatio === undefined) w.devicePixelRatio = 2;
    if (typeof w.addEventListener !== 'function') {
        w.addEventListener = function () {};
        w.removeEventListener = function () {};
    }
    w.innerWidth = 1320;
    w.innerHeight = 860;
    w.parent = w;

    // Synthetic requestAnimationFrame for one-shot pulp-screenshot.
    // In standalone Spectr the bridge's service_frame_callbacks() takes
    // precedence — we don't override an existing rAF.
    if (typeof w.requestAnimationFrame !== 'function') {
        var ticks = 0;
        var frameId = 0;
        w.requestAnimationFrame = function (cb) {
            frameId++;
            if (ticks < 3) {
                ticks++;
                try { cb(g.performance.now()); } catch (e) {}
            }
            return frameId;
        };
        w.cancelAnimationFrame = function () {};
    }
})();
