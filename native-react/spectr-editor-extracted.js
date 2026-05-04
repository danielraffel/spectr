
// ===== inner script 0 =====

// Pattern system — data model, factory presets, store, serialization, remapping.
//
// A "pattern" captures ONLY band gains. Patterns are portable, predictable, and
// composable. They do not encode viewport, selection, motion mode, DSP mode, or
// band count. On apply, gains are remapped proportionally if bandCount differs.

const PATTERN_SCHEMA_VERSION = 1;

// ---------- gain synthesis for factory presets (band-count-agnostic) ----------
// Each generator returns an Array<Number> of length N where value is in [-1,+1]
// or -Infinity for mute.

function genFlat(N) { return new Array(N).fill(0); }

function genHarmonic(N) {
  // Map N bands across log(20)..log(20000); mark the nearest band for each
  // harmonic of a fundamental at ~110 Hz, up to 12 harmonics.
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

function genAlternate(N) {
  return Array.from({ length: N }, (_, i) => (i % 2 === 0 ? 0.6 : -Infinity));
}

function genComb(N) {
  return Array.from({ length: N }, (_, i) => (i % 3 === 0 ? 0.4 : -0.6));
}

function genVocal(N) {
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

function genSubOnly(N) {
  const lmin = Math.log10(20), lmax = Math.log10(20000);
  return Array.from({ length: N }, (_, i) => {
    const pos = (i + 0.5) / N;
    const lf = lmin + pos * (lmax - lmin);
    const f = Math.pow(10, lf);
    return f < 160 ? 0.5 : -Infinity;
  });
}

function genTilt(N) {
  // +12 dB at 20 Hz linearly down to -12 dB at 20 kHz (in log space)
  return Array.from({ length: N }, (_, i) => 0.5 - (i / (N - 1)) * 1.0);
}

function genAirLift(N) {
  // neutral below 4 kHz, gentle boost above
  const lmin = Math.log10(20), lmax = Math.log10(20000);
  return Array.from({ length: N }, (_, i) => {
    const pos = (i + 0.5) / N;
    const lf = lmin + pos * (lmax - lmin);
    const f = Math.pow(10, lf);
    if (f < 4000) return 0;
    return Math.min(0.7, Math.log2(f / 4000) * 0.3);
  });
}

// ---------- factory list ----------
const FACTORY_PATTERNS = [
  { id: 'factory:flat', name: 'FLAT', source: 'factory', gen: genFlat, tags: ['baseline'] },
  { id: 'factory:harmonic', name: 'HARMONIC SERIES', source: 'factory', gen: genHarmonic, tags: ['musical'] },
  { id: 'factory:alternate', name: 'ALTERNATING', source: 'factory', gen: genAlternate, tags: ['structural'] },
  { id: 'factory:comb', name: 'COMB', source: 'factory', gen: genComb, tags: ['structural'] },
  { id: 'factory:vocal', name: 'VOCAL FORMANTS', source: 'factory', gen: genVocal, tags: ['tonal'] },
  { id: 'factory:sub', name: 'SUB ONLY (< 160 Hz)', source: 'factory', gen: genSubOnly, tags: ['tonal'] },
  { id: 'factory:tilt', name: 'DOWNWARD TILT', source: 'factory', gen: genTilt, tags: ['baseline'] },
  { id: 'factory:air', name: 'AIR LIFT (4k+)', source: 'factory', gen: genAirLift, tags: ['tonal'] },
];

// Resolve a factory preset to concrete gains for N bands.
function factoryGains(id, N) {
  const p = FACTORY_PATTERNS.find(p => p.id === id);
  if (!p) return null;
  return p.gen(N);
}

// ---------- remapping ----------
// Remap an array of gains of length M to a new length N, proportionally.
// Uses nearest-neighbor on the logical index axis — simple, predictable, and
// preserves mutes as "the closest source band was muted".
function remapGains(src, N) {
  const M = src.length;
  if (M === N) return src.slice();
  const out = new Array(N);
  for (let i = 0; i < N; i++) {
    // center of destination band i
    const pos = (i + 0.5) / N;
    const j = Math.min(M - 1, Math.max(0, Math.floor(pos * M)));
    out[i] = src[j];
  }
  return out;
}

// ---------- serialization ----------
// A portable pattern (for JSON I/O) uses a fixed-length canonical gains array
// at RESOLUTION=128, so user patterns stay meaningful across band counts.
const CANONICAL_RES = 128;

function toCanonical(gains) {
  return remapGains(gains, CANONICAL_RES).map(v => (v === -Infinity ? null : round3(v)));
}
function fromCanonical(arr) {
  return arr.map(v => (v === null ? -Infinity : v));
}
function round3(v) { return Math.round(v * 1000) / 1000; }

function makeUserPattern(name, gains) {
  const now = new Date().toISOString();
  return {
    id: 'user:' + Math.random().toString(36).slice(2, 10) + Date.now().toString(36),
    name: name || 'Untitled',
    source: 'user',
    version: PATTERN_SCHEMA_VERSION,
    createdAt: now,
    updatedAt: now,
    tags: [],
    gains: toCanonical(gains), // canonical 128-long array
  };
}

// Export: an envelope wrapping one or more patterns.
function exportEnvelope(patterns) {
  return {
    format: 'spectr.patterns',
    version: PATTERN_SCHEMA_VERSION,
    exportedAt: new Date().toISOString(),
    count: patterns.length,
    patterns: patterns.map(p => ({
      id: p.id,
      name: p.name,
      source: 'user',
      version: p.version || PATTERN_SCHEMA_VERSION,
      createdAt: p.createdAt,
      updatedAt: p.updatedAt,
      tags: p.tags || [],
      gains: p.gains, // already canonical
    })),
  };
}

// Validate + normalize an imported envelope. Returns { patterns, errors }.
function parseEnvelope(obj) {
  const errors = [];
  if (!obj || typeof obj !== 'object') {
    return { patterns: [], errors: ['not a JSON object'] };
  }
  let list;
  if (obj.format === 'spectr.patterns' && Array.isArray(obj.patterns)) list = obj.patterns;
  else if (Array.isArray(obj)) list = obj;
  else if (obj.gains) list = [obj];
  else return { patterns: [], errors: ['unrecognized structure'] };

  const out = [];
  for (const p of list) {
    if (!p || !Array.isArray(p.gains)) {
      errors.push(`skipped: missing gains (${p?.name || '?'})`);
      continue;
    }
    // Gains may be 128, or a legacy length — remap to canonical
    let gains = p.gains.map(v => (v === null ? -Infinity : Number(v)));
    if (gains.length !== CANONICAL_RES) {
      gains = remapGains(gains, CANONICAL_RES);
    }
    out.push({
      id: 'user:' + Math.random().toString(36).slice(2, 10) + Date.now().toString(36),
      name: (p.name || 'Imported').slice(0, 64),
      source: 'user',
      version: PATTERN_SCHEMA_VERSION,
      createdAt: p.createdAt || new Date().toISOString(),
      updatedAt: new Date().toISOString(),
      tags: Array.isArray(p.tags) ? p.tags : [],
      gains: gains.map(v => (v === -Infinity ? null : round3(v))),
    });
  }
  return { patterns: out, errors };
}

// ---------- store (localStorage) ----------
const LS_KEY = 'spectr.patterns.v1';
const LS_DEFAULT_KEY = 'spectr.defaultPatternId.v1';

function loadStore() {
  try {
    const raw = localStorage.getItem(LS_KEY);
    if (!raw) return [];
    const arr = JSON.parse(raw);
    if (!Array.isArray(arr)) return [];
    return arr;
  } catch { return []; }
}
function saveStore(list) {
  try { localStorage.setItem(LS_KEY, JSON.stringify(list)); } catch {}
}
function loadDefaultId() {
  try { return localStorage.getItem(LS_DEFAULT_KEY) || 'factory:flat'; } catch { return 'factory:flat'; }
}
function saveDefaultId(id) {
  try { localStorage.setItem(LS_DEFAULT_KEY, id); } catch {}
}

// Resolve any pattern (factory or user) to gains for N bands.
function resolveGains(pattern, N) {
  if (!pattern) return genFlat(N);
  if (pattern.source === 'factory') {
    // factory patterns resolve via their generator directly for max fidelity
    return factoryGains(pattern.id, N);
  }
  // user: canonical gains → remap
  const src = fromCanonical(pattern.gains);
  return remapGains(src, N);
}

// Public API
window.Spectr = window.Spectr || {};
Object.assign(window.Spectr, {
  FACTORY_PATTERNS,
  CANONICAL_RES,
  PATTERN_SCHEMA_VERSION,
  makeUserPattern,
  resolveGains,
  remapGains,
  exportEnvelope,
  parseEnvelope,
  loadStore,
  saveStore,
  loadDefaultId,
  saveDefaultId,
  factoryGains,
  toCanonical,
  fromCanonical,
});



// ===== inner script 1 =====

// Simulated audio signal generator.
// Produces a smooth, always-animating log-frequency spectrum in [0,1].
// Multiple overlapping "voices" (bass, mids, vocal formants, percussive transients)
// so the visuals have character without needing real audio.

(function(){
  const FMIN = 20;
  const FMAX = 20000;

  // Utility: gaussian bump in log-freq space
  function bump(logF, center, width, amp) {
    const d = (logF - center) / width;
    return amp * Math.exp(-d * d);
  }

  // A voice = set of time-varying bumps.
  // t is seconds.
  function bassVoice(logF, t) {
    const beat = 0.5 + 0.5 * Math.sin(t * Math.PI * 2 * 1.6); // ~96 bpm
    const kick = Math.max(0, 1 - ((t * 1.6) % 1) * 4); // decaying thump
    return bump(logF, Math.log10(70), 0.18, 0.85 * (0.35 + 0.65 * beat))
         + bump(logF, Math.log10(55), 0.12, 0.95 * kick)
         + bump(logF, Math.log10(110), 0.15, 0.55 * beat);
  }
  function midVoice(logF, t) {
    // slowly sweeping pad
    const sweep = Math.log10(300 + 400 * (0.5 + 0.5 * Math.sin(t * 0.25)));
    return bump(logF, sweep, 0.22, 0.55)
         + bump(logF, sweep + 0.3, 0.18, 0.40)
         + bump(logF, sweep + 0.6, 0.14, 0.28);
  }
  function vocalVoice(logF, t) {
    // formants move a little
    const wob = 0.04 * Math.sin(t * 1.3);
    return bump(logF, Math.log10(500) + wob, 0.10, 0.70)
         + bump(logF, Math.log10(1500) + wob * 1.4, 0.12, 0.60)
         + bump(logF, Math.log10(2800) + wob * 0.8, 0.14, 0.50);
  }
  function airVoice(logF, t) {
    const shimmer = 0.5 + 0.5 * Math.sin(t * 2.2 + logF * 6);
    return bump(logF, Math.log10(8000), 0.35, 0.45 * shimmer)
         + bump(logF, Math.log10(14000), 0.30, 0.30 * shimmer);
  }
  function hiss(logF, t, seed) {
    // pseudo-random per-bin noise that evolves
    const v = Math.sin(logF * 137.9 + t * 3.1 + seed * 11.7) * 0.5 + 0.5;
    const v2 = Math.sin(logF * 71.3 + t * 5.7 + seed * 3.4) * 0.5 + 0.5;
    return v * v2 * 0.35;
  }

  // Public: sample the spectrum at a log10-frequency value.
  // Returns 0..1.
  window.SpectrSignal = {
    sample(logF, t, scenarioMix = 1) {
      const base =
        bassVoice(logF, t) +
        midVoice(logF, t) +
        vocalVoice(logF, t) * 0.9 +
        airVoice(logF, t) +
        hiss(logF, t, 0.7) * 0.6;
      // High-shelf rolloff above 16kHz, low rolloff below 30Hz
      const lf = Math.pow(10, logF);
      let shape = 1;
      if (lf < 40) shape *= Math.max(0, (lf - 20) / 20);
      if (lf > 14000) shape *= Math.max(0, 1 - (lf - 14000) / 8000);
      // Transient burst every ~2s
      const burstPhase = (t % 2.0) / 2.0;
      const burst = burstPhase < 0.08 ? (1 - burstPhase / 0.08) * 0.8 : 0;
      const burstSpec = burst * (0.3 + 0.7 * Math.exp(-Math.pow((logF - 3.3) / 0.5, 2)));
      const v = (base + burstSpec) * shape * scenarioMix;
      return Math.max(0, Math.min(1, v * 0.55));
    },
    // Peak-and-hold helper for nicer visuals
    makePeakHold(n, decay = 0.92) {
      const peaks = new Float32Array(n);
      return function(arr) {
        for (let i = 0; i < n; i++) {
          peaks[i] = Math.max(arr[i], peaks[i] * decay);
        }
        return peaks;
      };
    },
  };

  window.SpectrFreq = {
    FMIN, FMAX,
    logMin: Math.log10(FMIN),
    logMax: Math.log10(FMAX),
    // Normalized [0,1] pos -> frequency given log min/max
    posToFreq(pos, lmin, lmax) {
      return Math.pow(10, lmin + (lmax - lmin) * pos);
    },
    freqToPos(f, lmin, lmax) {
      return (Math.log10(f) - lmin) / (lmax - lmin);
    },
    fmt(f) {
      if (f >= 1000) return (f / 1000).toFixed(f >= 10000 ? 1 : 2).replace(/\.?0+$/, '') + 'k';
      return f.toFixed(f < 100 ? 1 : 0);
    },
  };
})();



// ===== inner script 2 =====

// The FilterBank: a canvas-rendered zoomable band grid.
// Handles all drawing + pointer interaction for:
//  - volumetric light columns
//  - ghosted spectrum behind bands
//  - densifying grid + rulers + mini-map + edge-wall glow
//  - negative-space cutout mute
//  - drag gain, scroll zoom, drag pan, click mute, drag-paint curve
//  - multi-select + group move
//  - pattern stamps
//  - live/precision motion modes
//  - snapshot morph (owned by parent; we just render the blended gains)

const { useRef, useEffect, useState, useCallback, useMemo } = React;

// ---------- helpers ----------
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const lerp = (a, b, t) => a + (b - a) * t;
const smooth = (a, b, t) => a + (b - a) * (1 - Math.exp(-t));

// Gain model: value in [-1, 1] where
//  +1 => +24 dB, 0 => 0 dB, -1 => -24 dB.
// A special value of -Infinity represents mute (−∞).
function isMuted(g) { return g === -Infinity; }
function gainDb(g) { return isMuted(g) ? -Infinity : g * 24; }

// Map a gain [-1..+1] to a vertical Y position on the canvas.
// zeroY = where 0 dB sits, h = usable half-height.
function gainToY(g, zeroY, halfH) {
  if (isMuted(g)) return zeroY + halfH; // bottom
  return zeroY - g * halfH;
}

// Color for the spectral gradient position [0..1] across visible freq range.
// Theme-aware: mono (monochrome blue-white), spectral (full rainbow),
// cool (blues/teals), warm (amber/rose).
function specColor(pos, alpha = 1, theme = 'spectral') {
  let hue, sat = 80, light = 62;
  if (theme === 'mono') {
    hue = 210; sat = 10 + pos * 30; light = 55 + pos * 25;
  } else if (theme === 'cool') {
    hue = 260 - pos * 100; sat = 75;
  } else if (theme === 'warm') {
    hue = 50 - pos * 60; sat = 80; light = 58 + pos * 8;
  } else if (theme === 'neon') {
    // hot magenta → cyan, hypersaturated
    hue = 300 - pos * 120; sat = 98; light = 58 + Math.sin(pos * Math.PI) * 8;
  } else if (theme === 'dusk') {
    // deep violet → ember amber, dusky
    hue = 280 - pos * 250; sat = 55 + pos * 20; light = 50 + pos * 15;
  } else if (theme === 'forest') {
    // moss → lime → gold, vegetal
    hue = 140 - pos * 90; sat = 55 + pos * 25; light = 48 + pos * 18;
  } else if (theme === 'ember') {
    // coal → red → orange → pale yellow (heat gradient)
    hue = 10 + pos * 50; sat = 85; light = 35 + pos * 45;
  } else if (theme === 'phosphor') {
    // CRT phosphor green — narrow hue band, high luminance variation
    hue = 115 + pos * 25; sat = 85; light = 45 + pos * 28;
  } else if (theme === 'plasma') {
    // dark purple → pink → orange → yellow (matplotlib plasma)
    hue = 280 - pos * 240; sat = 85 - pos * 20; light = 35 + pos * 40;
  } else if (theme === 'ice') {
    // white → cyan → deep blue, glacial
    hue = 200 + pos * 20; sat = 45 + pos * 45; light = 88 - pos * 40;
  } else if (theme === 'rose') {
    // pale pink → magenta → deep wine
    hue = 340 + pos * 20; sat = 55 + pos * 25; light = 78 - pos * 30;
  } else if (theme === 'solar') {
    // deep red → orange → yellow → white-hot (black-body radiation)
    hue = 0 + pos * 55; sat = 95 - pos * 30; light = 42 + pos * 48;
  } else if (theme === 'oceanic') {
    // teal → deep blue → indigo, marine
    hue = 180 + pos * 80; sat = 65; light = 62 - pos * 20;
  } else if (theme === 'sodium') {
    // amber monochrome (sodium-vapor street lamp)
    hue = 38; sat = 80 + pos * 15; light = 45 + pos * 30;
  } else {
    // spectral — full rainbow
    hue = 240 - pos * 300;
  }
  return `hsla(${hue}, ${sat}%, ${light}%, ${alpha})`;
}

// Theme registry: used by the Settings picker to render swatches + labels.
// `preview` is a short gradient used as the swatch background.
const THEMES = [
  { k: 'spectral', label: 'Spectral', desc: 'Full rainbow — low → high',
    stops: ['hsl(240,80%,62%)', 'hsl(180,80%,62%)', 'hsl(120,80%,62%)', 'hsl(60,80%,62%)', 'hsl(0,80%,62%)'] },
  { k: 'mono',     label: 'Mono',     desc: 'Monochrome blue-grey',
    stops: ['hsl(210,10%,55%)', 'hsl(210,20%,65%)', 'hsl(210,30%,75%)', 'hsl(210,40%,80%)'] },
  { k: 'cool',     label: 'Cool',     desc: 'Violet → cyan',
    stops: ['hsl(260,75%,62%)', 'hsl(220,75%,62%)', 'hsl(180,75%,62%)', 'hsl(160,75%,62%)'] },
  { k: 'warm',     label: 'Warm',     desc: 'Amber → rose',
    stops: ['hsl(50,80%,58%)', 'hsl(30,80%,60%)', 'hsl(10,80%,62%)', 'hsl(350,80%,64%)'] },
  { k: 'neon',     label: 'Neon',     desc: 'Hyperchroma magenta → cyan',
    stops: ['hsl(300,98%,62%)', 'hsl(260,98%,62%)', 'hsl(220,98%,64%)', 'hsl(180,98%,62%)'] },
  { k: 'dusk',     label: 'Dusk',     desc: 'Violet → ember amber',
    stops: ['hsl(280,55%,50%)', 'hsl(220,60%,55%)', 'hsl(60,70%,60%)', 'hsl(30,75%,62%)'] },
  { k: 'forest',   label: 'Forest',   desc: 'Moss → lime → gold',
    stops: ['hsl(140,55%,48%)', 'hsl(100,70%,55%)', 'hsl(70,80%,60%)', 'hsl(50,85%,64%)'] },
  { k: 'ember',    label: 'Ember',    desc: 'Coal → flame → white hot',
    stops: ['hsl(10,85%,38%)', 'hsl(25,85%,55%)', 'hsl(45,85%,70%)', 'hsl(55,85%,82%)'] },
  { k: 'phosphor', label: 'Phosphor', desc: 'CRT green monochrome',
    stops: ['hsl(115,85%,48%)', 'hsl(125,85%,62%)', 'hsl(135,85%,72%)', 'hsl(140,85%,78%)'] },
  { k: 'plasma',   label: 'Plasma',   desc: 'Purple → pink → gold',
    stops: ['hsl(280,85%,38%)', 'hsl(320,80%,52%)', 'hsl(20,75%,62%)', 'hsl(50,65%,72%)'] },
  { k: 'ice',      label: 'Ice',      desc: 'Frost → glacial blue',
    stops: ['hsl(200,45%,88%)', 'hsl(210,70%,70%)', 'hsl(220,85%,55%)', 'hsl(220,90%,45%)'] },
  { k: 'rose',     label: 'Rose',     desc: 'Blush → magenta → wine',
    stops: ['hsl(345,55%,78%)', 'hsl(355,70%,62%)', 'hsl(0,75%,48%)'] },
  { k: 'solar',    label: 'Solar',    desc: 'Black-body: red → white',
    stops: ['hsl(0,95%,42%)', 'hsl(25,85%,55%)', 'hsl(45,75%,68%)', 'hsl(55,65%,85%)'] },
  { k: 'oceanic',  label: 'Oceanic',  desc: 'Teal → deep indigo',
    stops: ['hsl(180,65%,62%)', 'hsl(210,65%,55%)', 'hsl(240,65%,48%)', 'hsl(260,65%,42%)'] },
  { k: 'sodium',   label: 'Sodium',   desc: 'Amber vapor lamp',
    stops: ['hsl(38,85%,48%)', 'hsl(38,90%,60%)', 'hsl(38,92%,72%)'] },
];
if (typeof window !== 'undefined') window.SpectrThemes = THEMES;

// Metaphor registry — visual shape language for each band.
const METAPHORS = [
  { k: 'columns', label: 'Columns', desc: 'Sharp rectangular bars with razor edges',
    draw: (c, x, y, w, h) => { c.fillRect(x, y, w, h); c.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1); } },
  { k: 'liquid',  label: 'Liquid',  desc: 'Soft rounded-top columns',
    draw: (c, x, y, w, h) => {
      const r = Math.min(w/2, 3);
      c.beginPath();
      c.moveTo(x, y + h);
      c.lineTo(x, y + r);
      c.quadraticCurveTo(x, y, x + r, y);
      c.lineTo(x + w - r, y);
      c.quadraticCurveTo(x + w, y, x + w, y + r);
      c.lineTo(x + w, y + h);
      c.closePath(); c.fill();
    } },
  { k: 'shards',  label: 'Shards',  desc: 'Angular triangle caps, crystalline',
    draw: (c, x, y, w, h) => {
      c.beginPath();
      c.moveTo(x, y + h);
      c.lineTo(x + w/2, y);
      c.lineTo(x + w, y + h);
      c.closePath(); c.fill();
    } },
  { k: 'needle',  label: 'Needle',  desc: 'Thin line with cap dot — oscilloscope',
    draw: (c, x, y, w, h) => {
      c.strokeStyle = c.fillStyle;
      c.lineWidth = 1.5;
      c.beginPath(); c.moveTo(x + w/2, y + h); c.lineTo(x + w/2, y); c.stroke();
      c.beginPath(); c.arc(x + w/2, y, 1.8, 0, Math.PI * 2); c.fill();
    } },
  { k: 'brick',   label: 'Brick',   desc: 'Stacked LED segments',
    draw: (c, x, y, w, h) => {
      const step = 3, gap = 1;
      const n = Math.max(1, Math.floor(h / (step + gap)));
      for (let k = 0; k < n; k++) {
        c.fillRect(x, y + h - (k + 1) * (step + gap), w, step);
      }
    } },
  { k: 'candle',  label: 'Candle',  desc: 'Thin body with wick at tip',
    draw: (c, x, y, w, h) => {
      const bw = Math.max(2, w * 0.55);
      c.fillRect(x + (w - bw) / 2, y, bw, h);
      c.strokeStyle = c.fillStyle; c.lineWidth = 1;
      c.beginPath(); c.moveTo(x + w/2, y); c.lineTo(x + w/2, y - 2); c.stroke();
    } },
  { k: 'tape',    label: 'Tape',    desc: 'Thick stroke at tip, hairline below',
    draw: (c, x, y, w, h) => {
      c.fillRect(x, y - 1, w, 2.5);
      c.strokeStyle = c.fillStyle; c.globalAlpha *= 0.3;
      c.lineWidth = 1;
      c.beginPath(); c.moveTo(x + w/2, y + 1); c.lineTo(x + w/2, y + h); c.stroke();
      c.globalAlpha /= 0.3;
    } },
  { k: 'crystal', label: 'Crystal', desc: 'Hexagonal facets, gem-like',
    draw: (c, x, y, w, h) => {
      const cap = Math.min(3, h * 0.3);
      c.beginPath();
      c.moveTo(x + w/2, y);
      c.lineTo(x + w, y + cap);
      c.lineTo(x + w, y + h - cap);
      c.lineTo(x + w/2, y + h);
      c.lineTo(x, y + h - cap);
      c.lineTo(x, y + cap);
      c.closePath(); c.fill();
    } },
];
if (typeof window !== 'undefined') window.SpectrMetaphors = METAPHORS;

// Module-level stable style references — JSX `style={{...}}` literals
// would create a new object every render, which the dom-adapter sees as
// a prop change and re-applies via setFlex/setPosition/etc. on every
// pointer move. That re-application clobbers the canvas paint surface
// (resets to the View widget's default white background), causing the
// rainbow spectrum to vanish on hover. Hoisting the literal out keeps
// the object identity stable across renders so the dom-adapter's prop
// diff stays a no-op.
const CANVAS_FILL_STYLE = { position: 'absolute', inset: 0 };

function FilterBank({ settings, onStateChange, sharedState, onStatus, dspMode, editMode, analyzerMode, onEditModeChange }) {
  const canvasRef = useRef(null);
  const overlayRef = useRef(null);
  const wrapRef = useRef(null);

  // ---- bank state ----
  const { bandCount, metaphor, bloom, spectrumIntensity, muteStyle, motionMode, showMinimap, showRulers, theme } = settings;
  const N = bandCount;

  // Edit mode + analyzer mode refs (avoid re-binding pointer handlers on each change)
  const editModeRef = useRef(editMode || 'sculpt');
  useEffect(() => { editModeRef.current = editMode || 'sculpt'; }, [editMode]);
  const analyzerModeRef = useRef(analyzerMode || 'peak');
  useEffect(() => { analyzerModeRef.current = analyzerMode || 'peak'; }, [analyzerMode]);
  const dspModeRef = useRef(dspMode || 'iir');
  useEffect(() => { dspModeRef.current = dspMode || 'iir'; }, [dspMode]);

  // Rolling average spectrum (for ANALYZER 'avg' / 'both')
  const avgSpectrumRef = useRef(null);

  // Gain per band. Default 0 dB.
  const [gains, setGains] = useState(() => new Array(N).fill(0));
  // Target gains, used for smoothing (precision mode interpolates slower).
  const targetGainsRef = useRef(new Array(N).fill(0));
  const renderGainsRef = useRef(new Array(N).fill(0));
  // Selection set (Set<number>)
  const [selection, setSelection] = useState(() => new Set());
  // Viewport: logFreq min/max visible
  const [view, _setView] = useState({ lmin: Math.log10(20), lmax: Math.log10(20000) });
  // Drag handlers can compute NaN view bounds when getGeom returns
  // inner.w=0 during transient layout (divide-by-zero in span/dx
  // arithmetic). NaN in view propagates to view.lmin/lmax → SpectrSignal
  // sample receives NaN log-frequency → returns NaN → drawSpectrum's
  // lineTo Y is NaN → drawBands' geometry is NaN → canvas paint corrupts.
  // Wrap setView so NaN/Inf bounds never reach React state.
  const FULL_LMIN = Math.log10(20), FULL_LMAX = Math.log10(20000);
  const setView = (next) => {
    const lmin = Number.isFinite(next?.lmin) ? next.lmin : FULL_LMIN;
    const lmax = Number.isFinite(next?.lmax) ? next.lmax : FULL_LMAX;
    if (lmin >= lmax) { _setView({ lmin: FULL_LMIN, lmax: FULL_LMAX }); return; }
    _setView({ lmin, lmax });
  };
  // Snapshots A/B + morph
  const [snapshots, setSnapshots] = useState({ A: null, B: null });
  const snapshotsRef = useRef({ A: null, B: null });
  const [morph, setMorph] = useState(0); // 0 => A, 1 => B, -1 => inactive

  // Resize band arrays when N changes
  useEffect(() => {
    setGains(prev => {
      if (prev.length === N) return prev;
      const next = new Array(N).fill(0);
      // Map old → new by proportional index
      for (let i = 0; i < N; i++) {
        const j = Math.floor(i / N * prev.length);
        next[i] = prev[j] ?? 0;
      }
      targetGainsRef.current = next.slice();
      renderGainsRef.current = next.slice();
      return next;
    });
    setSelection(new Set());
  }, [N]);

  // Keep target in sync if an external change comes in
  useEffect(() => {
    targetGainsRef.current = gains.slice();
  }, [gains]);

  // ---- hover readout ----
  const [hover, setHover] = useState(null); // { band, freq, db, x, y }
  const [ctxMenu, setCtxMenu] = useState(null); // { x, y, band }
  const lastTapRef = useRef(null); // { band, t } — for double-tap-to-mute detection

  // ---- edge hit glow ----
  const edgeGlowRef = useRef({ left: 0, right: 0, top: 0, bottom: 0 });

  // ---- pointer state ref ----
  const pointerRef = useRef({
    mode: null, // 'gain' | 'pan' | 'paint' | 'marquee' | null
    startX: 0, startY: 0,
    lastX: 0, lastY: 0,
    band: -1,
    startGain: 0,
    groupStart: null, // Map<number, number> gains snapshot
    paintedBands: new Set(),
    viewStart: null,
    marqueeEnd: null,
  });
  const [marquee, setMarquee] = useState(null); // {x1,y1,x2,y2}

  // ---- draw loop ----
  const rafRef = useRef(0);
  const timeRef = useRef(0);
  const peaksRef = useRef(new Float32Array(512));

  useEffect(() => {
    let last = performance.now();
    const draw = (now) => {
      let dt = Math.min(0.05, (now - last) / 1000);
      last = now;
      // Guard against NaN/Infinity dt — propagates to timeRef →
      // SpectrSignal samples → entire drawSpectrum path goes NaN.
      if (!Number.isFinite(dt)) dt = 0;
      timeRef.current += dt;
      if (!Number.isFinite(timeRef.current)) timeRef.current = 0;
      // smooth gains toward target
      const tg = targetGainsRef.current;
      const rg = renderGainsRef.current;
      const k = motionMode === 'precision' ? 6 : 22;
      for (let i = 0; i < rg.length; i++) {
        if (isMuted(tg[i])) {
          // animate toward a synthetic "muted Y" via very fast collapse
          rg[i] = smooth(rg[i], -1.02, dt * 26);
          if (rg[i] < -1.01) rg[i] = -Infinity; // latch
        } else {
          if (isMuted(rg[i])) rg[i] = -1.02; // start from bottom
          rg[i] = smooth(rg[i], tg[i], dt * k);
        }
      }
      // decay edge glow
      const eg = edgeGlowRef.current;
      eg.left = Math.max(0, eg.left - dt * 2.5);
      eg.right = Math.max(0, eg.right - dt * 2.5);
      eg.top = Math.max(0, eg.top - dt * 2.5);
      eg.bottom = Math.max(0, eg.bottom - dt * 2.5);

      renderAll();
      rafRef.current = requestAnimationFrame(draw);
    };
    rafRef.current = requestAnimationFrame(draw);
    return () => cancelAnimationFrame(rafRef.current);
    // eslint-disable-next-line
  }, [N, bloom, spectrumIntensity, muteStyle, motionMode, metaphor, showMinimap, showRulers, theme, view]);

  // ---- handle resize ----
  useEffect(() => {
    const resize = () => {
      const wrap = wrapRef.current;
      const c = canvasRef.current;
      const o = overlayRef.current;
      if (!wrap || !c) return;
      const r = wrap.getBoundingClientRect();
      const dpr = Math.min(2, window.devicePixelRatio || 1);
      for (const cv of [c, o]) {
        if (!cv) continue;
        cv.width = Math.floor(r.width * dpr);
        cv.height = Math.floor(r.height * dpr);
        cv.style.width = r.width + 'px';
        cv.style.height = r.height + 'px';
        const ctx = cv.getContext('2d');
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      }
    };
    resize();
    window.addEventListener('resize', resize);
    return () => window.removeEventListener('resize', resize);
  }, []);

  // ---- geometry helpers ----
  const getGeom = useCallback(() => {
    const wrap = wrapRef.current;
    if (!wrap) return null;
    const r = wrap.getBoundingClientRect();
    const pad = { l: 56, r: 56, t: 56, b: 88 };
    const w = r.width, h = r.height;
    const inner = { x: pad.l, y: pad.t, w: w - pad.l - pad.r, h: h - pad.t - pad.b };
    const zeroY = inner.y + inner.h * 0.55;
    const halfH = Math.min(zeroY - inner.y, inner.y + inner.h - zeroY);
    const bandGap = 2;
    const bandW = (inner.w - bandGap * (N - 1)) / N;
    return { w, h, pad, inner, zeroY, halfH, bandW, bandGap };
  }, [N]);

  const bandCenterX = (i, g) => g.inner.x + i * (g.bandW + g.bandGap) + g.bandW / 2;
  const bandLeftX = (i, g) => g.inner.x + i * (g.bandW + g.bandGap);

  // Band i corresponds to normalized [i/N, (i+1)/N] across the viewport.
  const bandFreqRange = (i) => {
    const a = view.lmin + (i / N) * (view.lmax - view.lmin);
    const b = view.lmin + ((i + 1) / N) * (view.lmax - view.lmin);
    return [Math.pow(10, a), Math.pow(10, b)];
  };
  const bandCenterFreq = (i) => {
    const a = view.lmin + ((i + 0.5) / N) * (view.lmax - view.lmin);
    return Math.pow(10, a);
  };

  // ---- render ----
  const renderAll = useCallback(() => {
    const g = getGeom();
    if (!g) return;
    const ctx = canvasRef.current.getContext('2d');
    const octx = overlayRef.current.getContext('2d');
    const { w, h, pad, inner, zeroY, halfH, bandW, bandGap } = g;

    // --- background ---
    // pulp #1322 — Spectr's original bg gradient has stops at alpha 0.0
    // → 0.35. In a browser, the alpha-0 region composites with the
    // parent <div>'s dark theme color (web's layered rendering model).
    // In pulp's single-buffer CG/Skia model, the alpha-0 region zeroes
    // pixels which expose the underlying NSWindow buffer (WHITE on
    // macOS by default), producing the "white filterbank" symptom.
    // Fix: paint a solid dark fillRect BEFORE the gradient so the
    // gradient blends over a known dark backdrop instead of relying
    // on the parent layer to provide one. The visible result matches
    // the WebView reference: dark filterbank with subtle gradient
    // shading near the bottom.
    // pulp #1322 — paint solid dark backdrop BEFORE the gradient.
    // Spectr's gradient stops at alpha 0.0 → 0.35 are designed to
    // composite over a dark <div>'s CSS background in browsers.
    // Pulp's CG/Skia single-buffer renderer doesn't have a separate
    // compositing layer for the canvas widget, so an alpha-0 region
    // exposes the underlying NSWindow buffer (white on macOS).
    // Workaround: paint solid dark over an extra-wide region first
    // (the (-2k, -2k) → (3k, 3k) rect ensures full canvas-widget
    // coverage regardless of any internal coordinate offset/clip).
    ctx.clearRect(0, 0, w, h);
    // very subtle horizontal scanline gradient bg
    const bg = ctx.createLinearGradient(0, 0, 0, h);
    bg.addColorStop(0, 'rgba(8,12,18,0.0)');
    bg.addColorStop(1, 'rgba(0,0,0,0.35)');
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, w, h);

    // --- grid densify ---
    drawGrid(ctx, g);

    // --- ghosted spectrum behind bands ---
    drawSpectrum(ctx, g);

    // --- rulers ---
    if (showRulers) drawRulers(ctx, g);

    // --- bands ---
    drawBands(ctx, g);

    // --- marquee + selection + overlays ---
    octx.clearRect(0, 0, w, h);
    drawSelection(octx, g);
    drawMarquee(octx, g);
    drawEdgeWalls(octx, g);

    // --- hover readout ---
    drawHover(octx, g);

    // --- minimap ---
    if (showMinimap) drawMinimap(octx, g);

    // --- corner HUD ---
    drawHUD(octx, g);
  }, [view, N, bloom, spectrumIntensity, muteStyle, motionMode, metaphor, showMinimap, showRulers, theme, hover, marquee, selection, snapshots, morph, dspMode]);

  function drawGrid(ctx, g) {
    const { inner } = g;
    ctx.save();
    ctx.beginPath();
    ctx.rect(inner.x, inner.y, inner.w, inner.h);
    ctx.clip();

    const span = view.lmax - view.lmin;
    ctx.lineWidth = 1;

    // We want roughly uniform grid density regardless of zoom. Pick a "nice"
    // multiplier step in log-space so gridlines are visually even, not log-crammed.
    //
    // span  → step (in log10)    target visual gridlines
    //  >3.0 → 1.0 (decades)                ~3-4
    //  ~2.0 → 0.5                          ~4
    //  ~1.0 → 0.25                         ~4
    //  ~0.5 → 0.1                          ~5
    //  ~0.2 → 0.05                         ~4-5
    //  ~0.1 → 0.02                         ~5
    // We pick the step so span/step is roughly 5-10.
    const targetMajor = 6;
    const rawStep = span / targetMajor;
    // Snap to nice values: 1, 0.5, 0.25, 0.1, 0.05, 0.025, 0.01, …
    const niceSteps = [1, 0.5, 0.25, 0.1, 0.05, 0.025, 0.01, 0.005, 0.0025, 0.001];
    let majorStep = niceSteps[0];
    for (const s of niceSteps) { if (s <= rawStep * 1.25) { majorStep = s; break; } majorStep = s; }
    const minorStep = majorStep / 5;

    const lineAtLog = (lf) => inner.x + ((lf - view.lmin) / span) * inner.w + 0.5;

    // Minor lines (faint)
    ctx.strokeStyle = 'rgba(255,255,255,0.035)';
    const firstMinor = Math.ceil(view.lmin / minorStep) * minorStep;
    for (let lf = firstMinor; lf <= view.lmax + 1e-9; lf += minorStep) {
      const x = lineAtLog(lf);
      ctx.beginPath(); ctx.moveTo(x, inner.y); ctx.lineTo(x, inner.y + inner.h); ctx.stroke();
    }

    // Major lines (stronger) — these are the ones users perceive as "the grid"
    ctx.strokeStyle = 'rgba(255,255,255,0.10)';
    const firstMajor = Math.ceil(view.lmin / majorStep) * majorStep;
    for (let lf = firstMajor; lf <= view.lmax + 1e-9; lf += majorStep) {
      const x = lineAtLog(lf);
      ctx.beginPath(); ctx.moveTo(x, inner.y); ctx.lineTo(x, inner.y + inner.h); ctx.stroke();
    }

    // dB horizontal lines
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    for (let db = -24; db <= 24; db += 6) {
      const y = g.zeroY - (db / 24) * g.halfH + 0.5;
      if (y < g.inner.y || y > g.inner.y + g.inner.h) continue;
      ctx.beginPath(); ctx.moveTo(g.inner.x, y); ctx.lineTo(g.inner.x + g.inner.w, y); ctx.stroke();
    }
    // 0 dB reference
    ctx.strokeStyle = 'rgba(255,255,255,0.20)';
    ctx.setLineDash([4, 4]);
    ctx.beginPath(); ctx.moveTo(g.inner.x, g.zeroY + 0.5); ctx.lineTo(g.inner.x + g.inner.w, g.zeroY + 0.5); ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();
  }

  function drawSpectrum(ctx, g) {
    if (spectrumIntensity <= 0.001) return;
    const { inner, zeroY, halfH } = g;
    const t = timeRef.current;
    const steps = Math.min(320, Math.max(160, Math.floor(inner.w / 3)));
    const span = view.lmax - view.lmin;
    // sample instantaneous
    const arr = new Float32Array(steps + 1);
    for (let i = 0; i <= steps; i++) {
      const lf = view.lmin + (i / steps) * span;
      arr[i] = window.SpectrSignal.sample(lf, t);
    }
    // peak hold
    if (peaksRef.current.length !== steps + 1) peaksRef.current = new Float32Array(steps + 1);
    const peaks = peaksRef.current;
    for (let i = 0; i < peaks.length; i++) peaks[i] = Math.max(arr[i], peaks[i] * 0.88);
    // rolling average (heavy smoothing — "averaged energy" view)
    if (!avgSpectrumRef.current || avgSpectrumRef.current.length !== steps + 1) {
      avgSpectrumRef.current = new Float32Array(steps + 1);
    }
    const avg = avgSpectrumRef.current;
    const aK = 0.035; // slow EMA
    for (let i = 0; i < avg.length; i++) avg[i] = avg[i] + (arr[i] - avg[i]) * aK;

    const mode = analyzerModeRef.current; // 'peak' | 'avg' | 'both' | 'off'
    if (mode === 'off') return;

    ctx.save();
    ctx.beginPath();
    ctx.rect(inner.x, inner.y, inner.w, inner.h);
    ctx.clip();

    // ---- Fill under instantaneous curve with spectral gradient (subtle, under everything) ----
    if (mode === 'peak' || mode === 'both') {
      const grad = ctx.createLinearGradient(inner.x, 0, inner.x + inner.w, 0);
      // pulp #1371 — CoreGraphicsCanvas::set_blend_mode is a silent no-op
      // in CPU mode, so 'lighter' (additive) doesn't produce the vivid
      // additive blend WebView shows. Bump base alpha 0.18 → 0.50 so the
      // SrcOver fallback still shows visible color. Drop 'lighter' to
      // avoid the no-op call. After #1371 lands, restore alpha 0.18 +
      // 'lighter' for proper additive blending.
      grad.addColorStop(0.00, `hsla(240, 80%, 55%, ${0.50 * spectrumIntensity})`);
      grad.addColorStop(0.25, `hsla(200, 85%, 55%, ${0.50 * spectrumIntensity})`);
      grad.addColorStop(0.50, `hsla(150, 85%, 55%, ${0.50 * spectrumIntensity})`);
      grad.addColorStop(0.75, `hsla( 60, 90%, 60%, ${0.50 * spectrumIntensity})`);
      grad.addColorStop(1.00, `hsla(  0, 85%, 60%, ${0.50 * spectrumIntensity})`);

      ctx.globalCompositeOperation = 'source-over';
      ctx.beginPath();
      ctx.moveTo(inner.x, zeroY);
      for (let i = 0; i <= steps; i++) {
        const x = inner.x + (i / steps) * inner.w;
        const y = zeroY - arr[i] * halfH * 0.95;
        ctx.lineTo(x, y);
      }
      ctx.lineTo(inner.x + inner.w, zeroY);
      ctx.closePath();
      ctx.fillStyle = grad;
      ctx.fill();
    }

    ctx.globalCompositeOperation = 'source-over';

    // ---- Average (blue) overlay — smooth curve, dimmer, broader stroke ----
    if (mode === 'avg' || mode === 'both') {
      ctx.strokeStyle = `rgba(120,180,240,${0.55 * spectrumIntensity + 0.15})`;
      ctx.lineWidth = 1.6;
      ctx.beginPath();
      for (let i = 0; i <= steps; i++) {
        const x = inner.x + (i / steps) * inner.w;
        const y = zeroY - avg[i] * halfH * 0.95;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    // ---- Peak (green) outline ----
    if (mode === 'peak' || mode === 'both') {
      ctx.strokeStyle = `rgba(140,230,170,${0.50 * spectrumIntensity + 0.15})`;
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let i = 0; i <= steps; i++) {
        const x = inner.x + (i / steps) * inner.w;
        const y = zeroY - peaks[i] * halfH * 0.95;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }

    ctx.restore();
  }

  function drawRulers(ctx, g) {
    const { inner } = g;
    ctx.save();
    const span = view.lmax - view.lmin;
    ctx.font = '10px JetBrains Mono, monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    // Dynamic labels: pick step based on span
    const labels = [];
    const pushAt = (f, major) => {
      const lf = Math.log10(f);
      if (lf < view.lmin || lf > view.lmax) return;
      const pos = (lf - view.lmin) / span;
      labels.push({ x: inner.x + pos * inner.w, f, major });
    };
    if (span < 0.8) {
      // zoomed in — sub-decade
      for (let dec = Math.floor(view.lmin); dec <= Math.ceil(view.lmax); dec++) {
        for (let m = 10; m <= 100; m++) {
          pushAt((m / 10) * Math.pow(10, dec), m % 10 === 0);
        }
      }
    } else if (span < 1.6) {
      for (let dec = Math.floor(view.lmin); dec <= Math.ceil(view.lmax); dec++) {
        for (let m = 1; m <= 10; m++) pushAt(m * Math.pow(10, dec), m === 1);
      }
    } else {
      for (let dec = Math.floor(view.lmin); dec <= Math.ceil(view.lmax); dec++) {
        for (const m of [1, 2, 5]) pushAt(m * Math.pow(10, dec), m === 1);
      }
    }
    // Top ruler
    const rulerY = inner.y - 22;
    ctx.strokeStyle = 'rgba(255,255,255,0.18)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(inner.x, rulerY + 14); ctx.lineTo(inner.x + inner.w, rulerY + 14);
    ctx.stroke();
    for (const L of labels) {
      ctx.strokeStyle = L.major ? 'rgba(255,255,255,0.35)' : 'rgba(255,255,255,0.12)';
      ctx.beginPath();
      ctx.moveTo(L.x + 0.5, rulerY + 8);
      ctx.lineTo(L.x + 0.5, rulerY + 14);
      ctx.stroke();
      if (L.major) {
        ctx.fillStyle = 'rgba(255,255,255,0.55)';
        ctx.fillText(window.SpectrFreq.fmt(L.f) + 'Hz', L.x, rulerY - 2);
      }
    }
    // dB ruler (left)
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let db = -24; db <= 24; db += 6) {
      const y = g.zeroY - (db / 24) * g.halfH;
      ctx.fillStyle = db === 0 ? 'rgba(255,255,255,0.65)' : 'rgba(255,255,255,0.30)';
      ctx.fillText((db > 0 ? '+' : '') + db, inner.x - 8, y);
    }
    ctx.fillStyle = 'rgba(255,255,255,0.30)';
    ctx.fillText('dB', inner.x - 8, g.inner.y - 8);
    // bottom -∞
    ctx.fillStyle = 'rgba(255,255,255,0.5)';
    ctx.fillText('−∞', inner.x - 8, g.zeroY + g.halfH + 10);
    ctx.restore();
  }

  function drawBands(ctx, g) {
    const { inner, zeroY, halfH, bandW, bandGap } = g;
    const tg = targetGainsRef.current;
    const rg = renderGainsRef.current;
    const effectiveGains = rg;

    ctx.save();
    // Clip bands area (slightly taller so we can render the "cutout" below zero line)
    ctx.beginPath();
    ctx.rect(inner.x - 4, inner.y - 4, inner.w + 8, inner.h + 8);
    ctx.clip();

    // Pass 1: compact muted-band marker — sits AT the 0dB line, not floor-to-ceiling.
    // Looks like a small speaker-off chip where the band would be.
    if (muteStyle === 'cutout') {
      for (let i = 0; i < N; i++) {
        const x = bandLeftX(i, g);
        if (!isMuted(tg[i])) continue;

        // Compact chip height — ~22px tall, centered on zeroY
        const chipH = Math.min(26, Math.max(18, inner.h * 0.12));
        const chipY = zeroY - chipH / 2;
        const chipX = x + 0.5;
        const chipW = bandW - 1;

        // Faint full-band track behind (very subtle) — hint that this slot exists
        ctx.fillStyle = 'rgba(255,255,255,0.025)';
        ctx.fillRect(x, inner.y, bandW, inner.h);

        // Tiny zero-line dashes above and below chip (where signal would be)
        ctx.strokeStyle = 'rgba(255,255,255,0.12)';
        ctx.setLineDash([2, 3]);
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(x + bandW / 2, inner.y + 10);
        ctx.lineTo(x + bandW / 2, chipY - 2);
        ctx.moveTo(x + bandW / 2, chipY + chipH + 2);
        ctx.lineTo(x + bandW / 2, inner.y + inner.h - 10);
        ctx.stroke();
        ctx.setLineDash([]);

        // Chip background
        ctx.fillStyle = 'rgba(18,22,28,0.92)';
        roundRect(ctx, chipX, chipY, chipW, chipH, 3);
        ctx.fill();
        ctx.strokeStyle = 'rgba(255,255,255,0.28)';
        ctx.lineWidth = 1;
        ctx.stroke();

        // Speaker-off icon centered in chip
        const gx = x + bandW / 2;
        const gy = zeroY;
        ctx.save();
        ctx.strokeStyle = 'rgba(255,255,255,0.78)';
        ctx.fillStyle = 'rgba(255,255,255,0.78)';
        ctx.lineWidth = 1.1;
        ctx.lineCap = 'round';
        // speaker box
        ctx.beginPath();
        ctx.moveTo(gx - 3, gy - 1.5);
        ctx.lineTo(gx - 1, gy - 1.5);
        ctx.lineTo(gx + 1.5, gy - 4);
        ctx.lineTo(gx + 1.5, gy + 4);
        ctx.lineTo(gx - 1, gy + 1.5);
        ctx.lineTo(gx - 3, gy + 1.5);
        ctx.closePath();
        ctx.fill();
        // X mark to the right
        ctx.beginPath();
        ctx.moveTo(gx + 3.5, gy - 2.2);
        ctx.lineTo(gx + 6, gy + 2.2);
        ctx.moveTo(gx + 6, gy - 2.2);
        ctx.lineTo(gx + 3.5, gy + 2.2);
        ctx.stroke();
        ctx.restore();

        // "MUTE" label never shown here (chip too small) — skip
        if (false && bandW > 11) {
          ctx.save();
          ctx.fillStyle = 'rgba(255,255,255,0.55)';
          ctx.font = '7.5px JetBrains Mono, monospace';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'bottom';
          const label = bandW > 18 ? 'MUTE' : 'M';
          ctx.fillText(label, x + bandW / 2, inner.y + inner.h - 4);
          ctx.restore();
        }
      }
    }

    // Signal-gated energy at each band (for cap emission).
    // Sample the spectrum at each band center and peak-hold per-band.
    if (!window.__spectrBandEnergy || window.__spectrBandEnergy.length !== N) {
      window.__spectrBandEnergy = new Float32Array(N);
    }
    const energy = window.__spectrBandEnergy;
    const tNow = timeRef.current;
    for (let i = 0; i < N; i++) {
      const lf = view.lmin + ((i + 0.5) / N) * (view.lmax - view.lmin);
      const e = window.SpectrSignal.sample(lf, tNow);
      // peak hold with decay (temporal coherence)
      energy[i] = Math.max(e, energy[i] * 0.90);
    }

    // ---- BAND RENDER — new three-layer pipeline ----
    // Layer A: crisp band body (no bloom)
    // Layer B: cap emission (signal-gated, narrow, localized)
    // Layer C: selection / edges / filament

    // Pre-compute per-band geometry
    const geom = new Array(N);
    for (let i = 0; i < N; i++) {
      const gval = effectiveGains[i];
      const targetMuted = isMuted(tg[i]);
      const edge = (i === 0 || i === N - 1);
      const cx = bandCenterX(i, g);
      const topY = zeroY - Math.max(gval, 0) * halfH;
      const botY = zeroY - Math.min(gval, 0) * halfH;
      const pos = (i + 0.5) / N;
      const innerW = bandW * (edge ? 0.72 : 0.78);
      geom[i] = { gval, targetMuted, edge, cx, topY, botY, pos, innerW,
        isBoosted: gval > 0.02,
        isReduced: gval < -0.02 && !isMuted(gval),
        isSel: selection.has(i),
      };
    }

    // --- Layer A: crisp band bodies ---
    ctx.globalCompositeOperation = 'source-over';
    for (let i = 0; i < N; i++) {
      const G = geom[i];
      if (G.targetMuted && effectiveGains[i] <= -1.01) continue;
      // Rest state: subtle body even at 0 dB so it's recognisable as a band.
      // Boosted/reduced ramps alpha up/down.
      let bodyA;
      if (G.isBoosted) bodyA = 0.52 + Math.min(G.gval, 1) * 0.35;
      else if (G.isReduced) bodyA = 0.20 + (1 + G.gval) * 0.25; // more negative = dimmer
      else bodyA = 0.38; // rest body visible
      if (G.targetMuted) bodyA *= Math.max(0, 1 + effectiveGains[i]); // fade during collapse

      const cg = ctx.createLinearGradient(G.cx, G.topY, G.cx, G.botY);
      cg.addColorStop(0, specColor(G.pos, bodyA, theme));
      cg.addColorStop(1, specColor(G.pos, bodyA * 0.35, theme));
      ctx.fillStyle = cg;
      if (metaphor === 'shards') {
        // angular triangle tops on boosted, trapezoid on reduced
        const yTop = Math.min(G.topY, G.botY);
        const yBot = Math.max(G.topY, G.botY);
        const xL = G.cx - G.innerW / 2, xR = G.cx + G.innerW / 2;
        ctx.beginPath();
        if (G.isBoosted) {
          ctx.moveTo(xL, yBot);
          ctx.lineTo(G.cx, yTop - 2);
          ctx.lineTo(xR, yBot);
          ctx.closePath();
        } else {
          ctx.moveTo(xL + 1, yBot);
          ctx.lineTo(xL + 2, yTop);
          ctx.lineTo(xR - 2, yTop);
          ctx.lineTo(xR - 1, yBot);
          ctx.closePath();
        }
        ctx.fill();
      } else if (metaphor === 'liquid') {
        // soft rounded-top columns
        const yTop = Math.min(G.topY, G.botY);
        const yBot = Math.max(G.topY, G.botY);
        const xL = G.cx - G.innerW / 2;
        const r = Math.min(G.innerW / 2, 6);
        ctx.beginPath();
        ctx.moveTo(xL, yBot);
        ctx.lineTo(xL, yTop + r);
        ctx.quadraticCurveTo(xL, yTop, xL + r, yTop);
        ctx.lineTo(xL + G.innerW - r, yTop);
        ctx.quadraticCurveTo(xL + G.innerW, yTop, xL + G.innerW, yTop + r);
        ctx.lineTo(xL + G.innerW, yBot);
        ctx.closePath();
        ctx.fill();
      } else if (metaphor === 'needle') {
        // thin vertical line + cap dot — oscilloscope needle
        const yTop = Math.min(G.topY, G.botY);
        const yBot = Math.max(G.topY, G.botY);
        ctx.save();
        ctx.strokeStyle = specColor(G.pos, Math.min(1, bodyA + 0.3), theme);
        ctx.lineWidth = 1.3;
        ctx.beginPath();
        ctx.moveTo(G.cx, G.zeroY !== undefined ? G.zeroY : (yTop + yBot) / 2);
        // Draw from zero line to the gain tip
        ctx.moveTo(G.cx, geom[i].gval >= 0 ? yBot : yTop);
        ctx.lineTo(G.cx, geom[i].gval >= 0 ? yTop : yBot);
        ctx.stroke();
        // Cap dot at the tip
        ctx.fillStyle = specColor(G.pos, Math.min(1, bodyA + 0.45), theme);
        ctx.beginPath();
        ctx.arc(G.cx, geom[i].gval >= 0 ? yTop : yBot, Math.min(2.5, G.innerW * 0.22), 0, Math.PI * 2);
        ctx.fill();
        ctx.restore();
      } else if (metaphor === 'brick') {
        // stacked horizontal segments — LED-meter style
        const yTop = Math.min(G.topY, G.botY);
        const yBot = Math.max(G.topY, G.botY);
        const span = Math.abs(G.botY - G.topY);
        const stepH = 3.5;
        const gap = 1.2;
        const n = Math.max(1, Math.floor(span / (stepH + gap)));
        const xL = G.cx - G.innerW / 2;
        for (let k = 0; k < n; k++) {
          const yTopSeg = (geom[i].gval >= 0)
            ? yBot - (k + 1) * (stepH + gap) + gap / 2
            : yTop + k * (stepH + gap);
          // Fade alpha for bricks further from zero line
          const aMul = 1 - (k / n) * 0.35;
          ctx.fillStyle = specColor(G.pos, bodyA * aMul, theme);
          ctx.fillRect(xL, yTopSeg, G.innerW, stepH);
        }
      } else if (metaphor === 'candle') {
        // candlestick: thin body with small wick at gain tip
        const yTop = Math.min(G.topY, G.botY);
        const yBot = Math.max(G.topY, G.botY);
        const bodyW = Math.max(2, G.innerW * 0.55);
        const xL = G.cx - bodyW / 2;
        ctx.fillRect(xL, yTop, bodyW, Math.max(1, yBot - yTop));
        // Wick — thin 1px line extending 3px past the tip toward the cap
        ctx.save();
        ctx.strokeStyle = specColor(G.pos, Math.min(1, bodyA + 0.4), theme);
        ctx.lineWidth = 1;
        ctx.beginPath();
        const wickY = geom[i].gval >= 0 ? yTop : yBot;
        ctx.moveTo(G.cx, wickY);
        ctx.lineTo(G.cx, wickY + (geom[i].gval >= 0 ? -3 : 3));
        ctx.stroke();
        ctx.restore();
      } else if (metaphor === 'tape') {
        // thick colored stroke ONLY at the gain tip — no body
        const tipY = geom[i].gval >= 0 ? Math.min(G.topY, G.botY) : Math.max(G.topY, G.botY);
        const thickness = 3;
        ctx.fillStyle = specColor(G.pos, Math.min(1, bodyA + 0.3), theme);
        ctx.fillRect(G.cx - G.innerW / 2, tipY - thickness / 2, G.innerW, thickness);
        // Hairline connector back to zero line (subtle)
        ctx.save();
        ctx.strokeStyle = specColor(G.pos, bodyA * 0.25, theme);
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(G.cx, tipY);
        ctx.lineTo(G.cx, geom[i].gval >= 0 ? Math.max(G.topY, G.botY) : Math.min(G.topY, G.botY));
        ctx.stroke();
        ctx.restore();
      } else if (metaphor === 'crystal') {
        // Hexagonal crystal — angled top and bottom facets
        const yTop = Math.min(G.topY, G.botY);
        const yBot = Math.max(G.topY, G.botY);
        const xL = G.cx - G.innerW / 2, xR = G.cx + G.innerW / 2;
        const cap = Math.min(5, (yBot - yTop) * 0.3);
        ctx.beginPath();
        ctx.moveTo(G.cx, yTop);
        ctx.lineTo(xR, yTop + cap);
        ctx.lineTo(xR, yBot - cap);
        ctx.lineTo(G.cx, yBot);
        ctx.lineTo(xL, yBot - cap);
        ctx.lineTo(xL, yTop + cap);
        ctx.closePath();
        ctx.fill();
      } else {
        ctx.fillRect(G.cx - G.innerW / 2, Math.min(G.topY, G.botY), G.innerW, Math.abs(G.botY - G.topY) + 0.5);
      }

      // Razor edges (sharp) — skip for non-column metaphors (shape itself carries the edge)
      if (metaphor !== 'columns') continue;
      ctx.strokeStyle = specColor(G.pos, Math.min(0.9, bodyA + 0.15), theme);
      ctx.lineWidth = 1;
      ctx.strokeRect(
        Math.round(G.cx - G.innerW / 2) + 0.5,
        Math.round(Math.min(G.topY, G.botY)) + 0.5,
        Math.round(G.innerW) - 1,
        Math.round(Math.abs(G.botY - G.topY))
      );
    }

    // --- Layer B: cap emission (signal-gated, narrow, localized) ---
    // Only at the top cap, only if the band has signal energy there.
    ctx.globalCompositeOperation = 'lighter';
    const bloomAmt = bloom; // 0..1 user setting
    for (let i = 0; i < N; i++) {
      const G = geom[i];
      if (G.targetMuted) continue; // muted kills local emission
      if (effectiveGains[i] <= -1.01) continue;

      const e = energy[i]; // 0..1 signal energy at this band
      // Cap emission ignites only when signal is present.
      // Boosted bands amplify the response; reduced bands suppress it.
      let gainMul;
      if (G.isBoosted) gainMul = 1 + G.gval * 0.8;          // up to 1.8
      else if (G.isReduced) gainMul = Math.max(0.1, 1 + G.gval); // 0.1 at -24dB
      else gainMul = 0.85;

      const activity = e * gainMul;
      if (activity < 0.02) continue;

      // Tight cap radial: ~12px tall, ~1.3× band width
      const glowW = bandW * (1.25 + bloomAmt * 0.35);
      const glowH = 14 + bloomAmt * 10;
      const peakA = Math.min(0.42, 0.12 + activity * (0.18 + bloomAmt * 0.35));

      const rg = ctx.createRadialGradient(G.cx, G.topY, 0, G.cx, G.topY, Math.max(glowW, glowH));
      rg.addColorStop(0, specColor(G.pos, peakA, theme));
      rg.addColorStop(0.4, specColor(G.pos, peakA * 0.5, theme));
      rg.addColorStop(1, specColor(G.pos, 0, theme));
      ctx.fillStyle = rg;
      // Draw ellipse-like via scale
      ctx.save();
      ctx.translate(G.cx, G.topY);
      ctx.scale(glowW / glowH, 1);
      ctx.beginPath();
      ctx.arc(0, 0, glowH, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    }
    ctx.globalCompositeOperation = 'source-over';

    // --- Layer C: filament (top cap) + edges + selection ---
    for (let i = 0; i < N; i++) {
      const G = geom[i];
      if (G.targetMuted && effectiveGains[i] <= -1.01) continue;

      // Sharpest element: the top filament cap
      if (!G.targetMuted && metaphor === 'columns') {
        ctx.fillStyle = specColor(G.pos, 1.0, theme);
        ctx.fillRect(Math.round(G.cx - G.innerW / 2), Math.round(G.topY) - 1, Math.round(G.innerW), 2);
      }

      // Edge band wall treatment
      if (G.edge) {
        ctx.strokeStyle = 'rgba(255,255,255,0.38)';
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 2]);
        ctx.strokeRect(
          Math.round(G.cx - G.innerW / 2) - 1.5,
          Math.round(Math.min(G.topY, G.botY)) - 1.5,
          Math.round(G.innerW) + 3,
          Math.round(Math.abs(G.botY - G.topY)) + 3
        );
        ctx.setLineDash([]);
        ctx.fillStyle = 'rgba(255,255,255,0.55)';
        ctx.font = '9px JetBrains Mono, monospace';
        ctx.textAlign = 'center';
        ctx.fillText(i === 0 ? 'HPF' : 'LPF', G.cx, g.inner.y + g.inner.h + 14);
      }

      // Selection ring (white, not glow)
      if (G.isSel) {
        ctx.strokeStyle = 'rgba(255,255,255,0.85)';
        ctx.lineWidth = 1;
        ctx.strokeRect(
          Math.round(G.cx - G.innerW / 2) - 2.5,
          Math.round(Math.min(G.topY, G.botY)) - 2.5,
          Math.round(G.innerW) + 5,
          Math.round(Math.abs(G.botY - G.topY)) + 5
        );
      }
    }
    ctx.globalCompositeOperation = 'source-over';
    ctx.restore();

    // Curve line on top — the "response curve", shape depends on DSP mode.
    //   IIR    → smooth analog curve through band centers (cardinal spline),
    //            with lateral bleed shown as a soft glow under the curve.
    //   FFT    → hard stair-step between band boundaries (brick-wall),
    //            with a thin vertical tick at each band edge.
    //   HYBRID → blend of the two.
    const dspM = dspModeRef.current || 'iir';
    ctx.save();
    ctx.beginPath();
    ctx.rect(inner.x, inner.y - 4, inner.w, inner.h + 8);
    ctx.clip();

    // Collect visible (non-muted) points
    const pts = [];
    for (let i = 0; i < N; i++) {
      if (isMuted(tg[i])) { pts.push(null); continue; }
      pts.push({
        cx: bandCenterX(i, g),
        xL: bandLeftX(i, g),
        xR: bandLeftX(i, g) + bandW + bandGap,
        y: zeroY - effectiveGains[i] * halfH,
      });
    }

    if (dspM === 'fft') {
      // Hard brick-wall: rectangular stair-step from band-left to band-right.
      ctx.strokeStyle = 'rgba(210,225,245,0.70)';
      ctx.lineWidth = 1.1;
      ctx.beginPath();
      let started = false;
      for (let i = 0; i < N; i++) {
        const p = pts[i];
        if (!p) { started = false; continue; }
        if (!started) { ctx.moveTo(p.xL, p.y); ctx.lineTo(p.xR, p.y); started = true; }
        else {
          // vertical jump from previous y to new y at band boundary
          ctx.lineTo(p.xL, p.y);
          ctx.lineTo(p.xR, p.y);
        }
      }
      ctx.stroke();
      // Tiny band-boundary ticks at 0 dB line to reinforce "discrete bins"
      ctx.strokeStyle = 'rgba(210,225,245,0.18)';
      ctx.lineWidth = 1;
      ctx.setLineDash([1, 3]);
      ctx.beginPath();
      for (let i = 0; i <= N; i++) {
        const x = inner.x + i * (bandW + bandGap) + 0.5;
        ctx.moveTo(x, zeroY - 3); ctx.lineTo(x, zeroY + 3);
      }
      ctx.stroke();
      ctx.setLineDash([]);
    } else {
      // IIR / HYBRID: smooth cardinal spline through band centers.
      // Hybrid uses reduced tension so it's slightly less floppy than pure IIR.
      const tension = dspM === 'hybrid' ? 0.25 : 0.5; // 0 = linear, 0.5 = catmull-rom
      const ys = pts.map(p => p ? p.y : null);

      // Lateral bleed under curve (IIR only — the "analog spread" look)
      if (dspM === 'iir') {
        ctx.fillStyle = 'rgba(180,210,255,0.05)';
        ctx.beginPath();
        let filling = false;
        for (let i = 0; i < N; i++) {
          const p = pts[i];
          if (!p) continue;
          // Sample 3 neighbors each side, gaussian weighted — shows "skirt" under the curve
          if (!filling) { ctx.moveTo(p.cx, zeroY); filling = true; }
          ctx.lineTo(p.cx, p.y);
        }
        if (filling) {
          // Close back along zero line
          for (let i = N - 1; i >= 0; i--) {
            const p = pts[i];
            if (!p) continue;
            ctx.lineTo(p.cx, zeroY);
            break;
          }
          ctx.closePath();
          ctx.fill();
        }
      }

      // Spline curve through centers
      ctx.strokeStyle = dspM === 'hybrid' ? 'rgba(200,220,250,0.55)' : 'rgba(200,230,255,0.55)';
      ctx.lineWidth = dspM === 'hybrid' ? 1.4 : 1.6;
      ctx.beginPath();
      let started = false;
      let prevP = null, prevPrevP = null;
      for (let i = 0; i < N; i++) {
        const p = pts[i];
        if (!p) { started = false; prevP = null; prevPrevP = null; continue; }
        if (!started) { ctx.moveTo(p.cx, p.y); started = true; prevPrevP = p; prevP = p; continue; }
        // Cardinal spline segment from prevP → p using neighbors
        const pNext = (i + 1 < N && pts[i + 1]) ? pts[i + 1] : p;
        const c1x = prevP.cx + (p.cx - prevPrevP.cx) * tension / 3;
        const c1y = prevP.y  + (p.y  - prevPrevP.y)  * tension / 3;
        const c2x = p.cx    - (pNext.cx - prevP.cx) * tension / 3;
        const c2y = p.y     - (pNext.y  - prevP.y)  * tension / 3;
        ctx.bezierCurveTo(c1x, c1y, c2x, c2y, p.cx, p.y);
        prevPrevP = prevP; prevP = p;
      }
      ctx.stroke();

      // Hybrid: overlay faint brick-wall strokes at the band tops to hint at digital nature
      if (dspM === 'hybrid') {
        ctx.strokeStyle = 'rgba(220,235,255,0.22)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (let i = 0; i < N; i++) {
          const p = pts[i];
          if (!p) continue;
          ctx.moveTo(p.xL, p.y);
          ctx.lineTo(p.xR, p.y);
        }
        ctx.stroke();
      }
    }

    // DSP mode label overlay (top-left of plot area, small)
    ctx.font = '9px JetBrains Mono, monospace';
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.fillStyle = 'rgba(200,220,250,0.55)';
    const dspLabel = dspM === 'iir' ? 'IIR · analog' : dspM === 'fft' ? 'FFT · brick-wall' : 'HYBRID · blended';
    ctx.fillText(dspLabel, inner.x + 8, inner.y + 6);

    ctx.restore();
  }

  function drawSelection(ctx, g) {
    // (drawn inline in drawBands; marquee rectangle here)
  }

  function drawMarquee(ctx, g) {
    if (!marquee) return;
    const { x1, y1, x2, y2 } = marquee;
    const x = Math.min(x1, x2), y = Math.min(y1, y2);
    const w = Math.abs(x2 - x1), h = Math.abs(y2 - y1);
    ctx.fillStyle = 'rgba(120,200,255,0.08)';
    ctx.strokeStyle = 'rgba(180,220,255,0.55)';
    ctx.lineWidth = 1;
    ctx.setLineDash([3, 3]);
    ctx.fillRect(x, y, w, h);
    ctx.strokeRect(x + 0.5, y + 0.5, w, h);
    ctx.setLineDash([]);
  }

  function drawEdgeWalls(ctx, g) {
    const eg = edgeGlowRef.current;
    const { inner } = g;
    if (eg.left > 0.01) {
      const gr = ctx.createLinearGradient(inner.x, 0, inner.x + 40, 0);
      gr.addColorStop(0, `rgba(120,200,255,${0.35 * eg.left})`);
      gr.addColorStop(1, 'rgba(120,200,255,0)');
      ctx.fillStyle = gr;
      ctx.fillRect(inner.x, inner.y, 40, inner.h);
    }
    if (eg.right > 0.01) {
      const gr = ctx.createLinearGradient(inner.x + inner.w - 40, 0, inner.x + inner.w, 0);
      gr.addColorStop(0, 'rgba(120,200,255,0)');
      gr.addColorStop(1, `rgba(120,200,255,${0.35 * eg.right})`);
      ctx.fillStyle = gr;
      ctx.fillRect(inner.x + inner.w - 40, inner.y, 40, inner.h);
    }
  }

  function drawHover(ctx, g) {
    if (!hover) return;
    const { x, y, band } = hover;
    const f = bandCenterFreq(band);
    const gv = renderGainsRef.current[band];
    const db = isMuted(targetGainsRef.current[band]) ? '−∞' : (gv * 24).toFixed(1);
    const label = `${window.SpectrFreq.fmt(f)}Hz   ${db}${db === '−∞' ? '' : ' dB'}   band ${band + 1}/${N}`;
    ctx.save();
    ctx.font = '11px JetBrains Mono, monospace';
    const tw = ctx.measureText(label).width + 18;
    const tx = clamp(x - tw / 2, g.inner.x, g.inner.x + g.inner.w - tw);
    const ty = clamp(y - 30, g.inner.y + 2, g.inner.y + g.inner.h);
    ctx.fillStyle = 'rgba(10,14,20,0.88)';
    ctx.strokeStyle = 'rgba(255,255,255,0.18)';
    ctx.lineWidth = 1;
    roundRect(ctx, tx, ty, tw, 22, 4);
    ctx.fill(); ctx.stroke();
    ctx.fillStyle = 'rgba(255,255,255,0.9)';
    ctx.textAlign = 'left';
    ctx.textBaseline = 'middle';
    ctx.fillText(label, tx + 9, ty + 11);
    // crosshair
    ctx.strokeStyle = 'rgba(255,255,255,0.15)';
    ctx.setLineDash([2, 3]);
    ctx.beginPath();
    ctx.moveTo(x, g.inner.y); ctx.lineTo(x, g.inner.y + g.inner.h);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();
  }

  function drawMinimap(ctx, g) {
    const { inner } = g;
    const mx = inner.x, my = g.inner.y + g.inner.h + 28;
    const mw = inner.w, mh = 22;
    const fullMin = Math.log10(20), fullMax = Math.log10(20000);
    const fullSpan = fullMax - fullMin;
    const wx1 = mx + ((view.lmin - fullMin) / fullSpan) * mw;
    const wx2 = mx + ((view.lmax - fullMin) / fullSpan) * mw;
    // Detect hover over minimap / handles (hover state passed via hover.minimap)
    const mmHover = hover && hover.mini ? hover.mini : null; // 'window' | 'left' | 'right' | null

    ctx.save();
    // track frame
    ctx.fillStyle = 'rgba(255,255,255,0.03)';
    roundRect(ctx, mx, my, mw, mh, 3);
    ctx.fill();
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    ctx.stroke();

    // decade ticks
    ctx.strokeStyle = 'rgba(255,255,255,0.10)';
    ctx.beginPath();
    for (let dec = 2; dec <= 4; dec++) {
      const x = mx + ((dec - fullMin) / fullSpan) * mw;
      ctx.moveTo(x, my + 3); ctx.lineTo(x, my + mh - 3);
    }
    ctx.stroke();

    // mini spectrum silhouette
    const steps = 120;
    ctx.beginPath();
    ctx.moveTo(mx, my + mh);
    for (let i = 0; i <= steps; i++) {
      const lf = fullMin + (i / steps) * fullSpan;
      const v = window.SpectrSignal.sample(lf, timeRef.current);
      const x = mx + (i / steps) * mw;
      const y = my + mh - v * (mh - 4) * 0.85;
      ctx.lineTo(x, y);
    }
    ctx.lineTo(mx + mw, my + mh);
    ctx.closePath();
    ctx.fillStyle = 'rgba(150,180,220,0.16)';
    ctx.fill();

    // current viewport window (hover-lit)
    const winA = mmHover === 'window' ? 0.22 : 0.12;
    ctx.fillStyle = `rgba(140,200,255,${winA})`;
    ctx.fillRect(wx1, my, wx2 - wx1, mh);
    ctx.strokeStyle = mmHover === 'window' ? 'rgba(200,230,255,0.95)' : 'rgba(180,220,255,0.75)';
    ctx.lineWidth = 1;
    ctx.strokeRect(wx1 + 0.5, my + 0.5, wx2 - wx1 - 1, mh - 1);

    // HANDLES — visible grip bars on each edge
    const drawHandle = (x, hot) => {
      const w = 6;
      ctx.fillStyle = hot ? 'rgba(220,240,255,1.0)' : 'rgba(180,220,255,0.9)';
      roundRect(ctx, x - w / 2, my - 2, w, mh + 4, 2);
      ctx.fill();
      // grip dots
      ctx.fillStyle = hot ? 'rgba(10,20,30,0.55)' : 'rgba(10,20,30,0.45)';
      for (let k = 0; k < 3; k++) {
        ctx.fillRect(x - 0.5, my + 5 + k * 4, 1, 2);
      }
    };
    drawHandle(wx1, mmHover === 'left');
    drawHandle(wx2, mmHover === 'right');

    // labels
    ctx.font = '9px JetBrains Mono, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.50)';
    ctx.textAlign = 'left'; ctx.textBaseline = 'top';
    ctx.fillText('VIEWPORT', mx, my + mh + 5);
    ctx.textAlign = 'right';
    const zoomX = (fullSpan / (view.lmax - view.lmin)).toFixed(2);
    ctx.fillStyle = 'rgba(255,255,255,0.62)';
    ctx.fillText(`${window.SpectrFreq.fmt(Math.pow(10, view.lmin))}Hz – ${window.SpectrFreq.fmt(Math.pow(10, view.lmax))}Hz   ×${zoomX}`, mx + mw, my + mh + 5);
    ctx.restore();
  }

  function drawHUD(ctx, g) {
    ctx.save();
    ctx.font = '10px JetBrains Mono, monospace';
    ctx.fillStyle = 'rgba(255,255,255,0.45)';
    ctx.textAlign = 'left'; ctx.textBaseline = 'top';
    // labels already handled by chrome component
    ctx.restore();
  }

  function roundRect(ctx, x, y, w, h, r) {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  }

  // ---- pointer handlers ----
  const findBand = (x, g) => {
    if (x < g.inner.x || x > g.inner.x + g.inner.w) return -1;
    const rel = x - g.inner.x;
    const step = g.bandW + g.bandGap;
    const i = Math.floor(rel / step);
    return clamp(i, 0, N - 1);
  };
  const pxToGain = (y, g) => clamp((g.zeroY - y) / g.halfH, -1, 1);

  // Guard against NaN/non-finite values that could leak in from a
  // pointer event with a NaN clientY or a divide-by-zero (e.g. when
  // getGeom returns inner.h=0 during a transient layout state). A
  // single NaN gain corrupts ALL downstream drawing — drawBands' path
  // becomes a NaN-laced lineTo chain, save_layer composites empty,
  // and the user sees a black canvas. Sanitize at the write boundary.
  const sanitizeGain = (v) => {
    if (v === -Infinity) return v;  // legitimate "muted" sentinel
    if (typeof v !== 'number' || !Number.isFinite(v)) return 0;
    return Math.max(-1, Math.min(1, v));
  };
  const commitGain = (idx, value) => {
    const v = sanitizeGain(value);
    targetGainsRef.current[idx] = v;
    setGains(prev => {
      const nxt = prev.slice();
      nxt[idx] = v;
      return nxt;
    });
  };
  const commitMany = (map) => {
    setGains(prev => {
      const nxt = prev.slice();
      for (const [k, v] of map) {
        const sv = sanitizeGain(v);
        nxt[k] = sv;
        targetGainsRef.current[k] = sv;
      }
      return nxt;
    });
  };

  // Helper: classify minimap hover region
  const minimapHit = (x, y, g) => {
    if (!showMinimap) return null;
    const my = g.inner.y + g.inner.h + 28, mh = 22;
    if (y < my - 3 || y > my + mh + 3) return null;
    if (x < g.inner.x - 6 || x > g.inner.x + g.inner.w + 6) return null;
    const fullMin = Math.log10(20), fullMax = Math.log10(20000);
    const fullSpan = fullMax - fullMin;
    const wx1 = g.inner.x + ((view.lmin - fullMin) / fullSpan) * g.inner.w;
    const wx2 = g.inner.x + ((view.lmax - fullMin) / fullSpan) * g.inner.w;
    if (Math.abs(x - wx1) < 6) return 'left';
    if (Math.abs(x - wx2) < 6) return 'right';
    if (x >= wx1 && x <= wx2) return 'window';
    return 'track';
  };

  const onPointerDown = (e) => {
    const g = getGeom();
    if (!g) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left, y = e.clientY - rect.top;
    const shift = e.shiftKey, alt = e.altKey, meta = e.metaKey || e.ctrlKey;
    // Pulp's bridge view instances ship without setPointerCapture; guard so a
    // missing-method call doesn't kill the rest of the handler.
    try { if (typeof wrapRef.current.setPointerCapture === 'function') wrapRef.current.setPointerCapture(e.pointerId); } catch (_e) {}

    // Minimap interaction
    const mm = minimapHit(x, y, g);
    if (mm) {
      const fullMin = Math.log10(20), fullMax = Math.log10(20000);
      const fullSpan = fullMax - fullMin;
      if (mm === 'left' || mm === 'right') {
        pointerRef.current = { mode: 'minimap-resize', edge: mm, viewStart: { lmin: view.lmin, lmax: view.lmax } };
        return;
      }
      if (mm === 'track') {
        // jump window center here
        const center = fullMin + ((x - g.inner.x) / g.inner.w) * fullSpan;
        const span = view.lmax - view.lmin;
        let lmin = clamp(center - span / 2, fullMin, fullMax - span);
        setView({ lmin, lmax: lmin + span });
      }
      pointerRef.current = { mode: 'minimap-drag', startX: x, viewStart: { lmin: view.lmin, lmax: view.lmax } };
      return;
    }

    if (x < g.inner.x || x > g.inner.x + g.inner.w || y < g.inner.y || y > g.inner.y + g.inner.h) {
      return;
    }

    // Alt+drag = pan; middle click also pans
    if (alt || e.button === 1) {
      pointerRef.current = {
        mode: 'pan',
        startX: x, startY: y,
        viewStart: { lmin: view.lmin, lmax: view.lmax },
      };
      return;
    }

    // Meta+drag on canvas = marquee select
    if (meta) {
      pointerRef.current = { mode: 'marquee', startX: x, startY: y };
      setMarquee({ x1: x, y1: y, x2: x, y2: y });
      return;
    }

    const band = findBand(x, g);
    if (band < 0) return;

    // Single click (no drag) mute toggle handled on pointerUp if movement was tiny.
    const curGain = targetGainsRef.current[band];

    // Double click handled via click count via a small threshold on movement
    if (shift) {
      // add to selection / toggle
      setSelection(prev => {
        const nxt = new Set(prev);
        if (nxt.has(band)) nxt.delete(band); else nxt.add(band);
        return nxt;
      });
      pointerRef.current = { mode: 'shift-select', band };
      return;
    }

    // Clear selection unless clicking inside it
    if (!selection.has(band)) {
      setSelection(new Set());
    }

    // Snapshot all gains at drag start — used by Boost/Flare/Glide modes.
    const startSnap = targetGainsRef.current.map(v => isMuted(v) ? -Infinity : v);

    // Determine paint vs direct-drag: if we immediately move across bands we'll paint.
    pointerRef.current = {
      mode: 'gain',
      editMode: editModeRef.current,
      startX: x, startY: y,
      lastX: x, lastY: y,
      band,
      startGain: isMuted(curGain) ? 0 : curGain,
      groupStart: selection.size > 1 && selection.has(band)
        ? new Map([...selection].map(i => [i, targetGainsRef.current[i]]))
        : null,
      paintedBands: new Set([band]),
      startSnap,
      downTime: performance.now(),
      didDrag: false,
    };
    // Immediate gain update on mouse-down for sculpt/level — users expect
    // clicking-and-not-moving to set gain at the click Y, not just toggle mute.
    // We debounce: only set here if the click is clearly a drag intent (not a pure tap).
    // We still detect pure taps in pointerUp via didDrag.
  };

  const onPointerMove = (e) => {
    const g = getGeom();
    if (!g) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left, y = e.clientY - rect.top;

    // hover
    const mm = minimapHit(x, y, g);
    const bandH = findBand(x, g);
    // setCursor — pulp's bridge ref instances don't ship a `.style`
    // object, so the literal `wrapRef.current.style.cursor = ...` lookup
    // throws on the native bridge and aborts the rest of onPointerMove.
    // Guard with a try/catch so the canvas paint pump keeps running.
    const setCursor = (c) => {
      try {
        if (wrapRef.current && wrapRef.current.style) wrapRef.current.style.cursor = c;
      } catch (_e) { /* native bridge ref has no .style */ }
    };
    if (mm) {
      setHover({ mini: mm, x, y, band: -1 });
      setCursor((mm === 'left' || mm === 'right') ? 'ew-resize' : (mm === 'window' ? 'grab' : 'pointer'));
    } else if (bandH >= 0 && y >= g.inner.y && y <= g.inner.y + g.inner.h) {
      setHover({ band: bandH, x, y });
      setCursor('crosshair');
    } else {
      setHover(null);
      setCursor('default');
    }

    const p = pointerRef.current;
    if (!p || !p.mode) return;

    if (p.mode === 'minimap-resize') {
      const fullMin = Math.log10(20), fullMax = Math.log10(20000);
      const fullSpan = fullMax - fullMin;
      const f = fullMin + ((x - g.inner.x) / g.inner.w) * fullSpan;
      let lmin = p.viewStart.lmin, lmax = p.viewStart.lmax;
      if (p.edge === 'left') lmin = clamp(f, fullMin, lmax - 0.1);
      else lmax = clamp(f, lmin + 0.1, fullMax);
      setView({ lmin, lmax });
      return;
    }

    if (p.mode === 'pan') {
      const dx = x - p.startX;
      const span = p.viewStart.lmax - p.viewStart.lmin;
      const shift = -(dx / g.inner.w) * span;
      let lmin = p.viewStart.lmin + shift;
      let lmax = p.viewStart.lmax + shift;
      const fullMin = Math.log10(20), fullMax = Math.log10(20000);
      if (lmin < fullMin) { edgeGlowRef.current.left = 1; lmax += fullMin - lmin; lmin = fullMin; }
      if (lmax > fullMax) { edgeGlowRef.current.right = 1; lmin -= lmax - fullMax; lmax = fullMax; }
      setView({ lmin, lmax });
      return;
    }

    if (p.mode === 'marquee') {
      setMarquee({ x1: p.startX, y1: p.startY, x2: x, y2: y });
      // update selection live
      const x1 = Math.min(p.startX, x), x2 = Math.max(p.startX, x);
      const sel = new Set();
      for (let i = 0; i < N; i++) {
        const cx = bandCenterX(i, g);
        if (cx >= x1 && cx <= x2) sel.add(i);
      }
      setSelection(sel);
      return;
    }

    if (p.mode === 'minimap-drag') {
      const fullMin = Math.log10(20), fullMax = Math.log10(20000);
      const fullSpan = fullMax - fullMin;
      const span = p.viewStart.lmax - p.viewStart.lmin;
      const dx = x - p.startX;
      const shift = (dx / g.inner.w) * fullSpan;
      let lmin = clamp(p.viewStart.lmin + shift, fullMin, fullMax - span);
      setView({ lmin, lmax: lmin + span });
      return;
    }

    if (p.mode === 'gain') {
      const dx = x - p.startX;
      const dy = y - p.startY;
      if (Math.abs(dx) > 3 || Math.abs(dy) > 3) p.didDrag = true;
      const curBand = findBand(x, g);
      const em = p.editMode || 'sculpt';

      // Group move has precedence (selection > 1 bands)
      if (p.groupStart) {
        const delta = -dy / g.halfH;
        const map = new Map();
        for (const [i, v0] of p.groupStart.entries()) {
          if (isMuted(v0)) { map.set(i, -Infinity); continue; }
          map.set(i, clamp(v0 + delta, -1, 1));
        }
        commitMany(map);
        return;
      }

      // --- SCULPT: free-draw paint, cursor Y directly sets band gain. ---
      if (em === 'sculpt') {
        const newG = pxToGain(y, g);
        const from = Math.min(p.band, curBand), to = Math.max(p.band, curBand);
        const map = new Map();
        for (let i = from; i <= to; i++) { p.paintedBands.add(i); map.set(i, newG); }
        map.set(curBand, newG);
        commitMany(map);
        p.band = curBand;
        p.lastX = x; p.lastY = y;
        return;
      }

      // --- LEVEL: hold cursor Y, every band touched snaps to that same value. ---
      if (em === 'level') {
        const newG = pxToGain(y, g);
        const from = Math.min(p.band, curBand), to = Math.max(p.band, curBand);
        const map = new Map();
        for (let i = from; i <= to; i++) { p.paintedBands.add(i); map.set(i, newG); }
        for (const b of p.paintedBands) map.set(b, newG); // all painted follow
        commitMany(map);
        p.band = curBand;
        return;
      }

      // --- BOOST: scale painted bands' start value by vertical drag factor. ---
      if (em === 'boost') {
        const k = 1 + (-dy / g.halfH) * 1.5; // +1 halfH → 2.5×; -1 halfH → -0.5×
        const from = Math.min(p.band, curBand), to = Math.max(p.band, curBand);
        for (let i = from; i <= to; i++) p.paintedBands.add(i);
        const map = new Map();
        for (const b of p.paintedBands) {
          const v0 = p.startSnap[b];
          if (isMuted(v0)) { map.set(b, -Infinity); continue; }
          map.set(b, clamp(v0 * k, -1, 1));
        }
        commitMany(map);
        p.band = curBand;
        return;
      }

      // --- FLARE: exaggerate — push values AWAY from 0 dB by drag amount. ---
      if (em === 'flare') {
        const amt = -dy / g.halfH; // +up=exaggerate, -down=compress
        const from = Math.min(p.band, curBand), to = Math.max(p.band, curBand);
        for (let i = from; i <= to; i++) p.paintedBands.add(i);
        const map = new Map();
        for (const b of p.paintedBands) {
          const v0 = p.startSnap[b];
          if (isMuted(v0)) { map.set(b, -Infinity); continue; }
          const sign = v0 >= 0 ? 1 : -1;
          const mag = Math.abs(v0);
          // exaggerate: magnitude grows with amt; small vals get gentle kick
          const kick = amt * (0.25 + mag * 0.9);
          const out = sign * clamp(mag + kick, 0, 1);
          map.set(b, clamp(out, -1, 1));
        }
        commitMany(map);
        p.band = curBand;
        return;
      }

      // --- GLIDE: smooth — draw a path, neighbors are pulled toward cursor w/ falloff. ---
      if (em === 'glide') {
        const newG = pxToGain(y, g);
        const radius = 4; // bands of influence each side
        const from = Math.min(p.band, curBand), to = Math.max(p.band, curBand);
        for (let i = from; i <= to; i++) p.paintedBands.add(i);
        const map = new Map();
        // For each band within radius of ANY painted band, pull toward newG by gaussian falloff.
        const touched = new Set();
        for (const b of p.paintedBands) {
          for (let d = -radius; d <= radius; d++) {
            const i = b + d;
            if (i < 0 || i >= N) continue;
            touched.add(i);
          }
        }
        for (const i of touched) {
          // distance to nearest painted band
          let nd = Infinity;
          for (const b of p.paintedBands) nd = Math.min(nd, Math.abs(i - b));
          const w = Math.exp(-(nd * nd) / (2 * (radius * 0.55) ** 2));
          const v0 = p.startSnap[i];
          if (isMuted(v0)) { map.set(i, -Infinity); continue; }
          map.set(i, clamp(v0 + (newG - v0) * w, -1, 1));
        }
        commitMany(map);
        p.band = curBand;
        return;
      }
    }
  };

  const onPointerUp = (e) => {
    const p = pointerRef.current;
    pointerRef.current = { mode: null };
    if (!p || !p.mode) { setMarquee(null); return; }
    if (p.mode === 'marquee') { setMarquee(null); return; }
    if (p.mode === 'gain') {
      // A quick double-tap (same band, <350ms since last tap) toggles mute.
      // Single taps do nothing — prevents accidental muting.
      if (!p.didDrag) {
        const now = performance.now();
        const last = lastTapRef.current;
        if (last && last.band === p.band && now - last.t < 350) {
          const cur = targetGainsRef.current[p.band];
          commitGain(p.band, isMuted(cur) ? 0 : -Infinity);
          if (onStatus) onStatus(isMuted(cur) ? `BAND ${p.band + 1} UNMUTED` : `BAND ${p.band + 1} MUTED`);
          lastTapRef.current = null;
        } else {
          lastTapRef.current = { band: p.band, t: now };
        }
      }
    }
  };

  const onWheel = (e) => {
    e.preventDefault();
    const g = getGeom();
    if (!g) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    if (x < g.inner.x || x > g.inner.x + g.inner.w) return;

    // zoom factor based on wheel delta; preserve frequency under cursor
    const delta = e.deltaY;
    const factor = Math.exp(delta * 0.0012);
    const anchor = view.lmin + ((x - g.inner.x) / g.inner.w) * (view.lmax - view.lmin);
    let span = (view.lmax - view.lmin) * factor;
    span = clamp(span, 0.10, Math.log10(20000) - Math.log10(20)); // ~0.1 decade min
    const t = (anchor - view.lmin) / (view.lmax - view.lmin);
    let lmin = anchor - t * span;
    let lmax = lmin + span;
    const fullMin = Math.log10(20), fullMax = Math.log10(20000);
    if (lmin < fullMin) { edgeGlowRef.current.left = 1; lmax += fullMin - lmin; lmin = fullMin; }
    if (lmax > fullMax) { edgeGlowRef.current.right = 1; lmin -= lmax - fullMax; lmax = fullMax; }
    setView({ lmin, lmax });
  };

  // Stable handlers — inline lambdas in JSX would create a new function
  // every render, which the dom-adapter sees as a prop change and
  // re-binds to the bridge each frame. Re-binding event handlers churns
  // the bridge's dispatch table and causes a layout pass per frame,
  // which manifests as the canvas flashing blank. Using stable refs
  // means the bridge gets the same function identity across renders.
  const onPointerLeaveStable = useCallback(() => setHover(null), []);
  const onContextMenuStable = useCallback((e) => {
    e.preventDefault();
    const g = getGeom();
    if (!g) return;
    const rect = wrapRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left, y = e.clientY - rect.top;
    const band = findBand(x, g);
    const inPlot = x >= g.inner.x && x <= g.inner.x + g.inner.w && y >= g.inner.y && y <= g.inner.y + g.inner.h;
    setCtxMenu({ x: e.clientX, y: e.clientY, band: inPlot ? band : -1 });
  }, [getGeom, findBand, N]);

  // ---- public methods (via sharedState ref) ----
  useEffect(() => {
    if (!sharedState) return;
    sharedState.current = {
      reset: () => {
        const z = new Array(N).fill(0);
        targetGainsRef.current = z.slice();
        renderGainsRef.current = z.slice();
        setGains(z);
        setSelection(new Set());
        setView({ lmin: Math.log10(20), lmax: Math.log10(20000) });
        setMorph(0);
      },
      invert: () => {
        const map = new Map();
        for (let i = 0; i < N; i++) {
          const v = targetGainsRef.current[i];
          map.set(i, isMuted(v) ? 0 : -v);
        }
        commitMany(map);
      },
      muteAll: () => {
        const map = new Map();
        for (let i = 0; i < N; i++) map.set(i, -Infinity);
        commitMany(map);
      },
      unmuteAll: () => {
        const map = new Map();
        for (let i = 0; i < N; i++) if (isMuted(targetGainsRef.current[i])) map.set(i, 0);
        commitMany(map);
      },
      allMuted: () => targetGainsRef.current.every(v => isMuted(v)),
      clearGains: () => {
        const z = new Array(N).fill(0);
        targetGainsRef.current = z.slice();
        setGains(z);
      },
      resetAll: () => {
        const z = new Array(N).fill(0);
        targetGainsRef.current = z.slice();
        renderGainsRef.current = z.slice();
        setGains(z);
        setSelection(new Set());
        setView({ lmin: Math.log10(20), lmax: Math.log10(20000) });
        setMorph(0);
        snapshotsRef.current = { A: null, B: null };
        setSnapshots({ A: null, B: null });
      },
      stamp: (pattern) => {
        const map = new Map();
        const baseF = bandCenterFreq(pointerRef.current?.band ?? Math.floor(N / 3));
        if (pattern === 'harmonics') {
          // mute all, then unmute bands nearest to harmonics of baseF
          for (let i = 0; i < N; i++) map.set(i, -Infinity);
          for (let h = 1; h <= 12; h++) {
            const hf = baseF * h;
            const lf = Math.log10(hf);
            if (lf < view.lmin || lf > view.lmax) continue;
            const pos = (lf - view.lmin) / (view.lmax - view.lmin);
            const i = clamp(Math.round(pos * N - 0.5), 0, N - 1);
            map.set(i, 1);
          }
        } else if (pattern === 'alternate') {
          for (let i = 0; i < N; i++) map.set(i, (i % 2 === 0) ? 0.6 : -Infinity);
        } else if (pattern === 'comb') {
          for (let i = 0; i < N; i++) map.set(i, (i % 3 === 0) ? 0.4 : -0.6);
        } else if (pattern === 'vocal') {
          // bandpass around vocal formants
          for (let i = 0; i < N; i++) map.set(i, -Infinity);
          for (const f of [300, 900, 2800]) {
            const lf = Math.log10(f);
            const pos = (lf - view.lmin) / (view.lmax - view.lmin);
            const c = clamp(Math.round(pos * N - 0.5), 0, N - 1);
            for (let d = -2; d <= 2; d++) {
              const i = clamp(c + d, 0, N - 1);
              map.set(i, d === 0 ? 1 : 0.5);
            }
          }
        } else if (pattern === 'subonly') {
          for (let i = 0; i < N; i++) {
            const f = bandCenterFreq(i);
            map.set(i, f < 160 ? 0.5 : -Infinity);
          }
        }
        commitMany(map);
      },
      snapshot: (slot) => {
        const snap = targetGainsRef.current.slice();
        snapshotsRef.current = { ...snapshotsRef.current, [slot]: snap };
        setSnapshots(snapshotsRef.current);
        if (onStatus) onStatus(`SNAPSHOT ${slot} CAPTURED`);
      },
      recallSnap: (slot) => {
        const snap = snapshotsRef.current[slot];
        if (!snap) {
          if (onStatus) onStatus(`NO SNAPSHOT IN ${slot}`);
          return;
        }
        const map = new Map();
        for (let i = 0; i < Math.min(snap.length, N); i++) map.set(i, snap[i]);
        commitMany(map);
        if (onStatus) onStatus(`RECALLED ${slot}`);
      },
      setMorph: (v) => {
        setMorph(v);
        const s = snapshotsRef.current;
        if (!s.A || !s.B) return;
        const map = new Map();
        for (let i = 0; i < N; i++) {
          const a = s.A[i] ?? 0, b = s.B[i] ?? 0;
          let out;
          if (isMuted(a) && isMuted(b)) out = -Infinity;
          else if (isMuted(a)) out = v > 0.5 ? b : -Infinity;
          else if (isMuted(b)) out = v < 0.5 ? a : -Infinity;
          else out = lerp(a, b, v);
          map.set(i, out);
        }
        commitMany(map);
      },
      getSnapshots: () => snapshots,
      resetView: () => setView({ lmin: Math.log10(20), lmax: Math.log10(20000) }),
      zoomTo: (fmin, fmax) => setView({ lmin: Math.log10(fmin), lmax: Math.log10(fmax) }),
      setGains: (arr) => {
        const next = arr.slice(0, N);
        while (next.length < N) next.push(0);
        targetGainsRef.current = next.slice();
        setGains(next);
      },
      getGains: () => targetGainsRef.current.slice(),
      view,
      N,
    };
  }, [N, snapshots, view]);

  return (
    <div
      ref={wrapRef}
      style={{
        position: 'absolute', inset: 0,
        cursor: 'crosshair',
        touchAction: 'none',
      }}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerLeave={onPointerLeaveStable}
      onWheel={onWheel}
      onContextMenu={onContextMenuStable}
    >
      {/* Native order matches WebView: main canvas (analyzer) drawn first,
          overlay canvas (selection / marquee / hover) drawn on top. Pulp
          v0.74.1 (#1372) wraps each CanvasWidget's JS replay in its own
          save_layer, so sibling clearRect / kClear no longer erases the
          parent surface.

          spectr #32 — pulp's bridge does NOT bubble pointer events the
          way React's synthetic event system does. Each widget either
          has the handler bound directly OR the click goes nowhere.
          Binding onPointerDown/Move/Up/Wheel on the wrap div doesn't
          fire when the click lands on the canvas widgets, so we mirror
          the same handlers onto BOTH canvases. The handlers themselves
          use wrapRef.getBoundingClientRect() for coord math, so they
          work identically regardless of which element actually fires. */}
      <canvas
        ref={canvasRef}
        style={CANVAS_FILL_STYLE}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerLeave={onPointerLeaveStable}
        onWheel={onWheel}
      />
      <canvas
        ref={overlayRef}
        style={CANVAS_FILL_STYLE}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerLeave={onPointerLeaveStable}
        onWheel={onWheel}
      />
      {ctxMenu && (
        <ContextMenu
          x={ctxMenu.x} y={ctxMenu.y} band={ctxMenu.band} N={N}
          selection={selection}
          editMode={editModeRef.current}
          onClose={() => setCtxMenu(null)}
          onEditMode={(m) => { if (onEditModeChange) onEditModeChange(m); }}
          onMuteBand={(b) => { const cur = targetGainsRef.current[b]; commitGain(b, isMuted(cur) ? 0 : -Infinity); }}
          onZeroBand={(b) => commitGain(b, 0)}
          onSoloBand={(b) => {
            const map = new Map();
            for (let i = 0; i < N; i++) map.set(i, i === b ? Math.max(0, targetGainsRef.current[b]) : -Infinity);
            commitMany(map);
          }}
          onSelectAround={(b, r) => {
            const nxt = new Set();
            for (let i = Math.max(0, b - r); i <= Math.min(N - 1, b + r); i++) nxt.add(i);
            setSelection(nxt);
          }}
          onClearSel={() => setSelection(new Set())}
          onZeroSel={() => {
            const map = new Map();
            for (const i of selection) map.set(i, 0);
            commitMany(map);
          }}
          onMuteSel={() => {
            const map = new Map();
            for (const i of selection) map.set(i, -Infinity);
            commitMany(map);
          }}
          onFitView={() => setView({ lmin: Math.log10(20), lmax: Math.log10(20000) })}
        />
      )}
    </div>
  );
}

