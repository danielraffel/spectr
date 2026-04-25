// QuickJS shim — same as @pulp/react's smoke fixture shim. Required
// because Pulp's JS engine doesn't expose setTimeout / queueMicrotask
// out of the box, and react-reconciler references them at module load.
(function () {
    var g = globalThis;
    if (typeof g.setTimeout !== 'function') {
        g.setTimeout = function (fn) { return -1; };
    }
    if (typeof g.clearTimeout !== 'function') {
        g.clearTimeout = function () {};
    }
    if (typeof g.queueMicrotask !== 'function') {
        g.queueMicrotask = function (fn) {
            try { fn(); } catch (e) { /* swallow */ }
        };
    }
    if (typeof g.console === 'undefined') {
        g.console = { log: function () {}, error: function () {}, warn: function () {} };
    }
})();
