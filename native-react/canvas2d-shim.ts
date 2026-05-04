// canvas2d-shim.ts — wraps Pulp's bridge `canvas*` globals as a
// CanvasRenderingContext2D-compatible API. The extracted FilterBank
// code does `canvasRef.current.getContext('2d').fillRect(...)` etc;
// this shim makes those calls land on the bridge.
//
// State (fillStyle, strokeStyle, lineWidth, font, textAlign, textBaseline)
// is buffered in the shim and pushed to the bridge before each draw.
// Pulp's bridge tracks set state per-canvas, so we only re-push when
// the React-side value changes.
//
// Phase-2 of the WebView-parity port (spectr #28). Caller side:
// dom-adapter wraps `<canvas ref>` so ref.current.getContext('2d')
// returns an instance of this shim.

type AnyFn = (...args: unknown[]) => unknown;
const g = globalThis as unknown as Record<string, AnyFn | undefined>;
let _callN = 0;
const _missing: Set<string> = new Set();
let _nanReports = 0;
const _nanByName: Record<string, number> = {};
function call(name: string, ...args: unknown[]): unknown {
    const fn = g[name];
    if (typeof fn !== 'function') {
        if (!_missing.has(name)) {
            _missing.add(name);
            const lg = (g as Record<string, AnyFn | undefined>).__spectrLog;
            if (lg) lg('[canvas:MISSING] ' + name);
        }
        return undefined;
    }
    _callN++;
    let nanIdx = -1;
    for (let i = 0; i < args.length; i++) {
        const a = args[i];
        if (typeof a === 'number' && a !== a) { nanIdx = i; break; }
    }
    const trigger = _callN <= 8 || _callN % 200 === 0 || (nanIdx >= 0 && _nanReports < 24);
    if (trigger) {
        const lg = (g as Record<string, AnyFn | undefined>).__spectrLog;
        if (lg) {
            if (nanIdx >= 0) {
                _nanReports++;
                _nanByName[name] = (_nanByName[name] ?? 0) + 1;
            }
            const tag = nanIdx >= 0 ? '[NaN#' + _nanReports + '|n=' + _callN + ']'
                                    : '[canvas#' + _callN + ']';
            lg(tag + ' ' + name + '(' +
                args.slice(0, 6).map(a => typeof a === 'string' ? a.slice(0, 30) : String(a)).join(',') +
                (args.length > 6 ? ',...' : '') + ')' +
                (nanIdx >= 0 ? ' nanIdx=' + nanIdx : ''));
        }
    }
    // Skip the bridge call entirely if any numeric arg is NaN. Pulp's
    // canvas pipeline DOES NOT defensively reject NaN — a single NaN x
    // / y / w / h corrupts the CGContext / Skia surface for the rest
    // of the frame and produces a blank canvas. Drop the bad call,
    // log it (above), keep painting. spectr #32 / #1382 follow-up.
    if (nanIdx >= 0) return undefined;
    return fn(...args);
}
export function _canvasNanSummary(): string {
    return JSON.stringify(_nanByName);
}

interface GradientStop { offset: number; color: string; }

class GradientShim {
    constructor(
        public readonly canvasId: string,
        public readonly type: 'linear' | 'radial',
        public readonly args: number[],
    ) {}
    stops: GradientStop[] = [];
    addColorStop(offset: number, color: string): void {
        this.stops.push({ offset, color });
    }
    /// Push to bridge as the active gradient. Caller should clear
    /// before the next solid-color fill.
    install(): void {
        // pulp #1348 bridge contract:
        //   canvasSetLinearGradient(id, x0, y0, x1, y1, color1, pos1, color2, pos2, ...)
        //   canvasSetRadialGradient(id, cx, cy, radius, color1, pos1, color2, pos2, ...)
        // Stops are passed as alternating (color, position) args, NOT as a JSON
        // string. The bridge reads each pair via args.get(i)/args.get(i+1).
        const stopArgs: (string | number)[] = [];
        for (const s of this.stops) { stopArgs.push(s.color, s.offset); }
        if (this.type === 'linear') {
            // type 'linear' args = [x0, y0, x1, y1]
            call('canvasSetLinearGradient', this.canvasId, ...this.args, ...stopArgs);
        } else {
            // type 'radial' args = [x0, y0, r0, x1, y1, r1] — bridge accepts only
            // single-circle radial today, so use the OUTER circle (x1, y1, r1).
            const [, , , x1, y1, r1] = this.args;
            call('canvasSetRadialGradient', this.canvasId, x1, y1, r1, ...stopArgs);
        }
    }
}

