// Spectr's renderer-neutral native host bridge for the materialized editor.
// The captured application keeps its browser-facing `window.pulp` contract;
// this adapter forwards commands to the processor-owned C++ EditorBridge and
// delivers native publications back to the same listeners without a WebView.
(() => {
  const listeners = new Map();
  let analyzerFrame = null;
  // The imported editor populates this optional surface during tests. Keeping
  // the object present from the first application render lets lifecycle tests
  // inspect the same initial commit that users see, rather than attaching a
  // probe after React effects have already run.
  globalThis.__spectrTestHooks = globalThis.__spectrTestHooks || {};
  globalThis.__spectrNativeDispatchTrace = [];
  if (typeof window !== 'undefined')
    window.__spectrTestHooks = globalThis.__spectrTestHooks;
  const parseNativeState = payload => {
    const n = payload && Number(payload.n_visible);
    const gainDb = payload && payload.gain_db;
    const muted = payload && payload.muted;
    const minHz = payload && Number(payload.min_hz);
    const maxHz = payload && Number(payload.max_hz);
    if (![32, 40, 48, 56, 64].includes(n)
        || !Array.isArray(gainDb) || gainDb.length !== n
        || !Array.isArray(muted) || muted.length !== n
        || !gainDb.every(Number.isFinite)
        || !muted.every(value => typeof value === 'boolean')
        || !Number.isFinite(minHz) || !Number.isFinite(maxHz)
        || minHz <= 0 || maxHz <= minHz) return null;
    const parseSnapshot = projection => {
      if (!projection || projection.populated !== true) return null;
      if (!Array.isArray(projection.gain_db)
          || projection.gain_db.length !== n
          || !projection.gain_db.every(Number.isFinite)
          || !Array.isArray(projection.muted)
          || projection.muted.length !== n
          || !projection.muted.every(value => typeof value === 'boolean'))
        return undefined;
      return {
        gainDb: projection.gain_db.slice(),
        muted: projection.muted.slice(),
        values: projection.gain_db.map((db, index) => projection.muted[index]
          ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
      };
    };
    const projections = payload.snapshots || {};
    const A = parseSnapshot(projections.A);
    const B = parseSnapshot(projections.B);
    if (A === undefined || B === undefined) return null;
    return {
      n, gainDb: gainDb.slice(), muted: muted.slice(),
      gains: gainDb.map((db, index) => muted[index]
        ? -Infinity : Math.max(-1, Math.min(1, db / 24))),
      minHz, maxHz, snapshots: { A, B },
      revision: Number(payload.revision) || 0,
      patternsJson: typeof payload.patterns_json === 'string'
        ? payload.patterns_json : null,
    };
  };
  const parseNativePatterns = patternsJson => {
    try {
      const envelope = JSON.parse(patternsJson);
      if (!envelope || envelope.format !== 'spectr.patterns'
          || !Array.isArray(envelope.patterns)) return null;
      const patterns = [];
      for (const pattern of envelope.patterns) {
        if (!pattern || typeof pattern.id !== 'string'
            || typeof pattern.name !== 'string'
            || !Array.isArray(pattern.gain_db)
            || !Array.isArray(pattern.muted)
            || pattern.gain_db.length !== pattern.muted.length
            || !pattern.gain_db.every(Number.isFinite)
            || !pattern.muted.every(value => typeof value === 'boolean'))
          return null;
        patterns.push({
          id: pattern.id, name: pattern.name, source: 'user', version: 1,
          createdAt: pattern.created_at || '', updatedAt: pattern.updated_at || '',
          tags: Array.isArray(pattern.tags) ? pattern.tags.slice() : [],
          gains: pattern.gain_db.map((db, index) => pattern.muted[index]
            ? null : Math.max(-1, Math.min(1, db / 24))),
        });
      }
      return {
        patterns,
        defaultId: typeof envelope.default_id === 'string'
          ? envelope.default_id : 'factory:flat',
      };
    } catch { return null; }
  };
  const nativeState = { parse: parseNativeState };
  const nativePatterns = { parse: parseNativePatterns };
  globalThis.SpectrNativeState = nativeState;
  globalThis.SpectrNativePatterns = nativePatterns;
  if (typeof window !== 'undefined') {
    window.SpectrNativeState = nativeState;
    window.SpectrNativePatterns = nativePatterns;
  }
  const validTrace = (trace, expectedLength) => trace
    && Number.isFinite(trace.min_hz) && trace.min_hz > 0
    && Number.isFinite(trace.max_hz) && trace.max_hz > trace.min_hz
    && Array.isArray(trace.magnitude_db)
    && trace.magnitude_db.length === expectedLength
    && trace.magnitude_db.every(Number.isFinite);
  const acceptAnalyzerFrame = payload => {
    if (!payload || payload.schema_version !== 1
        || !Number.isSafeInteger(payload.epoch) || payload.epoch < 0
        || !Number.isSafeInteger(payload.sequence_number)
        || payload.sequence_number < 0
        || !Number.isFinite(payload.floor_db)
        || !Number.isFinite(payload.ceiling_db)
        || payload.ceiling_db <= payload.floor_db
        || !validTrace(payload.visible, 321)
        || !validTrace(payload.overview, 121)) return false;
    if (analyzerFrame
        && (payload.epoch < analyzerFrame.epoch
            || (payload.epoch === analyzerFrame.epoch
                && payload.sequence_number <= analyzerFrame.sequence_number)))
      return false;
    analyzerFrame = {
      ...payload,
      visible: { ...payload.visible,
        magnitude_db: payload.visible.magnitude_db.slice() },
      overview: { ...payload.overview,
        magnitude_db: payload.overview.magnitude_db.slice() },
    };
    return true;
  };
  const sampleTrace = (trace, logFrequency, frame) => {
    if (!trace || !Number.isFinite(logFrequency)) return 0;
    const frequency = Math.pow(10, logFrequency);
    const position = Math.max(0, Math.min(1,
      (Math.log10(frequency) - Math.log10(trace.min_hz))
      / (Math.log10(trace.max_hz) - Math.log10(trace.min_hz))));
    const exact = position * (trace.magnitude_db.length - 1);
    const left = Math.floor(exact);
    const right = Math.min(left + 1, trace.magnitude_db.length - 1);
    const mix = exact - left;
    const db = trace.magnitude_db[left]
      + (trace.magnitude_db[right] - trace.magnitude_db[left]) * mix;
    return Math.max(0, Math.min(1,
      (db - frame.floor_db) / (frame.ceiling_db - frame.floor_db)));
  };
  const normalizeAnalyzerDb = (db, floor, ceiling) => {
    if (!Number.isFinite(db) || !Number.isFinite(floor)
        || !Number.isFinite(ceiling) || ceiling <= floor) return 0;
    return Math.max(0, Math.min(1, (db - floor) / (ceiling - floor)));
  };
  const projectAnalyzerAmount = (amount, zeroY, halfH) => {
    if (!Number.isFinite(amount) || !Number.isFinite(zeroY)
        || !Number.isFinite(halfH) || halfH < 0) return zeroY;
    return zeroY - Math.max(0, Math.min(1, amount)) * halfH * 0.95;
  };
  const analyzer = {
    native: true,
    sample(logFrequency, _time, traceName = 'visible') {
      if (!analyzerFrame) return 0;
      return sampleTrace(traceName === 'overview'
        ? analyzerFrame.overview : analyzerFrame.visible,
        logFrequency, analyzerFrame);
    },
    scale() { return analyzerFrame
      ? { floor: analyzerFrame.floor_db, ceiling: analyzerFrame.ceiling_db }
      : { floor: -120, ceiling: 24 }; },
    normalizeDb(db) {
      const scale = this.scale();
      return normalizeAnalyzerDb(db, scale.floor, scale.ceiling);
    },
    project(amount, zeroY, halfH) {
      return projectAnalyzerAmount(amount, zeroY, halfH);
    },
    debugSnapshot() { return analyzerFrame; },
  };
  globalThis.SpectrAnalyzer = analyzer;
  if (typeof window !== 'undefined') window.SpectrAnalyzer = analyzer;

  const fallback = (type) => {
    const n = 32;
    if (type === 'processing_state_get') {
      return { ok: true, payload: {
        ok: true, revision: 0, n_visible: n,
        gain_db: Array(n).fill(0), muted: Array(n).fill(false),
        min_hz: 20, max_hz: 20000,
        snapshots: { A: { populated: false }, B: { populated: false } },
        patterns_json: '{"schema":"spectr.patterns.v1","version":1,"patterns":[]}',
      } };
    }
    if (type === 'spectral_resolution_request') {
      return { ok: true, payload: {
        ok: true, represented_bands: 32, active_bands: 32,
        fully_represented: true, fft_size: 8192, sample_rate: 48000,
        min_hz: 20, max_hz: 20000,
      } };
    }
    return { ok: true, payload: { ok: true } };
  };

  const emit = (type, payload, id = '') => {
    if (type === 'analyzer_frame' && !acceptAnalyzerFrame(payload)) return;
    const message = { type, payload, id };
    const callbacks = listeners.get(type);
    if (!callbacks) return;
    for (const callback of [...callbacks]) callback(message);
  };

  const dispatch = (type, payload, id) => {
    globalThis.__spectrNativeDispatchTrace.push({ type, payload, id });
    if (typeof globalThis.__spectrEditorDispatch !== 'function') {
      // `pulp-screenshot` intentionally has no processor. Keep its visual
      // import oracle deterministic; packaged plugins always install the
      // C++ dispatcher before this runtime is loaded.
      return Promise.resolve(fallback(type));
    }
    let response;
    try {
      response = JSON.parse(globalThis.__spectrEditorDispatch(JSON.stringify({
        type, payload: payload || {}, id: id || '',
      })));
    } catch (error) {
      return Promise.reject(error);
    }
    // ScriptedUiSession deliberately installs a rejecting dispatcher in its
    // validation realm. That realm must remain mutation-free, but it still
    // needs deterministic state to settle the imported React application.
    if (response && response.ok === false
        && String(response.error || '').includes('unavailable during validation')) {
      return Promise.resolve(fallback(type));
    }
    return Promise.resolve({ ok: response && response.ok === true, payload: response });
  };

  const postMessage = (type, payload = {}, id = '') => {
    if (type === 'editor_ready') {
      const ready = Promise.all([
        dispatch('processing_state_get', {}, 'spectr-native-state').then(result => {
          if (!result.ok) throw new Error(result.payload?.error || 'state unavailable');
          requestAnimationFrame(() => emit(
            'processing_state_hydrate', result.payload,
            'spectr-processing-state-hydrate'));
        }),
        dispatch('spectral_resolution_request', {}, 'spectr-native-resolution').then(result => {
          if (!result.ok) throw new Error(result.payload?.error || 'resolution unavailable');
          requestAnimationFrame(() => emit(
            'spectral_resolution', result.payload,
            'spectr-spectral-resolution'));
        }),
      ]);
      return ready.then(() => ({ ok: true, payload: { ok: true } }));
    }
    if (type === 'spectral_resolution_request') {
      return dispatch(type, payload, id).then(result => {
        if (result.ok) emit('spectral_resolution', result.payload, 'spectr-spectral-resolution');
        return result;
      });
    }
    return dispatch(type, payload, id);
  };

  const pulp = {
    on(type, callback) {
      if (typeof callback !== 'function') return () => {};
      let callbacks = listeners.get(type);
      if (!callbacks) listeners.set(type, callbacks = new Set());
      callbacks.add(callback);
      return () => callbacks.delete(callback);
    },
    postMessage,
  };

  globalThis.__spectrPublishNativeMessage = emit;
  globalThis.pulp = pulp;
  if (typeof window !== 'undefined') window.pulp = pulp;
})();
