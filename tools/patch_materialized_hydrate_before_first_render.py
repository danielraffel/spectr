#!/usr/bin/env python3
"""Hydrate the editor before its first render.

WHY THIS EXISTS

    The editor used to mount with defaults and hydrate afterwards: the App
    posted `editor_ready` from a mount effect, the processor's state arrived
    in an animation frame, and a passive effect handed it to the bank. Build
    info and the modulation panel each fetched their own state in a promise.
    Every one of those landed as its own React commit after the mount, and in
    this captured import every commit that touches a host node re-applies the
    captured document. Measured on the AU editor-open probe (M5 Max, traced
    build): six commits after the mount, 20-45 ms each, ~250 ms of the warm
    open.

    The processor's dispatcher answers synchronously, so none of that has to
    wait. `window.pulp.initial(type)` (spectr-native-services.js) reads a
    read-only verb once per realm, and the first render seeds from it.

WHAT IT CHANGES

    App          settings.bandCount, nativeHydrated, snapshot status and the
                 pattern library start from the processor's state, and the
                 bank gets that state as `initialNativeState`.
    FilterBank   gains, view, snapshots, macros, history availability and the
                 applied revision start from `initialNativeState`; the
                 publication effect's one-shot suppressor starts armed, so the
                 mount never publishes the state it was just given back to the
                 processor. Clearing the selection on a band-count change
                 keeps an already-empty selection. The plot's static canvas
                 mounts from a layout effect, so it is still created after
                 every first-render node (the reason it mounts late) but
                 inside the mount rather than as a commit of its own.
    Modulation   the hook's first state comes from the same read.
    Morph        the slider starts at the processor's morph.
    Freeze       the store starts from the same read.
    Build info   read synchronously from a layout effect (same id order: its
                 rows are still created after the first render).
    Latency      the rail chip and the Settings group subscribe from layout
                 effects, so the App's layout-pass handover (which keeps the
                 Latency group out of the first render, and so keeps every
                 first-render node's generated id) reaches them.
    Tracing      the badge shows from the mount when build info says the
                 build traces, so the native show call commits nothing.

    The mount's `editor_ready` reconciles instead of re-hydrating: it re-reads
    after every listener subscribed and hydrates only if the processor moved.

    Gate: test/test_editor_open.cpp "[editor-open]" counts commits after the
    mount (tools/patch_materialized_runtime_commit_stats.py).

Raw-text surgery on the escaped document. Idempotent.
Exit: 0 applied or already applied, 1 anchor missing/ambiguous.
"""
import json
import sys
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "native-ui/materialized/materialized-document.runtime.json"
MARKER = "const [initialNative] = useAppS("