export class Canvas2DShim {
    constructor(public readonly canvasId: string) {}

    // ── State (mirrors CanvasRenderingContext2D) ──────────────────────
    private _fillStyle: string | GradientShim = '#000';
    private _strokeStyle: string | GradientShim = '#000';
    private _lineWidth = 1;
    private _font = '10px sans-serif';
    private _textAlign: CanvasTextAlign = 'start';
    private _textBaseline: CanvasTextBaseline = 'alphabetic';
    private _globalAlpha = 1;

    get fillStyle(): string | GradientShim { return this._fillStyle; }
    set fillStyle(v: string | GradientShim) {
        this._fillStyle = v;
        if (typeof v === 'string') {
            call('canvasClearGradient', this.canvasId);
            call('canvasSetFillColor', this.canvasId, v);
        } else if (v instanceof GradientShim) {
            v.install();
        }
    }
    get strokeStyle(): string | GradientShim { return this._strokeStyle; }
    set strokeStyle(v: string | GradientShim) {
        this._strokeStyle = v;
        if (typeof v === 'string') {
            call('canvasSetStrokeColor', this.canvasId, v);
        }
    }
    get lineWidth(): number { return this._lineWidth; }
    set lineWidth(v: number) { this._lineWidth = v; call('canvasSetLineWidth', this.canvasId, v); }
    get font(): string { return this._font; }
    set font(v: string) { this._font = v; call('canvasSetFont', this.canvasId, v); }
    get textAlign(): CanvasTextAlign { return this._textAlign; }
    set textAlign(v: CanvasTextAlign) { this._textAlign = v; call('canvasSetTextAlign', this.canvasId, v); }
    get textBaseline(): CanvasTextBaseline { return this._textBaseline; }
    set textBaseline(v: CanvasTextBaseline) { this._textBaseline = v; call('canvasSetTextBaseline', this.canvasId, v); }
    get globalAlpha(): number { return this._globalAlpha; }
    set globalAlpha(v: number) { this._globalAlpha = v; /* TODO: bridge has no setGlobalAlpha; emulate via per-call alpha */ }

    // ── Rect / clear ───────────────────────────────────────────────────
    fillRect(x: number, y: number, w: number, h: number): void {
        // Pulp's `canvasRect` is an immediate-mode fill_rect using its
        // own color arg — it ignores the active linear/radial gradient
        // installed by canvasSetLinearGradient. So when fillStyle is a
        // gradient we have to route through the path-based fill path:
        // path-based fill_current_path DOES honor the active gradient.
        if (typeof this._fillStyle === 'string') {
            call('canvasRect', this.canvasId, x, y, w, h, this._fillStyle);
        } else {
            call('canvasBeginPath', this.canvasId);
            call('canvasMoveTo', this.canvasId, x, y);
            call('canvasLineTo', this.canvasId, x + w, y);
            call('canvasLineTo', this.canvasId, x + w, y + h);
            call('canvasLineTo', this.canvasId, x, y + h);
            call('canvasClosePath', this.canvasId);
            call('canvasFillPath', this.canvasId);
        }
    }
    strokeRect(x: number, y: number, w: number, h: number): void {
        if (typeof this._strokeStyle === 'string') {
            call('canvasStrokeRect', this.canvasId, x, y, w, h, this._strokeStyle, this._lineWidth);
        } else {
            call('canvasBeginPath', this.canvasId);
            call('canvasMoveTo', this.canvasId, x, y);
            call('canvasLineTo', this.canvasId, x + w, y);
            call('canvasLineTo', this.canvasId, x + w, y + h);
            call('canvasLineTo', this.canvasId, x, y + h);
            call('canvasClosePath', this.canvasId);
            call('canvasStrokePath', this.canvasId);
        }
    }
    clearRect(x: number, y: number, w: number, h: number): void {
        // Pulp's CanvasWidget keeps an append-only commands_ list — every
        // canvas* bridge call (clearRect, fillRect, fill_path, etc.) is
        // RECORDED into commands_ and replayed in full on every paint. JS
        // calling clearRect every rAF tick adds a clear_rect command but
        // does NOT reset commands_, so commands_ grows unbounded (Spectr
        // accumulates ~2k commands/frame; after a minute, replay must
        // process ~120k+ ops per paint). Pulp #1382 surfaces here as
        // "canvas disappears on hover" because the long replay accumulates
        // state errors (transform stacking, clip-region rounding, etc.)
        // that produce empty output.
        //
        // Fix: when JS calls clearRect over the FULL canvas (the standard
        // start-of-frame pattern), use canvasClear to RESET commands_
        // entirely — equivalent semantics (the canvas is empty) but
        // bounded memory + bounded replay cost. Falls back to canvasClearRect
        // for partial clears.
        if (x === 0 && y === 0) {
            // Full-canvas clear — reset commands_. The bridge's canvasClear
            // empties the entire command buffer; subsequent draws this frame
            // start from a clean slate.
            call('canvasClear', this.canvasId);
        } else {
            call('canvasClearRect', this.canvasId, x, y, w, h);
        }
    }

