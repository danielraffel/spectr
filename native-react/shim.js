// QuickJS shim — runs FIRST (prepended to dist/editor.js by build.sh).
// Pulp's bridge installs Element.prototype.* via kDomOpsInit but does
// NOT define `document` itself. The bundle IIFE that follows references
// `document` directly (e.g. `document.getElementById('tweak-defaults')`)
// and QuickJS's strict bare-reference resolution wants the global to
// exist before the IIFE evaluates. So we set up window/document AT THE
// TOP LEVEL here, not inside an IIFE.

// Direct probe — should print to stderr if __spectrLog wired
try {
    if (typeof globalThis.__spectrLog === 'function') {
        globalThis.__spectrLog('SHIM_LOAD: __spectrLog wired ok');
    } else {
        // Try alternative log pathway via setText on a known widget — no good fallback
        // available at QuickJS load time. Will know it's broken.
    }
} catch (_e) {}

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
    // Real setTimeout: enqueue callback in __setTimeoutQueue__ for the
    // bridge's frame pump to drain. Without this React's scheduler
    // (which uses setTimeout for deferred work like useEffect) silently
    // never fires its callbacks. Found via spectr#28 tracing.
    if (typeof g.setTimeout !== 'function' || g.setTimeout.toString().indexOf('return -1') >= 0) {
        g.__setTimeoutQueue__ = g.__setTimeoutQueue__ || [];
        g.__setTimeoutNextId__ = g.__setTimeoutNextId__ || 1;
        g.setTimeout = function (fn, delay) {
            var id = g.__setTimeoutNextId__++;
            g.__setTimeoutQueue__.push({ id: id, fn: fn, fireAt: 0 });
            // Hook into bridge's frame pump so deferred callbacks fire.
            // We piggyback on rAF — the next service_frame_callbacks
            // will drain.
            if (typeof g.__requestFrame__ === 'function' &&
                typeof g.__frameCallbacks__ === 'object' &&
                typeof g.__frameNextId__ === 'number') {
                var fid = g.__frameNextId__++;
                g.__frameCallbacks__[fid] = function () { g.__drainTimeouts__(); };
                g.__requestFrame__(fid);
            }
            return id;
        };
        g.__drainTimeouts__ = function () {
            var q = g.__setTimeoutQueue__;
            g.__setTimeoutQueue__ = [];
            for (var i = 0; i < q.length; i++) {
                try { q[i].fn(); } catch (e) {
                    if (g.__spectrLog) g.__spectrLog('[setTimeout-error] ' + (e && e.message || e));
                }
            }
        };
    }
    if (typeof g.clearTimeout !== 'function') g.clearTimeout = function () {};
    if (typeof g.queueMicrotask !== 'function') {
        g.queueMicrotask = function (fn) { try { fn(); } catch (e) {} };
    }
    // MessageChannel shim — React's scheduler uses
    // `new MessageChannel().port2.postMessage(...)` to post tasks.
    // QuickJS has no MessageChannel; without this React's effects
    // never fire (and FilterBank's RAF never sets up). Found via
    // spectr#28 trace — see editor.js:2494 in the bundle.
    // We back the channel by setTimeout(0) which our setTimeout
    // shim pumps via the bridge frame loop.
    if (typeof g.MessageChannel === 'undefined') {
        g.MessageChannel = function MessageChannel() {
            var port1Listeners = [];
            var port2Listeners = [];
            this.port1 = {
                onmessage: null,
                postMessage: function (data) {
                    var listeners = port2Listeners.slice();
                    var onm = this.port1 && this.port1.onmessage;
                    g.setTimeout(function () {
                        for (var i = 0; i < listeners.length; i++) {
                            try { listeners[i]({ data: data }); } catch (_e) {}
                        }
                    }, 0);
                    void onm;
                },
                addEventListener: function (_t, fn) { port2Listeners.push(fn); },
                removeEventListener: function () {},
                close: function () {},
                start: function () {},
            };
            this.port2 = {
                onmessage: null,
                postMessage: function (data) {
                    var p1 = this.port1; void p1;
                    var port = this; // ref
                    g.setTimeout(function () {
                        // React reads `onmessage` directly. Fire whatever's set.
                        // The scheduler attaches via `port1.onmessage = handler`
                        // and posts on port2 — so we deliver to port1.onmessage.
                        var p1onm = (port._other && port._other.onmessage);
                        if (typeof p1onm === 'function') {
                            try { p1onm({ data: data }); } catch (_e) {}
                        }
                        for (var i = 0; i < port1Listeners.length; i++) {
                            try { port1Listeners[i]({ data: data }); } catch (_e) {}
                        }
                    }, 0);
                },
                addEventListener: function (_t, fn) { port1Listeners.push(fn); },
                removeEventListener: function () {},
                close: function () {},
                start: function () {},
            };
            // Cross-link so port2.postMessage can reach port1.onmessage.
            this.port1._other = this.port2;
            this.port2._other = this.port1;
        };
    }
    // MessageChannel — React's scheduler uses `new MessageChannel()` +
    // port2.postMessage() to schedule its task loop in browsers. QuickJS
    // doesn't have it natively, so without this shim React's scheduler
    // queues tasks but they never run (useEffect callbacks silently
    // suspended). Found via spectr#28 tracing — bundle line 1884.
    // We synthesize via the same setTimeout queue that drains on the
    // bridge frame pump.
    if (typeof g.MessageChannel === 'undefined') {
        g.MessageChannel = function () {
            var listeners = [];
            var port1 = {
                addEventListener: function (_t, fn) { listeners.push(fn); },
                removeEventListener: function (_t, fn) {
                    var i = listeners.indexOf(fn);
                    if (i >= 0) listeners.splice(i, 1);
                },
                set onmessage(fn) { listeners.push(fn); },
                start: function () {},
                close: function () { listeners.length = 0; },
            };
            var port2 = {
                postMessage: function (data) {
                    // Defer via setTimeout so React's scheduler-loop pattern
                    // (port2.postMessage triggers port1.onmessage) works
                    // through our frame-pumped setTimeout queue.
                    g.setTimeout(function () {
                        for (var i = 0; i < listeners.length; i++) {
                            try { listeners[i]({ data: data }); }
                            catch (e) {
                                if (g.__spectrLog) g.__spectrLog('[mc-error] ' + (e && e.message || e));
                            }
                        }
                    }, 0);
                },
                close: function () {},
            };
            this.port1 = port1;
            this.port2 = port2;
        };
    }
    // QuickJS doesn't expose console. NativeEditorView (C++) registers
    // a `__spectrLog` global that writes to stderr. Wire console.* to
    // it so the bundle's diagnostic logs are visible during launch.
    // (Falls back to a no-op `print` for environments like
    // pulp-screenshot that don't register __spectrLog.)
    if (typeof g.console === 'undefined' || typeof g.console.log !== 'function') {
        var p = (typeof g.__spectrLog === 'function') ? g.__spectrLog
              : (typeof g.print === 'function') ? g.print
              : function () {};
        g.console = {
            log: function () {
                var s = ''; for (var i = 0; i < arguments.length; i++) {
                    if (i) s += ' ';
                    s += String(arguments[i]);
                }
                p('[js] ' + s);
            },
            error: function () { var s = ''; for (var i = 0; i < arguments.length; i++) { if (i) s += ' '; s += String(arguments[i]); } p('[js:error] ' + s); },
            warn:  function () { var s = ''; for (var i = 0; i < arguments.length; i++) { if (i) s += ' '; s += String(arguments[i]); } p('[js:warn] ' + s); },
        };
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

    // requestAnimationFrame wiring. Pulp's bridge already exposes
    // __requestFrame__ (C++) + __frameCallbacks__/__invokeFrame__ (JS
    // preamble loaded on first load_script). We just need to
    // register the standard rAF API on top: enqueue cb in
    // __frameCallbacks__, queue via __requestFrame__, and let
    // bridge.service_frame_callbacks() (called from
    // NativeEditorView::paint) drive the pump.
    var _rafCount = 0;
    try { if (g.__spectrLog) g.__spectrLog('shim: rAF was=' + typeof w.requestAnimationFrame +
                                           ' reqFrame=' + typeof g.__requestFrame__ +
                                           ' frameCB=' + typeof g.__frameCallbacks__); } catch (_e) {}
    // Always re-wire — even if a previous synthetic rAF was set up,
    // we want the bridge-driven one for spectr#28.
    {
        var hasFP = typeof g.__requestFrame__ === 'function' &&
                    typeof g.__frameCallbacks__ === 'object' &&
                    typeof g.__frameNextId__ === 'number';
        try { if (g.__spectrLog) g.__spectrLog('rAF wire: bridge-frame=' + hasFP +
              ' reqFn=' + (typeof g.__requestFrame__) +
              ' cbObj=' + (typeof g.__frameCallbacks__) +
              ' idNum=' + (typeof g.__frameNextId__)); } catch (_e) {}
        if (hasFP) {
            w.requestAnimationFrame = function (cb) {
                _rafCount++;
                if (_rafCount <= 8) {
                    try { if (g.__spectrLog) g.__spectrLog('[raf] #' + _rafCount + ' enqueue'); } catch (_e) {}
                }
                var id = g.__frameNextId__++;
                g.__frameCallbacks__[id] = function () {
                    try { cb(g.performance.now()); } catch (e) {
                        try { if (g.__spectrLog) g.__spectrLog('[raf:cb-error] ' + (e && e.message || e)); } catch (_e) {}
                    }
                };
                g.__requestFrame__(id);
                return id;
            };
            w.cancelAnimationFrame = function (id) {
                if (typeof g.__cancelFrame__ === 'function') g.__cancelFrame__(id);
                delete g.__frameCallbacks__[id];
            };
        } else {
            // pulp-screenshot or non-bridge env — synthetic 3-tick burst
            // so simple rAF chains complete during one-shot eval.
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
    }
})();