window.FilterBank = FilterBank;

function ContextMenu({ x, y, band, N, selection, editMode, onClose, onEditMode, onMuteBand, onZeroBand, onSoloBand, onSelectAround, onClearSel, onZeroSel, onMuteSel, onFitView }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const onDown = (e) => { if (ref.current && !ref.current.contains(e.target)) onClose(); };
    const onKey = (e) => { if (e.key === 'Escape') onClose(); };
    // Schedule so the click that opened it doesn't immediately close it.
    const t = setTimeout(() => document.addEventListener('pointerdown', onDown), 0);
    window.addEventListener('keydown', onKey);
    return () => { clearTimeout(t); document.removeEventListener('pointerdown', onDown); window.removeEventListener('keydown', onKey); };
  }, [onClose]);

  // Clamp to viewport
  const vw = window.innerWidth, vh = window.innerHeight;
  const W = 230, H = 380;
  const left = Math.min(x, vw - W - 8);
  const top = Math.min(y, vh - H - 8);

  const hasBand = band >= 0;
  const hasSel = selection && selection.size > 0;

  const modes = [
    { k: 'sculpt', label: 'Sculpt', hint: 'S' },
    { k: 'level',  label: 'Level',  hint: 'L' },
    { k: 'boost',  label: 'Boost',  hint: 'B' },
    { k: 'flare',  label: 'Flare',  hint: 'F' },
    { k: 'glide',  label: 'Glide',  hint: 'G' },
  ];

  const Item = ({ label, hint, onClick, disabled, danger, sub }) => (
    <button
      onClick={() => { if (!disabled) { onClick(); onClose(); } }}
      disabled={disabled}
      style={{
        display: 'flex', alignItems: 'center', gap: 10,
        width: '100%', padding: '6px 12px',
        background: 'transparent', border: 'none',
        color: disabled ? 'rgba(255,255,255,0.25)' : (danger ? 'rgba(255,180,190,0.9)' : 'rgba(255,255,255,0.88)'),
        cursor: disabled ? 'default' : 'pointer',
        fontFamily: 'var(--mono)', fontSize: 10.5, letterSpacing: 0.3,
        textAlign: 'left',
      }}
      onMouseEnter={e => { if (!disabled) e.currentTarget.style.background = 'rgba(120,180,255,0.14)'; }}
      onMouseLeave={e => { e.currentTarget.style.background = 'transparent'; }}
    >
      <span style={{ flex: 1 }}>{label}</span>
      {sub && <span style={{ opacity: 0.45, fontSize: 9.5 }}>{sub}</span>}
      {hint && <span style={{
        fontSize: 8.5, opacity: 0.5, padding: '1px 5px',
        border: '1px solid rgba(255,255,255,0.14)', borderRadius: 2,
      }}>{hint}</span>}
    </button>
  );
  const Divider = ({ label }) => (
    <div style={{
      fontSize: 8.5, letterSpacing: 2, opacity: 0.4,
      padding: '8px 12px 4px', textTransform: 'uppercase',
    }}>{label}</div>
  );

  return (
    <div ref={ref}
      style={{
        position: 'fixed', left, top, width: W,
        background: 'rgba(12,16,22,0.97)',
        border: '1px solid rgba(255,255,255,0.12)',
        borderRadius: 5, padding: '6px 0',
        boxShadow: '0 14px 40px rgba(0,0,0,0.6)',
        backdropFilter: 'blur(12px)',
        zIndex: 40, pointerEvents: 'auto',
      }}
    >
      {hasBand && (
        <>
          <Divider label={`Band ${band + 1}`} />
          <Item label="Mute / Unmute" onClick={() => onMuteBand(band)} />
          <Item label="Reset to 0 dB" onClick={() => onZeroBand(band)} />
          <Item label="Solo" onClick={() => onSoloBand(band)} sub="mute others" />
          <Item label="Select ±3" onClick={() => onSelectAround(band, 3)} />
          <Item label="Select ±8" onClick={() => onSelectAround(band, 8)} />
        </>
      )}
      {hasSel && (
        <>
          <Divider label={`Selection · ${selection.size}`} />
          <Item label="Zero selection" onClick={onZeroSel} />
          <Item label="Mute selection" onClick={onMuteSel} />
          <Item label="Clear selection" onClick={onClearSel} />
        </>
      )}
      <Divider label="Edit Mode" />
      {modes.map(m => (
        <Item key={m.k}
          label={(editMode === m.k ? '● ' : '   ') + m.label}
          hint={m.hint}
          onClick={() => onEditMode(m.k)} />
      ))}
      <Divider label="View" />
      <Item label="Fit full range" onClick={onFitView} sub="20 Hz – 20 kHz" />
    </div>
  );
}