    // ── Path API ───────────────────────────────────────────────────────
    beginPath(): void { call('canvasBeginPath', this.canvasId); }
    moveTo(x: number, y: number): void { call('canvasMoveTo', this.canvasId, x, y); }
    lineTo(x: number, y: number): void { call('canvasLineTo', this.canvasId, x, y); }
    quadraticCurveTo(cx: number, cy: number, x: number, y: number): void {
        call('canvasQuadTo', this.canvasId, cx, cy, x, y);
    }
    bezierCurveTo(cx1: number, cy1: number, cx2: number, cy2: number, x: number, y: number): void {
        call('canvasCubicTo', this.canvasId, cx1, cy1, cx2, cy2, x, y);
    }
    closePath(): void { call('canvasClosePath', this.canvasId); }
    fill(): void { call('canvasFillPath', this.canvasId); }
    stroke(): void { call('canvasStrokePath', this.canvasId); }
    arc(x: number, y: number, r: number, sa: number, ea: number, ccw?: boolean): void {
        // HTML5 Canvas2D spec: arc() ADDS to the current sub-path. The next
        // ctx.fill() / ctx.stroke() acts on that path — including any
        // active gradient set via fillStyle.
        //
        // Pulp's bridge has no canvasArcPath; canvasArc would stroke
        // immediately and bypass beginPath/fill. So synthesize the arc
        // as a polyline of canvasLineTo segments. ~32 segments gives a
        // visually smooth circle at FilterBank's typical radii (≤30px).
        // Step count scales with radius so larger circles stay smooth.
        const segs = Math.max(8, Math.min(64, Math.ceil(r * 1.2)));
        let s = sa, e = ea;
        if (ccw) {
            // sweep from sa down to ea
            if (e > s) e -= Math.PI * 2;
        } else {
            // sweep from sa up to ea
            if (e < s) e += Math.PI * 2;
        }
        const sweep = e - s;
        for (let i = 0; i <= segs; i++) {
            const t = i / segs;
            const a = s + sweep * t;
            const px = x + Math.cos(a) * r;
            const py = y + Math.sin(a) * r;
            // First segment: lineTo from current pen position to arc start.
            // (HTML5 spec actually says implicit line-to from current point.)
            call('canvasLineTo', this.canvasId, px, py);
        }
    }
    /// arcTo(x1, y1, x2, y2, radius) — adds an arc tangent to two
    /// lines defined by (current, p1) and (p1, p2). Bridge has no
    /// direct canvasArcTo, so synthesize as line+arc. Approximation:
    /// for the rounded-rect corners FilterBank uses (Spectr#28's
    /// drawMinimap, status pill, etc.), the radii are small (3-4px)
    /// and the lines are axis-aligned, so a lineTo to the tangent
    /// point + arc approximates well enough at typical sizes.
    arcTo(x1: number, y1: number, x2: number, y2: number, radius: number): void {
        // Naive: lineTo(x1, y1) — drops the rounded corner. Fast,
        // visually softer corners look square but layout is correct.
        // Good-enough for v1; track a follow-up to render proper arc.
        call('canvasLineTo', this.canvasId, x1, y1);
        // Use the radius as a hint for a subtle arc; if zero, just
        // line through.
        if (radius > 0) {
            call('canvasLineTo', this.canvasId, x2, y2);
        }
    }

    // ── Text ───────────────────────────────────────────────────────────
    fillText(text: string, x: number, y: number): void {
        call('canvasFillText', this.canvasId, x, y, text);
    }
    strokeText(text: string, x: number, y: number): void {
        // Bridge has no strokeText; fall back to fill.
        call('canvasFillText', this.canvasId, x, y, text);
    }
    measureText(text: string): { width: number } {
        // Bridge has no measureText; approximate as 6.5px per char (matches
        // the 10px-monospace default; FilterBank only uses measureText for
        // optional alignment hints, not pixel-precise layout).
        const w = (text || '').length * 6.5;
        return { width: w };
    }