EDITS = [
    # ── App ────────────────────────────────────────────────────────────────
    (
        "app reads the processor's state for its first render",
        '''  const defaults = JSON.parse(defaultsRaw);
  const [settings, setSettings] = useAppS(defaults);
''',
        '''  const defaults = JSON.parse(defaultsRaw);
  // Hydrate before the first render: the processor's dispatcher answers
  // synchronously, so the editor mounts showing its state instead of
  // mounting with defaults and committing again when a hydrate arrives.
  // See tools/patch_materialized_hydrate_before_first_render.py.
  const [initialNative] = useAppS(() => {
    const bridge = typeof window !== "undefined" ? window.pulp : null;
    if (!bridge || typeof bridge.initial !== "function") return null;
    const payload = bridge.initial("processing_state_get");
    // The Latency group renders only once its state is known. It is handed
    // over in the mount's layout pass below rather than here, so the first
    // render creates exactly the nodes -- and therefore the generated native
    // ids -- it created before this hydrated; tools and the runtime still
    // address a few of those ids directly.
    const state = payload
      ? window.SpectrNativeState.parse({ ...payload, latency: void 0 }) : null;
    if (!state) return null;
    if (window.spectrRecordModulationFrame)
      window.spectrRecordModulationFrame(payload.modulation);
    return { state, latency: payload.latency || null,
      library: window.SpectrNativePatterns.parse(state.patternsJson) };
  });
  (React.useLayoutEffect || React.useEffect)(() => {
    if (initialNative && initialNative.latency)
      window.SpectrNativeState.parse({ latency: initialNative.latency });
  }, []);
  const [settings, setSettings] = useAppS(() => initialNative
    ? { ...defaults, bandCount: initialNative.state.n } : defaults);
''',
    ),
    (
        "app starts hydrated when it read the state",
        '''  const [nativeHydrated, setNativeHydrated] = useAppS(!nativeBridgeAvailable);
''',
        '''  const [nativeHydrated, setNativeHydrated] = useAppS(
    !nativeBridgeAvailable || initialNative !== null);
''',
    ),
    (
        "snapshot status starts from the processor",
        '''  const [snapshotStatus, setSnapshotStatus] = useAppS({ A: false, B: false });
''',
        '''  const [snapshotStatus, setSnapshotStatus] = useAppS(() => initialNative
    ? { A: !!initialNative.state.snapshots.A, B: !!initialNative.state.snapshots.B }
    : { A: false, B: false });
''',
    ),
    (
        "pattern library starts from the processor",
        '''  const [userPatterns, setUserPatterns] = useAppS(() => nativeBridgeAvailable ? [] : window.Spectr.loadStore());
  const [defaultId, setDefaultId] = useAppS(() => nativeBridgeAvailable ? "factory:flat" : window.Spectr.loadDefaultId());
''',
        '''  const [userPatterns, setUserPatterns] = useAppS(() => nativeBridgeAvailable
    ? (initialNative && initialNative.library ? initialNative.library.patterns : [])
    : window.Spectr.loadStore());
  const [defaultId, setDefaultId] = useAppS(() => nativeBridgeAvailable
    ? (initialNative && initialNative.library ? initialNative.library.defaultId : "factory:flat")
    : window.Spectr.loadDefaultId());
''',
    ),
    (
        "the bank receives the first render's state",
        '''nativeHydrated, onNativeState: acceptNativeState, onEditModeChange:''',
        '''nativeHydrated, initialNativeState: initialNative ? initialNative.state : null, onNativeState: acceptNativeState, onEditModeChange:''',
    ),
    # ── FilterBank ─────────────────────────────────────────────────────────
    (
        "bank takes the initial state",
        '''onEditModeChange, nativeHydrated, onNativeState }) {''',
        '''onEditModeChange, nativeHydrated, onNativeState, initialNativeState }) {''',
    ),
    (
        "static canvas mounts inside the mount",
        '''  useEffect(() => { setStaticMounted(true); }, []);
''',
        '''  // A layout effect: still created after every first-render node, but
  // flushed inside the mount instead of as a commit of its own.
  (React.useLayoutEffect || useEffect)(() => { setStaticMounted(true); }, []);
''',
    ),
    (
        "gains and paint refs start from the processor",
        '''  const [gains, setGains] = useState(() => new Array(N).fill(0));
  const targetGainsRef = useRef(new Array(N).fill(0));
  const renderGainsRef = useRef(new Array(N).fill(0));
  const mutedGainDbRef = useRef(new Array(N).fill(0));
''',
        '''  // The processor's state at the first render (App read it synchronously),
  // or null. Read by initialisers only; later states arrive through
  // hydrateProcessingState and applyHostAutomationState as before.
  const seedRef = useRef(void 0);
  if (seedRef.current === void 0)
    seedRef.current = initialNativeState && initialNativeState.n === N
      ? initialNativeState : null;
  const seed = seedRef.current;
  const [gains, setGains] = useState(() => seed
    ? seed.gains.slice(0, N) : new Array(N).fill(0));
  const targetGainsRef = useRef(null);
  if (targetGainsRef.current === null)
    targetGainsRef.current = seed ? seed.gains.slice(0, N) : new Array(N).fill(0);
  const renderGainsRef = useRef(null);
  if (renderGainsRef.current === null)
    renderGainsRef.current = seed
      ? seed.gains.map((value, index) => seed.muted[index]
          ? -Infinity : clamp(value, -1.02, 1.02)).slice(0, N)
      : new Array(N).fill(0);
  const mutedGainDbRef = useRef(null);
  if (mutedGainDbRef.current === null)
    mutedGainDbRef.current = seed ? seed.gainDb.slice(0, N) : new Array(N).fill(0);
''',
    ),
    (
        "the mount does not publish the state it was given",
        '''  const nativeProjectionRef = useRef(false);
''',
        '''  // Armed when the first render was hydrated, exactly as
  // hydrateProcessingState arms it: the publication effect's first run
  // consumes it instead of echoing the processor's own state back.
  const nativeProjectionRef = useRef(!!seed);
''',
    ),
    (
        "applied revision starts at the processor's",
        '''  const nativeAppliedRevisionRef = useRef(0);
''',
        '''  const nativeAppliedRevisionRef = useRef(seed && Number.isSafeInteger(seed.revision)
    && seed.revision >= 0 ? seed.revision : 0);
''',
    ),
    (
        "history availability starts from the processor",
        '''  const [historyAvailability, setHistoryAvailability] = useState({ canUndo: false, canRedo: false });
''',
        '''  const [historyAvailability, setHistoryAvailability] = useState(() => seed
    ? { canUndo: seed.canUndo === true, canRedo: seed.canRedo === true }
    : { canUndo: false, canRedo: false });
''',
    ),
    (
        "macros start from the processor",
        '''  const setMacroState = (macros) => {
    macroStateRef.current = Array.isArray(macros) ? macros : null;
    recomputeMacroOffsets(macroStateRef.current);
  };
''',
        '''  const setMacroState = (macros) => {
    macroStateRef.current = Array.isArray(macros) ? macros : null;
    recomputeMacroOffsets(macroStateRef.current);
  };
  const macroSeededRef = useRef(false);
  if (!macroSeededRef.current) {
    macroSeededRef.current = true;
    if (seed) setMacroState(seed.macros);
  }
''',
    ),
    (
        "viewport starts from the processor",
        '''  const initialView = { lmin: Math.log10(20), lmax: Math.log10(2e4) };
''',
        '''  const initialView = seed
    ? { lmin: Math.log10(seed.minHz), lmax: Math.log10(seed.maxHz) }
    : { lmin: Math.log10(20), lmax: Math.log10(2e4) };
''',
    ),
    (
        "snapshots start from the processor",
        '''  const [snapshots, setSnapshots] = useState({ A: null, B: null });
  const snapshotsRef = useRef({ A: null, B: null });
''',
        '''  const [snapshots, setSnapshots] = useState(() => seed
    ? seed.snapshots : { A: null, B: null });
  const snapshotsRef = useRef(seed ? seed.snapshots : { A: null, B: null });
''',
    ),
    (
        "an empty selection stays the same selection",
        '''    setSelection(/* @__PURE__ */ new Set());
  }, [N]);
''',
        '''    // Keep an already-empty selection: a new empty Set is a new state, and
    // on the mount that was a re-render with nothing to show for it.
    setSelection((current) => current.size === 0 ? current : /* @__PURE__ */ new Set());
  }, [N]);
''',
    ),
    (
        "latency controls listen from the mount's layout pass",
        '''  React.useEffect(function () {
    const listeners = store.listeners || (store.listeners = []);
''',
        '''  // A layout effect: the App hands the processor's latency over in the
  // mount's layout pass, and a passive subscription would arrive after it
  // and miss it. (A test rig without layout effects falls back.)
  (React.useLayoutEffect || React.useEffect)(function () {
    const listeners = store.listeners || (store.listeners = []);
''',
        2,
    ),
    # ── Freeze, morph, modulation, build info, tracing badge ───────────────
    (
        "freeze store starts from the processor",
        '''    const accept = (message) => spectrFreezeAccept(message && message.payload);
    window.pulp.on("processing_state_hydrate", accept);
''',
        '''    const accept = (message) => spectrFreezeAccept(message && message.payload);
    window.pulp.on("processing_state_hydrate", accept);
    if (typeof window.pulp.initial === "function")
      spectrFreezeAccept(window.pulp.initial("processing_state_get"));
''',
    ),
    (
        "morph starts at the processor's",
        '''  const [v, setV] = useStateChrome(0);
  const [hovered, setHovered] = useStateChrome(false);
  const dragRef = React.useRef(null);
  const publishedRef = React.useRef(0);
''',
        '''  const [v, setV] = useStateChrome(() => {
    const bridge = typeof window !== "undefined" ? window.pulp : null;
    const body = bridge && typeof bridge.initial === "function"
      ? bridge.initial("processing_state_get") : null;
    const t = body ? Number(body.morph) : NaN;
    return Number.isFinite(t) ? t : 0;
  });
  const [hovered, setHovered] = useStateChrome(false);
  const dragRef = React.useRef(null);
  const publishedRef = React.useRef(v);
''',
    ),
    (
        "modulation hook starts from the processor",
        '''  const [ready, setReady] = React.useState(() => globalThis.__spectrModulationLast != null);
''',
        '''  const initialBridge = typeof window !== "undefined" ? window.pulp : null;
  if (globalThis.__spectrModulationLast == null && initialBridge
      && typeof initialBridge.initial === "function") {
    const body = initialBridge.initial("processing_state_get");
    const seeded = spectrModulationFromNative(body && body.modulation);
    if (seeded) globalThis.__spectrModulationLast = { ...seeded };
  }
  const [ready, setReady] = React.useState(() => globalThis.__spectrModulationLast != null);
''',
    ),
    (
        "build info is read inside the mount",
        '''  React.useEffect(() => {
    mountedRef.current = true;
    let live = true;
''',
        '''  // A layout effect, so the synchronous read below lands inside the mount:
  // the rows are still created after the first render, but not as a commit
  // of their own. (A test rig without layout effects falls back.)
  (React.useLayoutEffect || React.useEffect)(() => {
    mountedRef.current = true;
    let live = true;
''',
    ),
    (
        "build info answers from the synchronous read when it has one",
        '''    Promise.resolve(window.pulp.postMessage("build_info_get", {}, requestId("get"))).then(unwrap).then((body) => {
''',
        '''    // The request then resolves to the same object, which React ignores;
    // a runtime without the synchronous read keeps asking as before.
    const initialInfo = typeof window.pulp.initial === "function"
      ? window.pulp.initial("build_info_get") : null;
    const knownInfo = !!(initialInfo && initialInfo.ok === true
      && initialInfo.product_version && initialInfo.sdk_version);
    if (knownInfo) setInfo(initialInfo);
    (knownInfo ? Promise.resolve(initialInfo)
      : Promise.resolve(window.pulp.postMessage("build_info_get", {}, requestId("get"))).then(unwrap)).then((body) => {
''',
    ),
    (
        "tracing badge shows from the mount",
        '''  React.useEffect(() => {
    globalThis.__spectrShowTracingBadge = () => setShown(true);
''',
        '''  // Shown inside the mount when build info says this build traces, so the
  // native show call below finds it shown and commits nothing.
  (React.useLayoutEffect || React.useEffect)(() => {
    const bridge = typeof window !== "undefined" ? window.pulp : null;
    const info = bridge && typeof bridge.initial === "function"
      ? bridge.initial("build_info_get") : null;
    if (info && info.tracing === true) setShown(true);
    globalThis.__spectrShowTracingBadge = () => setShown(true);
''',
    ),
]


def encode(text):
    return json.dumps(text, ensure_ascii=False)[1:-1]


def main():
    raw = PATH.read_text(encoding="utf-8")
    if encode(MARKER) in raw:
        print("hydrate before first render already applied")
        return 0
    for edit in EDITS:
        name, old, new = edit[:3]
        expected = edit[3] if len(edit) > 3 else 1
        count = raw.count(encode(old))
        if count != expected:
            sys.exit("FAIL: %s anchor occurs %d times, expected %d"
                     % (name, count, expected))
        raw = raw.replace(encode(old), encode(new))
    json.loads(raw)
    PATH.write_text(raw, encoding="utf-8")
    print("hydrate before first render applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