window.ContextMenu = ContextMenu;



// ===== inner script 3 =====

// Pattern Manager — modal panel for save/name/rename/duplicate/delete/default/import/export.

const { useState: usePM, useEffect: usePE, useRef: usePR, useMemo: useMemoPM } = React;

function PatternManager({ open, onClose, userPatterns, setUserPatterns, defaultId, setDefaultId, N, onApply, currentGains, onStatus }) {
  const [query, setQuery] = usePM('');
  const [selectedId, setSelectedId] = usePM(null);
  const fileInputRef = usePR(null);
  const [importText, setImportText] = usePM('');
  const [showImport, setShowImport] = usePM(false);

  const factory = window.Spectr.FACTORY_PATTERNS;

  const filteredFactory = factory.filter(p => p.name.toLowerCase().includes(query.toLowerCase()));
  const filteredUser = userPatterns.filter(p => p.name.toLowerCase().includes(query.toLowerCase()));
  const selected = [...factory, ...userPatterns].find(p => p.id === selectedId);

  const saveCurrent = () => {
    const existingNames = new Set([...factory, ...userPatterns].map(p => p.name.toUpperCase()));
    let n = 1, name;
    do { name = `PRESET ${String(userPatterns.length + n).padStart(2, '0')}`; n++; } while (existingNames.has(name.toUpperCase()));
    const p = window.Spectr.makeUserPattern(name, currentGains);
    setUserPatterns([...userPatterns, p]);
    setSelectedId(p.id);
    onStatus && onStatus(`SAVED "${name}"`);
  };

  const rename = (id, newName) => {
    setUserPatterns(userPatterns.map(p => p.id === id ? { ...p, name: newName.slice(0, 48) || 'Untitled', updatedAt: new Date().toISOString() } : p));
  };
  const duplicate = (id) => {
    const src = [...factory, ...userPatterns].find(p => p.id === id);
    if (!src) return;
    const gains = src.source === 'factory'
      ? window.Spectr.factoryGains(src.id, 128)
      : window.Spectr.fromCanonical(src.gains);
    const p = window.Spectr.makeUserPattern(src.name + ' COPY', gains);
    setUserPatterns([...userPatterns, p]);
    setSelectedId(p.id);
    onStatus && onStatus(`DUPLICATED`);
  };
  const del = (id) => {
    setUserPatterns(userPatterns.filter(p => p.id !== id));
    if (defaultId === id) { setDefaultId('factory:flat'); }
    if (selectedId === id) setSelectedId(null);
    onStatus && onStatus(`DELETED`);
  };
  const overwrite = (id) => {
    const src = [...factory, ...userPatterns].find(p => p.id === id);
    if (!src || src.source === 'factory') return;
    setUserPatterns(userPatterns.map(p => p.id === id
      ? { ...p, gains: window.Spectr.toCanonical(currentGains), updatedAt: new Date().toISOString() }
      : p));
    onStatus && onStatus(`UPDATED "${src.name}"`);
  };

  const exportSelected = async (mode) => {
    if (!selected) return;
    const patterns = selected.source === 'user'
      ? [selected]
      : [{ ...selected, version: 1, createdAt: new Date().toISOString(), updatedAt: new Date().toISOString(), gains: window.Spectr.toCanonical(window.Spectr.factoryGains(selected.id, 128)) }];
    const env = window.Spectr.exportEnvelope(patterns);
    const json = JSON.stringify(env, null, 2);
    if (mode === 'clipboard') {
      try { await navigator.clipboard.writeText(json); onStatus && onStatus('COPIED TO CLIPBOARD'); }
      catch { onStatus && onStatus('CLIPBOARD DENIED'); }
    } else {
      const blob = new Blob([json], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url; a.download = `spectr-${selected.name.replace(/[^a-z0-9]+/gi, '-').toLowerCase()}.json`;
      a.click();
      URL.revokeObjectURL(url);
      onStatus && onStatus('EXPORTED');
    }
  };

  const exportAll = async (mode) => {
    if (userPatterns.length === 0) { onStatus && onStatus('NO USER PATTERNS'); return; }
    const env = window.Spectr.exportEnvelope(userPatterns);
    const json = JSON.stringify(env, null, 2);
    if (mode === 'clipboard') {
      try { await navigator.clipboard.writeText(json); onStatus && onStatus('COPIED ALL'); }
      catch { onStatus && onStatus('CLIPBOARD DENIED'); }
    } else {
      const blob = new Blob([json], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url; a.download = `spectr-patterns.json`;
      a.click();
      URL.revokeObjectURL(url);
      onStatus && onStatus('EXPORTED ALL');
    }
  };

  const handleImportJSON = (text) => {
    try {
      const obj = JSON.parse(text);
      const { patterns, errors } = window.Spectr.parseEnvelope(obj);
      if (patterns.length === 0) {
        onStatus && onStatus(`IMPORT FAILED: ${errors[0] || 'no patterns'}`);
        return;
      }
      // Collision handling: rename on conflict
      const existingNames = new Set([...userPatterns, ...factory].map(p => p.name.toUpperCase()));
      const renamed = patterns.map(p => {
        let name = p.name;
        let suffix = 2;
        while (existingNames.has(name.toUpperCase())) {
          name = `${p.name} (${suffix++})`;
        }
        existingNames.add(name.toUpperCase());
        return { ...p, name };
      });
      setUserPatterns([...userPatterns, ...renamed]);
      setShowImport(false);
      setImportText('');
      onStatus && onStatus(`IMPORTED ${renamed.length}${errors.length ? ` (${errors.length} skipped)` : ''}`);
    } catch (e) {
      onStatus && onStatus(`IMPORT FAILED: ${e.message}`);
    }
  };

  const handleFile = (file) => {
    const r = new FileReader();
    r.onload = () => handleImportJSON(String(r.result));
    r.readAsText(file);
  };
  const pasteFromClipboard = async () => {
    try {
      const text = await navigator.clipboard.readText();
      handleImportJSON(text);
    } catch {
      onStatus && onStatus('CLIPBOARD DENIED');
    }
  };

  if (!open) return null;

  return (
    <div style={{
      position: 'absolute', inset: 0, zIndex: 30,
      background: 'rgba(5,7,10,0.7)',
      backdropFilter: 'blur(6px)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      pointerEvents: 'auto',
    }} onClick={onClose}>
      <div onClick={e => e.stopPropagation()} style={{
        width: 780, height: 520,
        background: 'rgba(12,16,22,0.98)',
        border: '1px solid rgba(255,255,255,0.14)',
        borderRadius: 6,
        fontFamily: 'var(--mono)', color: 'rgba(255,255,255,0.9)',
        display: 'flex', flexDirection: 'column',
        boxShadow: '0 40px 100px rgba(0,0,0,0.6)',
      }}>
        {/* Header */}
        <div style={{
          padding: '12px 16px',
          borderBottom: '1px solid rgba(255,255,255,0.08)',
          display: 'flex', alignItems: 'center', gap: 12,
        }}>
          <span style={{ fontSize: 12, fontWeight: 600, letterSpacing: 2 }}>PRESET MANAGER</span>
          <span style={{ opacity: 0.45, fontSize: 10 }}>— {userPatterns.length} user · {factory.length} factory</span>
          <div style={{ flex: 1 }} />
          <input
            type="text" placeholder="search…" value={query} onChange={e => setQuery(e.target.value)}
            style={{
              background: 'rgba(255,255,255,0.04)', border: '1px solid rgba(255,255,255,0.1)',
              color: '#fff', fontFamily: 'var(--mono)', fontSize: 10, letterSpacing: 0.5,
              padding: '4px 8px', borderRadius: 3, width: 140, outline: 'none',
            }}
          />
          <button onClick={onClose} style={iconBtn}>×</button>
        </div>

        {/* Body */}
        <div style={{ flex: 1, display: 'flex', minHeight: 0 }}>
          {/* Left: list */}
          <div style={{
            width: 320, borderRight: '1px solid rgba(255,255,255,0.06)',
            display: 'flex', flexDirection: 'column',
          }}>
            <div style={{ flex: 1, overflow: 'auto', padding: '6px 0' }}>
              <ListHeader label={`FACTORY · ${filteredFactory.length}`} />
              {filteredFactory.map(p => (
                <PatternRow key={p.id} pattern={p}
                  selected={selectedId === p.id}
                  isDefault={defaultId === p.id}
                  onClick={() => setSelectedId(p.id)}
                  onDblClick={() => { onApply(p); }}
                  N={N}
                />
              ))}
              <ListHeader label={`USER · ${filteredUser.length}`} />
              {filteredUser.length === 0 && (
                <div style={{ padding: '8px 14px', fontSize: 10, opacity: 0.4, fontStyle: 'italic' }}>
                  no user patterns — click SAVE CURRENT below
                </div>
              )}
              {filteredUser.map(p => (
                <PatternRow key={p.id} pattern={p}
                  selected={selectedId === p.id}
                  isDefault={defaultId === p.id}
                  onClick={() => setSelectedId(p.id)}
                  onDblClick={() => { onApply(p); }}
                  N={N}
                />
              ))}
            </div>
            <div style={{
              padding: 10, borderTop: '1px solid rgba(255,255,255,0.06)',
              display: 'flex', gap: 6, flexWrap: 'wrap',
            }}>
              <MBtn onClick={saveCurrent} primary>SAVE CURRENT</MBtn>
              <MBtn onClick={() => fileInputRef.current?.click()}>IMPORT FILE</MBtn>
              <MBtn onClick={pasteFromClipboard}>PASTE JSON</MBtn>
              <MBtn onClick={() => setShowImport(v => !v)}>{showImport ? 'CANCEL' : 'PASTE…'}</MBtn>
              <input ref={fileInputRef} type="file" accept="application/json,.json"
                style={{ display: 'none' }}
                onChange={e => { if (e.target.files?.[0]) handleFile(e.target.files[0]); e.target.value=''; }}
              />
            </div>
          </div>

          {/* Right: detail */}
          <div style={{ flex: 1, padding: 16, display: 'flex', flexDirection: 'column', gap: 12, minWidth: 0 }}>
            {showImport ? (
              <>
                <div style={{ fontSize: 10, opacity: 0.6, letterSpacing: 1 }}>PASTE JSON BELOW</div>
                <textarea value={importText} onChange={e => setImportText(e.target.value)}
                  placeholder='{"format":"spectr.patterns", …}'
                  style={{
                    flex: 1, background: 'rgba(0,0,0,0.35)', border: '1px solid rgba(255,255,255,0.12)',
                    color: 'rgba(200,230,255,0.9)', fontFamily: 'var(--mono)', fontSize: 10.5,
                    padding: 10, borderRadius: 3, resize: 'none', outline: 'none',
                  }}
                />
                <div style={{ display: 'flex', gap: 6, justifyContent: 'flex-end' }}>
                  <MBtn onClick={() => setShowImport(false)}>CANCEL</MBtn>
                  <MBtn primary onClick={() => handleImportJSON(importText)}>IMPORT</MBtn>
                </div>
              </>
            ) : selected ? (
              <PatternDetail
                pattern={selected}
                N={N}
                isDefault={defaultId === selected.id}
                onApply={() => { onApply(selected); onStatus && onStatus(`APPLIED "${selected.name}"`); }}
                onRename={(name) => rename(selected.id, name)}
                onDuplicate={() => duplicate(selected.id)}
                onDelete={() => { if (confirm(`Delete "${selected.name}"?`)) del(selected.id); }}
                onOverwrite={() => overwrite(selected.id)}
                onSetDefault={() => { setDefaultId(selected.id); onStatus && onStatus(`DEFAULT → ${selected.name}`); }}
                onExport={(m) => exportSelected(m)}
              />
            ) : (
              <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', opacity: 0.35, fontSize: 11 }}>
                SELECT A PATTERN
              </div>
            )}
          </div>
        </div>

        {/* Footer */}
        <div style={{
          padding: '8px 14px', borderTop: '1px solid rgba(255,255,255,0.06)',
          fontSize: 9.5, opacity: 0.5, display: 'flex', gap: 16, alignItems: 'center',
        }}>
          <span>DOUBLE-CLICK to apply</span>
          <span>·</span>
          <span>⭐ = default on open</span>
          <div style={{ flex: 1 }} />
          <MBtn onClick={() => exportAll('file')}>EXPORT ALL (FILE)</MBtn>
          <MBtn onClick={() => exportAll('clipboard')}>EXPORT ALL (CLIP)</MBtn>
        </div>
      </div>
    </div>
  );
}

function ListHeader({ label }) {
  return (
    <div style={{
      padding: '10px 14px 4px', fontSize: 9, letterSpacing: 2, opacity: 0.4,
    }}>{label}</div>
  );
}

function PatternRow({ pattern, selected, isDefault, onClick, onDblClick, N }) {
  const gains = useMemoPM(() => window.Spectr.resolveGains(pattern, N), [pattern, pattern.updatedAt, pattern.id, N]);
  return (
    <div onClick={onClick} onDoubleClick={onDblClick}
      style={{
        padding: '6px 14px',
        background: selected ? 'rgba(120,180,255,0.14)' : 'transparent',
        borderLeft: selected ? '2px solid hsl(200,85%,65%)' : '2px solid transparent',
        cursor: 'pointer',
        display: 'flex', alignItems: 'center', gap: 10,
        fontSize: 10, letterSpacing: 0.5,
      }}
    >
      <MiniPreview gains={gains} />
      <div style={{ flex: 1, minWidth: 0, overflow: 'hidden' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          {isDefault && <span style={{ color: 'hsl(50,90%,65%)', fontSize: 10 }}>★</span>}
          <span style={{
            color: selected ? '#fff' : 'rgba(255,255,255,0.85)',
            whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis',
          }}>{pattern.name}</span>
        </div>
      </div>
      <span style={{
        fontSize: 8, letterSpacing: 1.5, opacity: 0.4,
        padding: '1px 4px', border: '1px solid rgba(255,255,255,0.12)', borderRadius: 2,
      }}>{pattern.source === 'factory' ? 'F' : 'U'}</span>
    </div>
  );
}

function MiniPreview({ gains, w = 56, h = 22 }) {
  const N = gains.length;
  const bw = w / N;
  return (
    <svg width={w} height={h} style={{ opacity: 0.9 }}>
      {gains.map((g, i) => {
        if (g === -Infinity) {
          return <rect key={i} x={i * bw} y={h - 2} width={Math.max(1, bw - 0.5)} height={2} fill="rgba(255,100,100,0.3)" />;
        }
        const gh = Math.max(1, Math.abs(g) * (h - 2));
        const y = g >= 0 ? h / 2 - gh : h / 2;
        const hue = 240 - (i / N) * 300;
        return <rect key={i} x={i * bw} y={y} width={Math.max(1, bw - 0.5)} height={gh}
          fill={`hsl(${hue}, 75%, 60%)`} opacity={0.85} />;
      })}
      <line x1={0} y1={h / 2} x2={w} y2={h / 2} stroke="rgba(255,255,255,0.15)" strokeWidth={0.5} />
    </svg>
  );
}

function PatternDetail({ pattern, N, isDefault, onApply, onRename, onDuplicate, onDelete, onOverwrite, onSetDefault, onExport }) {
  const [editName, setEditName] = usePM(false);
  const [draft, setDraft] = usePM(pattern.name);
  usePE(() => { setDraft(pattern.name); setEditName(false); }, [pattern.id]);
  const gains = useMemoPM(() => window.Spectr.resolveGains(pattern, N), [pattern, N, pattern.updatedAt]);
  const isFactory = pattern.source === 'factory';

  return (
    <>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        {editName && !isFactory ? (
          <input autoFocus value={draft}
            onChange={e => setDraft(e.target.value)}
            onBlur={() => { onRename(draft); setEditName(false); }}
            onKeyDown={e => { if (e.key === 'Enter') e.target.blur(); if (e.key === 'Escape') { setDraft(pattern.name); setEditName(false); } }}
            style={{
              background: 'rgba(255,255,255,0.05)', border: '1px solid rgba(180,210,255,0.4)',
              color: '#fff', fontFamily: 'var(--mono)', fontSize: 14, letterSpacing: 1,
              padding: '4px 8px', borderRadius: 3, outline: 'none', flex: 1,
            }}
          />
        ) : (
          <span style={{ fontSize: 14, letterSpacing: 1, fontWeight: 500 }}>
            {isDefault && <span style={{ color: 'hsl(50,90%,65%)', marginRight: 6 }}>★</span>}
            {pattern.name}
          </span>
        )}
        <span style={{
          fontSize: 8.5, letterSpacing: 1.5, opacity: 0.6,
          padding: '2px 6px', border: '1px solid rgba(255,255,255,0.15)', borderRadius: 2,
        }}>{isFactory ? 'FACTORY' : 'USER'}</span>
        {!isFactory && !editName && (
          <button onClick={() => setEditName(true)} style={iconBtn}>✎</button>
        )}
      </div>

      {/* Full preview */}
      <div style={{
        background: 'rgba(0,0,0,0.35)', border: '1px solid rgba(255,255,255,0.06)',
        borderRadius: 3, padding: 10, height: 110,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}>
        <MiniPreview gains={gains} w={380} h={86} />
      </div>

      <div style={{ fontSize: 9.5, opacity: 0.55, display: 'flex', gap: 14, flexWrap: 'wrap' }}>
        <span>BANDS (current): <span className="tnum">{N}</span></span>
        {!isFactory && pattern.createdAt && <span>CREATED: {pattern.createdAt.slice(0, 10)}</span>}
        {!isFactory && pattern.updatedAt && <span>UPDATED: {pattern.updatedAt.slice(0, 10)}</span>}
      </div>

      <div style={{ flex: 1 }} />

      <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
        <MBtn primary onClick={onApply}>APPLY</MBtn>
        <MBtn onClick={onSetDefault}>{isDefault ? '★ DEFAULT' : 'SET AS DEFAULT'}</MBtn>
        <MBtn onClick={onDuplicate}>DUPLICATE</MBtn>
        {!isFactory && <MBtn onClick={onOverwrite}>UPDATE FROM CURRENT</MBtn>}
        {!isFactory && <MBtn danger onClick={onDelete}>DELETE</MBtn>}
        <div style={{ flex: 1 }} />
        <MBtn onClick={() => onExport('file')}>EXPORT (FILE)</MBtn>
        <MBtn onClick={() => onExport('clipboard')}>EXPORT (CLIP)</MBtn>
      </div>
    </>
  );
}

const iconBtn = {
  background: 'transparent', border: 'none', color: 'rgba(255,255,255,0.6)',
  cursor: 'pointer', fontSize: 16, padding: '0 4px', lineHeight: 1,
};

function MBtn({ children, onClick, primary, danger }) {
  return (
    <button onClick={onClick} style={{
      background: primary ? 'rgba(80,140,210,0.22)' : (danger ? 'rgba(210,80,100,0.15)' : 'rgba(255,255,255,0.04)'),
      border: '1px solid ' + (primary ? 'rgba(140,190,240,0.4)' : (danger ? 'rgba(240,150,160,0.35)' : 'rgba(255,255,255,0.12)')),
      color: primary ? '#fff' : (danger ? 'rgba(255,200,210,0.95)' : 'rgba(255,255,255,0.85)'),
      padding: '5px 9px', fontFamily: 'var(--mono)', fontSize: 10, letterSpacing: 1,
      borderRadius: 3, cursor: 'pointer', height: 26,
    }}>{children}</button>
  );
}

window.PatternManager = PatternManager;



// ===== inner script 4 =====

// Top title bar + bottom action rail. Minimal, instrument-like chrome.

const { useState: useStateChrome, useEffect: useEffectChrome, useRef: useRefChrome } = React;

function Chrome({ settings, setSettings, bankRef, info, status, dspMode, setDspMode, editMode, setEditMode, analyzerMode, setAnalyzerMode, snapshotStatus, patterns, onApplyPattern, onOpenPatternManager, onClearAll, onResetAll, allMuted }) {
  const [helpOpen, setHelpOpen] = useStateChrome(false);
  const [settingsOpen, setSettingsOpen] = useStateChrome(false);
  // Only one bottom-bar menu is open at a time: 'pattern' | 'bands' | 'overflow' | 'edit' | 'analyzer' | null
  const [openMenu, setOpenMenu] = useStateChrome(null);
  const toggleMenu = (k) => setOpenMenu(m => (m === k ? null : k));
  const patternMenu = openMenu === 'pattern';
  const bandsMenu = openMenu === 'bands';
  const overflowMenu = openMenu === 'overflow';
  const editMenu = openMenu === 'edit';
  const analyzerMenu = openMenu === 'analyzer';
  // Shims so existing setXxxMenu callsites still compile
  const setPatternMenu = (v) => setOpenMenu(typeof v === 'function' ? (v(patternMenu) ? 'pattern' : null) : (v ? 'pattern' : null));
  const setBandsMenu = (v) => setOpenMenu(typeof v === 'function' ? (v(bandsMenu) ? 'bands' : null) : (v ? 'bands' : null));
  const setOverflowMenu = (v) => setOpenMenu(typeof v === 'function' ? (v(overflowMenu) ? 'overflow' : null) : (v ? 'overflow' : null));
  const setEditMenu = (v) => setOpenMenu(typeof v === 'function' ? (v(editMenu) ? 'edit' : null) : (v ? 'edit' : null));
  const setAnalyzerMenu = (v) => setOpenMenu(typeof v === 'function' ? (v(analyzerMenu) ? 'analyzer' : null) : (v ? 'analyzer' : null));

  const act = (fn) => () => {
    if (bankRef.current) fn(bankRef.current);
  };

  return (
    <>
      {/* Top bar */}
      <div style={{
        position: 'absolute', top: 0, left: 0, right: 0, height: 44,
        display: 'flex', alignItems: 'center', gap: 18,
        padding: '0 20px',
        borderBottom: '1px solid rgba(255,255,255,0.06)',
        background: 'linear-gradient(to bottom, rgba(10,14,20,0.8), rgba(10,14,20,0.0))',
        zIndex: 5, pointerEvents: 'auto',
        fontFamily: 'var(--mono)',
        fontSize: 11,
        letterSpacing: 0.5,
        color: 'rgba(255,255,255,0.75)',
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
            <rect x="1" y="6" width="2" height="4" fill="hsl(240,80%,65%)" />
            <rect x="4" y="3" width="2" height="10" fill="hsl(200,85%,60%)" />
            <rect x="7" y="1" width="2" height="14" fill="hsl(150,85%,60%)" />
            <rect x="10" y="4" width="2" height="8" fill="hsl(60,90%,60%)" />
            <rect x="13" y="7" width="2" height="2" fill="hsl(0,85%,60%)" />
          </svg>
          <span style={{ fontWeight: 600, letterSpacing: 1.5, color: '#fff' }}>SPECTR</span>
          <span style={{ opacity: 0.4 }}>·</span>
          <span style={{ opacity: 0.55 }}>ZOOMABLE FILTER BANK</span>
        </div>

        <div style={{ flex: 1 }} />

        {/* Mode pill */}
        <Segmented
          value={settings.motionMode}
          onChange={(v) => setSettings(s => ({ ...s, motionMode: v }))}
          options={[['live', 'LIVE'], ['precision', 'PRECISION']]}
        />

        <div style={{ width: 1, height: 20, background: 'rgba(255,255,255,0.08)' }} />

        {/* DSP pill — now functional */}
        <Segmented
          value={dspMode}
          onChange={setDspMode}
          options={[['iir', 'IIR'], ['fft', 'FFT'], ['hybrid', 'HYBRID']]}
        />

        <div style={{ width: 1, height: 20, background: 'rgba(255,255,255,0.08)' }} />

        <div style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 10, color: 'rgba(255,255,255,0.45)' }}>
          {/* Bands: click-to-edit popover */}
          <div style={{ position: 'relative' }}>
            <button onClick={() => setBandsMenu(v => !v)} style={{
              background: bandsMenu ? 'rgba(255,255,255,0.08)' : 'transparent',
              border: '1px solid ' + (bandsMenu ? 'rgba(255,255,255,0.18)' : 'rgba(255,255,255,0.08)'),
              color: 'rgba(255,255,255,0.7)',
              padding: '2px 7px', borderRadius: 3,
              fontFamily: 'var(--mono)', fontSize: 10, letterSpacing: 0.5,
              cursor: 'pointer',
            }} title="Click to change band count">
              <span className="tnum">{info.N}</span> bands ▾
            </button>
            {bandsMenu && (
              <div style={{
                position: 'absolute', top: 28, right: 0, zIndex: 20,
                background: 'rgba(12,16,22,0.96)',
                border: '1px solid rgba(255,255,255,0.12)',
                borderRadius: 4, padding: 3, display: 'flex', gap: 2,
                backdropFilter: 'blur(10px)',
              }}>
                {[32, 40, 48, 56, 64].map(n => (
                  <button key={n}
                    onClick={() => {
                      setSettings(s => ({ ...s, bandCount: n }));
                      try { window.parent.postMessage({ type: '__edit_mode_set_keys', edits: { bandCount: n } }, '*'); } catch {}
                      setBandsMenu(false);
                    }}
                    style={{
                      background: info.N === n ? 'rgba(120,180,255,0.18)' : 'transparent',
                      border: '1px solid ' + (info.N === n ? 'rgba(180,210,255,0.4)' : 'rgba(255,255,255,0.06)'),
                      color: '#fff', padding: '4px 8px', borderRadius: 2,
                      fontFamily: 'var(--mono)', fontSize: 10, cursor: 'pointer',
                      minWidth: 28,
                    }}>{n}</button>
                ))}
              </div>
            )}
          </div>
          <span>·</span>
          <span className="tnum">{info.zoom}× zoom</span>
        </div>
      </div>

      {/* Status banner */}
      <StatusBanner message={status} />

      {/* Settings modal */}
      {settingsOpen && (
        <SettingsModal
          settings={settings}
          setSettings={setSettings}
          onClose={() => setSettingsOpen(false)}
        />
      )}

      {/* Bottom action rail */}
      <div style={{
        position: 'absolute', bottom: 0, left: 0, right: 0, height: 56,
        display: 'flex', alignItems: 'center', gap: 8,
        padding: '0 20px',
        borderTop: '1px solid rgba(255,255,255,0.06)',
        background: 'linear-gradient(to top, rgba(10,14,20,0.8), rgba(10,14,20,0.0))',
        zIndex: 5, pointerEvents: 'auto',
        fontFamily: 'var(--mono)',
        fontSize: 10.5,
        color: 'rgba(255,255,255,0.7)',
      }}>
        <RailBtn onClick={onClearAll}>CLEAR</RailBtn>
        <div style={{ position: 'relative' }}>
          <RailBtn onClick={() => setOverflowMenu(v => !v)} active={overflowMenu}>⋯</RailBtn>
          {overflowMenu && (
            <div style={{
              position: 'absolute', bottom: 34, left: 0,
              background: 'rgba(12,16,22,0.96)',
              border: '1px solid rgba(255,255,255,0.1)',
              borderRadius: 4, padding: 4,
              display: 'flex', flexDirection: 'column', gap: 1,
              minWidth: 200, backdropFilter: 'blur(10px)',
            }}>
              <button onClick={() => { onResetAll(); setOverflowMenu(false); }} style={menuItem}>
                RESET ALL <span style={{ opacity: 0.4, marginLeft: 8 }}>gains · view · snapshots</span>
              </button>
              <button onClick={() => { act(b => b.invert())(); setOverflowMenu(false); }} style={menuItem}>INVERT</button>
              <button onClick={() => { act(b => allMuted ? b.unmuteAll() : b.muteAll())(); setOverflowMenu(false); }} style={menuItem}>
                {allMuted ? 'UNMUTE ALL' : 'MUTE ALL'}
              </button>
              <button onClick={() => { act(b => b.resetView())(); setOverflowMenu(false); }} style={menuItem}>FIT VIEW</button>
            </div>
          )}
        </div>

        <div style={{ width: 1, height: 22, background: 'rgba(255,255,255,0.08)', margin: '0 6px' }} />

        {/* EDIT MODE dropdown */}
        <div style={{ position: 'relative' }}>
          <RailBtn onClick={() => toggleMenu('edit')} active={editMenu}>
            <EditModeGlyph mode={editMode} />
            <span style={{ marginLeft: 6 }}>{editMode.toUpperCase()} ▾</span>
          </RailBtn>
          {editMenu && (
            <EditModePopover
              value={editMode}
              onChange={(v) => { setEditMode(v); setOpenMenu(null); }}
              onClose={() => setOpenMenu(null)}
            />
          )}
        </div>

        {/* ANALYZER dropdown */}
        <div style={{ position: 'relative' }}>
          <RailBtn onClick={() => toggleMenu('analyzer')} active={analyzerMenu}>
            <AnalyzerGlyph mode={analyzerMode} />
            <span style={{ marginLeft: 6 }}>{analyzerMode.toUpperCase()} ▾</span>
          </RailBtn>
          {analyzerMenu && (
            <AnalyzerPopover
              value={analyzerMode}
              onChange={(v) => { setAnalyzerMode(v); setOpenMenu(null); }}
            />
          )}
        </div>

        <div style={{ width: 1, height: 22, background: 'rgba(255,255,255,0.08)', margin: '0 6px' }} />

        <div style={{ position: 'relative' }}>
          <RailBtn onClick={() => setPatternMenu(v => !v)} active={patternMenu}>
            <svg width="18" height="13" viewBox="0 0 24 16" style={{ flex: 'none', verticalAlign: 'middle' }}>
              {/* Three stacked response curves — "a library of shapes" */}
              <path d="M 2 13 Q 7 13 10 9 Q 13 5 17 5 Q 20 5 22 7"
                stroke="currentColor" strokeWidth="1.3" fill="none" strokeLinecap="round" opacity="0.45" />
              <path d="M 2 11 Q 6 11 9 7 Q 13 3 17 7 Q 20 10 22 10"
                stroke="currentColor" strokeWidth="1.3" fill="none" strokeLinecap="round" opacity="0.7" />
              <path d="M 2 9 Q 5 4 9 10 Q 13 15 17 9 Q 20 5 22 6"
                stroke="currentColor" strokeWidth="1.4" fill="none" strokeLinecap="round" />
            </svg>
            <span style={{ marginLeft: 6 }}>PRESETS ▾</span>
          </RailBtn>
          {patternMenu && (
            <div style={{
              position: 'absolute', bottom: 34, left: 0,
              background: 'rgba(12,16,22,0.96)',
              border: '1px solid rgba(255,255,255,0.1)',
              borderRadius: 4, padding: 4,
              display: 'flex', flexDirection: 'column', gap: 1,
              minWidth: 220, maxHeight: 380, overflowY: 'auto',
              backdropFilter: 'blur(10px)',
            }}>
              <div style={{ padding: '6px 10px 4px', fontSize: 8.5, letterSpacing: 2, opacity: 0.4 }}>FACTORY</div>
              {(window.Spectr?.FACTORY_PATTERNS || []).map(p => (
                <button key={p.id} onClick={() => { onApplyPattern(p); setPatternMenu(false); }}
                  style={menuItem}>
                  {patterns?.defaultId === p.id && <span style={{ color: 'hsl(50,90%,65%)', marginRight: 4 }}>★</span>}
                  {p.name}
                </button>
              ))}
              {patterns?.user?.length > 0 && (
                <>
                  <div style={{ padding: '8px 10px 4px', fontSize: 8.5, letterSpacing: 2, opacity: 0.4, borderTop: '1px solid rgba(255,255,255,0.06)', marginTop: 4 }}>USER</div>
                  {patterns.user.map(p => (
                    <button key={p.id} onClick={() => { onApplyPattern(p); setPatternMenu(false); }}
                      style={menuItem}>
                      {patterns.defaultId === p.id && <span style={{ color: 'hsl(50,90%,65%)', marginRight: 4 }}>★</span>}
                      {p.name}
                    </button>
                  ))}
                </>
              )}
              <div style={{ borderTop: '1px solid rgba(255,255,255,0.06)', marginTop: 4, paddingTop: 2 }}>
                <button onClick={() => { onOpenPatternManager(); setPatternMenu(false); }}
                  style={{ ...menuItem, color: 'hsl(200,85%,70%)' }}>MANAGE…</button>
              </div>
            </div>
          )}
        </div>

        <div style={{ width: 1, height: 22, background: 'rgba(255,255,255,0.08)', margin: '0 6px' }} />

        <span style={{ opacity: 0.55, fontSize: 10 }}>SNAPSHOT</span>
        <SnapBtn filled={snapshotStatus.A} onClick={act(b => b.snapshot('A'))} capture label="A" />
        <SnapBtn filled={snapshotStatus.B} onClick={act(b => b.snapshot('B'))} capture label="B" />
        <SnapBtn filled={snapshotStatus.A} onClick={act(b => b.recallSnap('A'))} label="▸ A" />
        <SnapBtn filled={snapshotStatus.B} onClick={act(b => b.recallSnap('B'))} label="▸ B" />

        <MorphSlider bankRef={bankRef} hasBoth={snapshotStatus.A && snapshotStatus.B} />

        <div style={{ flex: 1 }} />

        {/* Settings */}
        <button onClick={() => setSettingsOpen(true)}
          title="Settings"
          style={{
            background: 'transparent',
            border: '1px solid rgba(255,255,255,0.12)',
            color: 'rgba(255,255,255,0.6)',
            width: 26, height: 26, borderRadius: 13,
            fontFamily: 'var(--mono)', fontSize: 13, cursor: 'pointer',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            marginRight: 6,
          }}>
          <svg width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.2" strokeLinejoin="round" strokeLinecap="round">
            {/* Proper gear: 8 rectangular teeth around a ring with a center hole */}
            <path d="M 8 1.3 L 9 1.3 L 9 3.0
                     L 11.0 3.7 L 12.2 2.5 L 13.5 3.8 L 12.3 5.0
                     L 13.0 7.0 L 14.7 7.0 L 14.7 9.0 L 13.0 9.0
                     L 12.3 11.0 L 13.5 12.2 L 12.2 13.5 L 11.0 12.3
                     L 9 13.0 L 9 14.7 L 7 14.7 L 7 13.0
                     L 5.0 12.3 L 3.8 13.5 L 2.5 12.2 L 3.7 11.0
                     L 3.0 9.0 L 1.3 9.0 L 1.3 7.0 L 3.0 7.0
                     L 3.7 5.0 L 2.5 3.8 L 3.8 2.5 L 5.0 3.7
                     L 7 3.0 L 7 1.3 Z" />
            <circle cx="8" cy="8" r="2.1" />
          </svg>
        </button>

        {/* Help affordance */}
        <div style={{ position: 'relative' }}>
          <button onClick={() => setHelpOpen(v => !v)}
            style={{
              background: helpOpen ? 'rgba(255,255,255,0.1)' : 'transparent',
              border: '1px solid rgba(255,255,255,0.12)',
              color: 'rgba(255,255,255,0.6)',
              width: 26, height: 26, borderRadius: 13,
              fontFamily: 'var(--mono)', fontSize: 11, cursor: 'pointer',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
            }}
            title="Shortcuts">?</button>
          {helpOpen && <HelpPopover onClose={() => setHelpOpen(false)} />}
        </div>
      </div>
    </>
  );
}

const EDIT_MODES = [
  { k: 'sculpt', label: 'SCULPT', hint: 'S', icon: 'M 2 14 Q 5 4 9 10 T 16 8 T 22 6',
    tagline: 'Free-draw',
    desc: 'Drag vertically on a band to set its gain directly. The sharpest, most direct tool — your pointer IS the band height. Horizontal drags paint across bands, each one snapping to the Y of your cursor at that column.' },
  { k: 'level',  label: 'LEVEL',  hint: 'L', icon: 'M 2 10 L 22 10',
    tagline: 'Flatten',
    desc: 'Paint a horizontal floor. All bands you drag over become the same gain — whichever Y you click first. Great for carving a flat shelf across a frequency range.' },
  { k: 'boost',  label: 'BOOST',  hint: 'B', icon: 'M 2 14 L 6 12 L 10 9 L 14 6 L 18 4 L 22 3',
    tagline: 'Scale curve',
    desc: 'Multiplies the existing shape instead of replacing it. Drag up to amplify whatever curve you already drew; drag down to flatten it toward 0 dB. Keeps the character, changes the intensity.' },
  { k: 'flare',  label: 'FLARE',  hint: 'F', icon: 'M 2 12 Q 6 2 10 12 Q 14 22 18 12 Q 20 6 22 12',
    tagline: 'Push outward',
    desc: 'Pushes bands away from 0 dB: positive gains go higher, negative go lower. Exaggerates whatever contour exists — makes peaks sharper and cuts deeper in one gesture.' },
  { k: 'glide',  label: 'GLIDE',  hint: 'G', icon: 'M 2 12 C 6 4 10 20 14 10 C 18 4 20 14 22 12',
    tagline: 'Smooth',
    desc: 'Blurs band-to-band differences wherever you paint. Neighboring gains are averaged together each frame, smoothing jagged curves into gentle slopes. Hold longer for more smoothing.' },
];

const ANALYZER_OPTS = [
  // Analyzer icons use a "bars + overlay" metaphor to visually distinguish them
  // from edit-mode icons (which are pure curves). PEAK = ticks hovering above
  // bars (peak-hold indicators); AVG = smooth envelope hugging bars; BOTH =
  // ticks + envelope; OFF = bars with a diagonal strike-through.
  { k: 'peak', label: 'PEAK', color: 'rgba(140,230,170,0.9)',
    icon: 'M 3 20 L 3 14 M 7 20 L 7 10 M 11 20 L 11 6 M 15 20 L 15 12 M 19 20 L 19 8 M 1 14 L 5 14 M 5 10 L 9 10 M 9 6 L 13 6 M 13 12 L 17 12 M 17 8 L 21 8',
    desc: 'Instantaneous peak line — fast response, shows transients.' },
  { k: 'avg',  label: 'AVG',  color: 'rgba(120,180,240,0.9)',
    icon: 'M 3 20 L 3 15 M 7 20 L 7 12 M 11 20 L 11 8 M 15 20 L 15 11 M 19 20 L 19 10 M 1 16 Q 5 13 9 10 Q 13 7 17 10 Q 20 12 22 11',
    desc: 'Rolling average — slow response, shows sustained energy.' },
  { k: 'both', label: 'BOTH', color: 'rgba(220,220,220,0.9)',
    icon: 'M 3 20 L 3 14 M 7 20 L 7 10 M 11 20 L 11 6 M 15 20 L 15 12 M 19 20 L 19 8 M 1 14 L 5 14 M 5 10 L 9 10 M 9 6 L 13 6 M 13 12 L 17 12 M 17 8 L 21 8 M 1 17 Q 5 14 9 11 Q 13 8 17 11 Q 20 13 22 12',
    desc: 'Overlay both lines so you can see transients vs. body.' },
  { k: 'off',  label: 'OFF',  color: 'rgba(255,255,255,0.3)',
    icon: 'M 3 20 L 3 15 M 7 20 L 7 12 M 11 20 L 11 9 M 15 20 L 15 13 M 19 20 L 19 11 M 2 4 L 22 22',
    desc: 'Hide analyzer overlay entirely — just show bands.' },
];

function EditModeGlyph({ mode }) {
  const m = EDIT_MODES.find(x => x.k === mode) || EDIT_MODES[0];
  return (
    <svg width="22" height="16" viewBox="0 0 24 24" style={{ flex: 'none', verticalAlign: 'middle' }}>
      <path d={m.icon} stroke="currentColor" strokeWidth="1.4" fill="none" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

function AnalyzerGlyph({ mode }) {
  const m = ANALYZER_OPTS.find(x => x.k === mode) || ANALYZER_OPTS[0];
  return (
    <svg width="22" height="16" viewBox="0 0 24 24" style={{ flex: 'none', verticalAlign: 'middle' }}>
      <path d={m.icon} stroke="currentColor" strokeWidth="1.3" fill="none" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

function EditModePopover({ value, onChange, onClose }) {
  return (
    <div style={{
      position: 'absolute', bottom: 34, left: 0,
      background: 'rgba(12,16,22,0.96)',
      border: '1px solid rgba(255,255,255,0.1)',
      borderRadius: 4, padding: 6,
      display: 'flex', flexDirection: 'column', gap: 2,
      width: 280,
      // pulp's Yoga doesn't propagate flex-chain widths through nested
      // spans the way browsers do, so inner text overflows the panel.
      // Clip at the container boundary; description spans below carry
      // explicit width so they wrap within the panel.
      overflow: 'hidden',
      backdropFilter: 'blur(10px)',
      boxShadow: '0 8px 30px rgba(0,0,0,0.5)',
    }}>
      <div style={{ fontSize: 8.5, letterSpacing: 2, opacity: 0.45, padding: '4px 8px 6px' }}>EDIT MODE · how dragging affects bands</div>
      {EDIT_MODES.map(m => {
        const active = value === m.k;
        return (
          <button key={m.k}
            onClick={() => onChange(m.k)}
            style={{
              background: active ? 'rgba(120,180,255,0.14)' : 'transparent',
              border: '1px solid ' + (active ? 'rgba(180,210,255,0.4)' : 'transparent'),
              color: active ? '#fff' : 'rgba(255,255,255,0.82)',
              padding: '8px 10px', borderRadius: 3, cursor: 'pointer',
              display: 'flex', alignItems: 'flex-start', gap: 10,
              fontFamily: 'var(--mono)', fontSize: 10, letterSpacing: 0.5,
              textAlign: 'left',
            }}>
            <svg width="28" height="20" viewBox="0 0 24 24" style={{ flex: 'none', marginTop: 2 }}>
              <path d={m.icon}
                stroke={active ? 'hsl(200,85%,75%)' : 'rgba(255,255,255,0.55)'}
                strokeWidth="1.4" fill="none" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
            <span style={{ flex: 1 }}>
              <span style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 3 }}>
                <span style={{ letterSpacing: 1.5, fontWeight: 600 }}>{m.label}</span>
                <span style={{ opacity: 0.5, fontSize: 9 }}>· {m.tagline}</span>
                <span style={{ flex: 1 }} />
                <span style={{
                  fontSize: 8.5, opacity: 0.5, padding: '1px 5px',
                  border: '1px solid rgba(255,255,255,0.14)', borderRadius: 2,
                }}>{m.hint}</span>
              </span>
              <span style={{
                display: 'block', fontSize: 9.5, opacity: 0.6,
                lineHeight: 1.5, fontFamily: 'var(--sans)',
                textTransform: 'none', letterSpacing: 0.1,
                // Explicit width so pulp's Yoga wraps text instead of
                // overflowing the panel. 230 = panel 280 − padding 16
                // − icon 28 − gap 10 − a few px slack.
                width: 230,
              }}>{m.desc}</span>
            </span>
          </button>
        );
      })}
    </div>
  );
}

function AnalyzerPopover({ value, onChange }) {
  return (
    <div style={{
      position: 'absolute', bottom: 34, left: 0,
      background: 'rgba(12,16,22,0.96)',
      border: '1px solid rgba(255,255,255,0.1)',
      borderRadius: 4, padding: 6,
      display: 'flex', flexDirection: 'column', gap: 2,
      width: 260,
      overflow: 'hidden',
      backdropFilter: 'blur(10px)',
      boxShadow: '0 8px 30px rgba(0,0,0,0.5)',
    }}>
      <div style={{ fontSize: 8.5, letterSpacing: 2, opacity: 0.45, padding: '4px 8px 6px' }}>ANALYZER · A to cycle</div>
      {ANALYZER_OPTS.map(a => {
        const active = value === a.k;
        return (
          <button key={a.k}
            onClick={() => onChange(a.k)}
            style={{
              background: active ? 'rgba(255,255,255,0.08)' : 'transparent',
              border: '1px solid ' + (active ? 'rgba(255,255,255,0.2)' : 'transparent'),
              color: active ? a.color : 'rgba(255,255,255,0.82)',
              padding: '7px 10px', borderRadius: 3, cursor: 'pointer',
              display: 'block', textAlign: 'left',
              fontFamily: 'var(--mono)', fontSize: 10, letterSpacing: 0.5,
            }}>
            <span style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <span style={{
                display: 'inline-block', width: 18, height: 2,
                background: a.color, borderRadius: 1,
              }} />
              <span style={{ letterSpacing: 1.5, fontWeight: 600 }}>{a.label}</span>
            </span>
            <span style={{
              display: 'block', fontSize: 9.5, opacity: 0.6, marginTop: 3,
              fontFamily: 'var(--sans)', letterSpacing: 0.1,
              // Explicit width so pulp's Yoga wraps text within panel.
              // 240 = panel 260 − padding 20 (left+right).
              width: 240,
            }}>{a.desc}</span>
          </button>
        );
      })}
    </div>
  );
}

// Swatch: renders a small gradient pill from a theme's preview stops.
function ThemeSwatch({ stops, w = 48, h = 14, radius = 2 }) {
  const gradient = `linear-gradient(to right, ${stops.join(', ')})`;
  return (
    <span style={{
      display: 'inline-block', width: w, height: h,
      background: gradient,
      borderRadius: radius,
      border: '1px solid rgba(255,255,255,0.15)',
      flex: 'none',
    }} />
  );
}

// Tiny canvas mini-band preview for a metaphor — 3 mocked bands with rising heights.
function MetaphorPreview({ metaKey, w = 48, h = 24 }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const c = ref.current; if (!c) return;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    c.width = w * dpr; c.height = h * dpr;
    c.style.width = w + 'px'; c.style.height = h + 'px';
    const ctx = c.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    const meta = (window.SpectrMetaphors || []).find(x => x.k === metaKey);
    if (!meta) return;
    const bw = 9, gap = 3;
    const heights = [7, 15, 10];
    const colors = ['hsl(220,70%,65%)', 'hsl(200,75%,66%)', 'hsl(180,75%,64%)'];
    for (let i = 0; i < 3; i++) {
      const x = 4 + i * (bw + gap);
      const bh = heights[i];
      const y = h - 4 - bh;
      ctx.save();
      ctx.fillStyle = colors[i];
      ctx.strokeStyle = colors[i];
      meta.draw(ctx, x, y, bw, bh);
      ctx.restore();
    }
  }, [metaKey, w, h]);
  return <canvas ref={ref} style={{ flex: 'none', borderRadius: 2, border: '1px solid rgba(255,255,255,0.1)', background: 'rgba(0,0,0,0.35)' }} />;
}

// Generic dropdown picker — label + preview (swatch or canvas) + rich list.
function PickerDropdown({ value, options, onChange, placeholder, renderPreview, renderOption, width = 260 }) {
  const [open, setOpen] = React.useState(false);
  const ref = React.useRef(null);
  React.useEffect(() => {
    if (!open) return;
    const onDoc = (e) => { if (ref.current && !ref.current.contains(e.target)) setOpen(false); };
    document.addEventListener('mousedown', onDoc);
    return () => document.removeEventListener('mousedown', onDoc);
  }, [open]);
  const current = options.find(o => o.k === value) || options[0];
  return (
    <div ref={ref} style={{ position: 'relative', width }}>
      <button onClick={() => setOpen(v => !v)} style={{
        width: '100%',
        background: open ? 'rgba(120,180,255,0.14)' : 'rgba(255,255,255,0.04)',
        border: '1px solid ' + (open ? 'rgba(180,210,255,0.4)' : 'rgba(255,255,255,0.12)'),
        color: '#fff', padding: '6px 10px', borderRadius: 3,
        fontFamily: 'var(--mono)', fontSize: 10.5, letterSpacing: 0.5,
        cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 10,
        textAlign: 'left',
      }}>
        {renderPreview && current && renderPreview(current)}
        <span style={{ flex: 1, textAlign: 'left' }}>{current?.label ?? placeholder}</span>
        <span style={{ opacity: 0.5 }}>▾</span>
      </button>
      {open && (
        <div style={{
          position: 'absolute', top: 'calc(100% + 4px)', left: 0, right: 0,
          zIndex: 40,
          maxHeight: 360, overflowY: 'auto',
          background: 'rgba(12,16,22,0.98)',
          border: '1px solid rgba(255,255,255,0.14)',
          borderRadius: 4, padding: 4,
          display: 'flex', flexDirection: 'column', gap: 1,
          boxShadow: '0 14px 40px rgba(0,0,0,0.6)',
          backdropFilter: 'blur(12px)',
        }}>
          {options.map(o => {
            const active = o.k === value;
            return (
              <button key={o.k}
                onClick={() => { onChange(o.k); setOpen(false); }}
                style={{
                  background: active ? 'rgba(120,180,255,0.16)' : 'transparent',
                  border: '1px solid ' + (active ? 'rgba(180,210,255,0.35)' : 'transparent'),
                  color: active ? '#fff' : 'rgba(255,255,255,0.85)',
                  padding: '7px 8px', borderRadius: 3, cursor: 'pointer',
                  display: 'flex', alignItems: 'center', gap: 10,
                  fontFamily: 'var(--mono)', fontSize: 10.5, letterSpacing: 0.3,
                  textAlign: 'left',
                }}>
                {renderOption ? renderOption(o, active) : (
                  <>
                    {renderPreview && renderPreview(o)}
                    <span style={{ flex: 1 }}>{o.label}</span>
                  </>
                )}
              </button>
            );
          })}
        </div>
      )}
    </div>
  );
}

function ThemeDropdown({ value, onChange }) {
  const options = (window.SpectrThemes || []);
  return (
    <PickerDropdown
      value={value}
      options={options}
      onChange={onChange}
      width={260}
      renderPreview={(o) => <ThemeSwatch stops={o.stops} />}
      renderOption={(o, active) => (
        <>
          <ThemeSwatch stops={o.stops} w={40} h={14} />
          <span style={{ flex: 1 }}>
            <span style={{ fontWeight: 600, letterSpacing: 1 }}>{o.label}</span>
            <span style={{
              display: 'block', fontSize: 9.5, opacity: 0.55, marginTop: 2,
              fontFamily: 'var(--sans)', letterSpacing: 0.1,
            }}>{o.desc}</span>
          </span>
        </>
      )}
    />
  );
}

function MetaphorDropdown({ value, onChange }) {
  const options = (window.SpectrMetaphors || []);
  return (
    <PickerDropdown
      value={value}
      options={options}
      onChange={onChange}
      width={260}
      renderPreview={(o) => <MetaphorPreview metaKey={o.k} w={38} h={20} />}
      renderOption={(o, active) => (
        <>
          <MetaphorPreview metaKey={o.k} w={44} h={24} />
          <span style={{ flex: 1 }}>
            <span style={{ fontWeight: 600, letterSpacing: 1 }}>{o.label}</span>
            <span style={{
              display: 'block', fontSize: 9.5, opacity: 0.55, marginTop: 2,
              fontFamily: 'var(--sans)', letterSpacing: 0.1,
            }}>{o.desc}</span>
          </span>
        </>
      )}
    />
  );
}

function SettingsModal({ settings, setSettings, onClose }) {
  const persist = (patch) => {
    setSettings(s => ({ ...s, ...patch }));
    try { window.parent.postMessage({ type: '__edit_mode_set_keys', edits: patch }, '*'); } catch {}
  };
  const Group = ({ title, subtitle, children }) => (
    <div style={{ marginBottom: 22 }}>
      <div style={{ fontSize: 9, letterSpacing: 2, opacity: 0.5, marginBottom: 4 }}>{title}</div>
      {subtitle && <div style={{ fontSize: 10, opacity: 0.45, marginBottom: 10, fontFamily: 'var(--sans)' }}>{subtitle}</div>}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>{children}</div>
    </div>
  );
  const Field = ({ label, hint, children }) => (
    <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
      <div style={{ width: 110, flexShrink: 0 }}>
        <div style={{ fontSize: 11, color: '#fff', letterSpacing: 0.5 }}>{label}</div>
        {hint && <div style={{ fontSize: 9.5, opacity: 0.45, marginTop: 2, fontFamily: 'var(--sans)', letterSpacing: 0.1 }}>{hint}</div>}
      </div>
      <div style={{ flex: 1, display: 'flex', justifyContent: 'flex-end' }}>{children}</div>
    </div>
  );
  const SChips = ({ value, onChange, opts }) => (
    <div style={{ display: 'flex', gap: 3 }}>
      {opts.map(([k, label]) => (
        <button key={k} onClick={() => onChange(k)} style={{
          background: value === k ? 'rgba(120,180,255,0.18)' : 'rgba(255,255,255,0.03)',
          color: value === k ? '#fff' : 'rgba(255,255,255,0.7)',
          border: '1px solid ' + (value === k ? 'rgba(180,210,255,0.4)' : 'rgba(255,255,255,0.1)'),
          padding: '5px 10px', fontSize: 10, letterSpacing: 0.8,
          fontFamily: 'var(--mono)', cursor: 'pointer', borderRadius: 3,
        }}>{label}</button>
      ))}
    </div>
  );
  const SSlider = ({ value, min, max, step, onChange, fmt }) => (
    <div style={{ display: 'flex', alignItems: 'center', gap: 8, width: 200 }}>
      <input type="range" min={min} max={max} step={step} value={value}
        onChange={e => onChange(parseFloat(e.target.value))}
        style={{ flex: 1, accentColor: 'hsl(200,80%,60%)' }} />
      <span className="tnum" style={{ width: 36, textAlign: 'right', fontSize: 10, opacity: 0.75 }}>
        {fmt ? fmt(value) : value.toFixed(2)}
      </span>
    </div>
  );
  const STog = ({ value, onChange }) => (
    <button onClick={() => onChange(!value)} style={{
      width: 40, height: 20, borderRadius: 11,
      background: value ? 'hsl(200,70%,45%)' : 'rgba(255,255,255,0.1)',
      border: '1px solid rgba(255,255,255,0.12)',
      position: 'relative', cursor: 'pointer', padding: 0,
    }}>
      <span style={{
        position: 'absolute', top: 1, left: value ? 21 : 1,
        width: 16, height: 16, borderRadius: 8,
        background: '#fff', transition: 'left 0.15s',
      }} />
    </button>
  );

  return (
    <div onClick={onClose} style={{
      position: 'absolute', inset: 0, zIndex: 50,
      background: 'rgba(0,0,0,0.55)', backdropFilter: 'blur(3px)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      padding: 40,
    }}>
      <div onClick={e => e.stopPropagation()} style={{
        width: 520, maxHeight: '90vh', overflowY: 'auto',
        background: 'rgba(14,18,25,0.98)',
        border: '1px solid rgba(255,255,255,0.1)',
        borderRadius: 8, padding: 26,
        fontFamily: 'var(--mono)', fontSize: 10.5,
        color: 'rgba(255,255,255,0.9)',
        boxShadow: '0 30px 80px rgba(0,0,0,0.6)',
      }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 22 }}>
          <div>
            <div style={{ fontSize: 14, letterSpacing: 2, fontWeight: 600 }}>SETTINGS</div>
          </div>
          <button onClick={onClose} style={{
            background: 'transparent', border: 'none', color: 'rgba(255,255,255,0.6)',
            cursor: 'pointer', fontSize: 20, padding: 0, lineHeight: 1,
          }}>×</button>
        </div>

        <Group title="APPEARANCE" subtitle="How bands and colors render.">
          <Field label="Theme" hint="Color palette for bands and glow">
            <SChips value={settings.theme} onChange={v => persist({ theme: v })}
              opts={[['spectral', 'Spectral'], ['mono', 'Mono'], ['cool', 'Cool'], ['warm', 'Warm']]} />
          </Field>
          <Field label="Metaphor" hint="Visual shape of each band">
            <SChips value={settings.metaphor} onChange={v => persist({ metaphor: v })}
              opts={[['columns', 'Columns'], ['liquid', 'Liquid'], ['shards', 'Shards']]} />
          </Field>
          <Field label="Bloom" hint="Halo intensity when bands react to signal">
            <SSlider value={settings.bloom} min={0} max={1} step={0.01}
              onChange={v => persist({ bloom: v })} />
          </Field>
          <Field label="Spectrum" hint="Analyzer trace opacity">
            <SSlider value={settings.spectrumIntensity} min={0} max={1} step={0.01}
              onChange={v => persist({ spectrumIntensity: v })} />
          </Field>
        </Group>

        <Group title="STRUCTURE" subtitle="Band count, mute behavior, chrome.">
          <Field label="Bands" hint="Number of filter bands across the range">
            <SChips value={String(settings.bandCount)} onChange={v => persist({ bandCount: parseInt(v, 10) })}
              opts={[['32', '32'], ['40', '40'], ['48', '48'], ['56', '56'], ['64', '64']]} />
          </Field>
          <Field label="Mute style" hint="How muted bands render">
            <SChips value={settings.muteStyle} onChange={v => persist({ muteStyle: v })}
              opts={[['cutout', 'Cutout'], ['collapse', 'Collapse']]} />
          </Field>
          <Field label="Minimap" hint="Bottom strip showing full-range viewport">
            <STog value={settings.showMinimap} onChange={v => persist({ showMinimap: v })} />
          </Field>
          <Field label="Rulers" hint="Frequency labels along the bottom axis">
            <STog value={settings.showRulers} onChange={v => persist({ showRulers: v })} />
          </Field>
        </Group>

        <Group title="MOTION" subtitle="How gain changes smooth over time.">
          <Field label="Response" hint="Live = snappy, Precision = eased">
            <SChips value={settings.motionMode} onChange={v => persist({ motionMode: v })}
              opts={[['live', 'Live'], ['precision', 'Precision']]} />
          </Field>
        </Group>
      </div>
    </div>
  );
}

function StatusBanner({ message }) {
  const [visible, setVisible] = useStateChrome(false);
  const [text, setText] = useStateChrome('');
  useEffectChrome(() => {
    if (!message) return;
    const display = message.split('|')[0];
    setText(display);
    setVisible(true);
    const t = setTimeout(() => setVisible(false), 1400);
    return () => clearTimeout(t);
  }, [message]);
  return (
    <div style={{
      position: 'absolute', top: 60, left: '50%', transform: 'translateX(-50%)',
      padding: '6px 14px',
      background: 'rgba(12,16,22,0.92)',
      border: '1px solid rgba(180,210,255,0.3)',
      borderRadius: 3,
      fontFamily: 'var(--mono)', fontSize: 10, letterSpacing: 1.5,
      color: 'rgba(200,220,255,0.95)',
      zIndex: 6, pointerEvents: 'none',
      opacity: visible ? 1 : 0,
      transition: 'opacity 0.2s',
      backdropFilter: 'blur(8px)',
    }}>{text}</div>
  );
}

function SnapBtn({ filled, onClick, capture, label }) {
  const [flash, setFlash] = useStateChrome(false);
  const handle = (e) => {
    setFlash(true);
    setTimeout(() => setFlash(false), 180);
    onClick && onClick(e);
  };
  const isCapture = capture;
  const dotColor = filled ? 'hsl(200,85%,65%)' : 'rgba(255,255,255,0.25)';
  return (
    <button onClick={handle}
      style={{
        background: flash ? 'rgba(180,220,255,0.22)'
          : (filled && !isCapture ? 'rgba(40,80,120,0.22)' : 'rgba(255,255,255,0.03)'),
        border: '1px solid ' + (flash ? 'rgba(200,230,255,0.6)'
          : (filled && !isCapture ? 'rgba(140,190,240,0.35)' : 'rgba(255,255,255,0.08)')),
        color: filled || isCapture ? 'rgba(255,255,255,0.9)' : 'rgba(255,255,255,0.4)',
        padding: '5px 8px', fontFamily: 'var(--mono)', fontSize: 10, letterSpacing: 1,
        borderRadius: 3, cursor: !isCapture && !filled ? 'not-allowed' : 'pointer',
        height: 26, minWidth: 34,
        transition: 'background 0.15s, border-color 0.15s',
        display: 'inline-flex', alignItems: 'center', gap: 4,
        opacity: !isCapture && !filled ? 0.45 : 1,
      }}
      disabled={!isCapture && !filled}
    >
      {isCapture && <span style={{
        width: 6, height: 6, borderRadius: 3, background: dotColor,
        boxShadow: filled ? '0 0 6px hsl(200,85%,65%)' : 'none',
      }} />}
      {label}
    </button>
  );
}

function HelpPopover({ onClose }) {
  return (
    <div style={{
      position: 'absolute', bottom: 34, right: 0, zIndex: 20,
      background: 'rgba(12,16,22,0.96)',
      border: '1px solid rgba(255,255,255,0.12)',
      borderRadius: 4, padding: 14,
      backdropFilter: 'blur(10px)',
      minWidth: 280,
      fontFamily: 'var(--mono)', fontSize: 10, color: 'rgba(255,255,255,0.8)',
      letterSpacing: 0.5, lineHeight: 1.7,
      boxShadow: '0 20px 60px rgba(0,0,0,0.5)',
    }}>
      <div style={{ fontSize: 9, opacity: 0.5, letterSpacing: 2, marginBottom: 8 }}>SHORTCUTS</div>
      <Hrow k="S / L / B">Sculpt · Level · Boost</Hrow>
      <Hrow k="F / G">Flare · Glide</Hrow>
      <Hrow k="A">Cycle analyzer</Hrow>
      <Hrow k="DRAG">Edit bands (mode-dependent)</Hrow>
      <Hrow k="DBL-CLICK">Toggle mute (−∞)</Hrow>
      <Hrow k="RIGHT-CLICK">Band context menu</Hrow>
      <Hrow k="SCROLL">Zoom frequency view</Hrow>
      <Hrow k="ALT+DRAG">Pan viewport</Hrow>
      <Hrow k="⌘+DRAG">Marquee select</Hrow>
      <Hrow k="⇧+CLICK">Add/remove from selection</Hrow>
      <Hrow k="DRAG SEL">Group move</Hrow>
    </div>
  );
}

function Hrow({ k, children }) {
  return (
    <div style={{ display: 'flex', gap: 10, alignItems: 'center', padding: '2px 0' }}>
      <span style={{
        background: 'rgba(255,255,255,0.06)',
        border: '1px solid rgba(255,255,255,0.12)',
        padding: '1px 6px', borderRadius: 2,
        fontSize: 9, minWidth: 84, textAlign: 'center',
        whiteSpace: 'nowrap',
        opacity: 0.9,
      }}>{k}</span>
      <span style={{ opacity: 0.75 }}>{children}</span>
    </div>
  );
}

const menuItem = {
  background: 'transparent', border: 'none', color: 'rgba(255,255,255,0.85)',
  fontFamily: 'var(--mono)', fontSize: 10.5, letterSpacing: 0.8,
  padding: '7px 10px', textAlign: 'left', cursor: 'pointer',
  borderRadius: 2,
};

function Segmented({ value, onChange, options }) {
  return (
    <div style={{
      display: 'flex', border: '1px solid rgba(255,255,255,0.1)',
      borderRadius: 3, overflow: 'hidden', height: 24,
    }}>
      {options.map(([k, label], i) => (
        <button key={k}
          onClick={() => onChange(k)}
          style={{
            background: value === k ? 'rgba(255,255,255,0.10)' : 'transparent',
            color: value === k ? '#fff' : 'rgba(255,255,255,0.55)',
            border: 'none',
            borderLeft: i === 0 ? 'none' : '1px solid rgba(255,255,255,0.08)',
            padding: '0 10px', fontSize: 10, letterSpacing: 1,
            fontFamily: 'var(--mono)', cursor: 'pointer',
          }}>
          {label}
        </button>
      ))}
    </div>
  );
}

function RailBtn({ children, onClick, active }) {
  const [flash, setFlash] = useStateChrome(false);
  const handle = (e) => {
    setFlash(true);
    setTimeout(() => setFlash(false), 180);
    onClick && onClick(e);
  };
  return (
    <button onClick={handle}
      style={{
        background: flash ? 'rgba(180,220,255,0.22)' : (active ? 'rgba(255,255,255,0.10)' : 'rgba(255,255,255,0.03)'),
        border: '1px solid ' + (flash ? 'rgba(200,230,255,0.6)' : 'rgba(255,255,255,0.08)'),
        color: 'rgba(255,255,255,0.85)',
        padding: '5px 10px',
        fontFamily: 'var(--mono)', fontSize: 10, letterSpacing: 1,
        borderRadius: 3, cursor: 'pointer',
        height: 26,
        transition: 'background 0.15s, border-color 0.15s',
      }}
    >{children}</button>
  );
}

function MorphSlider({ bankRef, hasBoth }) {
  const [v, setV] = useStateChrome(0);
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginLeft: 6, opacity: hasBoth ? 1 : 0.35 }}>
      <span style={{ fontSize: 9, color: 'rgba(255,255,255,0.5)' }}>A</span>
      <input type="range" min="0" max="1" step="0.001" value={v}
        disabled={!hasBoth}
        onChange={e => {
          const val = parseFloat(e.target.value);
          setV(val);
          if (bankRef.current) bankRef.current.setMorph(val);
        }}
        style={{ width: 90, accentColor: 'hsl(200,80%,60%)', cursor: hasBoth ? 'pointer' : 'not-allowed' }}
      />
      <span style={{ fontSize: 9, color: 'rgba(255,255,255,0.5)' }}>B</span>
    </div>
  );
}

window.Chrome = Chrome;



// ===== inner script 5 =====

// Tweaks panel — floating, toggled by host via __activate_edit_mode.
// Grouped: Visual / Structure / Motion.

const { useState: useTS, useEffect: useTE } = React;

function TweaksPanel({ settings, setSettings }) {
  const [open, setOpen] = useTS(false);

  useTE(() => {
    function onMsg(e) {
      const d = e.data || {};
      if (d.type === '__activate_edit_mode') setOpen(true);
      if (d.type === '__deactivate_edit_mode') setOpen(false);
    }
    window.addEventListener('message', onMsg);
    try {
      window.parent.postMessage({ type: '__edit_mode_available' }, '*');
    } catch {}
    return () => window.removeEventListener('message', onMsg);
  }, []);

  const persist = (patch) => {
    setSettings(s => ({ ...s, ...patch }));
    try {
      window.parent.postMessage({ type: '__edit_mode_set_keys', edits: patch }, '*');
    } catch {}
  };

  if (!open) return null;

  return (
    <div style={{
      position: 'absolute', bottom: 72, right: 16,
      width: 280, zIndex: 10,
      background: 'rgba(12,16,22,0.94)',
      border: '1px solid rgba(255,255,255,0.1)',
      borderRadius: 6,
      padding: 14,
      fontFamily: 'var(--mono)',
      fontSize: 10.5,
      color: 'rgba(255,255,255,0.85)',
      letterSpacing: 0.5,
      backdropFilter: 'blur(12px)',
      boxShadow: '0 20px 60px rgba(0,0,0,0.5)',
    }}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 10 }}>
        <span style={{ fontWeight: 600, letterSpacing: 1.5 }}>TWEAKS</span>
        <button onClick={() => setOpen(false)} style={{
          background: 'transparent', border: 'none', color: 'rgba(255,255,255,0.5)',
          cursor: 'pointer', fontSize: 14, padding: 0,
        }}>×</button>
      </div>

      <Section label="VISUAL">
        <Row label="Theme">
          <Chips value={settings.theme} onChange={v => persist({ theme: v })}
            opts={[['mono', 'Mono'], ['spectral', 'Spectral'], ['cool', 'Cool'], ['warm', 'Warm']]} />
        </Row>
        <Row label="Metaphor">
          <Chips value={settings.metaphor} onChange={v => persist({ metaphor: v })}
            opts={[['columns', 'Columns'], ['liquid', 'Liquid'], ['shards', 'Shards']]} />
        </Row>
        <Row label="Bloom">
          <Slider value={settings.bloom} min={0} max={1} step={0.01}
            onChange={v => persist({ bloom: v })} />
        </Row>
        <Row label="Spectrum">
          <Slider value={settings.spectrumIntensity} min={0} max={1} step={0.01}
            onChange={v => persist({ spectrumIntensity: v })} />
        </Row>
      </Section>

      <Section label="STRUCTURE">
        <Row label="Bands">
          <Chips value={String(settings.bandCount)} onChange={v => persist({ bandCount: parseInt(v, 10) })}
            opts={[['32', '32'], ['40', '40'], ['48', '48'], ['56', '56'], ['64', '64']]} />
        </Row>
        <Row label="Mute">
          <Chips value={settings.muteStyle} onChange={v => persist({ muteStyle: v })}
            opts={[['cutout', 'Cutout'], ['collapse', 'Collapse']]} />
        </Row>
        <Row label="Minimap">
          <Toggle value={settings.showMinimap} onChange={v => persist({ showMinimap: v })} />
        </Row>
        <Row label="Rulers">
          <Toggle value={settings.showRulers} onChange={v => persist({ showRulers: v })} />
        </Row>
      </Section>

      <Section label="MOTION">
        <Row label="Mode">
          <Chips value={settings.motionMode} onChange={v => persist({ motionMode: v })}
            opts={[['live', 'Live'], ['precision', 'Precision']]} />
        </Row>
      </Section>
    </div>
  );
}