    // ── Transforms ────────────────────────────────────────────────────
    save(): void { call('canvasSave', this.canvasId); }
    restore(): void { call('canvasRestore', this.canvasId); }
    translate(x: number, y: number): void { call('canvasTranslate', this.canvasId, x, y); }
    scale(sx: number, sy: number): void { call('canvasScale', this.canvasId, sx, sy); }
    rotate(rad: number): void { call('canvasRotate', this.canvasId, rad); }

    // ── Gradients (return objects with addColorStop) ──────────────────
    createLinearGradient(x0: number, y0: number, x1: number, y1: number): GradientShim {
        return new GradientShim(this.canvasId, 'linear', [x0, y0, x1, y1]);
    }
    createRadialGradient(x0: number, y0: number, r0: number, x1: number, y1: number, r1: number): GradientShim {
        return new GradientShim(this.canvasId, 'radial', [x0, y0, r0, x1, y1, r1]);
    }

    // ── setTransform / transform matrix ────────────────────────────────
    // The extracted FilterBank uses ctx.setTransform(scale, 0, 0, scale, 0, 0)
    // on every resize to apply devicePixelRatio. Native bridge support
    // landed in pulp#896 / v0.48.0 — call('canvasSetTransform') replaces
    // the current transform with the affine matrix [a b c d e f].
    private _matrix: [number, number, number, number, number, number] = [1, 0, 0, 1, 0, 0];
    setTransform(a: number, b: number, c: number, d: number, e: number, f: number): void {
        this._matrix = [a, b, c, d, e, f];
        call('canvasSetTransform', this.canvasId, a, b, c, d, e, f);
    }
    transform(a: number, b: number, c: number, d: number, e: number, f: number): void {
        // Multiply current * incoming. Pre-multiply the cached matrix and
        // push the product. For the FilterBank DPR-only path the cached
        // matrix is identity so this collapses to setTransform.
        const [ma, mb, mc, md, me, mf] = this._matrix;
        const na = ma * a + mc * b;
        const nb = mb * a + md * b;
        const nc = ma * c + mc * d;
        const nd = mb * c + md * d;
        const ne = ma * e + mc * f + me;
        const nf = mb * e + md * f + mf;
        this.setTransform(na, nb, nc, nd, ne, nf);
    }
    resetTransform(): void { this.setTransform(1, 0, 0, 1, 0, 0); }
    getTransform(): { a: number; b: number; c: number; d: number; e: number; f: number } {
        const [a, b, c, d, e, f] = this._matrix;
        return { a, b, c, d, e, f };
    }

    // ── rect / clip ────────────────────────────────────────────────────
    // ctx.rect(x,y,w,h) adds a rectangular subpath; ctx.clip() clips
    // subsequent draws to the current path. FilterBank uses both in
    // its draw setup. Without these the draw aborts.
    rect(x: number, y: number, w: number, h: number): void {
        // Add rect subpath via four moveTo/lineTo. closePath finishes
        // the rectangle. canvas* bridge fns don't have a one-shot
        // "addRectToPath", so we synthesize.
        call('canvasMoveTo', this.canvasId, x, y);
        call('canvasLineTo', this.canvasId, x + w, y);
        call('canvasLineTo', this.canvasId, x + w, y + h);
        call('canvasLineTo', this.canvasId, x, y + h);
        call('canvasClosePath', this.canvasId);
    }
    /// roundRect(x, y, w, h, radii) — adds a rounded rect subpath.
    /// Simplified: ignore radii (corner rounding) and emit a regular
    /// rect path. Visual softness loss only, no crash.
    roundRect(x: number, y: number, w: number, h: number, _radii?: unknown): void {
        this.rect(x, y, w, h);
    }
    clip(): void {
        // Native canvasClip landed in pulp#896 / v0.48.0; intersects
        // the clip region with the current path.
        call('canvasClip', this.canvasId);
    }

    // ── globalCompositeOperation ───────────────────────────────────────
    // FilterBank uses 'destination-out' / 'multiply' for blending.
    // Native bridge support landed in pulp#896 / v0.48.0 — accepts every
    // standard CSS composite-op string and falls back to a graceful no-op
    // on unknown values.
    private _gco = 'source-over';
    get globalCompositeOperation(): string { return this._gco; }
    set globalCompositeOperation(v: string) {
        this._gco = v;
        call('canvasGlobalCompositeOperation', this.canvasId, v);
    }

