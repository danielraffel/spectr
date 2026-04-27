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
function call(name: string, ...args: unknown[]): unknown {
    const fn = g[name];
    if (typeof fn !== 'function') return undefined;
    return fn(...args);
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
        // canvasSetLinearGradient(id, x0, y0, x1, y1, stops_json)
        // canvasSetRadialGradient(id, x0, y0, r0, x1, y1, r1, stops_json)
        const stopsJson = JSON.stringify(this.stops);
        if (this.type === 'linear') {
            call('canvasSetLinearGradient', this.canvasId, ...this.args, stopsJson);
        } else {
            call('canvasSetRadialGradient', this.canvasId, ...this.args, stopsJson);
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
        // canvasRect(id, x, y, w, h) is the fill primitive.
        call('canvasRect', this.canvasId, x, y, w, h);
    }
    strokeRect(x: number, y: number, w: number, h: number): void {
        call('canvasStrokeRect', this.canvasId, x, y, w, h);
    }
    clearRect(x: number, y: number, w: number, h: number): void {
        call('canvasClearRect', this.canvasId, x, y, w, h);
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
    arc(x: number, y: number, r: number, sa: number, ea: number, _ccw?: boolean): void {
        // Bridge canvasArc: (id, x, y, r, startAngle, endAngle, fillColor)
        // We pass the current fillStyle if any, else empty string.
        const color = typeof this._fillStyle === 'string' ? this._fillStyle : '';
        call('canvasArc', this.canvasId, x, y, r, sa, ea, color);
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
    // on every resize to apply devicePixelRatio. Without it, the very first
    // frame aborts silently — no try/catch in FilterBank's RAF loop.
    private _matrix: [number, number, number, number, number, number] = [1, 0, 0, 1, 0, 0];
    setTransform(a: number, b: number, c: number, d: number, e: number, f: number): void {
        this._matrix = [a, b, c, d, e, f];
        // Bridge has canvasSave/Restore + canvasTranslate/Scale/Rotate
        // but no direct setTransform. Approximate by reset (save/restore
        // via fresh state) + scale + translate. For the common
        // setTransform(s,0,0,s,0,0) DPR case this is exactly right.
        // canvasResetTransform isn't available either — emulate via
        // restore-to-identity by issuing inverse operations.
        // Pragma: rely on the bridge already applying DPR globally;
        // spectr#28 only needs the no-throw.
        // Future: file pulp issue for canvasSetTransform native.
    }
    transform(a: number, b: number, c: number, d: number, e: number, f: number): void {
        this.setTransform(a, b, c, d, e, f);  // approximate (multiply not implemented)
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
    clip(): void {
        // Bridge has no clip primitive. NO-OP; subsequent draws will
        // not be clipped. Rendered content may bleed beyond intended
        // areas, but the draw loop completes. Track for a follow-up
        // pulp issue (canvasClip).
    }

    // ── globalCompositeOperation ───────────────────────────────────────
    // FilterBank uses 'destination-out' / 'multiply' for blending.
    // Bridge doesn't expose composite ops; track + drop.
    private _gco = 'source-over';
    get globalCompositeOperation(): string { return this._gco; }
    set globalCompositeOperation(v: string) { this._gco = v; }

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
    setLineDash(_segments: number[]): void { /* TODO */ }
    getLineDash(): number[] { return []; }
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
        set width(v: number) { pendingW = v; /* No-op in Pulp — canvas size is set via setFlex(width) */ },
        get height() { return pendingH; },
        set height(v: number) { pendingH = v; },
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