function Section({ label, children }) {
  return (
    <div style={{ marginBottom: 14 }}>
      <div style={{
        fontSize: 9, opacity: 0.45, letterSpacing: 2, marginBottom: 8,
        borderBottom: '1px solid rgba(255,255,255,0.08)', paddingBottom: 4,
      }}>{label}</div>
      {children}
    </div>
  );
}

function Row({ label, children }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', marginBottom: 7, gap: 8 }}>
      <div style={{ width: 70, opacity: 0.6, fontSize: 10 }}>{label}</div>
      <div style={{ flex: 1, display: 'flex', justifyContent: 'flex-end' }}>{children}</div>
    </div>
  );
}

function Chips({ value, onChange, opts }) {
  return (
    <div style={{ display: 'flex', gap: 2, flexWrap: 'wrap', justifyContent: 'flex-end' }}>
      {opts.map(([k, label]) => (
        <button key={k} onClick={() => onChange(k)} style={{
          background: value === k ? 'rgba(255,255,255,0.14)' : 'rgba(255,255,255,0.03)',
          color: value === k ? '#fff' : 'rgba(255,255,255,0.6)',
          border: '1px solid rgba(255,255,255,0.1)',
          padding: '3px 7px', fontSize: 9.5, letterSpacing: 0.5,
          fontFamily: 'var(--mono)', cursor: 'pointer', borderRadius: 2,
        }}>{label}</button>
      ))}
    </div>
  );
}