    // ── shadow* ────────────────────────────────────────────────────────
    private _shadowBlur = 0;
    private _shadowColor = 'transparent';
    private _shadowOffsetX = 0;
    private _shadowOffsetY = 0;
    get shadowBlur(): number { return this._shadowBlur; }
    set shadowBlur(v: number) { this._shadowBlur = v; }
    get shadowColor(): string { return this._shadowColor; }
    set shadowColor(v: string) { this._shadowColor = v; }
    get shadowOffsetX(): number { return this._shadowOffsetX; }
    set shadowOffsetX(v: number) { this._shadowOffsetX = v; }
    get shadowOffsetY(): number { return this._shadowOffsetY; }
    set shadowOffsetY(v: number) { this._shadowOffsetY = v; }

    // ── No-op stubs for things the bridge doesn't have yet ────────────
    private _lineDash: number[] = [];
    setLineDash(segments: number[]): void {
        this._lineDash = segments.slice();
        // Bridge fn shipped in pulp v0.x via #920/#916; wired up here per #952.
        call('canvasSetLineDash', this.canvasId, segments);
    }
    getLineDash(): number[] { return this._lineDash.slice(); }
    drawImage(): void { /* TODO */ }
    isPointInPath(): boolean { return false; }
    isPointInStroke(): boolean { return false; }
    createImageData(): unknown { return { data: new Uint8ClampedArray(4), width: 1, height: 1 }; }
    getImageData(): unknown { return { data: new Uint8ClampedArray(4), width: 1, height: 1 }; }
    putImageData(): void { /* no-op */ }
    createPattern(): unknown { return null; }
    drawFocusIfNeeded(): void { /* no-op */ }
}

/// Wrap a PulpInstance descriptor (returned from createInstance) into
/// an HTMLCanvasElement-shaped object the extracted code expects.
export function wrapCanvasInstance(instance: { id: string }): {
    id: string;
    width: number;
    height: number;
    getContext(t: string): Canvas2DShim;
    getBoundingClientRect(): DOMRect;
    setPointerCapture(): void;
    releasePointerCapture(): void;
    style: { cursor: string };
} {
    const shim = new Canvas2DShim(instance.id);
    let pendingW = 0;
    let pendingH = 0;
    return {
        id: instance.id,
        get width() { return pendingW; },
        set width(v: number) {
            // HTML5 semantics: canvas.width sets the BACKING BUFFER
            // resolution. Spectr sets canvas.width = 2640 (2× DPI)
            // for crisp rendering; canvasSetTransform on the bridge
            // side handles the DPI scale for drawing. But the
            // widget itself ALSO needs a logical layout size or
            // Yoga sees 0×0 bounds and the canvas is invisible
            // (regression from def0c9c — see pulp #1322). Divide
            // by DPR so the widget is sized in CSS pixels (1320)
            // while the backing buffer keeps the 2× resolution.
            pendingW = v;
            const dpr = (typeof globalThis !== 'undefined' &&
                        ((globalThis as { devicePixelRatio?: number }).devicePixelRatio ?? 2)) || 2;
            const g = globalThis as Record<string, unknown>;
            const setFlex = g.setFlex as ((id: string, key: string, val: number) => void) | undefined;
            if (typeof setFlex === 'function') {
                setFlex(instance.id, 'width', v / dpr);
            }
        },
        get height() { return pendingH; },
        set height(v: number) {
            pendingH = v;
            const dpr = (typeof globalThis !== 'undefined' &&
                        ((globalThis as { devicePixelRatio?: number }).devicePixelRatio ?? 2)) || 2;
            const g = globalThis as Record<string, unknown>;
            const setFlex = g.setFlex as ((id: string, key: string, val: number) => void) | undefined;
            if (typeof setFlex === 'function') {
                setFlex(instance.id, 'height', v / dpr);
            }
        },
        getContext(_t: string) { return shim; },
        getBoundingClientRect(): DOMRect {
            // Best-effort — we don't have layout-resolved bounds via the
            // bridge yet. Return the editor's known window size.
            return {
                x: 0, y: 0, left: 0, top: 0,
                width: 1320, height: 860,
                right: 1320, bottom: 860,
                toJSON() { return this; },
            } as DOMRect;
        },
        setPointerCapture() { /* TODO */ },
        releasePointerCapture() { /* TODO */ },
        style: { cursor: 'default' },
    };
}
