(() => {
  var __create = Object.create;
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __getProtoOf = Object.getPrototypeOf;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __commonJS = (cb, mod) => function __require() {
    return mod || (0, cb[__getOwnPropNames(cb)[0]])((mod = { exports: {} }).exports, mod), mod.exports;
  };
  var __copyProps = (to, from, except, desc) => {
    if (from && typeof from === "object" || typeof from === "function") {
      for (let key of __getOwnPropNames(from))
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
    }
    return to;
  };
  var __toESM = (mod, isNodeMode, target) => (target = mod != null ? __create(__getProtoOf(mod)) : {}, __copyProps(
    // If the importer is in node compatibility mode or this is not an ESM
    // file that has been converted to a CommonJS file using a Babel-
    // compatible transform (i.e. "__esModule" has not been set), then set
    // "default" to the CommonJS "module.exports" for node compatibility.
    isNodeMode || !mod || !mod.__esModule ? __defProp(target, "default", { value: mod, enumerable: true }) : target,
    mod
  ));

  // ../pulp/packages/pulp-react/node_modules/react/cjs/react.production.min.js
  var require_react_production_min = __commonJS({
    "../pulp/packages/pulp-react/node_modules/react/cjs/react.production.min.js"(exports) {
      "use strict";
      var l = Symbol.for("react.element");
      var n = Symbol.for("react.portal");
      var p = Symbol.for("react.fragment");
      var q = Symbol.for("react.strict_mode");
      var r = Symbol.for("react.profiler");
      var t = Symbol.for("react.provider");
      var u = Symbol.for("react.context");
      var v = Symbol.for("react.forward_ref");
      var w = Symbol.for("react.suspense");
      var x = Symbol.for("react.memo");
      var y = Symbol.for("react.lazy");
      var z = Symbol.iterator;
      function A(a) {
        if (null === a || "object" !== typeof a) return null;
        a = z && a[z] || a["@@iterator"];
        return "function" === typeof a ? a : null;
      }
      var B = { isMounted: function() {
        return false;
      }, enqueueForceUpdate: function() {
      }, enqueueReplaceState: function() {
      }, enqueueSetState: function() {
      } };
      var C = Object.assign;
      var D = {};
      function E(a, b, e) {
        this.props = a;
        this.context = b;
        this.refs = D;
        this.updater = e || B;
      }
      E.prototype.isReactComponent = {};
      E.prototype.setState = function(a, b) {
        if ("object" !== typeof a && "function" !== typeof a && null != a) throw Error("setState(...): takes an object of state variables to update or a function which returns an object of state variables.");
        this.updater.enqueueSetState(this, a, b, "setState");
      };
      E.prototype.forceUpdate = function(a) {
        this.updater.enqueueForceUpdate(this, a, "forceUpdate");
      };
      function F() {
      }
      F.prototype = E.prototype;
      function G(a, b, e) {
        this.props = a;
        this.context = b;
        this.refs = D;
        this.updater = e || B;
      }
      var H = G.prototype = new F();
      H.constructor = G;
      C(H, E.prototype);
      H.isPureReactComponent = true;
      var I = Array.isArray;
      var J = Object.prototype.hasOwnProperty;
      var K = { current: null };
      var L = { key: true, ref: true, __self: true, __source: true };
      function M(a, b, e) {
        var d, c = {}, k = null, h = null;
        if (null != b) for (d in void 0 !== b.ref && (h = b.ref), void 0 !== b.key && (k = "" + b.key), b) J.call(b, d) && !L.hasOwnProperty(d) && (c[d] = b[d]);
        var g5 = arguments.length - 2;
        if (1 === g5) c.children = e;
        else if (1 < g5) {
          for (var f = Array(g5), m = 0; m < g5; m++) f[m] = arguments[m + 2];
          c.children = f;
        }
        if (a && a.defaultProps) for (d in g5 = a.defaultProps, g5) void 0 === c[d] && (c[d] = g5[d]);
        return { $$typeof: l, type: a, key: k, ref: h, props: c, _owner: K.current };
      }
      function N(a, b) {
        return { $$typeof: l, type: a.type, key: b, ref: a.ref, props: a.props, _owner: a._owner };
      }
      function O(a) {
        return "object" === typeof a && null !== a && a.$$typeof === l;
      }
      function escape(a) {
        var b = { "=": "=0", ":": "=2" };
        return "$" + a.replace(/[=:]/g, function(a2) {
          return b[a2];
        });
      }
      var P = /\/+/g;
      function Q(a, b) {
        return "object" === typeof a && null !== a && null != a.key ? escape("" + a.key) : b.toString(36);
      }
      function R(a, b, e, d, c) {
        var k = typeof a;
        if ("undefined" === k || "boolean" === k) a = null;
        var h = false;
        if (null === a) h = true;
        else switch (k) {
          case "string":
          case "number":
            h = true;
            break;
          case "object":
            switch (a.$$typeof) {
              case l:
              case n:
                h = true;
            }
        }
        if (h) return h = a, c = c(h), a = "" === d ? "." + Q(h, 0) : d, I(c) ? (e = "", null != a && (e = a.replace(P, "$&/") + "/"), R(c, b, e, "", function(a2) {
          return a2;
        })) : null != c && (O(c) && (c = N(c, e + (!c.key || h && h.key === c.key ? "" : ("" + c.key).replace(P, "$&/") + "/") + a)), b.push(c)), 1;
        h = 0;
        d = "" === d ? "." : d + ":";
        if (I(a)) for (var g5 = 0; g5 < a.length; g5++) {
          k = a[g5];
          var f = d + Q(k, g5);
          h += R(k, b, e, f, c);
        }
        else if (f = A(a), "function" === typeof f) for (a = f.call(a), g5 = 0; !(k = a.next()).done; ) k = k.value, f = d + Q(k, g5++), h += R(k, b, e, f, c);
        else if ("object" === k) throw b = String(a), Error("Objects are not valid as a React child (found: " + ("[object Object]" === b ? "object with keys {" + Object.keys(a).join(", ") + "}" : b) + "). If you meant to render a collection of children, use an array instead.");
        return h;
      }
      function S(a, b, e) {
        if (null == a) return a;
        var d = [], c = 0;
        R(a, d, "", "", function(a2) {
          return b.call(e, a2, c++);
        });
        return d;
      }
      function T(a) {
        if (-1 === a._status) {
          var b = a._result;
          b = b();
          b.then(function(b2) {
            if (0 === a._status || -1 === a._status) a._status = 1, a._result = b2;
          }, function(b2) {
            if (0 === a._status || -1 === a._status) a._status = 2, a._result = b2;
          });
          -1 === a._status && (a._status = 0, a._result = b);
        }
        if (1 === a._status) return a._result.default;
        throw a._result;
      }
      var U = { current: null };
      var V = { transition: null };
      var W = { ReactCurrentDispatcher: U, ReactCurrentBatchConfig: V, ReactCurrentOwner: K };
      function X() {
        throw Error("act(...) is not supported in production builds of React.");
      }
      exports.Children = { map: S, forEach: function(a, b, e) {
        S(a, function() {
          b.apply(this, arguments);
        }, e);
      }, count: function(a) {
        var b = 0;
        S(a, function() {
          b++;
        });
        return b;
      }, toArray: function(a) {
        return S(a, function(a2) {
          return a2;
        }) || [];
      }, only: function(a) {
        if (!O(a)) throw Error("React.Children.only expected to receive a single React element child.");
        return a;
      } };
      exports.Component = E;
      exports.Fragment = p;
      exports.Profiler = r;
      exports.PureComponent = G;
      exports.StrictMode = q;
      exports.Suspense = w;
      exports.__SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED = W;
      exports.act = X;
      exports.cloneElement = function(a, b, e) {
        if (null === a || void 0 === a) throw Error("React.cloneElement(...): The argument must be a React element, but you passed " + a + ".");
        var d = C({}, a.props), c = a.key, k = a.ref, h = a._owner;
        if (null != b) {
          void 0 !== b.ref && (k = b.ref, h = K.current);
          void 0 !== b.key && (c = "" + b.key);
          if (a.type && a.type.defaultProps) var g5 = a.type.defaultProps;
          for (f in b) J.call(b, f) && !L.hasOwnProperty(f) && (d[f] = void 0 === b[f] && void 0 !== g5 ? g5[f] : b[f]);
        }
        var f = arguments.length - 2;
        if (1 === f) d.children = e;
        else if (1 < f) {
          g5 = Array(f);
          for (var m = 0; m < f; m++) g5[m] = arguments[m + 2];
          d.children = g5;
        }
        return { $$typeof: l, type: a.type, key: c, ref: k, props: d, _owner: h };
      };
      exports.createContext = function(a) {
        a = { $$typeof: u, _currentValue: a, _currentValue2: a, _threadCount: 0, Provider: null, Consumer: null, _defaultValue: null, _globalName: null };
        a.Provider = { $$typeof: t, _context: a };
        return a.Consumer = a;
      };
      exports.createElement = M;
      exports.createFactory = function(a) {
        var b = M.bind(null, a);
        b.type = a;
        return b;
      };
      exports.createRef = function() {
        return { current: null };
      };
      exports.forwardRef = function(a) {
        return { $$typeof: v, render: a };
      };
      exports.isValidElement = O;
      exports.lazy = function(a) {
        return { $$typeof: y, _payload: { _status: -1, _result: a }, _init: T };
      };
      exports.memo = function(a, b) {
        return { $$typeof: x, type: a, compare: void 0 === b ? null : b };
      };
      exports.startTransition = function(a) {
        var b = V.transition;
        V.transition = {};
        try {
          a();
        } finally {
          V.transition = b;
        }
      };
      exports.unstable_act = X;
      exports.useCallback = function(a, b) {
        return U.current.useCallback(a, b);
      };
      exports.useContext = function(a) {
        return U.current.useContext(a);
      };
      exports.useDebugValue = function() {
      };
      exports.useDeferredValue = function(a) {
        return U.current.useDeferredValue(a);
      };
      exports.useEffect = function(a, b) {
        return U.current.useEffect(a, b);
      };
      exports.useId = function() {
        return U.current.useId();
      };
      exports.useImperativeHandle = function(a, b, e) {
        return U.current.useImperativeHandle(a, b, e);
      };
      exports.useInsertionEffect = function(a, b) {
        return U.current.useInsertionEffect(a, b);
      };
      exports.useLayoutEffect = function(a, b) {
        return U.current.useLayoutEffect(a, b);
      };
      exports.useMemo = function(a, b) {
        return U.current.useMemo(a, b);
      };
      exports.useReducer = function(a, b, e) {
        return U.current.useReducer(a, b, e);
      };
      exports.useRef = function(a) {
        return U.current.useRef(a);
      };
      exports.useState = function(a) {
        return U.current.useState(a);
      };
      exports.useSyncExternalStore = function(a, b, e) {
        return U.current.useSyncExternalStore(a, b, e);
      };
      exports.useTransition = function() {
        return U.current.useTransition();
      };
      exports.version = "18.3.1";
    }
  });

  // ../pulp/packages/pulp-react/node_modules/react/index.js
  var require_react = __commonJS({
    "../pulp/packages/pulp-react/node_modules/react/index.js"(exports, module) {
      "use strict";
      if (true) {
        module.exports = require_react_production_min();
      } else {
        module.exports = null;
      }
    }
  });

  // ../pulp/packages/pulp-react/node_modules/scheduler/cjs/scheduler.production.min.js
  var require_scheduler_production_min = __commonJS({
    "../pulp/packages/pulp-react/node_modules/scheduler/cjs/scheduler.production.min.js"(exports) {
      "use strict";
      function f(a, b) {
        var c = a.length;
        a.push(b);
        a: for (; 0 < c; ) {
          var d = c - 1 >>> 1, e = a[d];
          if (0 < g5(e, b)) a[d] = b, a[c] = e, c = d;
          else break a;
        }
      }
      function h(a) {
        return 0 === a.length ? null : a[0];
      }
      function k(a) {
        if (0 === a.length) return null;
        var b = a[0], c = a.pop();
        if (c !== b) {
          a[0] = c;
          a: for (var d = 0, e = a.length, w = e >>> 1; d < w; ) {
            var m = 2 * (d + 1) - 1, C = a[m], n = m + 1, x = a[n];
            if (0 > g5(C, c)) n < e && 0 > g5(x, C) ? (a[d] = x, a[n] = c, d = n) : (a[d] = C, a[m] = c, d = m);
            else if (n < e && 0 > g5(x, c)) a[d] = x, a[n] = c, d = n;
            else break a;
          }
        }
        return b;
      }
      function g5(a, b) {
        var c = a.sortIndex - b.sortIndex;
        return 0 !== c ? c : a.id - b.id;
      }
      if ("object" === typeof performance && "function" === typeof performance.now) {
        l = performance;
        exports.unstable_now = function() {
          return l.now();
        };
      } else {
        p = Date, q = p.now();
        exports.unstable_now = function() {
          return p.now() - q;
        };
      }
      var l;
      var p;
      var q;
      var r = [];
      var t = [];
      var u = 1;
      var v = null;
      var y = 3;
      var z = false;
      var A = false;
      var B = false;
      var D = "function" === typeof setTimeout ? setTimeout : null;
      var E = "function" === typeof clearTimeout ? clearTimeout : null;
      var F = "undefined" !== typeof setImmediate ? setImmediate : null;
      "undefined" !== typeof navigator && void 0 !== navigator.scheduling && void 0 !== navigator.scheduling.isInputPending && navigator.scheduling.isInputPending.bind(navigator.scheduling);
      function G(a) {
        for (var b = h(t); null !== b; ) {
          if (null === b.callback) k(t);
          else if (b.startTime <= a) k(t), b.sortIndex = b.expirationTime, f(r, b);
          else break;
          b = h(t);
        }
      }
      function H(a) {
        B = false;
        G(a);
        if (!A) if (null !== h(r)) A = true, I(J);
        else {
          var b = h(t);
          null !== b && K(H, b.startTime - a);
        }
      }
      function J(a, b) {
        A = false;
        B && (B = false, E(L), L = -1);
        z = true;
        var c = y;
        try {
          G(b);
          for (v = h(r); null !== v && (!(v.expirationTime > b) || a && !M()); ) {
            var d = v.callback;
            if ("function" === typeof d) {
              v.callback = null;
              y = v.priorityLevel;
              var e = d(v.expirationTime <= b);
              b = exports.unstable_now();
              "function" === typeof e ? v.callback = e : v === h(r) && k(r);
              G(b);
            } else k(r);
            v = h(r);
          }
          if (null !== v) var w = true;
          else {
            var m = h(t);
            null !== m && K(H, m.startTime - b);
            w = false;
          }
          return w;
        } finally {
          v = null, y = c, z = false;
        }
      }
      var N = false;
      var O = null;
      var L = -1;
      var P = 5;
      var Q = -1;
      function M() {
        return exports.unstable_now() - Q < P ? false : true;
      }
      function R() {
        if (null !== O) {
          var a = exports.unstable_now();
          Q = a;
          var b = true;
          try {
            b = O(true, a);
          } finally {
            b ? S() : (N = false, O = null);
          }
        } else N = false;
      }
      var S;
      if ("function" === typeof F) S = function() {
        F(R);
      };
      else if ("undefined" !== typeof MessageChannel) {
        T = new MessageChannel(), U = T.port2;
        T.port1.onmessage = R;
        S = function() {
          U.postMessage(null);
        };
      } else S = function() {
        D(R, 0);
      };
      var T;
      var U;
      function I(a) {
        O = a;
        N || (N = true, S());
      }
      function K(a, b) {
        L = D(function() {
          a(exports.unstable_now());
        }, b);
      }
      exports.unstable_IdlePriority = 5;
      exports.unstable_ImmediatePriority = 1;
      exports.unstable_LowPriority = 4;
      exports.unstable_NormalPriority = 3;
      exports.unstable_Profiling = null;
      exports.unstable_UserBlockingPriority = 2;
      exports.unstable_cancelCallback = function(a) {
        a.callback = null;
      };
      exports.unstable_continueExecution = function() {
        A || z || (A = true, I(J));
      };
      exports.unstable_forceFrameRate = function(a) {
        0 > a || 125 < a ? console.error("forceFrameRate takes a positive int between 0 and 125, forcing frame rates higher than 125 fps is not supported") : P = 0 < a ? Math.floor(1e3 / a) : 5;
      };
      exports.unstable_getCurrentPriorityLevel = function() {
        return y;
      };
      exports.unstable_getFirstCallbackNode = function() {
        return h(r);
      };
      exports.unstable_next = function(a) {
        switch (y) {
          case 1:
          case 2:
          case 3:
            var b = 3;
            break;
          default:
            b = y;
        }
        var c = y;
        y = b;
        try {
          return a();
        } finally {
          y = c;
        }
      };
      exports.unstable_pauseExecution = function() {
      };
      exports.unstable_requestPaint = function() {
      };
      exports.unstable_runWithPriority = function(a, b) {
        switch (a) {
          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
            break;
          default:
            a = 3;
        }
        var c = y;
        y = a;
        try {
          return b();
        } finally {
          y = c;
        }
      };
      exports.unstable_scheduleCallback = function(a, b, c) {
        var d = exports.unstable_now();
        "object" === typeof c && null !== c ? (c = c.delay, c = "number" === typeof c && 0 < c ? d + c : d) : c = d;
        switch (a) {
          case 1:
            var e = -1;
            break;
          case 2:
            e = 250;
            break;
          case 5:
            e = 1073741823;
            break;
          case 4:
            e = 1e4;
            break;
          default:
            e = 5e3;
        }
        e = c + e;
        a = { id: u++, callback: b, priorityLevel: a, startTime: c, expirationTime: e, sortIndex: -1 };
        c > d ? (a.sortIndex = c, f(t, a), null === h(r) && a === h(t) && (B ? (E(L), L = -1) : B = true, K(H, c - d))) : (a.sortIndex = e, f(r, a), A || z || (A = true, I(J)));
        return a;
      };
      exports.unstable_shouldYield = M;
      exports.unstable_wrapCallback = function(a) {
        var b = y;
        return function() {
          var c = y;
          y = b;
          try {
            return a.apply(this, arguments);
          } finally {
            y = c;
          }
        };
      };
    }
  });

  // ../pulp/packages/pulp-react/node_modules/scheduler/index.js
  var require_scheduler = __commonJS({
    "../pulp/packages/pulp-react/node_modules/scheduler/index.js"(exports, module) {
      "use strict";
      if (true) {
        module.exports = require_scheduler_production_min();
      } else {
        module.exports = null;
      }
    }
  });

  // ../pulp/packages/pulp-react/node_modules/react-reconciler/cjs/react-reconciler.production.min.js
  var require_react_reconciler_production_min = __commonJS({
    "../pulp/packages/pulp-react/node_modules/react-reconciler/cjs/react-reconciler.production.min.js"(exports, module) {
      module.exports = function $$$reconciler($$$hostConfig) {
        var exports2 = {};
        "use strict";
        var aa = require_react(), ba = require_scheduler(), ca = Object.assign;
        function n(a) {
          for (var b = "https://reactjs.org/docs/error-decoder.html?invariant=" + a, c = 1; c < arguments.length; c++) b += "&args[]=" + encodeURIComponent(arguments[c]);
          return "Minified React error #" + a + "; visit " + b + " for the full message or use the non-minified dev environment for full errors and additional helpful warnings.";
        }
        var da = aa.__SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED, ea = Symbol.for("react.element"), fa = Symbol.for("react.portal"), ha = Symbol.for("react.fragment"), ia = Symbol.for("react.strict_mode"), ja = Symbol.for("react.profiler"), ka = Symbol.for("react.provider"), la = Symbol.for("react.context"), ma = Symbol.for("react.forward_ref"), na = Symbol.for("react.suspense"), oa = Symbol.for("react.suspense_list"), pa = Symbol.for("react.memo"), qa = Symbol.for("react.lazy");
        Symbol.for("react.scope");
        Symbol.for("react.debug_trace_mode");
        var ra = Symbol.for("react.offscreen");
        Symbol.for("react.legacy_hidden");
        Symbol.for("react.cache");
        Symbol.for("react.tracing_marker");
        var sa = Symbol.iterator;
        function ta(a) {
          if (null === a || "object" !== typeof a) return null;
          a = sa && a[sa] || a["@@iterator"];
          return "function" === typeof a ? a : null;
        }
        function ua(a) {
          if (null == a) return null;
          if ("function" === typeof a) return a.displayName || a.name || null;
          if ("string" === typeof a) return a;
          switch (a) {
            case ha:
              return "Fragment";
            case fa:
              return "Portal";
            case ja:
              return "Profiler";
            case ia:
              return "StrictMode";
            case na:
              return "Suspense";
            case oa:
              return "SuspenseList";
          }
          if ("object" === typeof a) switch (a.$$typeof) {
            case la:
              return (a.displayName || "Context") + ".Consumer";
            case ka:
              return (a._context.displayName || "Context") + ".Provider";
            case ma:
              var b = a.render;
              a = a.displayName;
              a || (a = b.displayName || b.name || "", a = "" !== a ? "ForwardRef(" + a + ")" : "ForwardRef");
              return a;
            case pa:
              return b = a.displayName || null, null !== b ? b : ua(a.type) || "Memo";
            case qa:
              b = a._payload;
              a = a._init;
              try {
                return ua(a(b));
              } catch (c) {
              }
          }
          return null;
        }
        function va(a) {
          var b = a.type;
          switch (a.tag) {
            case 24:
              return "Cache";
            case 9:
              return (b.displayName || "Context") + ".Consumer";
            case 10:
              return (b._context.displayName || "Context") + ".Provider";
            case 18:
              return "DehydratedFragment";
            case 11:
              return a = b.render, a = a.displayName || a.name || "", b.displayName || ("" !== a ? "ForwardRef(" + a + ")" : "ForwardRef");
            case 7:
              return "Fragment";
            case 5:
              return b;
            case 4:
              return "Portal";
            case 3:
              return "Root";
            case 6:
              return "Text";
            case 16:
              return ua(b);
            case 8:
              return b === ia ? "StrictMode" : "Mode";
            case 22:
              return "Offscreen";
            case 12:
              return "Profiler";
            case 21:
              return "Scope";
            case 13:
              return "Suspense";
            case 19:
              return "SuspenseList";
            case 25:
              return "TracingMarker";
            case 1:
            case 0:
            case 17:
            case 2:
            case 14:
            case 15:
              if ("function" === typeof b) return b.displayName || b.name || null;
              if ("string" === typeof b) return b;
          }
          return null;
        }
        function wa(a) {
          var b = a, c = a;
          if (a.alternate) for (; b.return; ) b = b.return;
          else {
            a = b;
            do
              b = a, 0 !== (b.flags & 4098) && (c = b.return), a = b.return;
            while (a);
          }
          return 3 === b.tag ? c : null;
        }
        function xa(a) {
          if (wa(a) !== a) throw Error(n(188));
        }
        function za(a) {
          var b = a.alternate;
          if (!b) {
            b = wa(a);
            if (null === b) throw Error(n(188));
            return b !== a ? null : a;
          }
          for (var c = a, d = b; ; ) {
            var e = c.return;
            if (null === e) break;
            var f = e.alternate;
            if (null === f) {
              d = e.return;
              if (null !== d) {
                c = d;
                continue;
              }
              break;
            }
            if (e.child === f.child) {
              for (f = e.child; f; ) {
                if (f === c) return xa(e), a;
                if (f === d) return xa(e), b;
                f = f.sibling;
              }
              throw Error(n(188));
            }
            if (c.return !== d.return) c = e, d = f;
            else {
              for (var g5 = false, h = e.child; h; ) {
                if (h === c) {
                  g5 = true;
                  c = e;
                  d = f;
                  break;
                }
                if (h === d) {
                  g5 = true;
                  d = e;
                  c = f;
                  break;
                }
                h = h.sibling;
              }
              if (!g5) {
                for (h = f.child; h; ) {
                  if (h === c) {
                    g5 = true;
                    c = f;
                    d = e;
                    break;
                  }
                  if (h === d) {
                    g5 = true;
                    d = f;
                    c = e;
                    break;
                  }
                  h = h.sibling;
                }
                if (!g5) throw Error(n(189));
              }
            }
            if (c.alternate !== d) throw Error(n(190));
          }
          if (3 !== c.tag) throw Error(n(188));
          return c.stateNode.current === c ? a : b;
        }
        function Aa(a) {
          a = za(a);
          return null !== a ? Ba(a) : null;
        }
        function Ba(a) {
          if (5 === a.tag || 6 === a.tag) return a;
          for (a = a.child; null !== a; ) {
            var b = Ba(a);
            if (null !== b) return b;
            a = a.sibling;
          }
          return null;
        }
        function Ca(a) {
          if (5 === a.tag || 6 === a.tag) return a;
          for (a = a.child; null !== a; ) {
            if (4 !== a.tag) {
              var b = Ca(a);
              if (null !== b) return b;
            }
            a = a.sibling;
          }
          return null;
        }
        var Da = Array.isArray, Ea = $$$hostConfig.getPublicInstance, Fa = $$$hostConfig.getRootHostContext, Ga = $$$hostConfig.getChildHostContext, Ha = $$$hostConfig.prepareForCommit, Ia = $$$hostConfig.resetAfterCommit, Ja = $$$hostConfig.createInstance, Ka = $$$hostConfig.appendInitialChild, La = $$$hostConfig.finalizeInitialChildren, Ma = $$$hostConfig.prepareUpdate, Na = $$$hostConfig.shouldSetTextContent, Oa = $$$hostConfig.createTextInstance, Pa = $$$hostConfig.scheduleTimeout, Qa = $$$hostConfig.cancelTimeout, Ra = $$$hostConfig.noTimeout, Sa = $$$hostConfig.isPrimaryRenderer, Ta = $$$hostConfig.supportsMutation, Ua = $$$hostConfig.supportsPersistence, Va = $$$hostConfig.supportsHydration, Wa = $$$hostConfig.getInstanceFromNode, Xa = $$$hostConfig.preparePortalMount, Ya = $$$hostConfig.getCurrentEventPriority, Za = $$$hostConfig.detachDeletedInstance, $a = $$$hostConfig.supportsMicrotasks, ab = $$$hostConfig.scheduleMicrotask, bb = $$$hostConfig.supportsTestSelectors, cb = $$$hostConfig.findFiberRoot, db = $$$hostConfig.getBoundingRect, eb = $$$hostConfig.getTextContent, fb = $$$hostConfig.isHiddenSubtree, gb = $$$hostConfig.matchAccessibilityRole, hb = $$$hostConfig.setFocusIfFocusable, ib = $$$hostConfig.setupIntersectionObserver, jb = $$$hostConfig.appendChild, kb = $$$hostConfig.appendChildToContainer, lb = $$$hostConfig.commitTextUpdate, mb = $$$hostConfig.commitMount, nb = $$$hostConfig.commitUpdate, ob = $$$hostConfig.insertBefore, pb = $$$hostConfig.insertInContainerBefore, qb = $$$hostConfig.removeChild, rb = $$$hostConfig.removeChildFromContainer, sb = $$$hostConfig.resetTextContent, tb = $$$hostConfig.hideInstance, ub = $$$hostConfig.hideTextInstance, vb = $$$hostConfig.unhideInstance, wb = $$$hostConfig.unhideTextInstance, xb = $$$hostConfig.clearContainer, yb = $$$hostConfig.cloneInstance, zb = $$$hostConfig.createContainerChildSet, Ab = $$$hostConfig.appendChildToContainerChildSet, Bb = $$$hostConfig.finalizeContainerChildren, Cb = $$$hostConfig.replaceContainerChildren, Eb = $$$hostConfig.cloneHiddenInstance, Fb = $$$hostConfig.cloneHiddenTextInstance, Gb = $$$hostConfig.canHydrateInstance, Hb = $$$hostConfig.canHydrateTextInstance, Ib = $$$hostConfig.canHydrateSuspenseInstance, Jb = $$$hostConfig.isSuspenseInstancePending, Kb = $$$hostConfig.isSuspenseInstanceFallback, Lb = $$$hostConfig.getSuspenseInstanceFallbackErrorDetails, Mb = $$$hostConfig.registerSuspenseInstanceRetry, Nb = $$$hostConfig.getNextHydratableSibling, Ob = $$$hostConfig.getFirstHydratableChild, Pb = $$$hostConfig.getFirstHydratableChildWithinContainer, Qb = $$$hostConfig.getFirstHydratableChildWithinSuspenseInstance, Rb = $$$hostConfig.hydrateInstance, Sb = $$$hostConfig.hydrateTextInstance, Tb = $$$hostConfig.hydrateSuspenseInstance, Ub = $$$hostConfig.getNextHydratableInstanceAfterSuspenseInstance, Vb = $$$hostConfig.commitHydratedContainer, Wb = $$$hostConfig.commitHydratedSuspenseInstance, Xb = $$$hostConfig.clearSuspenseBoundary, Yb = $$$hostConfig.clearSuspenseBoundaryFromContainer, Zb = $$$hostConfig.shouldDeleteUnhydratedTailInstances, $b = $$$hostConfig.didNotMatchHydratedContainerTextInstance, ac = $$$hostConfig.didNotMatchHydratedTextInstance, bc;
        function cc(a) {
          if (void 0 === bc) try {
            throw Error();
          } catch (c) {
            var b = c.stack.trim().match(/\n( *(at )?)/);
            bc = b && b[1] || "";
          }
          return "\n" + bc + a;
        }
        var dc = false;
        function ec(a, b) {
          if (!a || dc) return "";
          dc = true;
          var c = Error.prepareStackTrace;
          Error.prepareStackTrace = void 0;
          try {
            if (b) if (b = function() {
              throw Error();
            }, Object.defineProperty(b.prototype, "props", { set: function() {
              throw Error();
            } }), "object" === typeof Reflect && Reflect.construct) {
              try {
                Reflect.construct(b, []);
              } catch (l) {
                var d = l;
              }
              Reflect.construct(a, [], b);
            } else {
              try {
                b.call();
              } catch (l) {
                d = l;
              }
              a.call(b.prototype);
            }
            else {
              try {
                throw Error();
              } catch (l) {
                d = l;
              }
              a();
            }
          } catch (l) {
            if (l && d && "string" === typeof l.stack) {
              for (var e = l.stack.split("\n"), f = d.stack.split("\n"), g5 = e.length - 1, h = f.length - 1; 1 <= g5 && 0 <= h && e[g5] !== f[h]; ) h--;
              for (; 1 <= g5 && 0 <= h; g5--, h--) if (e[g5] !== f[h]) {
                if (1 !== g5 || 1 !== h) {
                  do
                    if (g5--, h--, 0 > h || e[g5] !== f[h]) {
                      var k = "\n" + e[g5].replace(" at new ", " at ");
                      a.displayName && k.includes("<anonymous>") && (k = k.replace("<anonymous>", a.displayName));
                      return k;
                    }
                  while (1 <= g5 && 0 <= h);
                }
                break;
              }
            }
          } finally {
            dc = false, Error.prepareStackTrace = c;
          }
          return (a = a ? a.displayName || a.name : "") ? cc(a) : "";
        }
        var fc = Object.prototype.hasOwnProperty, gc = [], hc = -1;
        function ic(a) {
          return { current: a };
        }
        function q(a) {
          0 > hc || (a.current = gc[hc], gc[hc] = null, hc--);
        }
        function v(a, b) {
          hc++;
          gc[hc] = a.current;
          a.current = b;
        }
        var jc = {}, x = ic(jc), z = ic(false), kc = jc;
        function mc(a, b) {
          var c = a.type.contextTypes;
          if (!c) return jc;
          var d = a.stateNode;
          if (d && d.__reactInternalMemoizedUnmaskedChildContext === b) return d.__reactInternalMemoizedMaskedChildContext;
          var e = {}, f;
          for (f in c) e[f] = b[f];
          d && (a = a.stateNode, a.__reactInternalMemoizedUnmaskedChildContext = b, a.__reactInternalMemoizedMaskedChildContext = e);
          return e;
        }
        function A(a) {
          a = a.childContextTypes;
          return null !== a && void 0 !== a;
        }
        function nc() {
          q(z);
          q(x);
        }
        function oc(a, b, c) {
          if (x.current !== jc) throw Error(n(168));
          v(x, b);
          v(z, c);
        }
        function pc(a, b, c) {
          var d = a.stateNode;
          b = b.childContextTypes;
          if ("function" !== typeof d.getChildContext) return c;
          d = d.getChildContext();
          for (var e in d) if (!(e in b)) throw Error(n(108, va(a) || "Unknown", e));
          return ca({}, c, d);
        }
        function qc(a) {
          a = (a = a.stateNode) && a.__reactInternalMemoizedMergedChildContext || jc;
          kc = x.current;
          v(x, a);
          v(z, z.current);
          return true;
        }
        function rc(a, b, c) {
          var d = a.stateNode;
          if (!d) throw Error(n(169));
          c ? (a = pc(a, b, kc), d.__reactInternalMemoizedMergedChildContext = a, q(z), q(x), v(x, a)) : q(z);
          v(z, c);
        }
        var tc = Math.clz32 ? Math.clz32 : sc, uc = Math.log, vc = Math.LN2;
        function sc(a) {
          a >>>= 0;
          return 0 === a ? 32 : 31 - (uc(a) / vc | 0) | 0;
        }
        var wc = 64, xc = 4194304;
        function yc(a) {
          switch (a & -a) {
            case 1:
              return 1;
            case 2:
              return 2;
            case 4:
              return 4;
            case 8:
              return 8;
            case 16:
              return 16;
            case 32:
              return 32;
            case 64:
            case 128:
            case 256:
            case 512:
            case 1024:
            case 2048:
            case 4096:
            case 8192:
            case 16384:
            case 32768:
            case 65536:
            case 131072:
            case 262144:
            case 524288:
            case 1048576:
            case 2097152:
              return a & 4194240;
            case 4194304:
            case 8388608:
            case 16777216:
            case 33554432:
            case 67108864:
              return a & 130023424;
            case 134217728:
              return 134217728;
            case 268435456:
              return 268435456;
            case 536870912:
              return 536870912;
            case 1073741824:
              return 1073741824;
            default:
              return a;
          }
        }
        function zc(a, b) {
          var c = a.pendingLanes;
          if (0 === c) return 0;
          var d = 0, e = a.suspendedLanes, f = a.pingedLanes, g5 = c & 268435455;
          if (0 !== g5) {
            var h = g5 & ~e;
            0 !== h ? d = yc(h) : (f &= g5, 0 !== f && (d = yc(f)));
          } else g5 = c & ~e, 0 !== g5 ? d = yc(g5) : 0 !== f && (d = yc(f));
          if (0 === d) return 0;
          if (0 !== b && b !== d && 0 === (b & e) && (e = d & -d, f = b & -b, e >= f || 16 === e && 0 !== (f & 4194240))) return b;
          0 !== (d & 4) && (d |= c & 16);
          b = a.entangledLanes;
          if (0 !== b) for (a = a.entanglements, b &= d; 0 < b; ) c = 31 - tc(b), e = 1 << c, d |= a[c], b &= ~e;
          return d;
        }
        function Ac(a, b) {
          switch (a) {
            case 1:
            case 2:
            case 4:
              return b + 250;
            case 8:
            case 16:
            case 32:
            case 64:
            case 128:
            case 256:
            case 512:
            case 1024:
            case 2048:
            case 4096:
            case 8192:
            case 16384:
            case 32768:
            case 65536:
            case 131072:
            case 262144:
            case 524288:
            case 1048576:
            case 2097152:
              return b + 5e3;
            case 4194304:
            case 8388608:
            case 16777216:
            case 33554432:
            case 67108864:
              return -1;
            case 134217728:
            case 268435456:
            case 536870912:
            case 1073741824:
              return -1;
            default:
              return -1;
          }
        }
        function Bc(a, b) {
          for (var c = a.suspendedLanes, d = a.pingedLanes, e = a.expirationTimes, f = a.pendingLanes; 0 < f; ) {
            var g5 = 31 - tc(f), h = 1 << g5, k = e[g5];
            if (-1 === k) {
              if (0 === (h & c) || 0 !== (h & d)) e[g5] = Ac(h, b);
            } else k <= b && (a.expiredLanes |= h);
            f &= ~h;
          }
        }
        function Cc(a) {
          a = a.pendingLanes & -1073741825;
          return 0 !== a ? a : a & 1073741824 ? 1073741824 : 0;
        }
        function Dc() {
          var a = wc;
          wc <<= 1;
          0 === (wc & 4194240) && (wc = 64);
          return a;
        }
        function Ec(a) {
          for (var b = [], c = 0; 31 > c; c++) b.push(a);
          return b;
        }
        function Fc(a, b, c) {
          a.pendingLanes |= b;
          536870912 !== b && (a.suspendedLanes = 0, a.pingedLanes = 0);
          a = a.eventTimes;
          b = 31 - tc(b);
          a[b] = c;
        }
        function Gc(a, b) {
          var c = a.pendingLanes & ~b;
          a.pendingLanes = b;
          a.suspendedLanes = 0;
          a.pingedLanes = 0;
          a.expiredLanes &= b;
          a.mutableReadLanes &= b;
          a.entangledLanes &= b;
          b = a.entanglements;
          var d = a.eventTimes;
          for (a = a.expirationTimes; 0 < c; ) {
            var e = 31 - tc(c), f = 1 << e;
            b[e] = 0;
            d[e] = -1;
            a[e] = -1;
            c &= ~f;
          }
        }
        function Hc(a, b) {
          var c = a.entangledLanes |= b;
          for (a = a.entanglements; c; ) {
            var d = 31 - tc(c), e = 1 << d;
            e & b | a[d] & b && (a[d] |= b);
            c &= ~e;
          }
        }
        var C = 0;
        function Ic(a) {
          a &= -a;
          return 1 < a ? 4 < a ? 0 !== (a & 268435455) ? 16 : 536870912 : 4 : 1;
        }
        var Jc = ba.unstable_scheduleCallback, Kc = ba.unstable_cancelCallback, Lc = ba.unstable_shouldYield, Mc = ba.unstable_requestPaint, D = ba.unstable_now, Nc = ba.unstable_ImmediatePriority, Oc = ba.unstable_UserBlockingPriority, Pc = ba.unstable_NormalPriority, Qc = ba.unstable_IdlePriority, Rc = null, Sc = null;
        function Tc(a) {
          if (Sc && "function" === typeof Sc.onCommitFiberRoot) try {
            Sc.onCommitFiberRoot(Rc, a, void 0, 128 === (a.current.flags & 128));
          } catch (b) {
          }
        }
        function Uc(a, b) {
          return a === b && (0 !== a || 1 / a === 1 / b) || a !== a && b !== b;
        }
        var Vc = "function" === typeof Object.is ? Object.is : Uc, Wc = null, Xc = false, Yc = false;
        function Zc(a) {
          null === Wc ? Wc = [a] : Wc.push(a);
        }
        function $c(a) {
          Xc = true;
          Zc(a);
        }
        function ad() {
          if (!Yc && null !== Wc) {
            Yc = true;
            var a = 0, b = C;
            try {
              var c = Wc;
              for (C = 1; a < c.length; a++) {
                var d = c[a];
                do
                  d = d(true);
                while (null !== d);
              }
              Wc = null;
              Xc = false;
            } catch (e) {
              throw null !== Wc && (Wc = Wc.slice(a + 1)), Jc(Nc, ad), e;
            } finally {
              C = b, Yc = false;
            }
          }
          return null;
        }
        var bd = [], cd = 0, dd = null, ed = 0, fd = [], gd = 0, hd = null, id = 1, jd = "";
        function kd(a, b) {
          bd[cd++] = ed;
          bd[cd++] = dd;
          dd = a;
          ed = b;
        }
        function ld(a, b, c) {
          fd[gd++] = id;
          fd[gd++] = jd;
          fd[gd++] = hd;
          hd = a;
          var d = id;
          a = jd;
          var e = 32 - tc(d) - 1;
          d &= ~(1 << e);
          c += 1;
          var f = 32 - tc(b) + e;
          if (30 < f) {
            var g5 = e - e % 5;
            f = (d & (1 << g5) - 1).toString(32);
            d >>= g5;
            e -= g5;
            id = 1 << 32 - tc(b) + e | c << e | d;
            jd = f + a;
          } else id = 1 << f | c << e | d, jd = a;
        }
        function md(a) {
          null !== a.return && (kd(a, 1), ld(a, 1, 0));
        }
        function nd(a) {
          for (; a === dd; ) dd = bd[--cd], bd[cd] = null, ed = bd[--cd], bd[cd] = null;
          for (; a === hd; ) hd = fd[--gd], fd[gd] = null, jd = fd[--gd], fd[gd] = null, id = fd[--gd], fd[gd] = null;
        }
        var od = null, pd = null, F = false, qd = false, rd = null;
        function sd(a, b) {
          var c = td(5, null, null, 0);
          c.elementType = "DELETED";
          c.stateNode = b;
          c.return = a;
          b = a.deletions;
          null === b ? (a.deletions = [c], a.flags |= 16) : b.push(c);
        }
        function ud(a, b) {
          switch (a.tag) {
            case 5:
              return b = Gb(b, a.type, a.pendingProps), null !== b ? (a.stateNode = b, od = a, pd = Ob(b), true) : false;
            case 6:
              return b = Hb(b, a.pendingProps), null !== b ? (a.stateNode = b, od = a, pd = null, true) : false;
            case 13:
              b = Ib(b);
              if (null !== b) {
                var c = null !== hd ? { id, overflow: jd } : null;
                a.memoizedState = { dehydrated: b, treeContext: c, retryLane: 1073741824 };
                c = td(18, null, null, 0);
                c.stateNode = b;
                c.return = a;
                a.child = c;
                od = a;
                pd = null;
                return true;
              }
              return false;
            default:
              return false;
          }
        }
        function vd(a) {
          return 0 !== (a.mode & 1) && 0 === (a.flags & 128);
        }
        function wd(a) {
          if (F) {
            var b = pd;
            if (b) {
              var c = b;
              if (!ud(a, b)) {
                if (vd(a)) throw Error(n(418));
                b = Nb(c);
                var d = od;
                b && ud(a, b) ? sd(d, c) : (a.flags = a.flags & -4097 | 2, F = false, od = a);
              }
            } else {
              if (vd(a)) throw Error(n(418));
              a.flags = a.flags & -4097 | 2;
              F = false;
              od = a;
            }
          }
        }
        function xd(a) {
          for (a = a.return; null !== a && 5 !== a.tag && 3 !== a.tag && 13 !== a.tag; ) a = a.return;
          od = a;
        }
        function yd(a) {
          if (!Va || a !== od) return false;
          if (!F) return xd(a), F = true, false;
          if (3 !== a.tag && (5 !== a.tag || Zb(a.type) && !Na(a.type, a.memoizedProps))) {
            var b = pd;
            if (b) {
              if (vd(a)) throw zd(), Error(n(418));
              for (; b; ) sd(a, b), b = Nb(b);
            }
          }
          xd(a);
          if (13 === a.tag) {
            if (!Va) throw Error(n(316));
            a = a.memoizedState;
            a = null !== a ? a.dehydrated : null;
            if (!a) throw Error(n(317));
            pd = Ub(a);
          } else pd = od ? Nb(a.stateNode) : null;
          return true;
        }
        function zd() {
          for (var a = pd; a; ) a = Nb(a);
        }
        function Ad() {
          Va && (pd = od = null, qd = F = false);
        }
        function Bd(a) {
          null === rd ? rd = [a] : rd.push(a);
        }
        var Cd = da.ReactCurrentBatchConfig;
        function Dd(a, b) {
          if (Vc(a, b)) return true;
          if ("object" !== typeof a || null === a || "object" !== typeof b || null === b) return false;
          var c = Object.keys(a), d = Object.keys(b);
          if (c.length !== d.length) return false;
          for (d = 0; d < c.length; d++) {
            var e = c[d];
            if (!fc.call(b, e) || !Vc(a[e], b[e])) return false;
          }
          return true;
        }
        function Ed(a) {
          switch (a.tag) {
            case 5:
              return cc(a.type);
            case 16:
              return cc("Lazy");
            case 13:
              return cc("Suspense");
            case 19:
              return cc("SuspenseList");
            case 0:
            case 2:
            case 15:
              return a = ec(a.type, false), a;
            case 11:
              return a = ec(a.type.render, false), a;
            case 1:
              return a = ec(a.type, true), a;
            default:
              return "";
          }
        }
        function Fd(a, b, c) {
          a = c.ref;
          if (null !== a && "function" !== typeof a && "object" !== typeof a) {
            if (c._owner) {
              c = c._owner;
              if (c) {
                if (1 !== c.tag) throw Error(n(309));
                var d = c.stateNode;
              }
              if (!d) throw Error(n(147, a));
              var e = d, f = "" + a;
              if (null !== b && null !== b.ref && "function" === typeof b.ref && b.ref._stringRef === f) return b.ref;
              b = function(a2) {
                var b2 = e.refs;
                null === a2 ? delete b2[f] : b2[f] = a2;
              };
              b._stringRef = f;
              return b;
            }
            if ("string" !== typeof a) throw Error(n(284));
            if (!c._owner) throw Error(n(290, a));
          }
          return a;
        }
        function Gd(a, b) {
          a = Object.prototype.toString.call(b);
          throw Error(n(31, "[object Object]" === a ? "object with keys {" + Object.keys(b).join(", ") + "}" : a));
        }
        function Hd(a) {
          var b = a._init;
          return b(a._payload);
        }
        function Id(a) {
          function b(b2, c2) {
            if (a) {
              var d2 = b2.deletions;
              null === d2 ? (b2.deletions = [c2], b2.flags |= 16) : d2.push(c2);
            }
          }
          function c(c2, d2) {
            if (!a) return null;
            for (; null !== d2; ) b(c2, d2), d2 = d2.sibling;
            return null;
          }
          function d(a2, b2) {
            for (a2 = /* @__PURE__ */ new Map(); null !== b2; ) null !== b2.key ? a2.set(b2.key, b2) : a2.set(b2.index, b2), b2 = b2.sibling;
            return a2;
          }
          function e(a2, b2) {
            a2 = Jd(a2, b2);
            a2.index = 0;
            a2.sibling = null;
            return a2;
          }
          function f(b2, c2, d2) {
            b2.index = d2;
            if (!a) return b2.flags |= 1048576, c2;
            d2 = b2.alternate;
            if (null !== d2) return d2 = d2.index, d2 < c2 ? (b2.flags |= 2, c2) : d2;
            b2.flags |= 2;
            return c2;
          }
          function g5(b2) {
            a && null === b2.alternate && (b2.flags |= 2);
            return b2;
          }
          function h(a2, b2, c2, d2) {
            if (null === b2 || 6 !== b2.tag) return b2 = Kd(c2, a2.mode, d2), b2.return = a2, b2;
            b2 = e(b2, c2);
            b2.return = a2;
            return b2;
          }
          function k(a2, b2, c2, d2) {
            var f2 = c2.type;
            if (f2 === ha) return m(a2, b2, c2.props.children, d2, c2.key);
            if (null !== b2 && (b2.elementType === f2 || "object" === typeof f2 && null !== f2 && f2.$$typeof === qa && Hd(f2) === b2.type)) return d2 = e(b2, c2.props), d2.ref = Fd(a2, b2, c2), d2.return = a2, d2;
            d2 = Ld(c2.type, c2.key, c2.props, null, a2.mode, d2);
            d2.ref = Fd(a2, b2, c2);
            d2.return = a2;
            return d2;
          }
          function l(a2, b2, c2, d2) {
            if (null === b2 || 4 !== b2.tag || b2.stateNode.containerInfo !== c2.containerInfo || b2.stateNode.implementation !== c2.implementation) return b2 = Md(c2, a2.mode, d2), b2.return = a2, b2;
            b2 = e(b2, c2.children || []);
            b2.return = a2;
            return b2;
          }
          function m(a2, b2, c2, d2, f2) {
            if (null === b2 || 7 !== b2.tag) return b2 = Nd(c2, a2.mode, d2, f2), b2.return = a2, b2;
            b2 = e(b2, c2);
            b2.return = a2;
            return b2;
          }
          function r(a2, b2, c2) {
            if ("string" === typeof b2 && "" !== b2 || "number" === typeof b2) return b2 = Kd("" + b2, a2.mode, c2), b2.return = a2, b2;
            if ("object" === typeof b2 && null !== b2) {
              switch (b2.$$typeof) {
                case ea:
                  return c2 = Ld(b2.type, b2.key, b2.props, null, a2.mode, c2), c2.ref = Fd(a2, null, b2), c2.return = a2, c2;
                case fa:
                  return b2 = Md(b2, a2.mode, c2), b2.return = a2, b2;
                case qa:
                  var d2 = b2._init;
                  return r(a2, d2(b2._payload), c2);
              }
              if (Da(b2) || ta(b2)) return b2 = Nd(b2, a2.mode, c2, null), b2.return = a2, b2;
              Gd(a2, b2);
            }
            return null;
          }
          function p(a2, b2, c2, d2) {
            var e2 = null !== b2 ? b2.key : null;
            if ("string" === typeof c2 && "" !== c2 || "number" === typeof c2) return null !== e2 ? null : h(a2, b2, "" + c2, d2);
            if ("object" === typeof c2 && null !== c2) {
              switch (c2.$$typeof) {
                case ea:
                  return c2.key === e2 ? k(a2, b2, c2, d2) : null;
                case fa:
                  return c2.key === e2 ? l(a2, b2, c2, d2) : null;
                case qa:
                  return e2 = c2._init, p(
                    a2,
                    b2,
                    e2(c2._payload),
                    d2
                  );
              }
              if (Da(c2) || ta(c2)) return null !== e2 ? null : m(a2, b2, c2, d2, null);
              Gd(a2, c2);
            }
            return null;
          }
          function B(a2, b2, c2, d2, e2) {
            if ("string" === typeof d2 && "" !== d2 || "number" === typeof d2) return a2 = a2.get(c2) || null, h(b2, a2, "" + d2, e2);
            if ("object" === typeof d2 && null !== d2) {
              switch (d2.$$typeof) {
                case ea:
                  return a2 = a2.get(null === d2.key ? c2 : d2.key) || null, k(b2, a2, d2, e2);
                case fa:
                  return a2 = a2.get(null === d2.key ? c2 : d2.key) || null, l(b2, a2, d2, e2);
                case qa:
                  var f2 = d2._init;
                  return B(a2, b2, c2, f2(d2._payload), e2);
              }
              if (Da(d2) || ta(d2)) return a2 = a2.get(c2) || null, m(b2, a2, d2, e2, null);
              Gd(b2, d2);
            }
            return null;
          }
          function w(e2, g6, h2, k2) {
            for (var l2 = null, m2 = null, u = g6, t = g6 = 0, E = null; null !== u && t < h2.length; t++) {
              u.index > t ? (E = u, u = null) : E = u.sibling;
              var y = p(e2, u, h2[t], k2);
              if (null === y) {
                null === u && (u = E);
                break;
              }
              a && u && null === y.alternate && b(e2, u);
              g6 = f(y, g6, t);
              null === m2 ? l2 = y : m2.sibling = y;
              m2 = y;
              u = E;
            }
            if (t === h2.length) return c(e2, u), F && kd(e2, t), l2;
            if (null === u) {
              for (; t < h2.length; t++) u = r(e2, h2[t], k2), null !== u && (g6 = f(u, g6, t), null === m2 ? l2 = u : m2.sibling = u, m2 = u);
              F && kd(e2, t);
              return l2;
            }
            for (u = d(e2, u); t < h2.length; t++) E = B(u, e2, t, h2[t], k2), null !== E && (a && null !== E.alternate && u.delete(null === E.key ? t : E.key), g6 = f(E, g6, t), null === m2 ? l2 = E : m2.sibling = E, m2 = E);
            a && u.forEach(function(a2) {
              return b(e2, a2);
            });
            F && kd(e2, t);
            return l2;
          }
          function Y(e2, g6, h2, k2) {
            var l2 = ta(h2);
            if ("function" !== typeof l2) throw Error(n(150));
            h2 = l2.call(h2);
            if (null == h2) throw Error(n(151));
            for (var u = l2 = null, m2 = g6, t = g6 = 0, E = null, y = h2.next(); null !== m2 && !y.done; t++, y = h2.next()) {
              m2.index > t ? (E = m2, m2 = null) : E = m2.sibling;
              var w2 = p(e2, m2, y.value, k2);
              if (null === w2) {
                null === m2 && (m2 = E);
                break;
              }
              a && m2 && null === w2.alternate && b(e2, m2);
              g6 = f(w2, g6, t);
              null === u ? l2 = w2 : u.sibling = w2;
              u = w2;
              m2 = E;
            }
            if (y.done) return c(
              e2,
              m2
            ), F && kd(e2, t), l2;
            if (null === m2) {
              for (; !y.done; t++, y = h2.next()) y = r(e2, y.value, k2), null !== y && (g6 = f(y, g6, t), null === u ? l2 = y : u.sibling = y, u = y);
              F && kd(e2, t);
              return l2;
            }
            for (m2 = d(e2, m2); !y.done; t++, y = h2.next()) y = B(m2, e2, t, y.value, k2), null !== y && (a && null !== y.alternate && m2.delete(null === y.key ? t : y.key), g6 = f(y, g6, t), null === u ? l2 = y : u.sibling = y, u = y);
            a && m2.forEach(function(a2) {
              return b(e2, a2);
            });
            F && kd(e2, t);
            return l2;
          }
          function ya(a2, d2, f2, h2) {
            "object" === typeof f2 && null !== f2 && f2.type === ha && null === f2.key && (f2 = f2.props.children);
            if ("object" === typeof f2 && null !== f2) {
              switch (f2.$$typeof) {
                case ea:
                  a: {
                    for (var k2 = f2.key, l2 = d2; null !== l2; ) {
                      if (l2.key === k2) {
                        k2 = f2.type;
                        if (k2 === ha) {
                          if (7 === l2.tag) {
                            c(a2, l2.sibling);
                            d2 = e(l2, f2.props.children);
                            d2.return = a2;
                            a2 = d2;
                            break a;
                          }
                        } else if (l2.elementType === k2 || "object" === typeof k2 && null !== k2 && k2.$$typeof === qa && Hd(k2) === l2.type) {
                          c(a2, l2.sibling);
                          d2 = e(l2, f2.props);
                          d2.ref = Fd(a2, l2, f2);
                          d2.return = a2;
                          a2 = d2;
                          break a;
                        }
                        c(a2, l2);
                        break;
                      } else b(a2, l2);
                      l2 = l2.sibling;
                    }
                    f2.type === ha ? (d2 = Nd(f2.props.children, a2.mode, h2, f2.key), d2.return = a2, a2 = d2) : (h2 = Ld(f2.type, f2.key, f2.props, null, a2.mode, h2), h2.ref = Fd(a2, d2, f2), h2.return = a2, a2 = h2);
                  }
                  return g5(a2);
                case fa:
                  a: {
                    for (l2 = f2.key; null !== d2; ) {
                      if (d2.key === l2) if (4 === d2.tag && d2.stateNode.containerInfo === f2.containerInfo && d2.stateNode.implementation === f2.implementation) {
                        c(a2, d2.sibling);
                        d2 = e(d2, f2.children || []);
                        d2.return = a2;
                        a2 = d2;
                        break a;
                      } else {
                        c(a2, d2);
                        break;
                      }
                      else b(a2, d2);
                      d2 = d2.sibling;
                    }
                    d2 = Md(f2, a2.mode, h2);
                    d2.return = a2;
                    a2 = d2;
                  }
                  return g5(a2);
                case qa:
                  return l2 = f2._init, ya(a2, d2, l2(f2._payload), h2);
              }
              if (Da(f2)) return w(a2, d2, f2, h2);
              if (ta(f2)) return Y(a2, d2, f2, h2);
              Gd(a2, f2);
            }
            return "string" === typeof f2 && "" !== f2 || "number" === typeof f2 ? (f2 = "" + f2, null !== d2 && 6 === d2.tag ? (c(a2, d2.sibling), d2 = e(d2, f2), d2.return = a2, a2 = d2) : (c(a2, d2), d2 = Kd(f2, a2.mode, h2), d2.return = a2, a2 = d2), g5(a2)) : c(a2, d2);
          }
          return ya;
        }
        var Od = Id(true), Pd = Id(false), Qd = ic(null), Rd = null, Sd = null, Td = null;
        function Ud() {
          Td = Sd = Rd = null;
        }
        function Vd(a, b, c) {
          Sa ? (v(Qd, b._currentValue), b._currentValue = c) : (v(Qd, b._currentValue2), b._currentValue2 = c);
        }
        function Wd(a) {
          var b = Qd.current;
          q(Qd);
          Sa ? a._currentValue = b : a._currentValue2 = b;
        }
        function Xd(a, b, c) {
          for (; null !== a; ) {
            var d = a.alternate;
            (a.childLanes & b) !== b ? (a.childLanes |= b, null !== d && (d.childLanes |= b)) : null !== d && (d.childLanes & b) !== b && (d.childLanes |= b);
            if (a === c) break;
            a = a.return;
          }
        }
        function Yd(a, b) {
          Rd = a;
          Td = Sd = null;
          a = a.dependencies;
          null !== a && null !== a.firstContext && (0 !== (a.lanes & b) && (G = true), a.firstContext = null);
        }
        function Zd(a) {
          var b = Sa ? a._currentValue : a._currentValue2;
          if (Td !== a) if (a = { context: a, memoizedValue: b, next: null }, null === Sd) {
            if (null === Rd) throw Error(n(308));
            Sd = a;
            Rd.dependencies = { lanes: 0, firstContext: a };
          } else Sd = Sd.next = a;
          return b;
        }
        var $d = null;
        function ae(a) {
          null === $d ? $d = [a] : $d.push(a);
        }
        function be(a, b, c, d) {
          var e = b.interleaved;
          null === e ? (c.next = c, ae(b)) : (c.next = e.next, e.next = c);
          b.interleaved = c;
          return ce(a, d);
        }
        function ce(a, b) {
          a.lanes |= b;
          var c = a.alternate;
          null !== c && (c.lanes |= b);
          c = a;
          for (a = a.return; null !== a; ) a.childLanes |= b, c = a.alternate, null !== c && (c.childLanes |= b), c = a, a = a.return;
          return 3 === c.tag ? c.stateNode : null;
        }
        var de = false;
        function ee(a) {
          a.updateQueue = { baseState: a.memoizedState, firstBaseUpdate: null, lastBaseUpdate: null, shared: { pending: null, interleaved: null, lanes: 0 }, effects: null };
        }
        function fe(a, b) {
          a = a.updateQueue;
          b.updateQueue === a && (b.updateQueue = { baseState: a.baseState, firstBaseUpdate: a.firstBaseUpdate, lastBaseUpdate: a.lastBaseUpdate, shared: a.shared, effects: a.effects });
        }
        function ge(a, b) {
          return { eventTime: a, lane: b, tag: 0, payload: null, callback: null, next: null };
        }
        function he(a, b, c) {
          var d = a.updateQueue;
          if (null === d) return null;
          d = d.shared;
          if (0 !== (H & 2)) {
            var e = d.pending;
            null === e ? b.next = b : (b.next = e.next, e.next = b);
            d.pending = b;
            return ce(a, c);
          }
          e = d.interleaved;
          null === e ? (b.next = b, ae(d)) : (b.next = e.next, e.next = b);
          d.interleaved = b;
          return ce(a, c);
        }
        function ie(a, b, c) {
          b = b.updateQueue;
          if (null !== b && (b = b.shared, 0 !== (c & 4194240))) {
            var d = b.lanes;
            d &= a.pendingLanes;
            c |= d;
            b.lanes = c;
            Hc(a, c);
          }
        }
        function je(a, b) {
          var c = a.updateQueue, d = a.alternate;
          if (null !== d && (d = d.updateQueue, c === d)) {
            var e = null, f = null;
            c = c.firstBaseUpdate;
            if (null !== c) {
              do {
                var g5 = { eventTime: c.eventTime, lane: c.lane, tag: c.tag, payload: c.payload, callback: c.callback, next: null };
                null === f ? e = f = g5 : f = f.next = g5;
                c = c.next;
              } while (null !== c);
              null === f ? e = f = b : f = f.next = b;
            } else e = f = b;
            c = { baseState: d.baseState, firstBaseUpdate: e, lastBaseUpdate: f, shared: d.shared, effects: d.effects };
            a.updateQueue = c;
            return;
          }
          a = c.lastBaseUpdate;
          null === a ? c.firstBaseUpdate = b : a.next = b;
          c.lastBaseUpdate = b;
        }
        function ke(a, b, c, d) {
          var e = a.updateQueue;
          de = false;
          var f = e.firstBaseUpdate, g5 = e.lastBaseUpdate, h = e.shared.pending;
          if (null !== h) {
            e.shared.pending = null;
            var k = h, l = k.next;
            k.next = null;
            null === g5 ? f = l : g5.next = l;
            g5 = k;
            var m = a.alternate;
            null !== m && (m = m.updateQueue, h = m.lastBaseUpdate, h !== g5 && (null === h ? m.firstBaseUpdate = l : h.next = l, m.lastBaseUpdate = k));
          }
          if (null !== f) {
            var r = e.baseState;
            g5 = 0;
            m = l = k = null;
            h = f;
            do {
              var p = h.lane, B = h.eventTime;
              if ((d & p) === p) {
                null !== m && (m = m.next = {
                  eventTime: B,
                  lane: 0,
                  tag: h.tag,
                  payload: h.payload,
                  callback: h.callback,
                  next: null
                });
                a: {
                  var w = a, Y = h;
                  p = b;
                  B = c;
                  switch (Y.tag) {
                    case 1:
                      w = Y.payload;
                      if ("function" === typeof w) {
                        r = w.call(B, r, p);
                        break a;
                      }
                      r = w;
                      break a;
                    case 3:
                      w.flags = w.flags & -65537 | 128;
                    case 0:
                      w = Y.payload;
                      p = "function" === typeof w ? w.call(B, r, p) : w;
                      if (null === p || void 0 === p) break a;
                      r = ca({}, r, p);
                      break a;
                    case 2:
                      de = true;
                  }
                }
                null !== h.callback && 0 !== h.lane && (a.flags |= 64, p = e.effects, null === p ? e.effects = [h] : p.push(h));
              } else B = { eventTime: B, lane: p, tag: h.tag, payload: h.payload, callback: h.callback, next: null }, null === m ? (l = m = B, k = r) : m = m.next = B, g5 |= p;
              h = h.next;
              if (null === h) if (h = e.shared.pending, null === h) break;
              else p = h, h = p.next, p.next = null, e.lastBaseUpdate = p, e.shared.pending = null;
            } while (1);
            null === m && (k = r);
            e.baseState = k;
            e.firstBaseUpdate = l;
            e.lastBaseUpdate = m;
            b = e.shared.interleaved;
            if (null !== b) {
              e = b;
              do
                g5 |= e.lane, e = e.next;
              while (e !== b);
            } else null === f && (e.shared.lanes = 0);
            le |= g5;
            a.lanes = g5;
            a.memoizedState = r;
          }
        }
        function me(a, b, c) {
          a = b.effects;
          b.effects = null;
          if (null !== a) for (b = 0; b < a.length; b++) {
            var d = a[b], e = d.callback;
            if (null !== e) {
              d.callback = null;
              d = c;
              if ("function" !== typeof e) throw Error(n(191, e));
              e.call(d);
            }
          }
        }
        var ne = {}, oe = ic(ne), pe = ic(ne), qe = ic(ne);
        function re(a) {
          if (a === ne) throw Error(n(174));
          return a;
        }
        function se(a, b) {
          v(qe, b);
          v(pe, a);
          v(oe, ne);
          a = Fa(b);
          q(oe);
          v(oe, a);
        }
        function te() {
          q(oe);
          q(pe);
          q(qe);
        }
        function ue(a) {
          var b = re(qe.current), c = re(oe.current);
          b = Ga(c, a.type, b);
          c !== b && (v(pe, a), v(oe, b));
        }
        function ve(a) {
          pe.current === a && (q(oe), q(pe));
        }
        var I = ic(0);
        function we(a) {
          for (var b = a; null !== b; ) {
            if (13 === b.tag) {
              var c = b.memoizedState;
              if (null !== c && (c = c.dehydrated, null === c || Jb(c) || Kb(c))) return b;
            } else if (19 === b.tag && void 0 !== b.memoizedProps.revealOrder) {
              if (0 !== (b.flags & 128)) return b;
            } else if (null !== b.child) {
              b.child.return = b;
              b = b.child;
              continue;
            }
            if (b === a) break;
            for (; null === b.sibling; ) {
              if (null === b.return || b.return === a) return null;
              b = b.return;
            }
            b.sibling.return = b.return;
            b = b.sibling;
          }
          return null;
        }
        var xe = [];
        function ye() {
          for (var a = 0; a < xe.length; a++) {
            var b = xe[a];
            Sa ? b._workInProgressVersionPrimary = null : b._workInProgressVersionSecondary = null;
          }
          xe.length = 0;
        }
        var ze = da.ReactCurrentDispatcher, Ae = da.ReactCurrentBatchConfig, Be = 0, J = null, K = null, L = null, Ce = false, De = false, Ee = 0, Fe = 0;
        function M() {
          throw Error(n(321));
        }
        function Ge(a, b) {
          if (null === b) return false;
          for (var c = 0; c < b.length && c < a.length; c++) if (!Vc(a[c], b[c])) return false;
          return true;
        }
        function He(a, b, c, d, e, f) {
          Be = f;
          J = b;
          b.memoizedState = null;
          b.updateQueue = null;
          b.lanes = 0;
          ze.current = null === a || null === a.memoizedState ? Ie : Je;
          a = c(d, e);
          if (De) {
            f = 0;
            do {
              De = false;
              Ee = 0;
              if (25 <= f) throw Error(n(301));
              f += 1;
              L = K = null;
              b.updateQueue = null;
              ze.current = Ke;
              a = c(d, e);
            } while (De);
          }
          ze.current = Le;
          b = null !== K && null !== K.next;
          Be = 0;
          L = K = J = null;
          Ce = false;
          if (b) throw Error(n(300));
          return a;
        }
        function Me() {
          var a = 0 !== Ee;
          Ee = 0;
          return a;
        }
        function Ne() {
          var a = { memoizedState: null, baseState: null, baseQueue: null, queue: null, next: null };
          null === L ? J.memoizedState = L = a : L = L.next = a;
          return L;
        }
        function Oe() {
          if (null === K) {
            var a = J.alternate;
            a = null !== a ? a.memoizedState : null;
          } else a = K.next;
          var b = null === L ? J.memoizedState : L.next;
          if (null !== b) L = b, K = a;
          else {
            if (null === a) throw Error(n(310));
            K = a;
            a = { memoizedState: K.memoizedState, baseState: K.baseState, baseQueue: K.baseQueue, queue: K.queue, next: null };
            null === L ? J.memoizedState = L = a : L = L.next = a;
          }
          return L;
        }
        function Pe(a, b) {
          return "function" === typeof b ? b(a) : b;
        }
        function Qe(a) {
          var b = Oe(), c = b.queue;
          if (null === c) throw Error(n(311));
          c.lastRenderedReducer = a;
          var d = K, e = d.baseQueue, f = c.pending;
          if (null !== f) {
            if (null !== e) {
              var g5 = e.next;
              e.next = f.next;
              f.next = g5;
            }
            d.baseQueue = e = f;
            c.pending = null;
          }
          if (null !== e) {
            f = e.next;
            d = d.baseState;
            var h = g5 = null, k = null, l = f;
            do {
              var m = l.lane;
              if ((Be & m) === m) null !== k && (k = k.next = { lane: 0, action: l.action, hasEagerState: l.hasEagerState, eagerState: l.eagerState, next: null }), d = l.hasEagerState ? l.eagerState : a(d, l.action);
              else {
                var r = {
                  lane: m,
                  action: l.action,
                  hasEagerState: l.hasEagerState,
                  eagerState: l.eagerState,
                  next: null
                };
                null === k ? (h = k = r, g5 = d) : k = k.next = r;
                J.lanes |= m;
                le |= m;
              }
              l = l.next;
            } while (null !== l && l !== f);
            null === k ? g5 = d : k.next = h;
            Vc(d, b.memoizedState) || (G = true);
            b.memoizedState = d;
            b.baseState = g5;
            b.baseQueue = k;
            c.lastRenderedState = d;
          }
          a = c.interleaved;
          if (null !== a) {
            e = a;
            do
              f = e.lane, J.lanes |= f, le |= f, e = e.next;
            while (e !== a);
          } else null === e && (c.lanes = 0);
          return [b.memoizedState, c.dispatch];
        }
        function Re(a) {
          var b = Oe(), c = b.queue;
          if (null === c) throw Error(n(311));
          c.lastRenderedReducer = a;
          var d = c.dispatch, e = c.pending, f = b.memoizedState;
          if (null !== e) {
            c.pending = null;
            var g5 = e = e.next;
            do
              f = a(f, g5.action), g5 = g5.next;
            while (g5 !== e);
            Vc(f, b.memoizedState) || (G = true);
            b.memoizedState = f;
            null === b.baseQueue && (b.baseState = f);
            c.lastRenderedState = f;
          }
          return [f, d];
        }
        function Se() {
        }
        function Te(a, b) {
          var c = J, d = Oe(), e = b(), f = !Vc(d.memoizedState, e);
          f && (d.memoizedState = e, G = true);
          d = d.queue;
          Ue(Ve.bind(null, c, d, a), [a]);
          if (d.getSnapshot !== b || f || null !== L && L.memoizedState.tag & 1) {
            c.flags |= 2048;
            We(9, Xe.bind(null, c, d, e, b), void 0, null);
            if (null === N) throw Error(n(349));
            0 !== (Be & 30) || Ye(c, b, e);
          }
          return e;
        }
        function Ye(a, b, c) {
          a.flags |= 16384;
          a = { getSnapshot: b, value: c };
          b = J.updateQueue;
          null === b ? (b = { lastEffect: null, stores: null }, J.updateQueue = b, b.stores = [a]) : (c = b.stores, null === c ? b.stores = [a] : c.push(a));
        }
        function Xe(a, b, c, d) {
          b.value = c;
          b.getSnapshot = d;
          Ze(b) && $e(a);
        }
        function Ve(a, b, c) {
          return c(function() {
            Ze(b) && $e(a);
          });
        }
        function Ze(a) {
          var b = a.getSnapshot;
          a = a.value;
          try {
            var c = b();
            return !Vc(a, c);
          } catch (d) {
            return true;
          }
        }
        function $e(a) {
          var b = ce(a, 1);
          null !== b && af(b, a, 1, -1);
        }
        function bf(a) {
          var b = Ne();
          "function" === typeof a && (a = a());
          b.memoizedState = b.baseState = a;
          a = { pending: null, interleaved: null, lanes: 0, dispatch: null, lastRenderedReducer: Pe, lastRenderedState: a };
          b.queue = a;
          a = a.dispatch = cf.bind(null, J, a);
          return [b.memoizedState, a];
        }
        function We(a, b, c, d) {
          a = { tag: a, create: b, destroy: c, deps: d, next: null };
          b = J.updateQueue;
          null === b ? (b = { lastEffect: null, stores: null }, J.updateQueue = b, b.lastEffect = a.next = a) : (c = b.lastEffect, null === c ? b.lastEffect = a.next = a : (d = c.next, c.next = a, a.next = d, b.lastEffect = a));
          return a;
        }
        function df() {
          return Oe().memoizedState;
        }
        function ef(a, b, c, d) {
          var e = Ne();
          J.flags |= a;
          e.memoizedState = We(1 | b, c, void 0, void 0 === d ? null : d);
        }
        function ff(a, b, c, d) {
          var e = Oe();
          d = void 0 === d ? null : d;
          var f = void 0;
          if (null !== K) {
            var g5 = K.memoizedState;
            f = g5.destroy;
            if (null !== d && Ge(d, g5.deps)) {
              e.memoizedState = We(b, c, f, d);
              return;
            }
          }
          J.flags |= a;
          e.memoizedState = We(1 | b, c, f, d);
        }
        function gf(a, b) {
          return ef(8390656, 8, a, b);
        }
        function Ue(a, b) {
          return ff(2048, 8, a, b);
        }
        function hf(a, b) {
          return ff(4, 2, a, b);
        }
        function jf(a, b) {
          return ff(4, 4, a, b);
        }
        function kf(a, b) {
          if ("function" === typeof b) return a = a(), b(a), function() {
            b(null);
          };
          if (null !== b && void 0 !== b) return a = a(), b.current = a, function() {
            b.current = null;
          };
        }
        function lf(a, b, c) {
          c = null !== c && void 0 !== c ? c.concat([a]) : null;
          return ff(4, 4, kf.bind(null, b, a), c);
        }
        function mf() {
        }
        function nf(a, b) {
          var c = Oe();
          b = void 0 === b ? null : b;
          var d = c.memoizedState;
          if (null !== d && null !== b && Ge(b, d[1])) return d[0];
          c.memoizedState = [a, b];
          return a;
        }
        function of(a, b) {
          var c = Oe();
          b = void 0 === b ? null : b;
          var d = c.memoizedState;
          if (null !== d && null !== b && Ge(b, d[1])) return d[0];
          a = a();
          c.memoizedState = [a, b];
          return a;
        }
        function pf(a, b, c) {
          if (0 === (Be & 21)) return a.baseState && (a.baseState = false, G = true), a.memoizedState = c;
          Vc(c, b) || (c = Dc(), J.lanes |= c, le |= c, a.baseState = true);
          return b;
        }
        function qf(a, b) {
          var c = C;
          C = 0 !== c && 4 > c ? c : 4;
          a(true);
          var d = Ae.transition;
          Ae.transition = {};
          try {
            a(false), b();
          } finally {
            C = c, Ae.transition = d;
          }
        }
        function rf() {
          return Oe().memoizedState;
        }
        function sf(a, b, c) {
          var d = tf(a);
          c = { lane: d, action: c, hasEagerState: false, eagerState: null, next: null };
          if (uf(a)) vf(b, c);
          else if (c = be(a, b, c, d), null !== c) {
            var e = O();
            af(c, a, d, e);
            wf(c, b, d);
          }
        }
        function cf(a, b, c) {
          var d = tf(a), e = { lane: d, action: c, hasEagerState: false, eagerState: null, next: null };
          if (uf(a)) vf(b, e);
          else {
            var f = a.alternate;
            if (0 === a.lanes && (null === f || 0 === f.lanes) && (f = b.lastRenderedReducer, null !== f)) try {
              var g5 = b.lastRenderedState, h = f(g5, c);
              e.hasEagerState = true;
              e.eagerState = h;
              if (Vc(h, g5)) {
                var k = b.interleaved;
                null === k ? (e.next = e, ae(b)) : (e.next = k.next, k.next = e);
                b.interleaved = e;
                return;
              }
            } catch (l) {
            } finally {
            }
            c = be(a, b, e, d);
            null !== c && (e = O(), af(c, a, d, e), wf(c, b, d));
          }
        }
        function uf(a) {
          var b = a.alternate;
          return a === J || null !== b && b === J;
        }
        function vf(a, b) {
          De = Ce = true;
          var c = a.pending;
          null === c ? b.next = b : (b.next = c.next, c.next = b);
          a.pending = b;
        }
        function wf(a, b, c) {
          if (0 !== (c & 4194240)) {
            var d = b.lanes;
            d &= a.pendingLanes;
            c |= d;
            b.lanes = c;
            Hc(a, c);
          }
        }
        var Le = { readContext: Zd, useCallback: M, useContext: M, useEffect: M, useImperativeHandle: M, useInsertionEffect: M, useLayoutEffect: M, useMemo: M, useReducer: M, useRef: M, useState: M, useDebugValue: M, useDeferredValue: M, useTransition: M, useMutableSource: M, useSyncExternalStore: M, useId: M, unstable_isNewReconciler: false }, Ie = { readContext: Zd, useCallback: function(a, b) {
          Ne().memoizedState = [a, void 0 === b ? null : b];
          return a;
        }, useContext: Zd, useEffect: gf, useImperativeHandle: function(a, b, c) {
          c = null !== c && void 0 !== c ? c.concat([a]) : null;
          return ef(
            4194308,
            4,
            kf.bind(null, b, a),
            c
          );
        }, useLayoutEffect: function(a, b) {
          return ef(4194308, 4, a, b);
        }, useInsertionEffect: function(a, b) {
          return ef(4, 2, a, b);
        }, useMemo: function(a, b) {
          var c = Ne();
          b = void 0 === b ? null : b;
          a = a();
          c.memoizedState = [a, b];
          return a;
        }, useReducer: function(a, b, c) {
          var d = Ne();
          b = void 0 !== c ? c(b) : b;
          d.memoizedState = d.baseState = b;
          a = { pending: null, interleaved: null, lanes: 0, dispatch: null, lastRenderedReducer: a, lastRenderedState: b };
          d.queue = a;
          a = a.dispatch = sf.bind(null, J, a);
          return [d.memoizedState, a];
        }, useRef: function(a) {
          var b = Ne();
          a = { current: a };
          return b.memoizedState = a;
        }, useState: bf, useDebugValue: mf, useDeferredValue: function(a) {
          return Ne().memoizedState = a;
        }, useTransition: function() {
          var a = bf(false), b = a[0];
          a = qf.bind(null, a[1]);
          Ne().memoizedState = a;
          return [b, a];
        }, useMutableSource: function() {
        }, useSyncExternalStore: function(a, b, c) {
          var d = J, e = Ne();
          if (F) {
            if (void 0 === c) throw Error(n(407));
            c = c();
          } else {
            c = b();
            if (null === N) throw Error(n(349));
            0 !== (Be & 30) || Ye(d, b, c);
          }
          e.memoizedState = c;
          var f = { value: c, getSnapshot: b };
          e.queue = f;
          gf(Ve.bind(
            null,
            d,
            f,
            a
          ), [a]);
          d.flags |= 2048;
          We(9, Xe.bind(null, d, f, c, b), void 0, null);
          return c;
        }, useId: function() {
          var a = Ne(), b = N.identifierPrefix;
          if (F) {
            var c = jd;
            var d = id;
            c = (d & ~(1 << 32 - tc(d) - 1)).toString(32) + c;
            b = ":" + b + "R" + c;
            c = Ee++;
            0 < c && (b += "H" + c.toString(32));
            b += ":";
          } else c = Fe++, b = ":" + b + "r" + c.toString(32) + ":";
          return a.memoizedState = b;
        }, unstable_isNewReconciler: false }, Je = {
          readContext: Zd,
          useCallback: nf,
          useContext: Zd,
          useEffect: Ue,
          useImperativeHandle: lf,
          useInsertionEffect: hf,
          useLayoutEffect: jf,
          useMemo: of,
          useReducer: Qe,
          useRef: df,
          useState: function() {
            return Qe(Pe);
          },
          useDebugValue: mf,
          useDeferredValue: function(a) {
            var b = Oe();
            return pf(b, K.memoizedState, a);
          },
          useTransition: function() {
            var a = Qe(Pe)[0], b = Oe().memoizedState;
            return [a, b];
          },
          useMutableSource: Se,
          useSyncExternalStore: Te,
          useId: rf,
          unstable_isNewReconciler: false
        }, Ke = { readContext: Zd, useCallback: nf, useContext: Zd, useEffect: Ue, useImperativeHandle: lf, useInsertionEffect: hf, useLayoutEffect: jf, useMemo: of, useReducer: Re, useRef: df, useState: function() {
          return Re(Pe);
        }, useDebugValue: mf, useDeferredValue: function(a) {
          var b = Oe();
          return null === K ? b.memoizedState = a : pf(b, K.memoizedState, a);
        }, useTransition: function() {
          var a = Re(Pe)[0], b = Oe().memoizedState;
          return [a, b];
        }, useMutableSource: Se, useSyncExternalStore: Te, useId: rf, unstable_isNewReconciler: false };
        function xf(a, b) {
          if (a && a.defaultProps) {
            b = ca({}, b);
            a = a.defaultProps;
            for (var c in a) void 0 === b[c] && (b[c] = a[c]);
            return b;
          }
          return b;
        }
        function yf(a, b, c, d) {
          b = a.memoizedState;
          c = c(d, b);
          c = null === c || void 0 === c ? b : ca({}, b, c);
          a.memoizedState = c;
          0 === a.lanes && (a.updateQueue.baseState = c);
        }
        var zf = { isMounted: function(a) {
          return (a = a._reactInternals) ? wa(a) === a : false;
        }, enqueueSetState: function(a, b, c) {
          a = a._reactInternals;
          var d = O(), e = tf(a), f = ge(d, e);
          f.payload = b;
          void 0 !== c && null !== c && (f.callback = c);
          b = he(a, f, e);
          null !== b && (af(b, a, e, d), ie(b, a, e));
        }, enqueueReplaceState: function(a, b, c) {
          a = a._reactInternals;
          var d = O(), e = tf(a), f = ge(d, e);
          f.tag = 1;
          f.payload = b;
          void 0 !== c && null !== c && (f.callback = c);
          b = he(a, f, e);
          null !== b && (af(b, a, e, d), ie(b, a, e));
        }, enqueueForceUpdate: function(a, b) {
          a = a._reactInternals;
          var c = O(), d = tf(a), e = ge(c, d);
          e.tag = 2;
          void 0 !== b && null !== b && (e.callback = b);
          b = he(a, e, d);
          null !== b && (af(b, a, d, c), ie(b, a, d));
        } };
        function Af(a, b, c, d, e, f, g5) {
          a = a.stateNode;
          return "function" === typeof a.shouldComponentUpdate ? a.shouldComponentUpdate(d, f, g5) : b.prototype && b.prototype.isPureReactComponent ? !Dd(c, d) || !Dd(e, f) : true;
        }
        function Bf(a, b, c) {
          var d = false, e = jc;
          var f = b.contextType;
          "object" === typeof f && null !== f ? f = Zd(f) : (e = A(b) ? kc : x.current, d = b.contextTypes, f = (d = null !== d && void 0 !== d) ? mc(a, e) : jc);
          b = new b(c, f);
          a.memoizedState = null !== b.state && void 0 !== b.state ? b.state : null;
          b.updater = zf;
          a.stateNode = b;
          b._reactInternals = a;
          d && (a = a.stateNode, a.__reactInternalMemoizedUnmaskedChildContext = e, a.__reactInternalMemoizedMaskedChildContext = f);
          return b;
        }
        function Cf(a, b, c, d) {
          a = b.state;
          "function" === typeof b.componentWillReceiveProps && b.componentWillReceiveProps(c, d);
          "function" === typeof b.UNSAFE_componentWillReceiveProps && b.UNSAFE_componentWillReceiveProps(c, d);
          b.state !== a && zf.enqueueReplaceState(b, b.state, null);
        }
        function Df(a, b, c, d) {
          var e = a.stateNode;
          e.props = c;
          e.state = a.memoizedState;
          e.refs = {};
          ee(a);
          var f = b.contextType;
          "object" === typeof f && null !== f ? e.context = Zd(f) : (f = A(b) ? kc : x.current, e.context = mc(a, f));
          e.state = a.memoizedState;
          f = b.getDerivedStateFromProps;
          "function" === typeof f && (yf(a, b, f, c), e.state = a.memoizedState);
          "function" === typeof b.getDerivedStateFromProps || "function" === typeof e.getSnapshotBeforeUpdate || "function" !== typeof e.UNSAFE_componentWillMount && "function" !== typeof e.componentWillMount || (b = e.state, "function" === typeof e.componentWillMount && e.componentWillMount(), "function" === typeof e.UNSAFE_componentWillMount && e.UNSAFE_componentWillMount(), b !== e.state && zf.enqueueReplaceState(e, e.state, null), ke(a, c, e, d), e.state = a.memoizedState);
          "function" === typeof e.componentDidMount && (a.flags |= 4194308);
        }
        function Ef(a, b) {
          try {
            var c = "", d = b;
            do
              c += Ed(d), d = d.return;
            while (d);
            var e = c;
          } catch (f) {
            e = "\nError generating stack: " + f.message + "\n" + f.stack;
          }
          return { value: a, source: b, stack: e, digest: null };
        }
        function Ff(a, b, c) {
          return { value: a, source: null, stack: null != c ? c : null, digest: null != b ? b : null };
        }
        function Gf(a, b) {
          try {
            console.error(b.value);
          } catch (c) {
            setTimeout(function() {
              throw c;
            });
          }
        }
        var Hf = "function" === typeof WeakMap ? WeakMap : Map;
        function If(a, b, c) {
          c = ge(-1, c);
          c.tag = 3;
          c.payload = { element: null };
          var d = b.value;
          c.callback = function() {
            Jf || (Jf = true, Kf = d);
            Gf(a, b);
          };
          return c;
        }
        function Lf(a, b, c) {
          c = ge(-1, c);
          c.tag = 3;
          var d = a.type.getDerivedStateFromError;
          if ("function" === typeof d) {
            var e = b.value;
            c.payload = function() {
              return d(e);
            };
            c.callback = function() {
              Gf(a, b);
            };
          }
          var f = a.stateNode;
          null !== f && "function" === typeof f.componentDidCatch && (c.callback = function() {
            Gf(a, b);
            "function" !== typeof d && (null === Mf ? Mf = /* @__PURE__ */ new Set([this]) : Mf.add(this));
            var c2 = b.stack;
            this.componentDidCatch(b.value, { componentStack: null !== c2 ? c2 : "" });
          });
          return c;
        }
        function Nf(a, b, c) {
          var d = a.pingCache;
          if (null === d) {
            d = a.pingCache = new Hf();
            var e = /* @__PURE__ */ new Set();
            d.set(b, e);
          } else e = d.get(b), void 0 === e && (e = /* @__PURE__ */ new Set(), d.set(b, e));
          e.has(c) || (e.add(c), a = Of.bind(null, a, b, c), b.then(a, a));
        }
        function Pf(a) {
          do {
            var b;
            if (b = 13 === a.tag) b = a.memoizedState, b = null !== b ? null !== b.dehydrated ? true : false : true;
            if (b) return a;
            a = a.return;
          } while (null !== a);
          return null;
        }
        function Qf(a, b, c, d, e) {
          if (0 === (a.mode & 1)) return a === b ? a.flags |= 65536 : (a.flags |= 128, c.flags |= 131072, c.flags &= -52805, 1 === c.tag && (null === c.alternate ? c.tag = 17 : (b = ge(-1, 1), b.tag = 2, he(c, b, 1))), c.lanes |= 1), a;
          a.flags |= 65536;
          a.lanes = e;
          return a;
        }
        var Rf = da.ReactCurrentOwner, G = false;
        function P(a, b, c, d) {
          b.child = null === a ? Pd(b, null, c, d) : Od(b, a.child, c, d);
        }
        function Sf(a, b, c, d, e) {
          c = c.render;
          var f = b.ref;
          Yd(b, e);
          d = He(a, b, c, d, f, e);
          c = Me();
          if (null !== a && !G) return b.updateQueue = a.updateQueue, b.flags &= -2053, a.lanes &= ~e, Tf(a, b, e);
          F && c && md(b);
          b.flags |= 1;
          P(a, b, d, e);
          return b.child;
        }
        function Uf(a, b, c, d, e) {
          if (null === a) {
            var f = c.type;
            if ("function" === typeof f && !Vf(f) && void 0 === f.defaultProps && null === c.compare && void 0 === c.defaultProps) return b.tag = 15, b.type = f, Wf(a, b, f, d, e);
            a = Ld(c.type, null, d, b, b.mode, e);
            a.ref = b.ref;
            a.return = b;
            return b.child = a;
          }
          f = a.child;
          if (0 === (a.lanes & e)) {
            var g5 = f.memoizedProps;
            c = c.compare;
            c = null !== c ? c : Dd;
            if (c(g5, d) && a.ref === b.ref) return Tf(a, b, e);
          }
          b.flags |= 1;
          a = Jd(f, d);
          a.ref = b.ref;
          a.return = b;
          return b.child = a;
        }
        function Wf(a, b, c, d, e) {
          if (null !== a) {
            var f = a.memoizedProps;
            if (Dd(f, d) && a.ref === b.ref) if (G = false, b.pendingProps = d = f, 0 !== (a.lanes & e)) 0 !== (a.flags & 131072) && (G = true);
            else return b.lanes = a.lanes, Tf(a, b, e);
          }
          return Xf(a, b, c, d, e);
        }
        function Yf(a, b, c) {
          var d = b.pendingProps, e = d.children, f = null !== a ? a.memoizedState : null;
          if ("hidden" === d.mode) if (0 === (b.mode & 1)) b.memoizedState = { baseLanes: 0, cachePool: null, transitions: null }, v(Zf, $f), $f |= c;
          else {
            if (0 === (c & 1073741824)) return a = null !== f ? f.baseLanes | c : c, b.lanes = b.childLanes = 1073741824, b.memoizedState = { baseLanes: a, cachePool: null, transitions: null }, b.updateQueue = null, v(Zf, $f), $f |= a, null;
            b.memoizedState = { baseLanes: 0, cachePool: null, transitions: null };
            d = null !== f ? f.baseLanes : c;
            v(Zf, $f);
            $f |= d;
          }
          else null !== f ? (d = f.baseLanes | c, b.memoizedState = null) : d = c, v(Zf, $f), $f |= d;
          P(a, b, e, c);
          return b.child;
        }
        function ag(a, b) {
          var c = b.ref;
          if (null === a && null !== c || null !== a && a.ref !== c) b.flags |= 512, b.flags |= 2097152;
        }
        function Xf(a, b, c, d, e) {
          var f = A(c) ? kc : x.current;
          f = mc(b, f);
          Yd(b, e);
          c = He(a, b, c, d, f, e);
          d = Me();
          if (null !== a && !G) return b.updateQueue = a.updateQueue, b.flags &= -2053, a.lanes &= ~e, Tf(a, b, e);
          F && d && md(b);
          b.flags |= 1;
          P(a, b, c, e);
          return b.child;
        }
        function bg(a, b, c, d, e) {
          if (A(c)) {
            var f = true;
            qc(b);
          } else f = false;
          Yd(b, e);
          if (null === b.stateNode) cg(a, b), Bf(b, c, d), Df(b, c, d, e), d = true;
          else if (null === a) {
            var g5 = b.stateNode, h = b.memoizedProps;
            g5.props = h;
            var k = g5.context, l = c.contextType;
            "object" === typeof l && null !== l ? l = Zd(l) : (l = A(c) ? kc : x.current, l = mc(b, l));
            var m = c.getDerivedStateFromProps, r = "function" === typeof m || "function" === typeof g5.getSnapshotBeforeUpdate;
            r || "function" !== typeof g5.UNSAFE_componentWillReceiveProps && "function" !== typeof g5.componentWillReceiveProps || (h !== d || k !== l) && Cf(b, g5, d, l);
            de = false;
            var p = b.memoizedState;
            g5.state = p;
            ke(b, d, g5, e);
            k = b.memoizedState;
            h !== d || p !== k || z.current || de ? ("function" === typeof m && (yf(b, c, m, d), k = b.memoizedState), (h = de || Af(b, c, h, d, p, k, l)) ? (r || "function" !== typeof g5.UNSAFE_componentWillMount && "function" !== typeof g5.componentWillMount || ("function" === typeof g5.componentWillMount && g5.componentWillMount(), "function" === typeof g5.UNSAFE_componentWillMount && g5.UNSAFE_componentWillMount()), "function" === typeof g5.componentDidMount && (b.flags |= 4194308)) : ("function" === typeof g5.componentDidMount && (b.flags |= 4194308), b.memoizedProps = d, b.memoizedState = k), g5.props = d, g5.state = k, g5.context = l, d = h) : ("function" === typeof g5.componentDidMount && (b.flags |= 4194308), d = false);
          } else {
            g5 = b.stateNode;
            fe(a, b);
            h = b.memoizedProps;
            l = b.type === b.elementType ? h : xf(b.type, h);
            g5.props = l;
            r = b.pendingProps;
            p = g5.context;
            k = c.contextType;
            "object" === typeof k && null !== k ? k = Zd(k) : (k = A(c) ? kc : x.current, k = mc(b, k));
            var B = c.getDerivedStateFromProps;
            (m = "function" === typeof B || "function" === typeof g5.getSnapshotBeforeUpdate) || "function" !== typeof g5.UNSAFE_componentWillReceiveProps && "function" !== typeof g5.componentWillReceiveProps || (h !== r || p !== k) && Cf(b, g5, d, k);
            de = false;
            p = b.memoizedState;
            g5.state = p;
            ke(b, d, g5, e);
            var w = b.memoizedState;
            h !== r || p !== w || z.current || de ? ("function" === typeof B && (yf(b, c, B, d), w = b.memoizedState), (l = de || Af(b, c, l, d, p, w, k) || false) ? (m || "function" !== typeof g5.UNSAFE_componentWillUpdate && "function" !== typeof g5.componentWillUpdate || ("function" === typeof g5.componentWillUpdate && g5.componentWillUpdate(d, w, k), "function" === typeof g5.UNSAFE_componentWillUpdate && g5.UNSAFE_componentWillUpdate(d, w, k)), "function" === typeof g5.componentDidUpdate && (b.flags |= 4), "function" === typeof g5.getSnapshotBeforeUpdate && (b.flags |= 1024)) : ("function" !== typeof g5.componentDidUpdate || h === a.memoizedProps && p === a.memoizedState || (b.flags |= 4), "function" !== typeof g5.getSnapshotBeforeUpdate || h === a.memoizedProps && p === a.memoizedState || (b.flags |= 1024), b.memoizedProps = d, b.memoizedState = w), g5.props = d, g5.state = w, g5.context = k, d = l) : ("function" !== typeof g5.componentDidUpdate || h === a.memoizedProps && p === a.memoizedState || (b.flags |= 4), "function" !== typeof g5.getSnapshotBeforeUpdate || h === a.memoizedProps && p === a.memoizedState || (b.flags |= 1024), d = false);
          }
          return dg(a, b, c, d, f, e);
        }
        function dg(a, b, c, d, e, f) {
          ag(a, b);
          var g5 = 0 !== (b.flags & 128);
          if (!d && !g5) return e && rc(b, c, false), Tf(a, b, f);
          d = b.stateNode;
          Rf.current = b;
          var h = g5 && "function" !== typeof c.getDerivedStateFromError ? null : d.render();
          b.flags |= 1;
          null !== a && g5 ? (b.child = Od(b, a.child, null, f), b.child = Od(b, null, h, f)) : P(a, b, h, f);
          b.memoizedState = d.state;
          e && rc(b, c, true);
          return b.child;
        }
        function eg(a) {
          var b = a.stateNode;
          b.pendingContext ? oc(a, b.pendingContext, b.pendingContext !== b.context) : b.context && oc(a, b.context, false);
          se(a, b.containerInfo);
        }
        function fg(a, b, c, d, e) {
          Ad();
          Bd(e);
          b.flags |= 256;
          P(a, b, c, d);
          return b.child;
        }
        var gg = { dehydrated: null, treeContext: null, retryLane: 0 };
        function hg(a) {
          return { baseLanes: a, cachePool: null, transitions: null };
        }
        function ig(a, b, c) {
          var d = b.pendingProps, e = I.current, f = false, g5 = 0 !== (b.flags & 128), h;
          (h = g5) || (h = null !== a && null === a.memoizedState ? false : 0 !== (e & 2));
          if (h) f = true, b.flags &= -129;
          else if (null === a || null !== a.memoizedState) e |= 1;
          v(I, e & 1);
          if (null === a) {
            wd(b);
            a = b.memoizedState;
            if (null !== a && (a = a.dehydrated, null !== a)) return 0 === (b.mode & 1) ? b.lanes = 1 : Kb(a) ? b.lanes = 8 : b.lanes = 1073741824, null;
            g5 = d.children;
            a = d.fallback;
            return f ? (d = b.mode, f = b.child, g5 = { mode: "hidden", children: g5 }, 0 === (d & 1) && null !== f ? (f.childLanes = 0, f.pendingProps = g5) : f = jg(g5, d, 0, null), a = Nd(a, d, c, null), f.return = b, a.return = b, f.sibling = a, b.child = f, b.child.memoizedState = hg(c), b.memoizedState = gg, a) : kg(b, g5);
          }
          e = a.memoizedState;
          if (null !== e && (h = e.dehydrated, null !== h)) return lg(a, b, g5, d, h, e, c);
          if (f) {
            f = d.fallback;
            g5 = b.mode;
            e = a.child;
            h = e.sibling;
            var k = { mode: "hidden", children: d.children };
            0 === (g5 & 1) && b.child !== e ? (d = b.child, d.childLanes = 0, d.pendingProps = k, b.deletions = null) : (d = Jd(e, k), d.subtreeFlags = e.subtreeFlags & 14680064);
            null !== h ? f = Jd(h, f) : (f = Nd(f, g5, c, null), f.flags |= 2);
            f.return = b;
            d.return = b;
            d.sibling = f;
            b.child = d;
            d = f;
            f = b.child;
            g5 = a.child.memoizedState;
            g5 = null === g5 ? hg(c) : { baseLanes: g5.baseLanes | c, cachePool: null, transitions: g5.transitions };
            f.memoizedState = g5;
            f.childLanes = a.childLanes & ~c;
            b.memoizedState = gg;
            return d;
          }
          f = a.child;
          a = f.sibling;
          d = Jd(f, { mode: "visible", children: d.children });
          0 === (b.mode & 1) && (d.lanes = c);
          d.return = b;
          d.sibling = null;
          null !== a && (c = b.deletions, null === c ? (b.deletions = [a], b.flags |= 16) : c.push(a));
          b.child = d;
          b.memoizedState = null;
          return d;
        }
        function kg(a, b) {
          b = jg({ mode: "visible", children: b }, a.mode, 0, null);
          b.return = a;
          return a.child = b;
        }
        function mg(a, b, c, d) {
          null !== d && Bd(d);
          Od(b, a.child, null, c);
          a = kg(b, b.pendingProps.children);
          a.flags |= 2;
          b.memoizedState = null;
          return a;
        }
        function lg(a, b, c, d, e, f, g5) {
          if (c) {
            if (b.flags & 256) return b.flags &= -257, d = Ff(Error(n(422))), mg(a, b, g5, d);
            if (null !== b.memoizedState) return b.child = a.child, b.flags |= 128, null;
            f = d.fallback;
            e = b.mode;
            d = jg({ mode: "visible", children: d.children }, e, 0, null);
            f = Nd(f, e, g5, null);
            f.flags |= 2;
            d.return = b;
            f.return = b;
            d.sibling = f;
            b.child = d;
            0 !== (b.mode & 1) && Od(b, a.child, null, g5);
            b.child.memoizedState = hg(g5);
            b.memoizedState = gg;
            return f;
          }
          if (0 === (b.mode & 1)) return mg(a, b, g5, null);
          if (Kb(e)) return d = Lb(e).digest, f = Error(n(419)), d = Ff(
            f,
            d,
            void 0
          ), mg(a, b, g5, d);
          c = 0 !== (g5 & a.childLanes);
          if (G || c) {
            d = N;
            if (null !== d) {
              switch (g5 & -g5) {
                case 4:
                  e = 2;
                  break;
                case 16:
                  e = 8;
                  break;
                case 64:
                case 128:
                case 256:
                case 512:
                case 1024:
                case 2048:
                case 4096:
                case 8192:
                case 16384:
                case 32768:
                case 65536:
                case 131072:
                case 262144:
                case 524288:
                case 1048576:
                case 2097152:
                case 4194304:
                case 8388608:
                case 16777216:
                case 33554432:
                case 67108864:
                  e = 32;
                  break;
                case 536870912:
                  e = 268435456;
                  break;
                default:
                  e = 0;
              }
              e = 0 !== (e & (d.suspendedLanes | g5)) ? 0 : e;
              0 !== e && e !== f.retryLane && (f.retryLane = e, ce(a, e), af(
                d,
                a,
                e,
                -1
              ));
            }
            ng();
            d = Ff(Error(n(421)));
            return mg(a, b, g5, d);
          }
          if (Jb(e)) return b.flags |= 128, b.child = a.child, b = og.bind(null, a), Mb(e, b), null;
          a = f.treeContext;
          Va && (pd = Qb(e), od = b, F = true, rd = null, qd = false, null !== a && (fd[gd++] = id, fd[gd++] = jd, fd[gd++] = hd, id = a.id, jd = a.overflow, hd = b));
          b = kg(b, d.children);
          b.flags |= 4096;
          return b;
        }
        function pg(a, b, c) {
          a.lanes |= b;
          var d = a.alternate;
          null !== d && (d.lanes |= b);
          Xd(a.return, b, c);
        }
        function qg(a, b, c, d, e) {
          var f = a.memoizedState;
          null === f ? a.memoizedState = { isBackwards: b, rendering: null, renderingStartTime: 0, last: d, tail: c, tailMode: e } : (f.isBackwards = b, f.rendering = null, f.renderingStartTime = 0, f.last = d, f.tail = c, f.tailMode = e);
        }
        function rg(a, b, c) {
          var d = b.pendingProps, e = d.revealOrder, f = d.tail;
          P(a, b, d.children, c);
          d = I.current;
          if (0 !== (d & 2)) d = d & 1 | 2, b.flags |= 128;
          else {
            if (null !== a && 0 !== (a.flags & 128)) a: for (a = b.child; null !== a; ) {
              if (13 === a.tag) null !== a.memoizedState && pg(a, c, b);
              else if (19 === a.tag) pg(a, c, b);
              else if (null !== a.child) {
                a.child.return = a;
                a = a.child;
                continue;
              }
              if (a === b) break a;
              for (; null === a.sibling; ) {
                if (null === a.return || a.return === b) break a;
                a = a.return;
              }
              a.sibling.return = a.return;
              a = a.sibling;
            }
            d &= 1;
          }
          v(I, d);
          if (0 === (b.mode & 1)) b.memoizedState = null;
          else switch (e) {
            case "forwards":
              c = b.child;
              for (e = null; null !== c; ) a = c.alternate, null !== a && null === we(a) && (e = c), c = c.sibling;
              c = e;
              null === c ? (e = b.child, b.child = null) : (e = c.sibling, c.sibling = null);
              qg(b, false, e, c, f);
              break;
            case "backwards":
              c = null;
              e = b.child;
              for (b.child = null; null !== e; ) {
                a = e.alternate;
                if (null !== a && null === we(a)) {
                  b.child = e;
                  break;
                }
                a = e.sibling;
                e.sibling = c;
                c = e;
                e = a;
              }
              qg(b, true, c, null, f);
              break;
            case "together":
              qg(b, false, null, null, void 0);
              break;
            default:
              b.memoizedState = null;
          }
          return b.child;
        }
        function cg(a, b) {
          0 === (b.mode & 1) && null !== a && (a.alternate = null, b.alternate = null, b.flags |= 2);
        }
        function Tf(a, b, c) {
          null !== a && (b.dependencies = a.dependencies);
          le |= b.lanes;
          if (0 === (c & b.childLanes)) return null;
          if (null !== a && b.child !== a.child) throw Error(n(153));
          if (null !== b.child) {
            a = b.child;
            c = Jd(a, a.pendingProps);
            b.child = c;
            for (c.return = b; null !== a.sibling; ) a = a.sibling, c = c.sibling = Jd(a, a.pendingProps), c.return = b;
            c.sibling = null;
          }
          return b.child;
        }
        function sg(a, b, c) {
          switch (b.tag) {
            case 3:
              eg(b);
              Ad();
              break;
            case 5:
              ue(b);
              break;
            case 1:
              A(b.type) && qc(b);
              break;
            case 4:
              se(b, b.stateNode.containerInfo);
              break;
            case 10:
              Vd(b, b.type._context, b.memoizedProps.value);
              break;
            case 13:
              var d = b.memoizedState;
              if (null !== d) {
                if (null !== d.dehydrated) return v(I, I.current & 1), b.flags |= 128, null;
                if (0 !== (c & b.child.childLanes)) return ig(a, b, c);
                v(I, I.current & 1);
                a = Tf(a, b, c);
                return null !== a ? a.sibling : null;
              }
              v(I, I.current & 1);
              break;
            case 19:
              d = 0 !== (c & b.childLanes);
              if (0 !== (a.flags & 128)) {
                if (d) return rg(
                  a,
                  b,
                  c
                );
                b.flags |= 128;
              }
              var e = b.memoizedState;
              null !== e && (e.rendering = null, e.tail = null, e.lastEffect = null);
              v(I, I.current);
              if (d) break;
              else return null;
            case 22:
            case 23:
              return b.lanes = 0, Yf(a, b, c);
          }
          return Tf(a, b, c);
        }
        function tg(a) {
          a.flags |= 4;
        }
        function ug(a, b) {
          if (null !== a && a.child === b.child) return true;
          if (0 !== (b.flags & 16)) return false;
          for (a = b.child; null !== a; ) {
            if (0 !== (a.flags & 12854) || 0 !== (a.subtreeFlags & 12854)) return false;
            a = a.sibling;
          }
          return true;
        }
        var vg, wg, xg, yg;
        if (Ta) vg = function(a, b) {
          for (var c = b.child; null !== c; ) {
            if (5 === c.tag || 6 === c.tag) Ka(a, c.stateNode);
            else if (4 !== c.tag && null !== c.child) {
              c.child.return = c;
              c = c.child;
              continue;
            }
            if (c === b) break;
            for (; null === c.sibling; ) {
              if (null === c.return || c.return === b) return;
              c = c.return;
            }
            c.sibling.return = c.return;
            c = c.sibling;
          }
        }, wg = function() {
        }, xg = function(a, b, c, d, e) {
          a = a.memoizedProps;
          if (a !== d) {
            var f = b.stateNode, g5 = re(oe.current);
            c = Ma(f, c, a, d, e, g5);
            (b.updateQueue = c) && tg(b);
          }
        }, yg = function(a, b, c, d) {
          c !== d && tg(b);
        };
        else if (Ua) {
          vg = function(a, b, c, d) {
            for (var e = b.child; null !== e; ) {
              if (5 === e.tag) {
                var f = e.stateNode;
                c && d && (f = Eb(f, e.type, e.memoizedProps, e));
                Ka(a, f);
              } else if (6 === e.tag) f = e.stateNode, c && d && (f = Fb(f, e.memoizedProps, e)), Ka(a, f);
              else if (4 !== e.tag) {
                if (22 === e.tag && null !== e.memoizedState) f = e.child, null !== f && (f.return = e), vg(a, e, true, true);
                else if (null !== e.child) {
                  e.child.return = e;
                  e = e.child;
                  continue;
                }
              }
              if (e === b) break;
              for (; null === e.sibling; ) {
                if (null === e.return || e.return === b) return;
                e = e.return;
              }
              e.sibling.return = e.return;
              e = e.sibling;
            }
          };
          var zg = function(a, b, c, d) {
            for (var e = b.child; null !== e; ) {
              if (5 === e.tag) {
                var f = e.stateNode;
                c && d && (f = Eb(f, e.type, e.memoizedProps, e));
                Ab(a, f);
              } else if (6 === e.tag) f = e.stateNode, c && d && (f = Fb(f, e.memoizedProps, e)), Ab(a, f);
              else if (4 !== e.tag) {
                if (22 === e.tag && null !== e.memoizedState) f = e.child, null !== f && (f.return = e), zg(a, e, true, true);
                else if (null !== e.child) {
                  e.child.return = e;
                  e = e.child;
                  continue;
                }
              }
              if (e === b) break;
              for (; null === e.sibling; ) {
                if (null === e.return || e.return === b) return;
                e = e.return;
              }
              e.sibling.return = e.return;
              e = e.sibling;
            }
          };
          wg = function(a, b) {
            var c = b.stateNode;
            if (!ug(a, b)) {
              a = c.containerInfo;
              var d = zb(a);
              zg(d, b, false, false);
              c.pendingChildren = d;
              tg(b);
              Bb(a, d);
            }
          };
          xg = function(a, b, c, d, e) {
            var f = a.stateNode, g5 = a.memoizedProps;
            if ((a = ug(a, b)) && g5 === d) b.stateNode = f;
            else {
              var h = b.stateNode, k = re(oe.current), l = null;
              g5 !== d && (l = Ma(h, c, g5, d, e, k));
              a && null === l ? b.stateNode = f : (f = yb(f, l, c, g5, d, b, a, h), La(f, c, d, e, k) && tg(b), b.stateNode = f, a ? tg(b) : vg(f, b, false, false));
            }
          };
          yg = function(a, b, c, d) {
            c !== d ? (a = re(qe.current), c = re(oe.current), b.stateNode = Oa(d, a, c, b), tg(b)) : b.stateNode = a.stateNode;
          };
        } else wg = function() {
        }, xg = function() {
        }, yg = function() {
        };
        function Ag(a, b) {
          if (!F) switch (a.tailMode) {
            case "hidden":
              b = a.tail;
              for (var c = null; null !== b; ) null !== b.alternate && (c = b), b = b.sibling;
              null === c ? a.tail = null : c.sibling = null;
              break;
            case "collapsed":
              c = a.tail;
              for (var d = null; null !== c; ) null !== c.alternate && (d = c), c = c.sibling;
              null === d ? b || null === a.tail ? a.tail = null : a.tail.sibling = null : d.sibling = null;
          }
        }
        function Q(a) {
          var b = null !== a.alternate && a.alternate.child === a.child, c = 0, d = 0;
          if (b) for (var e = a.child; null !== e; ) c |= e.lanes | e.childLanes, d |= e.subtreeFlags & 14680064, d |= e.flags & 14680064, e.return = a, e = e.sibling;
          else for (e = a.child; null !== e; ) c |= e.lanes | e.childLanes, d |= e.subtreeFlags, d |= e.flags, e.return = a, e = e.sibling;
          a.subtreeFlags |= d;
          a.childLanes = c;
          return b;
        }
        function Bg(a, b, c) {
          var d = b.pendingProps;
          nd(b);
          switch (b.tag) {
            case 2:
            case 16:
            case 15:
            case 0:
            case 11:
            case 7:
            case 8:
            case 12:
            case 9:
            case 14:
              return Q(b), null;
            case 1:
              return A(b.type) && nc(), Q(b), null;
            case 3:
              c = b.stateNode;
              te();
              q(z);
              q(x);
              ye();
              c.pendingContext && (c.context = c.pendingContext, c.pendingContext = null);
              if (null === a || null === a.child) yd(b) ? tg(b) : null === a || a.memoizedState.isDehydrated && 0 === (b.flags & 256) || (b.flags |= 1024, null !== rd && (Cg(rd), rd = null));
              wg(a, b);
              Q(b);
              return null;
            case 5:
              ve(b);
              c = re(qe.current);
              var e = b.type;
              if (null !== a && null != b.stateNode) xg(a, b, e, d, c), a.ref !== b.ref && (b.flags |= 512, b.flags |= 2097152);
              else {
                if (!d) {
                  if (null === b.stateNode) throw Error(n(166));
                  Q(b);
                  return null;
                }
                a = re(oe.current);
                if (yd(b)) {
                  if (!Va) throw Error(n(175));
                  a = Rb(b.stateNode, b.type, b.memoizedProps, c, a, b, !qd);
                  b.updateQueue = a;
                  null !== a && tg(b);
                } else {
                  var f = Ja(e, d, c, a, b);
                  vg(f, b, false, false);
                  b.stateNode = f;
                  La(f, e, d, c, a) && tg(b);
                }
                null !== b.ref && (b.flags |= 512, b.flags |= 2097152);
              }
              Q(b);
              return null;
            case 6:
              if (a && null != b.stateNode) yg(a, b, a.memoizedProps, d);
              else {
                if ("string" !== typeof d && null === b.stateNode) throw Error(n(166));
                a = re(qe.current);
                c = re(oe.current);
                if (yd(b)) {
                  if (!Va) throw Error(n(176));
                  a = b.stateNode;
                  c = b.memoizedProps;
                  if (d = Sb(a, c, b, !qd)) {
                    if (e = od, null !== e) switch (e.tag) {
                      case 3:
                        $b(e.stateNode.containerInfo, a, c, 0 !== (e.mode & 1));
                        break;
                      case 5:
                        ac(e.type, e.memoizedProps, e.stateNode, a, c, 0 !== (e.mode & 1));
                    }
                  }
                  d && tg(b);
                } else b.stateNode = Oa(d, a, c, b);
              }
              Q(b);
              return null;
            case 13:
              q(I);
              d = b.memoizedState;
              if (null === a || null !== a.memoizedState && null !== a.memoizedState.dehydrated) {
                if (F && null !== pd && 0 !== (b.mode & 1) && 0 === (b.flags & 128)) zd(), Ad(), b.flags |= 98560, e = false;
                else if (e = yd(b), null !== d && null !== d.dehydrated) {
                  if (null === a) {
                    if (!e) throw Error(n(318));
                    if (!Va) throw Error(n(344));
                    e = b.memoizedState;
                    e = null !== e ? e.dehydrated : null;
                    if (!e) throw Error(n(317));
                    Tb(e, b);
                  } else Ad(), 0 === (b.flags & 128) && (b.memoizedState = null), b.flags |= 4;
                  Q(b);
                  e = false;
                } else null !== rd && (Cg(rd), rd = null), e = true;
                if (!e) return b.flags & 65536 ? b : null;
              }
              if (0 !== (b.flags & 128)) return b.lanes = c, b;
              c = null !== d;
              c !== (null !== a && null !== a.memoizedState) && c && (b.child.flags |= 8192, 0 !== (b.mode & 1) && (null === a || 0 !== (I.current & 1) ? 0 === R && (R = 3) : ng()));
              null !== b.updateQueue && (b.flags |= 4);
              Q(b);
              return null;
            case 4:
              return te(), wg(a, b), null === a && Xa(b.stateNode.containerInfo), Q(b), null;
            case 10:
              return Wd(b.type._context), Q(b), null;
            case 17:
              return A(b.type) && nc(), Q(b), null;
            case 19:
              q(I);
              e = b.memoizedState;
              if (null === e) return Q(b), null;
              d = 0 !== (b.flags & 128);
              f = e.rendering;
              if (null === f) if (d) Ag(e, false);
              else {
                if (0 !== R || null !== a && 0 !== (a.flags & 128)) for (a = b.child; null !== a; ) {
                  f = we(a);
                  if (null !== f) {
                    b.flags |= 128;
                    Ag(e, false);
                    a = f.updateQueue;
                    null !== a && (b.updateQueue = a, b.flags |= 4);
                    b.subtreeFlags = 0;
                    a = c;
                    for (c = b.child; null !== c; ) d = c, e = a, d.flags &= 14680066, f = d.alternate, null === f ? (d.childLanes = 0, d.lanes = e, d.child = null, d.subtreeFlags = 0, d.memoizedProps = null, d.memoizedState = null, d.updateQueue = null, d.dependencies = null, d.stateNode = null) : (d.childLanes = f.childLanes, d.lanes = f.lanes, d.child = f.child, d.subtreeFlags = 0, d.deletions = null, d.memoizedProps = f.memoizedProps, d.memoizedState = f.memoizedState, d.updateQueue = f.updateQueue, d.type = f.type, e = f.dependencies, d.dependencies = null === e ? null : { lanes: e.lanes, firstContext: e.firstContext }), c = c.sibling;
                    v(I, I.current & 1 | 2);
                    return b.child;
                  }
                  a = a.sibling;
                }
                null !== e.tail && D() > Dg && (b.flags |= 128, d = true, Ag(e, false), b.lanes = 4194304);
              }
              else {
                if (!d) if (a = we(f), null !== a) {
                  if (b.flags |= 128, d = true, a = a.updateQueue, null !== a && (b.updateQueue = a, b.flags |= 4), Ag(e, true), null === e.tail && "hidden" === e.tailMode && !f.alternate && !F) return Q(b), null;
                } else 2 * D() - e.renderingStartTime > Dg && 1073741824 !== c && (b.flags |= 128, d = true, Ag(e, false), b.lanes = 4194304);
                e.isBackwards ? (f.sibling = b.child, b.child = f) : (a = e.last, null !== a ? a.sibling = f : b.child = f, e.last = f);
              }
              if (null !== e.tail) return b = e.tail, e.rendering = b, e.tail = b.sibling, e.renderingStartTime = D(), b.sibling = null, a = I.current, v(I, d ? a & 1 | 2 : a & 1), b;
              Q(b);
              return null;
            case 22:
            case 23:
              return Eg(), c = null !== b.memoizedState, null !== a && null !== a.memoizedState !== c && (b.flags |= 8192), c && 0 !== (b.mode & 1) ? 0 !== ($f & 1073741824) && (Q(b), Ta && b.subtreeFlags & 6 && (b.flags |= 8192)) : Q(b), null;
            case 24:
              return null;
            case 25:
              return null;
          }
          throw Error(n(
            156,
            b.tag
          ));
        }
        function Fg(a, b) {
          nd(b);
          switch (b.tag) {
            case 1:
              return A(b.type) && nc(), a = b.flags, a & 65536 ? (b.flags = a & -65537 | 128, b) : null;
            case 3:
              return te(), q(z), q(x), ye(), a = b.flags, 0 !== (a & 65536) && 0 === (a & 128) ? (b.flags = a & -65537 | 128, b) : null;
            case 5:
              return ve(b), null;
            case 13:
              q(I);
              a = b.memoizedState;
              if (null !== a && null !== a.dehydrated) {
                if (null === b.alternate) throw Error(n(340));
                Ad();
              }
              a = b.flags;
              return a & 65536 ? (b.flags = a & -65537 | 128, b) : null;
            case 19:
              return q(I), null;
            case 4:
              return te(), null;
            case 10:
              return Wd(b.type._context), null;
            case 22:
            case 23:
              return Eg(), null;
            case 24:
              return null;
            default:
              return null;
          }
        }
        var Gg = false, S = false, Hg = "function" === typeof WeakSet ? WeakSet : Set, T = null;
        function Ig(a, b) {
          var c = a.ref;
          if (null !== c) if ("function" === typeof c) try {
            c(null);
          } catch (d) {
            U(a, b, d);
          }
          else c.current = null;
        }
        function Jg(a, b, c) {
          try {
            c();
          } catch (d) {
            U(a, b, d);
          }
        }
        var Kg = false;
        function Lg(a, b) {
          Ha(a.containerInfo);
          for (T = b; null !== T; ) if (a = T, b = a.child, 0 !== (a.subtreeFlags & 1028) && null !== b) b.return = a, T = b;
          else for (; null !== T; ) {
            a = T;
            try {
              var c = a.alternate;
              if (0 !== (a.flags & 1024)) switch (a.tag) {
                case 0:
                case 11:
                case 15:
                  break;
                case 1:
                  if (null !== c) {
                    var d = c.memoizedProps, e = c.memoizedState, f = a.stateNode, g5 = f.getSnapshotBeforeUpdate(a.elementType === a.type ? d : xf(a.type, d), e);
                    f.__reactInternalSnapshotBeforeUpdate = g5;
                  }
                  break;
                case 3:
                  Ta && xb(a.stateNode.containerInfo);
                  break;
                case 5:
                case 6:
                case 4:
                case 17:
                  break;
                default:
                  throw Error(n(163));
              }
            } catch (h) {
              U(a, a.return, h);
            }
            b = a.sibling;
            if (null !== b) {
              b.return = a.return;
              T = b;
              break;
            }
            T = a.return;
          }
          c = Kg;
          Kg = false;
          return c;
        }
        function Mg(a, b, c) {
          var d = b.updateQueue;
          d = null !== d ? d.lastEffect : null;
          if (null !== d) {
            var e = d = d.next;
            do {
              if ((e.tag & a) === a) {
                var f = e.destroy;
                e.destroy = void 0;
                void 0 !== f && Jg(b, c, f);
              }
              e = e.next;
            } while (e !== d);
          }
        }
        function Ng(a, b) {
          b = b.updateQueue;
          b = null !== b ? b.lastEffect : null;
          if (null !== b) {
            var c = b = b.next;
            do {
              if ((c.tag & a) === a) {
                var d = c.create;
                c.destroy = d();
              }
              c = c.next;
            } while (c !== b);
          }
        }
        function Og(a) {
          var b = a.ref;
          if (null !== b) {
            var c = a.stateNode;
            switch (a.tag) {
              case 5:
                a = Ea(c);
                break;
              default:
                a = c;
            }
            "function" === typeof b ? b(a) : b.current = a;
          }
        }
        function Pg(a) {
          var b = a.alternate;
          null !== b && (a.alternate = null, Pg(b));
          a.child = null;
          a.deletions = null;
          a.sibling = null;
          5 === a.tag && (b = a.stateNode, null !== b && Za(b));
          a.stateNode = null;
          a.return = null;
          a.dependencies = null;
          a.memoizedProps = null;
          a.memoizedState = null;
          a.pendingProps = null;
          a.stateNode = null;
          a.updateQueue = null;
        }
        function Qg(a) {
          return 5 === a.tag || 3 === a.tag || 4 === a.tag;
        }
        function Rg(a) {
          a: for (; ; ) {
            for (; null === a.sibling; ) {
              if (null === a.return || Qg(a.return)) return null;
              a = a.return;
            }
            a.sibling.return = a.return;
            for (a = a.sibling; 5 !== a.tag && 6 !== a.tag && 18 !== a.tag; ) {
              if (a.flags & 2) continue a;
              if (null === a.child || 4 === a.tag) continue a;
              else a.child.return = a, a = a.child;
            }
            if (!(a.flags & 2)) return a.stateNode;
          }
        }
        function Sg(a, b, c) {
          var d = a.tag;
          if (5 === d || 6 === d) a = a.stateNode, b ? pb(c, a, b) : kb(c, a);
          else if (4 !== d && (a = a.child, null !== a)) for (Sg(a, b, c), a = a.sibling; null !== a; ) Sg(a, b, c), a = a.sibling;
        }
        function Tg(a, b, c) {
          var d = a.tag;
          if (5 === d || 6 === d) a = a.stateNode, b ? ob(c, a, b) : jb(c, a);
          else if (4 !== d && (a = a.child, null !== a)) for (Tg(a, b, c), a = a.sibling; null !== a; ) Tg(a, b, c), a = a.sibling;
        }
        var V = null, Ug = false;
        function Vg(a, b, c) {
          for (c = c.child; null !== c; ) Wg(a, b, c), c = c.sibling;
        }
        function Wg(a, b, c) {
          if (Sc && "function" === typeof Sc.onCommitFiberUnmount) try {
            Sc.onCommitFiberUnmount(Rc, c);
          } catch (h) {
          }
          switch (c.tag) {
            case 5:
              S || Ig(c, b);
            case 6:
              if (Ta) {
                var d = V, e = Ug;
                V = null;
                Vg(a, b, c);
                V = d;
                Ug = e;
                null !== V && (Ug ? rb(V, c.stateNode) : qb(V, c.stateNode));
              } else Vg(a, b, c);
              break;
            case 18:
              Ta && null !== V && (Ug ? Yb(V, c.stateNode) : Xb(V, c.stateNode));
              break;
            case 4:
              Ta ? (d = V, e = Ug, V = c.stateNode.containerInfo, Ug = true, Vg(a, b, c), V = d, Ug = e) : (Ua && (d = c.stateNode.containerInfo, e = zb(d), Cb(d, e)), Vg(a, b, c));
              break;
            case 0:
            case 11:
            case 14:
            case 15:
              if (!S && (d = c.updateQueue, null !== d && (d = d.lastEffect, null !== d))) {
                e = d = d.next;
                do {
                  var f = e, g5 = f.destroy;
                  f = f.tag;
                  void 0 !== g5 && (0 !== (f & 2) ? Jg(c, b, g5) : 0 !== (f & 4) && Jg(c, b, g5));
                  e = e.next;
                } while (e !== d);
              }
              Vg(a, b, c);
              break;
            case 1:
              if (!S && (Ig(c, b), d = c.stateNode, "function" === typeof d.componentWillUnmount)) try {
                d.props = c.memoizedProps, d.state = c.memoizedState, d.componentWillUnmount();
              } catch (h) {
                U(c, b, h);
              }
              Vg(a, b, c);
              break;
            case 21:
              Vg(a, b, c);
              break;
            case 22:
              c.mode & 1 ? (S = (d = S) || null !== c.memoizedState, Vg(a, b, c), S = d) : Vg(a, b, c);
              break;
            default:
              Vg(
                a,
                b,
                c
              );
          }
        }
        function Xg(a) {
          var b = a.updateQueue;
          if (null !== b) {
            a.updateQueue = null;
            var c = a.stateNode;
            null === c && (c = a.stateNode = new Hg());
            b.forEach(function(b2) {
              var d = Yg.bind(null, a, b2);
              c.has(b2) || (c.add(b2), b2.then(d, d));
            });
          }
        }
        function Zg(a, b) {
          var c = b.deletions;
          if (null !== c) for (var d = 0; d < c.length; d++) {
            var e = c[d];
            try {
              var f = a, g5 = b;
              if (Ta) {
                var h = g5;
                a: for (; null !== h; ) {
                  switch (h.tag) {
                    case 5:
                      V = h.stateNode;
                      Ug = false;
                      break a;
                    case 3:
                      V = h.stateNode.containerInfo;
                      Ug = true;
                      break a;
                    case 4:
                      V = h.stateNode.containerInfo;
                      Ug = true;
                      break a;
                  }
                  h = h.return;
                }
                if (null === V) throw Error(n(160));
                Wg(f, g5, e);
                V = null;
                Ug = false;
              } else Wg(f, g5, e);
              var k = e.alternate;
              null !== k && (k.return = null);
              e.return = null;
            } catch (l) {
              U(e, b, l);
            }
          }
          if (b.subtreeFlags & 12854) for (b = b.child; null !== b; ) $g(b, a), b = b.sibling;
        }
        function $g(a, b) {
          var c = a.alternate, d = a.flags;
          switch (a.tag) {
            case 0:
            case 11:
            case 14:
            case 15:
              Zg(b, a);
              ah(a);
              if (d & 4) {
                try {
                  Mg(3, a, a.return), Ng(3, a);
                } catch (p) {
                  U(a, a.return, p);
                }
                try {
                  Mg(5, a, a.return);
                } catch (p) {
                  U(a, a.return, p);
                }
              }
              break;
            case 1:
              Zg(b, a);
              ah(a);
              d & 512 && null !== c && Ig(c, c.return);
              break;
            case 5:
              Zg(b, a);
              ah(a);
              d & 512 && null !== c && Ig(c, c.return);
              if (Ta) {
                if (a.flags & 32) {
                  var e = a.stateNode;
                  try {
                    sb(e);
                  } catch (p) {
                    U(a, a.return, p);
                  }
                }
                if (d & 4 && (e = a.stateNode, null != e)) {
                  var f = a.memoizedProps;
                  c = null !== c ? c.memoizedProps : f;
                  d = a.type;
                  b = a.updateQueue;
                  a.updateQueue = null;
                  if (null !== b) try {
                    nb(e, b, d, c, f, a);
                  } catch (p) {
                    U(a, a.return, p);
                  }
                }
              }
              break;
            case 6:
              Zg(b, a);
              ah(a);
              if (d & 4 && Ta) {
                if (null === a.stateNode) throw Error(n(162));
                e = a.stateNode;
                f = a.memoizedProps;
                c = null !== c ? c.memoizedProps : f;
                try {
                  lb(e, c, f);
                } catch (p) {
                  U(a, a.return, p);
                }
              }
              break;
            case 3:
              Zg(b, a);
              ah(a);
              if (d & 4) {
                if (Ta && Va && null !== c && c.memoizedState.isDehydrated) try {
                  Vb(b.containerInfo);
                } catch (p) {
                  U(a, a.return, p);
                }
                if (Ua) {
                  e = b.containerInfo;
                  f = b.pendingChildren;
                  try {
                    Cb(e, f);
                  } catch (p) {
                    U(a, a.return, p);
                  }
                }
              }
              break;
            case 4:
              Zg(
                b,
                a
              );
              ah(a);
              if (d & 4 && Ua) {
                f = a.stateNode;
                e = f.containerInfo;
                f = f.pendingChildren;
                try {
                  Cb(e, f);
                } catch (p) {
                  U(a, a.return, p);
                }
              }
              break;
            case 13:
              Zg(b, a);
              ah(a);
              e = a.child;
              e.flags & 8192 && (f = null !== e.memoizedState, e.stateNode.isHidden = f, !f || null !== e.alternate && null !== e.alternate.memoizedState || (bh = D()));
              d & 4 && Xg(a);
              break;
            case 22:
              var g5 = null !== c && null !== c.memoizedState;
              a.mode & 1 ? (S = (c = S) || g5, Zg(b, a), S = c) : Zg(b, a);
              ah(a);
              if (d & 8192) {
                c = null !== a.memoizedState;
                if ((a.stateNode.isHidden = c) && !g5 && 0 !== (a.mode & 1)) for (T = a, d = a.child; null !== d; ) {
                  for (b = T = d; null !== T; ) {
                    g5 = T;
                    var h = g5.child;
                    switch (g5.tag) {
                      case 0:
                      case 11:
                      case 14:
                      case 15:
                        Mg(4, g5, g5.return);
                        break;
                      case 1:
                        Ig(g5, g5.return);
                        var k = g5.stateNode;
                        if ("function" === typeof k.componentWillUnmount) {
                          var l = g5, m = g5.return;
                          try {
                            var r = l;
                            k.props = r.memoizedProps;
                            k.state = r.memoizedState;
                            k.componentWillUnmount();
                          } catch (p) {
                            U(l, m, p);
                          }
                        }
                        break;
                      case 5:
                        Ig(g5, g5.return);
                        break;
                      case 22:
                        if (null !== g5.memoizedState) {
                          ch(b);
                          continue;
                        }
                    }
                    null !== h ? (h.return = g5, T = h) : ch(b);
                  }
                  d = d.sibling;
                }
                if (Ta) {
                  a: if (d = null, Ta) for (b = a; ; ) {
                    if (5 === b.tag) {
                      if (null === d) {
                        d = b;
                        try {
                          e = b.stateNode, c ? tb(e) : vb(b.stateNode, b.memoizedProps);
                        } catch (p) {
                          U(a, a.return, p);
                        }
                      }
                    } else if (6 === b.tag) {
                      if (null === d) try {
                        f = b.stateNode, c ? ub(f) : wb(f, b.memoizedProps);
                      } catch (p) {
                        U(a, a.return, p);
                      }
                    } else if ((22 !== b.tag && 23 !== b.tag || null === b.memoizedState || b === a) && null !== b.child) {
                      b.child.return = b;
                      b = b.child;
                      continue;
                    }
                    if (b === a) break a;
                    for (; null === b.sibling; ) {
                      if (null === b.return || b.return === a) break a;
                      d === b && (d = null);
                      b = b.return;
                    }
                    d === b && (d = null);
                    b.sibling.return = b.return;
                    b = b.sibling;
                  }
                }
              }
              break;
            case 19:
              Zg(b, a);
              ah(a);
              d & 4 && Xg(a);
              break;
            case 21:
              break;
            default:
              Zg(b, a), ah(a);
          }
        }
        function ah(a) {
          var b = a.flags;
          if (b & 2) {
            try {
              if (Ta) {
                b: {
                  for (var c = a.return; null !== c; ) {
                    if (Qg(c)) {
                      var d = c;
                      break b;
                    }
                    c = c.return;
                  }
                  throw Error(n(160));
                }
                switch (d.tag) {
                  case 5:
                    var e = d.stateNode;
                    d.flags & 32 && (sb(e), d.flags &= -33);
                    var f = Rg(a);
                    Tg(a, f, e);
                    break;
                  case 3:
                  case 4:
                    var g5 = d.stateNode.containerInfo, h = Rg(a);
                    Sg(a, h, g5);
                    break;
                  default:
                    throw Error(n(161));
                }
              }
            } catch (k) {
              U(a, a.return, k);
            }
            a.flags &= -3;
          }
          b & 4096 && (a.flags &= -4097);
        }
        function dh(a, b, c) {
          T = a;
          eh(a, b, c);
        }
        function eh(a, b, c) {
          for (var d = 0 !== (a.mode & 1); null !== T; ) {
            var e = T, f = e.child;
            if (22 === e.tag && d) {
              var g5 = null !== e.memoizedState || Gg;
              if (!g5) {
                var h = e.alternate, k = null !== h && null !== h.memoizedState || S;
                h = Gg;
                var l = S;
                Gg = g5;
                if ((S = k) && !l) for (T = e; null !== T; ) g5 = T, k = g5.child, 22 === g5.tag && null !== g5.memoizedState ? fh(e) : null !== k ? (k.return = g5, T = k) : fh(e);
                for (; null !== f; ) T = f, eh(f, b, c), f = f.sibling;
                T = e;
                Gg = h;
                S = l;
              }
              gh(a, b, c);
            } else 0 !== (e.subtreeFlags & 8772) && null !== f ? (f.return = e, T = f) : gh(a, b, c);
          }
        }
        function gh(a) {
          for (; null !== T; ) {
            var b = T;
            if (0 !== (b.flags & 8772)) {
              var c = b.alternate;
              try {
                if (0 !== (b.flags & 8772)) switch (b.tag) {
                  case 0:
                  case 11:
                  case 15:
                    S || Ng(5, b);
                    break;
                  case 1:
                    var d = b.stateNode;
                    if (b.flags & 4 && !S) if (null === c) d.componentDidMount();
                    else {
                      var e = b.elementType === b.type ? c.memoizedProps : xf(b.type, c.memoizedProps);
                      d.componentDidUpdate(e, c.memoizedState, d.__reactInternalSnapshotBeforeUpdate);
                    }
                    var f = b.updateQueue;
                    null !== f && me(b, f, d);
                    break;
                  case 3:
                    var g5 = b.updateQueue;
                    if (null !== g5) {
                      c = null;
                      if (null !== b.child) switch (b.child.tag) {
                        case 5:
                          c = Ea(b.child.stateNode);
                          break;
                        case 1:
                          c = b.child.stateNode;
                      }
                      me(b, g5, c);
                    }
                    break;
                  case 5:
                    var h = b.stateNode;
                    null === c && b.flags & 4 && mb(h, b.type, b.memoizedProps, b);
                    break;
                  case 6:
                    break;
                  case 4:
                    break;
                  case 12:
                    break;
                  case 13:
                    if (Va && null === b.memoizedState) {
                      var k = b.alternate;
                      if (null !== k) {
                        var l = k.memoizedState;
                        if (null !== l) {
                          var m = l.dehydrated;
                          null !== m && Wb(m);
                        }
                      }
                    }
                    break;
                  case 19:
                  case 17:
                  case 21:
                  case 22:
                  case 23:
                  case 25:
                    break;
                  default:
                    throw Error(n(163));
                }
                S || b.flags & 512 && Og(b);
              } catch (r) {
                U(b, b.return, r);
              }
            }
            if (b === a) {
              T = null;
              break;
            }
            c = b.sibling;
            if (null !== c) {
              c.return = b.return;
              T = c;
              break;
            }
            T = b.return;
          }
        }
        function ch(a) {
          for (; null !== T; ) {
            var b = T;
            if (b === a) {
              T = null;
              break;
            }
            var c = b.sibling;
            if (null !== c) {
              c.return = b.return;
              T = c;
              break;
            }
            T = b.return;
          }
        }
        function fh(a) {
          for (; null !== T; ) {
            var b = T;
            try {
              switch (b.tag) {
                case 0:
                case 11:
                case 15:
                  var c = b.return;
                  try {
                    Ng(4, b);
                  } catch (k) {
                    U(b, c, k);
                  }
                  break;
                case 1:
                  var d = b.stateNode;
                  if ("function" === typeof d.componentDidMount) {
                    var e = b.return;
                    try {
                      d.componentDidMount();
                    } catch (k) {
                      U(b, e, k);
                    }
                  }
                  var f = b.return;
                  try {
                    Og(b);
                  } catch (k) {
                    U(b, f, k);
                  }
                  break;
                case 5:
                  var g5 = b.return;
                  try {
                    Og(b);
                  } catch (k) {
                    U(b, g5, k);
                  }
              }
            } catch (k) {
              U(b, b.return, k);
            }
            if (b === a) {
              T = null;
              break;
            }
            var h = b.sibling;
            if (null !== h) {
              h.return = b.return;
              T = h;
              break;
            }
            T = b.return;
          }
        }
        var hh = 0, ih = 1, jh = 2, kh = 3, lh = 4;
        if ("function" === typeof Symbol && Symbol.for) {
          var mh = Symbol.for;
          hh = mh("selector.component");
          ih = mh("selector.has_pseudo_class");
          jh = mh("selector.role");
          kh = mh("selector.test_id");
          lh = mh("selector.text");
        }
        function nh(a) {
          var b = Wa(a);
          if (null != b) {
            if ("string" !== typeof b.memoizedProps["data-testname"]) throw Error(n(364));
            return b;
          }
          a = cb(a);
          if (null === a) throw Error(n(362));
          return a.stateNode.current;
        }
        function oh(a, b) {
          switch (b.$$typeof) {
            case hh:
              if (a.type === b.value) return true;
              break;
            case ih:
              a: {
                b = b.value;
                a = [a, 0];
                for (var c = 0; c < a.length; ) {
                  var d = a[c++], e = a[c++], f = b[e];
                  if (5 !== d.tag || !fb(d)) {
                    for (; null != f && oh(d, f); ) e++, f = b[e];
                    if (e === b.length) {
                      b = true;
                      break a;
                    } else for (d = d.child; null !== d; ) a.push(d, e), d = d.sibling;
                  }
                }
                b = false;
              }
              return b;
            case jh:
              if (5 === a.tag && gb(a.stateNode, b.value)) return true;
              break;
            case lh:
              if (5 === a.tag || 6 === a.tag) {
                if (a = eb(a), null !== a && 0 <= a.indexOf(b.value)) return true;
              }
              break;
            case kh:
              if (5 === a.tag && (a = a.memoizedProps["data-testname"], "string" === typeof a && a.toLowerCase() === b.value.toLowerCase())) return true;
              break;
            default:
              throw Error(n(365));
          }
          return false;
        }
        function ph(a) {
          switch (a.$$typeof) {
            case hh:
              return "<" + (ua(a.value) || "Unknown") + ">";
            case ih:
              return ":has(" + (ph(a) || "") + ")";
            case jh:
              return '[role="' + a.value + '"]';
            case lh:
              return '"' + a.value + '"';
            case kh:
              return '[data-testname="' + a.value + '"]';
            default:
              throw Error(n(365));
          }
        }
        function qh(a, b) {
          var c = [];
          a = [a, 0];
          for (var d = 0; d < a.length; ) {
            var e = a[d++], f = a[d++], g5 = b[f];
            if (5 !== e.tag || !fb(e)) {
              for (; null != g5 && oh(e, g5); ) f++, g5 = b[f];
              if (f === b.length) c.push(e);
              else for (e = e.child; null !== e; ) a.push(e, f), e = e.sibling;
            }
          }
          return c;
        }
        function rh(a, b) {
          if (!bb) throw Error(n(363));
          a = nh(a);
          a = qh(a, b);
          b = [];
          a = Array.from(a);
          for (var c = 0; c < a.length; ) {
            var d = a[c++];
            if (5 === d.tag) fb(d) || b.push(d.stateNode);
            else for (d = d.child; null !== d; ) a.push(d), d = d.sibling;
          }
          return b;
        }
        var sh = Math.ceil, th = da.ReactCurrentDispatcher, uh = da.ReactCurrentOwner, W = da.ReactCurrentBatchConfig, H = 0, N = null, X = null, Z = 0, $f = 0, Zf = ic(0), R = 0, vh = null, le = 0, wh = 0, xh = 0, yh = null, zh = null, bh = 0, Dg = Infinity, Ah = null;
        function Bh() {
          Dg = D() + 500;
        }
        var Jf = false, Kf = null, Mf = null, Ch = false, Dh = null, Eh = 0, Fh = 0, Gh = null, Hh = -1, Ih = 0;
        function O() {
          return 0 !== (H & 6) ? D() : -1 !== Hh ? Hh : Hh = D();
        }
        function tf(a) {
          if (0 === (a.mode & 1)) return 1;
          if (0 !== (H & 2) && 0 !== Z) return Z & -Z;
          if (null !== Cd.transition) return 0 === Ih && (Ih = Dc()), Ih;
          a = C;
          return 0 !== a ? a : Ya();
        }
        function af(a, b, c, d) {
          if (50 < Fh) throw Fh = 0, Gh = null, Error(n(185));
          Fc(a, c, d);
          if (0 === (H & 2) || a !== N) a === N && (0 === (H & 2) && (wh |= c), 4 === R && Jh(a, Z)), Kh(a, d), 1 === c && 0 === H && 0 === (b.mode & 1) && (Bh(), Xc && ad());
        }
        function Kh(a, b) {
          var c = a.callbackNode;
          Bc(a, b);
          var d = zc(a, a === N ? Z : 0);
          if (0 === d) null !== c && Kc(c), a.callbackNode = null, a.callbackPriority = 0;
          else if (b = d & -d, a.callbackPriority !== b) {
            null != c && Kc(c);
            if (1 === b) 0 === a.tag ? $c(Lh.bind(null, a)) : Zc(Lh.bind(null, a)), $a ? ab(function() {
              0 === (H & 6) && ad();
            }) : Jc(Nc, ad), c = null;
            else {
              switch (Ic(d)) {
                case 1:
                  c = Nc;
                  break;
                case 4:
                  c = Oc;
                  break;
                case 16:
                  c = Pc;
                  break;
                case 536870912:
                  c = Qc;
                  break;
                default:
                  c = Pc;
              }
              c = Mh(c, Nh.bind(null, a));
            }
            a.callbackPriority = b;
            a.callbackNode = c;
          }
        }
        function Nh(a, b) {
          Hh = -1;
          Ih = 0;
          if (0 !== (H & 6)) throw Error(n(327));
          var c = a.callbackNode;
          if (Oh() && a.callbackNode !== c) return null;
          var d = zc(a, a === N ? Z : 0);
          if (0 === d) return null;
          if (0 !== (d & 30) || 0 !== (d & a.expiredLanes) || b) b = Ph(a, d);
          else {
            b = d;
            var e = H;
            H |= 2;
            var f = Qh();
            if (N !== a || Z !== b) Ah = null, Bh(), Rh(a, b);
            do
              try {
                Sh();
                break;
              } catch (h) {
                Th(a, h);
              }
            while (1);
            Ud();
            th.current = f;
            H = e;
            null !== X ? b = 0 : (N = null, Z = 0, b = R);
          }
          if (0 !== b) {
            2 === b && (e = Cc(a), 0 !== e && (d = e, b = Uh(a, e)));
            if (1 === b) throw c = vh, Rh(a, 0), Jh(a, d), Kh(a, D()), c;
            if (6 === b) Jh(a, d);
            else {
              e = a.current.alternate;
              if (0 === (d & 30) && !Vh(e) && (b = Ph(a, d), 2 === b && (f = Cc(a), 0 !== f && (d = f, b = Uh(a, f))), 1 === b)) throw c = vh, Rh(a, 0), Jh(a, d), Kh(a, D()), c;
              a.finishedWork = e;
              a.finishedLanes = d;
              switch (b) {
                case 0:
                case 1:
                  throw Error(n(345));
                case 2:
                  Wh(a, zh, Ah);
                  break;
                case 3:
                  Jh(a, d);
                  if ((d & 130023424) === d && (b = bh + 500 - D(), 10 < b)) {
                    if (0 !== zc(a, 0)) break;
                    e = a.suspendedLanes;
                    if ((e & d) !== d) {
                      O();
                      a.pingedLanes |= a.suspendedLanes & e;
                      break;
                    }
                    a.timeoutHandle = Pa(Wh.bind(null, a, zh, Ah), b);
                    break;
                  }
                  Wh(a, zh, Ah);
                  break;
                case 4:
                  Jh(a, d);
                  if ((d & 4194240) === d) break;
                  b = a.eventTimes;
                  for (e = -1; 0 < d; ) {
                    var g5 = 31 - tc(d);
                    f = 1 << g5;
                    g5 = b[g5];
                    g5 > e && (e = g5);
                    d &= ~f;
                  }
                  d = e;
                  d = D() - d;
                  d = (120 > d ? 120 : 480 > d ? 480 : 1080 > d ? 1080 : 1920 > d ? 1920 : 3e3 > d ? 3e3 : 4320 > d ? 4320 : 1960 * sh(d / 1960)) - d;
                  if (10 < d) {
                    a.timeoutHandle = Pa(Wh.bind(null, a, zh, Ah), d);
                    break;
                  }
                  Wh(a, zh, Ah);
                  break;
                case 5:
                  Wh(a, zh, Ah);
                  break;
                default:
                  throw Error(n(329));
              }
            }
          }
          Kh(a, D());
          return a.callbackNode === c ? Nh.bind(null, a) : null;
        }
        function Uh(a, b) {
          var c = yh;
          a.current.memoizedState.isDehydrated && (Rh(a, b).flags |= 256);
          a = Ph(a, b);
          2 !== a && (b = zh, zh = c, null !== b && Cg(b));
          return a;
        }
        function Cg(a) {
          null === zh ? zh = a : zh.push.apply(zh, a);
        }
        function Vh(a) {
          for (var b = a; ; ) {
            if (b.flags & 16384) {
              var c = b.updateQueue;
              if (null !== c && (c = c.stores, null !== c)) for (var d = 0; d < c.length; d++) {
                var e = c[d], f = e.getSnapshot;
                e = e.value;
                try {
                  if (!Vc(f(), e)) return false;
                } catch (g5) {
                  return false;
                }
              }
            }
            c = b.child;
            if (b.subtreeFlags & 16384 && null !== c) c.return = b, b = c;
            else {
              if (b === a) break;
              for (; null === b.sibling; ) {
                if (null === b.return || b.return === a) return true;
                b = b.return;
              }
              b.sibling.return = b.return;
              b = b.sibling;
            }
          }
          return true;
        }
        function Jh(a, b) {
          b &= ~xh;
          b &= ~wh;
          a.suspendedLanes |= b;
          a.pingedLanes &= ~b;
          for (a = a.expirationTimes; 0 < b; ) {
            var c = 31 - tc(b), d = 1 << c;
            a[c] = -1;
            b &= ~d;
          }
        }
        function Lh(a) {
          if (0 !== (H & 6)) throw Error(n(327));
          Oh();
          var b = zc(a, 0);
          if (0 === (b & 1)) return Kh(a, D()), null;
          var c = Ph(a, b);
          if (0 !== a.tag && 2 === c) {
            var d = Cc(a);
            0 !== d && (b = d, c = Uh(a, d));
          }
          if (1 === c) throw c = vh, Rh(a, 0), Jh(a, b), Kh(a, D()), c;
          if (6 === c) throw Error(n(345));
          a.finishedWork = a.current.alternate;
          a.finishedLanes = b;
          Wh(a, zh, Ah);
          Kh(a, D());
          return null;
        }
        function Xh(a) {
          null !== Dh && 0 === Dh.tag && 0 === (H & 6) && Oh();
          var b = H;
          H |= 1;
          var c = W.transition, d = C;
          try {
            if (W.transition = null, C = 1, a) return a();
          } finally {
            C = d, W.transition = c, H = b, 0 === (H & 6) && ad();
          }
        }
        function Eg() {
          $f = Zf.current;
          q(Zf);
        }
        function Rh(a, b) {
          a.finishedWork = null;
          a.finishedLanes = 0;
          var c = a.timeoutHandle;
          c !== Ra && (a.timeoutHandle = Ra, Qa(c));
          if (null !== X) for (c = X.return; null !== c; ) {
            var d = c;
            nd(d);
            switch (d.tag) {
              case 1:
                d = d.type.childContextTypes;
                null !== d && void 0 !== d && nc();
                break;
              case 3:
                te();
                q(z);
                q(x);
                ye();
                break;
              case 5:
                ve(d);
                break;
              case 4:
                te();
                break;
              case 13:
                q(I);
                break;
              case 19:
                q(I);
                break;
              case 10:
                Wd(d.type._context);
                break;
              case 22:
              case 23:
                Eg();
            }
            c = c.return;
          }
          N = a;
          X = a = Jd(a.current, null);
          Z = $f = b;
          R = 0;
          vh = null;
          xh = wh = le = 0;
          zh = yh = null;
          if (null !== $d) {
            for (b = 0; b < $d.length; b++) if (c = $d[b], d = c.interleaved, null !== d) {
              c.interleaved = null;
              var e = d.next, f = c.pending;
              if (null !== f) {
                var g5 = f.next;
                f.next = e;
                d.next = g5;
              }
              c.pending = d;
            }
            $d = null;
          }
          return a;
        }
        function Th(a, b) {
          do {
            var c = X;
            try {
              Ud();
              ze.current = Le;
              if (Ce) {
                for (var d = J.memoizedState; null !== d; ) {
                  var e = d.queue;
                  null !== e && (e.pending = null);
                  d = d.next;
                }
                Ce = false;
              }
              Be = 0;
              L = K = J = null;
              De = false;
              Ee = 0;
              uh.current = null;
              if (null === c || null === c.return) {
                R = 1;
                vh = b;
                X = null;
                break;
              }
              a: {
                var f = a, g5 = c.return, h = c, k = b;
                b = Z;
                h.flags |= 32768;
                if (null !== k && "object" === typeof k && "function" === typeof k.then) {
                  var l = k, m = h, r = m.tag;
                  if (0 === (m.mode & 1) && (0 === r || 11 === r || 15 === r)) {
                    var p = m.alternate;
                    p ? (m.updateQueue = p.updateQueue, m.memoizedState = p.memoizedState, m.lanes = p.lanes) : (m.updateQueue = null, m.memoizedState = null);
                  }
                  var B = Pf(g5);
                  if (null !== B) {
                    B.flags &= -257;
                    Qf(B, g5, h, f, b);
                    B.mode & 1 && Nf(f, l, b);
                    b = B;
                    k = l;
                    var w = b.updateQueue;
                    if (null === w) {
                      var Y = /* @__PURE__ */ new Set();
                      Y.add(k);
                      b.updateQueue = Y;
                    } else w.add(k);
                    break a;
                  } else {
                    if (0 === (b & 1)) {
                      Nf(f, l, b);
                      ng();
                      break a;
                    }
                    k = Error(n(426));
                  }
                } else if (F && h.mode & 1) {
                  var ya = Pf(g5);
                  if (null !== ya) {
                    0 === (ya.flags & 65536) && (ya.flags |= 256);
                    Qf(ya, g5, h, f, b);
                    Bd(Ef(k, h));
                    break a;
                  }
                }
                f = k = Ef(k, h);
                4 !== R && (R = 2);
                null === yh ? yh = [f] : yh.push(f);
                f = g5;
                do {
                  switch (f.tag) {
                    case 3:
                      f.flags |= 65536;
                      b &= -b;
                      f.lanes |= b;
                      var E = If(f, k, b);
                      je(f, E);
                      break a;
                    case 1:
                      h = k;
                      var u = f.type, t = f.stateNode;
                      if (0 === (f.flags & 128) && ("function" === typeof u.getDerivedStateFromError || null !== t && "function" === typeof t.componentDidCatch && (null === Mf || !Mf.has(t)))) {
                        f.flags |= 65536;
                        b &= -b;
                        f.lanes |= b;
                        var Db = Lf(f, h, b);
                        je(f, Db);
                        break a;
                      }
                  }
                  f = f.return;
                } while (null !== f);
              }
              Yh(c);
            } catch (lc) {
              b = lc;
              X === c && null !== c && (X = c = c.return);
              continue;
            }
            break;
          } while (1);
        }
        function Qh() {
          var a = th.current;
          th.current = Le;
          return null === a ? Le : a;
        }
        function ng() {
          if (0 === R || 3 === R || 2 === R) R = 4;
          null === N || 0 === (le & 268435455) && 0 === (wh & 268435455) || Jh(N, Z);
        }
        function Ph(a, b) {
          var c = H;
          H |= 2;
          var d = Qh();
          if (N !== a || Z !== b) Ah = null, Rh(a, b);
          do
            try {
              Zh();
              break;
            } catch (e) {
              Th(a, e);
            }
          while (1);
          Ud();
          H = c;
          th.current = d;
          if (null !== X) throw Error(n(261));
          N = null;
          Z = 0;
          return R;
        }
        function Zh() {
          for (; null !== X; ) $h(X);
        }
        function Sh() {
          for (; null !== X && !Lc(); ) $h(X);
        }
        function $h(a) {
          var b = ai(a.alternate, a, $f);
          a.memoizedProps = a.pendingProps;
          null === b ? Yh(a) : X = b;
          uh.current = null;
        }
        function Yh(a) {
          var b = a;
          do {
            var c = b.alternate;
            a = b.return;
            if (0 === (b.flags & 32768)) {
              if (c = Bg(c, b, $f), null !== c) {
                X = c;
                return;
              }
            } else {
              c = Fg(c, b);
              if (null !== c) {
                c.flags &= 32767;
                X = c;
                return;
              }
              if (null !== a) a.flags |= 32768, a.subtreeFlags = 0, a.deletions = null;
              else {
                R = 6;
                X = null;
                return;
              }
            }
            b = b.sibling;
            if (null !== b) {
              X = b;
              return;
            }
            X = b = a;
          } while (null !== b);
          0 === R && (R = 5);
        }
        function Wh(a, b, c) {
          var d = C, e = W.transition;
          try {
            W.transition = null, C = 1, bi(a, b, c, d);
          } finally {
            W.transition = e, C = d;
          }
          return null;
        }
        function bi(a, b, c, d) {
          do
            Oh();
          while (null !== Dh);
          if (0 !== (H & 6)) throw Error(n(327));
          c = a.finishedWork;
          var e = a.finishedLanes;
          if (null === c) return null;
          a.finishedWork = null;
          a.finishedLanes = 0;
          if (c === a.current) throw Error(n(177));
          a.callbackNode = null;
          a.callbackPriority = 0;
          var f = c.lanes | c.childLanes;
          Gc(a, f);
          a === N && (X = N = null, Z = 0);
          0 === (c.subtreeFlags & 2064) && 0 === (c.flags & 2064) || Ch || (Ch = true, Mh(Pc, function() {
            Oh();
            return null;
          }));
          f = 0 !== (c.flags & 15990);
          if (0 !== (c.subtreeFlags & 15990) || f) {
            f = W.transition;
            W.transition = null;
            var g5 = C;
            C = 1;
            var h = H;
            H |= 4;
            uh.current = null;
            Lg(a, c);
            $g(c, a);
            Ia(a.containerInfo);
            a.current = c;
            dh(c, a, e);
            Mc();
            H = h;
            C = g5;
            W.transition = f;
          } else a.current = c;
          Ch && (Ch = false, Dh = a, Eh = e);
          f = a.pendingLanes;
          0 === f && (Mf = null);
          Tc(c.stateNode, d);
          Kh(a, D());
          if (null !== b) for (d = a.onRecoverableError, c = 0; c < b.length; c++) e = b[c], d(e.value, { componentStack: e.stack, digest: e.digest });
          if (Jf) throw Jf = false, a = Kf, Kf = null, a;
          0 !== (Eh & 1) && 0 !== a.tag && Oh();
          f = a.pendingLanes;
          0 !== (f & 1) ? a === Gh ? Fh++ : (Fh = 0, Gh = a) : Fh = 0;
          ad();
          return null;
        }
        function Oh() {
          if (null !== Dh) {
            var a = Ic(Eh), b = W.transition, c = C;
            try {
              W.transition = null;
              C = 16 > a ? 16 : a;
              if (null === Dh) var d = false;
              else {
                a = Dh;
                Dh = null;
                Eh = 0;
                if (0 !== (H & 6)) throw Error(n(331));
                var e = H;
                H |= 4;
                for (T = a.current; null !== T; ) {
                  var f = T, g5 = f.child;
                  if (0 !== (T.flags & 16)) {
                    var h = f.deletions;
                    if (null !== h) {
                      for (var k = 0; k < h.length; k++) {
                        var l = h[k];
                        for (T = l; null !== T; ) {
                          var m = T;
                          switch (m.tag) {
                            case 0:
                            case 11:
                            case 15:
                              Mg(8, m, f);
                          }
                          var r = m.child;
                          if (null !== r) r.return = m, T = r;
                          else for (; null !== T; ) {
                            m = T;
                            var p = m.sibling, B = m.return;
                            Pg(m);
                            if (m === l) {
                              T = null;
                              break;
                            }
                            if (null !== p) {
                              p.return = B;
                              T = p;
                              break;
                            }
                            T = B;
                          }
                        }
                      }
                      var w = f.alternate;
                      if (null !== w) {
                        var Y = w.child;
                        if (null !== Y) {
                          w.child = null;
                          do {
                            var ya = Y.sibling;
                            Y.sibling = null;
                            Y = ya;
                          } while (null !== Y);
                        }
                      }
                      T = f;
                    }
                  }
                  if (0 !== (f.subtreeFlags & 2064) && null !== g5) g5.return = f, T = g5;
                  else b: for (; null !== T; ) {
                    f = T;
                    if (0 !== (f.flags & 2048)) switch (f.tag) {
                      case 0:
                      case 11:
                      case 15:
                        Mg(9, f, f.return);
                    }
                    var E = f.sibling;
                    if (null !== E) {
                      E.return = f.return;
                      T = E;
                      break b;
                    }
                    T = f.return;
                  }
                }
                var u = a.current;
                for (T = u; null !== T; ) {
                  g5 = T;
                  var t = g5.child;
                  if (0 !== (g5.subtreeFlags & 2064) && null !== t) t.return = g5, T = t;
                  else b: for (g5 = u; null !== T; ) {
                    h = T;
                    if (0 !== (h.flags & 2048)) try {
                      switch (h.tag) {
                        case 0:
                        case 11:
                        case 15:
                          Ng(9, h);
                      }
                    } catch (lc) {
                      U(h, h.return, lc);
                    }
                    if (h === g5) {
                      T = null;
                      break b;
                    }
                    var Db = h.sibling;
                    if (null !== Db) {
                      Db.return = h.return;
                      T = Db;
                      break b;
                    }
                    T = h.return;
                  }
                }
                H = e;
                ad();
                if (Sc && "function" === typeof Sc.onPostCommitFiberRoot) try {
                  Sc.onPostCommitFiberRoot(Rc, a);
                } catch (lc) {
                }
                d = true;
              }
              return d;
            } finally {
              C = c, W.transition = b;
            }
          }
          return false;
        }
        function ci(a, b, c) {
          b = Ef(c, b);
          b = If(a, b, 1);
          a = he(a, b, 1);
          b = O();
          null !== a && (Fc(a, 1, b), Kh(a, b));
        }
        function U(a, b, c) {
          if (3 === a.tag) ci(a, a, c);
          else for (; null !== b; ) {
            if (3 === b.tag) {
              ci(b, a, c);
              break;
            } else if (1 === b.tag) {
              var d = b.stateNode;
              if ("function" === typeof b.type.getDerivedStateFromError || "function" === typeof d.componentDidCatch && (null === Mf || !Mf.has(d))) {
                a = Ef(c, a);
                a = Lf(b, a, 1);
                b = he(b, a, 1);
                a = O();
                null !== b && (Fc(b, 1, a), Kh(b, a));
                break;
              }
            }
            b = b.return;
          }
        }
        function Of(a, b, c) {
          var d = a.pingCache;
          null !== d && d.delete(b);
          b = O();
          a.pingedLanes |= a.suspendedLanes & c;
          N === a && (Z & c) === c && (4 === R || 3 === R && (Z & 130023424) === Z && 500 > D() - bh ? Rh(a, 0) : xh |= c);
          Kh(a, b);
        }
        function di(a, b) {
          0 === b && (0 === (a.mode & 1) ? b = 1 : (b = xc, xc <<= 1, 0 === (xc & 130023424) && (xc = 4194304)));
          var c = O();
          a = ce(a, b);
          null !== a && (Fc(a, b, c), Kh(a, c));
        }
        function og(a) {
          var b = a.memoizedState, c = 0;
          null !== b && (c = b.retryLane);
          di(a, c);
        }
        function Yg(a, b) {
          var c = 0;
          switch (a.tag) {
            case 13:
              var d = a.stateNode;
              var e = a.memoizedState;
              null !== e && (c = e.retryLane);
              break;
            case 19:
              d = a.stateNode;
              break;
            default:
              throw Error(n(314));
          }
          null !== d && d.delete(b);
          di(a, c);
        }
        var ai;
        ai = function(a, b, c) {
          if (null !== a) if (a.memoizedProps !== b.pendingProps || z.current) G = true;
          else {
            if (0 === (a.lanes & c) && 0 === (b.flags & 128)) return G = false, sg(a, b, c);
            G = 0 !== (a.flags & 131072) ? true : false;
          }
          else G = false, F && 0 !== (b.flags & 1048576) && ld(b, ed, b.index);
          b.lanes = 0;
          switch (b.tag) {
            case 2:
              var d = b.type;
              cg(a, b);
              a = b.pendingProps;
              var e = mc(b, x.current);
              Yd(b, c);
              e = He(null, b, d, a, e, c);
              var f = Me();
              b.flags |= 1;
              "object" === typeof e && null !== e && "function" === typeof e.render && void 0 === e.$$typeof ? (b.tag = 1, b.memoizedState = null, b.updateQueue = null, A(d) ? (f = true, qc(b)) : f = false, b.memoizedState = null !== e.state && void 0 !== e.state ? e.state : null, ee(b), e.updater = zf, b.stateNode = e, e._reactInternals = b, Df(b, d, a, c), b = dg(null, b, d, true, f, c)) : (b.tag = 0, F && f && md(b), P(null, b, e, c), b = b.child);
              return b;
            case 16:
              d = b.elementType;
              a: {
                cg(a, b);
                a = b.pendingProps;
                e = d._init;
                d = e(d._payload);
                b.type = d;
                e = b.tag = ei(d);
                a = xf(d, a);
                switch (e) {
                  case 0:
                    b = Xf(null, b, d, a, c);
                    break a;
                  case 1:
                    b = bg(null, b, d, a, c);
                    break a;
                  case 11:
                    b = Sf(null, b, d, a, c);
                    break a;
                  case 14:
                    b = Uf(null, b, d, xf(d.type, a), c);
                    break a;
                }
                throw Error(n(
                  306,
                  d,
                  ""
                ));
              }
              return b;
            case 0:
              return d = b.type, e = b.pendingProps, e = b.elementType === d ? e : xf(d, e), Xf(a, b, d, e, c);
            case 1:
              return d = b.type, e = b.pendingProps, e = b.elementType === d ? e : xf(d, e), bg(a, b, d, e, c);
            case 3:
              a: {
                eg(b);
                if (null === a) throw Error(n(387));
                d = b.pendingProps;
                f = b.memoizedState;
                e = f.element;
                fe(a, b);
                ke(b, d, null, c);
                var g5 = b.memoizedState;
                d = g5.element;
                if (Va && f.isDehydrated) if (f = { element: d, isDehydrated: false, cache: g5.cache, pendingSuspenseBoundaries: g5.pendingSuspenseBoundaries, transitions: g5.transitions }, b.updateQueue.baseState = f, b.memoizedState = f, b.flags & 256) {
                  e = Ef(Error(n(423)), b);
                  b = fg(a, b, d, c, e);
                  break a;
                } else if (d !== e) {
                  e = Ef(Error(n(424)), b);
                  b = fg(a, b, d, c, e);
                  break a;
                } else for (Va && (pd = Pb(b.stateNode.containerInfo), od = b, F = true, rd = null, qd = false), c = Pd(b, null, d, c), b.child = c; c; ) c.flags = c.flags & -3 | 4096, c = c.sibling;
                else {
                  Ad();
                  if (d === e) {
                    b = Tf(a, b, c);
                    break a;
                  }
                  P(a, b, d, c);
                }
                b = b.child;
              }
              return b;
            case 5:
              return ue(b), null === a && wd(b), d = b.type, e = b.pendingProps, f = null !== a ? a.memoizedProps : null, g5 = e.children, Na(d, e) ? g5 = null : null !== f && Na(d, f) && (b.flags |= 32), ag(a, b), P(a, b, g5, c), b.child;
            case 6:
              return null === a && wd(b), null;
            case 13:
              return ig(a, b, c);
            case 4:
              return se(b, b.stateNode.containerInfo), d = b.pendingProps, null === a ? b.child = Od(b, null, d, c) : P(a, b, d, c), b.child;
            case 11:
              return d = b.type, e = b.pendingProps, e = b.elementType === d ? e : xf(d, e), Sf(a, b, d, e, c);
            case 7:
              return P(a, b, b.pendingProps, c), b.child;
            case 8:
              return P(a, b, b.pendingProps.children, c), b.child;
            case 12:
              return P(a, b, b.pendingProps.children, c), b.child;
            case 10:
              a: {
                d = b.type._context;
                e = b.pendingProps;
                f = b.memoizedProps;
                g5 = e.value;
                Vd(b, d, g5);
                if (null !== f) if (Vc(f.value, g5)) {
                  if (f.children === e.children && !z.current) {
                    b = Tf(a, b, c);
                    break a;
                  }
                } else for (f = b.child, null !== f && (f.return = b); null !== f; ) {
                  var h = f.dependencies;
                  if (null !== h) {
                    g5 = f.child;
                    for (var k = h.firstContext; null !== k; ) {
                      if (k.context === d) {
                        if (1 === f.tag) {
                          k = ge(-1, c & -c);
                          k.tag = 2;
                          var l = f.updateQueue;
                          if (null !== l) {
                            l = l.shared;
                            var m = l.pending;
                            null === m ? k.next = k : (k.next = m.next, m.next = k);
                            l.pending = k;
                          }
                        }
                        f.lanes |= c;
                        k = f.alternate;
                        null !== k && (k.lanes |= c);
                        Xd(f.return, c, b);
                        h.lanes |= c;
                        break;
                      }
                      k = k.next;
                    }
                  } else if (10 === f.tag) g5 = f.type === b.type ? null : f.child;
                  else if (18 === f.tag) {
                    g5 = f.return;
                    if (null === g5) throw Error(n(341));
                    g5.lanes |= c;
                    h = g5.alternate;
                    null !== h && (h.lanes |= c);
                    Xd(g5, c, b);
                    g5 = f.sibling;
                  } else g5 = f.child;
                  if (null !== g5) g5.return = f;
                  else for (g5 = f; null !== g5; ) {
                    if (g5 === b) {
                      g5 = null;
                      break;
                    }
                    f = g5.sibling;
                    if (null !== f) {
                      f.return = g5.return;
                      g5 = f;
                      break;
                    }
                    g5 = g5.return;
                  }
                  f = g5;
                }
                P(a, b, e.children, c);
                b = b.child;
              }
              return b;
            case 9:
              return e = b.type, d = b.pendingProps.children, Yd(b, c), e = Zd(e), d = d(e), b.flags |= 1, P(a, b, d, c), b.child;
            case 14:
              return d = b.type, e = xf(d, b.pendingProps), e = xf(d.type, e), Uf(a, b, d, e, c);
            case 15:
              return Wf(a, b, b.type, b.pendingProps, c);
            case 17:
              return d = b.type, e = b.pendingProps, e = b.elementType === d ? e : xf(d, e), cg(a, b), b.tag = 1, A(d) ? (a = true, qc(b)) : a = false, Yd(b, c), Bf(b, d, e), Df(b, d, e, c), dg(null, b, d, true, a, c);
            case 19:
              return rg(a, b, c);
            case 22:
              return Yf(a, b, c);
          }
          throw Error(n(156, b.tag));
        };
        function Mh(a, b) {
          return Jc(a, b);
        }
        function fi(a, b, c, d) {
          this.tag = a;
          this.key = c;
          this.sibling = this.child = this.return = this.stateNode = this.type = this.elementType = null;
          this.index = 0;
          this.ref = null;
          this.pendingProps = b;
          this.dependencies = this.memoizedState = this.updateQueue = this.memoizedProps = null;
          this.mode = d;
          this.subtreeFlags = this.flags = 0;
          this.deletions = null;
          this.childLanes = this.lanes = 0;
          this.alternate = null;
        }
        function td(a, b, c, d) {
          return new fi(a, b, c, d);
        }
        function Vf(a) {
          a = a.prototype;
          return !(!a || !a.isReactComponent);
        }
        function ei(a) {
          if ("function" === typeof a) return Vf(a) ? 1 : 0;
          if (void 0 !== a && null !== a) {
            a = a.$$typeof;
            if (a === ma) return 11;
            if (a === pa) return 14;
          }
          return 2;
        }
        function Jd(a, b) {
          var c = a.alternate;
          null === c ? (c = td(a.tag, b, a.key, a.mode), c.elementType = a.elementType, c.type = a.type, c.stateNode = a.stateNode, c.alternate = a, a.alternate = c) : (c.pendingProps = b, c.type = a.type, c.flags = 0, c.subtreeFlags = 0, c.deletions = null);
          c.flags = a.flags & 14680064;
          c.childLanes = a.childLanes;
          c.lanes = a.lanes;
          c.child = a.child;
          c.memoizedProps = a.memoizedProps;
          c.memoizedState = a.memoizedState;
          c.updateQueue = a.updateQueue;
          b = a.dependencies;
          c.dependencies = null === b ? null : { lanes: b.lanes, firstContext: b.firstContext };
          c.sibling = a.sibling;
          c.index = a.index;
          c.ref = a.ref;
          return c;
        }
        function Ld(a, b, c, d, e, f) {
          var g5 = 2;
          d = a;
          if ("function" === typeof a) Vf(a) && (g5 = 1);
          else if ("string" === typeof a) g5 = 5;
          else a: switch (a) {
            case ha:
              return Nd(c.children, e, f, b);
            case ia:
              g5 = 8;
              e |= 8;
              break;
            case ja:
              return a = td(12, c, b, e | 2), a.elementType = ja, a.lanes = f, a;
            case na:
              return a = td(13, c, b, e), a.elementType = na, a.lanes = f, a;
            case oa:
              return a = td(19, c, b, e), a.elementType = oa, a.lanes = f, a;
            case ra:
              return jg(c, e, f, b);
            default:
              if ("object" === typeof a && null !== a) switch (a.$$typeof) {
                case ka:
                  g5 = 10;
                  break a;
                case la:
                  g5 = 9;
                  break a;
                case ma:
                  g5 = 11;
                  break a;
                case pa:
                  g5 = 14;
                  break a;
                case qa:
                  g5 = 16;
                  d = null;
                  break a;
              }
              throw Error(n(130, null == a ? a : typeof a, ""));
          }
          b = td(g5, c, b, e);
          b.elementType = a;
          b.type = d;
          b.lanes = f;
          return b;
        }
        function Nd(a, b, c, d) {
          a = td(7, a, d, b);
          a.lanes = c;
          return a;
        }
        function jg(a, b, c, d) {
          a = td(22, a, d, b);
          a.elementType = ra;
          a.lanes = c;
          a.stateNode = { isHidden: false };
          return a;
        }
        function Kd(a, b, c) {
          a = td(6, a, null, b);
          a.lanes = c;
          return a;
        }
        function Md(a, b, c) {
          b = td(4, null !== a.children ? a.children : [], a.key, b);
          b.lanes = c;
          b.stateNode = { containerInfo: a.containerInfo, pendingChildren: null, implementation: a.implementation };
          return b;
        }
        function gi(a, b, c, d, e) {
          this.tag = b;
          this.containerInfo = a;
          this.finishedWork = this.pingCache = this.current = this.pendingChildren = null;
          this.timeoutHandle = Ra;
          this.callbackNode = this.pendingContext = this.context = null;
          this.callbackPriority = 0;
          this.eventTimes = Ec(0);
          this.expirationTimes = Ec(-1);
          this.entangledLanes = this.finishedLanes = this.mutableReadLanes = this.expiredLanes = this.pingedLanes = this.suspendedLanes = this.pendingLanes = 0;
          this.entanglements = Ec(0);
          this.identifierPrefix = d;
          this.onRecoverableError = e;
          Va && (this.mutableSourceEagerHydrationData = null);
        }
        function hi(a, b, c, d, e, f, g5, h, k) {
          a = new gi(a, b, c, h, k);
          1 === b ? (b = 1, true === f && (b |= 8)) : b = 0;
          f = td(3, null, null, b);
          a.current = f;
          f.stateNode = a;
          f.memoizedState = { element: d, isDehydrated: c, cache: null, transitions: null, pendingSuspenseBoundaries: null };
          ee(f);
          return a;
        }
        function ii(a) {
          if (!a) return jc;
          a = a._reactInternals;
          a: {
            if (wa(a) !== a || 1 !== a.tag) throw Error(n(170));
            var b = a;
            do {
              switch (b.tag) {
                case 3:
                  b = b.stateNode.context;
                  break a;
                case 1:
                  if (A(b.type)) {
                    b = b.stateNode.__reactInternalMemoizedMergedChildContext;
                    break a;
                  }
              }
              b = b.return;
            } while (null !== b);
            throw Error(n(171));
          }
          if (1 === a.tag) {
            var c = a.type;
            if (A(c)) return pc(a, c, b);
          }
          return b;
        }
        function ji(a) {
          var b = a._reactInternals;
          if (void 0 === b) {
            if ("function" === typeof a.render) throw Error(n(188));
            a = Object.keys(a).join(",");
            throw Error(n(268, a));
          }
          a = Aa(b);
          return null === a ? null : a.stateNode;
        }
        function ki(a, b) {
          a = a.memoizedState;
          if (null !== a && null !== a.dehydrated) {
            var c = a.retryLane;
            a.retryLane = 0 !== c && c < b ? c : b;
          }
        }
        function li(a, b) {
          ki(a, b);
          (a = a.alternate) && ki(a, b);
        }
        function mi(a) {
          a = Aa(a);
          return null === a ? null : a.stateNode;
        }
        function ni() {
          return null;
        }
        exports2.attemptContinuousHydration = function(a) {
          if (13 === a.tag) {
            var b = ce(a, 134217728);
            if (null !== b) {
              var c = O();
              af(b, a, 134217728, c);
            }
            li(a, 134217728);
          }
        };
        exports2.attemptDiscreteHydration = function(a) {
          if (13 === a.tag) {
            var b = ce(a, 1);
            if (null !== b) {
              var c = O();
              af(b, a, 1, c);
            }
            li(a, 1);
          }
        };
        exports2.attemptHydrationAtCurrentPriority = function(a) {
          if (13 === a.tag) {
            var b = tf(a), c = ce(a, b);
            if (null !== c) {
              var d = O();
              af(c, a, b, d);
            }
            li(a, b);
          }
        };
        exports2.attemptSynchronousHydration = function(a) {
          switch (a.tag) {
            case 3:
              var b = a.stateNode;
              if (b.current.memoizedState.isDehydrated) {
                var c = yc(b.pendingLanes);
                0 !== c && (Hc(b, c | 1), Kh(b, D()), 0 === (H & 6) && (Bh(), ad()));
              }
              break;
            case 13:
              Xh(function() {
                var b2 = ce(a, 1);
                if (null !== b2) {
                  var c2 = O();
                  af(b2, a, 1, c2);
                }
              }), li(a, 1);
          }
        };
        exports2.batchedUpdates = function(a, b) {
          var c = H;
          H |= 1;
          try {
            return a(b);
          } finally {
            H = c, 0 === H && (Bh(), Xc && ad());
          }
        };
        exports2.createComponentSelector = function(a) {
          return { $$typeof: hh, value: a };
        };
        exports2.createContainer = function(a, b, c, d, e, f, g5) {
          return hi(a, b, false, null, c, d, e, f, g5);
        };
        exports2.createHasPseudoClassSelector = function(a) {
          return { $$typeof: ih, value: a };
        };
        exports2.createHydrationContainer = function(a, b, c, d, e, f, g5, h, k) {
          a = hi(c, d, true, a, e, f, g5, h, k);
          a.context = ii(null);
          c = a.current;
          d = O();
          e = tf(c);
          f = ge(d, e);
          f.callback = void 0 !== b && null !== b ? b : null;
          he(c, f, e);
          a.current.lanes = e;
          Fc(a, e, d);
          Kh(a, d);
          return a;
        };
        exports2.createPortal = function(a, b, c) {
          var d = 3 < arguments.length && void 0 !== arguments[3] ? arguments[3] : null;
          return { $$typeof: fa, key: null == d ? null : "" + d, children: a, containerInfo: b, implementation: c };
        };
        exports2.createRoleSelector = function(a) {
          return { $$typeof: jh, value: a };
        };
        exports2.createTestNameSelector = function(a) {
          return { $$typeof: kh, value: a };
        };
        exports2.createTextSelector = function(a) {
          return { $$typeof: lh, value: a };
        };
        exports2.deferredUpdates = function(a) {
          var b = C, c = W.transition;
          try {
            return W.transition = null, C = 16, a();
          } finally {
            C = b, W.transition = c;
          }
        };
        exports2.discreteUpdates = function(a, b, c, d, e) {
          var f = C, g5 = W.transition;
          try {
            return W.transition = null, C = 1, a(b, c, d, e);
          } finally {
            C = f, W.transition = g5, 0 === H && Bh();
          }
        };
        exports2.findAllNodes = rh;
        exports2.findBoundingRects = function(a, b) {
          if (!bb) throw Error(n(363));
          b = rh(a, b);
          a = [];
          for (var c = 0; c < b.length; c++) a.push(db(b[c]));
          for (b = a.length - 1; 0 < b; b--) {
            c = a[b];
            for (var d = c.x, e = d + c.width, f = c.y, g5 = f + c.height, h = b - 1; 0 <= h; h--) if (b !== h) {
              var k = a[h], l = k.x, m = l + k.width, r = k.y, p = r + k.height;
              if (d >= l && f >= r && e <= m && g5 <= p) {
                a.splice(b, 1);
                break;
              } else if (!(d !== l || c.width !== k.width || p < f || r > g5)) {
                r > f && (k.height += r - f, k.y = f);
                p < g5 && (k.height = g5 - r);
                a.splice(b, 1);
                break;
              } else if (!(f !== r || c.height !== k.height || m < d || l > e)) {
                l > d && (k.width += l - d, k.x = d);
                m < e && (k.width = e - l);
                a.splice(b, 1);
                break;
              }
            }
          }
          return a;
        };
        exports2.findHostInstance = ji;
        exports2.findHostInstanceWithNoPortals = function(a) {
          a = za(a);
          a = null !== a ? Ca(a) : null;
          return null === a ? null : a.stateNode;
        };
        exports2.findHostInstanceWithWarning = function(a) {
          return ji(a);
        };
        exports2.flushControlled = function(a) {
          var b = H;
          H |= 1;
          var c = W.transition, d = C;
          try {
            W.transition = null, C = 1, a();
          } finally {
            C = d, W.transition = c, H = b, 0 === H && (Bh(), ad());
          }
        };
        exports2.flushPassiveEffects = Oh;
        exports2.flushSync = Xh;
        exports2.focusWithin = function(a, b) {
          if (!bb) throw Error(n(363));
          a = nh(a);
          b = qh(a, b);
          b = Array.from(b);
          for (a = 0; a < b.length; ) {
            var c = b[a++];
            if (!fb(c)) {
              if (5 === c.tag && hb(c.stateNode)) return true;
              for (c = c.child; null !== c; ) b.push(c), c = c.sibling;
            }
          }
          return false;
        };
        exports2.getCurrentUpdatePriority = function() {
          return C;
        };
        exports2.getFindAllNodesFailureDescription = function(a, b) {
          if (!bb) throw Error(n(363));
          var c = 0, d = [];
          a = [nh(a), 0];
          for (var e = 0; e < a.length; ) {
            var f = a[e++], g5 = a[e++], h = b[g5];
            if (5 !== f.tag || !fb(f)) {
              if (oh(f, h) && (d.push(ph(h)), g5++, g5 > c && (c = g5)), g5 < b.length) for (f = f.child; null !== f; ) a.push(f, g5), f = f.sibling;
            }
          }
          if (c < b.length) {
            for (a = []; c < b.length; c++) a.push(ph(b[c]));
            return "findAllNodes was able to match part of the selector:\n  " + (d.join(" > ") + "\n\nNo matching component was found for:\n  ") + a.join(" > ");
          }
          return null;
        };
        exports2.getPublicRootInstance = function(a) {
          a = a.current;
          if (!a.child) return null;
          switch (a.child.tag) {
            case 5:
              return Ea(a.child.stateNode);
            default:
              return a.child.stateNode;
          }
        };
        exports2.injectIntoDevTools = function(a) {
          a = { bundleType: a.bundleType, version: a.version, rendererPackageName: a.rendererPackageName, rendererConfig: a.rendererConfig, overrideHookState: null, overrideHookStateDeletePath: null, overrideHookStateRenamePath: null, overrideProps: null, overridePropsDeletePath: null, overridePropsRenamePath: null, setErrorHandler: null, setSuspenseHandler: null, scheduleUpdate: null, currentDispatcherRef: da.ReactCurrentDispatcher, findHostInstanceByFiber: mi, findFiberByHostInstance: a.findFiberByHostInstance || ni, findHostInstancesForRefresh: null, scheduleRefresh: null, scheduleRoot: null, setRefreshHandler: null, getCurrentFiber: null, reconcilerVersion: "18.3.1" };
          if ("undefined" === typeof __REACT_DEVTOOLS_GLOBAL_HOOK__) a = false;
          else {
            var b = __REACT_DEVTOOLS_GLOBAL_HOOK__;
            if (b.isDisabled || !b.supportsFiber) a = true;
            else {
              try {
                Rc = b.inject(a), Sc = b;
              } catch (c) {
              }
              a = b.checkDCE ? true : false;
            }
          }
          return a;
        };
        exports2.isAlreadyRendering = function() {
          return false;
        };
        exports2.observeVisibleRects = function(a, b, c, d) {
          if (!bb) throw Error(n(363));
          a = rh(a, b);
          var e = ib(a, c, d).disconnect;
          return { disconnect: function() {
            e();
          } };
        };
        exports2.registerMutableSourceForHydration = function(a, b) {
          var c = b._getVersion;
          c = c(b._source);
          null == a.mutableSourceEagerHydrationData ? a.mutableSourceEagerHydrationData = [b, c] : a.mutableSourceEagerHydrationData.push(b, c);
        };
        exports2.runWithPriority = function(a, b) {
          var c = C;
          try {
            return C = a, b();
          } finally {
            C = c;
          }
        };
        exports2.shouldError = function() {
          return null;
        };
        exports2.shouldSuspend = function() {
          return false;
        };
        exports2.updateContainer = function(a, b, c, d) {
          var e = b.current, f = O(), g5 = tf(e);
          c = ii(c);
          null === b.context ? b.context = c : b.pendingContext = c;
          b = ge(f, g5);
          b.payload = { element: a };
          d = void 0 === d ? null : d;
          null !== d && (b.callback = d);
          a = he(e, b, g5);
          null !== a && (af(a, e, g5, f), ie(a, e, g5));
          return g5;
        };
        return exports2;
      };
    }
  });

  // ../pulp/packages/pulp-react/node_modules/react-reconciler/index.js
  var require_react_reconciler = __commonJS({
    "../pulp/packages/pulp-react/node_modules/react-reconciler/index.js"(exports, module) {
      "use strict";
      if (true) {
        module.exports = require_react_reconciler_production_min();
      } else {
        module.exports = null;
      }
    }
  });

  // ../pulp/packages/pulp-react/node_modules/react-reconciler/cjs/react-reconciler-constants.production.min.js
  var require_react_reconciler_constants_production_min = __commonJS({
    "../pulp/packages/pulp-react/node_modules/react-reconciler/cjs/react-reconciler-constants.production.min.js"(exports) {
      "use strict";
      exports.ConcurrentRoot = 1;
      exports.ContinuousEventPriority = 4;
      exports.DefaultEventPriority = 16;
      exports.DiscreteEventPriority = 1;
      exports.IdleEventPriority = 536870912;
      exports.LegacyRoot = 0;
    }
  });

  // ../pulp/packages/pulp-react/node_modules/react-reconciler/constants.js
  var require_constants = __commonJS({
    "../pulp/packages/pulp-react/node_modules/react-reconciler/constants.js"(exports, module) {
      "use strict";
      if (true) {
        module.exports = require_react_reconciler_constants_production_min();
      } else {
        module.exports = null;
      }
    }
  });

  // native-ui/src/editor.tsx
  var import_react3 = __toESM(require_react());

  // ../pulp/packages/pulp-react/src/index.ts
  var import_react_reconciler = __toESM(require_react_reconciler(), 1);
  var import_constants2 = __toESM(require_constants(), 1);

  // ../pulp/packages/pulp-react/src/host-config.ts
  var import_constants = __toESM(require_constants(), 1);

  // ../pulp/packages/pulp-react/src/synthetic-event.ts
  var g = () => globalThis;
  function makeStyleProxy(id) {
    const setters = {
      background: (v) => callBridge("setBackground", id, String(v)),
      backgroundColor: (v) => callBridge("setBackground", id, String(v)),
      backgroundGradient: (v) => callBridge("setBackgroundGradient", id, String(v)),
      opacity: (v) => callBridge("setOpacity", id, Number(v)),
      // visibility: 'hidden' | 'visible' — matches CSS, not the inverse `hidden`.
      visibility: (v) => callBridge("setVisible", id, String(v) !== "hidden"),
      // Border shorthands route to the per-attribute bridge setters
      // that preserve unset siblings (pulp #1027).
      borderColor: (v) => callBridge("setBorderColor", id, String(v)),
      borderWidth: (v) => callBridge("setBorderWidth", id, Number(v)),
      borderRadius: (v) => callBridge("setBorderRadius", id, Number(v)),
      // Text
      color: (v) => callBridge("setTextColor", id, String(v)),
      fontSize: (v) => callBridge("setFontSize", id, Number(v)),
      // pulp #1434 Phase A2-5 — bridge forwards comma-separated CSS
      // family lists; first non-empty wins. Whole-list resolution is
      // gated on pulp #932.
      fontFamily: (v) => callBridge("setFontFamily", id, String(v)),
      // Layout — minimal subset; matches what setFlex accepts.
      width: (v) => callBridge("setFlex", id, "width", Number(v)),
      height: (v) => callBridge("setFlex", id, "height", Number(v))
    };
    const proxy = {};
    for (const key of Object.keys(setters)) {
      Object.defineProperty(proxy, key, {
        configurable: true,
        enumerable: true,
        get() {
          return void 0;
        },
        // reads not supported — by design
        set(v) {
          setters[key](v);
        }
      });
    }
    return proxy;
  }
  function callBridge(name, ...args) {
    const fn = g()[name];
    if (typeof fn === "function") fn(...args);
  }
  function makeElementWrapper(id) {
    return {
      id,
      _id: id,
      // mirrors web-compat Element naming for cross-compat consumers
      style: makeStyleProxy(id),
      // pulp #1352: setAttribute/getAttribute are common in DOM-shaped
      // code; we accept the writes but don't persist them — there is
      // no HTML attribute layer in Pulp. JSX prop-applier is the
      // canonical write path.
      setAttribute(_name, _value) {
      },
      getAttribute(_name) {
        return null;
      }
    };
  }
  function isPlainObject(v) {
    return typeof v === "object" && v !== null && !Array.isArray(v);
  }
  function makeSyntheticEvent(id, eventName, rawArgs) {
    const target = makeElementWrapper(id);
    const evt = {
      type: eventName,
      currentTarget: target,
      target,
      nativeEvent: { rawArgs },
      defaultPrevented: false,
      preventDefault() {
        evt.defaultPrevented = true;
      },
      stopPropagation() {
      },
      clientX: 0,
      clientY: 0,
      offsetX: 0,
      offsetY: 0,
      button: 0,
      pointerId: 0,
      pointerType: "mouse",
      isPrimary: true,
      pressure: 0.5,
      ctrlKey: false,
      shiftKey: false,
      altKey: false,
      metaKey: false,
      scale: 1,
      rotation: 0,
      deltaX: 0,
      deltaY: 0,
      deltaZ: 0,
      deltaMode: 0,
      key: "",
      keyCode: 0
    };
    const a0 = rawArgs[0];
    if (isPlainObject(a0)) {
      const d = a0;
      if (typeof d.clientX === "number") evt.clientX = d.clientX;
      if (typeof d.clientY === "number") evt.clientY = d.clientY;
      if (typeof d.offsetX === "number") evt.offsetX = d.offsetX;
      if (typeof d.offsetY === "number") evt.offsetY = d.offsetY;
      if (typeof d.deltaX === "number") evt.deltaX = d.deltaX;
      if (typeof d.deltaY === "number") evt.deltaY = d.deltaY;
      if (typeof d.deltaZ === "number") evt.deltaZ = d.deltaZ;
      if (typeof d.deltaMode === "number") evt.deltaMode = d.deltaMode;
      if (typeof d.button === "number") evt.button = d.button;
      if (typeof d.pointerId === "number") evt.pointerId = d.pointerId;
      if (typeof d.pointerType === "string") evt.pointerType = d.pointerType;
      if (typeof d.isPrimary === "boolean") evt.isPrimary = d.isPrimary;
      if (typeof d.pressure === "number") evt.pressure = d.pressure;
      if (typeof d.ctrlKey === "boolean") evt.ctrlKey = d.ctrlKey;
      if (typeof d.shiftKey === "boolean") evt.shiftKey = d.shiftKey;
      if (typeof d.altKey === "boolean") evt.altKey = d.altKey;
      if (typeof d.metaKey === "boolean") evt.metaKey = d.metaKey;
      if (typeof d.scale === "number") evt.scale = d.scale;
      if (typeof d.rotation === "number") evt.rotation = d.rotation;
      if (typeof d.key === "string") evt.key = d.key;
      if (typeof d.keyCode === "number") evt.keyCode = d.keyCode;
    }
    if (eventName === "change" || eventName === "return" || eventName === "input") {
      const val = typeof a0 === "string" ? a0 : typeof a0 === "number" ? a0 : a0;
      target.value = val;
      evt.value = val;
    }
    if (eventName === "toggle" && typeof a0 === "number") {
      target.checked = a0 !== 0;
      evt.checked = a0 !== 0;
    }
    return evt;
  }

  // ../pulp/packages/pulp-react/src/prop-applier-internal.ts
  var g2 = globalThis;
  var _pa_count = 0;
  function call(name, ...args) {
    const fn = g2[name];
    if (typeof fn !== "function") return;
    _pa_count++;
    if (_pa_count <= 100) {
      const lg = g2.__spectrLog;
      if (typeof lg === "function") {
        const a0 = args[0] !== void 0 ? String(args[0]).slice(0, 25) : "";
        const a1 = args[1] !== void 0 ? String(args[1]).slice(0, 25) : "";
        const a2 = args[2] !== void 0 ? String(args[2]).slice(0, 25) : "";
        lg("[pa#" + _pa_count + "] " + name + "(" + a0 + "," + a1 + (args.length > 2 ? "," + a2 : "") + ")");
      }
    }
    fn(...args);
  }
  function _resolveVar(value) {
    if (typeof value !== "string") return value;
    const s = value.trim();
    if (!s.startsWith("var(") || !s.endsWith(")")) return value;
    const inner = s.slice(4, s.length - 1);
    let commaPos = -1;
    let depth = 0;
    for (let i = 0; i < inner.length; i++) {
      const c = inner.charAt(i);
      if (c === "(") depth++;
      else if (c === ")") depth--;
      else if (c === "," && depth === 0) {
        commaPos = i;
        break;
      }
    }
    let name;
    let fallback;
    if (commaPos >= 0) {
      name = inner.slice(0, commaPos).trim();
      fallback = inner.slice(commaPos + 1).trim();
    } else {
      name = inner.trim();
    }
    if (name.startsWith("--")) name = name.slice(2);
    if (!name) return value;
    const reg = globalThis.__pulpCssVars;
    if (reg && typeof reg[name] === "string" && reg[name]) {
      return reg[name];
    }
    const getStr = globalThis.getStringToken;
    if (typeof getStr === "function") {
      const sv = getStr(name);
      if (typeof sv === "string" && sv) return sv;
    }
    const getNum = globalThis.getMotionToken;
    if (typeof getNum === "function") {
      const nv = getNum(name);
      if (typeof nv === "number" && nv !== 0 && Number.isFinite(nv)) {
        return String(nv);
      }
    }
    if (fallback !== void 0 && fallback.length > 0) {
      return _resolveVar(fallback);
    }
    return value;
  }

  // ../pulp/packages/pulp-react/src/prop-applier-layout.ts
  function _coerceLen(tok) {
    if (typeof tok === "number") return tok;
    const s = String(tok).trim();
    if (s.endsWith("%")) return s;
    const n = parseFloat(s);
    return Number.isFinite(n) ? n : 0;
  }
  function _coerceMarginLen(tok) {
    if (typeof tok === "number") return tok;
    const s = String(tok).trim();
    if (s === "auto") return "auto";
    if (s.endsWith("%")) return s;
    const n = parseFloat(s);
    return Number.isFinite(n) ? n : 0;
  }
  function applyLayoutProp(id, key, value, props) {
    switch (key) {
      // Flex / layout — all forwarded through setFlex
      // pulp #1434 (rn NOT-IMPL bundle 1) — `direction` is overloaded:
      //   • RN (and CSS spec) sense — writing direction: 'ltr' / 'rtl' /
      //     'inherit' (RN spec also accepts 'auto' on iOS-classic). The
      //     New Architecture surfaces this cross-platform.
      //   • pulp historical sense — flexDirection alias: 'row' / 'col' /
      //     'row-reverse' / 'column' / 'column-reverse'. Existing test
      //     at prop-applier-direction.test.ts:60 pins this behavior.
      // Disambiguate on value: writing-direction keywords route to
      // setDirection; everything else falls through to setFlex(direction)
      // for backward compat. `writingDirection` is preferred for new code
      // (case below) but RN code commonly emits `direction: 'rtl'`.
      case "direction": {
        const sval = String(value).trim().toLowerCase();
        if (sval === "ltr" || sval === "rtl" || sval === "inherit" || sval === "auto") {
          call("setDirection", id, sval);
          return true;
        }
        call("setFlex", id, "direction", value);
        return true;
      }
      // pulp #108 — RN/React-style `flexDirection` (camelCase) is the
      // canonical key in JSX. Without this case the prop fell through
      // as unknown, leaving Yoga's column default in place and
      // collapsing CSS-imported flex rows into vertical stacks. Maps
      // to the same setFlex(direction, …) dispatch as the `direction`
      // case for the flex-direction subset of values. Normalizes
      // `col` / `col-reverse` aliases to `column` / `column-reverse`
      // for the bridge's expected vocabulary.
      case "flexDirection": {
        const sval = String(value).trim().toLowerCase();
        const normalized = sval === "col" ? "column" : sval === "col-reverse" ? "column-reverse" : sval;
        call("setFlex", id, "direction", normalized);
        return true;
      }
      case "gap":
        call("setFlex", id, "gap", value);
        return true;
      case "rowGap":
        call("setFlex", id, "row_gap", value);
        return true;
      case "columnGap":
        call("setFlex", id, "column_gap", value);
        return true;
      // Wave 2 rn — `padding` shorthand accepts string forms (`'5%'`,
      // `'10px 20px'`, etc.). The bridge `padding` shorthand key only
      // takes a numeric value, so we fan out string values to the
      // four per-edge keys (which DO accept `number | string` via
      // setFlex(padding_top/...) and route through Yoga's
      // YGNodeStyleSetPaddingPercent for `'5%'`). Numeric values
      // continue to flow through the original shorthand path so we
      // preserve the single-bridge-call shape for the common case.
      case "padding": {
        if (typeof value === "string") {
          const tokens = value.trim().split(/\s+/);
          const t = _coerceLen(tokens[0] ?? 0);
          const r = _coerceLen(tokens[1] ?? tokens[0] ?? 0);
          const b = _coerceLen(tokens[2] ?? tokens[0] ?? 0);
          const l = _coerceLen(tokens[3] ?? tokens[1] ?? tokens[0] ?? 0);
          call("setFlex", id, "padding_top", t);
          call("setFlex", id, "padding_right", r);
          call("setFlex", id, "padding_bottom", b);
          call("setFlex", id, "padding_left", l);
          return true;
        }
        call("setFlex", id, "padding", value);
        return true;
      }
      // pulp #1434 (cross-surface mega-batch) — per-edge padding accepts
      // either a number (px) or a percent string ('5%' → percent of
      // parent main-axis size). Yoga padding does NOT support 'auto'.
      case "paddingTop":
        call("setFlex", id, "padding_top", value);
        return true;
      case "paddingRight":
        call("setFlex", id, "padding_right", value);
        return true;
      case "paddingBottom":
        call("setFlex", id, "padding_bottom", value);
        return true;
      case "paddingLeft":
        call("setFlex", id, "padding_left", value);
        return true;
      // Wave 2 rn — `margin` shorthand accepts string forms (`'5%'`,
      // `'auto'`, `'10px auto'`, etc.). The bridge `margin` shorthand
      // key only takes a numeric value, so we fan out string values
      // to the four per-edge keys (which DO accept `number | string`
      // including the `'auto'` keyword via Yoga's
      // YGNodeStyleSetMarginAuto for centering). Numeric values
      // continue through the original shorthand path so the common
      // single-call case is preserved.
      case "margin": {
        if (typeof value === "string") {
          const tokens = value.trim().split(/\s+/);
          const t = _coerceMarginLen(tokens[0] ?? 0);
          const r = _coerceMarginLen(tokens[1] ?? tokens[0] ?? 0);
          const b = _coerceMarginLen(tokens[2] ?? tokens[0] ?? 0);
          const l = _coerceMarginLen(tokens[3] ?? tokens[1] ?? tokens[0] ?? 0);
          call("setFlex", id, "margin_top", t);
          call("setFlex", id, "margin_right", r);
          call("setFlex", id, "margin_bottom", b);
          call("setFlex", id, "margin_left", l);
          return true;
        }
        call("setFlex", id, "margin", value);
        return true;
      }
      // pulp #1434 (cross-surface mega-batch) — per-edge margin accepts
      // a number (px), percent string ('5%'), or the keyword 'auto'
      // (Yoga's YGNodeStyleSetMarginAuto — used for centering with
      // marginLeft:'auto' + marginRight:'auto').
      case "marginTop":
        call("setFlex", id, "margin_top", value);
        return true;
      case "marginRight":
        call("setFlex", id, "margin_right", value);
        return true;
      case "marginBottom":
        call("setFlex", id, "margin_bottom", value);
        return true;
      case "marginLeft":
        call("setFlex", id, "margin_left", value);
        return true;
      // pulp #1434 batch 4 — React Native shorthand aliases. RN code
      // commonly writes `style={{ marginHorizontal: 8 }}` and expects
      // it to fan out to marginLeft + marginRight on the underlying
      // layout. Same pattern for marginVertical / paddingHorizontal /
      // paddingVertical. We dispatch to the existing per-edge bridge
      // setters so the value reaches the same FlexStyle slot whether
      // it arrived through this alias or through the explicit edge
      // prop. No FlexStyle field change required.
      // pulp #1434 cross-surface mega-batch — RN aliases now forward
      // percent strings (and 'auto' for margin) through the per-edge
      // fan-out. The per-edge keys `margin_*` / `padding_*` accept
      // number | string at the bridge boundary.
      case "marginHorizontal":
        call("setFlex", id, "margin_left", value);
        call("setFlex", id, "margin_right", value);
        return true;
      case "marginVertical":
        call("setFlex", id, "margin_top", value);
        call("setFlex", id, "margin_bottom", value);
        return true;
      case "paddingHorizontal":
        call("setFlex", id, "padding_left", value);
        call("setFlex", id, "padding_right", value);
        return true;
      case "paddingVertical":
        call("setFlex", id, "padding_top", value);
        call("setFlex", id, "padding_bottom", value);
        return true;
      case "flexGrow":
        call("setFlex", id, "flex_grow", value);
        return true;
      case "flexShrink":
        call("setFlex", id, "flex_shrink", value);
        return true;
      // pulp #1434 (#1518) — RN-style `flex: <number>` shorthand.
      // RN spec: `flex: positive` → `{flexGrow: n, flexShrink: 1, flexBasis: 0}`;
      // `flex: 0` → no growth / no shrink at intrinsic basis;
      // `flex: -1` (or any negative) → no growth, can shrink at auto basis.
      // CSS spec is more nuanced (bare number is `flex: <n> 1 0`), but RN's
      // narrow contract is what consumers passing JSX `flex={1}` expect;
      // our adapter is RN-flavored so we honor RN semantics.
      case "flex": {
        const n = value;
        if (typeof n !== "number" || !Number.isFinite(n)) return true;
        if (n > 0) {
          call("setFlex", id, "flex_grow", n);
          call("setFlex", id, "flex_shrink", 1);
          call("setFlex", id, "flex_basis", 0);
        } else if (n === 0) {
          call("setFlex", id, "flex_grow", 0);
          call("setFlex", id, "flex_shrink", 0);
          call("setFlex", id, "flex_basis", "auto");
        } else {
          call("setFlex", id, "flex_grow", 0);
          call("setFlex", id, "flex_shrink", 1);
          call("setFlex", id, "flex_basis", "auto");
        }
        return true;
      }
      // pulp #1434 (rn batch C) — dimension keys forward
      // `number | string` so the bridge sees `'50%'` / `'auto'`
      // verbatim. Numeric values still flow through unchanged.
      // The bridge's setFlex case for each key inspects the third
      // arg as a string and detects '%' / 'auto' suffix; otherwise
      // it falls back to the numeric path.
      case "flexBasis":
        call("setFlex", id, "flex_basis", value);
        return true;
      // pulp #1434 Triage #14 — flexWrap accepts boolean (legacy
      // true/false) or the CSS keyword strings (`"wrap"` /
      // `"wrap-reverse"` / `"nowrap"`). Forward strings verbatim
      // so the bridge can route wrap-reverse through Yoga's
      // YGWrapWrapReverse.
      case "flexWrap": {
        if (typeof value === "string") {
          call("setFlex", id, "flex_wrap", value);
          return true;
        }
        call("setFlex", id, "flex_wrap", value ? 1 : 0);
        return true;
      }
      case "order":
        call("setFlex", id, "order", value);
        return true;
      case "width":
        call("setFlex", id, "width", value);
        return true;
      case "height":
        call("setFlex", id, "height", value);
        return true;
      case "minWidth":
        call("setFlex", id, "min_width", value);
        return true;
      case "minHeight":
        call("setFlex", id, "min_height", value);
        return true;
      case "maxWidth":
        call("setFlex", id, "max_width", value);
        return true;
      case "maxHeight":
        call("setFlex", id, "max_height", value);
        return true;
      case "alignItems":
        call("setFlex", id, "align_items", value);
        return true;
      case "alignSelf":
        call("setFlex", id, "align_self", value);
        return true;
      // pulp #1434 (sub-agent #12 follow-up) — multi-line flex
      // cross-axis distribution. Yoga supports it natively via
      // YGNodeStyleSetAlignContent; the bridge accepts both bare
      // (`start`/`end`) and prefixed (`flex-start`/`flex-end`)
      // spellings plus the space-* values.
      case "alignContent":
        call("setFlex", id, "align_content", value);
        return true;
      case "justifyContent":
        call("setFlex", id, "justify_content", value);
        return true;
      // pulp #1434 — aspectRatio routes through setFlex like the other
      // flex props. Accepts a finite positive number (RN-style); strings
      // ("16/9", "auto") are NOT accepted at the JSX surface — those
      // belong to the CSS shim path (web-compat-style-decl.js). A value
      // of 0 / NaN / undefined clears the slot on the bridge side.
      case "aspectRatio":
        call("setFlex", id, "aspect_ratio", value);
        return true;
      // pulp #1434 (rn batch — Triage #12) — `display: 'flex' | 'none'`.
      // RN exports + Figma / v0 / Claude Design HTML routinely emit
      // `style={{ display: 'flex' }}` (the implicit default in pulp,
      // but the prop-applier shouldn't drop it as unknown) or
      // `style={{ display: 'none' }}` to hide a subtree. The yoga
      // surface wired this for the CSS shim in #1422; this branch
      // makes the same path reachable from RN-flavored JSX without a
      // round-trip through the el.style proxy.
      //
      // 'none'  → setVisible(id, false). View::visible() is the
      //           canonical "skip render + don't lay out" signal.
      // 'flex'  → setVisible(id, true). Yoga's flex layout is pulp's
      //           default; explicit 'flex' just confirms it.
      // Anything else (block / inline-block / inline-flex / grid)
      // is silently ignored at this layer — the CSS shim handles
      // those for the el.style path; for RN consumers, the typical
      // emission is just 'flex' / 'none'.
      case "display": {
        const sval = String(value);
        if (sval === "none") {
          call("setVisible", id, false);
          return true;
        }
        if (sval === "flex") {
          call("setVisible", id, true);
          if (props) {
            const hasDirectionFlexValue = (() => {
              if (!Object.prototype.hasOwnProperty.call(props, "direction")) return false;
              const dv = props["direction"];
              if (typeof dv !== "string") return false;
              const norm = dv.trim().toLowerCase();
              return norm === "row" || norm === "column" || norm === "row-reverse" || norm === "column-reverse" || norm === "col" || norm === "col-reverse";
            })();
            const hasFlexDirection = Object.prototype.hasOwnProperty.call(props, "flexDirection") || Object.prototype.hasOwnProperty.call(props, "flex-direction") || hasDirectionFlexValue;
            const ff = props.flexFlow;
            const flexFlowHasDirection = typeof ff === "string" && /\b(row|column)\b/.test(ff);
            if (!hasFlexDirection && !flexFlowHasDirection) {
              call("setFlex", id, "direction", "row");
            }
          }
          return true;
        }
        return true;
      }
      // pulp #1387 gap #1 — overflow was reachable via the DOM-lite
      // path (web-compat-style-decl.js routes 'overflow' to setOverflow)
      // but missing from the @pulp/react prop-applier, so JSX consumers
      // setting `style={{ overflow: 'hidden' }}` silently dropped it.
      // Spectr's dropdowns hit this — `width: 230 + overflow: hidden`
      // on the dropdown row was being discarded, so the row grew to
      // intrinsic content width and overflowed the container.
      // Accepts the CSS keyword strings ('hidden' / 'visible' /
      // 'scroll' / 'auto'); bridge maps to View::Overflow enum.
      case "overflow":
        call("setOverflow", id, value);
        return true;
      // CSS per-axis overflow. Pulp's View::Overflow is a single-axis
      // paint-clip flag (same surface as `overflow`); both axes alias
      // to the same setter. Already-supported in web-compat-style-decl.js
      // via the same routing — wire the React-applier path so JSX
      // `style={{ overflowY: 'scroll' }}` (common in scroll containers)
      // doesn't silently drop.
      case "overflowX":
      case "overflowY":
        call("setOverflow", id, value);
        return true;
      // pulp #1516 — CSS box-sizing. Yoga 3.x honors the spec via
      // YGNodeStyleSetBoxSizing; consumers passing JSX
      // `boxSizing: 'border-box'` get the standard
      // "padding+border are inside declared dimensions" behavior.
      // Web designs almost universally reset to `border-box`.
      case "boxSizing":
        call("setBoxSizing", id, value);
        return true;
      // pulp #1434 rn logical-edge bundle (sub-agent #27 finding) —
      // RN's CSS-spec-equivalent logical-flow props. LTR-only fast
      // path: Start → Left, End → Right, inset shorthand expands to
      // top/right/bottom/left, insetBlock → top+bottom,
      // insetInline → left+right (LTR). True RTL bidi requires a
      // future direction system — tracked as a separate big project.
      // The 11 entries here close the missing-on-rn gap with the
      // honest LTR-only caveat documented in the catalog.
      case "marginStart": {
        call("setFlex", id, "margin_left", value);
        return true;
      }
      case "marginEnd": {
        call("setFlex", id, "margin_right", value);
        return true;
      }
      case "paddingStart": {
        call("setFlex", id, "padding_left", value);
        return true;
      }
      case "paddingEnd": {
        call("setFlex", id, "padding_right", value);
        return true;
      }
      case "start": {
        call("setLeft", id, value);
        return true;
      }
      case "end": {
        call("setRight", id, value);
        return true;
      }
      // CSS `inset` shorthand: 1 / 2 / 3 / 4 values fan out to
      // top / right / bottom / left (CSS spec — same expansion as
      // `margin` / `padding` shorthands). Numeric or percent strings.
      case "inset": {
        const v = value;
        if (typeof v === "number") {
          call("setTop", id, v);
          call("setRight", id, v);
          call("setBottom", id, v);
          call("setLeft", id, v);
          return true;
        }
        const tokens = String(v).trim().split(/\s+/);
        const t = tokens[0] ?? 0;
        const r = tokens[1] ?? t;
        const b = tokens[2] ?? t;
        const l = tokens[3] ?? r;
        const coerce = (tok) => {
          if (typeof tok === "number") return tok;
          if (tok.endsWith("%")) return tok;
          const n = parseFloat(tok);
          return Number.isFinite(n) ? n : tok;
        };
        call("setTop", id, coerce(t));
        call("setRight", id, coerce(r));
        call("setBottom", id, coerce(b));
        call("setLeft", id, coerce(l));
        return true;
      }
      case "insetBlock": {
        const v = value;
        call("setTop", id, v);
        call("setBottom", id, v);
        return true;
      }
      case "insetInline": {
        const v = value;
        call("setLeft", id, v);
        call("setRight", id, v);
        return true;
      }
      // pulp #1434 Phase A2-2 — CSS Grid surface. Forwards each
      // property verbatim to setGrid; the C++ bridge handles
      // template-track parsing, named-area parsing, and the
      // grid-area shorthand (named token vs `row / col / row / col`
      // numeric form).
      case "gridTemplateColumns":
        call("setGrid", id, "template_columns", value);
        return true;
      case "gridTemplateRows":
        call("setGrid", id, "template_rows", value);
        return true;
      case "gridTemplateAreas":
        call("setGrid", id, "template_areas", value);
        return true;
      case "gridAutoColumns":
        call("setGrid", id, "auto_columns", value);
        return true;
      case "gridAutoRows":
        call("setGrid", id, "auto_rows", value);
        return true;
      case "gridAutoFlow":
        call("setGrid", id, "auto_flow", value);
        return true;
      case "gridArea":
        call("setGrid", id, "grid_area", value);
        return true;
      case "gridColumn": {
        const parts = String(value).split("/").map((s) => parseInt(s.trim(), 10));
        if (parts[0]) call("setGrid", id, "column_start", parts[0]);
        if (parts[1]) call("setGrid", id, "column_end", parts[1]);
        return true;
      }
      case "gridRow": {
        const parts = String(value).split("/").map((s) => parseInt(s.trim(), 10));
        if (parts[0]) call("setGrid", id, "row_start", parts[0]);
        if (parts[1]) call("setGrid", id, "row_end", parts[1]);
        return true;
      }
      case "gridColumnStart":
        call("setGrid", id, "column_start", value);
        return true;
      case "gridColumnEnd":
        call("setGrid", id, "column_end", value);
        return true;
      case "gridRowStart":
        call("setGrid", id, "row_start", value);
        return true;
      case "gridRowEnd":
        call("setGrid", id, "row_end", value);
        return true;
      case "gridGap":
        call("setGrid", id, "gap", value);
        return true;
      case "gridColumnGap":
        call("setGrid", id, "column_gap", value);
        return true;
      case "gridRowGap":
        call("setGrid", id, "row_gap", value);
        return true;
      // CSS-style positioning (pulp #779 follow-up; matches setPosition
      // + setTop/setLeft/setRight/setBottom on the bridge).
      // pulp #1434 batch 6 — top/right/bottom/left accept either a number
      // ('50' → px) or a percent string ('50%' → percent of parent).
      // Mirrors PR #1426 (width/height percent) for the four View
      // positional fields. Figma absolute-positioned overlays, v0.dev
      // hero anchors, and Claude Design sticky elements all emit
      // `top:'50%'` etc. routinely; without percent forwarding the
      // layout collapses to numeric 0 silently. The bridge inspects
      // arg index 1 as a string, detects the '%' suffix, and routes to
      // Yoga's YGNodeStyleSetPositionPercent path via View::top_unit_.
      case "position":
        call("setPosition", id, value);
        return true;
      case "top":
        call("setTop", id, value);
        return true;
      case "left":
        call("setLeft", id, value);
        return true;
      case "right":
        call("setRight", id, value);
        return true;
      case "bottom":
        call("setBottom", id, value);
        return true;
      case "zIndex":
        call("setZIndex", id, value);
        return true;
      default:
        return false;
    }
  }

  // ../pulp/packages/pulp-react/src/prop-applier-paint.ts
  function _parseBoxShadow(s) {
    let work = s.trim();
    let inset = false;
    if (/^inset\s+/i.test(work)) {
      inset = true;
      work = work.replace(/^inset\s+/i, "");
    } else if (/\s+inset\s*$/i.test(work)) {
      inset = true;
      work = work.replace(/\s+inset\s*$/i, "");
    }
    const m = work.match(/(-?[\d.]+)px\s+(-?[\d.]+)px\s+([\d.]+)px(?:\s+(-?[\d.]+)px)?\s+(.*)/);
    if (!m) return null;
    return {
      offsetX: parseFloat(m[1]),
      offsetY: parseFloat(m[2]),
      blur: parseFloat(m[3]),
      spread: parseFloat(m[4] ?? "0"),
      color: m[5].trim(),
      inset
    };
  }
  function _splitMultiShadow(s) {
    const out = [];
    let depth = 0;
    let start = 0;
    for (let i = 0; i < s.length; i++) {
      const c = s[i];
      if (c === "(") depth++;
      else if (c === ")") {
        if (depth > 0) depth--;
      } else if (c === "," && depth === 0) {
        out.push(s.slice(start, i).trim());
        start = i + 1;
      }
    }
    out.push(s.slice(start).trim());
    return out.filter((x) => x.length > 0);
  }
  function _coerceRadius(value) {
    if (typeof value === "number") return value;
    if (value != null && typeof value === "object") {
      const o = value;
      const x = typeof o.x === "number" ? o.x : 0;
      const y = typeof o.y === "number" ? o.y : 0;
      return (x + y) / 2;
    }
    if (typeof value === "string") {
      const trimmed = value.trim();
      if (trimmed.endsWith("%")) {
        const n2 = parseFloat(trimmed);
        return Number.isFinite(n2) ? `${n2}%` : 0;
      }
      const n = parseFloat(trimmed);
      return Number.isFinite(n) ? n : 0;
    }
    return 0;
  }
  function applyPaintProp(id, key, value) {
    switch (key) {
      // Visual style
      // CSS `background` shorthand can carry color, gradient, or url. The
      // C++ `setBackground` bridge fn (widget_bridge.cpp:3350) only parses
      // a color token via `set_background_color(parseHexColor(...))`, so
      // gradient strings collapse to a bogus color (often white). Caught
      // by Spectr's 2026-05-11 live render — chip strip's
      // `background: linear-gradient(to bottom, ...)` painted white, making
      // the white SPECTR / ZOOMABLE FILTER BANK / axis labels invisible.
      // Mirrors the same gradient-detection fix applied to backgroundImage
      // below (Codex P1 on #1831).
      case "background": {
        const sval = String(value);
        if (/(linear|radial|conic)-gradient\(/i.test(sval)) {
          call("setBackgroundGradient", id, sval);
          return true;
        }
        if (/^\s*url\s*\(/i.test(sval)) return true;
        call("setBackground", id, _resolveVar(sval));
        return true;
      }
      case "backgroundGradient":
        call("setBackgroundGradient", id, value);
        return true;
      // CSS `background-image` longhand. The C++ `setBackground` bridge fn
      // only parses a color token (widget_bridge.cpp:3350 →
      // `set_background_color(parseHexColor(...))`), so we must detect
      // gradient strings here and route to the gradient bridge instead;
      // otherwise inputs like `linear-gradient(...)` collapse to a bogus
      // color parse. `url(...)` has no image bridge today — drop quietly
      // rather than corrupting the color slot. (Codex P1 on #1831.)
      case "backgroundImage": {
        const sval = String(value);
        if (/(linear|radial|conic)-gradient\(/i.test(sval)) {
          call("setBackgroundGradient", id, sval);
          return true;
        }
        if (/^\s*url\s*\(/i.test(sval)) {
          return true;
        }
        call("setBackground", id, _resolveVar(sval));
        return true;
      }
      // pulp #1517 — background sub-properties. The bridge stores the
      // keyword on the View slot. Paint impact today is partial:
      //   • `backgroundAttachment` — `scroll` is conformant; `fixed` /
      //     `local` are noop (pulp doesn't model scroll contexts).
      //   • `backgroundClip` — `text` is the interesting variant
      //     (paint-time SkBlendMode::kSrcIn against text glyphs);
      //     deferred to a future PR. Other values noop on solid bg.
      //   • `backgroundOrigin` — relevant only for repeating gradients;
      //     noop today.
      case "backgroundAttachment":
        call("setBackgroundAttachment", id, value);
        return true;
      case "backgroundClip":
        call("setBackgroundClip", id, value);
        return true;
      case "backgroundOrigin":
        call("setBackgroundOrigin", id, value);
        return true;
      case "border": {
        const b = value;
        call("setBorder", id, b.color, b.width ?? 1, b.radius ?? 0);
        return true;
      }
      // pulp #1027 (audit PR #1166 finding #4) — RN-style flat border props.
      // These MUST route through the per-attribute bridge setters so a
      // commitUpdate that touches only one of them preserves the others.
      // Lowering them onto the unified `setBorder(id, color, width, radius)`
      // would clobber the unset slots back to 0/empty.
      case "borderColor":
        call("setBorderColor", id, _resolveVar(value));
        return true;
      case "borderWidth":
        call("setBorderWidth", id, value);
        return true;
      // Wave 2 rn — `borderRadius` accepts the RN Fabric elliptical
      // form `{ x, y }`. The Skia paint backend currently takes a
      // single uniform radius per corner (no rrect ellipse axes), so
      // we degrade the elliptical input by averaging x and y — the
      // closest visual fidelity for the common Fabric usage where x
      // and y differ only modestly. True elliptical rendering needs
      // a paint-side rrect (Skia SkRRect::setRectXY) and remains a
      // deferred gap. Numeric values flow through unchanged.
      case "borderRadius":
        call("setBorderRadius", id, _coerceRadius(value));
        return true;
      // pulp #1434 Triage #10 — borderStyle keyword passes verbatim
      // to setBorderStyle. Bridge maps to View::BorderStyle. Skia
      // installs the dash effect for `dashed` / `dotted`; other
      // named styles currently degrade to solid.
      case "borderStyle":
        call("setBorderStyle", id, value);
        return true;
      // pulp #1514 — list-style cluster. Pulp doesn't model
      // <li>/<ul>/<ol> semantics, so the bridge stores the value
      // verbatim on the View and a future paint pass renders the
      // marker. Today the catalog is `partial` (stored, not
      // painted). The shorthand `listStyle` parses on the JS side
      // into the 3 longhands; consumers MAY emit any combo of
      // type / position / image keywords (CSS spec: any order).
      case "listStyle": {
        const sval = String(value).trim();
        const tokens = sval.split(/\s+/);
        const typeSet = {
          none: true,
          disc: true,
          circle: true,
          square: true,
          decimal: true
        };
        const posSet = { inside: true, outside: true };
        let sawType = false, sawImage = false;
        for (const tok of tokens) {
          if (tok.indexOf("url(") === 0) {
            call("setListStyleImage", id, tok);
            sawImage = true;
          } else if (posSet[tok]) {
            call("setListStylePosition", id, tok);
          } else if (typeSet[tok]) {
            if (tok === "none" && sawType && !sawImage) {
              call("setListStyleImage", id, "none");
              sawImage = true;
            } else {
              call("setListStyleType", id, tok);
              sawType = true;
            }
          }
        }
        return true;
      }
      case "listStyleType":
        call("setListStyleType", id, value);
        return true;
      case "listStyleImage":
        call("setListStyleImage", id, value);
        return true;
      case "listStylePosition":
        call("setListStylePosition", id, value);
        return true;
      case "borderTop": {
        const b = value;
        call("setBorderSide", id, "top", b.width, b.color);
        return true;
      }
      case "borderRight": {
        const b = value;
        call("setBorderSide", id, "right", b.width, b.color);
        return true;
      }
      case "borderBottom": {
        const b = value;
        call("setBorderSide", id, "bottom", b.width, b.color);
        return true;
      }
      case "borderLeft": {
        const b = value;
        call("setBorderSide", id, "left", b.width, b.color);
        return true;
      }
      // RN per-side flat props — route to the per-side bridge setters
      // that already preserve the unrelated attribute (see widget_bridge
      // applyBorderSide helper introduced in pulp #1026).
      case "borderTopColor":
        call("setBorderTopColor", id, _resolveVar(value));
        return true;
      case "borderRightColor":
        call("setBorderRightColor", id, _resolveVar(value));
        return true;
      case "borderBottomColor":
        call("setBorderBottomColor", id, _resolveVar(value));
        return true;
      case "borderLeftColor":
        call("setBorderLeftColor", id, _resolveVar(value));
        return true;
      case "borderTopWidth":
        call("setBorderTopWidth", id, value);
        return true;
      case "borderRightWidth":
        call("setBorderRightWidth", id, value);
        return true;
      case "borderBottomWidth":
        call("setBorderBottomWidth", id, value);
        return true;
      case "borderLeftWidth":
        call("setBorderLeftWidth", id, value);
        return true;
      // Wave 2 rn — per-corner radii accept the RN Fabric elliptical
      // `{ x, y }` form too (degraded to averaged uniform radius;
      // see `borderRadius` above for the rrect rationale).
      case "borderTopLeftRadius":
        call("setBorderTopLeftRadius", id, _coerceRadius(value));
        return true;
      case "borderTopRightRadius":
        call("setBorderTopRightRadius", id, _coerceRadius(value));
        return true;
      case "borderBottomLeftRadius":
        call("setBorderBottomLeftRadius", id, _coerceRadius(value));
        return true;
      case "borderBottomRightRadius":
        call("setBorderBottomRightRadius", id, _coerceRadius(value));
        return true;
      // pulp #1519 — RN outline cluster. Paint-time ring drawn OUTSIDE
      // the border-box (no Yoga layout impact). Each prop routes to its
      // own per-attribute bridge fn so a JSX prop diff that touches one
      // outline-* preserves the others. Style keyword set mirrors
      // borderStyle (CSS spec is identical).
      case "outlineColor":
        call("setOutlineColor", id, _resolveVar(value));
        return true;
      case "outlineOffset":
        call("setOutlineOffset", id, value);
        return true;
      case "outlineStyle":
        call("setOutlineStyle", id, value);
        return true;
      case "outlineWidth":
        call("setOutlineWidth", id, value);
        return true;
      // CSS shorthand `outline: <width> <style> <color>` — mirror the
      // parser already wired in web-compat-style-decl.js so JSX
      // `style={{ outline: '1px solid red' }}` fans out to the three
      // per-attribute setters identically to el.style.outline = '...'.
      case "outline": {
        const m = String(value).match(/([\d.]+)px\s+(\w+)\s+(.+)/);
        if (!m) return true;
        call("setOutlineWidth", id, parseFloat(m[1]));
        call("setOutlineStyle", id, m[2]);
        call("setOutlineColor", id, m[3].trim());
        return true;
      }
      case "opacity":
        call("setOpacity", id, value);
        return true;
      case "visible":
        call("setVisible", id, value);
        return true;
      // pulp #1434 (Triage #15) — `boxShadow` accepts:
      //  • `null` / `undefined` / `'none'` → clearBoxShadow
      //  • String form (`'2px 4px 8px rgba(0,0,0,0.3)'` with optional
      //    `inset`) — parsed inline below.
      //  • Object form `{ offsetX, offsetY, blur?, spread?, color,
      //    inset? }` — dispatched directly.
      //
      // Wave 2 rn — multi-shadow comma-separated string lists are now
      // wired: we split on commas (respecting paren depth so a color
      // literal like `rgba(0,0,0,0.3)` doesn't get cut), then dispatch
      // one setBoxShadow per parsed shadow. CSS spec layers the first
      // shadow on TOP (closest to viewer); the bridge applies them in
      // dispatch order so paint order matches the input string.
      case "boxShadow": {
        if (value == null || value === "none" || value === "") {
          call("clearBoxShadow", id);
          return true;
        }
        if (Array.isArray(value)) {
          call("clearBoxShadow", id);
          for (const s of value) {
            if (!s) continue;
            const blur = typeof s.blurRadius === "number" ? s.blurRadius : typeof s.blur === "number" ? s.blur : 4;
            const spread = typeof s.spreadDistance === "number" ? s.spreadDistance : typeof s.spread === "number" ? s.spread : 0;
            call(
              "setBoxShadow",
              id,
              s.offsetX,
              s.offsetY,
              blur,
              spread,
              s.color,
              !!s.inset
            );
          }
          return true;
        }
        if (typeof value === "object") {
          const s = value;
          const blur = typeof s.blurRadius === "number" ? s.blurRadius : typeof s.blur === "number" ? s.blur : 4;
          const spread = typeof s.spreadDistance === "number" ? s.spreadDistance : typeof s.spread === "number" ? s.spread : 0;
          call(
            "setBoxShadow",
            id,
            s.offsetX,
            s.offsetY,
            blur,
            spread,
            s.color,
            !!s.inset
          );
          return true;
        }
        if (typeof value === "string") {
          const parts = _splitMultiShadow(value);
          let emitted = 0;
          for (const p of parts) {
            const parsed = _parseBoxShadow(p);
            if (parsed) {
              call(
                "setBoxShadow",
                id,
                parsed.offsetX,
                parsed.offsetY,
                parsed.blur,
                parsed.spread,
                parsed.color,
                parsed.inset
              );
              emitted++;
            }
          }
          return true;
        }
        return true;
      }
      // CSS `backdrop-filter: blur(Npx)`. The bridge setter takes a numeric
      // blur radius in px; mirror the parser in web-compat-style-decl.js
      // (other filter functions are intentionally ignored — matches the
      // `unsupportedValues: ["other filter functions"]` compat entry).
      case "backdropFilter": {
        const sval = String(value).trim().toLowerCase();
        if (sval === "" || sval === "none") {
          call("setBackdropFilter", id, 0);
          return true;
        }
        const bdm = sval.match(/blur\(\s*([\d.]+)\s*(px)?\s*\)/);
        if (bdm) {
          call("setBackdropFilter", id, parseFloat(bdm[1]) || 0);
          return true;
        }
        return true;
      }
      // pulp #1434 rn logical-edge bundle (sub-agent #27 finding) —
      // RN's logical border-width edges. Route to the per-side bridge
      // setter that preserves the unrelated attribute (color) — same
      // pattern as borderLeftWidth / borderRightWidth.
      case "borderStartWidth": {
        call("setBorderLeftWidth", id, value);
        return true;
      }
      case "borderEndWidth": {
        call("setBorderRightWidth", id, value);
        return true;
      }
      // pulp #1434 rn bridge-wires bundle (sub-agent #27 finding) —
      // RN-style props that already had C++ bridge fns registered
      // but no `@pulp/react` prop-applier dispatch. Each forwards
      // the keyword / string straight through to the matching setter.
      case "backfaceVisibility":
        call("setBackfaceVisibility", id, value);
        return true;
      case "cursor":
        call("setCursor", id, value);
        return true;
      case "filter":
        call("setFilter", id, value);
        return true;
      // pulp #1515 — CSS clip-path / mask cluster. The bridge fns
      // store the value on the View; Skia paint side honors
      // `clip-path: path("...")` via SkPath::FromSVGString. Other
      // clip-path forms (URL refs, named shapes) and mask painting
      // (saveLayer + SkBlendMode::kDstIn) are deferred. The
      // prop-applier dispatches verbatim — keyword normalization
      // and shorthand expansion live in the bridge / JS shim.
      case "clipPath":
        call("setClipPath", id, value);
        return true;
      case "mask":
        call("setMask", id, value);
        return true;
      case "maskImage":
        call("setMaskImage", id, value);
        return true;
      case "maskSize":
        call("setMaskSize", id, value);
        return true;
      // CSS `appearance` — Pulp paints all widgets custom, so this
      // is observably storage-only. Slot exists for round-trip.
      case "appearance":
        call("setAppearance", id, value);
        return true;
      // CSS `object-fit` / `object-position` — image fitting.
      // Storage-only today; ImageView paint follow-up.
      case "objectFit":
        call("setObjectFit", id, value);
        return true;
      case "objectPosition":
        call("setObjectPosition", id, value);
        return true;
      // pulp #1549 — RN `mixBlendMode` (RN 0.76 New Architecture).
      // Forwards the W3C blend-mode keyword string to
      // `setMixBlendMode`; the bridge keyword→enum table lives at
      // widget_bridge.cpp::setMixBlendMode.
      case "mixBlendMode":
        call("setMixBlendMode", id, value);
        return true;
      case "pointerEvents":
        call("setPointerEvents", id, value);
        return true;
      case "userSelect":
        call("setUserSelect", id, value);
        return true;
      // pulp #1552 — backgroundRepeat is storage-only at the View layer
      // (paint-time honoring is a follow-up for url() / repeating
      // gradient backgrounds).
      case "backgroundRepeat":
        call("setBackgroundRepeat", id, value);
        return true;
      // pulp #1737 RN-OOS-fixup (audit 2026-05-11) — RN iOS-legacy
      // box-shadow longhand. Modern RN code uses `boxShadow` (CSS
      // shorthand) which Pulp fully supports, but upstream RN still
      // accepts shadowColor / shadowOffset / shadowOpacity /
      // shadowRadius as cross-platform-ish style props (originally
      // iOS-only, but Pulp implements them cross-platform via the
      // unified BoxShadow struct on View). Each per-attribute setter
      // writes ONE slot of View::shadow_ in isolation so a JSX diff
      // that touches one prop doesn't clobber the others.
      case "shadowColor":
        call("setShadowColor", id, _resolveVar(value));
        return true;
      case "shadowOffset": {
        const o = value;
        const dx = typeof o?.width === "number" ? o.width : 0;
        const dy = typeof o?.height === "number" ? o.height : 0;
        call("setShadowOffset", id, dx, dy);
        return true;
      }
      case "shadowOpacity":
        call("setShadowOpacity", id, value);
        return true;
      case "shadowRadius":
        call("setShadowRadius", id, value);
        return true;
      // pulp #1737 RN-OOS-fixup final sweep — RN's Android-only
      // `elevation` (Material Design 0..24dp). The bridge shim
      // translates the elevation value to a Material-approximated
      // single-shadow BoxShadow so consumers shipping unchanged
      // RN-Android styles render a visible shadow on every Pulp
      // platform. Upstream RN ignores `elevation` on iOS entirely;
      // Pulp's cross-platform translation is a strict improvement.
      case "elevation":
        call("setElevation", id, value);
        return true;
      // pulp #1737 RN-OOS-fixup (#1812) — RN's `borderCurve` corner
      // shape. `circular` (default) keeps Pulp's standard rounded
      // corner; `continuous` switches the View paint to the iOS-
      // style squircle approximation (super-ellipse path). Authors
      // shipping iOS-aesthetic designs with `borderCurve: 'continuous'`
      // now get the visible squircle on every Pulp platform.
      case "borderCurve":
        call("setBorderCurve", id, value);
        return true;
      // pulp #1737 RN-OOS-fixup (final round) — RN's `isolation`.
      // Pulp's per-View paint model is structurally isolated by
      // default (per-View save_layer_with_blend composition + paint-
      // order-scoped z-index), matching the dominant author intent
      // of `isolation: isolate`. Honest CSS-subset claim: both
      // keywords round-trip; behavior matches `isolate` regardless.
      case "isolation":
        call("setIsolation", id, value);
        return true;
      // pulp #1434 (rn NOT-IMPL bundle 1) — RN's `experimental_backgroundImage`
      // (New Architecture only) accepts a CSS gradient string. Route
      // through the existing setBackgroundGradient bridge fn — same
      // shape, same parser. RN also allows an array-of-objects gradient
      // form on Fabric; that shape is NOT supported here (gradient
      // strings only). Catalog flips missing → partial.
      case "experimental_backgroundImage":
        call("setBackgroundGradient", id, value);
        return true;
      default:
        return false;
    }
  }

  // ../pulp/packages/pulp-react/src/prop-applier-typography.ts
  function _normalizeFontWeight(value) {
    if (typeof value === "number") return value;
    const s = String(value).trim().toLowerCase();
    if (s === "normal") return 400;
    if (s === "bold") return 700;
    if (s === "lighter") return 300;
    if (s === "bolder") return 700;
    const n = Number(s);
    return Number.isFinite(n) ? n : 400;
  }
  function _resolveLineHeight(value, props) {
    let n;
    if (typeof value === "number") {
      n = value;
    } else {
      const s = String(value).trim();
      const sp = s.endsWith("px") ? s.slice(0, -2) : s;
      n = parseFloat(sp);
    }
    if (!Number.isFinite(n)) return 0;
    if (n > 0 && n <= 8) {
      const fs = props && typeof props.fontSize === "number" ? props.fontSize : 14;
      return n * fs;
    }
    return n;
  }
  function applyTypographyProp(id, key, value, props) {
    switch (key) {
      // CSS `white-space` — text wrapping behavior (normal | nowrap | pre |
      // pre-wrap). Bridge stores the slot on View; consumed by TextShaper
      // when computing line breaks. Mirror el.style path so JSX
      // `style={{ whiteSpace: 'nowrap' }}` doesn't silently drop.
      case "whiteSpace":
        call("setWhiteSpace", id, value);
        return true;
      // CSS `text-overflow` — clip | ellipsis. Bridge stores the slot for
      // Label paint to consume.
      case "textOverflow":
        call("setTextOverflow", id, value);
        return true;
      // pulp #1434 Phase A2-3 — writing direction (RN ViewStyle uses
      // `writingDirection` for this — CSS uses `direction`, but the
      // pulp prop name `direction` already routes to FlexProps via
      // setFlex above. The CSS-string-form `style.direction = 'rtl'`
      // path goes through the el.style adapter's `direction` case
      // which calls setDirection directly).
      case "writingDirection":
        call("setDirection", id, value);
        return true;
      // pulp #1737 RN-OOS-fixup (catalog audit 2026-05-11) — these 4
      // RN style props were classified `wontfix` in compat.json despite
      // having fully-wired bridge fns AND css-side proof on the
      // matching css/* surfaces (css/verticalAlign, css/textDecoration*
      // all `supported`). One-line route closes the rn-side gap; the
      // catalog flips from wontfix → supported on the same JSON edit.
      //
      // verticalAlign — RN's cross-platform vertical-align prop.
      // textAlignVertical — RN-Android equivalent of verticalAlign;
      //                     same setVerticalAlign target works for
      //                     both since Pulp's Label::vertical_align_
      //                     models what both CSS+RN-Android need.
      // textDecorationColor / textDecorationStyle — text-decoration
      //                     longhand setters; bridge already routes
      //                     to Label::set_text_decoration_color /
      //                     ::set_text_decoration_style.
      case "verticalAlign":
        call("setVerticalAlign", id, value);
        return true;
      case "textAlignVertical":
        call("setVerticalAlign", id, value);
        return true;
      case "textDecorationColor":
        call("setTextDecorationColor", id, _resolveVar(value));
        return true;
      case "textDecorationStyle":
        call("setTextDecorationStyle", id, value);
        return true;
      // Text
      case "text":
        call("setText", id, String(value));
        return true;
      // CSS-canonical `color` aliases RN-canonical `textColor`. Imported
      // React designs (JSX `style={{ color: '...' }}`) silently dropped
      // before this — bridge has `setTextColor`, the dispatch case was
      // missing the alias.
      case "color":
      case "textColor":
        call("setTextColor", id, _resolveVar(value));
        return true;
      // pulp #1434 — widen to include `'auto'` and `'justify'` (CSS /
      // RN canonical). `'auto'` is writing-direction-relative
      // (LTR-only today, degrades to `'left'`); `'justify'` flows to
      // canvas TextAlign::justify (SkParagraph kJustify wiring is a
      // follow-up — backends approximate as left for now).
      case "textAlign":
        call("setTextAlign", id, value);
        return true;
      // Typography — Label widgets honor these via setX bridge fns.
      // pulp #1434 Phase A2-5 — fontFamily IS now dispatched. The
      // bridge picks the first non-empty family from a comma-
      // separated CSS list and stores it on the Label or on the
      // owning View's `inheritable_font_family_` slot for container
      // cascade. The whole-list fallback chain (full font-stack
      // resolution) still depends on SkFontMgr registration in
      // pulp #932 — until that lands, families that aren't already
      // registered with Skia fall through to the platform default.
      // Wiring is independent: when #932 lands, no consumer change
      // is needed — the registry just resolves the same name.
      // pulp #1899 (gap #3) — resolve `var(--mono)` before forwarding.
      // The bridge expects a real family name; the literal "var(--mono)"
      // gives Skia's font matcher nothing to match against and silently
      // falls back to a proportional sans (e.g. Spectr top-bar labels
      // rendered in the wrong typeface AND inside an opacity layer that
      // degraded the LCD AA — both fixed in this change).
      case "fontFamily":
        call("setFontFamily", id, _resolveVar(value));
        return true;
      case "fontSize":
        call("setFontSize", id, value);
        return true;
      case "fontWeight":
        call("setFontWeight", id, _normalizeFontWeight(value));
        return true;
      case "fontStyle":
        call("setFontStyle", id, value);
        return true;
      case "letterSpacing":
        call("setLetterSpacing", id, value);
        return true;
      // Wave 2 rn — `lineHeight` accepts CSS unitless-multiplier
      // semantics. A value `<= 8` is treated as a multiplier of the
      // current `fontSize` from props (defaults to 14 when absent —
      // matches the Label default). Larger values flow through as
      // absolute pixels (the existing path). Px-suffix strings strip
      // the suffix and pass through as absolute too. The bridge
      // setter signature is unchanged — it always sees a number.
      case "lineHeight":
        call("setLineHeight", id, _resolveLineHeight(value, props));
        return true;
      // pulp #1552 — line-clamp + webkit-line-clamp wiring. Both
      // line-clamp keys funnel through the same setter (shared CSS
      // shim case + RN-style alias). 0 / non-finite clears the slot.
      case "lineClamp":
      case "webkitLineClamp": {
        const n = typeof value === "number" ? value : parseInt(String(value), 10);
        call("setLineClamp", id, Number.isFinite(n) ? n : 0);
        return true;
      }
      // pulp #1434 (rn NOT-IMPL bundle 1) — RN's `textDecorationLine`
      // is the spec-aligned name; pulp's bridge uses `setTextDecoration`
      // (single-keyword form). RN allows `'underline line-through'` as
      // a compound — pass through verbatim; the bridge's keyword table
      // currently honors single-keyword `'none' / 'underline' /
      // 'line-through' / 'overline'`. Compound rendering is the same
      // partial gap as css/textDecoration's "single-keyword only" note.
      case "textDecorationLine":
        call("setTextDecoration", id, value);
        return true;
      // pulp #1434 (rn NOT-IMPL bundle 1) — RN textShadow cluster.
      // The CSS shim (`web-compat-style-decl.js`) calls `setTextShadow`
      // defensively (`if (typeof setTextShadow === 'function')`); the
      // bridge does NOT yet register that fn, so paint is a no-op
      // today. Wire the @pulp/react surface here so when the bridge
      // gains the registration (planned slot, see #1548 feature
      // branch) every consumer flips on without a JSX-side change.
      // Each per-attribute setter writes ONE slot in isolation so a
      // diff that touches one prop doesn't clobber the others.
      case "textShadowColor":
        call("setTextShadowColor", id, _resolveVar(value));
        return true;
      case "textShadowOffset": {
        const o = value;
        const dx = typeof o?.width === "number" ? o.width : 0;
        const dy = typeof o?.height === "number" ? o.height : 0;
        call("setTextShadowOffset", id, dx, dy);
        return true;
      }
      case "textShadowRadius":
        call("setTextShadowRadius", id, value);
        return true;
      // pulp #1737 RN-OOS-fixup final sweep — RN's Android-only
      // `includeFontPadding`. Pulp's text-shaping doesn't add
      // Android-vestigial vertical glyph padding regardless of this
      // value, so the bridge fn accepts the keyword + stores it on
      // a View slot (round-trip), and the paint pipeline ignores it.
      // Authors setting `false` (the common case — remove Android
      // padding) get what they want by default; authors setting
      // `true` get the same tight-baseline behavior (Pulp can't add
      // padding it doesn't model). Catalog status is `supported` as
      // an honest CSS-subset claim (same pattern as overscroll-behavior).
      case "includeFontPadding":
        call("setIncludeFontPadding", id, Boolean(value));
        return true;
      // CSS `text-transform`.
      case "textTransform":
        call("setTextTransform", id, value);
        return true;
      // pulp #1434 (rn NOT-IMPL bundle 1) — RN's `fontVariant` is a
      // string-array of OpenType feature tokens (`['small-caps',
      // 'tabular-nums', ...]`). True implementation requires HarfBuzz
      // hb_feature_t setting on the shape pass — outside the scope of
      // this prop-applier wiring. Forward as a comma-joined string to
      // a bridge fn name reserved for the future paint-time impl;
      // until the bridge registers `setFontVariant`, the dispatch is
      // a no-op (the `call` helper silently skips unregistered names).
      // Catalog flips missing → partial with the gotcha documented.
      case "fontVariant": {
        const joined = Array.isArray(value) ? value.join(",") : String(value);
        call("setFontVariant", id, joined);
        return true;
      }
      default:
        return false;
    }
  }

  // ../pulp/packages/pulp-react/src/prop-applier-transform.ts
  function _parseAngleDegrees(v) {
    if (typeof v === "number") return v;
    const s = String(v).trim();
    const m = s.match(/^(-?[\d.]+)\s*(deg|rad|turn|grad)?$/i);
    if (!m) return 0;
    const n = parseFloat(m[1]);
    const unit = (m[2] || "deg").toLowerCase();
    if (unit === "rad") return n * (180 / Math.PI);
    if (unit === "turn") return n * 360;
    if (unit === "grad") return n * 0.9;
    return n;
  }
  function _parseTransformOrigin(s) {
    const work = s.trim().toLowerCase();
    if (work === "center" || work === "") return { x: 0.5, y: 0.5 };
    const tokens = work.split(/\s+/);
    const tok2coord = (tok, axis) => {
      if (tok === "center") return 0.5;
      if (tok === "left" || tok === "top") return 0;
      if (tok === "right" || tok === "bottom") return 1;
      if (tok.endsWith("%")) {
        const n2 = parseFloat(tok.slice(0, -1));
        return Number.isFinite(n2) ? n2 / 100 : 0.5;
      }
      const n = parseFloat(tok);
      if (!Number.isFinite(n)) return 0.5;
      return Math.max(0, Math.min(1, n));
    };
    const x = tok2coord(tokens[0] ?? "center", "x");
    const y = tok2coord(tokens[1] ?? tokens[0] ?? "center", "y");
    return { x, y };
  }
  function _walkTransformArray(arr) {
    const snap = {
      tx: 0,
      ty: 0,
      rotateDeg: 0,
      scale: 1,
      skewX: 0,
      skewY: 0,
      haveTranslate: false,
      haveRotate: false,
      haveScale: false,
      haveSkew: false
    };
    for (const op of arr) {
      if (op == null || typeof op !== "object") continue;
      const o = op;
      const keys = Object.keys(o);
      if (keys.length === 0) continue;
      const k = keys[0];
      const v = o[k];
      switch (k) {
        case "translateX":
          snap.tx = typeof v === "number" ? v : parseFloat(String(v));
          snap.haveTranslate = true;
          break;
        case "translateY":
          snap.ty = typeof v === "number" ? v : parseFloat(String(v));
          snap.haveTranslate = true;
          break;
        case "rotate":
        case "rotateZ":
          snap.rotateDeg = _parseAngleDegrees(v);
          snap.haveRotate = true;
          break;
        case "scale":
          snap.scale = typeof v === "number" ? v : parseFloat(String(v));
          snap.haveScale = true;
          break;
        case "scaleX":
        case "scaleY":
          snap.scale = typeof v === "number" ? v : parseFloat(String(v));
          snap.haveScale = true;
          break;
        // pulp #1434 Triage #9 fan-out — skewX / skewY now reach the
        // bridge via the freshly-registered setSkew(id, x_deg, y_deg).
        // Both axes accumulate independently; one consolidated call
        // emits at dispatch time.
        case "skewX":
          snap.skewX = _parseAngleDegrees(v);
          snap.haveSkew = true;
          break;
        case "skewY":
          snap.skewY = _parseAngleDegrees(v);
          snap.haveSkew = true;
          break;
        // 3D / matrix ops — not modeled in pulp's 2D View. Silently
        // drop. pulp follow-up tracks if/when 3D transforms are
        // introduced.
        case "rotateX":
        case "rotateY":
        case "perspective":
        case "matrix":
          break;
        default:
          break;
      }
    }
    return snap;
  }
  function applyTransformProp(id, key, value) {
    switch (key) {
      // transformOrigin accepts CSS strings of the form `'NN% NN%'`,
      // `'NNpx NNpx'`, `'center'`, or two keyword tokens. The bridge
      // wants two numeric fractions (0..1). Defaults to 0.5/0.5
      // (center) when a token is unrecognized — matches CSS default.
      case "transformOrigin": {
        const parsed = _parseTransformOrigin(String(value ?? "center"));
        call("setTransformOrigin", id, parsed.x, parsed.y);
        return true;
      }
      // pulp #1434 Triage #9 — RN array transform.
      // RN's transform is an array of single-property objects:
      //   transform: [
      //     { translateX: 10 }, { translateY: 20 },
      //     { rotate: '45deg' }, { scale: 1.5 },
      //   ]
      // Walk-once accumulates the snapshot in one pass (so
      // {translateX:10} and {translateY:20} as separate entries
      // produce ONE setTranslate(10,20), not two clobbering calls),
      // then emits only the operations the user specified.
      // Within-array semantics: each render's array is a complete
      // description; absent fields reset to identity. No cross-
      // render state is maintained — passing `transform: undefined`
      // (or removing the prop) goes through the standard prop-
      // removal path and resets translate/rotate/scale on the next
      // re-render that includes the prop.
      case "transform": {
        if (value == null) return true;
        if (!Array.isArray(value)) return true;
        const snap = _walkTransformArray(value);
        if (snap.haveTranslate) call("setTranslate", id, snap.tx, snap.ty);
        if (snap.haveRotate) call("setRotation", id, snap.rotateDeg);
        if (snap.haveScale) call("setScale", id, snap.scale);
        if (snap.haveSkew) call("setSkew", id, snap.skewX, snap.skewY);
        return true;
      }
      // pulp #1434 Phase A2-1 — CSS transitions. The bridge parses
      // the full shorthand into a list of TransitionSpecs that the
      // dispatcher (PR 2 of the ladder) consults when a property
      // changes. Longhand fields apply uniformly across the parsed
      // list (CSS spec semantics).
      case "transition":
        call("setTransition", id, value);
        return true;
      case "transitionProperty":
        call("setTransitionProperty", id, value);
        return true;
      case "transitionDuration": {
        if (typeof value === "number") {
          call("setTransitionDuration", id, value);
          return true;
        }
        const s = String(value).trim();
        const ms = s.endsWith("ms");
        const n = parseFloat(s);
        call("setTransitionDuration", id, ms ? n / 1e3 : n);
        return true;
      }
      case "transitionDelay": {
        if (typeof value === "number") {
          call("setTransitionDelay", id, value);
          return true;
        }
        const s = String(value).trim();
        const ms = s.endsWith("ms");
        const n = parseFloat(s);
        call("setTransitionDelay", id, ms ? n / 1e3 : n);
        return true;
      }
      case "transitionTimingFunction":
        call("setTransitionTimingFunction", id, value);
        return true;
      // pulp #1434 Phase A2-1 — CSS animations. animation-name
      // resolves through the keyframes registry populated by
      // defineKeyframes; PR 4 wires the playback driver. The
      // shorthand path takes a single name + duration; longhand
      // props can be split out by the host as needed.
      case "animationName":
        call("setAnimation", id, value, 1, 1, "normal");
        return true;
      // Codex audit on pulp #1508 (P1): animationDuration was
      // dispatched to setTransitionDuration here, which mutated
      // *transition* timing on the same View instead of *animation*
      // timing. Route through the legacy 2-arg setAnimation control-
      // token form (`setAnimation(id, 'duration', seconds)`) so the
      // bridge stages it on the View's pending-animation slot
      // alongside animationName / animationDelay / etc.
      case "animationDuration": {
        if (typeof value === "number") {
          call("setAnimation", id, "duration", value);
          return true;
        }
        const s = String(value).trim();
        const ms = s.endsWith("ms");
        const n = parseFloat(s);
        call("setAnimation", id, "duration", ms ? n / 1e3 : n);
        return true;
      }
      case "animationDelay": {
        if (typeof value === "number") {
          call("setAnimation", id, "delay", value);
          return true;
        }
        const s = String(value).trim();
        const ms = s.endsWith("ms");
        const n = parseFloat(s);
        call("setAnimation", id, "delay", ms ? n / 1e3 : n);
        return true;
      }
      case "animationTimingFunction":
        call("setAnimation", id, "easing", value);
        return true;
      case "animationIterationCount":
        call(
          "setAnimation",
          id,
          "iterations",
          value === "infinite" ? -1 : typeof value === "number" ? value : parseFloat(String(value)) || 1
        );
        return true;
      case "animationDirection":
        call("setAnimation", id, "direction", value);
        return true;
      case "animationFillMode":
        call("setAnimation", id, "fill", value);
        return true;
      // pulp #1434 Wave 3 css.3 — animation-play-state. Routes
      // through the legacy 2-arg setAnimation control-token form so
      // the bridge stores the keyword on View::animation_play_state_;
      // View::tick_animations honors `paused` by skipping the
      // timeline advance (web spec semantic).
      case "animationPlayState":
        call("setAnimation", id, "play_state", value);
        return true;
      default:
        return false;
    }
  }

  // ../pulp/packages/pulp-react/src/prop-applier-events.ts
  function applyEventProp(id, key, value) {
    switch (key) {
      // pulp #1148 — generalized overlay-click routing. `overlay={true}`
      // claims the view as the active click-eligible overlay so React
      // popovers built on `<View position="absolute">` receive clicks
      // even though hit_test would otherwise resolve to a sibling. The
      // matching releaseOverlay is emitted by applyChangedProps when
      // the prop flips off, and by detach() at unmount.
      case "overlay":
        if (value) {
          call("claimOverlay", id);
          return true;
        }
        call("releaseOverlay", id);
        return true;
      // pulp ARIA modal/popup auto-overlay — UX best-practice default.
      // When the JSX declares an ARIA role that semantically IS a
      // dismissable overlay (`role="dialog" | "alertdialog" | "menu" |
      // "listbox"`) or sets `aria-modal="true"`, claim the overlay so
      // Esc-dismiss + outside-click routing fire automatically. Pre-fix,
      // every consumer (Spectr's dom-adapter, etc.) had to opt in by
      // mirroring a position:absolute heuristic, which missed inset:0
      // full-screen modal backdrops (the most common modal pattern) and
      // every dropdown/menu authored without explicit positioning.
      //
      // Override semantics: an explicit `overlay={false}` still wins
      // because applyChangedProps emits that case AFTER the role case
      // (object iteration order is insertion order, and JSX collects
      // props left-to-right; `overlay` typically appears after `role`).
      // For defensive parity, an explicit overlay={true} is a no-op on
      // top of the auto-claim (idempotent on the bridge side).
      case "role": {
        const r = typeof value === "string" ? value.toLowerCase() : "";
        if (r === "dialog" || r === "alertdialog" || r === "menu" || r === "listbox") {
          call("claimOverlay", id);
          return true;
        }
        return true;
      }
      case "aria-modal": {
        const truthy = value === true || value === "true" || value === "";
        if (truthy) {
          call("claimOverlay", id);
          return true;
        }
        return true;
      }
      default:
        return false;
    }
  }

  // ../pulp/packages/pulp-react/src/prop-applier.ts
  var g3 = globalThis;
  var _aap_count = 0;
  function logApply(stage, id, type, propCount) {
    _aap_count++;
    if (_aap_count <= 60) {
      const lg = g3.__spectrLog;
      if (typeof lg === "function") {
        lg("[applyAll#" + _aap_count + "] " + stage + " " + type + "/" + id + " props=" + propCount);
      }
    }
  }
  function isReactInternal(key) {
    return key === "children" || key === "key" || key === "ref" || key === "id";
  }
  function isEventHandler(key) {
    return key.startsWith("on") && key.length > 2 && key[2] === key[2]?.toUpperCase();
  }
  function eventNameFor(propName) {
    return propName.slice(2).toLowerCase();
  }
  function isHoverEvent(eventName) {
    return eventName === "mouseenter" || eventName === "mouseleave" || eventName === "pointerenter" || eventName === "pointerleave";
  }
  function isPointerEvent(eventName) {
    return eventName === "pointerdown" || eventName === "pointerup" || eventName === "pointercancel" || eventName === "pointermove";
  }
  function isWheelEvent(eventName) {
    return eventName === "wheel";
  }
  function applyEventHandler(id, key, value) {
    if (typeof value !== "function") return;
    const eventName = eventNameFor(key);
    if (isHoverEvent(eventName)) {
      call("registerHover", id);
    }
    if (isPointerEvent(eventName)) {
      call("registerPointer", id);
    }
    if (eventName === "mousedown" || eventName === "mouseup" || eventName === "mousemove") {
      call("registerPointer", id);
    }
    if (isWheelEvent(eventName)) {
      call("registerWheel", id);
    }
    const handler = value;
    call("on", id, eventName, (...rawArgs) => {
      const evt = makeSyntheticEvent(id, eventName, rawArgs);
      handler(evt);
    });
  }
  function emitSvgRectGeometry(id, props) {
    const x = typeof props.x === "number" ? props.x : 0;
    const y = typeof props.y === "number" ? props.y : 0;
    const w = typeof props.width === "number" ? props.width : 0;
    const h = typeof props.height === "number" ? props.height : 0;
    call("setSvgRect", id, x, y, w, h);
  }
  function emitSvgLineGeometry(id, props) {
    const x1 = typeof props.x1 === "number" ? props.x1 : 0;
    const y1 = typeof props.y1 === "number" ? props.y1 : 0;
    const x2 = typeof props.x2 === "number" ? props.x2 : 0;
    const y2 = typeof props.y2 === "number" ? props.y2 : 0;
    call("setSvgLine", id, x1, y1, x2, y2);
  }
  function applyOne(id, type, key, value, props) {
    if (value === void 0 || value === null) {
      return;
    }
    if (type === "SvgRect") {
      if (key === "x" || key === "y" || key === "width" || key === "height") {
        if (props) emitSvgRectGeometry(id, props);
        return;
      }
    }
    if (type === "SvgLine") {
      if (key === "x1" || key === "y1" || key === "x2" || key === "y2") {
        if (props) emitSvgLineGeometry(id, props);
        return;
      }
    }
    if (applyLayoutProp(id, key, value, props)) return;
    if (applyPaintProp(id, key, value)) return;
    if (applyTypographyProp(id, key, value, props)) return;
    if (applyTransformProp(id, key, value)) return;
    if (applyEventProp(id, key, value)) return;
    switch (key) {
      // Widget-specific data
      case "data":
        if (type === "Spectrum") return call("setSpectrumData", id, value);
        if (type === "Waveform") return call("setWaveformData", id, value);
        return;
      case "level":
        return call("setMeterLevel", id, value);
      case "value":
        if (type === "Progress") return call("setProgress", id, value);
        if (type === "Meter") return call("setMeterLevel", id, value);
        return call("setValue", id, value);
      // SvgPath (pulp #994) — wires the SvgPathWidget bridge surface
      // (createSvgPath / setSvgPath / setSvgViewBox / setSvgFill /
      // setSvgStroke / setSvgStrokeWidth) through a typed JSX intrinsic.
      case "d":
        return call("setSvgPath", id, value);
      case "viewBox": {
        if (Array.isArray(value) && value.length >= 2) {
          return call("setSvgViewBox", id, value[0], value[1]);
        }
        if (typeof value === "string") {
          const tokens = value.trim().split(/[\s,]+/).map(parseFloat).filter(Number.isFinite);
          if (tokens.length === 4) {
            return call("setSvgViewBox", id, tokens[2], tokens[3]);
          }
          if (tokens.length === 2) {
            return call("setSvgViewBox", id, tokens[0], tokens[1]);
          }
        }
        return;
      }
      case "fill":
        return call("setSvgFill", id, value);
      case "stroke":
        return call("setSvgStroke", id, value);
      case "strokeWidth":
        return call("setSvgStrokeWidth", id, value);
      default:
        return;
    }
  }
  var _classRulesProvider = null;
  function normalizeHostProps(_type, rawProps) {
    const hasStyle = rawProps.style !== void 0 && rawProps.style !== null && typeof rawProps.style === "object";
    const hasClassName = typeof rawProps.className === "string" && rawProps.className.length > 0;
    if (!hasStyle && !hasClassName) return rawProps;
    const out = /* @__PURE__ */ Object.create(null);
    const isSafeKey = (k) => k !== "__proto__" && k !== "constructor" && k !== "prototype";
    if (hasClassName && _classRulesProvider) {
      const tokens = rawProps.className.split(/\s+/).filter((t) => t.length > 0);
      for (const tok of tokens) {
        const rules = _classRulesProvider(tok);
        if (!rules) continue;
        for (const k of Object.keys(rules)) {
          if (isSafeKey(k)) out[k] = rules[k];
        }
      }
    }
    if (hasStyle) {
      const style = rawProps.style;
      for (const k of Object.keys(style)) out[k] = style[k];
    }
    for (const k of Object.keys(rawProps)) {
      if (k === "style" || k === "className") continue;
      out[k] = rawProps[k];
    }
    return out;
  }
  function applyAllProps(instance) {
    const { id, type, props } = instance;
    logApply("applyAll", id, type, Object.keys(props).length);
    let svgGeometryEmitted = false;
    if (type === "SvgRect") {
      if ("x" in props || "y" in props || "width" in props || "height" in props) {
        emitSvgRectGeometry(id, props);
        svgGeometryEmitted = true;
      }
    } else if (type === "SvgLine") {
      if ("x1" in props || "y1" in props || "x2" in props || "y2" in props) {
        emitSvgLineGeometry(id, props);
        svgGeometryEmitted = true;
      }
    }
    for (const key of Object.keys(props)) {
      if (isReactInternal(key)) continue;
      if (key === "children") continue;
      if (isEventHandler(key)) {
        applyEventHandler(id, key, props[key]);
        continue;
      }
      if (svgGeometryEmitted) {
        if (type === "SvgRect" && (key === "x" || key === "y" || key === "width" || key === "height")) continue;
        if (type === "SvgLine" && (key === "x1" || key === "y1" || key === "x2" || key === "y2")) continue;
      }
      applyOne(id, type, key, props[key], props);
    }
  }
  function applyChangedProps(instance, oldProps, newProps) {
    const { id, type } = instance;
    let mutated = false;
    const svgRectGeoChanged = type === "SvgRect" && (oldProps.x !== newProps.x || oldProps.y !== newProps.y || oldProps.width !== newProps.width || oldProps.height !== newProps.height);
    const svgLineGeoChanged = type === "SvgLine" && (oldProps.x1 !== newProps.x1 || oldProps.y1 !== newProps.y1 || oldProps.x2 !== newProps.x2 || oldProps.y2 !== newProps.y2);
    if (svgRectGeoChanged) {
      emitSvgRectGeometry(id, newProps);
      mutated = true;
    }
    if (svgLineGeoChanged) {
      emitSvgLineGeometry(id, newProps);
      mutated = true;
    }
    for (const key of Object.keys(newProps)) {
      if (isReactInternal(key)) continue;
      if (key === "children") continue;
      if (svgRectGeoChanged && type === "SvgRect" && (key === "x" || key === "y" || key === "width" || key === "height")) continue;
      if (svgLineGeoChanged && type === "SvgLine" && (key === "x1" || key === "y1" || key === "x2" || key === "y2")) continue;
      if (oldProps[key] !== newProps[key]) {
        if (isEventHandler(key)) {
          applyEventHandler(id, key, newProps[key]);
        } else {
          applyOne(id, type, key, newProps[key], newProps);
        }
        mutated = true;
      }
    }
    for (const key of Object.keys(oldProps)) {
      if (isReactInternal(key)) continue;
      if (key === "children") continue;
      if (!(key in newProps)) {
        if (key === "visible") {
          call("setVisible", id, true);
          mutated = true;
        }
        if (key === "opacity") {
          call("setOpacity", id, 1);
          mutated = true;
        }
        if (key === "overlay" && oldProps[key]) {
          call("releaseOverlay", id);
          mutated = true;
        }
        if (key === "background" || key === "backgroundGradient") {
          call("setBackground", id, "transparent");
          mutated = true;
        }
        if (key === "border" || key === "borderColor" || key === "borderWidth") {
          call("setBorderWidth", id, 0);
          mutated = true;
        }
        if (key === "borderTop" || key === "borderRight" || key === "borderBottom" || key === "borderLeft") {
          const side = key.slice("border".length).toLowerCase();
          call("setBorderSide", id, side, 0, "transparent");
          mutated = true;
        }
        if (key === "textColor") {
          call("setTextColor", id, "");
          mutated = true;
        }
      }
    }
    return mutated;
  }

  // ../pulp/packages/pulp-react/src/host-config.ts
  var NoEventPriority = 0;
  var currentUpdatePriority = NoEventPriority;
  var g4 = globalThis;
  var _hc_count = 0;
  function call2(name, ...args) {
    const fn = g4[name];
    if (typeof fn !== "function") {
      const lg = g4.__spectrLog;
      if (typeof lg === "function") lg("[host-config] bridge fn missing: " + name);
      throw new Error("@pulp/react: bridge function " + name + " is not installed");
    }
    _hc_count++;
    if (_hc_count <= 200) {
      const lg = g4.__spectrLog;
      if (typeof lg === "function") {
        const a0 = args[0] !== void 0 ? String(args[0]).slice(0, 30) : "";
        const a1 = args[1] !== void 0 ? String(args[1]).slice(0, 30) : "";
        lg("[hc#" + _hc_count + "] " + name + "(" + a0 + (args.length > 1 ? "," + a1 : "") + ")");
      }
    }
    return fn(...args);
  }
  function createWidget(type, id, parentId, props) {
    switch (type) {
      case "View":
      case "Col":
        call2("createCol", id, parentId);
        return;
      case "Row":
        call2("createRow", id, parentId);
        return;
      case "Panel":
        call2("createPanel", id, parentId);
        return;
      case "Label":
        call2("createLabel", id, asText(props.children) ?? (props.text ?? ""), parentId);
        return;
      case "Button": {
        const text = asText(props.children) ?? (props.text ?? "");
        if (typeof g4.createButton === "function") {
          call2("createButton", id, text, parentId);
        } else {
          call2("createPanel", id, parentId);
          call2("createLabel", id + "_l", text, id);
        }
        return;
      }
      case "TextEditor":
        call2("createTextEditor", id, parentId);
        return;
      case "ScrollView":
        call2("createScrollView", id, parentId);
        return;
      case "Modal":
        call2("createModal", id, parentId);
        return;
      case "Knob":
        call2("createKnob", id, parentId);
        return;
      case "Fader":
        call2("createFader", id, props.orientation ?? "vertical", parentId);
        return;
      case "Spectrum":
        call2("createSpectrum", id, parentId);
        return;
      case "Waveform":
        call2("createWaveform", id, parentId);
        return;
      case "Meter":
        call2("createMeter", id, parentId);
        return;
      case "Progress":
        call2("createProgress", id, parentId);
        return;
      case "XYPad":
        call2("createXYPad", id, parentId);
        return;
      case "Checkbox":
        call2("createCheckbox", id, parentId);
        return;
      case "Toggle":
        call2("createToggle", id, parentId);
        return;
      case "Combo":
        call2("createCombo", id, parentId);
        return;
      case "ListBox":
        call2("createListBox", id, parentId);
        return;
      case "Canvas":
        call2("createCanvas", id, parentId);
        return;
      case "Image":
        call2("createImage", id, parentId);
        return;
      case "Icon":
        call2("createIcon", id, parentId);
        return;
      case "SvgPath":
        call2("createSvgPath", id, parentId);
        return;
      case "SvgRect":
        call2("createSvgRect", id, parentId);
        return;
      case "SvgLine":
        call2("createSvgLine", id, parentId);
        return;
      default: {
        const lower = String(type).toLowerCase();
        switch (lower) {
          case "div":
          case "section":
          case "article":
          case "aside":
          case "header":
          case "footer":
          case "nav":
          case "main":
          case "figure":
          case "figcaption":
          case "form":
          case "ul":
          case "ol":
          case "li":
          case "dl":
          case "dt":
          case "dd": {
            const txt = asText(props.children);
            if (txt !== void 0 && txt.length > 0) {
              call2("createLabel", id, txt, parentId);
            } else {
              call2("createCol", id, parentId);
            }
            return;
          }
          case "span":
          case "p":
          case "label":
          case "h1":
          case "h2":
          case "h3":
          case "h4":
          case "h5":
          case "h6":
          case "b":
          case "i":
          case "em":
          case "strong":
          case "small":
          case "code":
          case "pre":
          case "a":
          case "td":
          case "th":
          case "title":
          case "text":
          case "tspan":
          case "desc": {
            const txt = asText(props.children);
            if (txt !== void 0) {
              call2("createLabel", id, txt, parentId);
            } else {
              call2("createRow", id, parentId);
            }
            return;
          }
          case "button": {
            const text = asText(props.children) ?? (props.text ?? "");
            if (typeof g4.createButton === "function") {
              call2("createButton", id, text, parentId);
            } else {
              call2("createPanel", id, parentId);
              call2("createLabel", id + "_l", text, id);
            }
            return;
          }
          case "input": {
            const inputType = String(props.type ?? "text").toLowerCase();
            if (inputType === "range") {
              const orient = props["aria-orientation"] === "vertical" ? "vertical" : "horizontal";
              call2("createFader", id, orient, parentId);
            } else if (inputType === "checkbox") {
              call2("createCheckbox", id, parentId);
            } else {
              call2("createTextEditor", id, parentId);
            }
            return;
          }
          case "textarea":
            call2("createTextEditor", id, parentId);
            return;
          case "select":
            call2("createCombo", id, parentId);
            return;
          case "progress":
            call2("createProgress", id, parentId);
            return;
          case "img":
            call2("createImage", id, parentId);
            return;
          case "canvas":
            call2("createCanvas", id, parentId);
            return;
          // pulp routing-parity sweep 2026-06-08 — lowercase widget
          // intrinsic aliases. These mirror the capitalized widget
          // cases above (the source of truth) so a lowercase
          // `<knob>` / `<fader>` / … tag dispatches to the SAME
          // native createX bridge call instead of silently falling
          // through to the createCol container fallback. (lowercase
          // `select`/`progress`/`img`/`canvas` are already handled
          // just above; widget-specific intrinsics added here.)
          case "knob":
            call2("createKnob", id, parentId);
            return;
          case "fader":
            call2("createFader", id, props.orientation ?? "vertical", parentId);
            return;
          case "toggle":
            call2("createToggle", id, parentId);
            return;
          case "combo":
            call2("createCombo", id, parentId);
            return;
          case "checkbox":
            call2("createCheckbox", id, parentId);
            return;
          case "spectrum":
            call2("createSpectrum", id, parentId);
            return;
          case "waveform":
            call2("createWaveform", id, parentId);
            return;
          case "meter":
            call2("createMeter", id, parentId);
            return;
          case "xypad":
            call2("createXYPad", id, parentId);
            return;
          case "listbox":
            call2("createListBox", id, parentId);
            return;
          case "icon":
            call2("createIcon", id, parentId);
            return;
          case "svg":
            call2("createCol", id, parentId);
            return;
          // SVG = container; children paint
          case "path": {
            call2("createSvgPath", id, parentId);
            call2("setPosition", id, "absolute");
            call2("setTop", id, 0);
            call2("setLeft", id, 0);
            call2("setRight", id, 0);
            call2("setBottom", id, 0);
            call2("setPointerEvents", id, "none");
            return;
          }
          case "circle": {
            call2("createSvgPath", id, parentId);
            const cx = typeof props.cx === "number" ? props.cx : Number(props.cx) || 0;
            const cy = typeof props.cy === "number" ? props.cy : Number(props.cy) || 0;
            const r = typeof props.r === "number" ? props.r : Number(props.r) || 0;
            const gg = g4;
            if (typeof gg.__pulpCircleStats__ !== "object" || gg.__pulpCircleStats__ === null) {
              gg.__pulpCircleStats__ = { total: 0, withR: 0, samples: [] };
            }
            const stats = gg.__pulpCircleStats__;
            stats.total = stats.total + 1;
            if (r > 0) {
              stats.withR = stats.withR + 1;
              const d = `M ${cx - r} ${cy} a ${r} ${r} 0 1 0 ${2 * r} 0 a ${r} ${r} 0 1 0 ${-2 * r} 0 Z`;
              call2("setSvgPath", id, d);
              const samples = stats.samples;
              if (samples.length < 5) {
                samples.push({ id, cx, cy, r, d, fill: props.fill, stroke: props.stroke });
              }
            }
            call2("setPosition", id, "absolute");
            call2("setTop", id, 0);
            call2("setLeft", id, 0);
            call2("setRight", id, 0);
            call2("setBottom", id, 0);
            call2("setPointerEvents", id, "none");
            return;
          }
          case "rect": {
            call2("createSvgRect", id, parentId);
            call2("setPosition", id, "absolute");
            call2("setTop", id, 0);
            call2("setLeft", id, 0);
            call2("setRight", id, 0);
            call2("setBottom", id, 0);
            call2("setPointerEvents", id, "none");
            return;
          }
          case "line": {
            call2("createSvgLine", id, parentId);
            call2("setPosition", id, "absolute");
            call2("setTop", id, 0);
            call2("setLeft", id, 0);
            call2("setRight", id, 0);
            call2("setBottom", id, 0);
            call2("setPointerEvents", id, "none");
            return;
          }
          case "g":
            call2("createCol", id, parentId);
            return;
          // <svg><g> group
          default: {
            const lg = g4.__spectrLog;
            if (typeof lg === "function") lg("[host-config] unknown intrinsic " + lower + " \u2014 falling back to createCol");
            call2("createCol", id, parentId);
            return;
          }
        }
      }
    }
  }
  function asText(children) {
    if (typeof children === "string") return children;
    if (typeof children === "number") return String(children);
    if (Array.isArray(children)) {
      const parts = [];
      for (const c of children) {
        if (c == null || typeof c === "boolean") continue;
        const part = asText(c);
        if (part === void 0) return void 0;
        parts.push(part);
      }
      return parts.join("");
    }
    return void 0;
  }
  var TEXT_BEARING = /* @__PURE__ */ new Set([
    "Label",
    "Button",
    "TextEditor",
    "b",
    "button",
    "code",
    "desc",
    "em",
    "h1",
    "h2",
    "h3",
    "h4",
    "h5",
    "h6",
    "i",
    "label",
    "li",
    "p",
    "pre",
    "span",
    "strong",
    "td",
    "text",
    "th",
    "title",
    "tspan",
    // pulp jsx-instrument-import 2026-05-17 — container tags that
    // host-config conditionally treats as text-leaves when their
    // children are pure string/number. Without this, shouldSetTextContent
    // returns false → React mounts the text children as separate
    // synthetic Label widgets → duplicate text rendered on top of the
    // createLabel we already made. The shouldSetTextContent check below
    // still gates on children-are-pure-text (so divs with element
    // children correctly route to createCol via createWidget's else
    // branch).
    "div",
    "section",
    "article",
    "aside",
    "header",
    "footer",
    "nav",
    "main",
    "figure",
    "figcaption",
    "form",
    "ul",
    "ol",
    "dl",
    "dt",
    "dd"
  ]);
  var PulpHostConfig = {
    // ── Renderer identity ───────────────────────────────────────────
    supportsMutation: true,
    supportsPersistence: false,
    supportsHydration: false,
    isPrimaryRenderer: true,
    supportsMicrotasks: true,
    scheduleMicrotask: typeof queueMicrotask === "function" ? queueMicrotask : (cb) => Promise.resolve().then(cb),
    // ── Timeouts (Ink pattern) ──────────────────────────────────────
    scheduleTimeout: setTimeout,
    cancelTimeout: clearTimeout,
    noTimeout: -1,
    // ── Event priority (DefaultEventPriority for v0) ────────────────
    setCurrentUpdatePriority(newPriority) {
      currentUpdatePriority = newPriority;
    },
    getCurrentUpdatePriority() {
      return currentUpdatePriority;
    },
    resolveUpdatePriority() {
      return currentUpdatePriority !== NoEventPriority ? currentUpdatePriority : import_constants.DefaultEventPriority;
    },
    /// React 18 name for the same thing — react-reconciler@0.29 calls this
    /// from `requestUpdateLane`. Without it, every render throws
    /// "getCurrentEventPriority is not a function". Keep both names so the
    /// host config works on both 0.29 (React 18) and 0.31 (React 19).
    getCurrentEventPriority() {
      return import_constants.DefaultEventPriority;
    },
    // ── Host context (no scoped state needed for v0) ────────────────
    getRootHostContext() {
      return {};
    },
    getChildHostContext() {
      return {};
    },
    // ── Instance lifecycle ──────────────────────────────────────────
    createInstance(type, props, rootContainer, _hostContext, _internalHandle) {
      const normalizedProps = normalizeHostProps(type, props);
      const id = normalizedProps.id ?? autoId(rootContainer);
      let domShim = null;
      try {
        const ElementCtor = globalThis.Element;
        if (typeof ElementCtor === "function") {
          const shim = new ElementCtor(type, id);
          shim._nativeCreated = true;
          shim.__pulpId = id;
          shim.id = id;
          domShim = shim;
        }
      } catch {
      }
      return {
        id,
        type,
        // parentId stays undefined until attached
        props: { ...normalizedProps },
        childIds: [],
        onBridge: false,
        pendingChildren: [],
        _dom: domShim
      };
    },
    createTextInstance(text, rootContainer, _hostContext, _internalHandle) {
      if (text == null) return { id: "text_empty", type: "Label", props: {}, childIds: [], onBridge: false, pendingChildren: [] };
      const id = autoId(rootContainer);
      return {
        id,
        type: "Label",
        props: { children: String(text), text: String(text) },
        childIds: [],
        onBridge: false,
        pendingChildren: []
      };
    },
    shouldSetTextContent(type, props) {
      if (!TEXT_BEARING.has(type)) return false;
      const children = props?.children;
      if (children == null) return true;
      if (typeof children === "string" || typeof children === "number") return true;
      if (Array.isArray(children)) {
        for (const c of children) {
          if (c == null) continue;
          if (typeof c !== "string" && typeof c !== "number") return false;
        }
        return true;
      }
      return false;
    },
    // ── First-mount attachment ──────────────────────────────────────
    appendInitialChild(parentInstance, child) {
      attach(parentInstance, child);
    },
    finalizeInitialChildren(_instance, _type, _props, _rootContainer, _hostContext) {
      return false;
    },
    // ── Mutation: append / insert / remove ──────────────────────────
    appendChild(parentInstance, child) {
      attach(parentInstance, child);
    },
    appendChildToContainer(container, child) {
      attachToRoot(container, child);
    },
    insertBefore(parentInstance, child, beforeChild) {
      const beforeIdx = parentInstance.childIds.indexOf(beforeChild.id);
      const sameParent = child.parentId === parentInstance.id && child.onBridge;
      if (sameParent) {
        const oldIdx = parentInstance.childIds.indexOf(child.id);
        if (oldIdx >= 0) parentInstance.childIds.splice(oldIdx, 1);
        const insertIdx = beforeIdx >= 0 ? beforeIdx : parentInstance.childIds.length;
        parentInstance.childIds.splice(insertIdx, 0, child.id);
        if (typeof g4.insertChild === "function") {
          call2("insertChild", parentInstance.id, child.id, insertIdx);
        } else if (typeof g4.moveWidget === "function") {
          call2("moveWidget", child.id, parentInstance.id, insertIdx);
        }
        return;
      }
      attach(parentInstance, child, beforeIdx >= 0 ? beforeIdx : void 0);
    },
    insertInContainerBefore(container, child, beforeChild) {
      attachToRoot(
        container,
        child,
        /* index */
        -1,
        beforeChild
      );
    },
    removeChild(parentInstance, child) {
      detach(parentInstance, child);
    },
    removeChildFromContainer(_container, child) {
      if (typeof g4.removeWidget === "function") call2("removeWidget", child.id);
    },
    clearContainer(_container) {
    },
    // ── Updates ────────────────────────────────────────────────────
    prepareUpdate(_instance, type, oldProps, newProps) {
      const oldN = normalizeHostProps(type, oldProps);
      const newN = normalizeHostProps(type, newProps);
      return shallowDiff(oldN, newN);
    },
    commitUpdate(instance, _updatePayload, type, oldProps, newProps, _internalHandle) {
      const oldN = normalizeHostProps(type, oldProps);
      const newN = normalizeHostProps(type, newProps);
      applyChangedProps(instance, oldN, newN);
      instance.props = { ...newN };
      if (TEXT_BEARING.has(instance.type)) {
        const oldText = asText(oldN.children) ?? oldN.text;
        const newText = asText(newN.children) ?? newN.text;
        if (oldText !== newText && newText !== void 0) {
          if (typeof g4.setText === "function") call2("setText", instance.id, newText);
        }
      }
    },
    commitTextUpdate(_textInstance, _oldText, _newText) {
    },
    // pulp #1840 P1 (Codex follow-up) — React's mutation reconciler
    // calls resetTextContent(instance) when shouldSetTextContent flips
    // from true → false on an existing TEXT_BEARING node. Concretely,
    // a transition like <span>hi</span> → <span><em>hi</em></span>:
    // the old commit treated <span> as text-bearing and pushed "hi" via
    // setText; the new commit needs the inner <em> child mounted, so
    // React first asks the host to clear the stale text. Without this
    // hook the reconciler can throw (or leave stale text un-cleared on
    // hosts that tolerate the missing callback). Clear by calling
    // setText(id, '') — which the bridge already handles as a no-op for
    // non-text-capable types, so this is safe for the whole alias set.
    resetTextContent(instance) {
      if (typeof g4.setText === "function") call2("setText", instance.id, "");
    },
    // ── Per-commit flush ───────────────────────────────────────────
    prepareForCommit(_container) {
      return null;
    },
    resetAfterCommit(_container) {
      if (typeof g4.layout === "function") call2("layout");
    },
    // ── Misc required no-ops / passthroughs ────────────────────────
    // Phase 7 codex round 5 — return the DOM-shim element when
    // available so the bundle's `ref.current.X` calls resolve to
    // browser-DOM-shape methods (getContext, getBoundingClientRect,
    // style setters, addEventListener, etc.). Falls back to the
    // Instance descriptor for tests that run without the C++ shim
    // chain. The PulpInstance return type is technically wrong (we
    // return an Element or PulpInstance), but react-reconciler's
    // types are inflexible here and we cast at the call site.
    getPublicInstance(instance) {
      const inst = instance;
      return inst._dom ?? instance;
    },
    preparePortalMount(_container) {
    },
    detachDeletedInstance(_instance) {
    },
    beforeActiveInstanceBlur() {
    },
    afterActiveInstanceBlur() {
    },
    prepareScopeUpdate() {
    },
    getInstanceFromScope() {
      return null;
    },
    getInstanceFromNode() {
      return null;
    }
  };
  function attach(parent, child, index) {
    const wasAttachedElsewhere = child.parentId !== void 0 && child.parentId !== parent.id;
    child.parentId = parent.id;
    const insertIdx = index !== void 0 && index >= 0 ? index : parent.childIds.length;
    if (insertIdx === parent.childIds.length) {
      parent.childIds.push(child.id);
    } else {
      parent.childIds.splice(insertIdx, 0, child.id);
    }
    if (wasAttachedElsewhere && child.onBridge) {
      if (typeof g4.moveWidget === "function") {
        call2("moveWidget", child.id, parent.id, insertIdx);
      } else {
        if (typeof g4.removeWidget === "function") call2("removeWidget", child.id);
        child.onBridge = false;
        if (parent.onBridge) materialize(parent, child);
      }
      return;
    }
    if (parent.onBridge) {
      materialize(parent, child);
    } else {
      parent.pendingChildren.push({ child, index: insertIdx });
    }
  }
  function attachToRoot(container, child, _index = -1, _before) {
    child.parentId = container.rootId;
    materializeUnder(container.rootId, child);
  }
  function materialize(parent, child) {
    materializeUnder(parent.id, child);
  }
  function bindSourceLocation(child) {
    const src = child.props.__source;
    if (!src || typeof src.fileName !== "string" || src.fileName.length === 0) {
      return;
    }
    if (typeof g4.setSource !== "function") return;
    const line = typeof src.lineNumber === "number" ? src.lineNumber : 0;
    const col = typeof src.columnNumber === "number" ? src.columnNumber : 0;
    call2("setSource", child.id, src.fileName, line, col);
  }
  function materializeUnder(parentId, child) {
    if (child.onBridge) return;
    createWidget(child.type, child.id, parentId, child.props);
    applyAllProps(child);
    bindSourceLocation(child);
    child.onBridge = true;
    if (child.pendingChildren.length > 0) {
      const drained = child.pendingChildren;
      child.pendingChildren = [];
      for (const { child: gc } of drained) {
        materializeUnder(child.id, gc);
      }
    }
  }
  function detach(parent, child) {
    const idx = parent.childIds.indexOf(child.id);
    if (idx >= 0) parent.childIds.splice(idx, 1);
    if (typeof g4.removeWidget === "function") call2("removeWidget", child.id);
    child.parentId = void 0;
  }
  function autoId(container) {
    const n = ++container.nextId;
    return `pr_${n.toString(36)}`;
  }
  function shallowDiff(a, b) {
    const aKeys = Object.keys(a);
    const bKeys = Object.keys(b);
    if (aKeys.length !== bKeys.length) return true;
    for (const k of aKeys) if (a[k] !== b[k]) return true;
    return false;
  }

  // ../pulp/packages/pulp-react/src/intrinsics.ts
  var import_react = __toESM(require_react(), 1);
  var View = (props) => (0, import_react.createElement)("View", props);
  var Row = (props) => (0, import_react.createElement)("Row", props);
  var Label = (props) => (0, import_react.createElement)("Label", props);
  var Button = (props) => (0, import_react.createElement)("Button", props);
  var Canvas = (props) => (0, import_react.createElement)("Canvas", props);

  // ../pulp/packages/pulp-react/src/shortcuts.ts
  var import_react2 = __toESM(require_react(), 1);
  var MOD_SHIFT = 1 << 0;
  var MOD_CTRL = 1 << 1;
  var MOD_ALT = 1 << 2;
  var MOD_META = 1 << 3;
  var MOD_CMD = 1 << 4;

  // ../pulp/packages/pulp-react/src/index.ts
  var reconciler = (0, import_react_reconciler.default)(PulpHostConfig);
  try {
    reconciler.injectIntoDevTools({
      bundleType: 0,
      // 0 = production, 1 = development
      version: "0.0.1",
      rendererPackageName: "@pulp/react"
    });
  } catch {
  }
  var rootsByContainer = /* @__PURE__ */ new WeakMap();
  function createRoot(rootId = "") {
    return { rootId, nextId: 0 };
  }
  var defaultContainer = null;
  function render(element, container) {
    const c = container ?? (defaultContainer ??= createRoot(""));
    let rec = rootsByContainer.get(c);
    if (!rec) {
      const fiberRoot = reconciler.createContainer(
        c,
        // LegacyRoot = synchronous mode. Matches the v0 architecture
        // doc ("Concurrent mode: deferred for v0") and means each
        // render() returns after the bridge calls have all been
        // emitted — no microtask gap, which is what tests and
        // AOT-bundled plugin code both expect.
        import_constants2.LegacyRoot,
        null,
        false,
        null,
        "@pulp/react",
        (err) => {
          console.error("[@pulp/react] recoverable error:", err);
        },
        null
      );
      rec = { container: c, fiberRoot };
      rootsByContainer.set(c, rec);
    }
    reconciler.updateContainer(element, rec.fiberRoot, null, null);
    return c;
  }

  // native-ui/src/editor.tsx
  var DESIGN_WIDTH = 1320;
  var DESIGN_HEIGHT = 860;
  var BAND_COUNT = 32;
  function clampGain(value) {
    return Math.max(-24, Math.min(24, value));
  }
  function AnalyzerCanvas({ magnitudes }) {
    (0, import_react3.useLayoutEffect)(() => {
      const call3 = (name, ...args) => {
        const fn = globalThis[name];
        if (typeof fn !== "function")
          throw new Error(`required CanvasWidget bridge op ${name} is unavailable`);
        fn("spectr-analyzer-canvas", ...args);
      };
      call3("canvasClear");
      call3("canvasFillRect", 0, 0, 1e3, 300, "#080b10");
      for (let x = 0; x <= 1e3; x += 125) {
        call3("canvasStrokeLine", x, 0, x, 300, "#202a35", 1);
      }
      for (let y = 0; y <= 300; y += 60) {
        call3("canvasStrokeLine", 0, y, 1e3, y, "#202a35", 1);
      }
      if (magnitudes.length > 1) {
        call3("canvasSetStrokeColor", "#2be1ff");
        call3("canvasSetLineWidth", 2);
        call3("canvasBeginPath");
        magnitudes.forEach((magnitude, index) => {
          const x = index * 1e3 / (magnitudes.length - 1);
          const y = 294 - Math.max(0, Math.min(1, magnitude)) * 270;
          call3(index === 0 ? "canvasMoveTo" : "canvasLineTo", x, y);
        });
        call3("canvasStrokePath");
      }
    }, [magnitudes]);
    return /* @__PURE__ */ import_react3.default.createElement(Canvas, { id: "spectr-analyzer-canvas", width: 1e3, height: 300 });
  }
  function App() {
    const [bands, setBands] = (0, import_react3.useState)(
      () => Array.from({ length: BAND_COUNT }, () => ({ gainDb: 0, muted: false }))
    );
    const [magnitudes, setMagnitudes] = (0, import_react3.useState)([]);
    const [viewport, setViewport] = (0, import_react3.useState)({ minHz: 20, maxHz: 2e4 });
    const sculpting = (0, import_react3.useRef)(false);
    (0, import_react3.useLayoutEffect)(() => {
      globalThis.__spectrHydrate = (payload) => {
        if (!payload || !Array.isArray(payload.bands) || payload.bands.length !== BAND_COUNT) return;
        setBands(payload.bands.map((band) => ({
          gainDb: clampGain(Number.isFinite(band.gainDb) ? band.gainDb : 0),
          muted: band.muted === true
        })));
        if (payload.viewport && Number.isFinite(payload.viewport.minHz) && Number.isFinite(payload.viewport.maxHz)) {
          setViewport(payload.viewport);
        }
      };
      globalThis.__spectrAnalyzer = (next) => {
        if (Array.isArray(next) && next.length > 1 && next.every(Number.isFinite))
          setMagnitudes(next);
      };
      return () => {
        globalThis.__spectrHydrate = void 0;
        globalThis.__spectrAnalyzer = void 0;
      };
    }, []);
    const sculpt = (index, event) => {
      const bounds = event.currentTarget?.getBoundingClientRect?.();
      if (!bounds || bounds.height <= 0 || !Number.isFinite(event.clientY)) return;
      const normalized = 1 - (event.clientY - bounds.top) / bounds.height;
      const gainDb = clampGain(normalized * 48 - 24);
      setBands((current) => current.map((band, i) => i === index ? { ...band, gainDb } : band));
    };
    return /* @__PURE__ */ import_react3.default.createElement(
      View,
      {
        id: "spectr-native-root",
        width: DESIGN_WIDTH,
        height: DESIGN_HEIGHT,
        background: "#05070a",
        padding: 24,
        gap: 18
      },
      /* @__PURE__ */ import_react3.default.createElement(Row, { height: 56, alignItems: "center", justifyContent: "space-between" }, /* @__PURE__ */ import_react3.default.createElement(Row, { alignItems: "center", gap: 14 }, /* @__PURE__ */ import_react3.default.createElement(Label, { textColor: "#f5f8fb" }, "SPECTR"), /* @__PURE__ */ import_react3.default.createElement(Label, { textColor: "#65717e" }, "NATIVE N1 / QUICKJS")), /* @__PURE__ */ import_react3.default.createElement(Row, { alignItems: "center", gap: 18 }, /* @__PURE__ */ import_react3.default.createElement(Label, { textColor: "#7e8b98" }, Math.round(viewport.minHz), " Hz"), /* @__PURE__ */ import_react3.default.createElement(Label, { textColor: "#2be1ff" }, "32 BANDS"), /* @__PURE__ */ import_react3.default.createElement(Label, { textColor: "#7e8b98" }, Math.round(viewport.maxHz / 1e3), " kHz"))),
      /* @__PURE__ */ import_react3.default.createElement(
        View,
        {
          height: 350,
          background: "#080b10",
          border: { color: "#1c2630", width: 1, radius: 8 },
          padding: 18
        },
        /* @__PURE__ */ import_react3.default.createElement(AnalyzerCanvas, { magnitudes })
      ),
      /* @__PURE__ */ import_react3.default.createElement(Row, { height: 330, gap: 5, alignItems: "end" }, bands.map((band, index) => {
        const fill = band.muted ? "#32151b" : band.gainDb >= 0 ? "#123943" : "#17212b";
        const height = 120 + (band.gainDb + 24) * 3.75;
        return /* @__PURE__ */ import_react3.default.createElement(
          Button,
          {
            key: index,
            id: `spectr-band-${index}`,
            width: 34,
            height,
            background: fill,
            border: { color: band.muted ? "#ff5964" : "#2be1ff", width: 1, radius: 3 },
            textColor: band.muted ? "#ff7a84" : "#d8f8ff",
            onClick: () => setBands((current) => current.map((item, i) => i === index ? { ...item, muted: !item.muted } : item)),
            onPointerDown: (event) => {
              sculpting.current = true;
              sculpt(index, event);
            },
            onPointerMove: (event) => {
              if (sculpting.current) sculpt(index, event);
            },
            onPointerUp: () => {
              sculpting.current = false;
            },
            onPointerCancel: () => {
              sculpting.current = false;
            }
          },
          String(index + 1)
        );
      })),
      /* @__PURE__ */ import_react3.default.createElement(Row, { height: 32, justifyContent: "space-between", alignItems: "center" }, /* @__PURE__ */ import_react3.default.createElement(Label, { textColor: "#596673" }, "INPUT PROBE: TAP / SCULPT IS UI-LOCAL IN N1"), /* @__PURE__ */ import_react3.default.createElement(Label, { textColor: "#596673" }, "LIVE ANALYZER DATA FROM VISUALIZATIONBRIDGE"))
    );
  }
  render(/* @__PURE__ */ import_react3.default.createElement(App, null));
})();
/*! Bundled license information:

react/cjs/react.production.min.js:
  (**
   * @license React
   * react.production.min.js
   *
   * Copyright (c) Facebook, Inc. and its affiliates.
   *
   * This source code is licensed under the MIT license found in the
   * LICENSE file in the root directory of this source tree.
   *)

scheduler/cjs/scheduler.production.min.js:
  (**
   * @license React
   * scheduler.production.min.js
   *
   * Copyright (c) Facebook, Inc. and its affiliates.
   *
   * This source code is licensed under the MIT license found in the
   * LICENSE file in the root directory of this source tree.
   *)

react-reconciler/cjs/react-reconciler.production.min.js:
  (**
   * @license React
   * react-reconciler.production.min.js
   *
   * Copyright (c) Facebook, Inc. and its affiliates.
   *
   * This source code is licensed under the MIT license found in the
   * LICENSE file in the root directory of this source tree.
   *)

react-reconciler/cjs/react-reconciler-constants.production.min.js:
  (**
   * @license React
   * react-reconciler-constants.production.min.js
   *
   * Copyright (c) Facebook, Inc. and its affiliates.
   *
   * This source code is licensed under the MIT license found in the
   * LICENSE file in the root directory of this source tree.
   *)
*/