function Slider({ value, min, max, step, onChange }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 6, width: 160 }}>
      <input type="range" min={min} max={max} step={step} value={value}
        onChange={e => onChange(parseFloat(e.target.value))}
        style={{ flex: 1, accentColor: 'hsl(200,80%,60%)' }} />
      <span className="tnum" style={{ width: 32, textAlign: 'right', fontSize: 9.5, opacity: 0.7 }}>
        {value.toFixed(2)}
      </span>
    </div>
  );
}

function Toggle({ value, onChange }) {
  return (
    <button onClick={() => onChange(!value)} style={{
      width: 36, height: 18, borderRadius: 10,
      background: value ? 'hsl(200,70%,45%)' : 'rgba(255,255,255,0.1)',
      border: '1px solid rgba(255,255,255,0.12)',
      position: 'relative', cursor: 'pointer', padding: 0,
    }}>
      <span style={{
        position: 'absolute', top: 1, left: value ? 19 : 1,
        width: 14, height: 14, borderRadius: 7,
        background: '#fff', transition: 'left 0.15s',
      }} />
    </button>
  );
}

window.TweaksPanel = TweaksPanel;



// ===== inner script 6 =====

// Main app entry.

const { useState: useAppS, useRef: useAppR, useEffect: useAppE, useCallback: useAppC } = React;

function App() {
  const defaultsRaw = document.getElementById('tweak-defaults').textContent
    .replace(/\/\*EDITMODE-BEGIN\*\//g, '')
    .replace(/\/\*EDITMODE-END\*\//g, '')
    .trim();
  const defaults = JSON.parse(defaultsRaw);
  const [settings, setSettings] = useAppS(defaults);
  const bankRef = useAppR(null);

  const [info, setInfo] = useAppS({ N: settings.bandCount, zoom: '1.00' });
  const [dspMode, setDspMode] = useAppS('iir');
  const [editMode, setEditMode] = useAppS('sculpt'); // sculpt|level|boost|flare|glide
  const [analyzerMode, setAnalyzerMode] = useAppS('peak'); // peak|avg|both|off
  const [status, setStatus] = useAppS('');
  const [snapshotStatus, setSnapshotStatus] = useAppS({ A: false, B: false });
  const [managerOpen, setManagerOpen] = useAppS(false);

  // Pattern store
  const [userPatterns, setUserPatterns] = useAppS(() => window.Spectr.loadStore());
  const [defaultId, setDefaultId] = useAppS(() => window.Spectr.loadDefaultId());
  useAppE(() => { window.Spectr.saveStore(userPatterns); }, [userPatterns]);
  useAppE(() => { window.Spectr.saveDefaultId(defaultId); }, [defaultId]);

  const fireStatus = useAppC((msg) => {
    setStatus(msg + '|' + Date.now());
    if (/SNAPSHOT ([AB]) CAPTURED/.test(msg)) {
      const slot = msg.match(/SNAPSHOT ([AB])/)[1];
      setSnapshotStatus(s => ({ ...s, [slot]: true }));
    }
  }, []);

  const applyPattern = useAppC((p) => {
    const b = bankRef.current;
    if (!b) return;
    const gains = window.Spectr.resolveGains(p, b.N);
    b.setGains(gains);
    fireStatus(`APPLIED "${p.name}"`);
  }, [fireStatus]);

  const clearAll = useAppC(() => {
    const b = bankRef.current;
    if (!b) return;
    if (b.clearGains) b.clearGains(); else b.setGains(new Array(b.N).fill(0));
    fireStatus('CLEARED GAINS');
  }, [fireStatus]);

  const resetAll = useAppC(() => {
    const b = bankRef.current;
    if (!b) return;
    if (b.resetAll) b.resetAll(); else b.reset();
    setSnapshotStatus({ A: false, B: false });
    fireStatus('RESET ALL');
  }, [fireStatus]);

  const [allMuted, setAllMuted] = useAppS(false);
  useAppE(() => {
    const iv = setInterval(() => {
      const b = bankRef.current;
      if (!b || !b.allMuted) return;
      const v = b.allMuted();
      setAllMuted(prev => prev === v ? prev : v);
    }, 250);
    return () => clearInterval(iv);
  }, []);

  const currentGains = useAppC(() => {
    const b = bankRef.current;
    return b ? b.getGains() : new Array(settings.bandCount).fill(0);
  }, [settings.bandCount]);

  // Load default pattern once on mount
  useAppE(() => {
    const t = setTimeout(() => {
      const b = bankRef.current;
      if (!b || !defaultId) return;
      const all = [...window.Spectr.FACTORY_PATTERNS, ...userPatterns];
      const p = all.find(x => x.id === defaultId);
      if (p && p.id !== 'factory:flat') {
        const gains = window.Spectr.resolveGains(p, b.N);
        b.setGains(gains);
      }
    }, 80);
    return () => clearTimeout(t);
  }, []);

  useAppE(() => {
    const iv = setInterval(() => {
      const b = bankRef.current;
      if (!b) return;
      const v = b.view;
      const full = Math.log10(20000) - Math.log10(20);
      const span = v.lmax - v.lmin;
      setInfo({ N: b.N, zoom: (full / span).toFixed(2) });
    }, 150);
    return () => clearInterval(iv);
  }, []);

  // Global keyboard shortcuts for edit modes + analyzer
  useAppE(() => {
    const modeKeys = {
      's': 'sculpt', 'l': 'level', 'b': 'boost', 'f': 'flare', 'g': 'glide'
    };
    const onKey = (e) => {
      // ignore if typing in a text field
      const t = e.target;
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
      if (e.metaKey || e.ctrlKey || e.altKey) return;
      const k = e.key.toLowerCase();
      if (modeKeys[k]) {
        e.preventDefault();
        setEditMode(modeKeys[k]);
        fireStatus(`EDIT → ${modeKeys[k].toUpperCase()}`);
        return;
      }
      if (k === 'a') {
        e.preventDefault();
        setAnalyzerMode(m => {
          const next = m === 'peak' ? 'avg' : m === 'avg' ? 'both' : m === 'both' ? 'off' : 'peak';
          fireStatus(`ANALYZER → ${next.toUpperCase()}`);
          return next;
        });
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [fireStatus]);

  return (
    <div style={{ position: 'absolute', top: 0, left: 0, width: 1320, height: 860, background: '#0a0e14' }} data-screen-label="Spectr main">
      <FilterBank settings={settings} sharedState={bankRef} onStatus={fireStatus} dspMode={dspMode} editMode={editMode} analyzerMode={analyzerMode} onEditModeChange={(v) => { setEditMode(v); fireStatus(`EDIT → ${v.toUpperCase()}`); }} />
      <Chrome
        settings={settings}
        setSettings={setSettings}
        bankRef={bankRef}
        info={info}
        status={status}
        dspMode={dspMode}
        setDspMode={(v) => { setDspMode(v); fireStatus(`DSP → ${v.toUpperCase()}`); }}
        editMode={editMode}
        setEditMode={(v) => { setEditMode(v); fireStatus(`EDIT → ${v.toUpperCase()}`); }}
        analyzerMode={analyzerMode}
        setAnalyzerMode={(v) => { setAnalyzerMode(v); fireStatus(`ANALYZER → ${v.toUpperCase()}`); }}
        snapshotStatus={snapshotStatus}
        patterns={{ user: userPatterns, defaultId }}
        onApplyPattern={applyPattern}
        onOpenPatternManager={() => setManagerOpen(true)}
        onClearAll={clearAll}
        onResetAll={resetAll}
        allMuted={allMuted}
      />
      <TweaksPanel settings={settings} setSettings={setSettings} />
      <PatternManager
        open={managerOpen}
        onClose={() => setManagerOpen(false)}
        userPatterns={userPatterns}
        setUserPatterns={setUserPatterns}
        defaultId={defaultId}
        setDefaultId={setDefaultId}
        N={info.N}
        onApply={applyPattern}
        currentGains={currentGains()}
        onStatus={fireStatus}
      />
    </div>
  );
}

ReactDOM.createRoot(document.getElementById('root')).render(<App />);


